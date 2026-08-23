#!/usr/bin/env python3
"""Долги первого сервиса собраны в одном списке, а не рассыпаны по документам.

Фундамент строится без сервиса намеренно, и часть механизмов дописана до половины:
компонент заданий ни разу не запускался, контур pytest_userver не заведён,
contract-набор хранилища гоняется только на фейке. Каждый такой долг записан там,
где он возник, — и это правильно. Неправильно другое: долг, о котором знают семь
разных страниц и ни одна не знает про остальные шесть.

Проверяется:

* документ, который говорит про первый сервис, ЛИБО назван в
  docs/architecture/first-service.md, ЛИБО сам ссылается на него. Долг обязан быть
  находим с двух сторон; иначе он появится в новом ADR и тихо разойдётся с общим
  списком;
* каждая страница, названная в списке, существует. Список, ссылающийся на
  исчезнувший файл, врёт;
* у каждой строки таблицы долгов заполнены все четыре столбца — в том числе «чем
  проверить». Долг без способа проверки закрывают словами.

Почему список, а не ссылки из документов в обе стороны: ADR не переписывают —
принятое решение заменяют новым (docs/adr/README.md). Значит, собирать долги
может только отдельная страница, которая ссылается на ADR, а не наоборот.

Ссылка на файл узнаётся по пути в круглых скобках, и путь не содержит пробелов и
скобок намеренно: иначе ссылка внутри скобок прозы — «(см. [layers.md](layers.md))»
— склеилась бы в несуществующий файл, а список долгов начал бы врать про
собственные ссылки.

Запуск:
    python3 scripts/check_debts.py
    python3 scripts/check_debts.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

DEBTS = Path("docs/architecture/first-service.md")

MENTIONS = re.compile(
    r"перв(?:ый|ого|ым|ому) сервис(?:|а|ом|у)\b|"
    r"первого живого прогона|"
    r"вместе с первым сервисом",
    re.IGNORECASE,
)

TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
PATH_IN_TEXT = re.compile(r"\(([^()\[\]\s]+\.(?:md|yaml|yml|py|hpp|cpp|sql))(?:#[^)]*)?\)")

SKIPPED_DIRS = frozenset({".git", "build", "out", "_deps", "__pycache__"})


def documents(root: Path):
    for path in sorted((root / "docs").rglob("*.md")) if (root / "docs").is_dir() else []:
        if any(part in SKIPPED_DIRS for part in path.parts):
            continue
        yield path
    readme = root / "README.md"
    if readme.is_file():
        yield readme


def table_rows(text: str) -> list[list[str]]:
    """Строки таблицы долгов: заголовок и разделитель отброшены."""
    rows: list[list[str]] = []
    for line in text.splitlines():
        found = TABLE_ROW.match(line.strip())
        if not found:
            continue
        cells = [cell.strip() for cell in found.group("cells").split("|")]
        if not cells or set("".join(cells)) <= set("- :"):
            continue
        if cells[0].lower().startswith("долг"):
            continue
        rows.append(cells)
    return rows


def check(root: Path) -> tuple[list[str], int]:
    debts_path = root / DEBTS
    violations: list[str] = []

    mentioning: list[Path] = []
    for path in documents(root):
        if path == debts_path:
            continue
        if MENTIONS.search(path.read_text(encoding="utf-8", errors="replace")):
            mentioning.append(path)

    if not debts_path.is_file():
        if mentioning:
            names = ", ".join(str(p.relative_to(root)) for p in mentioning)
            violations.append(
                f"{DEBTS}: списка долгов нет, а про первый сервис говорят: {names}. "
                f"Долг, о котором знают семь страниц и ни одна не знает про остальные "
                f"шесть, — это долг, о котором забудут"
            )
        return violations, 0

    debts_text = debts_path.read_text(encoding="utf-8")

    for path in mentioning:
        display = str(path.relative_to(root))
        name = path.name

        if DEBTS.name in path.read_text(encoding="utf-8", errors="replace"):
            continue

        if display not in debts_text and f"({name})" not in debts_text and name not in debts_text:
            violations.append(
                f"{display}: страница говорит про первый сервис, но в {DEBTS} не названа и "
                f"сама на него не ссылается. Допишите долг в список — иначе к началу "
                f"PDR-SCHED-04 о нём не вспомнят"
            )

    rows = table_rows(debts_text)
    for number, cells in enumerate(rows, start=1):
        if len(cells) < 4 or not all(cells[:4]):
            violations.append(
                f"{DEBTS}: в строке долгов №{number} заполнены не все четыре столбца "
                f"(долг, что сделать, чем проверить, где записано). Долг без способа "
                f"проверки закрывают словами"
            )

    for target in sorted(set(PATH_IN_TEXT.findall(debts_text))):
        if target.startswith(("http:", "https:")):
            continue
        if not (debts_path.parent / target).resolve().is_file():
            violations.append(
                f"{DEBTS}: названа страница «{target}», которой нет. Список, ссылающийся "
                f"на исчезнувший файл, врёт"
            )

    return violations, len(rows)


SELFTEST_DEBTS = """# Что обязан подобрать первый сервис

| Долг | Что сделать | Чем проверить | Где записано |
| --- | --- | --- | --- |
| Контур не заведён | Подключить плагин | Четыре сценария | [testing.md](../testing.md) |
| Кэш не заведён | Когда появится таблица | Инвалидация из теста | [ADR-0013](../adr/0013.md) |
| Без проверки | Что-то сделать |  | [testing.md](../testing.md) |
| Ссылка в пустоту | Что-то сделать | Как-то проверить | [нет](../adr/0099-missing.md) |

Ссылка внутри скобок прозы нарушением не считается (см. [testing.md](../testing.md)).
"""

SELFTEST_FILES = {
    "docs/architecture/first-service.md": SELFTEST_DEBTS,
    "docs/testing.md": "Контур появится вместе с первым сервисом.\n",
    "docs/adr/0013.md": "Кэш справочников ждёт первого сервиса.\n",
    "docs/adr/0014-orphan.md": "Это заведёт первый сервис, когда появится.\n",
    "docs/adr/0001.md": "Когда появится второй человек, это станет условием найма.\n",
    "README.md": "Долги первого сервиса — [first-service.md](docs/architecture/first-service.md).\n",
}

SELFTEST_EXPECTED = (
    ("0014-orphan.md", "в списке долгов не названа"),
    ("№3", "не все четыре столбца"),
    ("0099-missing.md", "которой нет"),
)

SELFTEST_CLEAN = ("0001.md", "README.md")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить потерянный долг."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, rows = check(root)

        for name, fragment in SELFTEST_EXPECTED:
            hits = [line for line in violations if name in line and fragment in line]
            if not hits:
                hits = [line for line in violations if name in line
                        and fragment.replace("в списке долгов ", "") in line]
            if not hits:
                print(f"самопроверка: не поймано «{fragment}» для {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: чистая страница объявлена нарушением: {name}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if rows != 4:
            print(f"самопроверка: разобрано {rows} строк долгов вместо четырёх", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Долги первого сервиса собраны в одном списке.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, rows = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Список долгов — {DEBTS}", file=sys.stderr)
        return 1

    print(f"Долгов первого сервиса в списке: {rows}. Ни один не потерян по документам.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
