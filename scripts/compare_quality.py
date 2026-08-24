#!/usr/bin/env python3
"""Замер «своя модель против внешней» на фиксированном наборе.

Решение «включать ли внешнюю» принимается числом, а не ощущением от пяти
примеров (ADR-0015). Число считается здесь.

Скрипт МОДЕЛЕЙ НЕ ЗАПУСКАЕТ. Он читает набор из ml/eval/<узел>/cases.jsonl и
прогоны из ml/eval/<узел>/runs/{own,external}.jsonl, считает метрику узла и
говорит, стоит ли включать внешнюю. Разделение намеренное: запуск модели зависит
от железа и весов, а счёт не зависит ни от чего и воспроизводится где угодно.

Метрика у каждого узла своя, потому что узлы отвечают на разные вопросы:

* транскрипция — WER, доля ошибочных слов; ниже лучше;
* рукопись — CER, доля ошибочных символов; ниже лучше;
* эмбеддинги — recall@k: доля запросов, где нужный материал попал в первые k;
* текст — покрытие обязательных фактов.

Метрика для текста выбрана не случайно. Сравнивать свободный текст с эталонным
текстом — значит мерить стиль и выдавать его за смысл. Эталон здесь список
фактов, которые обязаны сохраниться: дата, тема, число решённых заданий, имя
ошибки. Это тот же сквозной принцип продукта: факты считаем детерминированно,
смыслы отдаём модели.

Порог, при котором внешнюю стоит включать, живёт в PDR_AI_NODES
(external_min_gain) — не в этом скрипте: число, выставленное на глаз, правится
без переписывания.

Запуск:
    python3 scripts/compare_quality.py text_generation
    python3 scripts/compare_quality.py --selftest
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Sequence

EVAL = Path("ml/eval")
CONFIGS = Path("configs/dynamic/registry.yaml")
VARIABLE = "PDR_AI_NODES"
DEFAULT_LINE = re.compile(r"^\s{2}default:\s*(?P<value>.+)$")

METRICS = {
    "transcription_final": ("wer", "доля ошибочных слов", False),
    "transcription_live": ("wer", "доля ошибочных слов", False),
    "handwriting": ("cer", "доля ошибочных символов", False),
    "embeddings": ("recall_at_k", "доля запросов с нужным материалом в первых k", True),
    "text_generation": ("fact_coverage", "доля сохранённых обязательных фактов", True),
}

RECALL_K = 3
WORDS = re.compile(r"\w+", re.U)


class MeasureError(Exception):
    """Замер невозможен. Это отказ, а не число из воздуха."""


def levenshtein(first: Sequence[str], second: Sequence[str]) -> int:
    previous = list(range(len(second) + 1))
    for index, left in enumerate(first, start=1):
        current = [index]
        for position, right in enumerate(second, start=1):
            current.append(min(previous[position] + 1,
                               current[position - 1] + 1,
                               previous[position - 1] + (left != right)))
        previous = current
    return previous[-1]


def wer(reference: str, produced: str) -> float:
    expected = WORDS.findall(reference.lower())
    if not expected:
        return 0.0
    return levenshtein(expected, WORDS.findall(produced.lower())) / len(expected)


def cer(reference: str, produced: str) -> float:
    expected = list(reference.strip().lower())
    if not expected:
        return 0.0
    return levenshtein(expected, list(produced.strip().lower())) / len(expected)


def recall_at_k(reference: Sequence[str], produced: Sequence[str]) -> float:
    if not reference:
        return 1.0
    head = list(produced)[:RECALL_K]
    return sum(1 for item in reference if item in head) / len(reference)


def fact_coverage(reference: Sequence[str], produced: str) -> float:
    if not reference:
        return 1.0
    lowered = produced.lower()
    return sum(1 for fact in reference if fact.lower() in lowered) / len(reference)


def score(metric: str, reference, produced) -> float:
    if metric == "wer":
        return wer(reference, produced)
    if metric == "cer":
        return cer(reference, produced)
    if metric == "recall_at_k":
        return recall_at_k(reference, produced)
    if metric == "fact_coverage":
        return fact_coverage(reference, produced)
    raise MeasureError(f"метрика «{metric}» неизвестна")


def read_jsonl(path: Path) -> dict[str, dict]:
    rows: dict[str, dict] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise MeasureError(f"{path}:{number}: строка не разобрана как JSON ({error})") from error
        if "id" not in row:
            raise MeasureError(f"{path}:{number}: у случая нет поля id")
        rows[row["id"]] = row
    return rows


def fixed_set(root: Path, node: str) -> dict[str, dict]:
    base = root / EVAL / node
    cases = base / "cases.jsonl"
    if not cases.is_file():
        raise MeasureError(
            f"{cases.relative_to(root)}: набора нет. Замер идёт по фиксированному набору, "
            f"а не по тому, что оказалось под рукой"
        )

    stamp = base / "set.sha256"
    if not stamp.is_file():
        raise MeasureError(f"{stamp.relative_to(root)}: набор без контрольной суммы")

    digest = hashlib.sha256(cases.read_bytes()).hexdigest()
    if stamp.read_text(encoding="utf-8").split()[0] != digest:
        raise MeasureError(
            f"{cases.relative_to(root)}: набор разошёлся с set.sha256. Прошлые числа считались "
            f"на другом наборе, сравнивать их с новыми нельзя"
        )

    return read_jsonl(cases)


def measure(root: Path, node: str, implementation: str, cases: dict[str, dict]) -> float | None:
    path = root / EVAL / node / "runs" / f"{implementation}.jsonl"
    if not path.is_file():
        return None

    produced = read_jsonl(path)
    missing = sorted(set(cases) - set(produced))
    if missing:
        raise MeasureError(
            f"{path.relative_to(root)}: в прогоне нет случаев {', '.join(missing[:5])}"
            f"{' и ещё ' + str(len(missing) - 5) if len(missing) > 5 else ''}. "
            f"Замер на части набора — это другое число, а не то же самое"
        )

    metric = METRICS[node][0]
    return sum(score(metric, cases[name]["reference"], produced[name]["output"])
               for name in cases) / len(cases)


def threshold(root: Path, node: str) -> float | None:
    path = root / CONFIGS
    if not path.is_file():
        return None
    block = path.read_text(encoding="utf-8").split(f"\n{VARIABLE}:\n", 1)
    if len(block) != 2:
        return None
    for line in block[1].splitlines():
        found = DEFAULT_LINE.match(line)
        if found:
            try:
                return json.loads(found.group("value")).get(node, {}).get("external_min_gain")
            except json.JSONDecodeError:
                return None
    return None


def report(root: Path, node: str) -> int:
    cases = fixed_set(root, node)
    metric, meaning, higher_is_better = METRICS[node]

    own = measure(root, node, "own", cases)
    external = measure(root, node, "external", cases)

    print(f"Узел {node}: {metric} — {meaning}. Случаев в наборе: {len(cases)}.")
    for name, value in (("своя", own), ("внешняя", external)):
        print(f"  {name:8} {'—' if value is None else f'{value:.3f}'}")

    if own is None or external is None:
        print("\nСравнивать нечего: нет прогона "
              f"{'своей' if own is None else 'внешней'} модели. Положите его в "
              f"{(EVAL / node / 'runs')} и повторите.")
        return 0

    gain = (external - own) if higher_is_better else (own - external)
    relative = gain / own if own else gain
    limit = threshold(root, node)

    print(f"\nВыигрыш внешней: {gain:+.3f} ({relative:+.1%} от своей).")
    if limit is None:
        print(f"Порог для узла не найден в {CONFIGS}: решение принимать не по чему.")
        return 1

    if relative >= limit:
        print(f"Порог {limit:.0%} пройден: внешнюю стоит включить — "
              f"PDR_AI_NODES.{node}.implementation = external.")
    else:
        print(f"Порог {limit:.0%} НЕ пройден: своя остаётся. Выигрыш не окупает "
              f"зависимости от чужого решения о цене и доступности (ADR-0015).")
    return 0


SELFTEST_CASES = (
    '{"id": "a", "input": "x", "reference": ["дата", "тема"]}\n'
    '{"id": "b", "input": "y", "reference": ["число"]}\n'
)
SELFTEST_OWN = (
    '{"id": "a", "output": "дата есть, темы нет"}\n'
    '{"id": "b", "output": "число на месте"}\n'
)
SELFTEST_EXTERNAL = (
    '{"id": "a", "output": "дата и тема на месте"}\n'
    '{"id": "b", "output": "число на месте"}\n'
)


def selftest() -> int:
    """Метрики считают то, что обещают, а неполный или подменённый набор — отказ."""
    import tempfile

    if abs(wer("а б в г", "а б д г") - 0.25) > 1e-9:
        print("самопроверка: WER посчитан неверно", file=sys.stderr)
        return 1
    if abs(wer("а б в г", "а б в г") - 0.0) > 1e-9:
        print("самопроверка: WER на совпадении не ноль", file=sys.stderr)
        return 1
    if abs(cer("абвг", "абдг") - 0.25) > 1e-9:
        print("самопроверка: CER посчитан неверно", file=sys.stderr)
        return 1
    if abs(recall_at_k(["a", "b"], ["a", "x", "y", "b"]) - 0.5) > 1e-9:
        print("самопроверка: recall@k посчитан неверно", file=sys.stderr)
        return 1
    if abs(fact_coverage(["дата", "тема"], "дата на месте") - 0.5) > 1e-9:
        print("самопроверка: покрытие фактов посчитано неверно", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        base = root / EVAL / "text_generation"
        (base / "runs").mkdir(parents=True)
        (base / "cases.jsonl").write_text(SELFTEST_CASES, encoding="utf-8")
        (base / "set.sha256").write_text(
            hashlib.sha256((base / "cases.jsonl").read_bytes()).hexdigest() + "\n",
            encoding="utf-8",
        )
        (base / "runs" / "own.jsonl").write_text(SELFTEST_OWN, encoding="utf-8")
        (base / "runs" / "external.jsonl").write_text(SELFTEST_EXTERNAL, encoding="utf-8")

        cases = fixed_set(root, "text_generation")
        own = measure(root, "text_generation", "own", cases)
        external = measure(root, "text_generation", "external", cases)
        if abs(own - 0.75) > 1e-9 or abs(external - 1.0) > 1e-9:
            print(f"самопроверка: покрытие своей {own}, внешней {external} — ожидалось "
                  f"0.75 и 1.0", file=sys.stderr)
            return 1

        (base / "runs" / "own.jsonl").write_text('{"id": "a", "output": "дата"}\n',
                                                 encoding="utf-8")
        try:
            measure(root, "text_generation", "own", cases)
        except MeasureError as error:
            if "нет случаев" not in str(error):
                print(f"самопроверка: не та ошибка о неполном прогоне: {error}", file=sys.stderr)
                return 1
        else:
            print("самопроверка: замер на части набора прошёл", file=sys.stderr)
            return 1

        (base / "cases.jsonl").write_text(SELFTEST_CASES + '{"id": "c"}\n', encoding="utf-8")
        try:
            fixed_set(root, "text_generation")
        except MeasureError as error:
            if "разошёлся с set.sha256" not in str(error):
                print(f"самопроверка: не та ошибка о подменённом наборе: {error}",
                      file=sys.stderr)
                return 1
        else:
            print("самопроверка: подменённый набор прошёл", file=sys.stderr)
            return 1

    print("Самопроверка пройдена: четыре метрики считают обещанное, неполный прогон и "
          "подменённый набор отвергнуты.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Замер качества: своя против внешней.")
    parser.add_argument("node", nargs="?", choices=sorted(METRICS), help="какой узел мерить")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--selftest", action="store_true", help="проверить сам замер и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    if not arguments.node:
        parser.error("назовите узел: " + ", ".join(sorted(METRICS)))

    try:
        return report(arguments.root, arguments.node)
    except MeasureError as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
