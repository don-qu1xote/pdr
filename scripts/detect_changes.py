#!/usr/bin/env python3
"""Что затронуто изменениями — и ОДНА точка форсирования (PDR-CI-01).

Выборочный CI экономит минуты на каждой правке README, но ломается предсказуемым
образом: в условие каждой джобы дописывают «или форсировано», и следующая новая
джоба про этот флаг забывает. Тогда «прогнать всё» перестаёт прогонять всё, и
узнают об этом в худший момент.

Поэтому форсирование живёт ЗДЕСЬ, на выходе скрипта, а не в условиях джоб. Джоба
смотрит на свой флаг и только на него:

    if: needs.changes.outputs.cpp == 'true'

а `cpp` уже равен `true`, когда форсировано. Условие «или force_all» в джобе
не пишется никогда — его просто негде написать.

Форсирование включается тремя способами, и все три сходятся в одном флаге:

* `--force` — кнопка «прогнать весь CI» (`workflow_dispatch` с `full: true`);
* изменения в самом CI: правка workflow, составного действия или этого скрипта
  обязана гоняться целиком, иначе выборочность проверяет сама себя;
* не удалось узнать, что изменилось (первый коммит, отсутствие базы сравнения):
  неизвестность трактуется как «затронуто всё», а не как «ничего».

Запуск:
    python3 scripts/detect_changes.py --base origin/main
    python3 scripts/detect_changes.py --files README.md
    python3 scripts/detect_changes.py --selftest
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Iterable, Sequence

AREAS: dict[str, tuple[str, ...]] = {
    "cpp": ("libs/", "services/", "CMakeLists.txt", ".clang-format", ".clang-tidy",
            ".clang-format-version"),
    "python": ("scripts/",),
    "db": ("db/", "scripts/migrate.py", "scripts/migration_model.py", "scripts/check_plans.py"),
    "deploy": ("deploy/", "Makefile"),
    "configs": ("configs/",),
    "docs": ("docs/", "README.md", "CONTRIBUTING.md"),
}

CI_PATHS = (".github/", "scripts/detect_changes.py")

FLAG = re.compile(r"needs\.changes\.outputs\.([a-z_]+)")


def changed_files(base: str | None) -> tuple[list[str], bool]:
    """Изменённые файлы и признак «узнать не удалось»."""
    if not base:
        return [], True

    command = ["git", "diff", "--name-only", f"{base}...HEAD"]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
    except FileNotFoundError:
        return [], True

    if result.returncode != 0:
        return [], True

    files = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    return files, False


def touches(files: Iterable[str], prefixes: Sequence[str]) -> bool:
    for name in files:
        path = PurePosixPath(name).as_posix()
        for prefix in prefixes:
            if prefix.endswith("/"):
                if path.startswith(prefix):
                    return True
            elif path == prefix or path.endswith("/" + prefix):
                return True
    return False


def decide(files: Sequence[str], forced: bool, unknown: bool) -> dict[str, bool]:
    """Флаги областей. Форсирование подмешано ЗДЕСЬ и только здесь."""
    force = forced or unknown or touches(files, CI_PATHS)

    decision = {"forced": force}
    for area, prefixes in AREAS.items():
        decision[area] = force or touches(files, prefixes)

    decision["docs_only"] = not force and bool(files) and all(
        touches([name], AREAS["docs"]) for name in files
    )
    return decision


def report(decision: dict[str, bool]) -> None:
    output = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{name}={'true' if value else 'false'}" for name, value in decision.items()]

    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")

    for line in lines:
        print(line)


SELFTEST_CASES = (
    (["README.md"], False, {"cpp": False, "docs": True, "docs_only": True, "forced": False}),
    (["docs/testing.md", "CONTRIBUTING.md"], False, {"cpp": False, "docs_only": True}),
    (["libs/pdr-core/src/core/money.cpp"], False, {"cpp": True, "docs_only": False}),
    (["db/migrations/V004__x.sql"], False, {"db": True, "cpp": False, "docs_only": False}),
    (["db/explain/hot_queries.sql"], False, {"db": True, "cpp": False, "docs_only": False}),
    ([".clang-format-version"], False, {"cpp": True, "docs_only": False}),
    (["deploy/docker-compose.yml"], False, {"deploy": True, "cpp": False}),
    (["configs/dynamic/registry.yaml"], False, {"configs": True, "cpp": False}),
    (["README.md"], True, {"cpp": True, "db": True, "deploy": True, "forced": True,
                           "docs_only": False}),
    ([".github/workflows/ci.yml"], False, {"cpp": True, "forced": True, "docs_only": False}),
    (["scripts/detect_changes.py"], False, {"cpp": True, "forced": True}),
)


def selftest() -> int:
    """Отрицательные случаи: выборочность обязана быть выборочной, а кнопка —
    включать всё одним флагом."""
    for files, forced, expected in SELFTEST_CASES:
        decision = decide(files, forced=forced, unknown=False)
        for name, want in expected.items():
            if decision[name] != want:
                print(f"самопроверка: {files} (форсировано={forced}): «{name}» = "
                      f"{decision[name]}, ожидалось {want}", file=sys.stderr)
                return 1

    unknown = decide([], forced=False, unknown=True)
    if not all(unknown[area] for area in AREAS):
        print("самопроверка: при неизвестной базе сравнения не всё затронуто", file=sys.stderr)
        return 1

    root = Path(__file__).resolve().parent.parent
    workflows = list((root / ".github" / "workflows").glob("*.yml"))
    known = set(AREAS) | {"docs_only", "forced"}

    for path in workflows:
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), start=1):
            if line.lstrip().startswith("if:") and "forced" in line and "||" in line:
                print(f"самопроверка: {path.name}:{number}: условие джобы смотрит на "
                      f"«forced» через «||». Форсирование подмешивается в флаг области "
                      f"в detect_changes.py, а не в условие джобы", file=sys.stderr)
                return 1

            for name in FLAG.findall(line):
                if name not in known:
                    print(f"самопроверка: {path.name}:{number}: джоба смотрит на флаг "
                          f"«{name}», которого скрипт не выдаёт. Условие всегда ложно, "
                          f"и джоба не запустится ни разу", file=sys.stderr)
                    return 1

        if "detect" in text:
            for area in known:
                if f"{area}: ${{{{ steps.detect.outputs.{area} }}}}" not in text:
                    print(f"самопроверка: {path.name}: флаг «{area}» не проброшен в "
                          f"outputs джобы changes — джоба, посмотревшая на него, "
                          f"не запустится никогда", file=sys.stderr)
                    return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_CASES)} случаев выборочности, неизвестность "
          f"трактуется как «всё», условий «или форсировано» в джобах нет, все флаги джоб "
          f"скрипт выдаёт и workflow пробрасывает.")
    return 0


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Что затронуто изменениями (PDR-CI-01).")
    parser.add_argument("--base", help="с чем сравнивать: origin/main, SHA")
    parser.add_argument("--files", nargs="*", help="список файлов вместо git diff")
    parser.add_argument("--force", action="store_true", help="прогнать всё")
    parser.add_argument("--selftest", action="store_true", help="проверить сам скрипт и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    if arguments.files is not None:
        files, unknown = arguments.files, False
    else:
        files, unknown = changed_files(arguments.base)

    if unknown:
        print("база сравнения неизвестна: считаем затронутым всё", file=sys.stderr)

    report(decide(files, forced=arguments.force, unknown=unknown))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
