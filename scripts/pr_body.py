#!/usr/bin/env python3
"""Тело запроса на слияние: три раздела из шести собираются сами (PDR-DOC-11).

Сливать фазами мы будем всегда, а фаза — это десятки коммитов и сотни файлов.
Тело такого запроса, написанное руками целиком, расходится с деревом на второй
день: проверки переименовали, долг закрыли, задачу добавили — а в запросе
по-прежнему прошлая неделя. Поэтому здесь собирается всё, что можно собрать:

    (в) что проверено машиной — проверки берутся из цели `test` Makefile и
        гоняются НА САМОМ ДЕЛЕ; в тело попадает их вывод, а не пересказ;
    (д) долги — вывод scripts/check_debts.py как есть;
    (е) список задач — из git log по префиксам коммитов.

Руками пишутся только «что здесь», «чего здесь нет намеренно» и «как проверить у
себя»: это то, чего в дереве нет и быть не может.

СПИСОК ПРОВЕРОК НЕ ДУБЛИРУЕТСЯ. Он читается из Makefile, а не переписывается
сюда: два списка расходятся молча, и запрос начинает обещать проверки, которых
уже нет.

Запуск:
    python3 scripts/pr_body.py --phase 0 --name "Фундамент" > /tmp/pr.md
    python3 scripts/pr_body.py --phase 0 --name "Фундамент" --quick
    python3 scripts/pr_body.py --selftest

`--quick` не гоняет проверки, а называет их списком: годится, чтобы посмотреть
форму тела, и не годится, чтобы отправить запрос.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path
from typing import Sequence

MAKEFILE = Path("Makefile")
DEBTS = ("python3", "scripts/check_debts.py")

TARGET = re.compile(r"^test:\s*$", re.M)
CHECK_LINE = re.compile(r"^\t(python3|node)\s+(scripts/[\w.-]+)(?:\s+(--\S+))?\s*$")
TASK = re.compile(r"^\[(PDR-[A-Z]+-\d+)(?:\s+fix-\d+)?\]\s*(.+)$")

SECTIONS = (
    "Что здесь",
    "Чего здесь нет намеренно",
    "Что проверено машиной",
    "Как проверить у себя",
    "Долги",
    "Список задач",
)

WRITTEN_BY_HAND = {
    "Что здесь": "<!-- Одно предложение о том, что появилось в дереве. -->",
    "Чего здесь нет намеренно": "<!-- Граница фазы своими словами. -->",
    "Как проверить у себя": "<!-- Команды целиком, копируемые одним куском. -->",
}


def checks_from(makefile: str) -> list[tuple[str, ...]]:
    """Проверки цели `test`, в том порядке, в каком их гоняет сборка.

    Самопроверки (`--selftest`) в тело не попадают: они проверяют проверку, а
    читающему запрос нужен ответ про дерево. Гонять их всё равно будет `make
    test`, и это его работа, а не этой сборки текста.
    """
    found = TARGET.search(makefile)
    if not found:
        return []

    commands: list[tuple[str, ...]] = []
    for line in makefile[found.end():].splitlines():
        if line and not line.startswith("\t"):
            break
        parsed = CHECK_LINE.match(line)
        if not parsed:
            continue
        runner, script, flag = parsed.groups()
        if flag == "--selftest":
            continue
        commands.append((runner, script) if flag is None else (runner, script, flag))
    return commands


def run(command: Sequence[str], root: Path) -> tuple[bool, str]:
    """Прогон и его последняя содержательная строка."""
    result = subprocess.run(list(command), cwd=root, capture_output=True, text=True, check=False)
    output = (result.stdout + result.stderr).strip().splitlines()
    said = next((line.strip() for line in reversed(output) if line.strip()), "")
    return result.returncode == 0, said


def machine_checked(root: Path, quick: bool) -> list[str]:
    makefile = (root / MAKEFILE).read_text(encoding="utf-8") if (root / MAKEFILE).is_file() else ""
    commands = checks_from(makefile)
    if not commands:
        return ["**Проверок не нашлось.** Цель `test` в Makefile не разобралась — "
                "собирать нечего, и это само по себе повод не сливать."]

    if quick:
        return [f"* `{' '.join(command)}` — не гонялась (`--quick`)" for command in commands]

    lines = []
    for command in commands:
        passed, said = run(command, root)
        mark = "" if passed else " **ПРОВАЛ**"
        lines.append(f"* `{' '.join(command)}`{mark} — {said or 'ничего не сказала'}")
    return lines


def debts(root: Path, quick: bool) -> list[str]:
    if quick:
        return [f"* `{' '.join(DEBTS)}` — не гонялась (`--quick`)"]

    passed, said = run(DEBTS, root)
    mark = "" if passed else " **ПРОВАЛ**"
    return [f"{said or 'ничего не сказала'}{mark}"]


def tasks(root: Path, base: str) -> list[str]:
    """Задачи запроса — из git log, по одной строке на идентификатор.

    Правки (`fix-NN`) не заводят новой строки: правка — это та же задача,
    доделанная (CONTRIBUTING, «Правка к уже сделанной задаче»).
    """
    result = subprocess.run(
        ["git", "log", "--format=%s", "--reverse", f"{base}..HEAD"],
        cwd=root, capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        return [f"**Список задач не собрался:** {result.stderr.strip()}"]

    seen: dict[str, str] = {}
    for subject in result.stdout.splitlines():
        parsed = TASK.match(subject.strip())
        if parsed and parsed.group(1) not in seen:
            seen[parsed.group(1)] = parsed.group(2).strip()

    if not seen:
        return [f"**Задач в диапазоне `{base}..HEAD` не нашлось.** Либо ветка пуста, "
                f"либо в коммитах нет идентификаторов."]
    return [f"* `{name}` — {subject}" for name, subject in seen.items()]


def body(root: Path, phase: str, name: str, base: str, quick: bool) -> str:
    found = tasks(root, base)
    counted = sum(1 for line in found if line.startswith("* `"))

    parts = [f"# Фаза {phase}: {name} ({counted} задач)", ""]
    for section in SECTIONS:
        parts.append(f"## {section}")
        parts.append("")
        if section in WRITTEN_BY_HAND:
            parts.append(WRITTEN_BY_HAND[section])
        elif section == "Что проверено машиной":
            parts.extend(machine_checked(root, quick))
        elif section == "Долги":
            parts.extend(debts(root, quick))
        else:
            parts.extend(found)
        parts.append("")
    return "\n".join(parts).rstrip() + "\n"


SELFTEST_MAKEFILE = """
test:
\tcmake --build $(BUILD_DIR) --parallel
\tpython3 scripts/check_layers.py --selftest
\tpython3 scripts/check_layers.py
\tnode scripts/check-copy.mjs
\tpython3 scripts/gen_schema_doc.py --check

fmt:
\tpython3 scripts/check_format.py
"""


def selftest() -> int:
    """Разбор Makefile и разбор коммитов — на своих отрицательных случаях."""
    commands = checks_from(SELFTEST_MAKEFILE)
    expected = [
        ("python3", "scripts/check_layers.py"),
        ("node", "scripts/check-copy.mjs"),
        ("python3", "scripts/gen_schema_doc.py", "--check"),
    ]
    if commands != expected:
        print(f"самопроверка: разобрано {commands} вместо {expected}", file=sys.stderr)
        return 1

    if checks_from("all:\n\techo нет цели test\n"):
        print("самопроверка: проверки нашлись там, где цели test нет", file=sys.stderr)
        return 1

    cases = {
        "[PDR-DOC-11] Правила запроса на слияние": ("PDR-DOC-11", "Правила запроса на слияние"),
        "[PDR-DB-03 fix-01] Дождаться сессию": ("PDR-DB-03", "Дождаться сессию"),
    }
    for subject, want in cases.items():
        parsed = TASK.match(subject)
        if not parsed or (parsed.group(1), parsed.group(2)) != want:
            print(f"самопроверка: «{subject}» разобрано не в {want}", file=sys.stderr)
            return 1

    if TASK.match("Правки по ревью"):
        print("самопроверка: коммит без идентификатора сошёл за задачу", file=sys.stderr)
        return 1

    if len(SECTIONS) != 6:
        print(f"самопроверка: разделов {len(SECTIONS)} вместо шести", file=sys.stderr)
        return 1

    print("Самопроверка пройдена: разбор цели test, разбор коммитов и число разделов сходятся.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Тело запроса на слияние.")
    parser.add_argument("--phase", default="N", help="номер фазы")
    parser.add_argument("--name", default="короткое имя", help="короткое имя фазы")
    parser.add_argument("--base", default="origin/main", help="с чем сравнивать: <база>..HEAD")
    parser.add_argument("--root", type=Path, default=root, help="корень дерева")
    parser.add_argument("--quick", action="store_true", help="не гонять проверки, назвать списком")
    parser.add_argument("--out", type=Path, help="куда записать; по умолчанию в вывод")
    parser.add_argument("--selftest", action="store_true", help="проверить саму сборку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    text = body(arguments.root, arguments.phase, arguments.name, arguments.base, arguments.quick)
    if arguments.out:
        arguments.out.write_text(text, encoding="utf-8")
        print(f"собрано: {arguments.out}")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
