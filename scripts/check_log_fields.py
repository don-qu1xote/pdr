#!/usr/bin/env python3
"""Журнал полями, а не текстом (PDR-OBS-06).

Запись `LOG_INFO() << "стало " << value` читается человеком и не ищется машиной:
значение вклеено в текст, и найти по нему нельзя ничего. В час разбора это
превращается в grep по русским строкам — по тем самым, которые правят чаще
всего.

Проверяется четыре вещи:

* ЗНАЧЕНИЕ НЕ ВКЛЕИВАЕТСЯ В ТЕКСТ. В `LOG_*` попадают только строковые литералы
  и `LogExtra`; всё остальное — нарушение. Отступить можно, написав в той же
  строке `// журнал-ok: причина`, — как в scripts/check_testsuite.py, чтобы
  отступления находились одним grep;
* ИМЯ ПОЛЯ НЕ ПИШЕТСЯ ЛИТЕРАЛОМ. Ключи `LogExtra` и `Span::AddTag` берутся из
  констант log_fields.hpp. Литерал на месте ключа — это второе имя того же поля,
  и обнаруживается оно в тот день, когда по первому ничего не находится;
* РЕЕСТР И КОД СХОДЯТСЯ В ОБЕ СТОРОНЫ. Константа без записи в
  configs/log-fields.yaml — поле, о котором знает только его автор; запись без
  константы — обещание реестра, которого код не выполняет;
* ПЕРЕЧЕНЬ ПДн ДЕЙСТВУЕТ И ЗДЕСЬ. Имя поля вида `email`, `phone`, `password`,
  `birth_date`, `transcript` — отказ. Проверяются ИМЕНА, а не значения: значение
  бывает любым, а имя — это решение, и принимается оно один раз. Значения
  сторожит набор контура (services/scheduling/tests/test_journal.py), которому
  видны настоящие записи поднятого процесса.

ЧЕГО ЭТА ПРОВЕРКА НЕ ДЕЛАЕТ. Она не читает журнал и не знает, что в поле легло.
Имя `job_key` разрешено, и положить в него почту она не помешает — это работа
набора контура. Разделение намеренное: здесь ловится то, что видно в исходнике,
там — то, что видно только на живом процессе.

Запуск:
    python3 scripts/check_log_fields.py
    python3 scripts/check_log_fields.py --selftest
    python3 scripts/check_log_fields.py --forbidden
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

import yaml

REGISTRY = Path("configs/log-fields.yaml")
CONSTANTS = Path("libs/pdr-core/src/infrastructure/observe/log_fields.hpp")
ALERTS = Path("libs/pdr-core/src/infrastructure/observe/service_alerts.cpp")
OBSERVABILITY = Path("docs/architecture/observability.md")

SEARCHED_ROOTS = ("libs", "services")
SOURCE_SUFFIXES = frozenset({".hpp", ".cpp"})
SKIPPED = frozenset({"build", "out", "_deps", "__pycache__", ".git"})

REQUIRED = ("description", "type", "set_in")
TYPES = ("id", "name", "count", "ms", "flag", "text")

NUMERIC = frozenset({"count", "ms", "flag"})
"""Типы, которым перечень ПДн не писан.

Не послабление, а другой случай: в число не кладут ни почту, ни имя, ни текст
сообщения. `secrets_checked` — это СКОЛЬКО секретов проверено, и запрещать его
за слово «secret» значило бы запрещать слово, а не данные."""

WAIVER = re.compile(r"//\s*журнал-ok:\s*(\S.*)$")

LOG_CALL = re.compile(r"\bLOG_(?:INFO|WARNING|ERROR|DEBUG|TRACE|CRITICAL)(?:_TO)?\s*\(")
BUILDER = re.compile(r"(?:userver::)?logging::LogExtra\s+(\w+)\s*\(", re.M)
"""Помощник, СОБИРАЮЩИЙ поля: объявлен в том же файле и возвращает `LogExtra`.

Без этого пришлось бы либо запретить помощников вовсе — и повторять один и тот
же набор полей в четырёх местах, — либо узнавать их по имени, то есть по
договорённости. Возвращаемый тип договорённостью не является."""
CONSTANT = re.compile(r"^inline const std::string (k\w+Field) = \"([a-z][a-z0-9_]*)\";$", re.M)
LITERAL_KEY = re.compile(r"(?:AddTag\w*|Extend)\s*\(\s*\"")
LITERAL_PAIR = re.compile(r"LogExtra\s*\{\s*\{*\s*\"")
ALERT_NAME = re.compile(r'return "([a-z][a-z0-9_]*)";')

FORBIDDEN: tuple[tuple[str, str], ...] = (
    (r"(^|_)e?mails?(_|$)", "почта — «учётные данные» перечня"),
    (r"(^|_)phones?(_|$)|msisdn", "телефон"),
    (r"passw|(^|_)hash(_|$)", "пароль или его отпечаток"),
    (r"(^|_)(full|first|last|middle|display|user|person|child|guardian|tutor|student)"
     r"_?names?(_|$)", "имя человека"),
    (r"(^|_)(birth|born|dob|birthday|birthdate)\w*", "дата рождения"),
    (r"(^|_)(message|chat)s?(_|$)", "переписка"),
    (r"transcript|recording|(^|_)audio(_|$)", "запись занятия и расшифровка"),
    (r"(^|_)photos?(_|$)", "фотография работы"),
    (r"(^|_)(card|pan|cvv|cvc)(_|$)", "данные карты — их у нас нет вовсе"),
    (r"(^|_)(receipt|payment)_?(number|amount|sum)(_|$)", "платежи и чеки"),
    (r"(^|_)(cookie|authorization|bearer|secret|token|credential)s?(_|$)", "удостоверение"),
)
"""Имена, которых у поля журнала не бывает.

Список закрытый и выведен из docs/legal/personal-data.md по одной категории на
строку. Он не «эвристика на подозрительные слова»: каждая строка отвечает
категории перечня, у которой человеку обещан свой срок хранения.
"""


class RegistryError(Exception):
    """Разбор реестра не удался. Это отказ, а не предупреждение."""


def sources(root: Path):
    for name in SEARCHED_ROOTS:
        base = root / name
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            if any(part in SKIPPED for part in path.relative_to(root).parts):
                continue
            yield path.relative_to(root)


def masked(text: str) -> str:
    """Строковые литералы становятся пустыми, комментарии — пробелами.

    Номера строк сохраняются: без этого нарушение указывало бы не на ту строку,
    а проверка, врущая про место, хуже отсутствующей.
    """
    out: list[str] = []
    index = 0
    size = len(text)
    while index < size:
        symbol = text[index]
        if symbol == '"':
            out.append('""')
            index += 1
            while index < size and text[index] != '"':
                if text[index] == "\\":
                    index += 1
                if index < size and text[index] == "\n":
                    out.append("\n")
                index += 1
            index += 1
            continue
        if text.startswith("//", index):
            while index < size and text[index] != "\n":
                out.append(" ")
                index += 1
            continue
        if text.startswith("/*", index):
            while index < size and not text.startswith("*/", index):
                out.append("\n" if text[index] == "\n" else " ")
                index += 1
            out.append("  ")
            index += 2
            continue
        out.append(symbol)
        index += 1
    return "".join(out)


def statement_at(text: str, start: int) -> tuple[str, int]:
    """Всё выражение от `LOG_*(` до закрывающей его точки с запятой."""
    depth = 0
    index = start
    while index < len(text):
        symbol = text[index]
        if symbol in "([{":
            depth += 1
        elif symbol in ")]}":
            depth -= 1
        elif symbol == ";" and depth <= 0:
            return text[start:index], index
        index += 1
    return text[start:], len(text)


def check_concatenation(root: Path) -> list[str]:
    """В LOG_* попадают литералы и LogExtra, всё прочее — вклеенное значение."""
    violations: list[str] = []

    for path in sources(root):
        original = (root / path).read_text(encoding="utf-8", errors="replace")
        lines = original.splitlines()
        text = masked(original)

        for found in LOG_CALL.finditer(text):
            body, end = statement_at(text, found.end())
            opened = text.count("\n", 0, found.start())

            builders = set(BUILDER.findall(text))

            for piece in body.split("<<")[1:]:
                offset = body.index(piece)
                where = opened + body.count("\n", 0, offset)
                operand = piece.strip()
                if not operand or "LogExtra" in operand:
                    continue
                if re.fullmatch(r'(?:""\s*)+', operand):
                    continue
                called = re.match(r"(\w+)\s*\(", operand)
                if called is not None and called.group(1) in builders:
                    continue

                waived = any(
                    WAIVER.search(lines[line]) for line in {where, opened} if line < len(lines)
                )
                if waived:
                    continue
                violations.append(
                    f"{path}:{where + 1}: значение вклеено в текст записи. Текст остаётся "
                    f"человеческим, данные уходят в поля: "
                    f'LOG_INFO() << "что случилось" << LogExtra{{{{{{kПолеField, значение}}}}}}. '
                    f"Если отступить всё-таки нужно, напишите в этой строке "
                    f"«// журнал-ok: причина»"
                )
            del end

    return violations


def check_literal_keys(root: Path) -> list[str]:
    """Имя поля берётся из константы, а не пишется литералом на месте."""
    violations: list[str] = []

    for path in sources(root):
        if path == CONSTANTS:
            continue
        original = (root / path).read_text(encoding="utf-8", errors="replace")
        lines = original.splitlines()

        for number, line in enumerate(lines, start=1):
            if WAIVER.search(line):
                continue
            if LITERAL_KEY.search(line) or LITERAL_PAIR.search(line):
                violations.append(
                    f"{path}:{number}: имя поля написано литералом. Имена лежат в "
                    f"{CONSTANTS.name} и в {REGISTRY}: второе имя того же поля обнаруживается "
                    f"в тот день, когда по первому ничего не находится"
                )

    return violations


def parse_registry(root: Path) -> dict[str, dict]:
    path = root / REGISTRY
    if not path.is_file():
        raise RegistryError(f"{REGISTRY}: реестра имён полей нет, а правило есть")
    try:
        found = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    except yaml.YAMLError as error:
        raise RegistryError(f"{REGISTRY}: реестр не разобран как YAML ({error})") from error
    if not isinstance(found, dict) or not found:
        raise RegistryError(f"{REGISTRY}: в реестре нет ни одной записи")
    return found


def check_registry(entries: dict[str, dict]) -> list[str]:
    violations: list[str] = []

    for name, entry in sorted(entries.items()):
        if not isinstance(entry, dict):
            violations.append(f"{REGISTRY}: у поля {name} запись не разобрана")
            continue
        for required in REQUIRED:
            if not str(entry.get(required, "")).strip():
                violations.append(
                    f"{REGISTRY}: у поля {name} не заполнено «{required}». Поле, о котором "
                    f"сказали половину, ищут дважды"
                )
        kind = str(entry.get("type", "")).strip()
        if kind and kind not in TYPES:
            violations.append(
                f"{REGISTRY}: у поля {name} тип «{kind}» не из списка: {', '.join(TYPES)}"
            )

    return violations


def check_personal_data(entries: dict[str, dict]) -> list[str]:
    """Имя поля не из перечня ПДн — у всех полей, кроме числовых.

    Проверяется ИМЯ, а не значение: значение бывает любым, а имя — это решение,
    и принимается оно один раз. Значения сторожит набор контура, которому видны
    настоящие записи поднятого процесса.
    """
    violations: list[str] = []

    for name in sorted(entries):
        entry = entries[name] if isinstance(entries.get(name), dict) else {}
        if str(entry.get("type", "")).strip() in NUMERIC:
            continue
        for pattern, why in FORBIDDEN:
            if re.search(pattern, name):
                violations.append(
                    f"{REGISTRY}: поле «{name}» — это {why} (docs/legal/personal-data.md). "
                    f"Журнал живёт своим сроком и переживает обещанный человеку молча"
                )
                break

    return violations


def check_both_ways(root: Path, entries: dict[str, dict]) -> list[str]:
    path = root / CONSTANTS
    if not path.is_file():
        return [f"{CONSTANTS}: объявления имён полей нет, а реестр есть"]

    declared = {value: constant for constant, value in CONSTANT.findall(
        path.read_text(encoding="utf-8"))}

    violations: list[str] = []
    for name, constant in sorted(declared.items()):
        if name not in entries:
            violations.append(
                f"{CONSTANTS}: поле «{name}» ({constant}) объявлено, а в {REGISTRY} его нет. "
                f"О нём знает только тот, кто его завёл"
            )
    for name in sorted(entries):
        if name not in declared:
            violations.append(
                f"{REGISTRY}: поле «{name}» записано, а константы для него нет. Реестр обещает "
                f"поле, которого код не пишет"
            )

    return violations


def check_alerts(root: Path) -> list[str]:
    """Сигнал, поднимаемый сервисом, описан там, где дежурный его прочтёт."""
    alerts = root / ALERTS
    document = root / OBSERVABILITY
    if not alerts.is_file():
        return []
    if not document.is_file():
        return [f"{OBSERVABILITY}: описания сигналов нет, а сигналы есть"]

    text = document.read_text(encoding="utf-8")
    violations: list[str] = []
    for name in ALERT_NAME.findall(alerts.read_text(encoding="utf-8")):
        if f"`{name}`" not in text:
            violations.append(
                f"{ALERTS}: сигнал «{name}» поднимается, а в {OBSERVABILITY} его нет. "
                f"Красный огонёк без инструкции гасят, а не разбирают"
            )
    return violations


def check(root: Path) -> tuple[list[str], int]:
    violations = check_concatenation(root) + check_literal_keys(root) + check_alerts(root)

    try:
        entries = parse_registry(root)
    except RegistryError as error:
        return violations + [str(error)], 0

    violations.extend(check_registry(entries))
    violations.extend(check_personal_data(entries))
    violations.extend(check_both_ways(root, entries))
    return violations, len(entries)


SELFTEST_REGISTRY = """
tenant_id:
  description: арендатор
  type: id
  set_in: спан
job_name:
  description: имя задания
  type: name
  set_in: адаптер
orphan_field:
  description: поле без константы
  type: text
  set_in: нигде
half_told:
  type: text
  set_in: нигде
wrong_type:
  description: тип не из списка
  type: uuid
  set_in: нигде
user_email:
  description: почта человека
  type: text
  set_in: нигде
"""

SELFTEST_CONSTANTS = """#pragma once

#include <string>

namespace pdr::infrastructure::observe {

inline const std::string kTenantField = "tenant_id";
inline const std::string kJobNameField = "job_name";
inline const std::string kUnregisteredField = "unregistered";

}  // namespace pdr::infrastructure::observe
"""

SELFTEST_FILES = {
    "libs/pdr-x/src/x/glued.cpp": (
        '#include <userver/logging/log.hpp>\n'
        'void Glued(int count) {\n'
        '    LOG_INFO() << "заданий " << count;\n'
        '}\n'
    ),
    "libs/pdr-x/src/x/waived.cpp": (
        '#include <userver/logging/log.hpp>\n'
        'void Waived(const std::exception& why) {\n'
        '    LOG_ERROR() << "не поднимаемся: " << why.what();  // журнал-ok: процесс умрёт '
        'раньше, чем кто-нибудь прочтёт поле\n'
        '}\n'
    ),
    "libs/pdr-x/src/x/clean.cpp": (
        '#include <userver/logging/log.hpp>\n'
        'void Clean(int count) {\n'
        '    LOG_INFO() << "задания применены"\n'
        '               << userver::logging::LogExtra{{{kConfigEntriesField, count}}};\n'
        '}\n'
    ),
    "libs/pdr-x/src/x/literal.cpp": (
        '#include <userver/tracing/span.hpp>\n'
        'void Literal(userver::tracing::Span& span) {\n'
        '    span.AddTag("tenant", "abc");\n'
        '}\n'
    ),
    "libs/pdr-x/src/x/quoted.cpp": (
        '#include <userver/logging/log.hpp>\n'
        'void Quoted() {\n'
        '    LOG_INFO() << "стрелка << внутри литерала нарушением не считается";\n'
        '}\n'
    ),
}

SELFTEST_EXPECTED = (
    ("glued.cpp", "вклеено в текст"),
    ("literal.cpp", "написано литералом"),
    ("orphan_field", "константы для него нет"),
    ("half_told", "не заполнено «description»"),
    ("wrong_type", "тип «uuid» не из списка"),
    ("user_email", "почта"),
    ("unregistered", "объявлено, а в"),
)

SELFTEST_CLEAN = ("waived.cpp", "clean.cpp", "quoted.cpp", "tenant_id", "job_name")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / REGISTRY).parent.mkdir(parents=True)
        (root / REGISTRY).write_text(SELFTEST_REGISTRY, encoding="utf-8")
        (root / CONSTANTS).parent.mkdir(parents=True)
        (root / CONSTANTS).write_text(SELFTEST_CONSTANTS, encoding="utf-8")
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, fields = check(root)

        if fields != 6:
            print(f"самопроверка: разобрано {fields} полей вместо шести", file=sys.stderr)
            return 1

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» у {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for clean in SELFTEST_CLEAN:
            if any(clean in line for line in violations):
                print(f"самопроверка: правильный случай объявлен нарушением: {clean}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они есть, "
          f"и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Журнал полями, а не текстом: PDR-OBS-06.")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    parser.add_argument("--forbidden", action="store_true",
                        help="напечатать запрещённые имена полей и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    if arguments.forbidden:
        for pattern, _ in FORBIDDEN:
            print(pattern)
        return 0

    violations, fields = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Реестр имён — {REGISTRY}, устройство — "
              f"{OBSERVABILITY}", file=sys.stderr)
        return 1

    print(f"Полей журнала в реестре: {fields}. Значения уходят в поля, имена берутся из "
          f"констант, персональных данных среди них нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
