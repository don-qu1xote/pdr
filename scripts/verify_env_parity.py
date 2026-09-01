#!/usr/bin/env python3
"""Сверка профилей окружения: один compose на всех, различия — только по списку.

Профиль — это файл deploy/env/<профиль>.env.example. Скрипт прогоняет
`docker compose config` по каждому профилю и сверяет отрисованные конфигурации:

* набор сервисов одинаковый;
* образ и команда запуска у каждого сервиса одинаковые;
* вся остальная конфигурация совпадает буквально.

Как это проверяется: переменным из явного списка PROFILE_DIFFERENCES во всех
профилях подставляется одно и то же значение, и профили отрисовываются заново.
После этого любое расхождение — настоящее. Сравнения текста «на глазок» и
догадок «похоже, это порт, пусть отличается» здесь нет: отличаться разрешено
ровно тем переменным, которые названы в списке, и ничему больше.

Сверка идёт по ПРИМЕРАМ, а не по настоящим deploy/env/*.env: примеры лежат в
истории, а настоящие файлы — нет, и проверка обязана работать на свежем клоне
и в CI.

Нарушение печатается человеку и даёт код возврата 1.

Запуск:
    python3 scripts/verify_env_parity.py
    python3 scripts/verify_env_parity.py --selftest
"""

from __future__ import annotations

import argparse
import difflib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence

EXAMPLE_SUFFIX = ".env.example"

TEMPLATE = ".env.example"
"""Образец окружения: полный список переменных с пустыми значениями.

Профилем он не является — это список того, что профиль обязан заполнить, и
документация по запуску. Значения в нём пустые намеренно: образец со значением
однажды скопируют целиком, и «change-me» доживёт до прода.
"""

PROFILE_DIFFERENCES = {
    "ENV_PROFILE": "имя профиля отличает проект в docker",
    "POSTGRES_PASSWORD": "пароль базы задаётся на месте",
    "POSTGRES_PORT": "порт занят чужим процессом чаще, чем хотелось бы",
    "SCHEDULING_PORT": "тот же случай: порт сервиса занят чужим процессом",
    "SCHEDULING_IMAGE": "тег образа несёт имя профиля, иначе профили затирают друг друга",
}

COMPARED_FIELDS = ("image", "command", "entrypoint")


def read_env(path: Path) -> dict[str, str]:
    """Переменные из env-файла: KEY=VALUE, без подстановок и без экранирования."""
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or "=" not in stripped:
            continue
        key, _, value = stripped.partition("=")
        values[key.strip()] = value.strip()
    return values


def render(compose_file: Path, values: dict[str, str], label: str) -> dict:
    """Отрисовать конфигурацию с заданными значениями переменных."""
    with tempfile.NamedTemporaryFile("w", suffix=".env", encoding="utf-8", delete=False) as handle:
        for name, value in values.items():
            handle.write(f"{name}={value}\n")
        env_file = Path(handle.name)

    try:
        result = subprocess.run(
            [
                "docker", "compose",
                "--env-file", str(env_file),
                "-f", str(compose_file),
                "config", "--format", "json",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
    finally:
        env_file.unlink(missing_ok=True)

    if result.returncode != 0:
        raise RuntimeError(
            f"профиль {label}: docker compose config не отработал:\n{result.stderr.strip()}"
        )
    return json.loads(result.stdout)


def normalized(config: dict) -> str:
    """Текст конфигурации, устойчивый к порядку ключей."""
    return json.dumps(config, indent=2, sort_keys=True, ensure_ascii=False)


def check(compose_file: Path, env_dir: Path, root: Path,
          differences: dict[str, str] | None = None) -> tuple[list[str], list[str]]:
    """Нарушения и список проверенных профилей."""
    allowed = PROFILE_DIFFERENCES if differences is None else differences
    violations: list[str] = []

    examples = [path for path in sorted(env_dir.glob(f"*{EXAMPLE_SUFFIX}"))
                if path.name != TEMPLATE]
    profiles = [path.name[: -len(EXAMPLE_SUFFIX)] for path in examples]
    if len(profiles) < 2:
        return ([f"{env_dir.relative_to(root)}: профилей меньше двух, сверять нечего"], profiles)

    template = env_dir / TEMPLATE
    expected: set[str] | None = None
    if not template.is_file():
        violations.append(
            f"{(env_dir / TEMPLATE).relative_to(root)}: образца окружения нет. Он и есть "
            f"документация по запуску: полный список переменных с пустыми значениями"
        )
    else:
        blank = read_env(template)
        expected = set(blank)
        for name, value in sorted(blank.items()):
            if value:
                violations.append(
                    f"{template.relative_to(root)}: у {name} стоит значение «{value}». В "
                    f"образце значений не бывает: образец однажды скопируют целиком"
                )

    variables: dict[str, dict[str, str]] = {}
    for profile, example in zip(profiles, examples):
        values = read_env(example)
        variables[profile] = values

        if expected is not None:
            for missing in sorted(expected - set(values)):
                violations.append(
                    f"профиль {profile}: нет переменной {missing}, названной в {TEMPLATE}"
                )
            for extra in sorted(set(values) - expected):
                violations.append(
                    f"{TEMPLATE}: нет переменной {extra}, а профиль {profile} её задаёт. "
                    f"Образец, отставший от профиля, перестаёт быть документацией по запуску"
                )

        if values.get("ENV_PROFILE") != profile:
            violations.append(
                f"{example.relative_to(root)}: ENV_PROFILE={values.get('ENV_PROFILE')!r} "
                f"не совпадает с именем файла ({profile})"
            )

    first = profiles[0]

    canonical = {name: variables[first][name] for name in allowed if name in variables[first]}
    rendered = {
        profile: render(compose_file, {**variables[profile], **canonical}, profile)
        for profile in profiles
    }
    for profile in profiles[1:]:
        names_first = set(variables[first])
        names_other = set(variables[profile])
        for missing in sorted(names_first - names_other):
            violations.append(f"профиль {profile}: нет переменной {missing}, а в {first} она есть")
        for extra in sorted(names_other - names_first):
            violations.append(f"профиль {profile}: есть переменная {extra}, которой нет в {first}")

        services_first = rendered[first].get("services", {})
        services_other = rendered[profile].get("services", {})
        only_first = sorted(set(services_first) - set(services_other))
        only_other = sorted(set(services_other) - set(services_first))
        for name in only_first:
            violations.append(f"профиль {profile}: нет сервиса {name}, а в {first} он есть")
        for name in only_other:
            violations.append(f"профиль {profile}: есть сервис {name}, которого нет в {first}")

        for name in sorted(set(services_first) & set(services_other)):
            for field in COMPARED_FIELDS:
                left = services_first[name].get(field)
                right = services_other[name].get(field)
                if left != right:
                    violations.append(
                        f"сервис {name}: {field} различается — в {first} {left!r}, "
                        f"в {profile} {right!r}"
                    )

        if normalized(rendered[first]) != normalized(rendered[profile]):
            difference = list(
                difflib.unified_diff(
                    normalized(rendered[first]).splitlines(),
                    normalized(rendered[profile]).splitlines(),
                    fromfile=first,
                    tofile=profile,
                    lineterm="",
                )
            )
            interesting = [line for line in difference[2:] if line[:1] in {"+", "-"}]
            violations.append(
                f"профили {first} и {profile} расходятся не только разрешёнными переменными:\n    "
                + "\n    ".join(interesting[:20])
            )

    return violations, profiles


SELFTEST_COMPOSE = """name: "probe-${ENV_PROFILE:?нет ENV_PROFILE}"

x-hardening: &hardening
  read_only: true
  mem_limit: ${MEM_LIMIT:?нет MEM_LIMIT}

services:
  db:
    <<: *hardening
    image: ${DB_IMAGE:?нет DB_IMAGE}
    environment:
      PASSWORD: ${DB_PASSWORD:?нет DB_PASSWORD}
    ports:
      - "${DB_PORT:?нет DB_PORT}:5432"
"""

SELFTEST_TEMPLATE = "ENV_PROFILE=\nMEM_LIMIT=\nDB_IMAGE=\nDB_PASSWORD=\nDB_PORT=\n"

SELFTEST_ENV = {
    "local": "ENV_PROFILE=local\nMEM_LIMIT=256m\nDB_IMAGE=postgres:16\nDB_PASSWORD=local-one\nDB_PORT=5432\n",
    "ci": "ENV_PROFILE=ci\nMEM_LIMIT=256m\nDB_IMAGE=postgres:16\nDB_PASSWORD=ci-one\nDB_PORT=55432\n",
}


def selftest() -> int:
    """Отрицательные случаи: проверка обязана падать там, где расхождение есть."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        env_dir = root / "deploy" / "env"
        env_dir.mkdir(parents=True)
        compose_file = root / "deploy" / "docker-compose.yml"
        compose_file.write_text(SELFTEST_COMPOSE, encoding="utf-8")
        for profile, text in SELFTEST_ENV.items():
            (env_dir / f"{profile}{EXAMPLE_SUFFIX}").write_text(text, encoding="utf-8")
        (env_dir / TEMPLATE).write_text(SELFTEST_TEMPLATE, encoding="utf-8")

        allowed = {
            "ENV_PROFILE": "имя профиля",
            "DB_PASSWORD": "пароль на месте",
            "DB_PORT": "порт занят чужим процессом",
        }

        violations, profiles = check(compose_file, env_dir, root, allowed)
        if violations or sorted(profiles) != ["ci", "local"]:
            print(f"самопроверка: чистая пара не прошла: {violations}", file=sys.stderr)
            return 1

        cases = {
            "образ": "DB_IMAGE=postgres:16\n",
            "предел памяти": "MEM_LIMIT=256m\n",
        }
        replacements = {"образ": "DB_IMAGE=postgres:15\n", "предел памяти": "MEM_LIMIT=512m\n"}
        for name, old in cases.items():
            path = env_dir / f"ci{EXAMPLE_SUFFIX}"
            original = path.read_text(encoding="utf-8")
            path.write_text(original.replace(old, replacements[name]), encoding="utf-8")
            violations, _ = check(compose_file, env_dir, root, allowed)
            path.write_text(original, encoding="utf-8")
            if not violations:
                print(f"самопроверка: расхождение «{name}» не поймано", file=sys.stderr)
                return 1

        original_compose = compose_file.read_text(encoding="utf-8")
        compose_file.write_text(
            original_compose + '  extra:\n    <<: *hardening\n    image: alpine:3.20\n',
            encoding="utf-8",
        )
        with_extra, _ = check(compose_file, env_dir, root, allowed)
        compose_file.write_text(original_compose, encoding="utf-8")
        if with_extra:
            print("самопроверка: одинаковый лишний сервис не должен считаться расхождением",
                  file=sys.stderr)
            return 1

        violations, _ = check(compose_file, env_dir, root, allowed)
        if violations:
            print(f"самопроверка: разрешённые различия посчитаны нарушением: {violations}",
                  file=sys.stderr)
            return 1

    print("Самопроверка пройдена: расхождения по образу и по неразрешённой переменной "
          "пойманы, разрешённые пропущены.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Сверка профилей окружения между собой.")
    parser.add_argument("--compose", type=Path, default=root / "deploy/docker-compose.yml")
    parser.add_argument("--env-dir", type=Path, default=root / "deploy/env")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if subprocess.run(["docker", "compose", "version"], capture_output=True).returncode != 0:
        print("нет docker compose: сверять профили нечем", file=sys.stderr)
        return 2

    if arguments.selftest:
        return selftest()

    try:
        violations, profiles = check(arguments.compose, arguments.env_dir, root)
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 1

    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Различаться профилям разрешено только по списку: "
              f"{', '.join(sorted(PROFILE_DIFFERENCES))}.", file=sys.stderr)
        return 1

    print(f"Профили сверены: {', '.join(profiles)}. Состав сервисов, образы и команды совпадают.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
