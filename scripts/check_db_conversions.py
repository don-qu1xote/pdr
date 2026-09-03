#!/usr/bin/env python3
"""Перевод доменного типа в тип базы пишется ОДИН РАЗ, а не в каждом вызове.

Отображение живёт в io-трейтах userver (`libs/pdr-core/src/infrastructure/db/
domain_types.hpp`, PDR-DB-04): идентификатор уезжает в колонку `uuid`, момент —
в `timestamptz`, и делает это драйвер. Значит, в аргументах запроса не должно
остаться ни `ToString()`, ни `AsTimestamptz(...)`.

ПОЧЕМУ ЭТО ПРАВИЛО, А НЕ ВКУС. Ручной перевод на каждом вызове — сорок мест, в
которых можно ошибиться по одному. Пропуск не ловится компилятором: `ToString()`
у идентификатора есть всегда, и забытый (или лишний) вызов даёт не ошибку
сборки, а ДРУГОЙ ТИП В БАЗЕ — текст вместо uuid, строку вместо момента. Такое
находится на проде, а не на ревью.

ЧТО РАЗБИРАЕТСЯ. Аргументы вызовов `Execute`, `ExecuteBulk`,
`ExecuteDecomposeBulk` и `ExecuteDecompose` в слое infrastructure и в процессах:
от открывающей скобки до парной ей, с учётом вложенности. Тесты пропускаются —
там ручной перевод бывает предметом проверки.

ИСКЛЮЧЕНИЯ НАЗВАНЫ ПОИМЁННО, и их одно. Список в ALLOWED, у каждой строки
причина; безымянных исключений не бывает.

Запуск:
    python3 scripts/check_db_conversions.py
    python3 scripts/check_db_conversions.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

SOURCE_SUFFIXES = frozenset({".cpp", ".hpp"})
SKIPPED_DIRS = frozenset({".git", "out", "_deps", "__pycache__", "compile_fail", "tests"})
SKIPPED_PREFIXES = ("build", "venv", ".venv")

ROOTS = ("libs", "services")

CALL = re.compile(r"\b(Execute|ExecuteBulk|ExecuteDecompose|ExecuteDecomposeBulk)\s*\(")

FORBIDDEN = (
    (".ToString()", "идентификатор переводится в текст руками; отображение уже есть"),
    ("AsTimestamptz(", "момент переводится руками; отображение уже есть"),
)

ALLOWED = {
    "libs/pdr-core/src/infrastructure/db/tenant_context.cpp": (
        "set_config принимает ТЕКСТ: это параметр сессии, а не колонка типа uuid"
    ),
}


def source_files(root: Path) -> Iterator[Path]:
    for top in ROOTS:
        directory = root / top
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            parts = path.relative_to(root).parts
            if any(part in SKIPPED_DIRS or part.startswith(".") for part in parts):
                continue
            if any(part.startswith(SKIPPED_PREFIXES) for part in parts):
                continue
            yield path


def arguments(text: str, opening: int) -> tuple[str, int]:
    """Текст аргументов вызова и позиция за закрывающей скобкой."""
    depth = 0
    for position in range(opening, len(text)):
        symbol = text[position]
        if symbol == "(":
            depth += 1
        elif symbol == ")":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : position], position + 1
    return text[opening + 1 :], len(text)


def check(root: Path) -> tuple[list[str], int]:
    violations: list[str] = []
    checked = 0

    for path in source_files(root):
        display = str(path.relative_to(root))
        text = path.read_text(encoding="utf-8", errors="replace")

        position = 0
        while True:
            found = CALL.search(text, position)
            if found is None:
                break
            body, position = arguments(text, found.end() - 1)
            checked += 1

            for needle, reason in FORBIDDEN:
                if needle not in body:
                    continue
                if display in ALLOWED:
                    continue
                line = text[: found.start()].count("\n") + 1
                violations.append(
                    f"{display}:{line}: в аргументах {found.group(1)} остался «{needle}»: "
                    f"{reason} (libs/pdr-core/src/infrastructure/db/domain_types.hpp)"
                )

    for name, reason in sorted(ALLOWED.items()):
        if not checked:
            break
        if not (root / name).is_file():
            violations.append(
                f"{name}: исключение названо, а файла нет. Причина «{reason}» больше "
                f"ни к чему не относится — уберите строку из ALLOWED"
            )

    return violations, checked


SELFTEST_FILES = {
    "libs/pdr-alpha/src/alpha/infrastructure/bad.cpp": (
        "void Save(const Session& session) {\n"
        "    scope.Session().Execute(sql::kSave, session.Tenant().ToString());\n"
        "}\n"
    ),
    "libs/pdr-alpha/src/alpha/infrastructure/late.cpp": (
        "void Note(core::Instant at) {\n"
        "    access.Execute(sql::kNote, AsTimestamptz(at));\n"
        "}\n"
    ),
    "libs/pdr-alpha/src/alpha/infrastructure/good.cpp": (
        "void Save(const Session& session) {\n"
        "    scope.Session().Execute(sql::kSave, session.Tenant(), session.CreatedAt());\n"
        "}\n"
    ),
    "libs/pdr-alpha/src/alpha/infrastructure/elsewhere.cpp": (
        "std::string Shown(const core::TenantId& tenant) { return tenant.ToString(); }\n"
    ),
    "libs/pdr-alpha/tests/manual_test.cpp": (
        "TEST(X, Y) { session.Execute(kSave, tenant.ToString()); }\n"
    ),
    "libs/pdr-core/src/infrastructure/db/tenant_context.cpp": (
        "void Open() { transaction.Execute(sql::kDeclareTenant, tenant.ToString()); }\n"
    ),
}

SELFTEST_EXPECTED = (
    ("libs/pdr-alpha/src/alpha/infrastructure/bad.cpp", "ToString()"),
    ("libs/pdr-alpha/src/alpha/infrastructure/late.cpp", "AsTimestamptz("),
)

SELFTEST_CLEAN = (
    "libs/pdr-alpha/src/alpha/infrastructure/good.cpp",
    "libs/pdr-alpha/src/alpha/infrastructure/elsewhere.cpp",
    "libs/pdr-alpha/tests/manual_test.cpp",
    "libs/pdr-core/src/infrastructure/db/tenant_context.cpp",
)


def selftest() -> int:
    """Отрицательные случаи: ловится ровно то, ради чего правило написано."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        empty, checked = check(root)
        if empty or checked:
            print("самопроверка: пустое дерево объявлено нарушением", file=sys.stderr)
            return 1

        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, _checked = check(root)
        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» в {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: правильный вызов объявлен нарушением: {name}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Перевод в тип базы пишется один раз.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments_ = parser.parse_args(argv)

    if arguments_.selftest:
        return selftest()

    violations, checked = check(arguments_.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Отображение доменных типов на типы Postgres — "
              f"libs/pdr-core/src/infrastructure/db/domain_types.hpp", file=sys.stderr)
        return 1

    print(f"Обращений к базе разобрано: {checked}. Перевод в тип базы написан один раз, "
          f"названных исключений: {len(ALLOWED)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
