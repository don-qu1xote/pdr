# Схема базы

<!-- Файл собран scripts/gen_schema_doc.py из db/migrations. Руками не правится:
     правка переживёт ровно до следующей пересборки. Изменить схему — значит
     написать новую миграцию. -->

Собрано из миграций: 1. Таблиц: 1.

Правила, которым подчиняется каждая колонка, — в
[migrations.md](migrations.md). Первой схемы предметной области здесь пока нет:
её заводит `PDR-DB-02`.

## Таблицы

### schema_version

Применённые миграции: версия, момент применения в UTC и контрольная сумма файла.

Заведена миграцией `V001__schema_version.sql`.

| Колонка | Тип | Определение |
| --- | --- | --- |
| `version` | `integer` | integer primary key |
| `applied_at` | `timestamptz` | timestamptz not null default now() |
| `checksum` | `char(64)` | char(64) not null |

## Порядок применения

1. `V001__schema_version.sql` — schema_version
