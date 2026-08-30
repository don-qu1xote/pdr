#!/usr/bin/env python3
"""Уведомление опекуну о самостоятельном поступке подопечного не выключается.

Смысл возрастного порога — самостоятельность, а не тайна: подросток переносит
занятие без разрешения, а родитель узнаёт об этом фактом, а не расследованием.
Требование записано в PDR-IDENT-05 словами «ОБЯЗАТЕЛЬНО И НЕ ОТКЛЮЧАЕТСЯ», и без
машинной проверки оно продержится до первой задачи, в которой кто-нибудь добавит
настройку «не беспокоить».

Отключить его можно четырьмя способами, и все четыре ловятся здесь:

* завести условие в сценарии — настройку, уровень доступа опекуна, согласие.
  Поэтому в файле сценария запрещены упоминания настроек, уровней и согласий:
  у него есть ровно один вопрос — «кто опекун этого ученика»;
* перестать подписываться на событие в оповещениях. Поэтому проверяется, что
  подписка есть и что строка очереди уходит ИМЕННО опекуну;
* завести поступок и не отправить о нём событие. Поэтому каждое значение
  перечисления обязано встретиться в сценарии: разбор там полный, и поступок,
  которому не нашлось кода события, роняет проверку раньше, чем промолчит в
  проде;
* просочить в событие текст отзыва. Опекун видит, ЧТО отзыв написан, и не видит,
  что в нём: право высказаться без надзора и есть содержание порога. Поэтому в
  структуре события запрещены поля, в которые текст помещается ВООБЩЕ — любая
  строка, включая `std::string_view`: строковое поле однажды заполнят «заодно»,
  и заметят это, когда родитель прочитает то, чего читать не должен.

Слова, по которым узнаётся выключатель, подобраны по механизмам, которые в дереве
УЖЕ есть: настройки динамического конфига, уровни доступа опекуна, согласия.
Каждое из них — законный способ что-нибудь выключить, и ровно поэтому ни одного
из них не должно быть в сценарии уведомления: у него один вопрос.

Строковые типы, по которым узнаётся утечка текста: `std::string` копией,
`std::string_view` ссылкой, `char*` по старой памяти. Поступок передаётся кодом,
и ни один из них для кода не нужен.

Запуск:
    python3 scripts/check_guardian_notice.py
    python3 scripts/check_guardian_notice.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

ACTS = Path("libs/pdr-identity/src/identity/core/independent_act.hpp")
EVENT = Path("libs/pdr-events/include/events/identity/ward_acted_alone.hpp")
SCENARIO = Path("libs/pdr-identity/src/identity/application/notify_guardian_of_act.cpp")
DELIVERY = Path("libs/pdr-notifications/src/notifications/application/deliver_domain_events.cpp")

ENUM_VALUE = re.compile(r"^\s*(k[A-Z][A-Za-z0-9]*),\s*$", re.M)
LIST_VALUE = re.compile(r"IndependentAct::(k[A-Z][A-Za-z0-9]*)")
STRUCT_BODY = re.compile(r"struct\s+WardActedAlone\s+final\s*\{(.*?)\n\};", re.S)
FIELD = re.compile(r"^\s{4}([A-Za-z_][A-Za-z0-9_:<>\s]*?)\s+([a-z_][a-z0-9_]*)\s*(?:\{[^}]*\})?;",
                   re.M)

BOUNDARY = "kBoundary"

SWITCHES = (
    ("Settings", "настройка из динамического конфига"),
    ("dynamic_config", "то же самое"),
    ("GuardianScope", "уровень доступа опекуна"),
    ("Consent", "согласие"),
    ("enabled", "признак «включено»"),
    ("Enabled", "признак «включено»"),
)

TEXTUAL_TYPES = ("std::string", "std::string_view", "char*", "std::vector")
TEXTUAL_NAMES = ("text", "body", "comment", "review", "message", "note")


def read(root: Path, path: Path) -> str:
    target = root / path
    return target.read_text(encoding="utf-8") if target.is_file() else ""


def acts_of(text: str) -> list[str]:
    """Значения перечисления поступков — без границы списка."""
    head = text.split("enum class IndependentAct", 1)
    if len(head) < 2:
        return []
    body = head[1].split("};", 1)[0]
    return [value for value in ENUM_VALUE.findall(body) if value != BOUNDARY]


def check_event(text: str) -> list[str]:
    """В событии нет поля, в которое поместится текст отзыва."""
    if not text:
        return [f"{EVENT}: события о самостоятельном поступке нет вовсе"]

    body = STRUCT_BODY.search(text)
    if not body:
        return [f"{EVENT}: структуры WardActedAlone не нашлось — разбор не понял файл"]

    violations = []
    for kind, name in FIELD.findall(body.group(1)):
        spelling = " ".join(kind.split())
        if any(textual in spelling for textual in TEXTUAL_TYPES):
            violations.append(
                f"{EVENT}: поле «{spelling} {name}» вмещает текст. Опекун видит, ЧТО отзыв "
                f"написан, и не видит, что в нём: уберите поле, а не заполняйте его пустотой"
            )
        if name in TEXTUAL_NAMES:
            violations.append(
                f"{EVENT}: поле «{name}» названо так, будто в нём содержание поступка. "
                f"В событие уходит код из закрытого списка, а не то, что ученик написал"
            )
    return violations


def check_scenario(text: str, acts: Sequence[str]) -> list[str]:
    """У сценария нет выключателя и нет выбора между поступками."""
    if not text:
        return [f"{SCENARIO}: сценария уведомления нет вовсе"]

    violations = []
    for word, what in SWITCHES:
        if word in text:
            violations.append(
                f"{SCENARIO}: в сценарии появилось «{word}» — {what}. Уведомление опекуну не "
                f"отключается ничем: у сценария один вопрос, «кто опекун этого ученика»"
            )

    mentioned = set(LIST_VALUE.findall(text))
    for act in acts:
        if act not in mentioned:
            violations.append(
                f"{SCENARIO}: поступок «{act}» заведён, а кода события ему не нашлось. "
                f"О нём опекун не узнает, и заметит это он, а не проверка"
            )

    if "GuardiansOf" not in text:
        violations.append(
            f"{SCENARIO}: сценарий не спрашивает опекунов ученика. Сообщать некому, пока не "
            f"спросили, кто это"
        )
    return violations


def check_delivery(text: str) -> list[str]:
    """Оповещения подписаны на событие и шлют строку ИМЕННО опекуну."""
    if not text:
        return [f"{DELIVERY}: доставки оповещений нет вовсе"]

    violations = []
    if "WardActedAlone" not in text:
        violations.append(
            f"{DELIVERY}: на событие о самостоятельном поступке никто не подписан. Событие "
            f"без подписчика — это уведомление, которого не будет"
        )
    elif "event.guardian" not in text:
        violations.append(
            f"{DELIVERY}: подписка есть, а строка очереди уходит не опекуну. Сообщают именно "
            f"ему: он и есть тот, кто должен знать"
        )
    return violations


def check(root: Path) -> tuple[list[str], int]:
    acts = acts_of(read(root, ACTS))
    if not acts:
        return ([f"{ACTS}: списка самостоятельных поступков нет — проверять нечего"], 0)

    violations = check_event(read(root, EVENT))
    violations.extend(check_scenario(read(root, SCENARIO), acts))
    violations.extend(check_delivery(read(root, DELIVERY)))
    return violations, len(acts)


SELFTEST_ACTS = """
enum class IndependentAct : std::uint8_t {
    kLessonRescheduled,
    kLessonCancelled,
    kReviewWritten,

    kBoundary,
};
"""

SELFTEST_EVENT = """
struct WardActedAlone final {
    Envelope envelope;
    core::PersonId guardian;
    core::PersonId student;
    WardAct act;
    std::string text;
};
"""

SELFTEST_SCENARIO = """
WardAct Published(IndependentAct act) noexcept {
    switch (act) {
        case IndependentAct::kLessonRescheduled:
            return WardAct::kLessonRescheduled;
        case IndependentAct::kLessonCancelled:
            return WardAct::kLessonCancelled;
    }
}

core::Result<int> NotifyGuardianOfAct::Execute(const NotifyGuardianOfActRequest& request) const {
    if (!settings_.Enabled()) {
        return 0;
    }
    for (const auto& guardian : guardianships_.GuardiansOf(request.tenant, request.student)) {
        bus_.Publish(WardActedAlone{});
    }
    return 1;
}
"""

SELFTEST_DELIVERY = """
void DeliverDomainEvents::SubscribeTo(events::Bus& bus) {
    bus.Subscribe<LessonBooked>([](const LessonBooked&) {});
}
"""

SELFTEST_FILES = {
    ACTS: SELFTEST_ACTS,
    EVENT: SELFTEST_EVENT,
    SCENARIO: SELFTEST_SCENARIO,
    DELIVERY: SELFTEST_DELIVERY,
}

SELFTEST_EXPECTED = (
    "вмещает текст",
    "названо так, будто",
    "признак «включено»",
    "кода события ему не нашлось",
    "никто не подписан",
)


def selftest() -> int:
    """Отрицательные случаи: каждый способ выключить уведомление обязан ловиться."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path, content in SELFTEST_FILES.items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, acts = check(root)

        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if acts != 3:
            print(f"самопроверка: разобрано {acts} поступков вместо трёх", file=sys.stderr)
            return 1

        (root / EVENT).write_text(
            SELFTEST_EVENT.replace("    std::string text;\n", ""), encoding="utf-8"
        )
        clean, _ = check(root)
        if any("вмещает текст" in line for line in clean):
            print("самопроверка: событие без текста объявлено нарушением", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        description="Уведомление опекуну о поступке подопечного не отключается."
    )
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, acts = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Правило — docs/adr/"
              f"0018-age-thresholds-are-a-legal-question.md", file=sys.stderr)
        return 1

    print(f"Самостоятельных поступков: {acts}. О каждом опекун узнаёт, и выключателя нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
