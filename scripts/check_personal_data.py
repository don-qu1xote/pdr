#!/usr/bin/env python3
"""Перечень обработки персональных данных не расходится с деревом (PDR-SEC-05).

Перечень определяет, что мы вообще имеем право отправить во внешнюю модель. Он
стоит ВЫШЕ кода, а не рядом: написанный после, он либо запретил бы уже
сделанное, либо оказался бы враньём. Значит, расходиться с деревом ему нельзя, и
проверяется это машиной, а не вычиткой раз в полгода.

Проверяется пять вещей, и каждая — отдельный способ соврать человеку:

* КАЖДАЯ строка перечня отвечает на все шесть вопросов: что это, зачем, на каком
  основании, сколько храним, кому уходит, как удалить. Пустая клетка — это
  данные, о которых сказали половину;
* категории перечня и `identity::kEveryPersonalDataCategory` совпадают в обе
  стороны. Категория в коде без строки в перечне — данные, которые мы собираем,
  не сказав человеку; строка без категории — обещание, которого код не знает, и
  экран «мои данные» о ней умолчит;
* ПОЛУЧАТЕЛИ ПЕРЕЧНЯ И РЕВИЗИИ ОТКРЫТОСТИ СХОДЯТСЯ. Каждый получатель назван
  строкой ревизии (docs/architecture/openness.md) либо явной пометкой «не
  интеграция» с причиной. Получатель, которого нет в ревизии, — зависимость, о
  которой мы никому не сказали; строка ревизии, получающая данные и не
  названная в перечне, — передача, о которой не сказали человеку;
* у каждого получателя заполнено «что НЕ уходит». Оно важнее первого столбца:
  «уходит текст расшифровки» без «не уходит видео и имена» читается как «уходит
  всё»;
* КАНЦЕЛЯРИТ. Перечень читает родитель, а не юрист (docs/product/glossary.md).
  «Обработка осуществляется в соответствии» — это текст, который не читают, а
  пролистывают, и согласие после него ничего не значит.

Разбор таблиц — тот же, что у ревизии открытости: строка markdown-таблицы по
имени первого столбца. Чего разбор не понял, он называет вслух.

Запуск:
    python3 scripts/check_personal_data.py
    python3 scripts/check_personal_data.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

LIST = Path("docs/legal/personal-data.md")
OPENNESS = Path("docs/architecture/openness.md")
CATEGORIES = Path("libs/pdr-identity/src/identity/core/personal_data.cpp")

CATEGORY_HEADER = "Категория"
RECIPIENT_HEADER = "Получатель"
AUDIT_HEADER = "Получатель в перечне"
VERSION_HEADER = "Версия"
DEPENDENCY_HEADER = "Зависимость"

CATEGORY_COLUMNS = 7
RECIPIENT_COLUMNS = 3
AUDIT_COLUMNS = 2
VERSION_COLUMNS = 4

TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
CATEGORY_CODE = re.compile(r'case PersonalDataCategory::k\w+:\s*\n\s*return "([a-z_]+)";')
RECIPIENT_CODE = re.compile(r'case Recipient::k\w+:\s*\n\s*return "([a-z_]+)";')

NOT_AN_INTEGRATION = "не интеграция"

SENDS_NOTHING = ("Видео (LiveKit)", "Календари")
"""Строки ревизии, которые ничего наружу не отдают.

Получателями они не являются: видео стоит у нас, календари мы читаем и пишем
сами. Требовать их в перечне значило бы врать про передачу, которой нет.
"""

OFFICIALESE = (
    "осуществляется в соответствии",
    "в целях реализации",
    "надлежащим образом",
    "вышеуказанн",
    "нижеследующ",
    "субъект персональных данных",
    "оператор персональных данных",
    "настоящим соглашением",
    "в порядке, установленном",
    "иные действия",
)


def read(root: Path, path: Path) -> str:
    target = root / path
    return target.read_text(encoding="utf-8") if target.is_file() else ""


def rows_by(text: str, header: str) -> list[tuple[int, list[str]]]:
    """Строки таблицы, у которой первый столбец назван так."""
    rows: list[tuple[int, list[str]]] = []
    inside = False
    for number, line in enumerate(text.splitlines(), start=1):
        found = TABLE_ROW.match(line.strip())
        if not found:
            inside = False
            continue
        cells = [cell.strip() for cell in found.group("cells").split("|")]
        if cells[0] == header:
            inside = True
            continue
        if not inside:
            continue
        if all(set(cell) <= {"-", ":", " "} for cell in cells):
            continue
        rows.append((number, cells))
    return rows


QUOTED = re.compile(r"«[^»]*»")


def said_in_our_own_voice(text: str) -> str:
    """Текст без цитат.

    Перечень объясняет, как писать НЕ надо, и приводит канцелярит примером:
    «обработка осуществляется в соответствии». Упоминание — не употребление, и
    проверка, запрещающая объяснять собственное правило, отменяется первой же.
    """
    return QUOTED.sub(" ", text)


def unmarked(text: str) -> str:
    """Текст без разметки: жирное и кавычки не мешают искать слова."""
    return text.replace("**", "").replace("`", "")


def check_categories(root: Path, text: str) -> tuple[list[str], int]:
    """Каждая строка отвечает на все шесть вопросов, и список совпадает с кодом."""
    violations = []
    rows = rows_by(text, CATEGORY_HEADER)
    if not rows:
        return ([f"{LIST}: перечня по строкам нет — сплошной текст вместо таблицы"], 0)

    for number, cells in rows:
        if len(cells) != CATEGORY_COLUMNS:
            violations.append(
                f"{LIST}:{number}: в строке «{cells[0]}» {len(cells)} столбцов вместо "
                f"{CATEGORY_COLUMNS}. Шесть ответов обязательны: что, зачем, основание, "
                f"сколько храним, кому уходит, как удалить"
            )
            continue
        for index, cell in enumerate(cells[1:], start=1):
            if not cell:
                violations.append(
                    f"{LIST}:{number}: у категории «{cells[0]}» пустой столбец {index + 1}. "
                    f"Данные, о которых сказали половину, — это данные, о которых соврали"
                )

    named = {cells[0] for _, cells in rows}
    known = set(CATEGORY_CODE.findall(read(root, CATEGORIES)))
    if not known:
        violations.append(f"{CATEGORIES}: списка категорий в коде нет — разбор не понял файл")
    elif len(named) != len(known):
        violations.append(
            f"{LIST}: категорий в перечне {len(named)}, а в "
            f"identity::kEveryPersonalDataCategory — {len(known)}. Категория в коде без строки "
            f"в перечне — данные, которые мы собираем, не сказав человеку; строка без "
            f"категории — обещание, о котором экран «мои данные» умолчит"
        )

    return violations, len(rows)


def check_recipients(root: Path, text: str) -> tuple[list[str], int]:
    """Получатели перечня и ревизии открытости сходятся."""
    violations = []
    rows = rows_by(text, RECIPIENT_HEADER)
    if not rows:
        return ([f"{LIST}: таблицы получателей нет. «Кому уходит» — отдельный разговор"], 0)

    audit = read(root, OPENNESS)
    if not audit:
        return ([f"{OPENNESS}: ревизии открытости нет — сверять получателей не с чем"], 0)

    in_audit = {unmarked(cells[0]) for _, cells in rows_by(audit, DEPENDENCY_HEADER)}
    if not in_audit:
        return ([f"{OPENNESS}: таблица «Чужое у нас» не разобралась"], 0)

    for number, cells in rows:
        if len(cells) != RECIPIENT_COLUMNS:
            violations.append(
                f"{LIST}:{number}: у получателя «{cells[0]}» {len(cells)} столбцов вместо "
                f"{RECIPIENT_COLUMNS}: что уходит и что НЕ уходит"
            )
            continue

        recipient, goes, stays = cells
        if not goes:
            violations.append(f"{LIST}:{number}: у получателя «{recipient}» не сказано, что уходит")
        if not stays:
            violations.append(
                f"{LIST}:{number}: у получателя «{recipient}» не сказано, что НЕ уходит. Это "
                f"важнее первого столбца: «уходит расшифровка» без «не уходит видео» читается "
                f"как «уходит всё»"
            )

    named_here: set[str] = set()
    matched = rows_by(text, AUDIT_HEADER)
    matched_names = set()
    for number, cells in matched:
        if len(cells) != AUDIT_COLUMNS:
            violations.append(
                f"{LIST}:{number}: в сверке у «{cells[0]}» {len(cells)} столбцов вместо "
                f"{AUDIT_COLUMNS}"
            )
            continue

        recipient, audit_row = cells
        matched_names.add(recipient)

        marked = unmarked(audit_row)
        if NOT_AN_INTEGRATION in marked:
            if marked.strip() == NOT_AN_INTEGRATION:
                violations.append(
                    f"{LIST}:{number}: у получателя «{recipient}» стоит «{NOT_AN_INTEGRATION}» "
                    f"без причины. Пометка без объяснения — способ не попасть в ревизию"
                )
            continue

        named_here.add(marked)
        if marked not in in_audit:
            violations.append(
                f"{LIST}:{number}: получателя «{marked}» нет в ревизии открытости "
                f"({OPENNESS}). Получатель мимо ревизии — зависимость, о которой мы никому "
                f"не сказали"
            )

    for recipient in sorted({cells[0] for _, cells in rows} - matched_names):
        violations.append(
            f"{LIST}: получатель «{recipient}» есть в таблице, а в сверке с ревизией его нет. "
            f"Сверка, отставшая от таблицы, ничего не проверяет"
        )

    for row in sorted(in_audit - named_here - set(SENDS_NOTHING)):
        violations.append(
            f"{OPENNESS}: строка «{row}» есть в ревизии, а в перечне получателей её нет. "
            f"Передача, о которой не сказали человеку, — это то, ради чего перечень и пишут"
        )

    return violations, len(rows)


def check_versions(text: str) -> list[str]:
    """Версии перечня хранятся списком, и у каждой сказано, существенная ли она."""
    rows = rows_by(text, VERSION_HEADER)
    if not rows:
        return [f"{LIST}: таблицы версий нет. Перечень меняется, и человек принимает версию"]

    violations = []
    for number, cells in rows:
        if len(cells) != VERSION_COLUMNS:
            violations.append(
                f"{LIST}:{number}: у версии «{cells[0]}» {len(cells)} столбцов вместо "
                f"{VERSION_COLUMNS}: когда, что изменилось, существенное"
            )
            continue
        if unmarked(cells[3]).lower() not in ("да", "нет"):
            violations.append(
                f"{LIST}:{number}: у версии «{cells[0]}» существенность — «{cells[3]}». "
                f"Это «да» или «нет», названные человеком при выпуске: код отличить «стало "
                f"понятнее» от «стало больше» не умеет"
            )
    return violations


def check_language(text: str) -> list[str]:
    """Это читает родитель, а не юрист."""
    violations = []
    lowered = said_in_our_own_voice(text).lower()
    for phrase in OFFICIALESE:
        if phrase in lowered:
            violations.append(
                f"{LIST}: канцелярит — «{phrase}». Перечень читает родитель: «мы записываем "
                f"занятия, чтобы сделать конспект» вместо «обработка осуществляется»"
            )
    return violations


def check(root: Path) -> tuple[list[str], int, int]:
    text = read(root, LIST)
    if not text:
        return ([f"{LIST}: перечня обработки персональных данных нет вовсе"], 0, 0)

    violations, categories = check_categories(root, text)
    recipient_problems, recipients = check_recipients(root, text)
    violations.extend(recipient_problems)
    violations.extend(check_versions(text))
    violations.extend(check_language(text))
    return violations, categories, recipients


SELFTEST_LIST = """# Что мы храним

| Категория | Что это | Зачем | Основание | Сколько храним | Кому уходит | Как удалить |
| --- | --- | --- | --- | --- | --- | --- |
| Учётные данные | имя и почта | чтобы войти | согласие | пока пользуетесь | никому | кнопкой |
| Платежи | сумма | для чека | закон |  | провайдеру | не удаляется |

| Получатель | Что уходит | Что НЕ уходит |
| --- | --- | --- |
| Платёжный провайдер | сумма | имя ребёнка |
| Кто-то ещё | всё | ничего |
| Хостинг | всё | ничего |
| Забытый в сверке | что-то | что-то |

| Получатель в перечне | Строка ревизии |
| --- | --- |
| Платёжный провайдер | Платёжный провайдер |
| Кто-то ещё | Неизвестная строка |
| Хостинг | **не интеграция** |

| Версия | Когда | Что изменилось | Существенное |
| --- | --- | --- | --- |
| 1 | 2026-01-01 | первая | наверное |

Обработка осуществляется в соответствии с требованиями.

А вот так писать не надо: «в целях реализации», это никто не читает.
"""

SELFTEST_OPENNESS = """# Ревизия

| Зависимость | Стандарт или проприетарный | Альтернатива | Что при исчезновении | Набор |
| --- | --- | --- | --- | --- |
| Платёжный провайдер | проприетарный | четыре | приём останавливается | `кода нет` |
| Чеки самозанятого | проприетарно | нет | без чека | `кода нет` |
| Видео (LiveKit) | открытый | форк | ничего | `кода нет` |
"""

SELFTEST_CATEGORIES = """
std::string_view Name(PersonalDataCategory category) noexcept {
    switch (category) {
        case PersonalDataCategory::kAccount:
            return "account";
        case PersonalDataCategory::kMessages:
            return "messages";
        case PersonalDataCategory::kTechnicalRecords:
            return "technical_records";
    }
}
"""

SELFTEST_TREE = {
    LIST: SELFTEST_LIST,
    OPENNESS: SELFTEST_OPENNESS,
    CATEGORIES: SELFTEST_CATEGORIES,
}

SELFTEST_EXPECTED = (
    "пустой столбец",
    "категорий в перечне",
    "нет в ревизии открытости",
    "без причины",
    "есть в ревизии, а в перечне получателей её нет",
    "есть в таблице, а в сверке с ревизией его нет",
    "существенность — «наверное»",
    "канцелярит",
)


def selftest() -> int:
    """Отрицательные случаи: каждый способ соврать человеку обязан ловиться."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path, content in SELFTEST_TREE.items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, categories, recipients = check(root)
        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if (categories, recipients) != (2, 4):
            print(f"самопроверка: разобрано {categories} категорий и {recipients} получателей "
                  f"вместо двух и четырёх", file=sys.stderr)
            return 1

        if any("Видео (LiveKit)" in line for line in violations):
            print("самопроверка: строка, которая ничего не отдаёт, объявлена получателем",
                  file=sys.stderr)
            return 1

        if any("в целях реализации" in line for line in violations):
            print("самопроверка: канцелярит в кавычках объявлен канцеляритом: проверка, "
                  "запрещающая объяснять собственное правило, отменяется первой же",
                  file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Перечень обработки не расходится с деревом.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, categories, recipients = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Перечень — {LIST}", file=sys.stderr)
        return 1

    print(f"Категорий данных в перечне: {categories}, получателей: {recipients}. "
          f"С ревизией открытости и с кодом сходятся.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
