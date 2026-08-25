#!/usr/bin/env python3
"""Диаграммы не врут про готовность (PDR-DOC-03).

ADR отвечают «почему», диаграммы — «как устроено». Диаграмма, на которой всё
выглядит готовым, хуже отсутствующей: по ней принимают решения о том, чего нет.

Проверяется:

* у каждой стрелки на диаграмме контейнеров есть состояние — «есть» или
  «план» — прямо в подписи, а не в чьей-то памяти;
* набор стрелок на картинке совпадает с таблицей под ней в обе стороны:
  нарисованная стрелка без строки и строка без стрелки — одинаковая ложь;
* состояние в подписи совпадает с состоянием в таблице;
* у стрелки «есть» каждая ссылка ведёт в файл, который в дереве лежит. Это и
  есть проверка задачи: стрелка соответствует либо существующему коду, либо
  явной пометке «план»;
* у стрелки «план» названа область задачи, которая её заведёт: пустая клетка
  превращает пометку в отговорку;
* оглавление каталога не разошлось с каталогом: документ, не названный в
  README, и строка README без документа роняют сборку.

Запуск:
    python3 scripts/check_diagrams.py
    python3 scripts/check_diagrams.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

ARCHITECTURE = Path("docs/architecture")
CONTAINER = ARCHITECTURE / "c4-container.md"
INDEX = ARCHITECTURE / "README.md"

EXISTS = "есть"
PLANNED = "план"
STATES = (EXISTS, PLANNED)

EDGE = re.compile(r"^\s*(\w+)\s*-->\s*\|\"(?P<label>[^\"]*)\"\|\s*(\w+)\s*$")
TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
ARROW = re.compile(r"^(\w+)\s*→\s*(\w+)$")
CODE = re.compile(r"`([^`]+)`")
LINK = re.compile(r"\[[^\]]*\]\(([^)#]+)")
FENCE = re.compile(r"^```mermaid\s*$")


def edges(text: str) -> tuple[dict[tuple[str, str], tuple[int, str]], list[str]]:
    """Стрелки диаграммы: {(откуда, куда): (строка, состояние)} и жалобы."""
    found: dict[tuple[str, str], tuple[int, str]] = {}
    problems: list[str] = []

    inside = False
    for number, line in enumerate(text.splitlines(), start=1):
        if FENCE.match(line):
            inside = True
            continue
        if inside and line.strip() == "```":
            inside = False
            continue
        if not inside or "-->" not in line:
            continue

        match = EDGE.match(line)
        if not match:
            problems.append(
                f"{CONTAINER}:{number}: стрелку «{line.strip()[:50]}» разбор не понял. "
                f"Форма одна: откуда -->|\"протокол · что передаётся · состояние\"| куда"
            )
            continue

        label = match.group("label")
        state = label.rsplit("·", 1)[-1].strip()
        if state not in STATES:
            problems.append(
                f"{CONTAINER}:{number}: у стрелки «{match.group(1)} → {match.group(3)}» "
                f"состояние «{state}» — бывает «{EXISTS}» или «{PLANNED}». Диаграмма, на "
                f"которой всё выглядит готовым, хуже отсутствующей"
            )
            continue

        key = (match.group(1), match.group(3))
        if key in found:
            problems.append(
                f"{CONTAINER}:{number}: стрелка «{key[0]} → {key[1]}» нарисована второй раз"
            )
            continue
        found[key] = (number, state)

    return found, problems


def rows(text: str) -> tuple[dict[tuple[str, str], tuple[int, str, str]], list[str]]:
    """Строки таблицы стрелок: {(откуда, куда): (строка, состояние, чем подтверждается)}."""
    found: dict[tuple[str, str], tuple[int, str, str]] = {}
    problems: list[str] = []

    inside = False
    for number, line in enumerate(text.splitlines(), start=1):
        match = TABLE_ROW.match(line.strip())
        if not match:
            inside = False
            continue
        cells = [cell.strip() for cell in match.group("cells").split("|")]
        if cells[0] == "Стрелка":
            inside = True
            continue
        if not inside:
            continue
        if all(set(cell) <= {"-", ":", " "} for cell in cells):
            continue
        if len(cells) < 5:
            problems.append(
                f"{CONTAINER}:{number}: в строке стрелки меньше пяти частей — стрелка, "
                f"протокол, что передаётся, состояние, чем подтверждается"
            )
            continue

        arrow = ARROW.match(cells[0])
        if not arrow:
            problems.append(
                f"{CONTAINER}:{number}: «{cells[0]}» не похоже на стрелку вида «откуда → куда»"
            )
            continue
        found[(arrow.group(1), arrow.group(2))] = (number, cells[3], cells[4])

    return found, problems


def check_container(root: Path) -> list[str]:
    path = root / CONTAINER
    if not path.is_file():
        return [f"{CONTAINER}: диаграммы контейнеров нет"]

    text = path.read_text(encoding="utf-8")
    drawn, violations = edges(text)
    listed, table_problems = rows(text)
    violations.extend(table_problems)

    if not drawn:
        violations.append(f"{CONTAINER}: на диаграмме не нашлось ни одной стрелки")
    if not listed:
        violations.append(f"{CONTAINER}: таблицы стрелок не нашлось")

    for key in sorted(drawn.keys() - listed.keys()):
        number, _ = drawn[key]
        violations.append(
            f"{CONTAINER}:{number}: стрелка «{key[0]} → {key[1]}» нарисована, а строки о ней "
            f"нет. Нарисованная связь без разбора — обещание, за которое никто не отвечает"
        )
    for key in sorted(listed.keys() - drawn.keys()):
        number, _, _ = listed[key]
        violations.append(
            f"{CONTAINER}:{number}: строка «{key[0]} → {key[1]}» описывает стрелку, которой на "
            f"диаграмме нет"
        )

    for key in sorted(drawn.keys() & listed.keys()):
        line, drawn_state = drawn[key]
        number, state, proof = listed[key]
        if state not in STATES:
            violations.append(
                f"{CONTAINER}:{number}: состояние «{state}» — бывает «{EXISTS}» или «{PLANNED}»"
            )
            continue
        if state != drawn_state:
            violations.append(
                f"{CONTAINER}:{line}: на картинке у «{key[0]} → {key[1]}» состояние "
                f"«{drawn_state}», в таблице — «{state}»"
            )
            continue

        if not proof:
            violations.append(
                f"{CONTAINER}:{number}: у стрелки «{key[0]} → {key[1]}» не сказано, чем она "
                f"подтверждается"
            )
            continue

        if state == EXISTS:
            references = CODE.findall(proof)
            if not references:
                violations.append(
                    f"{CONTAINER}:{number}: стрелка «{key[0]} → {key[1]}» помечена «{EXISTS}», "
                    f"а ссылки на файл нет. Существующая связь показывается файлом, а не словом"
                )
            for reference in references:
                if not (root / reference).exists():
                    violations.append(
                        f"{CONTAINER}:{number}: стрелка «{key[0]} → {key[1]}» помечена "
                        f"«{EXISTS}» и ссылается на «{reference}», которого в дереве нет"
                    )

    return violations


def check_index(root: Path) -> list[str]:
    """Оглавление каталога не расходится с каталогом."""
    directory = root / ARCHITECTURE
    index = root / INDEX
    if not index.is_file():
        return [f"{INDEX}: оглавления каталога нет"]

    text = index.read_text(encoding="utf-8")
    named = {link.strip() for link in LINK.findall(text)}
    violations: list[str] = []

    for path in sorted(directory.glob("*.md")):
        if path.name == "README.md":
            continue
        if path.name not in named:
            violations.append(
                f"{INDEX}: документа «{path.name}» нет в оглавлении. Оглавление, разошедшееся "
                f"с каталогом, — это оглавление, которому нельзя верить"
            )

    for name in sorted(named):
        if name.endswith(".md") and "/" not in name and not (directory / name).is_file():
            violations.append(f"{INDEX}: в оглавлении назван «{name}», которого нет")

    return violations


def check(root: Path) -> tuple[list[str], int]:
    violations = check_container(root) + check_index(root)
    path = root / CONTAINER
    drawn = edges(path.read_text(encoding="utf-8"))[0] if path.is_file() else {}
    return violations, len(drawn)


SELFTEST_CONTAINER = """# Контейнеры для самопроверки

```mermaid
flowchart TB
    tools -->|"psql · миграции · есть"| postgres
    tools -->|"compose · заглушка · есть"| mlstub
    web -->|"HTTPS · запись · план"| main
    main -->|"Postgres · таблицы · готово"| postgres
    web -->|"HTTPS · конспект · план"| room
    ghost -->|"HTTPS · нарисована, а строки нет · план"| main
```

| Стрелка | Протокол | Что передаётся | Состояние | Чем подтверждается |
| --- | --- | --- | --- | --- |
| tools → postgres | psql | миграции | есть | `scripts/migrate.py` |
| tools → mlstub | compose | заглушка | есть | `deploy/nowhere.yml` |
| web → main | HTTPS | запись | план | область `WEB` |
| main → postgres | Postgres | таблицы | есть | `scripts/migrate.py` |
| web → room | HTTPS | конспект | план | |
| lost → main | HTTPS | строка без стрелки | план | область `WEB` |
"""

SELFTEST_INDEX = """# Архитектура для самопроверки

| Файл | О чём |
| --- | --- |
| [c4-container.md](c4-container.md) | контейнеры |
| [gone.md](gone.md) | документ, которого нет |
"""

SELFTEST_FILES = {
    "docs/architecture/c4-container.md": SELFTEST_CONTAINER,
    "docs/architecture/README.md": SELFTEST_INDEX,
    "docs/architecture/orphan.md": "# Документ мимо оглавления\n",
    "scripts/migrate.py": "#!/usr/bin/env python3\n",
}

SELFTEST_EXPECTED = (
    ("main → postgres", "состояние «готово»"),
    ("ghost → main", "нарисована, а строки о ней нет"),
    ("lost → main", "которой на диаграмме нет"),
    ("tools → mlstub", "которого в дереве нет"),
    ("web → room", "не сказано, чем она подтверждается"),
    ("orphan.md", "нет в оглавлении"),
    ("gone.md", "которого нет"),
)

SELFTEST_CLEAN = ("tools → postgres", "web → main")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, drawn = check(root)

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» у {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: правильное объявлено нарушением: {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if drawn != 5:
            print(f"самопроверка: разобрано {drawn} стрелок вместо пяти", file=sys.stderr)
            return 1

        (root / CONTAINER).write_text("# Без картинки\n", encoding="utf-8")
        empty, _ = check(root)
        if not any("ни одной стрелки" in line for line in empty):
            print("самопроверка: диаграмма без стрелок прошла мимо проверки", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 1} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Диаграммы архитектуры (PDR-DOC-03).")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, drawn = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Правило — {INDEX}", file=sys.stderr)
        return 1

    print(f"Стрелок на диаграмме контейнеров: {drawn}. Готовность помечена у каждой, "
          f"оглавление совпадает с каталогом.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
