"""Разбор миграций: имена файлов, контрольные суммы, таблицы и колонки.

Этим модулем пользуются и линтер (scripts/check_migrations.py), и генератор
документа схемы (scripts/gen_schema_doc.py), и применялка (scripts/migrate.py) —
чтобы «что такое колонка» понималось одинаково во всех трёх местах.

Разбирается ТОТ ПОДМНОЖЕСТВО DDL, которое мы себе разрешаем: create table с
обычными именами, колонки, табличные ограничения. Всё, чего разбор не понял, он
называет вслух и роняет проверку — молча пропускать непонятое нельзя, иначе
правило «tenant_id на каждой таблице» перестаёт что-либо значить ровно в тот
день, когда кто-то напишет непривычный DDL.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from pathlib import Path

FILE_NAME = re.compile(r"^V(\d{3,})__([a-z0-9_]+)\.sql$")
CREATE_TABLE = re.compile(r"\bcreate\s+table\s+(?:if\s+not\s+exists\s+)?([^\s(]+)\s*\(", re.I)
COMMENT_ON_TABLE = re.compile(
    r"comment\s+on\s+table\s+([a-z_][a-z0-9_]*)\s+is\s+'((?:[^']|'')*)'", re.I
)

# Типы из нескольких слов: разбирать их как «первое слово» нельзя, иначе
# «timestamp without time zone» окажется просто «timestamp».
COMPOUND_TYPES = (
    "timestamp with time zone",
    "timestamp without time zone",
    "time with time zone",
    "time without time zone",
    "double precision",
    "character varying",
)

# Слова, с которых начинается ТАБЛИЧНОЕ ограничение, а не колонка.
CONSTRAINT_STARTS = ("constraint", "primary", "unique", "foreign", "check", "exclude", "like")


# Таблицы самого механизма миграций. У них нет контекста-владельца и нет
# tenant_id, потому что они не про предметную область. Список закрытый и общий
# для линтера миграций и для сверки владения: новая строка здесь требует
# причины, а не «ну это же служебная».
META_TABLES = {
    "schema_version": "реестр применённых миграций",
}


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
class Migration:
    path: Path
    version: int
    name: str
    sql: str
    checksum: str
    tables: tuple[Table, ...]

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
    # Подписи берём из исходного текста: в очищенном строковые литералы стёрты.
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
            # Номер строки — по первому непробельному символу элемента: перенос
            # перед именем колонки принадлежит элементу и сдвинул бы отсчёт.
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


# Разбор понимает create table. Всё, что меняет уже заведённую таблицу, он
# понимать обязан ДО того, как такая миграция появится: иначе и линтер, и
# документ схемы начнут тихо врать.
UNSUPPORTED = (
    (re.compile(r"\balter\s+table\b", re.I), "alter table"),
    (re.compile(r"\bdrop\s+table\b", re.I), "drop table"),
    (re.compile(r"\bcreate\s+table\s+[^\s(]+\s+as\b", re.I), "create table as"),
)


def unsupported(sql: str, source: str) -> list[str]:
    """Конструкции, которые разбор пока не понимает."""
    text = strip_comments(sql)
    found = []
    for pattern, name in UNSUPPORTED:
        match = pattern.search(text)
        if match:
            line = text[: match.start()].count("\n") + 1
            found.append(
                f"{source}:{line}: «{name}» разбор миграций пока не умеет. Допишите "
                f"scripts/migration_model.py вместе с первой такой миграцией — "
                f"иначе линтер и docs/architecture/schema.md начнут врать"
            )
    return found


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
            )
        )

    migrations.sort(key=lambda migration: migration.version)
    return migrations
