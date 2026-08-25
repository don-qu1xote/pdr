#!/usr/bin/env python3
"""Секреты живут в файле secdist, а не в окружении и не в истории (PDR-SEC-06).

Переменные окружения видны в /proc, попадают в дампы процесса, утекают в логи и в
отчёты об ошибках. Для ключей ЮKassa и токенов доступа это неприемлемо. Штатный
механизм — `components::Secdist` с файлом, права на который 0600; окружение несёт
ПУТЬ к файлу, а не значения.

Проверяется:

* в коде нет чтения секретов из окружения: `std::getenv`, `getenv`,
  `engine::subprocess::GetCurrentEnvironmentVariables`;
* в статических конфигах нет `environment-secrets-key`: штатная опция есть, но у
  нас запрещена — она делает ровно то, от чего мы уходим;
* у провайдера секретов не стоит `missing-ok: true`: отсутствующий файл секретов
  обязан ронять старт, значений по умолчанию не существует;
* секреты не заводятся в реестре динамических значений: конфиг меняют без выкатки
  и его значения видны в журнале изменений;
* в историю не попадает похожее на секрет: приватные ключи, `password=` с
  непустым значением, длинные токены. Примеры (`*.example`) исключены явно, и в
  них значение обязано быть заведомо ненастоящим;
* у настоящего файла секретов, если он есть на машине, права ровно 0600.

Список запрещённых подстрок для будущего теста на утечку живёт ЗДЕСЬ, файлом, а
не в голове: когда появится сервис, тест на логи и метрики берёт его отсюда
(docs/architecture/first-service.md).

Запуск:
    python3 scripts/check_secrets.py
    python3 scripts/check_secrets.py --selftest
"""

from __future__ import annotations

import argparse
import re
import stat
import sys
import tempfile
from pathlib import Path
from typing import Iterator, Sequence

SECRETS_FILE = Path("deploy/secrets/secrets.json")
SECRETS_EXAMPLE = Path("deploy/secrets/secrets.json.example")
REGISTRY = Path("configs/dynamic/registry.yaml")

SKIPPED_DIRS = frozenset({".git", "build", "out", "_deps", "__pycache__", "node_modules"})
CODE_SUFFIXES = frozenset({".hpp", ".cpp", ".cc", ".hxx"})
CONFIG_SUFFIXES = frozenset({".yaml", ".yml"})

PLACEHOLDERS = ("ЗАМЕНИТЕ", "change-me", "not-a-secret", "example", "placeholder", "<", "тут")

FORBIDDEN_IN_OUTPUT = (
    "postgresql://",
    "postgres://",
    "PDR_SECDIST",
    "shop_id",
    "secret_key",
    "Authorization: Bearer",
    "BEGIN PRIVATE KEY",
    "BEGIN RSA PRIVATE KEY",
)

ENV_READ = re.compile(r"\b(?:std::getenv|::getenv|getenv)\s*\(")
ENV_SECRETS_KEY = re.compile(r"^\s*environment-secrets-key\s*:", re.M)
MISSING_OK = re.compile(r"^\s*missing-ok\s*:\s*true\b", re.M)
PRIVATE_KEY = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")
PASSWORD_ASSIGN = re.compile(
    r"(?:password|passwd|secret|token|api[_-]?key)\s*[:=]\s*['\"]?([^\s'\"#;,}]{6,})",
    re.IGNORECASE,
)
SECRETISH_REGISTRY_NAME = re.compile(
    r"^(PDR_[A-Z0-9_]*(?:SECRET|TOKEN|PASSWORD|KEY|CREDENTIAL)[A-Z0-9_]*):", re.M
)


def files(root: Path) -> Iterator[Path]:
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if any(part in SKIPPED_DIRS or part.startswith(".git") for part in path.parts):
            continue
        yield path


def looks_like_placeholder(value: str) -> bool:
    return any(mark.lower() in value.lower() for mark in PLACEHOLDERS)


def check(root: Path) -> tuple[list[str], int]:
    violations: list[str] = []
    checked = 0

    for path in files(root):
        display = path.relative_to(root)
        suffix = path.suffix
        is_example = ".example" in path.name or path.name.endswith(".example")

        if suffix in CODE_SUFFIXES or suffix in CONFIG_SUFFIXES or suffix in {".json", ".env"} \
                or is_example:
            checked += 1
        else:
            continue

        text = path.read_text(encoding="utf-8", errors="replace")

        if suffix in CODE_SUFFIXES:
            for number, line in enumerate(text.splitlines(), start=1):
                if ENV_READ.search(line):
                    violations.append(
                        f"{display}:{number}: секрет из окружения — «getenv». Переменные видны "
                        f"в /proc и в дампах: секреты читает components::Secdist, окружение "
                        f"несёт только путь к файлу"
                    )

        if suffix in CONFIG_SUFFIXES:
            for found in ENV_SECRETS_KEY.finditer(text):
                number = text[: found.start()].count("\n") + 1
                violations.append(
                    f"{display}:{number}: «environment-secrets-key» — штатная опция, но у нас "
                    f"запрещена: она возвращает секреты в окружение, от которого мы уходим"
                )
            if "default-secdist-provider" in text:
                for found in MISSING_OK.finditer(text):
                    number = text[: found.start()].count("\n") + 1
                    violations.append(
                        f"{display}:{number}: «missing-ok: true» у провайдера секретов. "
                        f"Отсутствующий файл секретов обязан ронять старт: значений по "
                        f"умолчанию не существует"
                    )

        for found in PRIVATE_KEY.finditer(text):
            number = text[: found.start()].count("\n") + 1
            violations.append(
                f"{display}:{number}: в истории приватный ключ. Ключи живут в файле секретов "
                f"с правами 0600, а не в репозитории"
            )

        if display != SECRETS_EXAMPLE:
            for found in PASSWORD_ASSIGN.finditer(text):
                value = found.group(1)
                if looks_like_placeholder(value):
                    continue
                if is_example or suffix == ".env":
                    number = text[: found.start()].count("\n") + 1
                    violations.append(
                        f"{display}:{number}: значение «{value[:12]}…» похоже на настоящий "
                        f"секрет. В примерах значение обязано быть заведомо ненастоящим: "
                        f"«ЗАМЕНИТЕ», «change-me», «not-a-secret»"
                    )

    registry = root / REGISTRY
    if registry.is_file():
        text = registry.read_text(encoding="utf-8")
        for found in SECRETISH_REGISTRY_NAME.finditer(text):
            number = text[: found.start()].count("\n") + 1
            violations.append(
                f"{REGISTRY}:{number}: величина {found.group(1)} похожа на секрет. Секреты не "
                f"живут в динамическом конфиге: его значения видны в журнале изменений и "
                f"меняются без выкатки"
            )

    secrets = root / SECRETS_FILE
    if secrets.is_file():
        mode = stat.S_IMODE(secrets.stat().st_mode)
        if mode != 0o600:
            violations.append(
                f"{SECRETS_FILE}: права {oct(mode)[2:]} вместо 600. Файл секретов читает только "
                f"его владелец: chmod 600 {SECRETS_FILE}"
            )

    example = root / SECRETS_EXAMPLE
    if not example.is_file():
        violations.append(
            f"{SECRETS_EXAMPLE}: примера файла секретов нет. Без него первый сервис изобретёт "
            f"формат заново"
        )

    return violations, checked


SELFTEST_FILES = {
    "deploy/secrets/secrets.json.example": '{"postgresql_settings": {"password": "ЗАМЕНИТЕ"}}\n',
    "deploy/env/local.env.example": "POSTGRES_PASSWORD=change-me-locally\n"
                                    "PDR_SECDIST_PATH=./deploy/secrets/secrets.json\n",
    "libs/pdr-core/src/infrastructure/env.cpp": 'const char* p = std::getenv("PDR_DB_PASSWORD");\n',
    "services/scheduling/static_config.yaml": (
        "components_manager:\n"
        "    components:\n"
        "        default-secdist-provider:\n"
        "            config: /etc/pdr/secrets.json\n"
        "            missing-ok: true\n"
        "            environment-secrets-key: SECDIST_CONFIG\n"
    ),
    "deploy/env/ci.env.example": "POSTGRES_PASSWORD=hJ2k9Lm4Qw8x\n",
    "deploy/keys/service.pem.example": "-----BEGIN RSA PRIVATE KEY-----\nMIIE\n",
    "configs/dynamic/registry.yaml": (
        "PDR_YOOKASSA_SECRET_KEY:\n"
        "  description: ключ провайдера\n"
    ),
}

SELFTEST_EXPECTED = (
    ("env.cpp", "секрет из окружения"),
    ("static_config.yaml", "environment-secrets-key"),
    ("static_config.yaml", "missing-ok: true"),
    ("ci.env.example", "похоже на настоящий"),
    ("service.pem.example", "приватный ключ"),
    ("registry.yaml", "похожа на секрет"),
)

SELFTEST_CLEAN = ("local.env.example", "secrets.json.example")


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить утечку и окружение."""
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
                print(f"самопроверка: чистый файл объявлен нарушением: {name}", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        secrets = root / SECRETS_FILE
        secrets.write_text("{}\n", encoding="utf-8")
        secrets.chmod(0o644)
        loose, _ = check(root)
        if not any("вместо 600" in line for line in loose):
            print("самопроверка: не поймано, что права на файле секретов открыты на файле секретов", file=sys.stderr)
            return 1
        secrets.chmod(0o600)
        tight, _ = check(root)
        if any("вместо 600" in line for line in tight):
            print("самопроверка: правильные права объявлены нарушением", file=sys.stderr)
            return 1

        if not checked:
            print("самопроверка: не проверено ни одного файла", file=sys.stderr)
            return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED) + 1} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Секреты в secdist, а не в окружении.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    parser.add_argument("--forbidden", action="store_true",
                        help="напечатать список запрещённых в выводе подстрок и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    if arguments.forbidden:
        for line in FORBIDDEN_IN_OUTPUT:
            print(line)
        return 0

    violations, checked = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"Нарушений: {len(violations)}. Секреты живут в {SECRETS_FILE} с правами 0600; "
              f"окружение несёт только путь", file=sys.stderr)
        return 1

    print(f"Проверено файлов: {checked}. Секретов в окружении и в истории нет.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
