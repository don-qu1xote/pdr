#!/usr/bin/env python3
"""Планы горячих запросов на ЖИВОЙ базе: индекс либо применяется, либо нет.

Потерянный индекс — самая тихая поломка производительности из всех. Запрос
отвечает то же самое; ошибок нет; тесты зелёные. Меняется только время ответа,
и только там, где данных много, — то есть не у разработчика. Узнают об этом от
пользователя.

Поэтому здесь снимается EXPLAIN каждого запроса из db/explain/hot_queries.sql и
проверяется ровно две вещи:

* в плане есть индекс, который список назвал обязательным;
* в плане нет перебора таблицы, кроме явно разрешённого.

Планы снимаются под ролью приложения и с объявленным арендатором: политика RLS
добавляет условие по tenant_id, и план без неё — план другого запроса.

Планировщик считает по статистике, поэтому на пустой базе любой план — перебор,
и это ПРАВИЛЬНЫЙ план. Данные засевает db/explain/seed.sql, и без него проверка
не имеет смысла: make test-plans делает оба шага.

Запуск:
    make test-plans
    make test-plans ENV_PROFILE=ci
    python3 scripts/check_plans.py --selftest
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_isolation as live  # noqa: E402  (после правки sys.path)

HOT_QUERIES = Path("db/explain/hot_queries.sql")
SEED = Path("db/explain/seed.sql")

SUBSTITUTIONS = {
    "tenant": "0e0e0e0e-0000-4000-8000-000000000007",
    "person": "0e0e0e0e-0001-4000-8000-000000007042",
    "guardian": "0e0e0e0e-0001-4000-8000-000000007041",
    "email": "person-7-42@example.test",
    "session": "0e0e0e0e-0006-4000-8000-000000007042",
    "token": "e8c38699bcf7891a10c3c3ad1a56673b94a08da1fecbb6e8b46990538c7b50b2",
    "job": "pdr-plans-reminders",
    "keep_days": "30",
}

HEADER = re.compile(r"^--\s*([а-яa-z_]+)\s*:\s*(.*)$")
REQUIRED_KEYS = ("запрос", "откуда", "индекс")
KNOWN_KEYS = REQUIRED_KEYS + ("перебор",)


class PlanError(Exception):
    """Список горячих запросов разобрать не удалось. Это отказ."""


class HotQuery:
    def __init__(self, headers: dict[str, str], sql: str, line: int) -> None:
        self.name = headers["запрос"]
        self.origin = headers["откуда"]
        self.indexes = tuple(part.strip() for part in headers["индекс"].split(",") if part.strip())
        self.seq_scan_allowed = tuple(
            part.strip() for part in headers.get("перебор", "").split(",") if part.strip()
        )
        self.sql = sql
        self.line = line

    def rendered(self) -> str:
        text = self.sql
        for key, value in SUBSTITUTIONS.items():
            text = text.replace("{" + key + "}", value)
        return text


def parse(text: str, source: str) -> list[HotQuery]:
    """Блоки «заголовки + запрос» по порядку."""
    queries: list[HotQuery] = []
    headers: dict[str, str] = {}
    header_line = 0
    body: list[str] = []
    body_line = 0

    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()

        if not body:
            if not line:
                headers = {}
                header_line = 0
                continue
            if line.startswith("--"):
                found = HEADER.match(line)
                if found and found.group(1) in KNOWN_KEYS:
                    if not headers:
                        header_line = number
                    headers[found.group(1)] = found.group(2).strip()
                elif headers:
                    headers[list(headers)[-1]] += " " + line.lstrip("- ").strip()
                continue

            if not headers:
                raise PlanError(
                    f"{source}:{number}: запрос без заголовков. Каждому горячему запросу "
                    f"предшествуют строки «-- ключ: значение», иначе неизвестно, какой "
                    f"индекс он обязан использовать"
                )
            body_line = number
        body.append(raw)

        if line.endswith(";"):
            missing = [key for key in REQUIRED_KEYS if key not in headers]
            if missing:
                raise PlanError(
                    f"{source}:{header_line or body_line}: у запроса нет ключей "
                    f"{', '.join(missing)}. Горячий запрос без «откуда» — догадка"
                )
            queries.append(HotQuery(headers, "\n".join(body), body_line))
            headers = {}
            header_line = 0
            body = []

    if body:
        raise PlanError(f"{source}:{body_line}: запрос не закончен точкой с запятой")
    if not queries:
        raise PlanError(f"{source}: горячих запросов нет — проверять нечего")
    return queries


def walk(node: dict) -> tuple[list[str], list[str]]:
    """Имена индексов и таблицы, которые план читает перебором."""
    indexes: list[str] = []
    scanned: list[str] = []

    name = node.get("Index Name")
    if name:
        indexes.append(name)
    indexes.extend(node.get("Conflict Arbiter Indexes", []))

    if node.get("Node Type") == "Seq Scan":
        scanned.append(node.get("Relation Name", "?"))

    for child in node.get("Plans", []):
        child_indexes, child_scanned = walk(child)
        indexes.extend(child_indexes)
        scanned.extend(child_scanned)

    return indexes, scanned


def inspect(query: HotQuery, plan: dict) -> list[str]:
    indexes, scanned = walk(plan)
    problems: list[str] = []

    if not any(name in indexes for name in query.indexes):
        expected = " или ".join(f"«{name}»" for name in query.indexes)
        used = ", ".join(indexes) if indexes else "ни одного индекса"
        problems.append(
            f"потерян индекс: план не использует {expected}. Использует: {used}. "
            f"Ответ прежний, время — другое, и увидит это пользователь, а не тест"
        )

    for table in scanned:
        if table not in query.seq_scan_allowed:
            problems.append(
                f"перебор таблицы {table}: план читает её целиком. Если так и задумано, "
                f"это пишется в списке ключом «перебор» и с причиной"
            )

    return problems


def explain(database: live.Database, query: HotQuery) -> dict:
    answer = database.app_text(
        f"explain (format json, costs on) {query.rendered()}", SUBSTITUTIONS["tenant"]
    )
    try:
        return json.loads(answer)[0]["Plan"]
    except (ValueError, KeyError, IndexError) as error:
        raise live.DatabaseError(
            f"{query.name}: план не разобран ({error}). Ответ базы:\n{answer}"
        ) from error


def check(database: live.Database, queries: Sequence[HotQuery], source: str) -> int:
    failed = 0
    for query in queries:
        problems = inspect(query, explain(database, query))
        if problems:
            failed += 1
            print(f"  ПРОВАЛ  {query.name}", file=sys.stderr)
            for line in problems:
                print(f"          {line}", file=sys.stderr)
            print(f"          список: {source}:{query.line}, откуда: {query.origin}",
                  file=sys.stderr)
        else:
            print(f"  ок      {query.name} — {', '.join(query.indexes)}")
    return failed


SELFTEST_LIST = """
-- запрос: person_by_id
-- откуда: самый частый доступ
-- индекс: identity_person_pk
select 1 from identity_person where id = '{person}';

-- запрос: cleanup
-- откуда: уборка по возрасту
-- индекс: jobs_effect_by_age, jobs_effect_pk
-- перебор: jobs_run
delete from jobs_effect where produced_at < now() - make_interval(days => {keep_days});
"""

SELFTEST_GOOD_PLAN = {
    "Node Type": "Index Scan",
    "Index Name": "identity_person_pk",
    "Relation Name": "identity_person",
}

SELFTEST_LOST_INDEX_PLAN = {
    "Node Type": "Seq Scan",
    "Relation Name": "identity_person",
}

SELFTEST_NESTED_PLAN = {
    "Node Type": "Delete",
    "Plans": [
        {
            "Node Type": "Bitmap Heap Scan",
            "Relation Name": "jobs_effect",
            "Plans": [{"Node Type": "Bitmap Index Scan", "Index Name": "jobs_effect_by_age"}],
        },
        {"Node Type": "Seq Scan", "Relation Name": "jobs_run"},
    ],
}


def selftest() -> int:
    """Отрицательные случаи: проверка обязана падать на потерянном индексе и на
    переборе, и обязана НЕ падать на плане, который в порядке."""
    queries = parse(SELFTEST_LIST, "самопроверка")
    if len(queries) != 2:
        print(f"самопроверка: разобрано {len(queries)} запросов из 2", file=sys.stderr)
        return 1

    person, cleanup = queries

    if "{person}" in person.rendered() or SUBSTITUTIONS["person"] not in person.rendered():
        print("самопроверка: подстановка не сработала", file=sys.stderr)
        return 1

    if inspect(person, SELFTEST_GOOD_PLAN):
        print("самопроверка: исправный план объявлен нарушением", file=sys.stderr)
        return 1

    problems = inspect(person, SELFTEST_LOST_INDEX_PLAN)
    if not any("потерян индекс" in line for line in problems):
        print("самопроверка: потерянный индекс не пойман", file=sys.stderr)
        return 1
    if not any("перебор таблицы identity_person" in line for line in problems):
        print("самопроверка: перебор таблицы не пойман", file=sys.stderr)
        return 1

    if inspect(cleanup, SELFTEST_NESTED_PLAN):
        print("самопроверка: вложенный план разобран неверно", file=sys.stderr)
        return 1

    try:
        parse("-- запрос: x\n-- индекс: y\nselect 1;\n", "самопроверка")
    except PlanError as error:
        if "откуда" not in str(error):
            print(f"самопроверка: не та ошибка разбора: {error}", file=sys.stderr)
            return 1
    else:
        print("самопроверка: запрос без «откуда» прошёл разбор", file=sys.stderr)
        return 1

    try:
        parse("select 1;\n", "самопроверка")
    except PlanError as error:
        if "без заголовков" not in str(error):
            print(f"самопроверка: не та ошибка разбора: {error}", file=sys.stderr)
            return 1
    else:
        print("самопроверка: запрос без заголовков прошёл разбор", file=sys.stderr)
        return 1

    print("Самопроверка пройдена: потерянный индекс и перебор пойманы, исправный план "
          "и вложенный разобраны верно, запрос без «откуда» и SQL без заголовков "
          "отвергнуты.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Планы горячих запросов на живой базе.")
    parser.add_argument("--list", type=Path, default=root / HOT_QUERIES)
    parser.add_argument("--psql", default="psql", help="чем спрашивать базу")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    try:
        source = str(arguments.list.relative_to(root))
    except ValueError:
        source = str(arguments.list)

    try:
        queries = parse(arguments.list.read_text(encoding="utf-8"), source)
    except (OSError, PlanError) as error:
        print(str(error), file=sys.stderr)
        return 1

    database = live.Database(arguments.psql)
    try:
        failed = check(database, queries, source)
    except live.DatabaseError as error:
        print(str(error), file=sys.stderr)
        return 2

    if failed:
        print(f"\nПланов не в порядке: {failed} из {len(queries)}. Потерянный индекс не даёт "
              f"ни ошибки, ни красного теста — только время ответа. "
              f"Что делать: docs/architecture/plans.md", file=sys.stderr)
        return 1

    print(f"\nПланы сняты: {len(queries)} горячих запросов, каждый идёт по своему индексу.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
