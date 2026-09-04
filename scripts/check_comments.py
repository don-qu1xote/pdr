#!/usr/bin/env python3
"""Комментарии: только те, без которых не соберётся или не проверится сборка.

Пояснение в коде — признак того, что код непонятен. Чинить надо код, а не
дописывать к нему сноску: комментарий не проверяется компилятором, устаревает
молча и через год врёт с тем же уверенным видом, что и в день, когда его
написали.

Остаются четыре вида и никаких больше:

* прагмы и директивы инструментов — NOLINT, IWYU pragma, clang-format off,
  noqa, type: ignore, eslint-disable, shellcheck, syntax= в Dockerfile;
* doc-комментарии — Doxygen /// и /**, JSDoc, докстринги Python: это часть
  публичного контракта, а не пояснение к строчке;
* shebang и объявление кодировки;
* РУССКИЕ комментарии в построечных файлах — Makefile, CMake, Dockerfile,
  compose, shell, SQL, YAML-конфигурация, .env. Там комментарий помогает
  человеку собрать и поднять проект, и это единственное место, где пояснение
  оправдано.

В построечных файлах остаётся не всё: английский комментарий, закомментированный
код и декоративная линейка из решёток — нарушение и там.

Русский язык — потому что это язык проекта: смешивать два языка в одном файле
хуже, чем выбрать один.

Правило с примерами «было / стало» — docs/comments.md. Белых списков «этому
файлу можно» здесь нет и не будет: исключение, выданное одному файлу, через
полгода выдано десяти.

Три образца ниже стоит прочитать вместе с правилом. `NAMESPACE_CLOSER` — это
закрывающий комментарий пространства имён; его ставит сам clang-format
(`FixNamespaceComments` в .clang-format), и снять такой комментарий значит
уронить `make fmt-check`. `DECORATION` — строка целиком из знаков препинания:
рамка, разделитель, линейка из решёток. `CONFIG_LINE` и `CODE_KEYWORD` опознают
закомментированный код по форме, а не по смыслу, и только когда имя слева от
«:» или «=» записано латиницей: «Профиль: local» — фраза, «image: ${MAIN_IMAGE}»
— строка конфигурации.

Запуск:
    make comments           проверить (это же делает CI и хук pre-commit)
    make comments-fix       удалить нарушения
    python3 scripts/check_comments.py --selftest
"""

from __future__ import annotations

import argparse
import io
import re
import sys
import tempfile
import tokenize
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

CYRILLIC = re.compile(r"[А-Яа-яЁё]")
LATIN_WORD = re.compile(r"[A-Za-z]{2,}")

PRAGMAS = (
    "nolint",
    "iwyu pragma",
    "clang-format off",
    "clang-format on",
    "clang-tidy",
    "noqa",
    "type: ignore",
    "pragma: no cover",
    "pragma: no branch",
    "coverage:",
    "eslint-disable",
    "@ts-ignore",
    "@ts-expect-error",
    "prettier-ignore",
    "shellcheck",
    "syntax=",
    "штатное-ok:",
    "контур-ok:",
    "журнал-ok:",
    "userver-ok:",
)

CODING = re.compile(r"coding[:=]\s*[-\w.]+")

NAMESPACE_CLOSER = re.compile(r"^//\s*namespace(\s|$)")

DECORATION = re.compile(r"^[#\-=*/_~+\s]+$")

CONFIG_LINE = re.compile(r"^[A-Za-z_][\w.\-]*\s*[:=]\s*\S")
CODE_KEYWORD = re.compile(
    r"^(if|for|while|return|import|from|def|class|const|let|var|function|template|"
    r"using|struct|public|private|switch|case|#include|#define|select|insert|update|"
    r"delete|create|alter|drop|grant|revoke)\b",
    re.I,
)
CODE_TAIL = re.compile(r"[;{}]\s*$")

CODE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx",
                 ".py", ".js", ".jsx", ".mjs", ".ts", ".tsx"}
BUILD_SUFFIXES = {".mk", ".cmake", ".sh", ".bash", ".sql", ".yml", ".yaml",
                  ".env", ".conf", ".dockerfile", ".ini", ".toml"}
BUILD_NAMES = {"Makefile", "makefile", "GNUmakefile", "CMakeLists.txt", "Dockerfile",
               ".gitignore", ".dockerignore", ".editorconfig", ".clang-format",
               ".clang-tidy", ".gitattributes"}

SHELL_SHEBANG = re.compile(r"^#!.*\b(sh|bash|zsh|dash)\b")
PYTHON_SHEBANG = re.compile(r"^#!.*\bpython")

SKIP_DIRS = {".git", "node_modules", "__pycache__", "dist"}

SKIP_PREFIXES = ("build", "venv", ".venv")
"""Каталоги сборки и виртуальные окружения — по приставке, а не по точному имени.

build-userver и venv-utest — такие же каталоги сборки, как build и .venv, а
внутри у них чужой код, который нашего правила не читал.
"""


class CommentError(Exception):
    """Файл не разобран. Это отказ, а не повод его пропустить."""


@dataclass(frozen=True)
class Comment:
    line: int
    end_line: int
    column: int
    text: str
    inline: bool


@dataclass(frozen=True)
class Violation:
    path: str
    line: int
    end_line: int
    reason: str
    comments: tuple[Comment, ...]


def _content(text: str) -> str:
    """Комментарий без своего маркера."""
    stripped = text.strip()
    for marker in ("///", "//", "/**", "/*", "*/", "--", "#!", "#"):
        if stripped.startswith(marker):
            stripped = stripped[len(marker):]
            break
    return stripped.strip(" \t*")


def scan_hash(lines: Sequence[str]) -> list[Comment]:
    """Комментарии `#` вне кавычек: shell, YAML, CMake, Dockerfile, .env.

    Решётка начинает комментарий в начале строки или после пробела; внутри
    `${#name}` и `$#` она означает другое.
    """
    found: list[Comment] = []
    for number, raw in enumerate(lines, start=1):
        quote = ""
        for column, symbol in enumerate(raw):
            if quote:
                if symbol == quote:
                    quote = ""
                continue
            if symbol in "'\"":
                quote = symbol
                continue
            if symbol != "#":
                continue
            if column and raw[column - 1] not in " \t":
                continue
            before = raw[:column]
            found.append(Comment(number, number, column, raw[column:].rstrip("\n"),
                                 bool(before.strip())))
            break
    return found


def scan_python(text: str, source: str) -> list[Comment]:
    """Комментарии Python — токенизатором, а не регулярками.

    Решётка внутри строкового литерала комментарием не является, и разобрать это
    регулярным выражением нельзя: нужен разбор кавычек, тройных кавычек, f-строк
    и продолжений строки.
    """
    found: list[Comment] = []
    try:
        tokens = tokenize.generate_tokens(io.StringIO(text).readline)
        for token in tokens:
            if token.type != tokenize.COMMENT:
                continue
            row, column = token.start
            before = token.line[:column]
            found.append(Comment(row, row, column, token.string, bool(before.strip())))
    except (tokenize.TokenError, IndentationError, SyntaxError) as error:
        raise CommentError(f"{source}: не разобран как Python ({error})") from error
    return found


def scan_slash(text: str, lines: Sequence[str]) -> list[Comment]:
    """Комментарии // и /* */ мимо строковых и символьных литералов.

    Обратная кавычка — тоже литерал: в шаблонной строке JavaScript живут и
    адреса вида https://..., и звёздочки в путях, и ни то ни другое не является
    комментарием. Внутри самих комментариев обратная кавычка сюда не доходит:
    комментарий разобран раньше и целиком.
    """
    found: list[Comment] = []
    index = 0
    size = len(text)
    line = 1
    column = 0
    starts = [0]
    for position, symbol in enumerate(text):
        if symbol == "\n":
            starts.append(position + 1)

    def place(position: int) -> tuple[int, int]:
        row = 1
        for number, start in enumerate(starts, start=1):
            if start > position:
                break
            row = number
        return row, position - starts[row - 1]

    while index < size:
        symbol = text[index]
        following = text[index + 1] if index + 1 < size else ""

        if symbol in "\"'`":
            quote = symbol
            index += 1
            while index < size:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            continue

        if symbol == "/" and following == "/":
            end = text.find("\n", index)
            end = size if end < 0 else end
            line, column = place(index)
            before = lines[line - 1][:column] if line - 1 < len(lines) else ""
            found.append(Comment(line, line, column, text[index:end], bool(before.strip())))
            index = end
            continue

        if symbol == "/" and following == "*":
            end = text.find("*/", index + 2)
            end = size if end < 0 else end + 2
            line, column = place(index)
            last, _ = place(max(end - 1, index))
            before = lines[line - 1][:column] if line - 1 < len(lines) else ""
            found.append(Comment(line, last, column, text[index:end], bool(before.strip())))
            index = end
            continue

        index += 1

    return found


def scan_sql(text: str, lines: Sequence[str]) -> list[Comment]:
    """Комментарии -- и /* */ мимо литералов, включая долларовые кавычки."""
    found: list[Comment] = []
    index = 0
    size = len(text)
    dollar = re.compile(r"\$([A-Za-z_][A-Za-z0-9_]*)?\$")

    def place(position: int) -> tuple[int, int]:
        row = text.count("\n", 0, position) + 1
        start = text.rfind("\n", 0, position) + 1
        return row, position - start

    while index < size:
        symbol = text[index]
        following = text[index + 1] if index + 1 < size else ""

        if symbol == "'":
            index += 1
            while index < size:
                if text[index] == "'" and text[index + 1: index + 2] == "'":
                    index += 2
                    continue
                if text[index] == "'":
                    index += 1
                    break
                index += 1
            continue

        if symbol == "$":
            opening = dollar.match(text, index)
            if opening:
                closing = text.find(opening.group(0), opening.end())
                index = size if closing < 0 else closing + len(opening.group(0))
                continue

        if symbol == "-" and following == "-":
            end = text.find("\n", index)
            end = size if end < 0 else end
            row, column = place(index)
            before = lines[row - 1][:column] if row - 1 < len(lines) else ""
            found.append(Comment(row, row, column, text[index:end], bool(before.strip())))
            index = end
            continue

        if symbol == "/" and following == "*":
            end = text.find("*/", index + 2)
            end = size if end < 0 else end + 2
            row, column = place(index)
            last, _ = place(max(end - 1, index))
            before = lines[row - 1][:column] if row - 1 < len(lines) else ""
            found.append(Comment(row, last, column, text[index:end], bool(before.strip())))
            index = end
            continue

        index += 1

    return found


def kind_of(path: Path, first_line: str) -> tuple[str, str]:
    """(вид файла, семейство комментариев). Вид: code, build или пусто."""
    name = path.name
    suffix = path.suffix
    if suffix == ".example":
        name = path.stem
        suffix = Path(name).suffix

    if name in BUILD_NAMES or name.startswith("Dockerfile") or name.startswith(".env"):
        family = "sql" if suffix == ".sql" else "hash"
        return "build", family

    if suffix in CODE_SUFFIXES:
        return "code", "python" if suffix == ".py" else "slash"

    if suffix in BUILD_SUFFIXES:
        return "build", "sql" if suffix == ".sql" else "hash"

    if not suffix:
        if PYTHON_SHEBANG.match(first_line):
            return "code", "python"
        if SHELL_SHEBANG.match(first_line):
            return "build", "hash"

    return "", ""


def comments_of(path: Path, text: str, family: str) -> list[Comment]:
    lines = text.splitlines()
    if family == "python":
        return scan_python(text, str(path))
    if family == "slash":
        return scan_slash(text, lines)
    if family == "sql":
        return scan_sql(text, lines)
    return scan_hash(lines)


def blocks_of(comments: Sequence[Comment]) -> list[list[Comment]]:
    """Соседние строки комментария — один блок: язык определяется по блоку.

    Иначе продолжение русской фразы, в котором оказался только путь к файлу или
    пример команды, каждый раз выглядело бы английским.
    """
    grouped: list[list[Comment]] = []
    for comment in comments:
        if (grouped and not comment.inline and not grouped[-1][-1].inline
                and comment.line == grouped[-1][-1].end_line + 1
                and comment.column == grouped[-1][-1].column):
            grouped[-1].append(comment)
            continue
        grouped.append([comment])
    return grouped


def is_pragma(text: str) -> bool:
    lowered = text.lower()
    return any(word in lowered for word in PRAGMAS)


def is_doc(text: str) -> bool:
    stripped = text.strip()
    return stripped.startswith("///") or stripped.startswith("/**")


def _code_shape(text: str) -> bool:
    """Похоже ли содержимое комментария на строку кода, а не на фразу.

    Вложенный маркер отсекается: в «# main: <<: [*hardening] # фаза 1» пояснение
    относится к закомментированной строке, а не к тексту, и мешает её узнать.
    Кириллица в остатке означает, что это всё-таки фраза: русское предложение
    может начинаться со слова «alter» и кончаться точкой с запятой.
    """
    for marker in ("#", "//", "--"):
        position = text.find(marker)
        if position > 0:
            text = text[:position]
    text = text.strip()
    if not text or CYRILLIC.search(text):
        return False
    return bool(CONFIG_LINE.match(text) or CODE_KEYWORD.match(text) or CODE_TAIL.search(text))


def verdict(comment: Comment, russian: bool, kind: str) -> str:
    """Причина, по которой комментарий нарушает правило; пустая — не нарушает."""
    text = comment.text
    content = _content(text)

    if is_pragma(text):
        return ""
    if is_doc(text):
        return ""
    if comment.line == 1 and text.startswith("#!"):
        return ""
    if comment.line <= 2 and CODING.search(text):
        return ""
    if NAMESPACE_CLOSER.match(text.strip()):
        return ""

    if content and DECORATION.match(content):
        return ("декоративная линейка. Разделять код должны пустая строка и имя "
                "функции, а не рамка из знаков")

    if _code_shape(content):
        return ("закомментированный код. Он не собирается, не проверяется и устаревает "
                "молча; нужен — верните, не нужен — история хранит его лучше комментария")

    if kind == "code":
        return ("пояснение к коду. Оно не проверяется компилятором и через год врёт; "
                "непонятный код чинится, а не комментируется. Часть контракта "
                "оформляется doc-комментарием")

    if not russian and LATIN_WORD.search(content):
        return ("английский комментарий. Язык проекта — русский, и смешивать два "
                "языка в одном файле хуже, чем выбрать один")

    return ""


def check_file(path: Path, root: Path) -> list[Violation]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return []

    first = text.splitlines()[0] if text else ""
    kind, family = kind_of(path, first)
    if not kind:
        return []

    try:
        display = str(path.relative_to(root))
    except ValueError:
        display = str(path)

    violations: list[Violation] = []
    for block in blocks_of(comments_of(path, text, family)):
        russian = bool(CYRILLIC.search("\n".join(item.text for item in block)))
        run: list[Comment] = []
        reason = ""
        for comment in block:
            found = verdict(comment, russian, kind)
            if found and found == reason:
                run.append(comment)
                continue
            if run:
                violations.append(
                    Violation(display, run[0].line, run[-1].end_line, reason, tuple(run))
                )
            run = [comment] if found else []
            reason = found
        if run:
            violations.append(
                Violation(display, run[0].line, run[-1].end_line, reason, tuple(run))
            )
    return violations


def sources(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        parts = path.relative_to(root).parts
        if any(part in SKIP_DIRS for part in parts):
            continue
        if any(part.startswith(SKIP_PREFIXES) for part in parts):
            continue
        yield path


BARE_MARKER = re.compile(r"^\s*(#|//|--)\s*$")


def _drop_dangling(lines: Sequence[str]) -> list[str]:
    """Снять пустые строки комментария, оставшиеся в хвосте блока.

    Абзацный разделитель «#» посреди блока остаётся; тот же «#», за которым
    больше нет ни строчки комментария, — след удалённого куска.
    """
    kept: list[str] = []
    for index, line in enumerate(lines):
        if BARE_MARKER.match(line):
            following = next((item for item in lines[index + 1:] if item.strip()), "")
            if not BARE_MARKER.match(following) and not following.lstrip().startswith(
                    line.strip()):
                continue
        kept.append(line)
    return kept


def fix_file(path: Path, violations: Sequence[Violation]) -> int:
    """Убрать нарушения из файла. Возвращает число снятых блоков."""
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    drop: set[int] = set()

    for violation in violations:
        for comment in violation.comments:
            if comment.inline:
                index = comment.line - 1
                head = lines[index][:comment.column].rstrip()
                ending = "\n" if lines[index].endswith("\n") else ""
                if head:
                    lines[index] = head + ending
                else:
                    drop.add(index)
                continue
            for number in range(comment.line, comment.end_line + 1):
                drop.add(number - 1)

    kept = [line for index, line in enumerate(lines) if index not in drop]
    kept = _drop_dangling(kept)

    collapsed: list[str] = []
    empty = 0
    for line in kept:
        if line.strip():
            empty = 0
        else:
            empty += 1
            if empty > 2:
                continue
        collapsed.append(line)

    path.write_text("".join(collapsed), encoding="utf-8")
    return sum(len(violation.comments) for violation in violations)


SELFTEST_FILES = {
    "libs/pdr-core/src/core/money.hpp": (
        "#pragma once\n"
        "\n"
        "namespace pdr::core {\n"
        "\n"
        "/// Сумма в минорных единицах и код валюты.\n"
        "class Money final {\n"
        "public:\n"
        "    // Округление вниз, потому что так делает касса.\n"
        "    long long minor{0};  // NOLINT(misc-non-private-member-variables)\n"
        "    // int old_field = 0;\n"
        "};\n"
        "\n"
        "}  // namespace pdr::core\n"
    ),
    "scripts/tool.mjs": (
        "#!/usr/bin/env node\n"
        "/** Докстринг остаётся: часть контракта. */\n"
        "const address = `https://example.test/a/*b*/c`;\n"
        "export default address;\n"
    ),
    "libs/pdr-core/src/core/money.cpp": (
        "#include \"core/money.hpp\"\n"
        "\n"
        "// Rounds towards zero.\n"
        "namespace pdr::core {\n"
        "\n"
        "// ============================================================\n"
        "\n"
        "}  // namespace pdr::core\n"
    ),
    "scripts/tool.py": (
        "#!/usr/bin/env python3\n"
        '"""Докстринг остаётся: он часть контракта."""\n'
        "\n"
        "import re\n"
        "\n"
        "# Решётка ниже живёт в строке, а не в комментарии.\n"
        'MARK = re.compile(r"^#\\s*(.*)$")  # noqa: E501\n'
        "\n"
        "\n"
        "def run() -> str:\n"
        '    """Что делает — здесь, а не сноской."""\n'
        '    return "# не комментарий"\n'
    ),
    "Makefile": (
        "# Русский комментарий в построечном файле остаётся: он помогает собрать.\n"
        "test:\n"
        "\t@echo ok\n"
        "\n"
        "# Runs the tests.\n"
        "check:\n"
        "\t@echo ok\n"
    ),
    "db/migrations/V001__init.sql": (
        "-- Арендатор есть на каждой доменной таблице.\n"
        "create table identity_person (\n"
        "    tenant_id uuid not null\n"
        ");\n"
        "-- create index identity_person_by_name on identity_person (name);\n"
    ),
    "docs/README.md": "# Заголовок, а не комментарий\n",
}

SELFTEST_EXPECTED = {
    ("libs/pdr-core/src/core/money.hpp", 8, "пояснение к коду"),
    ("libs/pdr-core/src/core/money.hpp", 10, "закомментированный код"),
    ("libs/pdr-core/src/core/money.cpp", 3, "пояснение к коду"),
    ("libs/pdr-core/src/core/money.cpp", 6, "декоративная линейка"),
    ("scripts/tool.py", 6, "пояснение к коду"),
    ("Makefile", 5, "английский комментарий"),
    ("db/migrations/V001__init.sql", 5, "закомментированный код"),
}

SELFTEST_KEPT = (
    ("libs/pdr-core/src/core/money.hpp", "/// Сумма в минорных единицах"),
    ("libs/pdr-core/src/core/money.hpp", "NOLINT"),
    ("libs/pdr-core/src/core/money.hpp", "}  // namespace pdr::core"),
    ("scripts/tool.py", "#!/usr/bin/env python3"),
    ("scripts/tool.mjs", "Докстринг остаётся"),
    ("scripts/tool.mjs", "https://example.test"),
    ("scripts/tool.py", "Докстринг остаётся"),
    ("scripts/tool.py", "noqa"),
    ("scripts/tool.py", '"# не комментарий"'),
    ("scripts/tool.py", 'r"^#\\s*(.*)$"'),
    ("Makefile", "Русский комментарий в построечном файле остаётся"),
    ("db/migrations/V001__init.sql", "Арендатор есть на каждой доменной таблице"),
    ("docs/README.md", "# Заголовок, а не комментарий"),
)


def selftest() -> int:
    """Отрицательные случаи: проверка обязана ловить то, ради чего написана, и
    обязана НЕ трогать то, без чего сборка не соберётся."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, text in SELFTEST_FILES.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")

        violations = [item for path in sources(root) for item in check_file(path, root)]

        for name, line, fragment in SELFTEST_EXPECTED:
            if not any(item.path == name and item.line == line and fragment in item.reason
                       for item in violations):
                print(f"самопроверка: не поймано «{fragment}» в {name}:{line}", file=sys.stderr)
                for item in violations:
                    print(f"    {item.path}:{item.line}: {item.reason}", file=sys.stderr)
                return 1

        if len(violations) != len(SELFTEST_EXPECTED):
            print(f"самопроверка: нарушений {len(violations)}, ожидалось "
                  f"{len(SELFTEST_EXPECTED)}", file=sys.stderr)
            for item in violations:
                print(f"    {item.path}:{item.line}: {item.reason}", file=sys.stderr)
            return 1

        by_file: dict[str, list[Violation]] = {}
        for item in violations:
            by_file.setdefault(item.path, []).append(item)
        for name, items in by_file.items():
            fix_file(root / name, items)

        left = [item for path in sources(root) for item in check_file(path, root)]
        if left:
            print("самопроверка: после --fix остались нарушения", file=sys.stderr)
            for item in left:
                print(f"    {item.path}:{item.line}: {item.reason}", file=sys.stderr)
            return 1

        for name, fragment in SELFTEST_KEPT:
            if fragment not in (root / name).read_text(encoding="utf-8"):
                print(f"самопроверка: --fix убрал то, что обязан оставить: "
                      f"{name} — «{fragment}»", file=sys.stderr)
                return 1

    print(f"Самопроверка пройдена: {len(SELFTEST_EXPECTED)} нарушений найдено там, где они "
          f"есть, {len(SELFTEST_KEPT)} обязательных комментариев не тронуто.")
    return 0


def main(argv: Sequence[str]) -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Политика комментариев.")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--fix", action="store_true", help="удалить нарушения, а не показать")
    parser.add_argument("--selftest", action="store_true", help="проверить саму проверку и выйти")
    arguments = parser.parse_args(argv)

    if arguments.selftest:
        return selftest()

    try:
        violations = [item for path in sources(arguments.root)
                      for item in check_file(path, arguments.root)]
    except CommentError as error:
        print(str(error), file=sys.stderr)
        return 1

    if not violations:
        print("Комментарии проверены. Лишних нет.")
        return 0

    if arguments.fix:
        by_file: dict[str, list[Violation]] = {}
        for item in violations:
            by_file.setdefault(item.path, []).append(item)
        removed = 0
        for name, items in sorted(by_file.items()):
            removed += fix_file(arguments.root / name, items)
        print(f"Снято комментариев: {removed} в {len(by_file)} файлах. "
              f"Пересоберите формат: make fmt")
        return 0

    for item in violations:
        where = f"{item.path}:{item.line}"
        if item.end_line != item.line:
            where += f"-{item.end_line}"
        print(f"{where}: {item.reason}", file=sys.stderr)
        print(f"    {item.comments[0].text.strip()[:88]}", file=sys.stderr)

    print(f"\nНарушений: {len(violations)}. Убрать: make comments-fix. "
          f"Правило и примеры «было / стало» — docs/comments.md", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
