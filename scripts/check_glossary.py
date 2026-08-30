#!/usr/bin/env python3
"""Словарь продукта: инженерный термин в интерфейсе роняет сборку (PDR-DOC-06).

Половина ощущаемой сложности живёт не в функциях, а в словах. «Граф умений» и
«дескриптор» заставляют человека учить чужую модель мира вместо того, чтобы
работать. Правило и сам словарь — docs/product/glossary.md; здесь только
проверка.

СЛОВАРЬ ЧИТАЕТСЯ ИЗ ДОКУМЕНТА, а не дублируется тут: два списка запрещённых
слов в двух местах расходятся на второй неделе. Пополнение словаря — строка в
таблицу, без правки кода.

Проверяется:

* в тексте, который видит человек, нет запретного выражения. Найденное
  печатается вместе с заменой из столбца «Что видит человек»;
* в тексте, который видит человек, нет процента при числе: «готовность 64 %»
  не говорит ни к чему готовность, ни из чего посчитана. Число всегда с
  основанием;
* утверждённая формулировка сама не нарушает словарь: второй столбец
  проверяется тем же списком, что и всё остальное;
* у строки таблицы заполнены все три части. Строка без запретной части —
  украшение, по ней ничего не проверить;
* список жаргона в C++ (`JargonInExplanation`) не разошёлся со словарём:
  каждое его слово обязано быть в словаре. Числовые коды из этого требования
  исключены — в тексте интерфейса цифры законны («15 500 ₽»), и запретить их
  одним словом нельзя.

ГДЕ ЖИВЁТ ТЕКСТ, КОТОРЫЙ ВИДИТ ЧЕЛОВЕК. Список закрытый и растёт вместе с
продуктом: сегодня это клиент (`clients/`, которого ещё нет) и утверждённые
формулировки самого словаря. Подробность доменного отказа сюда не входит: в
libs/pdr-core/src/core/errors.hpp про неё сказано, что она «не для показа
ученику без перевода на язык интерфейса», — перевод и есть тот момент, где
словарь применяется.

Из клиента берутся строковые литералы, в которых есть русские буквы: в
продукте, который говорит по-русски, латинская строка — это код, а не надпись.

Запуск:
    python3 scripts/check_glossary.py
    python3 scripts/check_glossary.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

GLOSSARY = Path("docs/product/glossary.md")
JARGON_HEADER = Path("libs/pdr-testing/include/pdr/testing/integration_contract.hpp")

CLIENT_ROOT = Path("clients")

LEGAL = Path("docs/legal")

LEGAL_SKIP_FROM = "<!-- сверка получателей: начало -->"
LEGAL_SKIP_TO = "<!-- сверка получателей: конец -->"
"""Тексты, которые человек читает целиком, а не строкой интерфейса.

Перечень обработки персональных данных читает родитель, и «дескриптор» в нём
работает ровно так же, как на экране: заставляет учить чужую модель мира вместо
того, чтобы понять, что мы о нём храним (PDR-SEC-05, PDR-DOC-06).
"""
CLIENT_SUFFIXES = frozenset({".ts", ".tsx", ".js", ".jsx", ".json"})
SKIPPED_DIRS = frozenset({".git", "build", "out", "node_modules", "_deps", "dist", "__pycache__"})

HEADERS = ("Внутреннее название", "Что видит человек", "Чего не пишем никогда")
NOT_SHOWN = "не показывается вовсе"

TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
BACKTICKED = re.compile(r"`([^`]+)`")
CYRILLIC = re.compile(r"[А-Яа-яЁё]")
TOKEN = re.compile(r"[0-9A-Za-zА-Яа-яЁё]+(?:-[0-9A-Za-zА-Яа-яЁё]+)*")
PERCENT = re.compile(r"\d\s*%")

LITERAL = re.compile(
    r"'(?P<single>(?:[^'\\\n]|\\.)*)'"
    r"|\"(?P<double>(?:[^\"\\\n]|\\.)*)\""
    r"|`(?P<back>(?:[^`\\]|\\.)*)`",
    re.S,
)

JARGON_LIST = re.compile(
    r"JargonInExplanation\(\)[^{]*\{.*?words\s*\{(?P<body>[^}]*)\}", re.S
)
JARGON_WORD = re.compile(r'"([^"]*)"')


class GlossaryError(Exception):
    """Разбор словаря не удался. Это отказ, а не предупреждение."""


class Entry:
    """Одно запретное выражение и то, чем его заменяют."""

    def __init__(self, phrase: str, replacement: str, line: int) -> None:
        self.phrase = phrase
        self.replacement = replacement
        self.line = line
        self.tokens = tuple(part.lower() for part in TOKEN.findall(phrase))
        if not self.tokens:
            raise GlossaryError(
                f"{GLOSSARY}:{line}: «{phrase}» не содержит ни одного слова"
            )
    def stems(self) -> tuple[tuple[str, bool], ...]:
        """Пары «основа, продолжать ли поиск дальше по слову»."""
        pairs: list[tuple[str, bool]] = []
        for word in self.phrase.lower().split():
            found = TOKEN.findall(word)
            if not found:
                continue
            pairs.append((found[0], word.endswith("*")))
        return tuple(pairs)


def _cells(line: str) -> list[str]:
    found = TABLE_ROW.match(line.strip())
    if not found:
        return []
    return [cell.strip() for cell in found.group("cells").split("|")]


def parse_glossary(text: str) -> tuple[list[Entry], list[tuple[int, str]], list[str]]:
    """Запретные выражения, утверждённые формулировки и жалобы на таблицу."""
    entries: list[Entry] = []
    approved: list[tuple[int, str]] = []
    problems: list[str] = []

    inside = False
    for number, line in enumerate(text.splitlines(), start=1):
        cells = _cells(line)
        if not cells:
            inside = False
            continue
        if [cell for cell in cells[:3]] == list(HEADERS):
            inside = True
            continue
        if not inside:
            continue
        if all(set(cell) <= {"-", ":", " "} for cell in cells):
            continue
        if len(cells) < 3:
            problems.append(
                f"{GLOSSARY}:{number}: в строке словаря меньше трёх частей — "
                f"внутреннее название, что видит человек, чего не пишем никогда"
            )
            continue

        internal, shown, banned = cells[0], cells[1], cells[2]

        if not shown:
            problems.append(
                f"{GLOSSARY}:{number}: у «{internal}» не сказано, что видит человек. "
                f"Внутреннее название без замены — это не словарь, а список претензий"
            )
        forbidden = BACKTICKED.findall(banned)
        if not forbidden:
            problems.append(
                f"{GLOSSARY}:{number}: у «{internal}» пуста запретная часть. Строка без "
                f"неё — украшение: по ней ничего не проверить, а словарь заводился ради "
                f"проверки"
            )

        for phrase in forbidden:
            try:
                entries.append(Entry(phrase, shown, number))
            except GlossaryError as error:
                problems.append(str(error))

        if shown and NOT_SHOWN not in shown:
            approved.append((number, shown))

    if not entries and not problems:
        problems.append(f"{GLOSSARY}: таблицы словаря не нашлось")

    return entries, approved, problems


def words(text: str) -> list[str]:
    return [part.lower() for part in TOKEN.findall(text)]


def found_in(entry: Entry, tokens: Sequence[str]) -> bool:
    stems = entry.stems()
    if not stems:
        return False
    for start in range(len(tokens) - len(stems) + 1):
        if all(
            tokens[start + shift].startswith(stem) if wildcard else tokens[start + shift] == stem
            for shift, (stem, wildcard) in enumerate(stems)
        ):
            return True
    return False


def interface_texts(root: Path) -> Iterator[tuple[str, int, str]]:
    """(где, строка, текст) для всего, что видит человек."""
    client = root / CLIENT_ROOT
    if client.is_dir():
        for path in sorted(client.rglob("*")):
            if path.suffix not in CLIENT_SUFFIXES or not path.is_file():
                continue
            if any(part in SKIPPED_DIRS for part in path.parts):
                continue
            content = path.read_text(encoding="utf-8", errors="replace")
            where = str(path.relative_to(root))
            for match in LITERAL.finditer(content):
                value = match.group("single") or match.group("double") or match.group("back") or ""
                if not CYRILLIC.search(value):
                    continue
                yield where, content.count("\n", 0, match.start()) + 1, value

    legal = root / LEGAL
    if legal.is_dir():
        for path in sorted(legal.rglob("*.md")):
            content = path.read_text(encoding="utf-8", errors="replace")
            where = str(path.relative_to(root))
            skipping = False
            for number, line in enumerate(content.splitlines(), start=1):
                if LEGAL_SKIP_FROM in line:
                    skipping = True
                    continue
                if LEGAL_SKIP_TO in line:
                    skipping = False
                    continue
                if skipping or not CYRILLIC.search(line):
                    continue
                yield where, number, line

    glossary = root / GLOSSARY
    if glossary.is_file():
        _, approved, _ = parse_glossary(glossary.read_text(encoding="utf-8"))
        for number, shown in approved:
            yield str(GLOSSARY), number, shown


def check_texts(root: Path, entries: Sequence[Entry]) -> list[str]:
    violations: list[str] = []
    for where, number, text in interface_texts(root):
        tokens = words(text)
        for entry in entries:
            if found_in(entry, tokens):
                instead = (
                    "Этого человек не видит вовсе"
                    if NOT_SHOWN in entry.replacement
                    else f"Пишем {entry.replacement}"
                )
                violations.append(
                    f"{where}:{number}: «{entry.phrase}» — инженерный термин в тексте, "
                    f"который видит человек. {instead}. Словарь — {GLOSSARY}"
                )
        if PERCENT.search(text):
            violations.append(
                f"{where}:{number}: процент при числе. «Готовность 64 %» не говорит ни к "
                f"чему готовность, ни из чего посчитана; число показывается с основанием — "
                f"«из 25 умений уверенно 16»"
            )
    return violations


def check_jargon_list(root: Path, entries: Sequence[Entry]) -> list[str]:
    """Список жаргона в C++ обязан быть подмножеством словаря."""
    path = root / JARGON_HEADER
    if not path.is_file():
        return [
            f"{JARGON_HEADER}: заголовка нет. В нём живёт список жаргона, который сверяется "
            f"со словарём; переехал файл — проверка ослепла, и об этом надо знать"
        ]

    text = path.read_text(encoding="utf-8", errors="replace")
    block = JARGON_LIST.search(text)
    if block is None:
        return [
            f"{JARGON_HEADER}: списка JargonInExplanation не видно. Разбор понимает "
            f"инициализацию вида words{{\"...\", \"...\"}}"
        ]

    known = {entry.phrase.lower() for entry in entries}
    violations: list[str] = []
    for word in JARGON_WORD.findall(block.group("body")):
        if word.isdigit():
            continue
        if word.lower() not in known:
            violations.append(
                f"{JARGON_HEADER}: «{word}» есть в списке жаргона, но не в {GLOSSARY}. Два "
                f"списка запрещённых слов в двух местах расходятся на второй неделе — "
                f"главный здесь словарь"
            )
    return violations


def check(root: Path) -> tuple[list[str], int, int]:
    path = root / GLOSSARY
    if not path.is_file():
        return [f"{GLOSSARY}: словаря продукта нет"], 0, 0

    entries, _, violations = parse_glossary(path.read_text(encoding="utf-8"))
    violations = list(violations)
    violations.extend(check_texts(root, entries))
    violations.extend(check_jargon_list(root, entries))

    return violations, len(entries), sum(1 for _ in interface_texts(root))


SELFTEST_GLOSSARY = """# Словарь для самопроверки

| Внутреннее название | Что видит человек | Чего не пишем никогда |
| --- | --- | --- |
| граф умений | «что уже получается» | `граф умений`, `topic graph` |
| дескриптор | «умение» | `дескриптор*` |
| эмбеддинг | «похожие» | `эмбеддинг*`, `векторный поиск` |
| техническая строка | «чего нет и что делать» | `error`, `timeout` |
| горизонт | «граф умений покажет горизонт» | `горизонт*` |
| корзины репутации | не показывается вовсе | |
| модель владения | | `BKT` |
"""

SELFTEST_JARGON = """#pragma once

inline const std::vector<std::string>& JargonInExplanation() {
    static const std::vector<std::string> words{
        "error",
        "timeout",
        "500",
        "503",
        "retry",
    };
    return words;
}
"""

SELFTEST_FILES = {
    "docs/product/glossary.md": SELFTEST_GLOSSARY,
    "libs/pdr-testing/include/pdr/testing/integration_contract.hpp": SELFTEST_JARGON,
    "clients/web/src/Skills.tsx": (
        'export const title = "Граф умений";\n'
        'export const hint = "Дескрипторы подобраны по эмбеддингам";\n'
        'export const ready = "Готовность 64 %";\n'
    ),
    "clients/web/src/Good.tsx": (
        'export const title = "Что уже получается";\n'
        'export const hint = "Из 25 умений уверенно 16";\n'
        'export const similar = "Похожие";\n'
    ),
    "clients/web/src/plumbing.ts": (
        'try { load(); } catch (error) { report("error", "timeout"); }\n'
    ),
    "clients/web/src/vectors.tsx": ('export const topic = "Векторы в пространстве";\n'),
}

SELFTEST_EXPECTED = (
    ("Skills.tsx", "«граф умений»"),
    ("Skills.tsx", "«дескриптор*»"),
    ("Skills.tsx", "«эмбеддинг*»"),
    ("Skills.tsx", "процент при числе"),
    ("glossary.md", "«горизонт*»"),
    ("glossary.md", "пуста запретная часть"),
    ("glossary.md", "не сказано, что видит человек"),
    ("integration_contract.hpp", "«retry»"),
)

SELFTEST_CLEAN = ("Good.tsx", "plumbing.ts", "vectors.tsx", "«500»", "«503»")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, entries, texts = check(root)

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» в {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: правильное объявлено нарушением: {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if entries != 9:
            print(f"самопроверка: разобрано {entries} выражений вместо девяти", file=sys.stderr)
            return 1
        if texts < 4:
            print(f"самопроверка: собрано {texts} кусков текста, ожидалось больше",
                  file=sys.stderr)
            return 1

        (root / JARGON_HEADER).unlink()
        blind, _, _ = check(root)
        if not any("проверка ослепла" in line for line in blind):
            print("самопроверка: пропажа списка жаргона прошла мимо проверки", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 1} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Словарь продукта (PDR-DOC-06).")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, entries, texts = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Словарь и правила — {GLOSSARY}", file=sys.stderr)
        return 1

    print(f"Словарь: {entries} запретных выражений, проверено кусков текста: {texts}. "
          f"Инженерных терминов в интерфейсе нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
