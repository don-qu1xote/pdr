#!/usr/bin/env python3
"""Реестр секретов: код и реестр не расходятся (PDR-SEC-02).

Мелкая задача, ловящая целый класс отказов: сервис, поднявшийся с пустым
секретом и работающий «как будто нормально». Процесс жив, метрика зелёная,
платежи не уходят — и узнаёт об этом человек, у которого не прошёл платёж.

Держится это на трёх вещах сразу, и любая из них поодиночке ничего не стоит:
список секретов, отказ подниматься без любого из них и невозможность напечатать
секрет. Здесь проверяется первое и охраняется третье.

Проверяется:

* каждая запись configs/secrets_registry.yaml названа в core::kEverySecret, и
  наоборот — запись без секрета в коде роняет сборку, чтобы реестр не зарастал
  мёртвыми строками;
* у записи есть все обязательные поля: description, purpose, source;
* purpose — из закрытого списка назначений, объявленного в core/secrets.hpp, и
  совпадает с назначением в коде;
* «где взять» непусто и не отговорка: «у того, кто знает» — не источник;
* ЗНАЧЕНИЙ У ЗАПИСЕЙ НЕТ. Ни поля default, ни поля value, ни примера значения:
  умолчание вида "change-me" опаснее отсутствия, потому что доживает до прода и
  работает. Отсутствие роняет старт сразу;
* awaits — область из закрытого списка CONTRIBUTING.md, и она совпадает с
  ожиданием в коде. Ждать вечно у записи не получится: как только читатель
  появляется, поле снимается тем же изменением;
* секрет не заводится в реестре динамических значений: конфиг меняют без
  выкатки, а его значения видны в журнале изменений;
* `SecretString` не обзавёлся выводом: удалённый шаблонный operator<< обязан
  оставаться удалённым, иначе «секреты не печатаются» перестаёт быть свойством
  типа и становится обещанием.

YAML разбирается строгим подмножеством, как и реестр динамических значений:
чего разбор не понял, он называет вслух и роняет проверку.

Запуск:
    python3 scripts/check_secrets_registry.py
    python3 scripts/check_secrets_registry.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

REGISTRY = Path("configs/secrets_registry.yaml")
MODEL = Path("libs/pdr-core/src/core/secrets.hpp")
PURPOSES = Path("libs/pdr-core/src/core/secrets.cpp")
SECRET_STRING = Path("libs/pdr-core/src/core/secret_string.hpp")
ADAPTER = Path("libs/pdr-core/src/infrastructure/secdist/secdist_secret_source.hpp")
DYNAMIC = Path("configs/dynamic/registry.yaml")
AREAS = Path("CONTRIBUTING.md")

REQUIRED_FIELDS = ("description", "purpose", "source")
FORBIDDEN_FIELDS = ("default", "value", "example", "sample")

ENTRY = re.compile(r"^([a-z][a-z0-9_]*):\s*$", re.M)
FIELD = re.compile(r"^  ([a-z][a-z0-9_]*):\s*(.*)$")
SPEC = re.compile(
    r'SecretSpec\{\s*"([a-z][a-z0-9_]*)"\s*,\s*SecretPurpose::(k\w+)\s*,\s*"([A-Z]*)"\s*\}'
)
PURPOSE_CODE = re.compile(r'case SecretPurpose::(k\w+):\s*\n\s*return "([a-z_]+)";')
AREA_ROW = re.compile(r"^\|\s*`([A-Z]+)`\s*\|", re.M)
DELETED_OUTPUT = re.compile(
    r"template<class \w+>\s*\n\s*\w+&& operator<<\([^)]*const SecretString&[^)]*\)\s*=\s*delete;"
)

VAGUE = ("у того, кто знает", "спросить", "todo", "уточнить", "не знаю")


def read(root: Path, path: Path) -> str:
    target = root / path
    return target.read_text(encoding="utf-8") if target.is_file() else ""


def entries_of(text: str) -> dict[str, dict[str, str]]:
    """Записи реестра: имя и его поля.

    Свёрнутые скаляры (`>-`) разворачиваются: без этого у поля «source»
    значением было бы само «>-», непустое и всегда осмысленное, — и проверка на
    отговорку не смотрела бы ни на что.
    """
    found: dict[str, dict[str, str]] = {}
    current: str | None = None
    folded: tuple[str, list[str]] | None = None

    def close() -> None:
        nonlocal folded
        if folded is not None and current is not None:
            name, lines = folded
            found[current][name] = " ".join(line.strip() for line in lines).strip()
        folded = None

    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue

        if folded is not None:
            if not line.strip():
                continue
            if line.startswith("    "):
                folded[1].append(line)
                continue
            close()

        if not line.strip():
            continue

        top = ENTRY.match(line)
        if top:
            current = top.group(1)
            found[current] = {}
            continue

        field = FIELD.match(line)
        if field and current is not None:
            name, value = field.group(1), field.group(2).strip()
            if value in (">-", ">", "|", "|-"):
                folded = (name, [])
                found[current][name] = ""
            else:
                found[current][name] = value

    close()
    return found


def purposes_of(text: str) -> dict[str, str]:
    """{kДлинноеИмя: слово-в-реестре} — из switch, а не из второго списка."""
    return {name: word for name, word in PURPOSE_CODE.findall(text)}


def check_registry(root: Path, areas: set[str], words: set[str]) -> tuple[list[str], dict]:
    text = read(root, REGISTRY)
    if not text:
        return ([f"{REGISTRY}: реестра секретов нет вовсе"], {})

    violations = []
    entries = entries_of(text)
    if not entries:
        return ([f"{REGISTRY}: ни одной записи не разобралось"], {})

    for name, fields in sorted(entries.items()):
        for field in REQUIRED_FIELDS:
            if not fields.get(field):
                violations.append(f"{REGISTRY}: у секрета «{name}» нет поля {field}")

        for field in FORBIDDEN_FIELDS:
            if field in fields:
                violations.append(
                    f"{REGISTRY}: у секрета «{name}» есть поле {field}. Значений по умолчанию у "
                    f"секретов не существует: умолчание доживает до прода и работает, а "
                    f"отсутствие роняет старт сразу"
                )

        purpose = fields.get("purpose", "")
        if purpose and purpose not in words:
            violations.append(
                f"{REGISTRY}: у секрета «{name}» назначение «{purpose}», которого нет в "
                f"core/secrets.hpp. Список назначений закрыт"
            )

        source = fields.get("source", "").lower()
        for vague in VAGUE:
            if vague in source:
                violations.append(
                    f"{REGISTRY}: у секрета «{name}» вместо источника отговорка «{vague}». "
                    f"«Где взять» — это кабинет, файл или человек, а не направление поиска"
                )

        awaits = fields.get("awaits", "")
        if awaits and awaits not in areas:
            violations.append(
                f"{REGISTRY}: у секрета «{name}» ожидание «{awaits}», которого нет в списке "
                f"областей CONTRIBUTING.md"
            )

    return violations, entries


def check_code(root: Path, entries: dict, words: dict[str, str]) -> list[str]:
    text = read(root, MODEL)
    if not text:
        return [f"{MODEL}: списка секретов в коде нет вовсе"]

    violations = []
    specs = {name: (purpose, awaits) for name, purpose, awaits in SPEC.findall(text)}
    if not specs:
        return [f"{MODEL}: ни одного SecretSpec не разобралось — разбор не понял файл"]

    for name in sorted(set(entries) - set(specs)):
        violations.append(
            f"{REGISTRY}: секрет «{name}» назван в реестре, а в core::kEverySecret его нет. "
            f"Реестр, о котором код не знает, ничего не проверяет"
        )
    for name in sorted(set(specs) - set(entries)):
        violations.append(
            f"{MODEL}: секрет «{name}» есть в коде, а в реестре его нет. Спрашивать секрет, о "
            f"котором нигде не сказано, откуда он берётся, — способ получить «change-me»"
        )

    for name in sorted(set(specs) & set(entries)):
        purpose, awaits = specs[name]
        expected = words.get(purpose, "")
        if expected != entries[name].get("purpose"):
            violations.append(
                f"{MODEL}: у секрета «{name}» назначение {purpose} («{expected}»), а в реестре "
                f"«{entries[name].get('purpose')}»"
            )
        if awaits != entries[name].get("awaits", ""):
            violations.append(
                f"{MODEL}: у секрета «{name}» ожидание «{awaits}», а в реестре "
                f"«{entries[name].get('awaits', '')}». Как только читатель появляется, поле "
                f"снимается в обоих местах тем же изменением"
            )

    return violations


def check_separation(root: Path, entries: dict) -> list[str]:
    """Секрет не живёт в динамическом конфиге, а вывод его остаётся удалённым."""
    violations = []

    dynamic = read(root, DYNAMIC)
    for name in sorted(entries):
        if name.upper() in dynamic or name in dynamic:
            violations.append(
                f"{DYNAMIC}: секрет «{name}» попал в реестр динамических значений. Там "
                f"продуктовые числа: их меняют без выкатки, и они видны в журнале изменений"
            )

    if not read(root, ADAPTER):
        violations.append(
            f"{ADAPTER}: настоящего источника секретов нет. Каталог назван «secdist», а не "
            f"«secrets», намеренно: .gitignore прячет любой каталог с именем secrets, и "
            f"адаптер молча не попал бы в историю"
        )

    text = read(root, SECRET_STRING)
    if not text:
        violations.append(f"{SECRET_STRING}: типа секрета нет вовсе")
    elif not DELETED_OUTPUT.search(text):
        violations.append(
            f"{SECRET_STRING}: у SecretString нет удалённого шаблонного operator<<. Без него "
            f"«секреты не печатаются» — обещание, а не свойство типа: журнал userver не "
            f"std::ostream, и перегрузка для потока его не касается"
        )

    return violations


def check(root: Path) -> tuple[list[str], int, int]:
    areas = set(AREA_ROW.findall(read(root, AREAS)))
    words = purposes_of(read(root, PURPOSES))

    violations, entries = check_registry(root, areas, set(words.values()))
    if entries:
        violations.extend(check_code(root, entries, words))
        violations.extend(check_separation(root, entries))

    now = sum(1 for fields in entries.values() if not fields.get("awaits"))
    return violations, len(entries), now


SELFTEST_REGISTRY = """
postgres_dsn:
  description: >-
    Строка подключения.
  purpose: database
  source: >-
    Пароль роли pdr в вашей установке.

yookassa_secret_key:
  description: >-
    Ключ провайдера.
  purpose: payment_provider
  awaits: BILL
  default: change-me
  source: >-
    Спросить у того, кто знает.

livekit_api_key:
  description: >-
    Ключ видеосервера.
  purpose: свой_вариант
  awaits: ВИДЕО
  source: >-
    Конфигурация своего LiveKit.

forgotten_in_code:
  description: >-
    Запись, о которой код не знает.
  purpose: database
  source: >-
    Личный кабинет.
"""

SELFTEST_MODEL = """
inline constexpr std::array<SecretSpec, 3> kEverySecret{
    SecretSpec{"postgres_dsn", SecretPurpose::kDatabase, ""},
    SecretSpec{"yookassa_secret_key", SecretPurpose::kWebhookSigning, ""},
    SecretSpec{"only_in_code", SecretPurpose::kDatabase, ""},
};
"""

SELFTEST_PURPOSES = """
std::string_view Name(SecretPurpose purpose) noexcept {
    switch (purpose) {
        case SecretPurpose::kDatabase:
            return "database";
        case SecretPurpose::kPaymentProvider:
            return "payment_provider";
        case SecretPurpose::kWebhookSigning:
            return "webhook_signing";
    }
}
"""

SELFTEST_AREAS = "| `BILL` | деньги |\n| `VIDEO` | видеозанятие |\n"

SELFTEST_DYNAMIC = "POSTGRES_DSN:\n  description: величина\n"

SELFTEST_SECRET_STRING = "class SecretString final {\n};\n"

SELFTEST_ADAPTER = "class SecdistSecretSource final {\n};\n"

SELFTEST_TREE = {
    REGISTRY: SELFTEST_REGISTRY,
    MODEL: SELFTEST_MODEL,
    PURPOSES: SELFTEST_PURPOSES,
    AREAS: SELFTEST_AREAS,
    DYNAMIC: SELFTEST_DYNAMIC,
    SECRET_STRING: SELFTEST_SECRET_STRING,
    ADAPTER: SELFTEST_ADAPTER,
}

SELFTEST_EXPECTED = (
    "есть поле default",
    "вместо источника отговорка",
    "назначение «свой_вариант»",
    "ожидание «ВИДЕО»",
    "в core::kEverySecret его нет",
    "в реестре его нет",
    "а в реестре «payment_provider»",
    "попал в реестр динамических значений",
    "нет удалённого шаблонного operator<<",
)


def selftest() -> int:
    """Отрицательные случаи: каждый способ развести реестр с кодом ловится."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path, content in SELFTEST_TREE.items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, entries, _ = check(root)
        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if entries != 4:
            print(f"самопроверка: разобрано {entries} записей вместо четырёх", file=sys.stderr)
            return 1

        (root / SECRET_STRING).write_text(
            "template<class Sink>\nSink&& operator<<(Sink&& sink, const SecretString&) = delete;\n",
            encoding="utf-8",
        )
        clean, _, _ = check(root)
        if any("удалённого шаблонного" in line for line in clean):
            print("самопроверка: удалённый вывод объявлен нарушением", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Реестр секретов и код не расходятся.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, entries, now = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Реестр — {REGISTRY}", file=sys.stderr)
        return 1

    print(f"Секретов в реестре: {entries}, обязательных при старте сейчас: {now}. "
          f"Значений по умолчанию нет ни у одного.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
