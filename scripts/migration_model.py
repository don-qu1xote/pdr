"""Разбор миграций: имена файлов, контрольные суммы, таблицы и колонки.

Этим модулем пользуются и линтер (scripts/check_migrations.py), и генератор
документа схемы (scripts/gen_schema_doc.py), и применялка (scripts/migrate.py) —
чтобы «что такое колонка» понималось одинаково во всех трёх местах.

Разбирается ТО ПОДМНОЖЕСТВО DDL, которое мы себе разрешаем: create table с
обычными именами, колонки, табличные ограничения, индексы, включение RLS и
политики. Всё, чего разбор не понял, он называет вслух и роняет проверку —
молча пропускать непонятое нельзя, иначе правила «tenant_id на каждой таблице»
и «RLS на каждой таблице» перестают что-либо значить ровно в тот день, когда
кто-то напишет непривычный DDL.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass, replace
from pathlib import Path

FILE_NAME = re.compile(r"^V(\d{3,})__([a-z0-9_]+)\.sql$")
CREATE_TABLE = re.compile(r"\bcreate\s+table\s+(?:if\s+not\s+exists\s+)?([^\s(]+)\s*\(", re.I)
COMMENT_ON_TABLE = re.compile(
    r"comment\s+on\s+table\s+([a-z_][a-z0-9_]*)\s+is\s+'((?:[^']|'')*)'", re.I
)

DOLLAR_TAG = re.compile(r"\$([A-Za-z_][A-Za-z0-9_]*)?\$")

ALTER_ROW_SECURITY = re.compile(
    r"\balter\s+table\s+(?:if\s+exists\s+)?(?:only\s+)?([a-z_][a-z0-9_]*)\s+"
    r"(enable|disable|force|no\s+force)\s+row\s+level\s+security\b",
    re.I,
)
ALTER_TABLE_ANY = re.compile(r"\balter\s+table\b", re.I)

ALTER_TABLE_ADD = re.compile(
    r"\balter\s+table\s+(?:if\s+exists\s+)?([a-z_][a-z0-9_]*)\s+add\s+"
    r"(?:(column)\s+(?:if\s+not\s+exists\s+)?|(?=constraint\s))",
    re.I,
)

CREATE_POLICY = re.compile(
    r"\bcreate\s+policy\s+([a-z_][a-z0-9_]*)\s+on\s+(?:only\s+)?([a-z_][a-z0-9_]*)\b", re.I
)
CREATE_INDEX = re.compile(
    r"\bcreate\s+(unique\s+)?index\s+(?:if\s+not\s+exists\s+)?([a-z_][a-z0-9_]*)\s+"
    r"on\s+(?:only\s+)?([a-z_][a-z0-9_]*)\b",
    re.I,
)

COMPOUND_TYPES = (
    "timestamp with time zone",
    "timestamp without time zone",
    "time with time zone",
    "time without time zone",
    "double precision",
    "character varying",
)

CONSTRAINT_STARTS = ("constraint", "primary", "unique", "foreign", "check", "exclude", "like")


META_TABLES = {
    "schema_version": "реестр применённых миграций",
    "jobs_lock": "распределённая блокировка периодических заданий, одна на кластер",
    "jobs_run": "журнал последнего прогона задания, один на кластер",
    "identity_account": "один человек на всю площадку: отпечаток почты и идентификатор (ADR-0019)",
    "identity_signup_attempt": "счётчик самостоятельных заведений с одного адреса, до всякого арендатора",
}

META_TABLE_COLUMNS = {
    "schema_version": {"version", "applied_at", "checksum"},
    "jobs_lock": {"key", "owner", "expiration_time"},
    "jobs_run": {
        "job",
        "attempt_at",
        "started_at",
        "finished_at",
        "duration_ms",
        "outcome",
        "produced",
        "repeated",
        "runs",
    },
    "identity_account": {
        "id",
        "email_digest",
        "confirmed_at",
        "confirmation_digest",
        "confirmation_expires_at",
        "created_at",
    },
    "identity_signup_attempt": {"address_hash", "window_started_at", "attempts"},
}


"""META_TABLE_COLUMNS — что мета-таблице разрешено хранить.

Список закрыт, и он же проверяет правило «общего числа готовности вообще не
существует нигде»: учебные данные пересекают границу арендатора только через
новую колонку в таблице без построчной защиты, а новая колонка там не заводится
молча — её ловит scripts/check_rls.py.
"""


class MigrationError(Exception):
    """Разбор не удался. Это отказ, а не предупреждение."""


@dataclass(frozen=True)
class Column:
    name: str
    type: str
    definition: str
    line: int


@dataclass(frozen=True)
class Table:
    name: str
    columns: tuple[Column, ...]
    constraints: tuple[str, ...]
    line: int
    comment: str = ""


@dataclass(frozen=True)
class Alteration:
    """Одна поздняя правка таблицы: добавленная колонка либо ограничение.

    Правки живут отдельно от `Table` намеренно: `create table` отвечает за день
    рождения таблицы, а «как она выглядит сейчас» — это создание плюс все
    правки, и складывает их `merged_tables`.
    """

    table: str
    column: Column | None
    constraint: str
    line: int


@dataclass(frozen=True)
class Policy:
    """Политика RLS: чьи строки таблица показывает."""

    name: str
    table: str
    body: str
    line: int


@dataclass(frozen=True)
class RowSecurity:
    """Одно изменение построчной защиты: enable / force / disable / no force."""

    table: str
    action: str
    line: int


@dataclass(frozen=True)
class Index:
    name: str
    table: str
    unique: bool
    body: str
    line: int


@dataclass(frozen=True)
class Migration:
    path: Path
    version: int
    name: str
    sql: str
    checksum: str
    tables: tuple[Table, ...]
    alterations: tuple[Alteration, ...] = ()
    policies: tuple[Policy, ...] = ()
    row_security: tuple[RowSecurity, ...] = ()
    indexes: tuple[Index, ...] = ()

    @property
    def file_name(self) -> str:
        return self.path.name


def checksum_of(path: Path) -> str:
    """sha256 содержимого файла — ровно то, что покажет sha256sum.

    Считается от байтов: правка пробела тоже меняет сумму, и это не придирка.
    Применённую миграцию не редактируют вовсе.
    """
    return hashlib.sha256(path.read_bytes()).hexdigest()


def strip_comments(text: str) -> str:
    """Убрать комментарии и строковые литералы, сохранив номера строк."""
    out: list[str] = []
    index = 0
    size = len(text)
    while index < size:
        symbol = text[index]
        following = text[index + 1] if index + 1 < size else ""

        if symbol == "-" and following == "-":
            while index < size and text[index] != "\n":
                out.append(" ")
                index += 1
            continue

        if symbol == "/" and following == "*":
            out.append("  ")
            index += 2
            while index + 1 < size and not (text[index] == "*" and text[index + 1] == "/"):
                out.append("\n" if text[index] == "\n" else " ")
                index += 1
            out.append("  ")
            index = min(index + 2, size)
            continue

        if symbol == "$":
            opening = DOLLAR_TAG.match(text, index)
            if opening:
                delimiter = opening.group(0)
                closing = text.find(delimiter, opening.end())
                end = size if closing < 0 else closing + len(delimiter)
                out.append("".join("\n" if s == "\n" else " " for s in text[index:end]))
                index = end
                continue

        if symbol == "'":
            out.append(" ")
            index += 1
            while index < size:
                if text[index] == "'" and text[index + 1 : index + 2] == "'":
                    out.append("  ")
                    index += 2
                    continue
                closing = text[index] == "'"
                out.append("\n" if text[index] == "\n" else " ")
                index += 1
                if closing:
                    break
            continue

        out.append(symbol)
        index += 1

    return "".join(out)


def _split_top_level(body: str) -> list[tuple[int, str]]:
    """Разбить тело create table по запятым верхнего уровня.

    Возвращает (смещение начала, текст). Скобки внутри — типы вроде char(3) и
    выражения check(...) — уровень учитывается.
    """
    items: list[tuple[int, str]] = []
    depth = 0
    start = 0
    for position, symbol in enumerate(body):
        if symbol == "(":
            depth += 1
        elif symbol == ")":
            depth -= 1
        elif symbol == "," and depth == 0:
            items.append((start, body[start:position]))
            start = position + 1
    items.append((start, body[start:]))
    return [(offset, text) for offset, text in items if text.strip()]


def _type_of(definition: str) -> str:
    """Тип колонки из её определения."""
    lowered = " ".join(definition.split()).lower()
    for compound in COMPOUND_TYPES:
        if lowered.startswith(compound):
            return compound
    match = re.match(r"([a-z_][a-z0-9_]*)\s*(\([^)]*\))?", lowered)
    if not match:
        return ""
    return match.group(1) + (match.group(2).replace(" ", "") if match.group(2) else "")


def parse_tables(sql: str, source: str) -> tuple[Table, ...]:
    """Таблицы, заводимые этим текстом."""
    text = strip_comments(sql)
    comments = {
        match.group(1): match.group(2).replace("''", "'")
        for match in COMMENT_ON_TABLE.finditer(sql)
    }
    tables: list[Table] = []

    for match in CREATE_TABLE.finditer(text):
        raw_name = match.group(1).strip('"')
        if not re.fullmatch(r"[a-z_][a-z0-9_]*", raw_name):
            raise MigrationError(
                f"{source}: имя таблицы «{raw_name}» разбор не понял. "
                f"Имена таблиц — строчными буквами с подчёркиваниями."
            )

        depth = 1
        position = match.end()
        while position < len(text) and depth:
            if text[position] == "(":
                depth += 1
            elif text[position] == ")":
                depth -= 1
            position += 1
        if depth:
            raise MigrationError(f"{source}: у create table {raw_name} не закрыта скобка")

        body = text[match.end() : position - 1]
        line = text[: match.start()].count("\n") + 1

        columns: list[Column] = []
        constraints: list[str] = []
        for offset, item in _split_top_level(body):
            cleaned = " ".join(item.split())
            first = cleaned.split(" ", 1)[0].lower()
            lead = len(item) - len(item.lstrip())
            item_line = text[: match.end() + offset + lead].count("\n") + 1
            if first in CONSTRAINT_STARTS:
                constraints.append(cleaned)
                continue
            name, _, rest = cleaned.partition(" ")
            column_type = _type_of(rest)
            if not column_type:
                raise MigrationError(
                    f"{source}:{item_line}: у колонки «{name}» не разобран тип: {cleaned}"
                )
            columns.append(
                Column(name=name.strip('"'), type=column_type, definition=cleaned, line=item_line)
            )

        tables.append(
            Table(
                name=raw_name,
                columns=tuple(columns),
                constraints=tuple(constraints),
                line=line,
                comment=comments.get(raw_name, ""),
            )
        )

    return tuple(tables)


def parse_alterations(sql: str, source: str) -> tuple[Alteration, ...]:
    """Поздние правки таблиц: `alter table ... add column` и `... add constraint`.

    Понимается ровно добавление и больше ничего: переименование, смена типа и
    удаление колонки — это правки, после которых старые записи читаются
    по-новому, и разрешать их разбору, который их не понимает, нельзя. Всё
    остальное `unsupported()` объявляет непонятым и роняет проверку.
    """
    text = strip_comments(sql)
    found: list[Alteration] = []

    for match in ALTER_TABLE_ADD.finditer(text):
        line = text[: match.start()].count("\n") + 1
        tail = " ".join(_statement_tail(text, match.end()).split())
        if not tail:
            raise MigrationError(f"{source}:{line}: у alter table нечего добавлять")

        if match.group(2) is None:
            found.append(
                Alteration(table=match.group(1), column=None, constraint=tail, line=line)
            )
            continue

        name, _, rest = tail.partition(" ")
        column_type = _type_of(rest)
        if not column_type:
            raise MigrationError(
                f"{source}:{line}: у добавляемой колонки «{name}» не разобран тип: {tail}"
            )
        found.append(
            Alteration(
                table=match.group(1),
                column=Column(name=name.strip('"'), type=column_type, definition=tail, line=line),
                constraint="",
                line=line,
            )
        )

    return tuple(found)


def merged_tables(migrations: "list[Migration]") -> dict[str, Table]:
    """Таблицы со всеми поздними правками — «как они выглядят сейчас».

    Единственный правильный способ спросить о составе таблицы: `create table`
    отвечает только за день её рождения, а колонка, добавленная третьей
    миграцией, для линтера и для docs/architecture/schema.md существует так же,
    как и все остальные.
    """
    tables: dict[str, Table] = {}
    for migration in migrations:
        for table in migration.tables:
            tables[table.name] = table
    for migration in migrations:
        for change in migration.alterations:
            table = tables.get(change.table)
            if table is None:
                continue
            if change.column is not None:
                tables[change.table] = replace(table, columns=table.columns + (change.column,))
            else:
                tables[change.table] = replace(
                    table, constraints=table.constraints + (change.constraint,)
                )
    return tables


def _statement_tail(text: str, start: int) -> str:
    """Хвост оператора от позиции до точки с запятой верхнего уровня."""
    depth = 0
    for position in range(start, len(text)):
        symbol = text[position]
        if symbol == "(":
            depth += 1
        elif symbol == ")":
            depth -= 1
        elif symbol == ";" and depth == 0:
            return text[start:position]
    return text[start:]


def parse_policies(sql: str) -> tuple[Policy, ...]:
    """Политики RLS, заводимые этим текстом.

    Ищутся в очищенном тексте (иначе `create policy` из комментария сошёл бы за
    настоящую), а тело берётся из ИСХОДНОГО по тем же смещениям: очистка
    посимвольная и длину не меняет. Тело нужно целиком, вместе со строковым
    литералом: имя параметра сессии — это и есть то, что проверяет
    scripts/check_rls.py.
    """
    text = strip_comments(sql)
    return tuple(
        Policy(
            name=match.group(1),
            table=match.group(2),
            body=" ".join(_statement_tail(sql, match.end()).split()),
            line=text[: match.start()].count("\n") + 1,
        )
        for match in CREATE_POLICY.finditer(text)
    )


def parse_row_security(sql: str) -> tuple[RowSecurity, ...]:
    """Включения и выключения построчной защиты."""
    text = strip_comments(sql)
    return tuple(
        RowSecurity(
            table=match.group(1),
            action=" ".join(match.group(2).split()).lower(),
            line=text[: match.start()].count("\n") + 1,
        )
        for match in ALTER_ROW_SECURITY.finditer(text)
    )


def parse_indexes(sql: str) -> tuple[Index, ...]:
    """Индексы, заводимые этим текстом."""
    text = strip_comments(sql)
    return tuple(
        Index(
            name=match.group(2),
            table=match.group(3),
            unique=bool(match.group(1)),
            body=" ".join(_statement_tail(sql, match.end()).split()),
            line=text[: match.start()].count("\n") + 1,
        )
        for match in CREATE_INDEX.finditer(text)
    )


UNSUPPORTED = (
    (re.compile(r"\bdrop\s+table\b", re.I), "drop table"),
    (re.compile(r"\bcreate\s+table\s+[^\s(]+\s+as\b", re.I), "create table as"),
    (re.compile(r"\b(?:drop|alter)\s+policy\b", re.I), "drop/alter policy"),
    (re.compile(r"\bdrop\s+index\b", re.I), "drop index"),
    (re.compile(r"\bcreate\s+(?:unique\s+)?index\s+concurrently\b", re.I),
     "create index concurrently"),
)


def _teach_me(source: str, line: int, name: str) -> str:
    return (
        f"{source}:{line}: «{name}» разбор миграций пока не умеет. Допишите "
        f"scripts/migration_model.py вместе с первой такой миграцией — "
        f"иначе линтер и docs/architecture/schema.md начнут врать"
    )


def unsupported(sql: str, source: str) -> list[str]:
    """Конструкции, которые разбор пока не понимает."""
    text = strip_comments(sql)
    found = []
    for pattern, name in UNSUPPORTED:
        for match in pattern.finditer(text):
            found.append(_teach_me(source, text[: match.start()].count("\n") + 1, name))

    understood = {match.start() for match in ALTER_ROW_SECURITY.finditer(text)}
    understood |= {match.start() for match in ALTER_TABLE_ADD.finditer(text)}
    for match in ALTER_TABLE_ANY.finditer(text):
        if match.start() in understood:
            continue
        found.append(
            _teach_me(
                source,
                text[: match.start()].count("\n") + 1,
                "alter table в любой форме, кроме row level security",
            )
        )

    return sorted(found, key=lambda line: int(line.split(":")[1]))


def load(directory: Path) -> list[Migration]:
    """Все миграции каталога по возрастанию версии."""
    if not directory.is_dir():
        return []

    migrations: list[Migration] = []
    seen: dict[int, str] = {}

    for path in sorted(directory.iterdir()):
        if path.is_dir() or path.suffix != ".sql":
            continue
        match = FILE_NAME.match(path.name)
        if not match:
            raise MigrationError(
                f"{path.name}: имя не по правилу V001__короткое_имя.sql"
            )
        version = int(match.group(1))
        if version in seen:
            raise MigrationError(
                f"{path.name}: версия {version} уже занята файлом {seen[version]}. "
                f"Номер выдаётся один раз."
            )
        seen[version] = path.name

        sql = path.read_text(encoding="utf-8")
        migrations.append(
            Migration(
                path=path,
                version=version,
                name=match.group(2),
                sql=sql,
                checksum=checksum_of(path),
                tables=parse_tables(sql, path.name),
                alterations=parse_alterations(sql, path.name),
                policies=parse_policies(sql),
                row_security=parse_row_security(sql),
                indexes=parse_indexes(sql),
            )
        )

    migrations.sort(key=lambda migration: migration.version)
    return migrations
