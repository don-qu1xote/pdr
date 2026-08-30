#!/usr/bin/env python3
"""Правила запроса на слияние не расходятся с шаблоном и со скриптом (PDR-DOC-11).

Сливаем мы фазами, и каждый запрос — десятки коммитов и сотни файлов. Правило,
которое описано в CONTRIBUTING и не проверяется, держится ровно до первого
запроса, где кто-нибудь добавит седьмой раздел «прочее», — а дальше шесть
остальных перестают что-либо значить.

Проверяется:

* в шаблоне РОВНО ШЕСТЬ разделов, ровно тех и ровно в том порядке, что описаны в
  CONTRIBUTING. Седьмой роняет сборку, пропавший — тоже;
* тот же список разделов знает `scripts/pr_body.py`: тело, собранное скриптом, и
  шаблон, который видит человек, обязаны совпадать по составу;
* в шаблоне нет чеклистов (`- [ ]`): «я протестировал» проверяет машина, а не
  совесть, и галочка в теле запроса — это подпись под тем, чего никто не читал;
* в шаблоне нет раздела «прочее» ни под каким именем;
* заголовок описан БЕЗ идентификатора задачи: запрос — не задача, и выдуманный
  `[PDR-F0]` ссылается в никуда;
* три правила комментариев названы в CONTRIBUTING: требует действия, спор в ADR,
  чужое — отдельная задача.

Запуск:
    python3 scripts/check_pr_rules.py
    python3 scripts/check_pr_rules.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

TEMPLATE = Path(".github/pull_request_template.md")
CONTRIBUTING = Path("CONTRIBUTING.md")
BUILDER = Path("scripts/pr_body.py")

HEADING = re.compile(r"^##\s+(.+?)\s*$", re.M)
BUILDER_SECTIONS = re.compile(r"SECTIONS\s*=\s*\((.*?)\)", re.S)
QUOTED = re.compile(r'"([^"]+)"')
CHECKBOX = re.compile(r"^\s*[-*]\s*\[[ xX]\]", re.M)
COMMENT = re.compile(r"<!--.*?-->", re.S)

EXPECTED = (
    "Что здесь",
    "Чего здесь нет намеренно",
    "Что проверено машиной",
    "Как проверить у себя",
    "Долги",
    "Список задач",
)

CATCH_ALL = ("прочее", "разное", "остальное", "дополнительно", "misc", "other")

COMMENT_RULES = (
    "требует действия",
    "уходит в ADR",
    "отдельная задача",
)

TITLE_SHAPE = "Фаза N: короткое имя (K задач)"


def read(root: Path, path: Path) -> str:
    target = root / path
    return target.read_text(encoding="utf-8") if target.is_file() else ""


def check_template(text: str) -> list[str]:
    """Ровно шесть разделов, ровно тех и в том порядке."""
    if not text:
        return [f"{TEMPLATE}: шаблона запроса нет. Без него правила живут только в тексте, "
                f"а форму запроса каждый придумывает свою"]

    violations = []
    found = tuple(HEADING.findall(COMMENT.sub(" ", text)))

    if found != EXPECTED:
        if len(found) > len(EXPECTED):
            violations.append(
                f"{TEMPLATE}: разделов {len(found)} вместо {len(EXPECTED)}. Седьмой раздел — "
                f"это место, куда через месяц сваливают всё, и тогда шесть остальных "
                f"перестают что-либо значить: {list(found)}"
            )
        elif len(found) < len(EXPECTED):
            violations.append(
                f"{TEMPLATE}: разделов {len(found)} вместо {len(EXPECTED)}; нет "
                f"{sorted(set(EXPECTED) - set(found))}"
            )
        else:
            violations.append(
                f"{TEMPLATE}: разделы {list(found)} вместо {list(EXPECTED)}"
            )

    for heading in found:
        lowered = heading.lower()
        for word in CATCH_ALL:
            if word in lowered:
                violations.append(
                    f"{TEMPLATE}: раздел «{heading}» — свалка под другим именем. Ни одного "
                    f"раздела «на всё остальное» в теле запроса не бывает"
                )

    if CHECKBOX.search(text):
        violations.append(
            f"{TEMPLATE}: в шаблоне есть чеклист. «Я протестировал» проверяет машина, а не "
            f"совесть: галочка — это подпись под тем, чего никто не читал"
        )

    return violations


def check_builder(text: str) -> list[str]:
    """Скрипт знает те же разделы, что и шаблон."""
    if not text:
        return [f"{BUILDER}: сборки тела нет. Тело, написанное руками целиком, расходится с "
                f"деревом на второй день"]

    found = BUILDER_SECTIONS.search(text)
    if not found:
        return [f"{BUILDER}: списка разделов не нашлось — разбор не понял файл"]

    named = tuple(QUOTED.findall(found.group(1)))
    if named != EXPECTED:
        return [f"{BUILDER}: разделы {list(named)} вместо {list(EXPECTED)}. Шаблон, который "
                f"видит человек, и тело, которое собирает скрипт, обязаны совпадать по "
                f"составу"]
    return []


def check_contributing(text: str) -> list[str]:
    """Правила описаны там же, где остальные соглашения, и не разошлись."""
    if not text:
        return [f"{CONTRIBUTING}: соглашений нет вовсе"]

    violations = []
    if "## Запрос на слияние" not in text:
        violations.append(
            f"{CONTRIBUTING}: раздела «Запрос на слияние» нет. Коммит описан, а запрос — нет, "
            f"и сливать фазами будут кто во что горазд"
        )
        return violations

    if TITLE_SHAPE not in text:
        violations.append(
            f"{CONTRIBUTING}: форма заголовка «{TITLE_SHAPE}» не описана"
        )

    for section in EXPECTED:
        if section.lower() not in text.lower():
            violations.append(
                f"{CONTRIBUTING}: раздел «{section}» есть в шаблоне, а в соглашениях о нём "
                f"ничего не сказано"
            )

    for rule in COMMENT_RULES:
        if rule not in text:
            violations.append(
                f"{CONTRIBUTING}: правила комментариев «{rule}» нет. Комментарий, который ничем "
                f"не кончается, — это чужое время"
            )

    return violations


def check(root: Path) -> tuple[list[str], int]:
    violations = check_template(read(root, TEMPLATE))
    violations.extend(check_builder(read(root, BUILDER)))
    violations.extend(check_contributing(read(root, CONTRIBUTING)))
    return violations, len(EXPECTED)


SELFTEST_TEMPLATE = """<!-- подсказка -->

## Что здесь

## Чего здесь нет намеренно

## Что проверено машиной

## Как проверить у себя

## Долги

## Список задач

## Прочее

- [ ] я протестировал
"""

SELFTEST_BUILDER = '''
SECTIONS = (
    "Что здесь",
    "Долги",
)
'''

SELFTEST_CONTRIBUTING = """# Как здесь работают

## Коммит
"""

SELFTEST_TREE = {
    TEMPLATE: SELFTEST_TEMPLATE,
    BUILDER: SELFTEST_BUILDER,
    CONTRIBUTING: SELFTEST_CONTRIBUTING,
}

SELFTEST_EXPECTED = (
    "разделов 7 вместо 6",
    "свалка под другим именем",
    "есть чеклист",
    "обязаны совпадать по составу",
    "раздела «Запрос на слияние» нет",
)


def selftest() -> int:
    """Отрицательные случаи: каждый способ развалить форму запроса ловится."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path, content in SELFTEST_TREE.items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, sections = check(root)
        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if sections != 6:
            print(f"самопроверка: разделов {sections} вместо шести", file=sys.stderr)
            return 1

        (root / TEMPLATE).write_text(
            "\n".join(f"## {name}\n" for name in EXPECTED), encoding="utf-8"
        )
        clean = check_template(read(root, TEMPLATE))
        if clean:
            print(f"самопроверка: исправный шаблон объявлен нарушением: {clean}", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Правила запроса на слияние.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, sections = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Правила — {CONTRIBUTING}, раздел "
              f"«Запрос на слияние»", file=sys.stderr)
        return 1

    print(f"Разделов в запросе: {sections}. Шаблон, соглашения и сборка тела сходятся.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
