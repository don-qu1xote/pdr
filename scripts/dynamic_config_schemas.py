#!/usr/bin/env python3
"""Реестр динамических значений — в то, что читает штатный chaotic (PDR-ARCH-09).

ЗАЧЕМ ЭТОТ ШАГ ВООБЩЕ ЕСТЬ. Штатное порождение структур динамического конфига
(`chaotic-gen-dynamic-configs`) читает ОДИН ФАЙЛ НА ОДНУ ВЕЛИЧИНУ и берёт её имя
из имени файла. Реестр же у нас один файл на все величины — и остаётся им:
человек читает реестр целиком, а не двадцать три файла по одному полю, и
`configs/dynamic/registry.yaml` назван источником правды в восьми проверках и
одиннадцати документах.

Поэтому здесь РАСКЛАДКА, а не порождение: ни одной строки C++ этот скрипт не
пишет и писать не будет. Он режет реестр на блоки по верхнеуровневым именам и
раскладывает их по файлам, которые ждёт штатный инструмент. Порождает — он.

РЕЖЕТ ТЕКСТОМ, А НЕ РАЗБОРОМ. Схема попадает в порождение ровно такой, какой её
написал человек: разбор и обратная запись YAML переставили бы ключи, развернули
бы якоря и переписали бы многострочные значения — и порождённый тип отличался бы
от написанной схемы способом, который никто не заметит.

ЗАПИСИ С «awaits» ПРОПУСКАЮТСЯ. Такая величина решена, но кода, который её
читает, ещё нет; порождать ключ для неё значило бы объявить её заведённой и
лишить поле «awaits» смысла (scripts/check_dynamic_configs.py).

ЗАПИСИ С «штатная: да» ПРОПУСКАЮТСЯ ТОЖЕ, и по причине посерьёзнее: ключ у них
объявляет сам userver (`POSTGRES_QUERIES_COMMAND_CONTROL` и соседи). Второй
ключ с тем же именем — вторая ячейка хранилища: значение, положенное в одну, из
другой не видно, и выглядит это как «конфиг не применился».

Запуск:
    python3 scripts/dynamic_config_schemas.py --out build/dynamic_configs
    python3 scripts/dynamic_config_schemas.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

REGISTRY = Path("configs/dynamic/registry.yaml")

NAME = re.compile(r"^(?P<name>[A-Z][A-Z0-9_]*):\s*$")

AWAITS = re.compile(r"^\s{2}awaits:\s*(?P<area>\S+)\s*$", re.M)

STANDARD = re.compile(r"^\s{2}штатная:\s*да\s*$", re.M)
"""Величина принадлежит штатному механизму: ключ объявляет userver, не мы.

Порождать ей структуру нельзя, и это не осторожность: второй
`dynamic_config::Key` с тем же именем — вторая ячейка хранилища, и значение,
которое положили в одну, не видно из другой.
"""


def blocks(text: str) -> list[tuple[str, str]]:
    """[(имя, текст записи без её заголовка)] в порядке реестра."""
    lines = text.splitlines()
    starts: list[tuple[int, str]] = []
    for number, line in enumerate(lines):
        found = NAME.match(line)
        if found:
            starts.append((number, found.group("name")))

    found_blocks: list[tuple[str, str]] = []
    for index, (number, name) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(lines)
        body = lines[number + 1 : end]
        found_blocks.append((name, "\n".join(body).rstrip() + "\n"))
    return found_blocks


def dedent(body: str) -> str:
    """Запись без отступа записи: у штатного инструмента она сама себе документ."""
    out = []
    for line in body.splitlines():
        if not line.strip():
            out.append("")
        elif line.startswith("  "):
            out.append(line[2:])
        else:
            out.append(line)
    return "\n".join(out).rstrip() + "\n"


def lay_out(registry: str, out: Path) -> list[str]:
    """Разложить реестр по файлам штатного инструмента. Возвращает имена величин."""
    out.mkdir(parents=True, exist_ok=True)

    written: list[str] = []
    for name, body in blocks(registry):
        if AWAITS.search(body) or STANDARD.search(body):
            continue
        target = out / f"{name}.yaml"
        content = dedent(body)
        if not target.is_file() or target.read_text(encoding="utf-8") != content:
            target.write_text(content, encoding="utf-8")
        written.append(name)

    keep = {f"{name}.yaml" for name in written}
    for stale in sorted(out.glob("*.yaml")):
        if stale.name not in keep:
            stale.unlink()

    return written


SELFTEST_REGISTRY = """# Заголовок реестра, который в записи не попадает.

PDR_READY:
  description: заведённая величина
  default:
    hours: 24
  schema:
    type: object
    properties:
      hours:
        type: integer
        minimum: 1
        maximum: 720

PDR_WAITING:
  description: величина, ждущая своей области
  awaits: SCHED
  default: 4
  schema:
    type: integer
    minimum: 1
    maximum: 10

USERVER_SOMETHING_STANDARD:
  description: величина штатного механизма, ключ у неё чужой
  штатная: да
  default: true
  schema:
    type: boolean
"""


def selftest() -> int:
    """Разложено ровно заведённое, отступ снят, схема не переписана."""
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory) / "dynamic_configs"

        stale = out / "PDR_GONE.yaml"
        out.mkdir(parents=True)
        stale.write_text("default: 1\n", encoding="utf-8")

        written = lay_out(SELFTEST_REGISTRY, out)

        if written != ["PDR_READY"]:
            print(f"самопроверка: разложено {written} вместо ['PDR_READY']", file=sys.stderr)
            return 1

        if stale.exists():
            print("самопроверка: файл снятой величины остался лежать", file=sys.stderr)
            return 1

        laid = (out / "PDR_READY.yaml").read_text(encoding="utf-8")
        if not laid.startswith("description: заведённая величина\n"):
            print(f"самопроверка: отступ записи не снят:\n{laid}", file=sys.stderr)
            return 1
        if "    hours:\n      type: integer\n      minimum: 1\n      maximum: 720\n" not in laid:
            print(f"самопроверка: схема переписана при раскладке:\n{laid}", file=sys.stderr)
            return 1
        if "PDR_WAITING" in laid or (out / "PDR_WAITING.yaml").exists():
            print("самопроверка: ждущая величина разложена как заведённая", file=sys.stderr)
            return 1
        if (out / "USERVER_SOMETHING_STANDARD.yaml").exists():
            print("самопроверка: штатной величине порождён свой ключ", file=sys.stderr)
            return 1

    print("Самопроверка пройдена: ждущее и штатное пропущены, снятое убрано, "
          "схема не переписана.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Реестр — в файлы штатного порождения.")
    parser.add_argument("--root", type=Path, default=root, help="корень дерева")
    parser.add_argument("--out", type=Path, help="куда разложить")
    parser.add_argument("--selftest", action="store_true", help="проверить саму раскладку")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    if not arguments.out:
        parser.error("нужен --out")

    registry = arguments.root / REGISTRY
    if not registry.is_file():
        print(f"{REGISTRY}: реестра нет — раскладывать нечего", file=sys.stderr)
        return 1

    written = lay_out(registry.read_text(encoding="utf-8"), arguments.out)
    print(f"Разложено величин: {len(written)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
