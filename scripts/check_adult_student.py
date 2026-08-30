#!/usr/bin/env python3
"""Взрослый ученик без опекуна проходит весь путь: опека нигде не обязательна.

Репетиторы нужны не только школьникам: языки, вуз, переподготовка. У такого
ученика опекуна нет ВОВСЕ, и модель не должна делать вид, что он потерялся.
Требование записано в PDR-IDENT-07 и в ADR-0020; без машинной проверки оно
держится ровно до первой задачи, в которой кто-нибудь спросит опекуна «на всякий
случай» — и обнаружит это на демонстрации, когда взрослый ученик упрётся в
пустоту там, где у ребёнка есть родитель.

Опека спрашивается ровно в шести сценариях, и все шесть — про саму опеку: её
устанавливают, отзывают, о ней сообщают, по ней выдают доступ. Список закрыт
здесь, и седьмой файл, решивший заглянуть в опеку, роняет проверку.

Ловятся шесть способов сделать опекуна обязательным:

* спросить опеку в сценарии, который не про неё, — в записи, оплате, отзыве,
  аналитике, регистрации. Поэтому список файлов, которым порт опеки доступен,
  закрыт, а пути взрослого перечислены отдельно и проверяются поимённо;
* спросить её в политике. Политики решают по собранному Subject и не ходят в
  хранилища вовсе: политика, знающая слово «опека», — это отказ взрослому,
  написанный за месяц до того, как его увидят;
* завести третью роль — «наблюдатель», «плательщик». Роли перечислены в домене
  закрытым списком, и он сверяется с известным: наблюдение — это основание
  доступа, а не новое место человека в аренде;
* завести вторую таблицу наблюдателей. Второй набор уровней, второй отзыв,
  второй журнал — и через полгода они разойдутся. Поэтому основание живёт
  колонкой в согласии, и колонка проверяется на месте;
* открыть плательщику содержание занятий. «Деньги не дают права смотреть»
  держится двумя замками — воротами в домене и ограничением схемы; проверяется,
  что стоят оба, потому что снять один поодиночке слишком легко;
* потребовать документ о возрасте. Возраст взрослого — заявительный: продукт не
  просит паспорт, не хранит его номер и не заводит проверку подлинности.

Запуск:
    python3 scripts/check_adult_student.py
    python3 scripts/check_adult_student.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

IDENTITY = Path("libs/pdr-identity/src/identity")
APPLICATION = IDENTITY / "application"
POLICIES = APPLICATION / "policies"
PORT = "ports/guardianship_repository.hpp"

MEMBERSHIP = IDENTITY / "core/membership.hpp"
CONSENT = IDENTITY / "core/guardian_consent.hpp"
MIGRATIONS = Path("db/migrations")

SOURCES = ("*.hpp", "*.cpp")

GUARDIANSHIP = re.compile(r"guardianship", re.I)
LITERAL_OR_COMMENT = re.compile(
    r"\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/", re.S
)
ROLE_VALUE = re.compile(r"^\s*(k[A-Z][A-Za-z0-9]*),\s*$", re.M)
CREATE_TABLE = re.compile(r"^create table (\w+)", re.M)

KNOWN_ROLES = ("kOwner", "kTutor", "kStudent", "kGuardian")

ABOUT_GUARDIANSHIP = (
    APPLICATION / "announce_coming_of_age.hpp",
    APPLICATION / "announce_guardian_handover.hpp",
    APPLICATION / "enrol_child.hpp",
    APPLICATION / "give_consent.hpp",
    APPLICATION / "grant_guardian_scope.hpp",
    APPLICATION / "notify_guardian_of_act.hpp",
    APPLICATION / "revoke_guardianship.hpp",
    POLICIES / "subject_builder.hpp",
)

PATHS_OF_THE_ADULT = (
    ("завести ученика", (APPLICATION / "register_on_my_own.cpp",
                         APPLICATION / "register_on_my_own.hpp",
                         APPLICATION / "accept_invitation.cpp",
                         APPLICATION / "accept_invitation.hpp")),
    ("записаться на занятие", (Path("libs/pdr-scheduling/src/scheduling/application/"
                                    "book_lesson.cpp"),
                               Path("libs/pdr-scheduling/src/scheduling/application/"
                                    "book_lesson.hpp"),
                               POLICIES / "scheduling_policy.cpp")),
    ("заплатить", (Path("libs/pdr-billing/src/billing/application/quote_lesson_package.cpp"),
                   POLICIES / "billing_policy.cpp")),
    ("написать отзыв", (POLICIES / "review_policy.cpp",)),
    ("смотреть аналитику", (POLICIES / "progress_policy.cpp",
                            APPLICATION / "show_access_journal.cpp",
                            APPLICATION / "show_access_journal.hpp")),
)

PAPERS = (
    ("passport", "паспорт"),
    ("birth_certificate", "свидетельство о рождении"),
    ("id_card", "удостоверение"),
    ("identity_document", "документ, удостоверяющий личность"),
    ("document_number", "номер документа"),
    ("age_verification", "проверка возраста"),
    ("verify_age", "проверка возраста"),
    ("proof_of_age", "подтверждение возраста"),
    ("notarized", "нотариальное заверение"),
)

MONEY_GATE = "MayCarry"
MONEY_CONSTRAINT = "identity_guardian_consent_money_is_not_sight"
BASIS_COLUMN = "basis"
SECOND_TABLE = ("observer", "watcher", "payer", "supervis")


def read(root: Path, path: Path) -> str:
    target = root / path
    return target.read_text(encoding="utf-8") if target.is_file() else ""


def code_of(text: str) -> str:
    """Текст без комментариев: у хранилища спрашивает код, а не рассказ о нём.

    Иначе doc-комментарий «сюда мы за опекой не ходим» считался бы походом за
    опекой, и проверка требовала бы молчать о том, чего не делаешь.
    """
    return LITERAL_OR_COMMENT.sub(
        lambda found: found.group(0) if found.group(0)[0] in "\"'" else " ", text
    )


def sources_of(root: Path, directory: Path) -> list[Path]:
    """Исходники поддерева — путями от корня дерева, а не от диска."""
    found: list[Path] = []
    for pattern in SOURCES:
        found.extend(
            path.relative_to(root) for path in sorted((root / directory).rglob(pattern))
        )
    return sorted(set(found))


def check_port(root: Path) -> list[str]:
    """Порт опеки виден закрытому списку сценариев, и все они — про опеку."""
    violations = []
    allowed = set(ABOUT_GUARDIANSHIP)

    for path in sources_of(root, APPLICATION):
        if PORT not in code_of(read(root, path)):
            continue
        if path not in allowed:
            violations.append(
                f"{path}: сценарий спрашивает опеку. У взрослого ученика опекуна нет ВОВСЕ: "
                f"либо сценарий про опеку и его место в списке проверки, либо опека здесь "
                f"лишняя"
            )

    for path in ABOUT_GUARDIANSHIP:
        if not (root / path).is_file():
            violations.append(
                f"{path}: сценарий из списка «про опеку» исчез. Список закрыт вручную и "
                f"сверяется с деревом: разъехавшись, он перестаёт что-либо значить"
            )
    return violations


def check_policies(root: Path) -> list[str]:
    """Политики решают по собранному Subject и слова «опека» не знают."""
    violations = []
    for path in sources_of(root, POLICIES):
        if path.stem == "subject_builder":
            continue
        if GUARDIANSHIP.search(code_of(read(root, path))):
            violations.append(
                f"{path}: политика знает про опеку. Решение принимается по собранному "
                f"Subject: политика, которая ходит за опекой, отказывает взрослому — и "
                f"выяснится это на демонстрации"
            )
    return violations


def check_paths(root: Path) -> list[str]:
    """Пути взрослого — поимённо: ни один не упирается в опеку."""
    violations = []
    for what, paths in PATHS_OF_THE_ADULT:
        for path in paths:
            text = read(root, path)
            if not text:
                violations.append(
                    f"{path}: путь «{what}» проверять не на чем — файла нет. Пути взрослого "
                    f"перечислены вручную и обязаны существовать"
                )
            elif GUARDIANSHIP.search(code_of(text)):
                violations.append(
                    f"{path}: чтобы «{what}», спрашивают опеку. Взрослый ученик — основной "
                    f"случай, а не исключение: у него опекуна нет и не будет"
                )
    return violations


def check_roles(root: Path) -> list[str]:
    """Третьей роли нет: наблюдение — основание доступа, а не место в аренде."""
    text = read(root, MEMBERSHIP)
    if not text:
        return [f"{MEMBERSHIP}: списка ролей нет — проверять нечего"]

    head = text.split("enum class Role", 1)
    if len(head) < 2:
        return [f"{MEMBERSHIP}: перечисления Role не нашлось — разбор не понял файл"]

    roles = tuple(ROLE_VALUE.findall(head[1].split("};", 1)[0]))
    if roles == KNOWN_ROLES:
        return []
    return [
        f"{MEMBERSHIP}: роли стали {list(roles)} вместо {list(KNOWN_ROLES)}. Наблюдатель и "
        f"плательщик — это ОСНОВАНИЕ доступа, а не третья роль: роль завела бы себе вторые "
        f"уровни, второй отзыв и второй журнал"
    ]


def check_one_mechanism(root: Path) -> list[str]:
    """Основание — колонка в согласии, а не вторая таблица рядом."""
    violations = []
    schema = "\n".join(
        read(root, path) for path in sorted((root / MIGRATIONS).glob("*.sql"))
    ) if (root / MIGRATIONS).is_dir() else ""

    for table in CREATE_TABLE.findall(schema):
        for word in SECOND_TABLE:
            if word in table:
                violations.append(
                    f"{MIGRATIONS}: заведена таблица «{table}». Опека и наблюдение — ОДИН "
                    f"механизм: вторая таблица означает второй набор уровней и второй "
                    f"отзыв, и через полгода они разойдутся"
                )

    if f"identity_guardian_consent add column {BASIS_COLUMN}" not in schema:
        violations.append(
            f"{MIGRATIONS}: у согласия нет колонки «{BASIS_COLUMN}». Без основания доступ "
            f"опекуна и доступ названного взрослым наблюдателя неразличимы"
        )
    return violations


def check_money(root: Path) -> list[str]:
    """Деньги не дают права смотреть — воротами в домене И ограничением схемы."""
    violations = []
    if MONEY_GATE not in read(root, CONSENT):
        violations.append(
            f"{CONSENT}: из домена убрали ворота {MONEY_GATE}. Плательщик получает деньги и "
            f"только деньги: без ворот согласие «плательщик видит записи» соберётся"
        )

    schema = "\n".join(
        read(root, path) for path in sorted((root / MIGRATIONS).glob("*.sql"))
    ) if (root / MIGRATIONS).is_dir() else ""
    if MONEY_CONSTRAINT not in schema:
        violations.append(
            f"{MIGRATIONS}: из схемы убрали ограничение {MONEY_CONSTRAINT}. Второй замок "
            f"нужен именно потому, что первый однажды обойдут запросом мимо домена"
        )
    return violations


def check_papers(root: Path) -> list[str]:
    """Возраст взрослого — заявительный: паспорта продукт не просит."""
    violations = []
    places = sources_of(root, IDENTITY)
    if (root / MIGRATIONS).is_dir():
        places.extend(path.relative_to(root) for path in sorted((root / MIGRATIONS).glob("*.sql")))

    for path in places:
        text = read(root, path).lower()
        for word, what in PAPERS:
            if word in text:
                violations.append(
                    f"{path}: появилось «{word}» — {what}. Возраст при регистрации "
                    f"заявительный: продукт верит человеку на слово и документов не просит"
                )
    return violations


def check(root: Path) -> tuple[list[str], int]:
    violations = check_port(root)
    violations.extend(check_policies(root))
    violations.extend(check_paths(root))
    violations.extend(check_roles(root))
    violations.extend(check_one_mechanism(root))
    violations.extend(check_money(root))
    violations.extend(check_papers(root))
    return violations, sum(len(paths) for _, paths in PATHS_OF_THE_ADULT)


SELFTEST_TREE = {
    APPLICATION / "register_on_my_own.hpp": "class RegisterOnMyOwn final {};\n",
    APPLICATION / "register_on_my_own.cpp": "namespace pdr::identity {}\n",
    APPLICATION / "accept_invitation.hpp": "class AcceptInvitation final {};\n",
    APPLICATION / "accept_invitation.cpp": "namespace pdr::identity {}\n",
    APPLICATION / "show_access_journal.hpp": "class ShowAccessJournal final {};\n",
    APPLICATION / "show_access_journal.cpp": "namespace pdr::identity {}\n",
    APPLICATION / "quote_lesson.cpp": f'#include "identity/{PORT}"\n',
    POLICIES / "billing_policy.cpp": "const ports::GuardianshipRepository& guardianships_;\n",
    POLICIES / "scheduling_policy.cpp": "namespace pdr::identity {}\n",
    POLICIES / "review_policy.cpp": "namespace pdr::identity {}\n",
    POLICIES / "progress_policy.cpp":
        "/// Опеку здесь не спрашивают: guardianship — не наше дело.\n",
    Path("libs/pdr-scheduling/src/scheduling/application/book_lesson.hpp"):
        "class BookLesson final {};\n",
    Path("libs/pdr-scheduling/src/scheduling/application/book_lesson.cpp"):
        "const auto link = guardianships_.FindActive(tenant, actor, student);\n",
    Path("libs/pdr-billing/src/billing/application/quote_lesson_package.cpp"):
        "namespace pdr::billing {}\n",
    MEMBERSHIP: "enum class Role : std::uint8_t {\n    kOwner,\n    kTutor,\n"
                "    kStudent,\n    kGuardian,\n    kObserver,\n};\n",
    CONSENT: "class GuardianConsent final {\n    ConsentBasis basis_;\n};\n"
             "std::string passport_number;\n",
    MIGRATIONS / "V007__guardian_access.sql": "create table identity_guardian_consent (\n);\n",
    MIGRATIONS / "V009__consent_basis.sql": "create table identity_observer (\n);\n",
}

SELFTEST_EXPECTED = (
    "сценарий спрашивает опеку",
    "политика знает про опеку",
    "спрашивают опеку",
    "роли стали",
    "заведена таблица",
    "нет колонки",
    "убрали ворота",
    "убрали ограничение",
    "паспорт",
)

SELFTEST_CLEAN = (
    ((POLICIES / "billing_policy.cpp"), "namespace pdr::identity {}\n",
     "политика знает про опеку"),
    ((POLICIES / "progress_policy.cpp"), "namespace pdr::identity {}\n",
     "смотреть аналитику"),
)


def selftest() -> int:
    """Отрицательные случаи: каждый способ вернуть обязательного опекуна ловится."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path, content in SELFTEST_TREE.items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")
        for path in ABOUT_GUARDIANSHIP:
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(f'#include "identity/{PORT}"\n', encoding="utf-8")

        violations, _ = check(root)
        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for path, content, fragment in SELFTEST_CLEAN:
            (root / path).write_text(content, encoding="utf-8")
        clean, _ = check(root)
        for _, _, fragment in SELFTEST_CLEAN:
            if any(fragment in line for line in clean):
                print(f"самопроверка: чистый файл объявлен нарушением «{fragment}»",
                      file=sys.stderr)
                return 1

        (root / ABOUT_GUARDIANSHIP[-1]).unlink()
        (root / POLICIES / "review_policy.cpp").unlink()
        shrunk, _ = check(root)
        for fragment in ("исчез", "проверять не на чем"):
            if not any(fragment in line for line in shrunk):
                print(f"самопроверка: списки разъехались с деревом молча — «{fragment}»",
                      file=sys.stderr)
                return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 2} нарушений найдено там, где "
          f"они есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        description="Взрослый ученик без опекуна проходит весь путь."
    )
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, paths = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Правило — docs/adr/"
              f"0020-adult-student-is-the-main-case.md", file=sys.stderr)
        return 1

    print(f"Опека спрашивается в {len(ABOUT_GUARDIANSHIP)} сценариях, и все они про неё. "
          f"Путей взрослого проверено: {paths}; ни один не требует опекуна.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
