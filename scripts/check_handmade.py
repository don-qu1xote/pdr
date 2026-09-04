#!/usr/bin/env python3
"""Признаки самоделки там, где в userver есть штатное (ADR-0013).

В предыдущем проекте руками написаны свой LRU-кэш, свой ограничитель скорости с
токенами, свой размыкатель цепи и свой экспоненциальный откат — при том что в
userver всё это есть. Каждая самоделка потом требовала своих тестов, своей отладки
и своего человека, который помнит, как она работает.

Проверка ловит четыре признака:

* класс с именем механизма: `*Cache`, `*LruMap`, `*RateLimiter`, `*CircuitBreaker`,
  `*Backoff`, `*TokenBucket`, `*Throttler`;
* мапа с временем жизни, собранная руками: `std::unordered_map` в файле, где рядом
  живут `ttl`, `expires_at`, `evict`;
* периодическое задание на потоке: `std::thread`, `.detach()`,
  `std::this_thread::sleep_for`;
* вторая библиотека вместо штатной: `nlohmann::json`, `rapidjson`,
  `prometheus-cpp`, `opentelemetry`, `spdlog`, а также СИНХРОННЫЙ HTTP-клиент —
  `libcurl`, `cpr`, `httplib`, `boost::beast`. Последний случай стоит отдельного
  слова: синхронный клиент в корутинном рантайме блокирует поток целиком, и один
  медленный чужой сервис останавливает всё, что делит с ним поток. Наружу ходят
  штатным `clients::http` через одну дверь (PDR-ARCH-10,
  docs/architecture/integrations.md).

Ловит по ИМЕНАМ, и это осознанно: линтер закрывает случайную самоделку, а не
саботаж. От намеренного обхода защищает не grep, а то, что отступление требует
строки в ADR.

ОТСТУПЛЕНИЕ. Своё пишется, только когда штатного нет. Тогда в коде ставится
директива инструмента с ключом, а в ADR-0013 — строка с тем же ключом:

    class RingBuffer final {  // штатное-ok: ring-buffer — в userver его нет

    docs/adr/0013-standard-over-handmade.md:
        * **ring-buffer** — очередь фиксированной длины: в userver нет.

Одно сверяется с другим: директива без строки в ADR роняет сборку, строка без
директивы — тоже (иначе список отступлений зарастает мёртвыми записями).

Запуск:
    python3 scripts/check_handmade.py
    python3 scripts/check_handmade.py --selftest
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

SOURCE_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".ipp", ".c", ".cc", ".cpp", ".cxx"})
SKIPPED_DIRS = frozenset({".git", "build", "out", "node_modules", "_deps", "third_party",
                          "compile_fail", "__pycache__"})

ADR = Path("docs/adr/0013-standard-over-handmade.md")

WAIVER = re.compile(r"//\s*штатное-ok:\s*([a-z0-9-]+)\s*[—-]\s*(\S.*)$")

ADR_WAIVER = re.compile(r"^\s*\*\s+\*\*([a-z0-9-]+)\*\*")

HANDMADE_NAMES = re.compile(
    r"\b(?:class|struct)\s+(\w*(?:Cache|LruMap|LruSet|RateLimiter|CircuitBreaker|"
    r"Backoff|TokenBucket|Throttler|Throttle))\b"
)

MAP_DECLARATION = re.compile(r"\bstd::(?:unordered_)?(?:multi)?map\s*<")
TTL_WORDS = re.compile(r"\b(?:ttl|expires_at|expire_at|expiry|expires|evict|eviction)\b",
                       re.IGNORECASE)

THREAD_PERIODIC = re.compile(r"\b(?:std::thread|std::jthread|\.detach\(\)|"
                             r"std::this_thread::sleep_for|std::async)\b")

FOREIGN_LIBRARIES = re.compile(
    r"\b(?:nlohmann|rapidjson|jsoncpp|json11|picojson|boost::property_tree|"
    r"prometheus|statsd|opentelemetry|spdlog|glog|easyloggingpp|"
    r"curl_easy_\w+|curl_multi_\w+|CURLcode|CURLOPT_\w+|cpr|httplib|cpp_httplib|"
    r"boost::beast)\b"
)

REPLACEMENTS = {
    "имя механизма": "кэш — cache::LruMap или components::CachingComponentBase, "
                     "ограничитель — utils::TokenBucket, сброс нагрузки — congestion_control, "
                     "повторы — utils::RetryBudget",
    "мапа с временем жизни": "cache::LruMap умеет вытеснение по времени; справочник с "
                             "обновлением — components::CachingComponentBase",
    "задание на потоке": "utils::PeriodicTask, а одиночное на кластер — "
                         "storages::postgres::DistLock",
    "вторая библиотека": "форматы — formats::json и formats::parse::To, метрики — "
                         "utils::statistics, трассировка — tracing::Span, логи — LOG_*",
}


def source_files(root: Path) -> Iterator[Path]:
    for path in sorted(root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        if any(part in SKIPPED_DIRS or part.startswith(".") for part in path.parts):
            continue
        yield path


def strip_comments(text: str, keep_waivers: bool = True) -> str:
    """Убрать комментарии и литералы, сохранив номера строк.

    Директивы `// штатное-ok:` остаются: по ним проверка узнаёт об отступлении.
    Слово «cache» в пояснении или в строке — не самоделка.
    """
    out: list[str] = []
    index = 0
    size = len(text)

    while index < size:
        symbol = text[index]
        following = text[index + 1] if index + 1 < size else ""

        if symbol == "/" and following == "/":
            end = text.find("\n", index)
            end = size if end == -1 else end
            fragment = text[index:end]
            if keep_waivers and WAIVER.search(fragment):
                out.append(fragment)
            else:
                out.append(" " * len(fragment))
            index = end
            continue

        if symbol == "/" and following == "*":
            end = text.find("*/", index + 2)
            end = size if end == -1 else end + 2
            out.append("".join("\n" if s == "\n" else " " for s in text[index:end]))
            index = end
            continue

        if symbol in "\"'":
            quote = symbol
            out.append(" ")
            index += 1
            while index < size:
                if text[index] == "\\" and index + 1 < size:
                    out.append("  ")
                    index += 2
                    continue
                closing = text[index] == quote
                out.append("\n" if text[index] == "\n" else " ")
                index += 1
                if closing:
                    break
            continue

        out.append(symbol)
        index += 1

    return "".join(out)


def check_file(path: Path, display: Path) -> tuple[list[str], set[str]]:
    """Нарушения файла и ключи отступлений, которые он объявил."""
    text = path.read_text(encoding="utf-8", errors="replace")
    code = strip_comments(text)
    lines = code.splitlines()

    keys: set[str] = set()
    waived_lines: set[int] = set()
    for number, line in enumerate(lines, start=1):
        found = WAIVER.search(line)
        if found:
            keys.add(found.group(1))
            waived_lines.add(number)

    violations: list[str] = []
    has_ttl_words = bool(TTL_WORDS.search(code))

    for number, line in enumerate(lines, start=1):
        if number in waived_lines:
            continue

        named = HANDMADE_NAMES.search(line)
        if named:
            violations.append(
                f"{display}:{number}: самоделка по имени — «{named.group(1)}». "
                f"{REPLACEMENTS['имя механизма']}"
            )

        if has_ttl_words and MAP_DECLARATION.search(line):
            violations.append(
                f"{display}:{number}: мапа с временем жизни собрана руками. "
                f"{REPLACEMENTS['мапа с временем жизни']}"
            )

        thread = THREAD_PERIODIC.search(line)
        if thread:
            violations.append(
                f"{display}:{number}: задание на потоке — «{thread.group(0)}». "
                f"{REPLACEMENTS['задание на потоке']}"
            )

        foreign = FOREIGN_LIBRARIES.search(line)
        if foreign:
            violations.append(
                f"{display}:{number}: вторая библиотека вместо штатной — "
                f"«{foreign.group(0)}». {REPLACEMENTS['вторая библиотека']}"
            )

    return violations, keys


def adr_waivers(text: str) -> set[str]:
    """Ключи отступлений, ЗАПИСАННЫЕ в ADR.

    Блоки кода пропускаются: в них живут примеры того, как строка выглядит, и
    пример не должен считаться настоящим отступлением.
    """
    keys: set[str] = set()
    inside_fence = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            inside_fence = not inside_fence
            continue
        if inside_fence:
            continue
        found = ADR_WAIVER.match(line)
        if found:
            keys.add(found.group(1))
    return keys


def check(root: Path) -> tuple[list[str], int]:
    violations: list[str] = []
    declared: dict[str, Path] = {}
    checked = 0

    for path in source_files(root):
        checked += 1
        try:
            display = path.relative_to(root)
        except ValueError:
            display = path
        found, keys = check_file(path, display)
        violations.extend(found)
        for key in keys:
            declared.setdefault(key, display)

    adr = root / ADR
    if not adr.is_file():
        if declared:
            violations.append(
                f"{ADR}: отступления объявлены ({', '.join(sorted(declared))}), а ADR нет. "
                f"Своё пишется только когда штатного нет, и причина живёт в ADR"
            )
        return violations, checked

    recorded = adr_waivers(adr.read_text(encoding="utf-8"))

    for key, where in sorted(declared.items()):
        if key not in recorded:
            violations.append(
                f"{where}: отступление «{key}» без строки в {ADR}. Своё пишется только "
                f"когда штатного нет — и тогда причина записана там, а не в коде"
            )

    for key in sorted(recorded - set(declared)):
        violations.append(
            f"{ADR}: отступление «{key}» записано, а в коде его нет. Список отступлений "
            f"не зарастает мёртвыми записями: уберите строку или верните директиву"
        )

    return violations, checked


SELFTEST_FILES = {
    "libs/pdr-events/include/events/in_memory_bus.hpp": (
        "#pragma once\n"
        "#include <unordered_map>\n"
        "class InMemoryBus final {\n"
        "    std::unordered_map<int, int> handlers_;\n"
        "};\n"
    ),
    "libs/pdr-core/src/core/talks.cpp": (
        "// свой Cache и std::thread здесь только на словах\n"
        "const char* kNote = \"nlohmann::json и prometheus упомянуты строкой\";\n"
    ),
    "libs/pdr-core/src/infrastructure/my_cache.hpp": (
        "#pragma once\n"
        "class MyCache final {\n"
        "public:\n"
        "    int Get(int key) const;\n"
        "};\n"
    ),
    "libs/pdr-core/src/infrastructure/limits.hpp": (
        "#pragma once\n"
        "class TenantRateLimiter final {};\n"
        "class ProviderCircuitBreaker final {};\n"
        "struct ExponentialBackoff final {};\n"
    ),
    "libs/pdr-core/src/infrastructure/ttl_store.hpp": (
        "#pragma once\n"
        "#include <unordered_map>\n"
        "class Store final {\n"
        "    struct Row { int value; long expires_at; };\n"
        "    std::unordered_map<int, Row> rows_;\n"
        "    void Evict();\n"
        "};\n"
    ),
    "libs/pdr-core/src/infrastructure/ticker.cpp": (
        "#include <thread>\n"
        "void Start() {\n"
        "    std::thread worker{[] { std::this_thread::sleep_for(std::chrono::seconds{1}); }};\n"
        "    worker.detach();\n"
        "}\n"
    ),
    "libs/pdr-core/src/infrastructure/foreign.cpp": (
        "#include <nlohmann/json.hpp>\n"
        "#include <spdlog/spdlog.h>\n"
    ),
    "libs/pdr-core/src/infrastructure/outside.cpp": (
        "#include <curl/curl.h>\n"
        "void Ask() {\n"
        "    auto* handle = curl_easy_init();\n"
        "    curl_easy_setopt(handle, CURLOPT_URL, \"https://example.org\");\n"
        "}\n"
    ),
    "libs/pdr-core/src/infrastructure/asked.cpp": (
        "#include <userver/clients/http/client.hpp>\n"
        "void Ask(userver::clients::http::Client& http) {\n"
        "    http.CreateRequest().get(\"https://example.org\").perform();\n"
        "}\n"
    ),
    "libs/pdr-core/src/infrastructure/allowed.hpp": (
        "#pragma once\n"
        "class RingBuffer final {  // штатное-ok: ring-buffer — в userver его нет\n"
        "};\n"
    ),
    "libs/pdr-core/src/infrastructure/undocumented.hpp": (
        "#pragma once\n"
        "class SecondCache final {  // штатное-ok: second-cache — причина есть, строки нет\n"
        "};\n"
    ),
    "docs/adr/0013-standard-over-handmade.md": (
        "# ADR-0013\n"
        "\n"
        "Пример строки, а не сама строка:\n"
        "\n"
        "```\n"
        "* **example-key** — так выглядит запись об отступлении.\n"
        "```\n"
        "\n"
        "#### Чего в userver нет — отступления\n"
        "\n"
        "* **ring-buffer** — очередь фиксированной длины: в userver нет.\n"
        "* **forgotten** — отступление, которое из кода уже убрали.\n"
    ),
}

SELFTEST_EXPECTED = {
    ("my_cache.hpp", "MyCache"),
    ("limits.hpp", "TenantRateLimiter"),
    ("limits.hpp", "ProviderCircuitBreaker"),
    ("limits.hpp", "ExponentialBackoff"),
    ("ttl_store.hpp", "мапа с временем жизни"),
    ("ticker.cpp", "std::thread"),
    ("foreign.cpp", "nlohmann"),
    ("foreign.cpp", "spdlog"),
    ("outside.cpp", "curl_easy_init"),
    ("undocumented.hpp", "без строки в"),
    ("0013-standard-over-handmade.md", "«forgotten» записано, а в коде его нет"),
}

SELFTEST_CLEAN = ("in_memory_bus.hpp", "talks.cpp", "allowed.hpp", "asked.cpp", "example-key")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана падать на нарочной самоделке."""
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

        if not checked:
            print("самопроверка: не проверено ни одного файла", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Штатное вместо самодельного (ADR-0013).")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, checked = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Список штатного и порядок отступления — "
              f"{ADR}", file=sys.stderr)
        return 1

    print(f"Проверено файлов: {checked}. Самоделок вместо штатного нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
