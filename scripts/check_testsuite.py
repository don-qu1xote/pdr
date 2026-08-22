#!/usr/bin/env python3
"""Правила тестового контура: контур штатный, самодельного стенда не бывает.

Болезнь предыдущего проекта: `conftest.py` руками поднимал пять бинарников,
разводил порты и держал собственные HTTP-моки — при том что `testsuite-support`
был подключён во всех сервисах. Каркас стоял, а плагином не пользовались.

Здесь эта история закрыта проверкой, а не уговором. Запрещено:

* поднимать сервис самим — `subprocess`, `Popen`, `os.system`: сервис поднимает и
  гасит `pytest_userver`;
* держать свой HTTP-сервер — `http.server`, `flask`, `aiohttp.web`: внешние
  зависимости мокирует `mockserver`;
* ходить к сервису своим HTTP-клиентом — `requests`, `httpx`: запросы идут через
  `service_client`;
* раздавать порты самим — `socket.socket`, `bind(`: порты раздаёт плагин;
* спать вместо ожидания — `time.sleep` запрещён совсем, `asyncio.sleep` требует
  явного маркера с причиной. Фоновую работу дожидаются `testpoint`, время двигают
  `mocked_time`, состояние сервиса — `service_client.sync_with_service()`;
* иметь тесты, не подключившие плагин: каталог с `test_*.py`, у которого ни в
  одном `conftest.py` нет `pytest_plugins` с `pytest_userver`, — это ровно
  «каркас стоял, плагином не пользовались».

Исключение пишется в той же строке и с причиной — тогда оно видно на ревью и
находится одним `grep`:

    await asyncio.sleep(1)  # контур-ok: проверяем, что за секунду НИЧЕГО не пришло

Исключения нет ровно у одного запрета: `time.sleep` в асинхронном наборе не
оправдан никогда — он останавливает не тест, а весь цикл событий.

Пока python-тестов в дереве нет, проверка честно говорит, что проверять нечего, и
не роняет сборку: сервиса нет (`services/` пуст), а значит, нет и контура.
Правило при этом уже действует — оно сработает на первом же тесте.

Запуск:
    python3 scripts/check_testsuite.py
    python3 scripts/check_testsuite.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

SKIPPED_DIRS = frozenset({".git", "build", "out", "node_modules", "_deps", "__pycache__", "venv"})

# Каталоги, где живут не тесты контура, а инструменты разработчика: они ходят в
# базу через psql и поднимают что угодно, потому что сервиса им не нужно.
TOOLING_DIRS = ("scripts",)

PLUGIN = "pytest_userver"

# Исключение с обязательной причиной: «контур-ok:» без объяснения не считается.
WAIVER = re.compile(r"#\s*контур-ok:\s*(\S.*)$")

# Каждое правило: имя, что искать, чем это заменяется и можно ли отступить с
# причиной. Заменяющее средство названо в самом сообщении — иначе проверка
# сообщает о запрете, но не о выходе.
Rule = tuple[str, re.Pattern[str], str, bool]

RULES: tuple[Rule, ...] = (
    (
        "свой стенд",
        re.compile(r"\b(?:subprocess|Popen|os\.system|os\.spawn\w*|os\.fork)\b"),
        "сервис поднимает и гасит pytest_userver: фикстура service_client, "
        "а не свой процесс",
        True,
    ),
    (
        "свой HTTP-сервер",
        re.compile(
            r"\b(?:http\.server|BaseHTTPRequestHandler|HTTPServer|socketserver|"
            r"flask|Flask|aiohttp\.web|uvicorn|werkzeug|httpretty|responses\.activate)\b"
        ),
        "внешние зависимости мокирует mockserver: @mockserver.json_handler(...)",
        True,
    ),
    (
        "свой HTTP-клиент к сервису",
        re.compile(r"\b(?:requests\.\w+|httpx\.\w+|urllib\.request|aiohttp\.ClientSession)\b"),
        "запросы к сервису идут через service_client",
        True,
    ),
    (
        "свои порты",
        re.compile(r"\b(?:socket\.socket\(|\.bind\(|get_free_port\(|find_free_port\()"),
        "порты раздаёт плагин; чужой адрес берётся из mockserver.base_url. "
        "Аннотация типа socket.socket нарушением не считается — считается вызов",
        True,
    ),
    (
        "sleep вместо ожидания",
        re.compile(r"\basyncio\.sleep\("),
        "фоновую работу дожидаются testpoint, время двигают mocked_time, "
        "состояние сервиса — service_client.sync_with_service()",
        True,
    ),
    (
        "блокирующий sleep",
        re.compile(r"\btime\.sleep\("),
        "в асинхронном наборе он останавливает весь цикл событий, а не один тест; "
        "исключения у этого запрета нет",
        False,
    ),
)

PYTEST_PLUGINS = re.compile(r"^\s*pytest_plugins\s*=", re.M)


def strip_comments_and_strings(text: str) -> str:
    """Убрать комментарии и строковые литералы, сохранив номера строк.

    Без этого «flask» в строке документации сойдёт за зависимость, а рассказ про
    time.sleep в комментарии — за вызов.
    """
    out: list[str] = []
    index = 0
    size = len(text)
    quote: str | None = None

    while index < size:
        symbol = text[index]

        if quote is None:
            if symbol == "#":
                while index < size and text[index] != "\n":
                    out.append(" ")
                    index += 1
                continue
            for candidate in ('"""', "'''"):
                if text.startswith(candidate, index):
                    quote = candidate
                    out.append("   ")
                    index += 3
                    break
            else:
                if symbol in "\"'":
                    quote = symbol
                    out.append(" ")
                    index += 1
                    continue
                out.append(symbol)
                index += 1
            continue

        if symbol == "\\" and index + 1 < size:
            out.append("  " if text[index + 1] != "\n" else " \n")
            index += 2
            continue
        if text.startswith(quote, index):
            out.append(" " * len(quote))
            index += len(quote)
            quote = None
            continue
        out.append("\n" if symbol == "\n" else " ")
        index += 1

    return "".join(out)


def python_files(root: Path) -> Iterator[Path]:
    """Файлы тестов контура: test_*.py и conftest.py, кроме инструментов."""
    for path in sorted(root.rglob("*.py")):
        parts = path.relative_to(root).parts
        if any(part in SKIPPED_DIRS or part.startswith(".") for part in parts):
            continue
        if parts and parts[0] in TOOLING_DIRS:
            continue
        if path.name == "conftest.py" or path.name.startswith("test_"):
            yield path


def check_file(path: Path, display: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    code = strip_comments_and_strings(text)
    raw_lines = text.splitlines()

    violations: list[str] = []
    for number, line in enumerate(code.splitlines(), start=1):
        original = raw_lines[number - 1] if number <= len(raw_lines) else ""
        waived = bool(WAIVER.search(original))

        for name, pattern, instead, escapable in RULES:
            found = pattern.search(line)
            if not found:
                continue
            if waived and escapable:
                continue
            waiver_hint = ""
            if escapable:
                waiver_hint = " Если отступить всё-таки нужно, напишите в этой строке " \
                              "«# контур-ok: причина»."
            violations.append(
                f"{display}:{number}: {name} — «{found.group(0)}». {instead}.{waiver_hint}"
            )
    return violations


def check_plugin_is_used(root: Path, files: Sequence[Path]) -> list[str]:
    """Тесты есть — значит, плагин обязан быть подключён.

    Ищется вверх по дереву: `conftest.py` с `pytest_plugins`, где назван
    pytest_userver. Подключение в родительском каталоге считается — так его и
    делают, один раз на весь набор.
    """
    violations: list[str] = []
    test_dirs = sorted({path.parent for path in files if path.name.startswith("test_")})

    for directory in test_dirs:
        current = directory
        connected = False
        while True:
            conftest = current / "conftest.py"
            if conftest.is_file():
                text = conftest.read_text(encoding="utf-8", errors="replace")
                if PYTEST_PLUGINS.search(text) and PLUGIN in text:
                    connected = True
                    break
            if current == root:
                break
            current = current.parent

        if not connected:
            try:
                display = directory.relative_to(root)
            except ValueError:
                display = directory
            violations.append(
                f"{display}: тесты есть, а плагин не подключён. В conftest.py этого "
                f"каталога или выше обязано быть «pytest_plugins» с {PLUGIN}: иначе "
                f"каркас стоит, а плагином не пользуются — ровно то, чем болел "
                f"предыдущий проект"
            )
    return violations


def check(root: Path) -> tuple[list[str], int]:
    files = list(python_files(root))

    violations: list[str] = []
    for path in files:
        try:
            display = path.relative_to(root)
        except ValueError:
            display = path
        violations.extend(check_file(path, display))

    violations.extend(check_plugin_is_used(root, files))
    return violations, len(files)


SELFTEST_FILES = {
    # Чистый случай: так выглядит правильный набор — плагин подключён, сервис
    # поднимает он, внешнее закрыто mockserver, фон дожидается testpoint.
    "services/main/tests/conftest.py": (
        "import pytest\n"
        "\n"
        "pytest_plugins = ['pytest_userver.plugins.postgresql']\n"
    ),
    "services/main/tests/test_good.py": (
        "async def test_lesson_is_booked(service_client, mockserver, testpoint):\n"
        "    @mockserver.json_handler('/yookassa/payments')\n"
        "    def _payments(request):\n"
        "        return {'status': 'succeeded'}\n"
        "\n"
        "    @testpoint('outbox-drained')\n"
        "    def drained(data):\n"
        "        pass\n"
        "\n"
        "    response = await service_client.post('/v1/lessons')\n"
        "    assert response.status == 201\n"
        "    await drained.wait_call()\n"
    ),
    # Свой стенд и свои порты.
    "services/main/tests/test_own_stand.py": (
        "import subprocess\n"
        "import socket\n"
        "\n"
        "def test_service():\n"
        "    port = socket.socket()\n"
        "    service = subprocess.Popen(['./pdr_main'])\n"
        "    service.kill()\n"
    ),
    # Свой HTTP-мок и свой клиент к сервису.
    "services/main/tests/test_own_mock.py": (
        "import http.server\n"
        "import requests\n"
        "\n"
        "def test_payment():\n"
        "    server = http.server.HTTPServer(('', 8080), None)\n"
        "    requests.post('http://localhost:8080/v1/lessons')\n"
    ),
    # Sleep вместо ожидания: блокирующий — совсем нельзя, даже с маркером,
    # асинхронный — только с маркером и причиной.
    "services/main/tests/test_sleep.py": (
        "import asyncio\n"
        "import time\n"
        "\n"
        "async def test_background(service_client):\n"
        "    time.sleep(5)\n"
        "    time.sleep(5)  # контур-ok: маркер не спасает блокирующий sleep\n"
        "    await asyncio.sleep(3)\n"
        "    await asyncio.sleep(1)  # контур-ok: проверяем, что НИЧЕГО не пришло\n"
        "    await asyncio.sleep(2)  # контур-ok:\n"
    ),
    # Разговор про запреты — не нарушение: строки и комментарии не считаются.
    "services/main/tests/test_talks_about_it.py": (
        "async def test_documented(service_client):\n"
        "    # никакого time.sleep(1) и никакого subprocess.Popen здесь нет\n"
        "    reason = 'flask и requests.get упомянуты строкой'\n"
        "    assert reason\n"
    ),
    # Тесты без подключённого плагина — та самая болезнь предыдущего проекта.
    "services/chat/tests/test_orphan.py": (
        "async def test_chat(service_client):\n"
        "    assert await service_client.get('/ping')\n"
    ),
    # Инструменты разработчика проверкой не покрыты: им можно и psql, и subprocess.
    "scripts/check_something.py": (
        "import subprocess\n"
        "\n"
        "def main():\n"
        "    subprocess.run(['psql', '-c', 'select 1'])\n"
    ),
}

SELFTEST_EXPECTED = {
    ("services/main/tests/test_own_stand.py", "свой стенд"),
    ("services/main/tests/test_own_stand.py", "свои порты"),
    ("services/main/tests/test_own_mock.py", "свой HTTP-сервер"),
    ("services/main/tests/test_own_mock.py", "свой HTTP-клиент к сервису"),
    ("services/main/tests/test_sleep.py", "блокирующий sleep"),
    ("services/main/tests/test_sleep.py", "sleep вместо ожидания"),
    ("services/chat/tests", "плагин не подключён"),
}

SELFTEST_CLEAN = (
    "services/main/tests/test_good.py",
    "services/main/tests/test_talks_about_it.py",
    "scripts/check_something.py",
)


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, content in SELFTEST_FILES.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, checked = check(root)

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» в {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for name in SELFTEST_CLEAN:
            if any(name in line for line in violations):
                print(f"самопроверка: правильный файл объявлен нарушением: {name}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        # Маркер обязан работать построчно и не спасать блокирующий sleep, а
        # маркер без причины — не маркер.
        blocking = [line for line in violations if "блокирующий sleep" in line]
        if len(blocking) != 2:
            print(f"самопроверка: маркер простил блокирующий sleep: {blocking}", file=sys.stderr)
            return 1

        waived = [line for line in violations if "sleep вместо ожидания" in line]
        if len(waived) != 2:
            print(f"самопроверка: маркер сработал не построчно или простил «контур-ok:» "
                  f"без причины: {waived}", file=sys.stderr)
            return 1

        # Пустое дерево — не нарушение: сервиса нет, контура нет, проверять нечего.
        empty, empty_checked = check(root / "services" / "nothing-here")
        if empty or empty_checked:
            print("самопроверка: пустое дерево объявлено нарушением", file=sys.stderr)
            return 1

        if checked != len(SELFTEST_FILES) - len(TOOLING_DIRS):
            print(f"самопроверка: проверено {checked} файлов", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Правила тестового контура на pytest_userver.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, checked = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Контур штатный: service_client, mockserver, "
              f"testpoint, mocked_time, chaos-прокси — docs/testing.md", file=sys.stderr)
        return 1

    if not checked:
        print("Python-тестов в дереве пока нет: сервиса нет, значит нет и контура. "
              "Правило действует и сработает на первом же тесте.")
        return 0

    print(f"Тестов контура проверено: {checked}. Своего стенда, своих моков и sleep нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
