#!/usr/bin/env python3
"""Суверенитет ИИ-узлов: у каждого есть своя реализация, работающая без сети.

ADR-0014 требует пережить отказ внешнего сервиса. ADR-0015 требует большего:
деградация обязана быть рабочей, а не заглушкой. Проверяется то, что можно
проверить без запуска моделей:

* таблица узлов в docs/architecture/ai-sovereignty.md заполнена целиком — у
  каждого узла названы своя реализация, внешняя, потеря при отключении внешней,
  ответственный и дата последней проверки. Модели меняются быстро, и строка без
  даты через год врёт молча;
* умолчание в PDR_AI_NODES совпадает с таблицей: у узла со своей реализацией
  оно `own`. Умолчание, которым никто не пользуется, перестаёт работать
  незаметно;
* у узла без своей реализации записано условие пересмотра ЧИСЛАМИ. «Потом» и
  «когда-нибудь» условиями не считаются;
* обращение наружу не появилось вне зарегистрированного адаптера интеграции.
  Это и есть незаметно возникшая жёсткая зависимость: строка, из-за которой
  сквозной прогон без сети однажды покраснеет;
* фиксированный набор для замера качества не разошёлся со своей контрольной
  суммой: изменился набор — изменились все прошлые числа.

Список признаков обращения наружу (`OUTBOUND` ниже) короткий и закрытый: он
ловит не стиль, а сам факт сетевого вызова в слое, где его быть не должно.

Свежесть даты проверка НЕ сторожит намеренно. Проверка, краснеющая по календарю
без единой правки кода, отключается первой; когда перепроверять таблицу —
написано в самой таблице, и это работа человека.

Запуск:
    python3 scripts/check_sovereignty.py
    python3 scripts/check_sovereignty.py --selftest
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

SOVEREIGNTY = Path("docs/architecture/ai-sovereignty.md")
INTEGRATIONS = Path("docs/architecture/integrations.md")
CONFIGS = Path("configs/dynamic/registry.yaml")
EVAL = Path("ml/eval")
VARIABLE = "PDR_AI_NODES"

TABLE_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
NODE_CELL = re.compile(r"^`([a-z][a-z0-9_]*)`$")
HAS_OWN = re.compile(r"^\*{0,2}есть\*{0,2}\s*[:—-]", re.I)
NO_OWN = re.compile(r"^\*{0,2}нет\*{0,2}\b", re.I)
ISO_DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
DIGITS = re.compile(r"\d[\d\s]{2,}")
VAGUE = ("потом", "когда-нибудь", "TODO", "по мере", "в будущем")

DEFAULT_LINE = re.compile(r"^\s{2}default:\s*(?P<value>.+)$")

OUTBOUND = (
    ("clients::http", "клиент HTTP userver"),
    ("curl_easy", "libcurl"),
    ("curl/curl.h", "libcurl"),
    ("httplib", "cpp-httplib"),
    ("boost::asio::ip::tcp", "сокет boost::asio"),
    ("::socket(", "системный сокет"),
    ("getaddrinfo", "разрешение имени"),
)

SOURCE_SUFFIXES = frozenset({".hpp", ".cpp"})
SEARCHED_ROOTS = ("libs", "services")
SKIPPED = frozenset({"build", "out", "_deps", "__pycache__", "tests"})


class SovereigntyError(Exception):
    """Разбор не удался. Это отказ, а не предупреждение."""


def parse_nodes(text: str) -> list[dict[str, str]]:
    """Строки таблицы узлов: шесть столбцов, ключ — имя узла в обратных кавычках."""
    columns = ("node", "own", "external", "lost", "owner", "checked")
    nodes: list[dict[str, str]] = []

    for line in text.splitlines():
        found = TABLE_ROW.match(line.strip())
        if not found:
            continue
        cells = [cell.strip() for cell in found.group("cells").split("|")]
        if len(cells) != len(columns) or not NODE_CELL.match(cells[0]):
            continue
        row = dict(zip(columns, cells))
        row["node"] = NODE_CELL.match(cells[0]).group(1)
        nodes.append(row)

    return nodes


def config_default(text: str) -> dict[str, dict[str, object]]:
    """Умолчание PDR_AI_NODES из реестра динамических значений."""
    block = text.split(f"\n{VARIABLE}:\n", 1)
    if len(block) != 2:
        raise SovereigntyError(f"{CONFIGS}: величины {VARIABLE} нет — переключать узлы нечем")

    for line in block[1].splitlines():
        found = DEFAULT_LINE.match(line)
        if found:
            try:
                return json.loads(found.group("value"))
            except json.JSONDecodeError as error:
                raise SovereigntyError(
                    f"{CONFIGS}: умолчание {VARIABLE} не разобрано как JSON ({error})"
                ) from error
    raise SovereigntyError(f"{CONFIGS}: у величины {VARIABLE} нет умолчания")


def revision_section(text: str, node: str) -> str:
    """Раздел про пересмотр для узла без своей реализации."""
    for chunk in text.split("\n## ")[1:]:
        heading = chunk.splitlines()[0]
        if node in heading and "пересмотр" in heading.lower():
            return chunk
    return ""


def check_table(nodes: Sequence[dict[str, str]], text: str,
                default: dict[str, dict[str, object]]) -> list[str]:
    violations: list[str] = []

    for row in nodes:
        node = row["node"]

        for column, value in row.items():
            if not value:
                violations.append(f"{SOVEREIGNTY}: у узла {node} пуст столбец «{column}»")

        if not ISO_DATE.match(row["checked"]):
            violations.append(
                f"{SOVEREIGNTY}: у узла {node} дата проверки «{row['checked']}» не вида "
                f"ГГГГ-ММ-ДД. Модели меняются быстро, и строка без даты через год врёт молча"
            )

        chosen = default.get(node)
        if chosen is None:
            violations.append(
                f"{CONFIGS}: узла {node} нет в умолчании {VARIABLE} — переключать его нечем"
            )
            continue

        if HAS_OWN.match(row["own"]):
            if chosen.get("implementation") != "own":
                violations.append(
                    f"{CONFIGS}: у узла {node} есть своя реализация, а умолчание "
                    f"«{chosen.get('implementation')}». Своя, включённая по умолчанию, "
                    f"проверяется каждым днём работы; своя «на случай отказа» — только в "
                    f"день отказа (ADR-0015)"
                )
        elif NO_OWN.match(row["own"]):
            section = revision_section(text, node)
            if not section:
                violations.append(
                    f"{SOVEREIGNTY}: у узла {node} нет своей реализации и нет раздела с "
                    f"условием пересмотра. Пока нет данных, план был бы обещанием — но "
                    f"условие обязано быть наблюдаемым"
                )
            else:
                if len(DIGITS.findall(section)) < 2:
                    violations.append(
                        f"{SOVEREIGNTY}: условие пересмотра для узла {node} без чисел. "
                        f"Наблюдаемое условие — это то, что можно посмотреть запросом"
                    )
                for word in VAGUE:
                    if word.lower() in section.lower():
                        violations.append(
                            f"{SOVEREIGNTY}: в условии пересмотра для узла {node} стоит "
                            f"«{word}». Это не условие, а его отсутствие"
                        )
        else:
            violations.append(
                f"{SOVEREIGNTY}: у узла {node} столбец «своя» — «{row['own'][:40]}». "
                f"Ожидается «есть: …» или «нет»"
            )

    named = {row["node"] for row in nodes}
    for node in sorted(set(default) - named):
        violations.append(
            f"{SOVEREIGNTY}: узел {node} переключается в {VARIABLE}, но его нет в таблице. "
            f"Узел без строки — это узел, о котором никто не отвечает"
        )

    return violations


def registered_adapters(root: Path) -> set[str]:
    """Пути, которым разрешено ходить наружу: порты и адаптеры из реестра интеграций."""
    path = root / INTEGRATIONS
    if not path.is_file():
        return set()

    allowed: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        found = TABLE_ROW.match(line.strip())
        if not found:
            continue
        cells = [cell.strip().strip("`") for cell in found.group("cells").split("|")]
        for cell in cells:
            if cell.endswith((".hpp", ".cpp")):
                allowed.add(cell)
                allowed.add(str(Path(cell).parent))
    return allowed


def check_outbound(root: Path) -> list[str]:
    """Обращение наружу вне зарегистрированного адаптера."""
    allowed = registered_adapters(root)
    violations: list[str] = []

    for name in SEARCHED_ROOTS:
        base = root / name
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            relative = path.relative_to(root)
            if any(part in SKIPPED for part in relative.parts):
                continue
            if str(relative) in allowed or str(relative.parent) in allowed:
                continue

            body = path.read_text(encoding="utf-8", errors="replace")
            for token, what in OUTBOUND:
                if token in body:
                    violations.append(
                        f"{relative}: обращение наружу ({what}) вне адаптера интеграции. "
                        f"Внешний вызов живёт в адаптере, названном в {INTEGRATIONS}; "
                        f"здесь он однажды уронит сквозной прогон без сети (ADR-0015)"
                    )
                    break

    return violations


def check_fixed_sets(root: Path) -> list[str]:
    """Фиксированный набор совпадает со своей контрольной суммой."""
    base = root / EVAL
    if not base.is_dir():
        return []

    violations: list[str] = []
    for cases in sorted(base.rglob("cases.jsonl")):
        digest = hashlib.sha256(cases.read_bytes()).hexdigest()
        stamp = cases.parent / "set.sha256"
        if not stamp.is_file():
            violations.append(
                f"{cases.relative_to(root)}: набор без контрольной суммы. Фиксированный "
                f"набор фиксируется файлом set.sha256, иначе он не фиксированный"
            )
            continue
        recorded = stamp.read_text(encoding="utf-8").split()[0]
        if recorded != digest:
            violations.append(
                f"{cases.relative_to(root)}: набор разошёлся с set.sha256. Изменился "
                f"набор — изменились и все прошлые числа: обновите сумму осознанно"
            )
    return violations


def check(root: Path) -> tuple[list[str], int]:
    violations = check_outbound(root) + check_fixed_sets(root)

    path = root / SOVEREIGNTY
    if not path.is_file():
        violations.append(f"{SOVEREIGNTY}: таблицы узлов нет, а правило есть (ADR-0015)")
        return violations, 0

    text = path.read_text(encoding="utf-8")
    nodes = parse_nodes(text)
    if not nodes:
        violations.append(f"{SOVEREIGNTY}: в таблице узлов нет ни одной строки")
        return violations, 0

    try:
        default = config_default((root / CONFIGS).read_text(encoding="utf-8")
                                 if (root / CONFIGS).is_file() else "")
    except SovereigntyError as error:
        violations.append(str(error))
        return violations, len(nodes)

    violations.extend(check_table(nodes, text, default))
    return violations, len(nodes)


SELFTEST_TABLE = """# Суверенитет ИИ-узлов

| Узел | Своя | Внешняя | Что теряем без внешней | Отвечает | Проверено |
| --- | --- | --- | --- | --- | --- |
| `transcription_final` | есть: GigaAM | облако | ничего | владелец ml | 2026-08-24 |
| `embeddings` | есть: bge-m3 | провайдер | ничего | владелец exercises | вчера |
| `text_generation` | есть: Qwen | провайдер LLM | формулировки грубее | владелец notes | 2026-08-24 |
| `handwriting` | нет | сервис распознавания | автоматическую проверку | владелец practice | 2026-08-24 |
| `vague` | нет | сервис | функцию | владелец | 2026-08-24 |

## `handwriting`: почему своей нет и когда пересмотрим

1. открытый датасет не менее 10 000 страниц;
2. не менее 10 000 своих работ с согласием.

## `vague`: почему своей нет и когда пересмотрим

Сделаем потом, когда-нибудь.
"""

SELFTEST_CONFIGS = """
PDR_AI_NODES:
  description: узлы
  default: {"transcription_final": {"implementation": "own"}, "embeddings": {"implementation": "external"}, "text_generation": {"implementation": "own"}, "handwriting": {"implementation": "external"}, "vague": {"implementation": "external"}, "orphan": {"implementation": "own"}}
  schema:
    type: object
"""

SELFTEST_EXPECTED = (
    ("embeddings", "умолчание «external»"),
    ("embeddings", "не вида ГГГГ-ММ-ДД"),
    ("vague", "«потом»"),
    ("vague", "без чисел"),
    ("orphan", "нет в таблице"),
    ("leaky.cpp", "обращение наружу"),
    ("cases.jsonl", "разошёлся с set.sha256"),
)


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / SOVEREIGNTY).parent.mkdir(parents=True)
        (root / SOVEREIGNTY).write_text(SELFTEST_TABLE, encoding="utf-8")
        (root / CONFIGS).parent.mkdir(parents=True)
        (root / CONFIGS).write_text(SELFTEST_CONFIGS, encoding="utf-8")

        leaky = root / "libs/pdr-notes/src/notes/application/leaky.cpp"
        leaky.parent.mkdir(parents=True)
        leaky.write_text("auto response = clients::http::Get(url);\n", encoding="utf-8")

        cases = root / EVAL / "handwriting" / "cases.jsonl"
        cases.parent.mkdir(parents=True)
        cases.write_text('{"id": "1", "input": "a", "reference": ["b"]}\n', encoding="utf-8")
        (cases.parent / "set.sha256").write_text("0" * 64 + "\n", encoding="utf-8")

        violations, nodes = check(root)

        if nodes != 5:
            print(f"самопроверка: разобрано {nodes} узлов вместо пяти", file=sys.stderr)
            return 1

        for name, fragment in SELFTEST_EXPECTED:
            if not any(name in line and fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}» у {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        for clean in ("transcription_final", "text_generation"):
            if any(clean in line for line in violations):
                print(f"самопроверка: правильный узел объявлен нарушением: {clean}",
                      file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Суверенитет ИИ-узлов: ADR-0015.")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, nodes = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Правило — docs/adr/"
              f"0015-own-model-for-every-ai-node.md, таблица — {SOVEREIGNTY}", file=sys.stderr)
        return 1

    print(f"ИИ-узлов в таблице: {nodes}. У каждого назван ответственный и дата проверки, "
          f"умолчания совпадают с таблицей, обращений наружу вне адаптеров нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
