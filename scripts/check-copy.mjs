#!/usr/bin/env node
/**
 * Как продукт разговаривает: четыре правила речи по файлам локализации (PDR-UI-07).
 *
 * Правила и списки живут в docs/product/voice.md и читаются оттуда: пополнение —
 * строка в таблицу, без правки кода. Здесь только разбор и отчёт.
 *
 * Проверяется:
 *
 * - стоп-слова: «успешно», «упс», «просто», «к сожалению» и остальные из таблицы.
 *   У каждого в отчёте стоит замена — линтер учит, а не наказывает;
 * - отказ содержит альтернативу: в тексте раздела problem обязано быть действие
 *   в повелительном наклонении. «Это время занято» — половина сообщения;
 * - у каждого кода доменного отказа есть русский текст. Нет текста — красная
 *   сборка; англоязычного запасного не существует. Код, который человек не
 *   увидит, называется в таблице voice.md и там же объясняется;
 * - оценочных слов в разделе student нет: продукт показывает факты, оценивает
 *   репетитор;
 * - разделение имён: «ПДР» и «PDR» запрещены в локализации, «Поля» и «polya» —
 *   в путях, именах таблиц, имени ветки и латинских идентификаторах исходников.
 *
 * Проверяются файлы локализации, а не компоненты. Текст, зашитый в компонент
 * мимо локализации, — отдельная поломка, и ловить её должна проверка, которая
 * требует локализации: иначе одна проверка знала бы две вещи и обе делала
 * наполовину.
 *
 * Запуск:
 *     node scripts/check-copy.mjs
 *     node scripts/check-copy.mjs --selftest
 */

import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import process from "node:process";

const VOICE = "docs/product/voice.md";
const CLIENTS = "clients";
const LOCALE_DIR = "i18n";
const REFUSAL = "problem";
const STUDENT = "student";
const EVERYWHERE = "везде";

const ACTION = /[а-яё]+(?:йте|ите|ьте|тесь)(?![а-яё])/i;

const SOURCE_SUFFIXES = new Set([
  ".cpp", ".cc", ".cxx", ".hpp", ".hxx", ".h",
  ".py", ".mjs", ".js", ".jsx", ".ts", ".tsx",
  ".sql", ".cmake", ".yml", ".yaml",
]);
const SOURCE_NAMES = new Set(["CMakeLists.txt", "Makefile", "GNUmakefile"]);
const SKIP_DIRS = new Set([".git", "build", "node_modules", "__pycache__", "dist", "out", "_deps"]);

const IDENTIFIER = /[A-Za-z_][A-Za-z0-9_]*/g;

const HASH_COMMENT = new Set([".py", ".yml", ".yaml", ".cmake"]);
const DASH_COMMENT = new Set([".sql"]);
const QUOTES = new Set(['"', "'", "`"]);

const ERROR_SHAPES = [
  /core::Error\s*\{\s*core::ErrorKind::k\w+\s*,\s*"([a-z][a-z0-9_]*)"/g,
  /\bRefuse\s*\(\s*"([a-z][a-z0-9_]*)"/g,
];
const ERROR_SITE = /core::Error\s*\{/g;

/** Разбор не удался. Это отказ, а не предупреждение. */
class VoiceError extends Error {}

function escapeForRegExp(text) {
  return text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

/** Выражение из таблицы: звёздочка в конце означает «и любое продолжение». */
function phraseRegExp(entry) {
  const wild = entry.endsWith("*");
  const body = wild ? entry.slice(0, -1) : entry;
  const tail = wild ? "" : "(?![А-Яа-яЁёA-Za-z0-9_])";
  return new RegExp(`(?<![А-Яа-яЁёA-Za-z0-9_])${escapeForRegExp(body)}${tail}`, "i");
}

/** Имя: подчёркивание и дефис считаются границей, буква и цифра — нет. */
function nameRegExp(entry) {
  return new RegExp(
    `(?<![A-Za-z0-9А-Яа-яЁё])${escapeForRegExp(entry)}(?![A-Za-z0-9А-Яа-яЁё])`,
    "i",
  );
}

/**
 * Текст без комментариев и без строковых литералов, номера строк сохранены.
 *
 * Имя — это идентификатор в коде, а не слово в прозе: «поля» по-русски значит
 * ещё и поля события, и запрещать его в комментарии было бы глупостью. Поэтому
 * разбор имён смотрит только на то, что осталось после этой чистки.
 */
function bare(name, suffix, text) {
  const line = DASH_COMMENT.has(suffix) ? "--" : HASH_COMMENT.has(suffix) || SOURCE_NAMES.has(name) ? "#" : "//";
  const block = line === "#" ? null : "/*";
  const out = [];
  let index = 0;

  const blank = (from, to) => {
    for (let at = from; at < to && at < text.length; at += 1) {
      out.push(text[at] === "\n" ? "\n" : " ");
    }
  };

  while (index < text.length) {
    const symbol = text[index];
    if (text.startsWith(line, index)) {
      const end = text.indexOf("\n", index);
      const stop = end === -1 ? text.length : end;
      blank(index, stop);
      index = stop;
      continue;
    }
    if (block !== null && text.startsWith(block, index)) {
      const end = text.indexOf("*/", index + 2);
      const stop = end === -1 ? text.length : end + 2;
      blank(index, stop);
      index = stop;
      continue;
    }
    if (QUOTES.has(symbol)) {
      let at = index + 1;
      while (at < text.length && text[at] !== symbol) {
        at += text[at] === "\\" ? 2 : 1;
      }
      blank(index, Math.min(at + 1, text.length));
      index = at + 1;
      continue;
    }
    out.push(symbol);
    index += 1;
  }

  return out.join("");
}

function tables(text) {
  const lines = text.split("\n");
  const collected = [];
  let current = null;
  lines.forEach((raw, index) => {
    const line = raw.trim();
    if (!line.startsWith("|") || !line.endsWith("|")) {
      current = null;
      return;
    }
    const cells = line.slice(1, -1).split("|").map((cell) => cell.trim());
    if (current === null) {
      current = { headers: cells, rows: [] };
      collected.push(current);
      return;
    }
    if (cells.every((cell) => /^[-: ]*$/.test(cell))) {
      return;
    }
    current.rows.push({ cells, line: index + 1 });
  });
  return collected;
}

function tableBy(text, header) {
  return tables(text).filter((table) => table.headers[0] === header);
}

const BACKTICKED = new RegExp("`([^`]+)`", "g");

function backticked(cell) {
  return [...cell.matchAll(BACKTICKED)].map((match) => match[1]);
}

function parseVoice(text) {
  const stop = [];
  const evaluative = [];
  const silent = new Map();
  const names = new Map();

  tableBy(text, "Слово").forEach((table) => {
    table.rows.forEach((row) => {
      const [word, where, , instead] = row.cells;
      backticked(word).forEach((entry) => {
        stop.push({
          entry,
          where: backticked(where)[0] ?? EVERYWHERE,
          instead: instead ?? "",
          match: phraseRegExp(entry),
        });
      });
    });
  });

  tableBy(text, "Оценка").forEach((table) => {
    table.rows.forEach((row) => {
      const [word, , instead] = row.cells;
      backticked(word).forEach((entry) => {
        evaluative.push({ entry, instead: instead ?? "", match: phraseRegExp(entry) });
      });
    });
  });

  tableBy(text, "Код").forEach((table) => {
    table.rows.forEach((row) => {
      backticked(row.cells[0]).forEach((code) => silent.set(code, row.cells[1] ?? ""));
    });
  });

  tableBy(text, "Где").forEach((table) => {
    table.rows.forEach((row) => {
      const scope = backticked(row.cells[0])[0];
      if (!scope) {
        return;
      }
      names.set(
        scope,
        backticked(row.cells[1]).map((entry) => ({ entry, why: row.cells[2] ?? "" })),
      );
    });
  });

  if (stop.length === 0) {
    throw new VoiceError(`${VOICE}: таблицы стоп-слов не нашлось`);
  }
  if (!names.has("локализация") || !names.has("имена")) {
    throw new VoiceError(`${VOICE}: таблицы разделения имён не нашлось`);
  }

  return { stop, evaluative, silent, names };
}

function walk(root, current, out) {
  const here = join(root, current);
  if (!existsSync(here)) {
    return;
  }
  readdirSync(here, { withFileTypes: true }).forEach((entry) => {
    if (SKIP_DIRS.has(entry.name)) {
      return;
    }
    const next = current ? `${current}/${entry.name}` : entry.name;
    if (entry.isDirectory()) {
      walk(root, next, out);
      return;
    }
    if (entry.isFile()) {
      out.push(next);
    }
  });
}

function allFiles(root) {
  const out = [];
  walk(root, "", out);
  return out.sort();
}

function localeFiles(files) {
  return files.filter(
    (path) =>
      path.startsWith(`${CLIENTS}/`) &&
      path.includes(`/${LOCALE_DIR}/`) &&
      path.endsWith(".json"),
  );
}

function lineOf(text, section, key) {
  const sectionAt = text.indexOf(`"${section}"`);
  const from = sectionAt === -1 ? 0 : sectionAt;
  const keyAt = text.indexOf(`"${key}"`, from);
  if (keyAt === -1) {
    return 1;
  }
  return text.slice(0, keyAt).split("\n").length;
}

function readLocalization(root, files) {
  const strings = [];
  const problems = [];

  localeFiles(files).forEach((path) => {
    const raw = readFileSync(join(root, path), "utf8");
    let parsed;
    try {
      parsed = JSON.parse(raw);
    } catch (error) {
      problems.push(`${path}: разобрать не удалось — ${error.message}`);
      return;
    }
    Object.entries(parsed).forEach(([section, entries]) => {
      if (entries === null || typeof entries !== "object" || Array.isArray(entries)) {
        problems.push(
          `${path}: раздел «${section}» не набор строк. Раздел определяет, какие правила ` +
            `действуют: problem — отказ, student — читает ученик`,
        );
        return;
      }
      Object.entries(entries).forEach(([key, value]) => {
        if (typeof value !== "string") {
          problems.push(`${path}: «${section}.${key}» не строка`);
          return;
        }
        strings.push({ path, section, key, value, line: lineOf(raw, section, key) });
      });
    });
  });

  return { strings, problems };
}

function domainCodes(root, files) {
  const codes = new Map();
  const problems = [];

  files
    .filter((path) => /\.(cpp|hpp|cc|cxx|hxx)$/.test(path) && !path.includes("/tests/"))
    .forEach((path) => {
      const text = readFileSync(join(root, path), "utf8");
      let found = 0;
      ERROR_SHAPES.forEach((shape) => {
        [...text.matchAll(shape)].forEach((match) => {
          found += 1;
          if (!codes.has(match[1])) {
            codes.set(match[1], {
              path,
              line: text.slice(0, match.index).split("\n").length,
            });
          }
        });
      });
      const sites = [...text.matchAll(ERROR_SITE)].length;
      if (sites > found) {
        problems.push(
          `${path}: отказов построено ${sites}, а кодов разобрано ${found}. Разбор понимает ` +
            `две формы — core::Error{ErrorKind::…, "код", …} и Refuse("код", …); третья ` +
            `добавляется в линтер тем же изменением, которым появляется`,
        );
      }
    });

  return { codes, problems };
}

function currentBranch(root) {
  const pointer = join(root, ".git");
  if (!existsSync(pointer)) {
    return null;
  }
  let head = pointer;
  if (statSync(pointer).isFile()) {
    const link = readFileSync(pointer, "utf8").trim();
    const at = link.indexOf("gitdir:");
    if (at === -1) {
      return null;
    }
    head = resolve(root, link.slice(at + "gitdir:".length).trim());
  }
  const file = join(head, "HEAD");
  if (!existsSync(file)) {
    return null;
  }
  const text = readFileSync(file, "utf8").trim();
  const at = text.indexOf("refs/heads/");
  return at === -1 ? null : text.slice(at + "refs/heads/".length);
}

function checkWords(strings, voice) {
  const violations = [];
  strings.forEach((entry) => {
    voice.stop.forEach((word) => {
      if (word.where !== EVERYWHERE && word.where !== entry.section) {
        return;
      }
      if (word.match.test(entry.value)) {
        violations.push(
          `${entry.path}:${entry.line}: «${word.entry}» в «${entry.section}.${entry.key}». ` +
            `Вместо: ${word.instead}. Правило — ${VOICE}`,
        );
      }
    });
    if (entry.section === STUDENT) {
      voice.evaluative.forEach((word) => {
        if (word.match.test(entry.value)) {
          violations.push(
            `${entry.path}:${entry.line}: «${word.entry}» в тексте для ученика ` +
              `(«${entry.section}.${entry.key}»). Продукт показывает факты, оценивает ` +
              `репетитор. Вместо: ${word.instead}`,
          );
        }
      });
    }
  });
  return violations;
}

function checkRefusals(strings) {
  return strings
    .filter((entry) => entry.section === REFUSAL && !ACTION.test(entry.value))
    .map(
      (entry) =>
        `${entry.path}:${entry.line}: отказ «${entry.key}» не говорит, что делать. ` +
        `Добавьте действие в повелительном наклонении — «выберите другое время», ` +
        `«напишите репетитору». Правило — ${VOICE}`,
    );
}

function checkCodes(codes, problems, strings, voice) {
  const violations = [...problems];
  const described = new Map(
    strings.filter((entry) => entry.section === REFUSAL).map((entry) => [entry.key, entry]),
  );

  [...codes.keys()].sort().forEach((code) => {
    const where = codes.get(code);
    if (described.has(code) || voice.silent.has(code)) {
      if (described.has(code) && voice.silent.has(code)) {
        violations.push(
          `${VOICE}: у кода «${code}» есть и текст, и строка «человек не увидит». ` +
            `Одно из двух: либо человек его читает, либо нет`,
        );
      }
      return;
    }
    violations.push(
      `${where.path}:${where.line}: у отказа «${code}» нет русского текста. Напишите его ` +
        `в разделе problem файла локализации или назовите код в таблице «Коды, которых ` +
        `человек не увидит» (${VOICE}). Запасного текста на чужом языке не существует`,
    );
  });

  described.forEach((entry, code) => {
    if (!codes.has(code)) {
      violations.push(
        `${entry.path}:${entry.line}: текст написан на код «${code}», которого в дереве нет. ` +
          `Уберите строку: она осталась от удалённого сценария`,
      );
    }
  });

  return violations;
}

function checkNames(root, strings, voice, files) {
  const violations = [];

  (voice.names.get("локализация") ?? []).forEach((forbidden) => {
    const match = phraseRegExp(forbidden.entry);
    strings.forEach((entry) => {
      if (match.test(entry.value)) {
        violations.push(
          `${entry.path}:${entry.line}: «${forbidden.entry}» в «${entry.section}.${entry.key}». ` +
            `${forbidden.why}`,
        );
      }
    });
  });

  const inNames = (voice.names.get("имена") ?? []).map((forbidden) => ({
    entry: forbidden.entry,
    why: forbidden.why,
    match: nameRegExp(forbidden.entry),
  }));

  const branch = currentBranch(root);
  inNames.forEach((forbidden) => {
    if (branch !== null && forbidden.match.test(branch)) {
      violations.push(
        `ветка ${branch}: «${forbidden.entry}» в имени ветки. ${forbidden.why}`,
      );
    }
  });

  files.forEach((path) => {
    const segments = path.split("/");
    const named = new Set();
    segments.forEach((segment, depth) => {
      inNames.forEach((forbidden) => {
        const mark = `${forbidden.entry}/${segment}`;
        if (!forbidden.match.test(segment) || named.has(mark)) {
          return;
        }
        named.add(mark);
        const what = depth === segments.length - 1 ? "файла" : "каталога";
        violations.push(
          `${path}: «${forbidden.entry}» в имени ${what} «${segment}». ${forbidden.why}`,
        );
      });
    });

    const name = path.split("/").pop();
    const dot = name.lastIndexOf(".");
    const suffix = dot === -1 ? "" : name.slice(dot);
    if (!SOURCE_SUFFIXES.has(suffix) && !SOURCE_NAMES.has(name)) {
      return;
    }
    const text = bare(name, suffix, readFileSync(join(root, path), "utf8"));
    const seen = new Set();
    [...text.matchAll(IDENTIFIER)].forEach((match) => {
      const token = match[0];
      if (seen.has(token)) {
        return;
      }
      inNames.forEach((forbidden) => {
        if (forbidden.match.test(token)) {
          seen.add(token);
          violations.push(
            `${path}:${text.slice(0, match.index).split("\n").length}: «${forbidden.entry}» ` +
              `в имени «${token}». ${forbidden.why}`,
          );
        }
      });
    });
  });

  return violations;
}

function check(root) {
  const voicePath = join(root, VOICE);
  if (!existsSync(voicePath)) {
    return { violations: [`${VOICE}: правил речи нет`], strings: 0, codes: 0 };
  }

  let voice;
  try {
    voice = parseVoice(readFileSync(voicePath, "utf8"));
  } catch (error) {
    return { violations: [error.message], strings: 0, codes: 0 };
  }

  const files = allFiles(root);
  const { strings, problems } = readLocalization(root, files);
  const { codes, problems: shapes } = domainCodes(root, files);
  if (strings.length === 0 && problems.length === 0) {
    problems.push(
      `${CLIENTS}/**/${LOCALE_DIR}/*.json: файлов локализации нет. Тексты живут в них, а не ` +
        `в компонентах — иначе речь продукта проверить нечем`,
    );
  }

  const violations = [
    ...problems,
    ...checkWords(strings, voice),
    ...checkRefusals(strings),
    ...checkCodes(codes, shapes, strings, voice),
    ...checkNames(root, strings, voice, files),
  ];

  return { violations, strings: strings.length, codes: codes.size };
}

const SELFTEST_VOICE = `# Правила речи для самопроверки

| Слово | Где | Почему не пишем | Как вместо |
| --- | --- | --- | --- |
| \`просто\` | \`везде\` | «просто нажмите» — это «ты глупый» | «Нажмите» |
| \`ошибк*\` | \`problem\` | называет наш сбой | «Это время уже занято» |

| Оценка | Почему не пишем | Как вместо |
| --- | --- | --- |
| \`молодец\` | оценивает репетитор | назовите, что сделано |

| Код | Почему человек его не увидит |
| --- | --- |
| \`job_settings_missing\` | разбор настроек фонового задания |
| \`job_run_counter_negative\` | запись о прогоне задания, её ведёт механизм |

| Где | Что запрещено | Почему |
| --- | --- | --- |
| \`локализация\` | \`ПДР\`, \`PDR\` | с человеком говорит бренд |
| \`имена\` | \`Поля\`, \`polya\` | внутри всё остаётся ПДР |
`;

const SELFTEST_LOCALE = JSON.stringify(
  {
    problem: {
      slot_already_taken: "Это время занято.",
      job_settings_missing: "Настроек задания нет. Проверьте конфигурацию.",
      ghost_code: "Такого отказа больше нет. Обновите страницу.",
      lesson_starts_in_past: "Это время уже прошло. Выберите другое.",
    },
    student: {
      "skills.title": "Что уже получается",
      "skills.praise": "Молодец, так держать",
    },
    common: {
      "app.name": "Поля",
      "app.about": "Просто откройте ПДР и начните занятие",
    },
  },
  null,
  2,
);

const SELFTEST_SOURCE = `#include "core/errors.hpp"

namespace pdr::scheduling {

core::Result<int> Book() {
    return core::Error{core::ErrorKind::kConflict, "slot_already_taken", "занято"};
}

core::Result<int> Start() {
    return core::Error{core::ErrorKind::kValidation, "lesson_starts_in_past", "прошло"};
}

core::Result<int> Settle() {
    return core::Error{core::ErrorKind::kValidation, "job_settings_missing", "нет настроек"};
}

core::Result<int> Count() {
    return core::Error{core::ErrorKind::kValidation, "job_run_counter_negative", "минус"};
}

core::Result<int> Quote() {
    return core::Error{core::ErrorKind::kNotFound, "tariff_not_found", "нет тарифа"};
}

}  // namespace pdr::scheduling
`;

const SELFTEST_FILES = {
  "docs/product/voice.md": SELFTEST_VOICE,
  "clients/shared/i18n/ru.json": `${SELFTEST_LOCALE}\n`,
  "libs/pdr-scheduling/src/scheduling/application/book_lesson.cpp": SELFTEST_SOURCE,
  "libs/pdr-polya/CMakeLists.txt": "add_library(pdr_polya STATIC src/polya.cpp)\n",
};

const SELFTEST_EXPECTED = [
  ["app.about", "«просто»"],
  ["slot_already_taken", "не говорит, что делать"],
  ["tariff_not_found", "нет русского текста"],
  ["skills.praise", "«молодец»"],
  ["app.about", "«ПДР»"],
  ["libs/pdr-polya", "«polya»"],
  ["job_settings_missing", "и текст, и строка"],
  ["ghost_code", "которого в дереве нет"],
];

const SELFTEST_CLEAN = ["skills.title", "app.name", "lesson_starts_in_past", "job_run_counter_negative"];

function selftest() {
  const root = mkdtempSync(join(tmpdir(), "pdr-copy-"));
  try {
    Object.entries(SELFTEST_FILES).forEach(([name, content]) => {
      const target = join(root, name);
      mkdirSync(dirname(target), { recursive: true });
      writeFileSync(target, content, "utf8");
    });

    const { violations } = check(root);

    for (const [name, fragment] of SELFTEST_EXPECTED) {
      if (!violations.some((line) => line.includes(name) && line.includes(fragment))) {
        process.stderr.write(`самопроверка: не поймано «${fragment}» у ${name}\n`);
        violations.forEach((line) => process.stderr.write(`    ${line}\n`));
        return 1;
      }
    }

    for (const name of SELFTEST_CLEAN) {
      if (violations.some((line) => line.includes(name))) {
        process.stderr.write(`самопроверка: правильное объявлено нарушением: ${name}\n`);
        violations.forEach((line) => process.stderr.write(`    ${line}\n`));
        return 1;
      }
    }

    rmSync(join(root, "clients"), { recursive: true, force: true });
    const empty = check(root);
    if (!empty.violations.some((line) => line.includes("файлов локализации нет"))) {
      process.stderr.write("самопроверка: пропажа локализации прошла мимо проверки\n");
      return 1;
    }
  } finally {
    rmSync(root, { recursive: true, force: true });
  }

  process.stdout.write(
    `Самопроверка пройдена: ${SELFTEST_EXPECTED.length + 1} нарушений найдено там, где они ` +
      "есть, и ни одного там, где их нет.\n",
  );
  return 0;
}

function main(argv) {
  const here = dirname(fileURLToPath(import.meta.url));
  const root = resolve(here, "..");

  if (argv.includes("--selftest")) {
    return selftest();
  }

  const { violations, strings, codes } = check(root);
  violations.forEach((line) => process.stderr.write(`${line}\n`));

  if (violations.length > 0) {
    process.stderr.write(`Нарушений: ${violations.length}. Правила речи — ${VOICE}\n`);
    return 1;
  }

  process.stdout.write(
    `Строк локализации: ${strings}, кодов отказа: ${codes}. Продукт разговаривает по правилам.\n`,
  );
  return 0;
}

process.exit(main(process.argv.slice(2)));
