#!/usr/bin/env python3
"""Реестр динамических значений: код и реестр не расходятся (PDR-CFG-01).

Число, влияющее на людей, живёт в динамическом конфиге, а не в константе. Но
конфиг без пределов — та же константа, только теперь её можно сломать опечаткой
из чужих рук. Поэтому у каждой величины есть запись в configs/dynamic/registry.yaml:
что это, в каких пределах осмысленна, что сломается за пределами, чей это выбор и
привязана ли она к стране.

Проверяется:

* у записи есть все обязательные поля: description, kind, owner, jurisdiction,
  breaks, default, schema;
* `kind` — техническая или продуктовая, `jurisdiction` — да или нет: не «TODO» и
  не пусто;
* у каждого числа в схеме есть minimum и maximum. Конфигов без диапазонов не
  бывает: значение вне пределов должно отвергаться ДО попадания в работу;
* умолчание не выходит за собственные пределы: значение, которое схема тут же
  объявляет негодным, — ловушка, не видная ни в одном тесте;
* рядом с чтением величины стоит подписка `UpdateAndListen` — журнал
  «было → стало». Величина без журнала означает, что «оно само сломалось» будет
  разбираться по памяти;
* запись, которую никто не читает, разрешена ровно с полем `awaits` — областью
  задачи, которая её заведёт. Так число попадает в реестр раньше, чем в
  константу, и не остаётся там навсегда: как только величину начали читать, поле
  обязано исчезнуть.

ЧТО ОТСЮДА УБРАНО И ПОЧЕМУ (PDR-ARCH-09). Структуры, ключи, умолчания и пределы
порождаются из ЭТОГО ЖЕ реестра штатным chaotic, и четыре проверки стали
проверять невозможное:

* ~~каждый `dynamic_config::Key` из кода назван в реестре~~ — ключ объявляет не
  человек, а порождение, и объявить оно может только то, что в реестре есть;
* ~~умолчание в реестре совпадает с объявленным в коде~~ — умолчание в коде И
  ЕСТЬ умолчание из реестра: текст один, источник один, расходиться нечему;
* ~~у ключа есть умолчание прямо в объявлении~~ — порождение ставит его всегда,
  объявления без умолчания не бывает;
* ~~ключ объявляется только в infrastructure~~ — объявлений в дереве не осталось
  вовсе, они в каталоге сборки. Что домен не читает конфиг сам, проверяет цель
  `pdr_compile_fail_config_in_core`, и проверяет строже: не грепом, а сборкой.

Вычеркнуты, а не удалены молча: иначе через год непонятно, почему проверка
похудела, и кто-нибудь вернёт их обратно — вместе с руками писанной структурой,
ради которой они и были нужны.

ЧТО ОСТАЛОСЬ И ПОЧЕМУ. Порождение не отвечает на вопросы «чей это выбор», «что
сломается за пределами» и «привязано ли значение к стране» — на них отвечает
только человек. И оно не ловит умолчание, вышедшее за собственные пределы:
негодное умолчание порождается молча и падает при первом разборе, а не при
сборке.

YAML разбирается СТРОГИМ ПОДМНОЖЕСТВОМ, как разбор миграций разбирает подмножество
DDL: чего разбор не понял, он называет вслух и роняет проверку. Молча пропущенная
запись означала бы, что реестр проверен только на ту половину, которую поняли.

Запуск:
    python3 scripts/check_dynamic_configs.py
    python3 scripts/check_dynamic_configs.py --selftest
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

REGISTRY = Path("configs/dynamic/registry.yaml")

SOURCE_SUFFIXES = frozenset({".hpp", ".cpp", ".hxx", ".cc"})
TESTS_DIR = "tests"
"""Проверки не считаются чтением величины.

Подмена значения в тесте — не то же, что «величина кому-то нужна»: реестр не
должен считать живой запись, которую читает только её собственный тест."""
SKIPPED_DIRS = frozenset({".git", "build", "out", "_deps", "__pycache__", "compile_fail"})

REQUIRED_FIELDS = ("description", "kind", "owner", "jurisdiction", "breaks", "default", "schema")
KINDS = ("техническая", "продуктовая")
JURISDICTIONS = ("да", "нет")

CONTRIBUTING = Path("CONTRIBUTING.md")
AREA_ROW = re.compile(r"^\|\s*`([A-Z]+)`\s*\|")

KEY_USE = re.compile(r"dynamic_config::(?P<name>PDR_[A-Z0-9_]+)\b")
"""Чтение порождённой величины: `::dynamic_config::PDR_ЧТО_ТО`.

Объявлений в дереве больше нет — их порождает chaotic, — поэтому со стороны кода
величина видна только тем, что её читают. Это и есть вопрос, на который реестру
нужен ответ: не «объявлена ли она», а «нужна ли она кому-нибудь».
"""

VARIABLE_LINE = re.compile(r"^([A-Z][A-Z0-9_]*):\s*$")
FIELD_LINE = re.compile(r"^  ([a-z]+):\s*(.*)$")


class RegistryError(Exception):
    """Разбор реестра не удался. Это отказ, а не предупреждение."""


def _dedent_block(lines: Sequence[str], start: int, indent: int) -> tuple[list[str], int]:
    """Строки блока с отступом больше indent и позиция за блоком."""
    block: list[str] = []
    index = start
    while index < len(lines):
        line = lines[index]
        if not line.strip():
            block.append("")
            index += 1
            continue
        current = len(line) - len(line.lstrip())
        if current <= indent:
            break
        block.append(line[indent + 2:])
        index += 1
    return block, index


def parse_registry(text: str) -> dict[str, dict[str, object]]:
    """Записи реестра. Понимается ровно то, что описано в его же шапке."""
    lines = text.splitlines()
    entries: dict[str, dict[str, object]] = {}

    index = 0
    while index < len(lines):
        line = lines[index]
        if not line.strip() or line.lstrip().startswith("#"):
            index += 1
            continue

        found = VARIABLE_LINE.match(line)
        if not found:
            raise RegistryError(
                f"{REGISTRY}:{index + 1}: разбор не понял строку «{line.strip()[:40]}». "
                f"На верхнем уровне бывают только имена величин вида PDR_ИМЯ:"
            )

        name = found.group(1)
        block, index = _dedent_block(lines, index + 1, 0)
        entries[name] = parse_entry(name, block)

    return entries


def parse_entry(name: str, block: Sequence[str]) -> dict[str, object]:
    fields: dict[str, object] = {}
    index = 0
    while index < len(block):
        line = block[index]
        if not line.strip() or line.lstrip().startswith("#"):
            index += 1
            continue

        field, _, rest = line.partition(":")
        if line.startswith(" ") or not field.isidentifier():
            raise RegistryError(
                f"{REGISTRY}: у величины {name} разбор не понял строку «{line.strip()[:40]}»"
            )

        rest = rest.strip()
        nested, index = _dedent_block(block, index + 1, 0)
        if rest in (">-", ">", "|", "|-", ""):
            fields[field] = " ".join(part.strip() for part in nested if part.strip())
            if rest == "" and nested:
                fields[field] = nested
        else:
            fields[field] = rest
            if nested:
                fields[field] = nested if not rest else [rest, *nested]
    return fields


def registry_default(entry: dict[str, object]) -> str:
    """Значение по умолчанию как компактный текст: «{}», «60000», «true»."""
    value = entry.get("default")
    if isinstance(value, list):
        return " ".join(part.strip() for part in value if part.strip())
    return str(value).strip()


def parse_schema(lines: Sequence[str]) -> dict[str, object]:
    """JSON-схема из строк реестра: вложенные отображения, скаляры и списки.

    Разбирается то же строгое подмножество, что и остальной реестр. Непонятая
    строка — отказ: схема, разобранная наполовину, проверяет пределы наполовину.
    """
    schema: dict[str, object] = {}
    stack: list[tuple[int, dict[str, object]]] = [(-1, schema)]

    for line in lines:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip())
        field, _, rest = line.strip().partition(":")
        if not field:
            raise RegistryError(f"{REGISTRY}: разбор схемы не понял строку «{line.strip()[:40]}»")

        while stack and stack[-1][0] >= indent:
            stack.pop()
        if not stack:
            raise RegistryError(f"{REGISTRY}: отступ схемы съехал на «{line.strip()[:40]}»")

        rest = rest.strip()
        if rest:
            stack[-1][1][field] = _scalar(rest)
            continue

        nested: dict[str, object] = {}
        stack[-1][1][field] = nested
        stack.append((indent, nested))

    return schema


def _scalar(text: str) -> object:
    if text.startswith("[") and text.endswith("]"):
        return [part.strip() for part in text[1:-1].split(",") if part.strip()]
    if text in ("true", "false"):
        return text == "true"
    try:
        return int(text)
    except ValueError:
        return text.strip("'\"")


def numeric_ranges(entry: dict[str, object], name: str) -> list[str]:
    """Числа без пределов. Диапазон живёт в JSON-схеме, а не отдельным полем."""
    schema = entry.get("schema")
    if not isinstance(schema, list):
        return [f"{REGISTRY}: у величины {name} нет схемы: пределы задаются ею"]

    try:
        parsed = parse_schema(schema)
    except RegistryError as error:
        return [str(error)]

    problems = _walk_schema(parsed, name)
    problems.extend(_default_fits(entry, parsed, name))
    return problems


def _walk_schema(node: object, name: str) -> list[str]:
    if not isinstance(node, dict):
        return []

    problems: list[str] = []
    if node.get("type") in ("integer", "number"):
        missing = [limit for limit in ("minimum", "maximum") if limit not in node]
        if missing:
            problems.append(
                f"{REGISTRY}: у величины {name} число без пределов ({', '.join(missing)}). "
                f"Конфиг без диапазона — та же константа, только сломать её теперь можно "
                f"опечаткой из чужих рук"
            )

    for value in node.values():
        problems.extend(_walk_schema(value, name))
    return problems


def _default_fits(entry: dict[str, object], schema: dict[str, object], name: str) -> list[str]:
    """Умолчание внутри собственных пределов.

    Умолчание вне диапазона — ловушка, которую не видно ни в одном тесте: сервис
    поднимается на нём, когда источник недоступен, и поднимается со значением,
    которое сам же считает негодным.
    """
    text = registry_default(entry)
    try:
        value = json.loads(text)
    except (json.JSONDecodeError, TypeError):
        return []

    kind = schema.get("type")
    if kind in ("integer", "number") and isinstance(value, (int, float)):
        low, high = schema.get("minimum"), schema.get("maximum")
        if isinstance(low, (int, float)) and value < low:
            return [f"{REGISTRY}: у величины {name} умолчание {text} меньше её же minimum {low}"]
        if isinstance(high, (int, float)) and value > high:
            return [f"{REGISTRY}: у величины {name} умолчание {text} больше её же maximum {high}"]
        return []

    expected = {"object": dict, "array": list, "string": str, "boolean": bool}.get(str(kind))
    if expected and not isinstance(value, expected):
        return [
            f"{REGISTRY}: у величины {name} умолчание {text} не того вида, что объявлено "
            f"схемой ({kind})"
        ]
    return []


def source_files(root: Path):
    for path in sorted(root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        if any(part in SKIPPED_DIRS or part.startswith(".") for part in path.parts):
            continue
        yield path


def keys_in_use(root: Path) -> tuple[dict[str, Path], list[str]]:
    """{имя величины: файл, который её читает} и нарушения вокруг чтения."""
    found: dict[str, Path] = {}
    violations: list[str] = []

    for path in source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        used = {match.group("name") for match in KEY_USE.finditer(text)}
        if not used:
            continue

        try:
            display = path.relative_to(root)
        except ValueError:
            display = path

        if TESTS_DIR in display.parts:
            continue

        for name in sorted(used):
            found.setdefault(name, display)

        if "UpdateAndListen" not in text:
            violations.append(
                f"{display}: величины {', '.join(sorted(used))} читаются без журнала "
                f"изменений. Рядом стоит подписка dynamic_config::Source::UpdateAndListen: "
                f"без неё «оно само сломалось» разбирается не за минуту, а по памяти"
            )

    return found, violations


def known_areas(root: Path) -> set[str]:
    """Области задач из CONTRIBUTING.md. Список закрытый, и «awaits» ссылается в него."""
    path = root / CONTRIBUTING
    if not path.is_file():
        return set()
    return {
        found.group(1)
        for line in path.read_text(encoding="utf-8").splitlines()
        if (found := AREA_ROW.match(line))
    }


def check(root: Path) -> tuple[list[str], int]:
    registry_path = root / REGISTRY
    used, violations = keys_in_use(root)
    areas = known_areas(root)

    if not registry_path.is_file():
        if used:
            violations.append(
                f"{REGISTRY}: реестра нет, а величины читаются "
                f"({', '.join(sorted(used))}). Число, влияющее на людей, живёт в реестре"
            )
        return violations, 0

    try:
        entries = parse_registry(registry_path.read_text(encoding="utf-8"))
    except RegistryError as error:
        return [str(error)], 0

    for name in sorted(used):
        if name not in entries:
            violations.append(
                f"{used[name]}: величина {name} читается, но её нет в {REGISTRY}. "
                f"Запись отвечает на то, чего в коде не видно: пределы, что сломается "
                f"за ними, чей это выбор и привязана ли она к стране"
            )

    for name in sorted(entries):
        entry = entries[name]

        awaited = str(entry.get("awaits", "")).strip()

        if name not in used and not awaited:
            violations.append(
                f"{REGISTRY}: величина {name} записана, а не читает её никто. Реестр не "
                f"зарастает мёртвыми строками: уберите запись, прочитайте величину или "
                f"назовите полем «awaits» область, которая её заведёт"
            )

        if awaited and awaited not in areas:
            violations.append(
                f"{REGISTRY}: у величины {name} «awaits: {awaited}» — такой области нет в "
                f"{CONTRIBUTING}. Список областей закрыт: запись, ждущая несуществующую "
                f"область, не дождётся никогда"
            )

        if awaited and name in used:
            violations.append(
                f"{REGISTRY}: у величины {name} стоит «awaits: {awaited}», а её уже читают "
                f"({used[name]}). Поле снимается тем же изменением, что заводит чтение, — "
                f"иначе реестр продолжает обещать сделанное"
            )

        for field in REQUIRED_FIELDS:
            if field not in entry or (isinstance(entry[field], str) and not entry[field].strip()):
                violations.append(f"{REGISTRY}: у величины {name} нет поля «{field}»")

        kind = str(entry.get("kind", "")).strip()
        if kind and kind not in KINDS:
            violations.append(
                f"{REGISTRY}: у величины {name} вид «{kind}» — бывает {' или '.join(KINDS)}. "
                f"Продуктовые значения правятся без участия разработчика, и это надо знать "
                f"заранее"
            )

        jurisdiction = str(entry.get("jurisdiction", "")).strip()
        if jurisdiction and jurisdiction not in JURISDICTIONS:
            violations.append(
                f"{REGISTRY}: у величины {name} привязка к юрисдикции «{jurisdiction}» — "
                f"бывает {' или '.join(JURISDICTIONS)}. Рынок сегодня один, но пометка "
                f"превращает будущий выход в другую страну в разбор списка, а не в ревизию "
                f"всего продукта"
            )

        violations.extend(numeric_ranges(entry, name))

    return violations, len(entries)


SELFTEST_REGISTRY = """PDR_GOOD:
  description: >-
    Хорошая запись: есть всё и пределы у числа.
  kind: продуктовая
  owner: владелец продукта
  jurisdiction: да
  breaks: >-
    За пределами окно отмены перестаёт быть окном.
  default: 60000
  schema:
    type: integer
    minimum: 1000
    maximum: 86400000

PDR_TOO_SMALL:
  description: Умолчание вне собственных пределов.
  kind: продуктовая
  owner: владелец продукта
  jurisdiction: нет
  awaits: SCHED
  breaks: Ничего — и это худший исход.
  default: 500
  schema:
    type: integer
    minimum: 1000
    maximum: 5000

PDR_WAITS_FOREVER:
  description: Ждёт область, которой нет в списке.
  kind: продуктовая
  owner: владелец продукта
  jurisdiction: нет
  awaits: ЗАВТРА
  breaks: Ничего.
  default: 1
  schema:
    type: integer
    minimum: 1
    maximum: 2

PDR_ALREADY_THERE:
  description: Ключ уже объявлен, а поле «awaits» осталось.
  kind: техническая
  owner: тот, кто держит сервис
  jurisdiction: нет
  awaits: CFG
  breaks: Ничего.
  default: 5
  schema:
    type: integer
    minimum: 1
    maximum: 10

PDR_SILENT:
  description: Ключ без журнала изменений.
  kind: техническая
  owner: тот, кто держит сервис
  jurisdiction: нет
  breaks: Ничего.
  default: 5
  schema:
    type: integer
    minimum: 1
    maximum: 10

PDR_NO_DEFAULT:
  description: Ключ без умолчания в объявлении.
  kind: техническая
  owner: тот, кто держит сервис
  jurisdiction: нет
  breaks: Недоступный источник роняет старт.
  default: 5
  schema:
    type: integer
    minimum: 1
    maximum: 10

PDR_NO_LIMITS:
  description: Число без пределов.
  kind: техническая
  owner: тот, кто держит сервис
  jurisdiction: нет
  breaks: Ничего, и это подозрительно.
  default: 5
  schema:
    type: integer

PDR_BAD_FIELDS:
  description: Нет владельца, вид и юрисдикция не из списка.
  kind: важная
  jurisdiction: возможно
  breaks: Неизвестно.
  default: {}
  schema:
    type: object

PDR_FORGOTTEN:
  description: Записана, а ключа в коде нет.
  kind: техническая
  owner: тот, кто держит сервис
  jurisdiction: нет
  breaks: Ничего.
  default: 1
  schema:
    type: integer
    minimum: 1
    maximum: 2
"""

SELFTEST_FILES = {
    "configs/dynamic/registry.yaml": SELFTEST_REGISTRY,
    "libs/pdr-jobs/src/jobs/infrastructure/good.cpp": (
        "auto every = snapshot[::dynamic_config::PDR_GOOD];\n"
        "auto limits = snapshot[::dynamic_config::PDR_NO_LIMITS];\n"
        "auto fields = snapshot[::dynamic_config::PDR_BAD_FIELDS];\n"
        "auto already = snapshot[::dynamic_config::PDR_ALREADY_THERE];\n"
        'auto scope = source.UpdateAndListen(this, "good", &Good::OnConfigUpdate);\n'
    ),
    "libs/pdr-jobs/src/jobs/infrastructure/silent.cpp": (
        "auto value = snapshot[::dynamic_config::PDR_SILENT];\n"
    ),
    "libs/pdr-jobs/src/jobs/infrastructure/unregistered.cpp": (
        "auto value = snapshot[::dynamic_config::PDR_UNREGISTERED];\n"
        'auto scope = source.UpdateAndListen(this, "u", &U::OnConfigUpdate);\n'
    ),
    "libs/pdr-jobs/tests/only_a_test.cpp": (
        "auto value = snapshot[::dynamic_config::PDR_NO_DEFAULT];\n"
    ),
}
"""Дерево одной самопроверки.

`PDR_NO_DEFAULT` читается ТОЛЬКО проверкой — и обязан считаться непрочитанным:
величина, которую читает лишь её собственный тест, никому не нужна.
"""

SELFTEST_EXPECTED = (
    ("PDR_NO_LIMITS", "без пределов"),
    ("PDR_BAD_FIELDS", "нет поля «owner»"),
    ("PDR_BAD_FIELDS", "вид «важная»"),
    ("PDR_BAD_FIELDS", "юрисдикции «возможно»"),
    ("PDR_FORGOTTEN", "не читает её никто"),
    ("PDR_NO_DEFAULT", "не читает её никто"),
    ("PDR_UNREGISTERED", "нет в configs/dynamic/registry.yaml"),
    ("PDR_SILENT", "без журнала изменений"),
    ("PDR_TOO_SMALL", "меньше её же minimum"),
    ("PDR_WAITS_FOREVER", "такой области нет"),
    ("PDR_ALREADY_THERE", "а её уже читают"),
)

SELFTEST_CLEAN = ("PDR_GOOD",)


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, entries = check(root)

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» у {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: правильная запись объявлена нарушением: {name}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        expected_entries = SELFTEST_REGISTRY.count("\nPDR_") + SELFTEST_REGISTRY.startswith("PDR_")
        if entries != expected_entries:
            print(f"самопроверка: разобрано {entries} записей вместо {expected_entries}",
                  file=sys.stderr)
            return 1

        (root / REGISTRY).write_text("это не реестр\n", encoding="utf-8")
        broken, _ = check(root)
        if not any("разбор не понял" in line for line in broken):
            print("самопроверка: непонятный реестр прошёл мимо проверки", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 1} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Реестр динамических значений (PDR-CFG-01).")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, entries = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Форма записи и правила — {REGISTRY}",
              file=sys.stderr)
        return 1

    print(f"Динамических величин в реестре: {entries}. Код и реестр совпадают.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
