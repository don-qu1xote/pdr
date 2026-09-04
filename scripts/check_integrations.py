#!/usr/bin/env python3
"""Внешние интеграции: чужой API — украшение, а не несущая конструкция.

В проекте six-feat неофициальный API сделали обязательным источником данных, и
удаление стоило 182 файла (ADR-0014). Дорогим его сделало не «сломалось», а
решение: несущая роль у сервиса без обязательств плюс чужие идентификаторы,
разложенные по своим таблицам.

Проверяется реестр docs/architecture/integrations.md и то, что за каждой его
строкой стоит:

* неофициальный API не бывает несущим — это то самое сочетание;
* ADR интеграции существует и в нём заполнен раздел «Как это выключить»:
  какие таблицы осиротеют, какие функции исчезнут, сколько файлов затронуто.
  «Неизвестно» означает, что цена удаления неизвестна, — интеграцию делать рано;
* у порта минимум две реализации: настоящая и заглушка;
* флаг отключения — ключ в PDR_INTEGRATIONS, а не правка кода;
* набор «сервиса нет навсегда» для интеграции инстанцирован;
* чужой идентификатор не стал первичным ключом нашей таблицы.

Последняя проверка работает и при пустом реестре: она смотрит на миграции, а не
на интеграции. Остальные начнут работать с первой строкой — правило введено до
кода, который им связан.

Чужой идентификатор опознаётся по имени колонки: `external_id`, `*_external_id`,
`provider_id`, `*_provider_id`, `external_key`. Это не полный список возможных
имён, а список тех, по которым чужое происхождение значения видно в самом
запросе; для того имена так и выбираются.

Запуск:
    python3 scripts/check_integrations.py
    python3 scripts/check_integrations.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))

import migration_model as model  # noqa: E402

REGISTRY = Path("docs/architecture/integrations.md")
CONFIGS = Path("configs/dynamic/registry.yaml")
VARIABLE = "PDR_INTEGRATIONS"

OFFICIAL = ("официальный", "неофициальный")
ROLES = ("украшение", "несущая")

TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")
CODE = re.compile(r"`([^`]+)`")

DISABLE_SECTION = "## Как это выключить"
DISABLE_ANSWERS = ("Осиротеют таблицы:", "Исчезнут функции:", "Затронуто файлов:")
UNKNOWN = ("неизвестно", "TODO", "<", "уточнить")

CONTRACT_CALL = re.compile(r"PDR_INTEGRATION_CONTRACT\(\s*(\w+)")
IMPLEMENTS = re.compile(r":\s*public\s+(?:[\w:]*::)?(\w+)\b")

FOREIGN_ID = re.compile(r"^(external_id|.*_external_id|provider_id|.*_provider_id|"
                        r"external_key|.*_external_key)$")

SOURCE_SUFFIXES = frozenset({".hpp", ".cpp"})
SKIPPED = frozenset({".git", "build", "out", "_deps", "__pycache__"})

SERVICES = Path("services")
STATIC_CONFIG = Path("configs/static_config.yaml")

DEADLINE = "PDR_REQUEST_DEADLINE"
OUTGOING = "PDR_OUTGOING_CALLS"

OUTGOING_COMPONENTS = {
    "http-client": (
        "штатный асинхронный клиент. Синхронный в корутинном рантайме блокирует поток "
        "целиком, и один медленный чужой сервис останавливает всё, что делит с ним поток"
    ),
    "dns-client": (
        "разрешение имён тем же контуром: getaddrinfo блокирующий, и в корутине он "
        "стоит ровно столько же, сколько синхронный HTTP"
    ),
    "outgoing-calls": (
        "единственная дверь наружу: у направления свой срок, свой бюджет повторов и своя "
        "квота, а срок не меньше срока запроса роняет старт (PDR_OUTGOING_CALLS)"
    ),
}


class RegistryError(Exception):
    """Реестр не разобран. Это отказ, а не предупреждение."""


def parse_registry(text: str) -> list[dict[str, str]]:
    """Строки таблицы реестра. Заголовок и разделитель пропускаются."""
    rows: list[dict[str, str]] = []
    columns = ("name", "covers", "official", "role", "adr", "port", "flag")

    for line in text.splitlines():
        found = TABLE_ROW.match(line.strip())
        if not found:
            continue
        cells = [cell.strip() for cell in found.group("cells").split("|")]
        if len(cells) != len(columns):
            continue
        if cells[0].startswith("---") or cells[0] == "Интеграция":
            continue
        if not re.fullmatch(r"`?[a-z][a-z0-9_]*`?", cells[0]):
            continue
        rows.append(dict(zip(columns, (cell.strip("`") for cell in cells))))

    return rows


def sources(root: Path):
    for path in sorted(root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        if any(part in SKIPPED for part in path.relative_to(root).parts):
            continue
        yield path


def contract_prefixes(root: Path) -> set[str]:
    found: set[str] = set()
    for path in sources(root):
        found.update(name.lower()
                     for name in CONTRACT_CALL.findall(path.read_text(encoding="utf-8",
                                                                     errors="replace")))
    return found


def implementations(root: Path, port_header: Path) -> int:
    """Сколько классов наследуют порт. Заглушка — тоже реализация."""
    if not port_header.is_file():
        return 0
    text = port_header.read_text(encoding="utf-8", errors="replace")
    names = re.findall(r"\bclass\s+(\w+)\s*\{", text)
    if not names:
        return 0

    count = 0
    for path in sources(root):
        if path == port_header:
            continue
        body = path.read_text(encoding="utf-8", errors="replace")
        if any(base in names for base in IMPLEMENTS.findall(body)):
            count += 1
    return count


def check_disable_section(path: Path, name: str) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    if DISABLE_SECTION not in text:
        return [
            f"{path}: у интеграции {name} нет раздела «{DISABLE_SECTION[3:]}». План удаления "
            f"пишется до интеграции, иначе его пишут по факту раскопок (ADR-0014)"
        ]

    section = text.split(DISABLE_SECTION, 1)[1].split("\n## ", 1)[0]
    problems: list[str] = []
    for answer in DISABLE_ANSWERS:
        line = next((row for row in section.splitlines() if answer in row), "")
        if not line:
            problems.append(
                f"{path}: у интеграции {name} в разделе «как выключить» нет ответа "
                f"«{answer.rstrip(':')}»"
            )
            continue
        value = line.split(answer, 1)[1].strip(" *<>")
        if not value or any(word.lower() in value.lower() for word in UNKNOWN):
            problems.append(
                f"{path}: у интеграции {name} ответ «{answer.rstrip(':')}» не заполнен "
                f"(«{value or 'пусто'}»). Неизвестная цена удаления означает, что "
                f"интеграцию делать рано"
            )
    return problems


def check_rows(rows: Sequence[dict[str, str]], root: Path) -> list[str]:
    violations: list[str] = []
    prefixes = contract_prefixes(root)
    configs = (root / CONFIGS).read_text(encoding="utf-8") if (root / CONFIGS).is_file() else ""

    for row in rows:
        name = row["name"]

        if row["official"] not in OFFICIAL:
            violations.append(
                f"{REGISTRY}: у интеграции {name} официальность «{row['official']}» — "
                f"бывает {' или '.join(OFFICIAL)}"
            )
        if row["role"] not in ROLES:
            violations.append(
                f"{REGISTRY}: у интеграции {name} роль «{row['role']}» — "
                f"бывает {' или '.join(ROLES)}"
            )

        if row["official"] == "неофициальный" and row["role"] == "несущая":
            violations.append(
                f"{REGISTRY}: интеграция {name} неофициальная и при этом несущая. Это ровно "
                f"то сочетание, которое стоило six-feat 182 файлов: у сервиса без "
                f"обязательств не бывает несущей роли (ADR-0014)"
            )

        link = LINK.search(row["adr"])
        if not link:
            violations.append(
                f"{REGISTRY}: у интеграции {name} не назван ADR. Решение пишется до кода"
            )
        else:
            adr = (root / REGISTRY).parent / link.group(1)
            if not adr.is_file():
                violations.append(f"{REGISTRY}: у интеграции {name} ADR не существует: {adr}")
            else:
                violations.extend(check_disable_section(adr, name))

        port = root / row["port"]
        if not row["port"] or not port.is_file():
            violations.append(
                f"{REGISTRY}: у интеграции {name} нет заголовка порта ({row['port'] or 'пусто'}). "
                f"Внешний API живёт за портом, а не вызовом из сценария"
            )
        else:
            count = implementations(root, port)
            if count < 2:
                violations.append(
                    f"{REGISTRY}: у порта интеграции {name} реализаций {count}, нужно минимум "
                    f"две — настоящая и заглушка. Одна означает, что порт списан с этого API"
                )

        expected_flag = f"{VARIABLE}.{name}"
        if row["flag"] != expected_flag:
            violations.append(
                f"{REGISTRY}: у интеграции {name} флаг «{row['flag']}», ожидается "
                f"«{expected_flag}». Отключение — снятый флаг, а не правка кода"
            )
        elif f"{VARIABLE}:" not in configs:
            violations.append(
                f"{CONFIGS}: интеграция {name} отключается флагом {expected_flag}, а величины "
                f"{VARIABLE} в реестре динамических значений нет"
            )

        if name not in prefixes:
            violations.append(
                f"{REGISTRY}: для интеграции {name} не инстанцирован набор «сервиса нет "
                f"навсегда»: PDR_INTEGRATION_CONTRACT({name.capitalize()}, ...). Тест «вернул "
                f"ошибку» его не заменяет"
            )

    return violations


def check_foreign_keys(root: Path) -> list[str]:
    """Чужой идентификатор первичным ключом наших таблиц не бывает."""
    try:
        migrations = model.load(root / "db/migrations")
    except model.MigrationError as error:
        return [str(error)]

    violations: list[str] = []
    merged = model.merged_tables(migrations)
    for migration in migrations:
        try:
            source = str(migration.path.relative_to(root))
        except ValueError:
            source = migration.path.name

        for table in (merged.get(item.name, item) for item in migration.tables):
            foreign = [column.name for column in table.columns if FOREIGN_ID.match(column.name)]
            if not foreign:
                continue

            keys = " ".join(constraint for constraint in table.constraints
                            if "primary key" in constraint.lower())
            inline = [column.name for column in table.columns
                      if FOREIGN_ID.match(column.name) and "primary key" in column.definition.lower()]

            for name in foreign:
                if name in inline or re.search(rf"\b{re.escape(name)}\b", keys):
                    violations.append(
                        f"{source}:{table.line}: чужой идентификатор {table.name}.{name} — "
                        f"первичный ключ. Внешний идентификатор живёт в таблице соответствия: "
                        f"первичным ключом он прорастает в каждый запрос, и это делает "
                        f"удаление интеграции дорогим (ADR-0014)"
                    )
    return violations


def check_outgoing_components(root: Path) -> list[str]:
    """У процесса заведены штатные компоненты выхода наружу.

    Проверяется НАЛИЧИЕ, а не отсутствие чужого: чужое ловит
    scripts/check_handmade.py по именам библиотек. Здесь другое: процесс без
    `http-client` не может сходить наружу штатно вовсе, и первая же интеграция
    подключит то, что подвернётся.
    """
    violations: list[str] = []
    directory = root / SERVICES
    if not directory.is_dir():
        return violations

    for service in sorted(directory.iterdir()):
        config = service / STATIC_CONFIG
        if not config.is_file():
            continue

        text = config.read_text(encoding="utf-8", errors="replace")
        display = config.relative_to(root)
        for name, reason in OUTGOING_COMPONENTS.items():
            if re.search(rf"^\s*{re.escape(name)}:", text, re.M):
                continue
            violations.append(
                f"{display}: не заведён компонент «{name}». {reason}"
            )

    return violations


def check_outgoing_deadlines(root: Path) -> list[str]:
    """Умолчание срока направления меньше умолчания срока запроса.

    Ту же сверку делает компонент на старте, и негодная настройка не даёт
    процессу подняться. Здесь она повторена по реестру ровно затем, чтобы
    негодное умолчание было видно в ревью, а не в упавшем сервисе: правка
    реестра и падение процесса разнесены во времени на целую выкладку.

    Отсутствие величин здесь молчит намеренно: «читается, а в реестре нет» —
    вопрос scripts/check_dynamic_configs.py, и отвечать на него дважды значит
    получить два разных текста об одном и том же.
    """
    path = root / CONFIGS
    if not path.is_file():
        return []

    try:
        registry = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    except yaml.YAMLError as error:
        return [f"{CONFIGS}: реестр не разобран как YAML ({error})"]

    deadline = (registry.get(DEADLINE) or {}).get("default")
    directions = (registry.get(OUTGOING) or {}).get("default")
    if not isinstance(deadline, int) or not isinstance(directions, dict):
        return []

    violations: list[str] = []
    for name, settings in sorted(directions.items()):
        timeout = (settings or {}).get("timeout_ms")
        if not isinstance(timeout, int) or timeout < deadline:
            continue
        violations.append(
            f"{CONFIGS}: у направления «{name}» умолчание срока {timeout} мс не меньше "
            f"{DEADLINE} ({deadline} мс). С такой настройкой процесс не поднимется вовсе — "
            f"вызов пережил бы собственный запрос"
        )
    return violations


def check(root: Path) -> tuple[list[str], int]:
    violations = check_foreign_keys(root)
    violations.extend(check_outgoing_components(root))
    violations.extend(check_outgoing_deadlines(root))

    path = root / REGISTRY
    if not path.is_file():
        violations.append(f"{REGISTRY}: реестра интеграций нет, а правило есть (ADR-0014)")
        return violations, 0

    rows = parse_registry(path.read_text(encoding="utf-8"))
    violations.extend(check_rows(rows, root))
    return violations, len(rows)


SELFTEST_REGISTRY = """# Внешние интеграции

| Интеграция | Что закрывает | Официальность | Роль | ADR | Порт | Флаг |
| --- | --- | --- | --- | --- | --- | --- |
| `music` | Подбор музыки | неофициальный | несущая | [ADR-0100](0100-music.md) | `libs/pdr-x/src/x/application/ports/music_gateway.hpp` | `PDR_INTEGRATIONS.music` |
| `receipts` | Чеки | официальный | украшение | [ADR-0101](0101-receipts.md) | `libs/pdr-x/src/x/application/ports/receipt_gateway.hpp` | `PDR_INTEGRATIONS.receipts` |
| `rooms` | Видеокомнаты | официальный | украшение | [ADR-0102](0102-rooms.md) | `libs/pdr-x/src/x/application/ports/room_gateway.hpp` | `ROOMS_ENABLED` |
"""

SELFTEST_GOOD_ADR = """# ADR-0101. Интеграция: чеки

## Как это выключить

**Осиротеют таблицы:** `billing_receipt_link`.

**Исчезнут функции:** выдача чека; человек видит «чек придёт позже».

**Затронуто файлов:** 9, посчитано `git grep -l receipt_gateway`.
"""

SELFTEST_UNKNOWN_ADR = """# ADR-0100. Интеграция: музыка

## Как это выключить

**Осиротеют таблицы:** неизвестно.

**Исчезнут функции:** подбор музыки.

**Затронуто файлов:** неизвестно.
"""

SELFTEST_PORT = """#pragma once
namespace pdr::x::ports {
class ReceiptGateway {
public:
    virtual ~ReceiptGateway() = default;
};
}
"""

SELFTEST_FILES = {
    "docs/architecture/integrations.md": SELFTEST_REGISTRY,
    "docs/architecture/0100-music.md": SELFTEST_UNKNOWN_ADR,
    "docs/architecture/0101-receipts.md": SELFTEST_GOOD_ADR,
    "docs/architecture/0102-rooms.md": SELFTEST_GOOD_ADR,
    "libs/pdr-x/src/x/application/ports/receipt_gateway.hpp": SELFTEST_PORT,
    "libs/pdr-x/src/x/infrastructure/real_receipt_gateway.hpp":
        "class RealReceiptGateway final : public ports::ReceiptGateway {};\n",
    "configs/dynamic/registry.yaml": (
        "PDR_OTHER:\n"
        "  default: 1\n"
        "PDR_REQUEST_DEADLINE:\n"
        "  default: 5000\n"
        "PDR_OUTGOING_CALLS:\n"
        "  default:\n"
        "    payments:\n"
        "      timeout_ms: 3000\n"
        "    slow:\n"
        "      timeout_ms: 5000\n"
    ),
    "db/migrations/V001__init.sql": (
        "create table schema_version (version integer primary key, checksum char(64) not null);\n"
    ),
    "db/migrations/V002__links.sql": (
        "create table billing_receipt_link (\n"
        "    tenant_id uuid not null,\n"
        "    external_id text not null,\n"
        "    constraint billing_receipt_link_pk primary key (tenant_id, external_id)\n"
        ");\n"
    ),
}

SELFTEST_SERVICES = {
    "services/alpha/configs/static_config.yaml": (
        "components_manager:\n"
        "    components:\n"
        "        http-client: {}\n"
        "        dns-client:\n"
        "            fs-task-processor: fs-task-processor\n"
        "        outgoing-calls: {}\n"
    ),
    "services/beta/configs/static_config.yaml": (
        "components_manager:\n"
        "    components:\n"
        "        http-client: {}\n"
    ),
}

SELFTEST_EXPECTED = (
    ("music", "неофициальная и при этом несущая"),
    ("music", "не заполнен"),
    ("rooms", "флаг «ROOMS_ENABLED»"),
    ("rooms", "нет заголовка порта"),
    ("receipts", "реализаций 1"),
    ("receipts", "не инстанцирован набор"),
    ("PDR_INTEGRATIONS", "в реестре динамических значений нет"),
    ("billing_receipt_link.external_id", "первичный ключ"),
    ("services/beta/configs/static_config.yaml", "«dns-client»"),
    ("services/beta/configs/static_config.yaml", "«outgoing-calls»"),
    ("slow", "не меньше PDR_REQUEST_DEADLINE"),
)


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана.

    Последним идёт чистый случай — пустой реестр и схема без чужих ключей:
    проверка, которая ловит всё подряд, бесполезна так же, как не ловящая ничего.
    """
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in {**SELFTEST_FILES, **SELFTEST_SERVICES}.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, rows = check(root)

        if rows != 3:
            print(f"самопроверка: разобрано {rows} строк реестра вместо трёх", file=sys.stderr)
            return 1

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» у {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        (root / REGISTRY).write_text(
            "# Внешние интеграции\n\n| Интеграция | Что закрывает | Официальность | Роль | "
            "ADR | Порт | Флаг |\n| --- | --- | --- | --- | --- | --- | --- |\n",
            encoding="utf-8",
        )
        (root / "db/migrations/V002__links.sql").write_text(
            "create table billing_receipt_link (\n"
            "    tenant_id uuid not null,\n"
            "    id uuid not null,\n"
            "    external_id text not null,\n"
            "    constraint billing_receipt_link_pk primary key (tenant_id, id)\n"
            ");\n",
            encoding="utf-8",
        )
        (root / "services/beta/configs/static_config.yaml").unlink()
        (root / CONFIGS).write_text(
            "PDR_REQUEST_DEADLINE:\n"
            "  default: 5000\n"
            "PDR_OUTGOING_CALLS:\n"
            "  default:\n"
            "    payments:\n"
            "      timeout_ms: 3000\n",
            encoding="utf-8",
        )

        clean, rows = check(root)
        if clean or rows:
            print(f"самопроверка: чистый случай не прошёл: {clean}", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они есть, "
          f"и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Внешние интеграции: правило ADR-0014.")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, rows = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Правило — docs/adr/"
              f"0014-external-apis-are-never-load-bearing.md, реестр — {REGISTRY}",
              file=sys.stderr)
        return 1

    if not rows:
        print("Внешних интеграций в дереве нет. Чужих идентификаторов в первичных ключах тоже.")
        return 0

    print(f"Интеграций в реестре: {rows}. У каждой порт с двумя реализациями, набор "
          f"«сервиса нет навсегда», флаг в конфиге и посчитанная цена удаления.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
