#!/usr/bin/env python3
"""Формат кода и ВЕРСИЯ форматтера, которой он проверен.

Проверять «установлен ли clang-format» бесполезно. Он не байт-стабилен между
версиями: разработчик с семнадцатой версией отформатирует чисто, отправит, и CI
покраснеет на восемнадцатой. Виноватым при этом выглядит CI, а починить его
нечем — обе стороны «правы».

Поэтому версия закреплена файлом .clang-format-version и проверяется до самого
форматирования. Несовпадение — отказ с названием обеих версий, а не
предупреждение в конце вывода.

Форматтер ищется по порядку: переменная CLANG_FORMAT, потом clang-format-<пин>,
потом clang-format. Разработчику с другой версией по умолчанию достаточно
поставить нужную рядом — CLANG_FORMAT прописывать не нужно.

Этот скрипт зовёт цель Makefile, а её зовут и хук githooks/pre-commit, и джоба
CI. Одна команда, а не две похожие: иначе «у меня чисто» и «в CI красно»
расходятся, и разойдутся они молча.

Запуск:
    make fmt-check      проверить, ничего не меняя
    make fmt            привести к .clang-format
    python3 scripts/check_format.py --selftest
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence

PIN = Path(".clang-format-version")
SOURCES = ("libs", "services")
SUFFIXES = (".hpp", ".cpp")

VERSION = re.compile(r"version\s+(\d+)\.(\d+)\.(\d+)")


class FormatError(Exception):
    """Форматтера нужной версии нет. Это отказ, а не предупреждение."""


def pinned(root: Path) -> str:
    path = root / PIN
    try:
        value = path.read_text(encoding="utf-8").strip()
    except OSError as error:
        raise FormatError(f"{PIN}: файла с закреплённой версией нет ({error})") from error
    if not value.isdigit():
        raise FormatError(f"{PIN}: ожидалась основная версия числом, а не «{value}»")
    return value


def version_of(binary: str) -> str | None:
    """Основная версия форматтера или None, если её не удалось спросить."""
    try:
        result = subprocess.run(
            [binary, "--version"], capture_output=True, text=True, check=False
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    found = VERSION.search(result.stdout)
    return found.group(1) if found else None


def resolve(major: str) -> str:
    """Форматтер закреплённой версии — или отказ с тем, что нашлось."""
    candidates = [name for name in (os.environ.get("CLANG_FORMAT"),
                                    f"clang-format-{major}",
                                    "clang-format") if name]

    seen: list[str] = []
    for name in candidates:
        path = shutil.which(name)
        if not path:
            continue
        found = version_of(path)
        if found == major:
            return path
        seen.append(f"{path} — {'версия ' + found if found else 'версию узнать не удалось'}")

    if not seen:
        raise FormatError(
            f"clang-format {major} не найден. Искал: {', '.join(candidates)}.\n"
            f"    Ubuntu:  sudo apt-get install clang-format-{major}\n"
            f"    macOS:   brew install llvm@{major}\n"
            f"    Другой путь: CLANG_FORMAT=/путь/к/clang-format make fmt-check"
        )

    return _mismatch(major, seen)


def _mismatch(major: str, seen: Sequence[str]) -> str:
    raise FormatError(
        f"нужна версия clang-format {major}, а найдено:\n"
        + "".join(f"    {line}\n" for line in seen)
        + f"    Версия закреплена в {PIN}, потому что clang-format не байт-стабилен "
        f"между версиями: чужая версия отформатирует «чисто», а CI покраснеет.\n"
        f"    Поставьте clang-format-{major} рядом или укажите путь: "
        f"CLANG_FORMAT=... make fmt-check"
    )


def sources(root: Path) -> list[str]:
    files: list[str] = []
    for directory in SOURCES:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix in SUFFIXES and path.is_file():
                files.append(str(path.relative_to(root)))
    return files


def run(binary: str, files: Sequence[str], fix: bool, root: Path) -> int:
    if not files:
        print("исходников C++ пока нет — форматировать нечего")
        return 0

    command = [binary, "-i"] if fix else [binary, "--dry-run", "--Werror"]
    result = subprocess.run(command + list(files), cwd=root, check=False)
    if result.returncode != 0:
        return result.returncode

    if fix:
        print(f"формат приведён к .clang-format ({len(files)} файлов)")
    else:
        print(f"формат совпадает с .clang-format ({len(files)} файлов)")
    return 0


# Подставные форматтеры: каждый в своём каталоге, потому что проверять надо
# «у разработчика ТОЛЬКО эта версия», а не «эта версия есть среди прочих».
STUBS = {
    # каталог: (имя файла, что печатает --version, код возврата)
    "good": ("clang-format-18", "Ubuntu clang-format version 18.1.3 (1ubuntu1)", 0),
    "old": ("clang-format", "clang-format version 14.0.0", 0),
    "broken": ("clang-format", "не тот вывод вовсе", 0),
    "dead": ("clang-format", "", 1),
    "empty": (None, "", 0),
}


def _write_stubs(root: Path) -> None:
    for directory, (name, output, code) in STUBS.items():
        place = root / directory
        place.mkdir()
        if not name:
            continue
        path = place / name
        path.write_text(f"#!/bin/sh\necho '{output}'\nexit {code}\n", encoding="utf-8")
        path.chmod(0o755)


def selftest() -> int:
    """Отрицательные случаи: проверяется ВЕРСИЯ, а не наличие.

    У подставных форматтеров чужая версия, мусор вместо версии и код возврата
    ошибки. Все они установлены и все отвечают — ровно поэтому «command -v
    clang-format» их пропускает, а эта проверка обязана отвергнуть.
    """
    import tempfile

    original = os.environ.get("PATH", "")
    had = os.environ.get("CLANG_FORMAT")

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        _write_stubs(root)
        try:
            os.environ.pop("CLANG_FORMAT", None)

            os.environ["PATH"] = str(root / "good")
            if Path(resolve("18")).name != "clang-format-18":
                print("самопроверка: закреплённая версия не найдена", file=sys.stderr)
                return 1

            # Явно указанный путь тоже обязан работать: не у всех форматтер
            # называется clang-format-18.
            os.environ["CLANG_FORMAT"] = str(root / "good" / "clang-format-18")
            if Path(resolve("18")).name != "clang-format-18":
                print("самопроверка: CLANG_FORMAT не учтён", file=sys.stderr)
                return 1
            os.environ.pop("CLANG_FORMAT")

            for place, fragment in (("old", "версия 14"),
                                    ("broken", "узнать не удалось"),
                                    ("dead", "узнать не удалось")):
                os.environ["PATH"] = str(root / place)
                try:
                    resolve("18")
                except FormatError as error:
                    if fragment not in str(error) or "нужна версия clang-format 18" not in str(error):
                        print(f"самопроверка: {place}: не то сообщение: {error}", file=sys.stderr)
                        return 1
                else:
                    print(f"самопроверка: {place} принят за версию 18", file=sys.stderr)
                    return 1

            # Форматтера нет вовсе — отказ с тем, как его поставить.
            os.environ["PATH"] = str(root / "empty")
            try:
                resolve("18")
            except FormatError as error:
                if "не найден" not in str(error) or "apt-get install" not in str(error):
                    print(f"самопроверка: не то сообщение об отсутствии: {error}", file=sys.stderr)
                    return 1
            else:
                print("самопроверка: отсутствие форматтера не замечено", file=sys.stderr)
                return 1
        finally:
            os.environ["PATH"] = original
            os.environ.pop("CLANG_FORMAT", None)
            if had is not None:
                os.environ["CLANG_FORMAT"] = had

    place = Path(__file__).resolve().parent.parent
    if not (place / PIN).is_file():
        print(f"самопроверка: нет {PIN} — закреплять нечего", file=sys.stderr)
        return 1

    print(f"Самопроверка пройдена: чужая версия, мусор вместо версии, неработающий "
          f"двоичный файл и его отсутствие отвергнуты; закреплённая версия — "
          f"{pinned(place)}.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Формат кода и версия форматтера.")
    parser.add_argument("--fix", action="store_true", help="править файлы, а не только проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    try:
        major = pinned(root)
        binary = resolve(major)
    except FormatError as error:
        print(str(error), file=sys.stderr)
        return 1

    return run(binary, sources(root), fix=arguments.fix, root=root)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
