#!/usr/bin/env python3
"""Продуктовые события: ни события без вопроса, ни вопроса без события (PDR-OBS-03).

Продукт полон чисел, выбранных на глаз: окно возражения, доля выдачи под
новичков, сила априора. Данные, которыми их проверяют, соберутся только если их
собирать с самого начала — спохватиться через полгода нельзя, прошлого нет.
Поэтому в проекте два файла, и они обязаны сходиться:

* docs/product/open-questions.md — вопросы, на которые сегодня нет ответа;
* configs/product-events.yaml — события, которыми эти вопросы закрываются.

Проверяется:

* у события есть хотя бы один вопрос в поле closes. Событие без вопроса — это
  сбор «на всякий случай»: данные, которые никто не смотрит, и риск, который
  никто не считал;
* у вопроса, закрываемого ДАННЫМИ, есть хотя бы одно событие; у вопроса,
  закрываемого правом или замером, событий нет — иначе столбец врёт о том,
  зачем эти данные собираются;
* ссылки сходятся в обе стороны: вопрос называет событие тогда и только тогда,
  когда событие называет вопрос. Односторонняя ссылка означает, что один из
  файлов уже правили без второго;
* у каждой ПРОДУКТОВОЙ величины из configs/dynamic/registry.yaml есть вопрос.
  Это и есть машинная форма правила из CONTRIBUTING.md: задача, вводящая число
  на глаз, обязана добавить вопрос и событие, которое это число проверит;
* имя события — «контекст.что_произошло», и контекст существует на карте
  (docs/architecture/context-map.md). Событие от несуществующего издателя никто
  не напишет;
* у события есть версия схемы, описание и поля; у каждого поля — тип из
  закрытого списка и описание;
* У КАЖДОГО СОБЫТИЯ ЕСТЬ СЦЕНАРИЙ В ТЕСТАХ: имя типа встречается в тестах
  модулей (libs/*/tests). «Событие есть в реестре» и «событие пишется» — разные
  утверждения, и первое без второго закрывает вопрос молчанием;
* НЕТ ПОЛЯ, ИМЕНУЮЩЕГО ЧЕЛОВЕКА. Ни «*_id», ни почты, ни телефона, ни имени:
  продуктовое событие обезличено на уровне записи, ссылка только на арендатора
  и роль;
* ОПУБЛИКОВАННАЯ СХЕМА НЕ МЕНЯЕТСЯ. Снимок каждой пары «тип + версия» лежит в
  configs/product-events.published.yaml. Поле можно ДОБАВИТЬ — старый читатель
  не заметит нового поля. Убрать или переименовать нельзя: старые записи
  остаются в таблице и читаются тем же кодом. Нужно убрать — поднимите версию,
  и снимок получит новую строку, а старая останется.

Снимок обновляется --update и обновляется НЕ МОЛЧА: если обновление стёрло бы
поле из уже опубликованной пары, оно отказывается это делать. Иначе обход
правила состоял бы из одной команды.

YAML разбирается СТРОГИМ ПОДМНОЖЕСТВОМ, как разбор миграций разбирает
подмножество DDL: чего разбор не понял, он называет вслух и роняет проверку.
Молча пропущенная запись означала бы, что реестр проверен наполовину.

Запуск:
    python3 scripts/check_product_events.py
    python3 scripts/check_product_events.py --update
    python3 scripts/check_product_events.py --selftest
"""

from __future__ import annotations

import argparse
import contextlib
import io
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_dynamic_configs as configs  # noqa: E402  (после правки sys.path)

EVENTS = Path("configs/product-events.yaml")
PUBLISHED = Path("configs/product-events.published.yaml")
QUESTIONS = Path("docs/product/open-questions.md")
REGISTRY = configs.REGISTRY
CONTEXT_MAP = Path("docs/architecture/context-map.md")
MODULES = Path("libs")

TEST_SUFFIXES = frozenset({".cpp", ".hpp", ".cc", ".hxx"})

EVENT_FIELDS = ("description", "closes", "version", "fields")

FIELD_TYPES = ("count", "minutes", "hours", "days", "flag", "bucket", "code", "reference", "score")

EVENT_NAME = re.compile(r"^[a-z][a-z0-9_]*\.[a-z][a-z0-9_]*$")
FIELD_NAME = re.compile(r"^[a-z][a-z0-9_]*$")
QUESTION_ID = re.compile(r"^[a-z][a-z0-9_]*$")
CONTEXT_HEADING = re.compile(r"^### ([a-z][a-z0-9_]*) — ", re.M)
PUBLISHED_KEY = re.compile(r"^([a-z][a-z0-9_.]*)@(\d+)$")

PERSON_WORDS = ("person", "email", "phone", "login", "name", "passport", "birthday", "address")

BY_DATA = "данными"
CLOSED_BY = (BY_DATA, "правом", "замером")

TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")


class EventsError(Exception):
    """Разбор не удался. Это отказ, а не предупреждение."""


def _cells(line: str) -> list[str]:
    found = TABLE_ROW.match(line.strip())
    if not found:
        return []
    return [cell.strip().strip("`").strip() for cell in found.group("cells").split("|")]


def parse_block(lines: Sequence[str], start: int, indent: int) -> tuple[object, int]:
    """Значение блока с отступом больше indent и позиция за ним.

    Понимается ровно то, из чего состоят оба файла: отображения, списки через
    дефис, списки в квадратных скобках, скаляры и свёрнутый текст.
    """
    body: list[tuple[int, str]] = []
    index = start
    while index < len(lines):
        line = lines[index]
        if not line.strip() or line.lstrip().startswith("#"):
            index += 1
            continue
        current = len(line) - len(line.lstrip())
        if current <= indent:
            break
        body.append((index, line))
        index += 1

    if not body:
        return "", index

    own = min(len(text) - len(text.lstrip()) for _, text in body)

    if body[0][1].lstrip().startswith("- "):
        items: list[str] = []
        for number, text in body:
            stripped = text.strip()
            if not stripped.startswith("- "):
                raise EventsError(f"строка {number + 1}: список продолжен не элементом: «{stripped[:40]}»")
            items.append(stripped[2:].strip().strip("'\""))
        return items, index

    mapping: dict[str, object] = {}
    position = 0
    while position < len(body):
        number, text = body[position]
        level = len(text) - len(text.lstrip())
        if level != own:
            raise EventsError(f"строка {number + 1}: отступ съехал на «{text.strip()[:40]}»")

        key, separator, rest = text.strip().partition(":")
        if not separator:
            raise EventsError(f"строка {number + 1}: разбор не понял строку «{text.strip()[:40]}»")
        if key in mapping:
            raise EventsError(f"строка {number + 1}: «{key}» встречается второй раз")

        rest = rest.strip()
        nested_start = body[position][0] + 1
        if rest in (">-", ">", "|", "|-"):
            folded, _ = _folded(lines, nested_start, own)
            mapping[key] = folded
            position = _skip(body, position, own)
            continue
        if rest == "":
            value, _ = parse_block(lines, nested_start, own)
            mapping[key] = value
            position = _skip(body, position, own)
            continue
        if rest.startswith("[") and rest.endswith("]"):
            mapping[key] = [part.strip().strip("'\"") for part in rest[1:-1].split(",") if part.strip()]
        else:
            mapping[key] = rest.strip("'\"")
        position += 1

    return mapping, index


def _folded(lines: Sequence[str], start: int, indent: int) -> tuple[str, int]:
    parts: list[str] = []
    index = start
    while index < len(lines):
        line = lines[index]
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
        if line.strip():
            parts.append(line.strip())
        index += 1
    return " ".join(parts), index


def _skip(body: Sequence[tuple[int, str]], position: int, own: int) -> int:
    position += 1
    while position < len(body):
        _, text = body[position]
        if len(text) - len(text.lstrip()) <= own:
            break
        position += 1
    return position


def parse_events(text: str) -> dict[str, dict[str, object]]:
    """Записи реестра событий: имя типа события — его описание."""
    lines = text.splitlines()
    events: dict[str, dict[str, object]] = {}

    index = 0
    while index < len(lines):
        line = lines[index]
        if not line.strip() or line.lstrip().startswith("#"):
            index += 1
            continue
        if line[0].isspace():
            raise EventsError(
                f"{EVENTS}:{index + 1}: разбор не понял строку «{line.strip()[:40]}». "
                f"На верхнем уровне бывают только имена событий вида контекст.что_произошло:"
            )

        name, separator, rest = line.partition(":")
        if not separator or rest.strip():
            raise EventsError(
                f"{EVENTS}:{index + 1}: разбор не понял строку «{line.strip()[:40]}». "
                f"На верхнем уровне бывают только имена событий вида контекст.что_произошло:"
            )
        if name in events:
            raise EventsError(f"{EVENTS}:{index + 1}: событие {name} описано второй раз")

        try:
            block, index = parse_block(lines, index + 1, 0)
        except EventsError as error:
            raise EventsError(f"{EVENTS}: у события {name} {error}") from error
        if not isinstance(block, dict):
            raise EventsError(f"{EVENTS}: у события {name} тело не похоже на набор полей")
        events[name] = block

    return events


def parse_questions(text: str) -> tuple[dict[str, dict[str, object]], list[str]]:
    """Вопросы из таблиц реестра и жалобы на сами таблицы."""
    questions: dict[str, dict[str, object]] = {}
    problems: list[str] = []

    header: list[str] | None = None
    columns: dict[str, int] = {}

    for number, line in enumerate(text.splitlines(), start=1):
        cells = _cells(line)
        if not cells:
            header = None
            continue
        if header is None:
            header = cells
            columns = {}
            for position, title in enumerate(cells):
                lowered = title.lower()
                if lowered == "id":
                    columns["id"] = position
                elif "величина" in lowered:
                    columns["value"] = position
                elif "чем закрывается" in lowered:
                    columns["closed_by"] = position
                elif lowered == "события":
                    columns["events"] = position
            if "id" not in columns:
                problems.append(
                    f"{QUESTIONS}:{number}: у таблицы нет столбца «id». По нему событие "
                    f"говорит, что закрывает, и по нему же расхождение находится одним grep"
                )
                header = []
            continue
        if all(set(cell) <= {"-", ":", " "} for cell in cells):
            continue
        if not columns:
            continue

        identifier = cells[columns["id"]]
        if not QUESTION_ID.match(identifier):
            problems.append(
                f"{QUESTIONS}:{number}: «{identifier}» не годится в идентификатор вопроса: "
                f"строчные буквы, цифры и подчёркивание"
            )
            continue
        if identifier in questions:
            problems.append(f"{QUESTIONS}:{number}: вопрос {identifier} записан второй раз")
            continue

        events = cells[columns["events"]] if "events" in columns else ""
        questions[identifier] = {
            "line": number,
            "value": cells[columns["value"]] if "value" in columns else "",
            "closed_by": cells[columns["closed_by"]] if "closed_by" in columns else "",
            "events": [part.strip().strip("`") for part in events.split(",") if part.strip()],
        }

    return questions, problems


def parse_published(text: str) -> dict[tuple[str, int], dict[str, str]]:
    """Снимок опубликованных схем: (тип, версия) — поля и их типы."""
    published: dict[tuple[str, int], dict[str, str]] = {}
    lines = text.splitlines()

    index = 0
    while index < len(lines):
        line = lines[index]
        if not line.strip() or line.lstrip().startswith("#"):
            index += 1
            continue
        if line[0].isspace():
            raise EventsError(f"{PUBLISHED}:{index + 1}: разбор не понял строку «{line.strip()[:40]}»")

        key = line.partition(":")[0].strip()
        found = PUBLISHED_KEY.match(key)
        if not found:
            raise EventsError(
                f"{PUBLISHED}:{index + 1}: «{key}» не похоже на «тип@версия». Снимок пишется "
                f"машиной: python3 scripts/check_product_events.py --update"
            )

        block, index = parse_block(lines, index + 1, 0)
        if not isinstance(block, dict):
            raise EventsError(f"{PUBLISHED}: у {key} тело не похоже на набор полей")
        published[(found.group(1), int(found.group(2)))] = {
            name: str(kind) for name, kind in block.items()
        }

    return published


def render_published(snapshot: dict[tuple[str, int], dict[str, str]]) -> str:
    """Снимок текстом. Порядок — по имени и версии, чтобы разница читалась глазами."""
    head = (
        "# Опубликованные схемы продуктовых событий. Файл пишет машина:\n"
        "# python3 scripts/check_product_events.py --update (make product-events-lock).\n"
        "#\n"
        "# Он существует ради одного правила: ОПУБЛИКОВАННАЯ СХЕМА НЕ МЕНЯЕТСЯ. Поле можно\n"
        "# добавить — старый читатель не заметит нового поля. Убрать или переименовать\n"
        "# нельзя: старые записи остаются в таблице и читаются тем же кодом. Нужно убрать —\n"
        "# поднимите версию: снимок получит новую строку, а старая останется навсегда.\n"
        "#\n"
        "# Строки исчезнувших версий отсюда НЕ удаляются: событие убрали из реестра, а\n"
        "# записи о нём в базе остались, и читать их кто-то будет.\n"
    )
    parts = [head]
    for (name, version) in sorted(snapshot):
        parts.append(f"\n{name}@{version}:\n")
        for field in sorted(snapshot[(name, version)]):
            parts.append(f"  {field}: {snapshot[(name, version)][field]}\n")
    return "".join(parts)


def current_snapshot(events: dict[str, dict[str, object]]) -> dict[tuple[str, int], dict[str, str]]:
    snapshot: dict[tuple[str, int], dict[str, str]] = {}
    for name, event in events.items():
        version = event.get("version")
        fields = event.get("fields")
        if not isinstance(fields, dict):
            continue
        try:
            number = int(str(version))
        except (TypeError, ValueError):
            continue
        snapshot[(name, number)] = {
            field: str(body.get("type", "")) if isinstance(body, dict) else ""
            for field, body in fields.items()
        }
    return snapshot


def frozen_schema_violations(
    current: dict[tuple[str, int], dict[str, str]],
    published: dict[tuple[str, int], dict[str, str]],
) -> list[str]:
    """Что нельзя было делать с уже опубликованной парой «тип + версия»."""
    violations: list[str] = []
    for key in sorted(published):
        if key not in current:
            continue
        name, version = key
        for field, kind in sorted(published[key].items()):
            if field not in current[key]:
                violations.append(
                    f"{EVENTS}: у события {name} версии {version} пропало поле «{field}». "
                    f"Опубликованная схема не меняется: старые записи остаются в таблице и "
                    f"читаются тем же кодом. Нужно убрать поле — поднимите версию"
                )
            elif current[key][field] != kind:
                violations.append(
                    f"{EVENTS}: у события {name} версии {version} поле «{field}» сменило тип "
                    f"«{kind}» на «{current[key][field]}». Это та же смена схемы: читатель "
                    f"старых записей ждёт прежний тип. Поднимите версию"
                )
    return violations


def check_event(name: str, event: dict[str, object], contexts: set[str]) -> list[str]:
    violations: list[str] = []

    if not EVENT_NAME.match(name):
        violations.append(
            f"{EVENTS}: «{name}» не похоже на имя события. Имя — «контекст.что_произошло»: "
            f"по нему видно издателя, и подписчик не гадает, чьё это"
        )
    else:
        context = name.split(".", 1)[0]
        if contexts and context not in contexts:
            violations.append(
                f"{EVENTS}: событие {name} публикует контекст «{context}», которого нет на "
                f"карте ({CONTEXT_MAP}). Событие от несуществующего издателя никто не напишет"
            )

    for field in EVENT_FIELDS:
        if field not in event or not event[field]:
            violations.append(f"{EVENTS}: у события {name} нет поля «{field}»")

    version = str(event.get("version", "")).strip()
    if version and (not version.isdigit() or int(version) < 1):
        violations.append(
            f"{EVENTS}: у события {name} версия «{version}». Версия — целое от единицы: схема "
            f"без версии запрещена, менять её придётся, и не один раз"
        )

    closes = event.get("closes")
    if closes is not None and not isinstance(closes, list):
        violations.append(
            f"{EVENTS}: у события {name} поле closes не список. Вопросы перечисляются списком, "
            f"даже если он из одного вопроса"
        )

    fields = event.get("fields")
    if fields is not None and not isinstance(fields, dict):
        violations.append(f"{EVENTS}: у события {name} поле fields не похоже на набор полей")
        return violations

    for field, body in (fields or {}).items():
        violations.extend(check_field(name, str(field), body))

    return violations


def check_field(event: str, field: str, body: object) -> list[str]:
    violations: list[str] = []
    where = f"{EVENTS}: у события {event} поле «{field}»"

    if not FIELD_NAME.match(field):
        violations.append(f"{where} названо не по форме: строчные буквы, цифры и подчёркивание")

    if field == "id" or field.endswith("_id"):
        violations.append(
            f"{where} — идентификатор. В продуктовом событии его нет: ссылка только на "
            f"арендатора и роль. Считает издатель, а не аналитик: «какое занятие по счёту» "
            f"вычисляет тот, кто знает ученика, в событие попадает число"
        )
    for word in PERSON_WORDS:
        if word in field:
            violations.append(
                f"{where} именует человека («{word}»). Продуктовое событие обезличено на "
                f"уровне записи: ответ на вопрос «репетиторы правят конспекты?» не требует "
                f"знать, кто именно правил"
            )
            break

    if not isinstance(body, dict):
        violations.append(f"{where} описано не парой «тип и описание»")
        return violations

    kind = str(body.get("type", "")).strip()
    if not kind:
        violations.append(f"{where} без типа")
    elif kind not in FIELD_TYPES:
        violations.append(
            f"{where} имеет тип «{kind}», которого нет в закрытом списке "
            f"({', '.join(FIELD_TYPES)}). Список закрыт затем, чтобы «свободный текст» не "
            f"появился в обезличенном потоке как ни в чём не бывало"
        )
    if not str(body.get("description", "")).strip():
        violations.append(f"{where} без описания: зачем оно собрано, знает только автор")

    return violations


def cross_check(
    events: dict[str, dict[str, object]], questions: dict[str, dict[str, object]]
) -> list[str]:
    """Ссылки сходятся в обе стороны. Односторонняя означает, что файлы разъехались."""
    violations: list[str] = []

    for name in sorted(events):
        closes = events[name].get("closes")
        closes = closes if isinstance(closes, list) else []
        if not closes:
            violations.append(
                f"{EVENTS}: событие {name} не закрывает ни одного вопроса. Сбор «на всякий "
                f"случай» — это данные, которые никто не смотрит, и риск, который никто не "
                f"считал. Вопрос заводится в {QUESTIONS}"
            )
        for question in closes:
            if question not in questions:
                violations.append(
                    f"{EVENTS}: событие {name} закрывает вопрос «{question}», которого нет в "
                    f"{QUESTIONS}"
                )
            elif name not in questions[question]["events"]:
                violations.append(
                    f"{QUESTIONS}: вопрос {question} не называет событие {name}, а событие "
                    f"называет вопрос. Ссылка односторонняя: один из двух файлов уже правили "
                    f"без второго"
                )

    for identifier in sorted(questions):
        question = questions[identifier]
        closed_by = str(question["closed_by"])
        named = list(question["events"])

        if not any(word in closed_by for word in CLOSED_BY):
            violations.append(
                f"{QUESTIONS}:{question['line']}: у вопроса {identifier} способ закрытия "
                f"«{closed_by}» — бывает {', '.join(CLOSED_BY)}"
            )

        if BY_DATA in closed_by and not named:
            violations.append(
                f"{QUESTIONS}:{question['line']}: вопрос {identifier} закрывается данными, а "
                f"события у него нет. Данные соберутся, только если их собирать с самого "
                f"начала: спохватиться через полгода нельзя, прошлого нет"
            )
        if BY_DATA not in closed_by and named:
            violations.append(
                f"{QUESTIONS}:{question['line']}: вопрос {identifier} закрывается не данными, "
                f"а события у него названы. Тогда непонятно, зачем они собираются"
            )

        for name in named:
            if name not in events:
                violations.append(
                    f"{QUESTIONS}:{question['line']}: вопрос {identifier} закрывается событием "
                    f"{name}, которого нет в {EVENTS}"
                )
            else:
                closes = events[name].get("closes")
                closes = closes if isinstance(closes, list) else []
                if identifier not in closes:
                    violations.append(
                        f"{EVENTS}: событие {name} не называет вопрос «{identifier}», а вопрос "
                        f"называет событие. Ссылка односторонняя: один из двух файлов уже "
                        f"правили без второго"
                    )

    return violations


def check_registry(root: Path, questions: dict[str, dict[str, object]]) -> list[str]:
    """Продуктовое число без вопроса — то самое, ради чего задача и делается."""
    path = root / REGISTRY
    if not path.is_file():
        return []

    try:
        entries = configs.parse_registry(path.read_text(encoding="utf-8"))
    except configs.RegistryError as error:
        return [str(error)]

    asked = {str(question["value"]) for question in questions.values() if question["value"]}
    violations: list[str] = []

    for name in sorted(entries):
        if str(entries[name].get("kind", "")).strip() != "продуктовая":
            continue
        if name not in asked:
            violations.append(
                f"{REGISTRY}: у продуктовой величины {name} нет вопроса в {QUESTIONS}. Число, "
                f"выбранное на глаз, без вопроса нечем оспорить — спорить будут мнениями. "
                f"Правило целиком — CONTRIBUTING.md"
            )

    for name in sorted(asked):
        if name not in entries:
            violations.append(
                f"{QUESTIONS}: вопрос ссылается на величину {name}, которой нет в {REGISTRY}"
            )

    return violations


def scenarios_in_tests(root: Path) -> str:
    """Весь текст тестов модулей одной строкой. Сценарий ищется в нём по имени типа."""
    modules = root / MODULES
    if not modules.is_dir():
        return ""

    parts: list[str] = []
    for path in sorted(modules.rglob("*")):
        if path.suffix not in TEST_SUFFIXES or not path.is_file():
            continue
        if "tests" not in path.parts:
            continue
        parts.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def check_scenarios(root: Path, events: dict[str, dict[str, object]]) -> list[str]:
    """Событие без сценария отвечает на вопрос молчанием."""
    text = scenarios_in_tests(root)
    violations: list[str] = []
    for name in sorted(events):
        if f'"{name}"' not in text:
            violations.append(
                f"{EVENTS}: у события {name} нет сценария в тестах ({MODULES}/*/tests). "
                f"«Событие есть в реестре» и «событие пишется» — разные утверждения, и "
                f"первое без второго закрывает вопрос молчанием"
            )
    return violations


def contexts_on_map(root: Path) -> set[str]:
    path = root / CONTEXT_MAP
    if not path.is_file():
        return set()
    return set(CONTEXT_HEADING.findall(path.read_text(encoding="utf-8")))


def check(root: Path) -> tuple[list[str], int]:
    events_path = root / EVENTS
    questions_path = root / QUESTIONS

    if not events_path.is_file():
        return [f"{EVENTS}: реестра продуктовых событий нет"], 0
    if not questions_path.is_file():
        return [f"{QUESTIONS}: реестра продуктовых вопросов нет"], 0

    try:
        events = parse_events(events_path.read_text(encoding="utf-8"))
    except EventsError as error:
        return [str(error)], 0

    questions, violations = parse_questions(questions_path.read_text(encoding="utf-8"))
    contexts = contexts_on_map(root)

    for name in sorted(events):
        violations.extend(check_event(name, events[name], contexts))

    violations.extend(cross_check(events, questions))
    violations.extend(check_registry(root, questions))
    violations.extend(check_scenarios(root, events))

    published_path = root / PUBLISHED
    if not published_path.is_file():
        violations.append(
            f"{PUBLISHED}: снимка опубликованных схем нет. Он пишется машиной: "
            f"make product-events-lock"
        )
        return violations, len(events)

    try:
        published = parse_published(published_path.read_text(encoding="utf-8"))
    except EventsError as error:
        violations.append(str(error))
        return violations, len(events)

    current = current_snapshot(events)
    violations.extend(frozen_schema_violations(current, published))

    merged = dict(published)
    merged.update(current)
    if render_published(merged) != published_path.read_text(encoding="utf-8"):
        violations.append(
            f"{PUBLISHED}: снимок устарел. Схема поменялась, а снимок остался прежним: "
            f"make product-events-lock"
        )

    return violations, len(events)


def update(root: Path) -> int:
    """Записать снимок. Отказывается стирать поле уже опубликованной версии."""
    try:
        events = parse_events((root / EVENTS).read_text(encoding="utf-8"))
    except EventsError as error:
        print(str(error), file=sys.stderr)
        return 1

    published: dict[tuple[str, int], dict[str, str]] = {}
    path = root / PUBLISHED
    if path.is_file():
        try:
            published = parse_published(path.read_text(encoding="utf-8"))
        except EventsError as error:
            print(str(error), file=sys.stderr)
            return 1

    current = current_snapshot(events)
    refusals = frozen_schema_violations(current, published)
    if refusals:
        for line in refusals:
            print(line, file=sys.stderr)
        print(
            "Снимок НЕ обновлён. Обход правила не состоит из одной команды: поднимите версию "
            "события, и снимок получит новую строку, а старая останется.",
            file=sys.stderr,
        )
        return 1

    merged = dict(published)
    merged.update(current)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(render_published(merged), encoding="utf-8")
    print(f"Снимок опубликованных схем записан: {PUBLISHED}, пар «тип + версия»: {len(merged)}")
    return 0


SELFTEST_EVENTS = """# Реестр для самопроверки.

notes.note_published:
  description: Хорошее событие: вопрос есть, поля обезличены.
  closes: [note_delay]
  version: 1
  fields:
    minutes_after_lesson:
      type: minutes
      description: сколько прошло от конца занятия до публикации

identity.self_account_revoked:
  description: >-
    Откат самостоятельности. Схема была опубликована с полем days_granted,
    и поле пропало без смены версии.
  closes: [rollback]
  version: 1
  fields:
    student_age_years:
      type: count
      description: сколько лет ученику

Notes.Bad:
  description: Имя не по форме.
  closes: [note_delay]
  version: 1
  fields:
    ok:
      type: count
      description: годное поле

sorcery.spell_cast:
  description: Издателя нет на карте.
  closes: [note_delay]
  version: 1
  fields:
    ok:
      type: count
      description: годное поле

content.material_opened:
  description: Поле именует человека и ещё одно — идентификатор.
  closes: [nobody_asked]
  version: 0
  fields:
    person_email:
      type: code
      description: почта читателя
    reader_id:
      type: reference
      description: кто открыл
    weird:
      type: freetext
      description: тип не из списка
    nodescription:
      type: count
      description:

matching.newcomer_shown:
  description: Событие без вопроса вовсе.
  closes: []
  version: 1
  fields:
    results_shown:
      type: count
      description: сколько репетиторов было в выдаче
"""

SELFTEST_QUESTIONS = """# Открытые продуктовые вопросы

| id | Величина | Вопрос | Сейчас | Чем закрывается | События |
| --- | --- | --- | --- | --- | --- |
| `note_delay` | `PDR_NOTE_DELAY` | через сколько появляется конспект | на глаз | данными | `notes.note_published` |
| `rollback` | | когда родители отдают управление | неизвестно | данными | `identity.self_account_revoked` |
| `lonely` | `PDR_LONELY` | число без события | на глаз | данными | |
| `by_law` | `PDR_BY_LAW` | что решает закон | правом | правом | `notes.note_published` |
| `phantom` | `PDR_PHANTOM` | величины нет в реестре | на глаз | данными | `notes.no_such_event` |
| `mystery` | | способ закрытия неизвестен | никак | гаданием | |
"""

SELFTEST_REGISTRY = """PDR_NOTE_DELAY:
  description: Задержка конспекта.
  kind: продуктовая
  owner: владелец продукта
  jurisdiction: нет
  awaits: NOTES
  breaks: Ничего.
  default: 60
  schema:
    type: integer
    minimum: 1
    maximum: 600

PDR_LONELY:
  description: Число без события.
  kind: продуктовая
  owner: владелец продукта
  jurisdiction: нет
  awaits: NOTES
  breaks: Ничего.
  default: 1
  schema:
    type: integer
    minimum: 1
    maximum: 2

PDR_BY_LAW:
  description: Решается правом.
  kind: продуктовая
  owner: владелец продукта
  jurisdiction: да
  awaits: IDENT
  breaks: Ничего.
  default: 1
  schema:
    type: integer
    minimum: 1
    maximum: 2

PDR_UNASKED:
  description: Продуктовое число, о котором никто не спросил.
  kind: продуктовая
  owner: владелец продукта
  jurisdiction: нет
  awaits: SCHED
  breaks: Ничего.
  default: 1
  schema:
    type: integer
    minimum: 1
    maximum: 2

PDR_TECHNICAL:
  description: Техническое число вопроса не требует.
  kind: техническая
  owner: тот, кто держит сервис
  jurisdiction: нет
  awaits: OBS
  breaks: Ничего.
  default: 1
  schema:
    type: integer
    minimum: 1
    maximum: 2
"""

SELFTEST_MAP = """# Карта контекстов

### identity — люди и роли

**Владеет:** ничем.

### notes — конспекты

**Владеет:** ничем.

### content — материалы

**Владеет:** ничем.

### matching — подбор

**Владеет:** ничем.
"""

SELFTEST_PUBLISHED = """# Снимок для самопроверки.

identity.self_account_revoked@1:
  days_granted: days
  student_age_years: count

notes.note_published@1:
  minutes_after_lesson: minutes
"""

SELFTEST_SCENARIOS = """TEST_F(ProductEventTest, EveryScenarioOfTheRegistryIsWritten) {
    EXPECT_TRUE(Record("notes.note_published", 1, Role::kTutor, {}));
    EXPECT_TRUE(Record("identity.self_account_revoked", 1, Role::kGuardian, {}));
    EXPECT_TRUE(Record("content.material_opened", 1, Role::kStudent, {}));
    EXPECT_TRUE(Record("Notes.Bad", 1, Role::kTutor, {}));
    EXPECT_TRUE(Record("sorcery.spell_cast", 1, Role::kSystem, {}));
}
"""

SELFTEST_FILES = {
    "configs/product-events.yaml": SELFTEST_EVENTS,
    "libs/pdr-observability/tests/product_event_test.cpp": SELFTEST_SCENARIOS,
    "configs/product-events.published.yaml": SELFTEST_PUBLISHED,
    "docs/product/open-questions.md": SELFTEST_QUESTIONS,
    "configs/dynamic/registry.yaml": SELFTEST_REGISTRY,
    "docs/architecture/context-map.md": SELFTEST_MAP,
}

SELFTEST_EXPECTED = (
    ("Notes.Bad", "не похоже на имя события"),
    ("sorcery.spell_cast", "которого нет на карте"),
    ("content.material_opened", "версия «0»"),
    ("person_email", "именует человека"),
    ("reader_id", "идентификатор"),
    ("weird", "закрытом списке"),
    ("nodescription", "без описания"),
    ("nobody_asked", "которого нет в docs/product/open-questions.md"),
    ("matching.newcomer_shown", "не закрывает ни одного вопроса"),
    ("lonely", "закрывается данными, а события у него нет"),
    ("by_law", "закрывается не данными"),
    ("mystery", "способ закрытия"),
    ("notes.no_such_event", "которого нет в configs/product-events.yaml"),
    ("PDR_PHANTOM", "которой нет в configs/dynamic/registry.yaml"),
    ("PDR_UNASKED", "нет вопроса"),
    ("days_granted", "пропало поле"),
    ("matching.newcomer_shown", "нет сценария в тестах"),
)

SELFTEST_CLEAN = ("PDR_TECHNICAL", "minutes_after_lesson")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, events = check(root)

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

        if events != 6:
            print(f"самопроверка: разобрано {events} событий вместо шести", file=sys.stderr)
            return 1

        refused = io.StringIO()
        with contextlib.redirect_stderr(refused), contextlib.redirect_stdout(io.StringIO()):
            refusal = update(root)
        if refusal == 0 or "пропало поле" not in refused.getvalue():
            print("самопроверка: снимок обновился, стерев поле опубликованной версии",
                  file=sys.stderr)
            return 1

        (root / EVENTS).write_text("  это не реестр\n", encoding="utf-8")
        broken, _ = check(root)
        if not any("разбор не понял" in line for line in broken):
            print("самопроверка: непонятный реестр прошёл мимо проверки", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 2} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Продуктовые события и вопросы (PDR-OBS-03).")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--update", action="store_true", help="записать снимок и выйти")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()
    if arguments.update:
        return update(arguments.root)

    violations, events = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Форма записи и правила — {EVENTS}", file=sys.stderr)
        return 1

    print(f"Продуктовых событий: {events}. Реестр вопросов, реестр событий и снимок сходятся.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
