#!/usr/bin/env python3
"""Форма HTTP-слоя задана один раз, и разойтись ей нечем.

Хендлеров в дереве пока нет — есть базовый класс, который задаёт им форму
(PDR-API-01). Проверка стережёт именно её: всё, что здесь ловится, ловится в
тот день, когда первый хендлер решит «а у меня случай особенный». Через полгода
таких случаев пять, и клиент разбирает пять форматов отказа вместо одного.

Ловятся пять способов развести форму:

* решить статус на месте. Числа четырёхсотых и пятисотых, `HttpStatus::` и
  `SetStatus` разрешены ровно одному файлу — `error_mapping.cpp`, где живёт
  таблица «род отказа → ответ», и базовому хендлеру, который эту таблицу
  применяет. Хендлер, назначающий статус сам, — это «слот занят», приходящий то
  400, то 409, то 500;
* завести свой формат отказа. Тело отказа собирает только `problem.cpp`; имена
  `error`, `message`, `errors` и `error_code` в теле — признак второго формата,
  который придётся разбирать всем и чинить никому;
* потерять член формы. Все шесть членов RFC 9457 обязаны быть и в структуре, и
  в сборке тела: `request_id`, выпавший из ответа, — это жалоба «у меня не
  работает», которую не по чему найти;
* потерять заголовок безопасности. Все четыре названы в списке, и список
  ставится один раз до всякой развилки;
* сделать след запроса глобальным. Изменяемая переменная уровня файла или
  `thread_local` под именем со словом request/trace — это ровно тот способ,
  которым след протекает между запросами; задача называет его прямо;
* завести ВТОРУЮ ДВЕРЬ. `DoorHandler` — форма для запроса, у которого сессии ещё
  нет: она не спрашивает политику. Наследник у неё ровно один на систему — вход.
  Второй означает ручку без прав, о которой через полгода никто не помнит;
* завести ВТОРОЙ МАРШРУТ-НАСЛЕДНИК. Наследник `HttpHandlerBase`, который зовёт
  форму, в процессе ровно один: маршрутов сколько угодно, а места, где решают,
  что делать с запросом, — одно. Два таких наследника расходятся в первый же
  месяц: один спросит политику до открытия области арендатора, другой после;
* РАЗОБРАТЬ ТЕЛО РУКАМИ. Типы тел и ответов порождены из docs/api/openapi.yaml
  (PDR-API-04), и ручка получает их готовыми. `formats::json::Value` и
  `.As<...>` в файле ручки означают, что кто-то читает тело мимо схемы: поле,
  которого в схеме нет, так появляется в коде, и спецификация начинает врать.

  Ловится по ФАЙЛАМ РУЧЕК, а не по всему дереву: JSON вокруг ручек живёт
  законно — динамические настройки, секреты, продуктовые события, перевод
  спецификации из YAML. Схемы у них нет, и порождать типы неоткуда.

  Разбор чужого API — тоже законный случай, и он тоже не здесь: у внешнего
  сервиса своей спецификации в дереве нет. Адаптером он и остаётся, а ручка —
  это то, что отвечает НАШЕМУ клиенту.

  Единственное послабление — статический конфиг компонента: `config["имя"]` это
  не тело запроса, а настройка, которую задаёт не клиент, а тот, кто ставит
  сервис.

  Проверки (`tests/`) не считаются вовсе: тест читает то, что ручка ОТДАЛА, и
  другого способа убедиться у него нет.

Запуск:
    python3 scripts/check_http_form.py
    python3 scripts/check_http_form.py --selftest
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Sequence

HTTP = Path("libs/pdr-http/src/infrastructure/http")
PROBLEM = HTTP / "problem.hpp"
PROBLEM_BODY = HTTP / "problem.cpp"
MAPPING = HTTP / "error_mapping.cpp"
HANDLER = HTTP / "authorized_handler.hpp"
HEADERS = HTTP / "security_headers.hpp"
REQUEST_ID = HTTP / "request_id.hpp"

SOURCES = ("*.hpp", "*.cpp")

SERVICES = Path("services")
TESTS_DIR = "tests"
"""Каталог проверок. Разбор ответа руками здесь — не нарушение, а сама проверка:
тест читает то, что ручка ОТДАЛА, и другого способа убедиться у него нет."""
SKIPPED_DIRS = frozenset({".git", "out", "node_modules", "_deps", "__pycache__"})
SKIPPED_PREFIXES = ("build", "venv", ".venv")

DOOR_HEIR = re.compile(r"public\s+(?:\w+::)*DoorHandler\s*<")
CALLS_THE_FORM = re.compile(r"\.Serve\s*\(\s*request")

HANDLER_HEIR = re.compile(
    r"public\s+(?:\w+::)*"
    r"(?:HttpHandlerBase|ServedHandler|AuthorizedHandler|DoorHandler)\b"
)

HANDMADE_JSON = re.compile(r"\bformats::json::")

HANDMADE_READ = re.compile(r"\.\s*As\s*<")
"""Чтение значения по типу: `.As<T>()`, чем бы слева ни было.

Образцом ловится только само чтение. Кто его хозяин — образцом не выяснить:
слева бывает и `body["поле"]`, и `request.GetArg("x")`, и цепочка подлиннее.
Хозяина ищет `receiver_root` разбором назад, иначе послабление для конфига
превращается в дырку размером с любой вызов.
"""

CONFIGURED = frozenset({"config"})
"""Кому чтение по типу разрешено даже в ручке.

Статический конфиг компонента — не тело запроса: его задаёт тот, кто ставит
сервис, а не клиент, схемы у него своя (`GetStaticConfigSchema`), и порождать
типы неоткуда.
"""

CLOSERS = {")": "(", "]": "["}

MEMBERS = ("type", "title", "status", "detail", "instance", "request_id")

SECURITY = (
    "Content-Security-Policy",
    "Referrer-Policy",
    "Permissions-Policy",
    "X-Content-Type-Options",
)

STATUS_NUMBER = re.compile(r"\b[45]\d{2}\b")
STATUS_CALL = re.compile(r"HttpStatus::|StatusCode::|SetStatus|SetResponseStatus")
SECOND_FORMAT = re.compile(r'"(error|errors|message|error_code|errorMessage)"')
GLOBAL_TRACE = re.compile(
    r"^(?!\s)(?:static\s+|inline\s+)?(?:thread_local\s+)?(?:std::)?string\s+"
    r"\w*(?:request|trace|correlation)\w*\s*(?:=|;)",
    re.I | re.M,
)
THREAD_LOCAL = re.compile(r"\bthread_local\b")

LITERAL_OR_COMMENT = re.compile(
    r"\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/", re.S
)


def read(root: Path, path: Path) -> str:
    target = root / path
    return target.read_text(encoding="utf-8") if target.is_file() else ""


def code_of(text: str) -> str:
    """Текст без комментариев: формат задаёт код, а не рассказ о нём.

    Иначе doc-комментарий «свой формат отказа не заводится» считался бы своим
    форматом отказа, и проверка требовала бы молчать о том, чего не делаешь.
    """
    return LITERAL_OR_COMMENT.sub(
        lambda found: found.group(0) if found.group(0)[0] in "\"'" else " ", text
    )


def sources_of(root: Path) -> list[Path]:
    found: list[Path] = []
    for pattern in SOURCES:
        found.extend(path.relative_to(root) for path in sorted((root / HTTP).rglob(pattern)))
    return sorted(set(found))


def check_one_mapping(root: Path) -> list[str]:
    """Статус решается в одном файле, а применяется в одном месте."""
    violations = []
    for path in sources_of(root):
        if path == MAPPING:
            continue

        text = code_of(read(root, path))
        if path != HANDLER:
            for number in sorted(set(STATUS_NUMBER.findall(text))):
                violations.append(
                    f"{path}: статус {number} назван мимо таблицы. Ответ выбирает "
                    f"{MAPPING.name}, и он один: иначе один и тот же отказ приходит то 400, "
                    f"то 409, то 500"
                )
        found = STATUS_CALL.search(text)
        if found is not None and path != HANDLER:
            violations.append(
                f"{path}: «{found.group(0)}» вне базового хендлера. Статус проставляется один "
                f"раз, до всякой развилки в обработке"
            )
    return violations


def check_one_format(root: Path) -> list[str]:
    """Тело отказа собирает один файл, и все члены формы на месте."""
    violations = []
    body = read(root, PROBLEM_BODY)
    if not body:
        return [f"{PROBLEM_BODY}: сборки тела отказа нет вовсе"]

    shape = read(root, PROBLEM)
    for member in MEMBERS:
        if member not in shape:
            violations.append(
                f"{PROBLEM}: из формы отказа пропал член «{member}». Форма одна на всю "
                f"систему, и убавлять её нельзя молча"
            )
        if f'"{member}"' not in body:
            violations.append(
                f"{PROBLEM_BODY}: член «{member}» объявлен, а в тело не попадает. "
                f"Объявленный и не отданный — хуже отсутствующего: на него рассчитывают"
            )

    for path in sources_of(root):
        if path == PROBLEM_BODY:
            continue
        found = SECOND_FORMAT.search(code_of(read(root, path)))
        if found is not None:
            violations.append(
                f"{path}: тело отказа собирают с {found.group(0)} — это второй формат. "
                f"Отказ описан RFC 9457, и разбирать клиенту его один"
            )
    return violations


def check_security(root: Path) -> list[str]:
    """Все четыре заголовка безопасности названы, и ставятся они списком."""
    text = read(root, HEADERS)
    if not text:
        return [f"{HEADERS}: списка заголовков безопасности нет вовсе"]

    violations = []
    for name in SECURITY:
        if name not in text:
            violations.append(
                f"{HEADERS}: нет заголовка «{name}». Ответ без него — это ровно тот ответ, "
                f"который покажут чужому"
            )
    if "ApplySecurityHeaders" not in code_of(read(root, HANDLER)):
        violations.append(
            f"{HANDLER}: заголовки безопасности не ставятся. «На всех ответах» — не оборот "
            f"речи: страница отказа тоже ответ"
        )
    return violations


def check_request_id(root: Path) -> list[str]:
    """След запроса не бывает глобальным и не бывает чужим."""
    violations = []
    if not read(root, REQUEST_ID):
        return [f"{REQUEST_ID}: следа запроса нет вовсе"]

    for path in sources_of(root):
        text = code_of(read(root, path))
        if GLOBAL_TRACE.search(text) or THREAD_LOCAL.search(text):
            violations.append(
                f"{path}: след запроса лежит глобально. Он протечёт между запросами, и "
                f"жалоба одного человека приведёт к чужому запросу — протаскивается он "
                f"параметром"
            )

    if "IsUsableRequestId" not in code_of(read(root, REQUEST_ID)):
        violations.append(
            f"{REQUEST_ID}: принесённый клиентом след берут как есть. Он уходит в журнал и в "
            f"заголовок ответа: перевод строки в нём пишет клиент, а читаем мы"
        )
    return violations


def tree_sources(root: Path) -> list[Path]:
    """Исходники всего дерева, кроме каталогов сборки и окружений."""
    found: list[Path] = []
    for pattern in SOURCES:
        for path in sorted(root.rglob(pattern)):
            parts = path.relative_to(root).parts
            if any(part in SKIPPED_DIRS or part.startswith(".") for part in parts):
                continue
            if any(part.startswith(SKIPPED_PREFIXES) for part in parts):
                continue
            found.append(path)
    return found


def check_one_door(root: Path) -> list[str]:
    """Дверь одна: наследник формы, которая не спрашивает политику."""
    heirs = [
        path.relative_to(root)
        for path in tree_sources(root)
        if path.relative_to(root) != HANDLER and DOOR_HEIR.search(code_of(path.read_text(
            encoding="utf-8", errors="replace"
        )))
    ]
    if len(heirs) <= 1:
        return []

    names = ", ".join(str(path) for path in heirs)
    return [
        f"дверей стало {len(heirs)}: {names}. `DoorHandler` не спрашивает политику, и "
        f"наследник у неё ровно один — вход. Второй — это ручка без прав"
    ]


def check_one_route(root: Path) -> list[str]:
    """Форму в процессе зовут из одного места.

    Считаются файлы, а не классы: объявление наследника и его тело лежат в
    разных файлах, и по одному файлу «наследник, зовущий форму» не собрать.
    Место вызова при этом ровно одно и там же, где наследник, — по нему и
    считаем.
    """
    violations: list[str] = []
    services = root / SERVICES
    if not services.is_dir():
        return violations

    for service in sorted(entry for entry in services.iterdir() if entry.is_dir()):
        callers = [
            path.relative_to(root)
            for path in tree_sources(service)
            if CALLS_THE_FORM.search(code_of(path.read_text(encoding="utf-8", errors="replace")))
        ]
        if len(callers) > 1:
            names = ", ".join(str(path) for path in callers)
            violations.append(
                f"{service.relative_to(root)}: форму зовут из {len(callers)} мест: {names}. "
                f"Маршрутов сколько угодно, а место, где решают, что делать с запросом, — одно: "
                f"второе разойдётся с первым в первый же месяц"
            )
    return violations


def receiver_root(code: str, dot: int) -> str | None:
    """Имя, с которого начинается выражение слева от точки.

    Идём назад от точки, перешагивая уже закрытые скобки целиком: `body["поле"]`
    даёт `body`, `request.GetArg("x")` — `request`, `call.request.GetArg("x")` —
    `call`. Ничего не разобрав, возвращаем None — и тогда чтение считается
    разбором руками: неизвестное послаблением не бывает.
    """
    index = dot - 1
    while True:
        while index >= 0 and code[index].isspace():
            index -= 1
        if index < 0:
            return None

        if code[index] in CLOSERS:
            opener, closer = CLOSERS[code[index]], code[index]
            depth = 0
            while index >= 0:
                if code[index] == closer:
                    depth += 1
                elif code[index] == opener:
                    depth -= 1
                    if depth == 0:
                        break
                index -= 1
            if index < 0:
                return None
            index -= 1
            continue

        if not (code[index].isalnum() or code[index] == "_"):
            return None

        end = index + 1
        while index >= 0 and (code[index].isalnum() or code[index] == "_"):
            index -= 1
        name = code[index + 1 : end]

        while index >= 0 and code[index].isspace():
            index -= 1
        if index >= 1 and code[index] == ":" and code[index - 1] == ":":
            index -= 2
            continue
        if index >= 0 and code[index] == ".":
            index -= 1
            continue
        if index >= 1 and code[index] == ">" and code[index - 1] == "-":
            index -= 2
            continue
        return name


def handler_files(root: Path) -> list[Path]:
    """Файлы ручек: где объявлен наследник формы — и его сосед по имени.

    Пара нужна потому, что объявление и тело лежат врозь: разбор руками пишут в
    `.cpp`, а наследника видно в `.hpp`. Проверять только тот файл, где нашёлся
    наследник, значило бы не проверять как раз то место, где разбор и заводят.
    """
    stems: set[Path] = set()
    for path in tree_sources(root):
        relative = path.relative_to(root)
        if relative == HANDLER or TESTS_DIR in relative.parts:
            continue
        if HANDLER_HEIR.search(code_of(path.read_text(encoding="utf-8", errors="replace"))):
            stems.add(path.with_suffix(""))

    found = [
        neighbour
        for stem in sorted(stems)
        for suffix in (".hpp", ".cpp")
        if (neighbour := stem.with_suffix(suffix)).is_file()
    ]
    return sorted(found)


def check_generated_bodies(root: Path) -> tuple[list[str], int]:
    """Ручка получает тело ПОРОЖДЁННЫМ ТИПОМ, а не разбирает его сама."""
    violations: list[str] = []
    found = handler_files(root)

    for path in found:
        display = path.relative_to(root)
        code = code_of(path.read_text(encoding="utf-8", errors="replace"))

        if HANDMADE_JSON.search(code):
            violations.append(
                f"{display}: ручка трогает formats::json. Тело и ответ порождаются из "
                f"docs/api/openapi.yaml и приходят готовыми: поле, которого нет в схеме, "
                f"в ручке взяться не должно"
            )

        for read in HANDMADE_READ.finditer(code):
            owner = receiver_root(code, read.start())
            if owner in CONFIGURED:
                continue
            named = owner if owner else "неизвестно что"
            violations.append(
                f"{display}: разбор руками — `{named}...As<...>`. Тело запроса разбирает "
                f"форма, порождённым разборщиком; вручную читают только то, чему схемы "
                f"нет, и не в ручке"
            )

    return violations, len(found)


def check(root: Path) -> tuple[list[str], int]:
    if not (root / HTTP).is_dir():
        return ([f"{HTTP}: слоя HTTP нет вовсе — проверять нечего"], 0)

    violations = check_one_mapping(root)
    violations.extend(check_one_format(root))
    violations.extend(check_security(root))
    violations.extend(check_request_id(root))
    violations.extend(check_one_door(root))
    violations.extend(check_one_route(root))
    handmade, counted = check_generated_bodies(root)
    violations.extend(handmade)
    return violations, counted


SELFTEST_TREE = {
    MAPPING: "int StatusOf(core::ErrorKind kind) { return 422; }\n",
    PROBLEM: "struct Problem { std::string type, title, detail, instance, request_id; "
             "int status; };\n",
    PROBLEM_BODY: 'body["type"] = problem.type;\nbody["title"] = problem.title;\n'
                  'body["status"] = problem.status;\nbody["detail"] = problem.detail;\n'
                  'body["instance"] = problem.instance;\n',
    HANDLER: "ApplySecurityHeaders(response);\nresponse.SetStatus(problem.status);\n",
    HEADERS: 'SecurityHeader{"Content-Security-Policy", "default-src \'none\'"},\n'
             'SecurityHeader{"Referrer-Policy", "no-referrer"},\n'
             'SecurityHeader{"X-Content-Type-Options", "nosniff"},\n',
    REQUEST_ID: "thread_local std::string current_request_id;\n",
    HTTP / "book_lesson_handler.cpp": 'response.SetStatus(409);\nbody["error"] = "занято";\n',
    HTTP / "cancel_lesson_handler.cpp": '/// Своего формата тут нет: ни "error", ни "message".\n',
    Path("libs/pdr-identity/src/identity/infrastructure/http/sign_in.hpp"):
        "class SignInDoor final : public infrastructure::http::DoorHandler<Request, Session> {};\n",
    Path("libs/pdr-identity/src/identity/infrastructure/http/sign_in.cpp"):
        "auto minutes = call.body[\"minutes\"].As<int>();\n",
    Path("services/main/src/health.cpp"):
        "userver::formats::json::ValueBuilder made;\n",
    Path("services/main/src/openapi.hpp"):
        "class Doc final : public server::handlers::HttpHandlerBase {};\n",
    Path("services/main/src/openapi.cpp"):
        'auto path = config["document"].As<std::string>();\n',
    Path("libs/pdr-http/tests/sign_in_test.cpp"):
        "class Twin final : public infrastructure::http::AuthorizedHandler<R, S, B, A> {};\n"
        'EXPECT_EQ(body["type"].As<std::string>(), "urn:pdr:error:sign_in_refused");\n',
    Path("libs/pdr-billing/src/billing/infrastructure/http/pay.hpp"):
        "class PayDoor final : public infrastructure::http::DoorHandler<Request, Session> {};\n",
    Path("services/main/src/route.cpp"):
        "std::string Route::Handle() const { return form_.Serve(request, done); }\n",
    Path("services/main/src/second_route.cpp"):
        "std::string Another::Handle() const { return form_.Serve(request, done); }\n",
    Path("services/main/src/health.hpp"):
        "class Health final : public server::handlers::HttpHandlerBase {};\n",
}

"""Дерево одной самопроверки.

Разбор руками подсажен дважды и по-разному: `sign_in.cpp` читает тело
`.As<int>()` там, где наследника не видно — его выдаёт сосед по имени, —
а `health.cpp` собирает ответ через `formats::json`.

Молчать при этом обязаны два законных случая: `openapi.cpp` читает свой
статический конфиг (`config["document"]`), а `sign_in_test.cpp` разбирает
ОТВЕТ, потому что он проверка и другого способа убедиться у него нет.
"""

SELFTEST_EXPECTED = (
    "статус 409 назван мимо таблицы",
    "вне базового хендлера",
    "второй формат",
    "в тело не попадает",
    "нет заголовка «Permissions-Policy»",
    "лежит глобально",
    "берут как есть",
    "разбор руками",
    "трогает formats::json",
    "дверей стало 2",
    "форму зовут из 2 мест",
)


def selftest() -> int:
    """Отрицательные случаи: каждый способ развести форму обязан ловиться.

    Файлов ручек в этом дереве семь: дверь входа парой, дверь оплаты (только
    `.hpp`), состояние парой, спецификация парой. Число сверяется затем, что
    проверка, нашедшая ноль ручек, зеленела бы и на дереве, где ручек нет вовсе.
    """
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path, content in SELFTEST_TREE.items():
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")

        violations, handlers = check(root)
        for fragment in SELFTEST_EXPECTED:
            if not any(fragment in line for line in violations):
                print(f"самопроверка: не поймано «{fragment}»", file=sys.stderr)
                for line in violations:
                    print("    " + line, file=sys.stderr)
                return 1

        if handlers != 7:
            print(f"самопроверка: найдено {handlers} файлов ручек вместо семи", file=sys.stderr)
            return 1

        if any("sign_in_test.cpp" in line for line in violations):
            print("самопроверка: разбор ответа в проверке объявлен нарушением", file=sys.stderr)
            return 1
        if any("openapi.cpp" in line for line in violations):
            print("самопроверка: статический конфиг объявлен разбором тела", file=sys.stderr)
            return 1

        (root / HTTP / "book_lesson_handler.cpp").unlink()
        (root / "libs/pdr-billing/src/billing/infrastructure/http/pay.hpp").unlink()
        (root / "services/main/src/second_route.cpp").unlink()
        (root / "libs/pdr-identity/src/identity/infrastructure/http/sign_in.cpp").unlink()
        (root / "services/main/src/health.cpp").unlink()
        (root / REQUEST_ID).write_text(
            "bool IsUsableRequestId(std::string_view) noexcept;\n", encoding="utf-8"
        )
        clean, _ = check(root)
        if any("cancel_lesson_handler" in line for line in clean):
            print("самопроверка: комментарий о запрете объявлен нарушением", file=sys.stderr)
            return 1
        if any("health.hpp" in line for line in clean):
            print("самопроверка: ручка состояния объявлена вторым маршрутом", file=sys.stderr)
            return 1
        for fragment in ("назван мимо таблицы", "второй формат", "лежит глобально",
                         "берут как есть", "дверей стало", "форму зовут из",
                         "разбор руками", "трогает formats::json"):
            if any(fragment in line for line in clean):
                print(f"самопроверка: чистый файл объявлен нарушением «{fragment}»",
                      file=sys.stderr)
                return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, и ни одного там, где их нет.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Форма HTTP-слоя задана один раз.")
    parser.add_argument("--root", type=Path, default=root, help="что проверять")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    violations, handlers = check(arguments.root)
    for line in violations:
        print(line, file=sys.stderr)

    if violations:
        print(f"\nНарушений: {len(violations)}. Форма — docs/architecture/http.md",
              file=sys.stderr)
        return 1

    print(f"Форма отказа одна: {len(MEMBERS)} членов, {len(SECURITY)} заголовков "
          f"безопасности, статус решается в {MAPPING.name}. Файлов ручек проверено: {handlers}.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
