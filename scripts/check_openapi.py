#!/usr/bin/env python3
"""Спецификация и сервис не расходятся СОСТАВОМ ручек (PDR-API-03).

Спецификация — источник правды: клиенты порождают из неё типы, а не наоборот
(docs/adr/0009-monorepo-with-clients.md). Источник правды, который никто не
сверяет с работающим процессом, устаревает за две недели — это происходит
всегда.

Проверок две, и они ловят РАЗНОЕ:

* contract-набор `tests/contract/openapi_conformance` прогоняет настоящие ответы
  сервиса против схем и ловит расхождение ФОРМЫ;
* эта проверка ловит расхождение СОСТАВА. Набор не заметит ни ручку, которую
  подняли и не описали, ни описанную ручку, которую сняли: он ходит только туда,
  куда ему сказали ходить.

Правила:

* каждая ручка клиентского порта названа в спецификации, и наоборот;
* прикладной путь лежит под `/api/v1`. Исключение закрытым списком — пробы
  состояния: их читает оркестратор, схема у них не развивается, версионировать
  нечего (docs/api/README.md);
* ТИПЫ ФРОНТА НЕ ОТСТАЛИ ОТ СПЕЦИФИКАЦИИ. `clients/shared/api/openapi.ts`
  порождён из того же файла и, в отличие от C++, лежит в истории: у фронта
  своего chaotic нет, и без закоммиченного файла он не собирается без
  бэкендового инструментария. Асимметрия названа в docs/api/README.md.

  Плата за закоммиченное порождённое — ровно одна: копия расходится с
  оригиналом. Поэтому проверка ПОРОЖДАЕТ ЗАНОВО и сравнивает побайтно. Правки
  руками, забытый перегон после смены схемы, чужая версия инструмента — всё это
  один и тот же отказ, и чинится он одной строкой: `make api-types`.

ЧЕГО ПРОВЕРКА НЕ СЧИТАЕТ РУЧКОЙ, и каждый случай назван:

* `monitor-handler: true` — служебный порт, не внешний контур;
* `as_fallback` — у такого обработчика пути нет вовсе;
* `load-enabled`, выключенный в профиле, — ручка не поднимается в установке
  (так выключен `tests-control`).

YAML разбирается СТРОГИМ ПОДМНОЖЕСТВОМ, как это делают остальные проверки схемы:
чего разбор не понял, он называет вслух и роняет проверку. Молча пропущенная
ручка означала бы, что состав сверен только наполовину.

Пустота — не нарушение: сервисов нет — проверять нечего, и проверка говорит это
честно, как `scripts/check_testsuite.py`.

Запуск:
    python3 scripts/check_openapi.py
    python3 scripts/check_openapi.py --selftest
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

SPEC = Path("docs/api/openapi.yaml")
SERVICES = Path("services")

STATIC_CONFIG = Path("configs/static_config.yaml")
CONFIG_VARS = Path("configs/config_vars.yaml")

VERSIONED_PREFIX = "/api/v1/"

PROBES = ("/ping", "/health", "/readiness")

CLIENTS = Path("clients")
CLIENT_TYPES = CLIENTS / "shared/api/openapi.ts"
GENERATOR = CLIENTS / "node_modules/.bin/openapi-typescript"

"""Пробы состояния: единственное, чему разрешено жить вне `/api/v1`.

Список закрытый и короткий намеренно. Их читает оркестратор, а не клиент; схема
у них не развивается, и версионировать нечего. Всё остальное — прикладной
контракт, и он версионируется путём.
"""

METHODS = ("get", "put", "post", "delete", "patch", "options", "head", "trace")

PATH_FIELDS = ("summary", "description", "servers", "parameters", "$ref")
"""Поля пути, которые не являются операцией. Всё прочее на этом уровне — либо
метод, либо то, чего разбор не понял, и он скажет об этом вслух."""

INDENT_COMPONENT = 8
INDENT_FIELD = 12

KEY = re.compile(r"^(?P<indent> *)(?P<key>[^\s:#][^:]*):(?P<rest>.*)$")


def lines_of(text: str) -> Iterator[tuple[int, str]]:
    for number, line in enumerate(text.splitlines(), start=1):
        without = line.split("#", 1)[0] if line.lstrip().startswith("#") else line
        if not without.strip():
            continue
        yield number, without.rstrip()


def spec_operations(text: str) -> tuple[set[tuple[str, str]], list[str]]:
    """(путь, метод) из раздела paths. Второе — то, чего разбор не понял."""
    found: set[tuple[str, str]] = set()
    complaints: list[str] = []

    inside = False
    path = ""
    for number, line in lines_of(text):
        shape = KEY.match(line)
        if shape is None:
            continue
        indent = len(shape.group("indent"))
        key = shape.group("key").strip()

        if indent == 0:
            inside = key == "paths"
            path = ""
            continue
        if not inside:
            continue

        if indent == 2:
            if not key.startswith("/"):
                complaints.append(
                    f"{SPEC}:{number}: в разделе paths ключ «{key}» не похож на путь"
                )
                path = ""
                continue
            path = key
            continue

        if indent == 4 and path:
            if key in METHODS:
                found.add((path, key.upper()))
            elif key not in PATH_FIELDS:
                complaints.append(
                    f"{SPEC}:{number}: у пути «{path}» разбор не понял поле «{key}». "
                    f"Молча пропустить его нельзя: состав окажется сверен наполовину"
                )
    return found, complaints


def config_vars(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for _, line in lines_of(text):
        shape = KEY.match(line)
        if shape is None or len(shape.group("indent")) != 0:
            continue
        values[shape.group("key").strip()] = shape.group("rest").strip()
    return values


def service_handlers(
    config: str, variables: dict[str, str], display: Path
) -> tuple[set[tuple[str, str]], list[str]]:
    """(путь, метод) ручек клиентского порта одного сервиса."""
    components: dict[str, dict[str, str]] = {}
    current = ""

    for _, line in lines_of(config):
        shape = KEY.match(line)
        if shape is None:
            continue
        indent = len(shape.group("indent"))
        key = shape.group("key").strip()
        rest = shape.group("rest").strip()

        if indent == INDENT_COMPONENT:
            current = key
            components.setdefault(current, {})
        elif indent == INDENT_FIELD and current:
            components[current][key] = rest

    found: set[tuple[str, str]] = set()
    complaints: list[str] = []

    for name, fields in sorted(components.items()):
        if "path" not in fields:
            continue
        if fields.get("monitor-handler") == "true" or "as_fallback" in fields:
            continue

        enabled = fields.get("load-enabled", "true")
        if enabled.startswith("$"):
            enabled = variables.get(enabled[1:], "true")
        if enabled == "false":
            continue

        methods = fields.get("method", "")
        if not methods:
            complaints.append(
                f"{display}: у ручки «{name}» есть путь, но не назван метод. "
                f"Спецификации нечего описывать"
            )
            continue

        for method in methods.split(","):
            found.add((fields["path"], method.strip().upper()))

    return found, complaints


def services_of(root: Path) -> list[Path]:
    directory = root / SERVICES
    if not directory.is_dir():
        return []
    return sorted(entry for entry in directory.iterdir() if (entry / STATIC_CONFIG).is_file())


def client_types(root: Path) -> list[str]:
    """Закоммиченные типы фронта совпадают со свежепорождёнными."""
    spec = root / SPEC
    committed = root / CLIENT_TYPES
    generator = root / GENERATOR

    if not spec.is_file():
        return []

    if not committed.is_file():
        return [
            f"{CLIENT_TYPES}: типов фронта нет вовсе. Клиент порождает их из "
            f"{SPEC}, а без файла в истории он собирается только у того, у кого "
            f"поставлен бэкендовый инструментарий: `make api-types`"
        ]

    if not generator.is_file():
        return [
            f"{GENERATOR}: порождать нечем, и потому расхождение НЕ ПРОВЕРЕНО. "
            f"Пропустить эту проверку молча значило бы держать в истории копию, "
            f"за которой никто не следит: `make api-types`"
        ]

    with tempfile.TemporaryDirectory() as directory:
        fresh = Path(directory) / "openapi.ts"
        made = subprocess.run(
            [str(generator), str(spec), "--output", str(fresh)],
            capture_output=True,
            text=True,
            check=False,
        )
        if made.returncode != 0:
            return [f"{CLIENT_TYPES}: порождение не прошло: {made.stderr.strip()}"]

        if fresh.read_text(encoding="utf-8") != committed.read_text(encoding="utf-8"):
            return [
                f"{CLIENT_TYPES}: закоммиченные типы разошлись со спецификацией. "
                f"Фронт собирается по ним и будет собран по устаревшему контракту: "
                f"`make api-types`"
            ]

    return []


def check(root: Path) -> tuple[list[str], int]:
    violations: list[str] = []
    services = services_of(root)
    spec_path = root / SPEC

    if not services and not spec_path.is_file():
        return [], 0

    raised: set[tuple[str, str]] = set()
    for service in services:
        variables = {}
        vars_path = service / CONFIG_VARS
        if vars_path.is_file():
            variables = config_vars(vars_path.read_text(encoding="utf-8"))

        handlers, complaints = service_handlers(
            (service / STATIC_CONFIG).read_text(encoding="utf-8"),
            variables,
            (service / STATIC_CONFIG).relative_to(root),
        )
        violations.extend(complaints)
        raised |= handlers

    if not spec_path.is_file():
        if raised:
            violations.append(
                f"{SPEC}: ручек поднято {len(raised)}, а спецификации нет вовсе. Клиенты "
                f"порождают типы из неё, и без неё они порождают их из чужой головы"
            )
        return violations, len(raised)

    described, complaints = spec_operations(spec_path.read_text(encoding="utf-8"))
    violations.extend(complaints)

    for path, method in sorted(raised - described):
        violations.append(
            f"{SPEC}: ручка {method} {path} поднята сервисом, но не описана. Клиент узнает "
            f"о ней из чужого кода, а не из контракта"
        )

    for path, method in sorted(described - raised):
        violations.append(
            f"{SPEC}: описана ручка {method} {path}, которой в сервисе нет. Спецификация, "
            f"обещающая снятую ручку, врёт ровно так же, как молчащая о новой"
        )

    violations.extend(client_types(root))

    for path, _ in sorted(described):
        if path in PROBES or path.startswith(VERSIONED_PREFIX):
            continue
        violations.append(
            f"{SPEC}: путь {path} лежит вне «{VERSIONED_PREFIX}» и не входит в закрытый "
            f"список проб состояния. Прикладной контракт версионируется путём, а не "
            f"заголовком (docs/api/README.md)"
        )

    return violations, len(described)


SELFTEST_SPEC = """openapi: 3.1.0
info:
  title: Проверка
  version: 1.0.0
paths:
  /health:
    get:
      operationId: health
      responses:
        '200':
          description: жив
  /api/v1/lessons:
    post:
      operationId: bookLesson
      responses:
        '200':
          description: записан
  /api/v1/gone:
    get:
      operationId: gone
      responses:
        '200':
          description: описана, а ручки нет
  /lessons/legacy:
    get:
      operationId: legacy
      responses:
        '200':
          description: мимо версии
components: {}
"""

SELFTEST_CONFIG = """components_manager:
    components:
        handler-health:
            path: /health
            method: GET
            task_processor: main-task-processor
        handler-book-lesson:
            path: /api/v1/lessons
            method: POST
            task_processor: main-task-processor
        handler-cancel-lesson:
            path: /api/v1/lessons/{lesson}/cancel
            method: POST
            task_processor: main-task-processor
        handler-legacy:
            path: /lessons/legacy
            method: GET
            task_processor: main-task-processor
        handler-server-monitor:
            path: /service/monitor
            method: GET
            monitor-handler: true
        handler-implicit-http-options:
            as_fallback: implicit-http-options
            method: OPTIONS
        tests-control:
            load-enabled: $testsuite-enabled
            path: /tests/{action}
            method: POST
"""

SELFTEST_VARS = "testsuite-enabled: false\n"

SELFTEST_EXPECTED = (
    "POST /api/v1/lessons/{lesson}/cancel поднята сервисом, но не описана",
    "описана ручка GET /api/v1/gone",
    "путь /lessons/legacy лежит вне",
)

SELFTEST_CLEAN = ("/service/monitor", "/tests/{action}", "implicit-http-options")

SELFTEST_SILENCE = (
    "типов фронта нет вовсе",
    "порождать нечем",
)
"""Два способа промолчать вместо проверки, и оба обязаны быть слышны.

Ни того ни другого нельзя пропускать тихо: «файла нет» и «порождать нечем»
выглядят как «всё хорошо» ровно до дня, когда фронт соберут по контракту
позапрошлого месяца.
"""


def selftest() -> int:
    """Отрицательные случаи: обе стороны расхождения обязаны ловиться.

    Типы фронта проверяются дважды, и оба раза на молчание: сперва их нет вовсе,
    потом файл на месте, а порождать нечем. И то и другое обязано быть слышно —
    «не проверено» не то же самое, что «совпадает».
    """
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        empty, counted = check(root)
        if empty or counted:
            print("самопроверка: пустое дерево объявлено нарушением", file=sys.stderr)
            return 1

        (root / SPEC).parent.mkdir(parents=True, exist_ok=True)
        (root / SPEC).write_text(SELFTEST_SPEC, encoding="utf-8")

        service = root / SERVICES / "scheduling"
        (service / STATIC_CONFIG).parent.mkdir(parents=True, exist_ok=True)
        (service / STATIC_CONFIG).write_text(SELFTEST_CONFIG, encoding="utf-8")
        (service / CONFIG_VARS).write_text(SELFTEST_VARS, encoding="utf-8")

        violations, described = check(root)

        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if not any(SELFTEST_SILENCE[0] in line for line in violations):
            print(f"самопроверка: не поймано «{SELFTEST_SILENCE[0]}»", file=sys.stderr)
            return 1

        (root / CLIENT_TYPES).parent.mkdir(parents=True, exist_ok=True)
        (root / CLIENT_TYPES).write_text("export interface paths {}\n", encoding="utf-8")
        unchecked = client_types(root)
        if not any(SELFTEST_SILENCE[1] in line for line in unchecked):
            print(f"самопроверка: не поймано «{SELFTEST_SILENCE[1]}»", file=sys.stderr)
            return 1

        for fragment in SELFTEST_CLEAN:
            if any(fragment in line for line in violations):
                print(f"самопроверка: пропускаемое объявлено нарушением: {fragment}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if described != 4:
            print(f"самопроверка: разобрано {described} операций вместо четырёх",
                  file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Состав ручек и спецификация не расходятся.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, described = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Источник правды — {SPEC}", file=sys.stderr)
        return 1

    if not described:
        print("Ручек нет — проверять нечего. Правило уже действует и сработает на первой же.")
        return 0

    print(f"Операций в спецификации: {described}. Состав ручек и спецификация совпадают.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
