# Одна команда, чтобы поднять, и одна, чтобы проверить.
#
# Живого прода нет и не планируется — здесь нет и не будет целей вроде «deploy»
# или «release»: docs/adr/0007-local-without-fake-production.md. Это рабочая
# локальная установка, а не заготовка под прод.
#
# Профиль выбирается переменной: make up ENV_PROFILE=ci. Второго compose-файла
# не существует, различия профилей живут в deploy/env/<профиль>.env.

ENV_PROFILE ?= local
ENV_FILE := deploy/env/$(ENV_PROFILE).env
COMPOSE := docker compose --env-file $(ENV_FILE) -f deploy/docker-compose.yml
MIGRATIONS := db/migrations
BUILD_DIR ?= build

# Подключение к базе профиля: psql и scripts/migrate.py читают обычные PG*.
# Значения берутся из того же файла профиля, второго источника правды нет.
PG_ENV = set -a; . ./$(ENV_FILE); set +a; \
	export PGHOST=$${PGHOST:-127.0.0.1} PGPORT=$$POSTGRES_PORT PGUSER=$$POSTGRES_USER \
	       PGPASSWORD=$$POSTGRES_PASSWORD PGDATABASE=$$POSTGRES_DB;

.DEFAULT_GOAL := help
.PHONY: help up down test test-unit test-isolation test-jobs test-idempotency test-plans fmt fmt-check \
        comments comments-fix hooks logs migrate migrate-verify migrate-status schema-doc \
        product-events-lock product-events-export product-events-prune \
        idempotency-prune \
        account-export account-delete practice-queue ps check-env permissions-lock

help:
	@echo "Цели:"
	@echo "  make up          поднять всё с нуля: база, заглушка ml, миграции"
	@echo "  make down        погасить (make down VOLUMES=1 — вместе с томами)"
	@echo "  make test        собрать, прогнать тесты и все проверки границ"
	@echo "  make test-unit   только unit-прогон: без базы, без докера, за миллисекунды"
	@echo "  make test-isolation   проверить изоляцию арендаторов на живой базе"
	@echo "  make test-jobs   проверить одиночные задания на живой базе"
	@echo "  make test-idempotency  проверить защиту от повтора на живой базе"
	@echo "  make test-plans  снять планы горячих запросов на живой базе"
	@echo "  make fmt         привести C++ к .clang-format"
	@echo "  make fmt-check   проверить формат, ничего не меняя (та же цель в CI и в хуке)"
	@echo "  make comments    проверить политику комментариев (та же цель в CI и в хуке)"
	@echo "  make comments-fix   снять комментарии, нарушающие правило"
	@echo "  make hooks       включить githooks/ (pre-commit проверяет формат и комментарии)"
	@echo "  make logs        смотреть логи (make logs SERVICE=postgres)"
	@echo "  make migrate     применить миграции из $(MIGRATIONS)"
	@echo "  make migrate-verify   сверить суммы, ничего не применяя"
	@echo "  make migrate-status   что применено, что ждёт"
	@echo "  make schema-doc  пересобрать docs/architecture/schema.md"
	@echo "  make product-events-lock     пересобрать снимок опубликованных схем событий"
	@echo "  make product-events-export OUT=<файл>   выгрузить продуктовый поток в CSV"
	@echo "  make product-events-prune DAYS=<дней>   убрать записи старше срока"
	@echo "  make idempotency-prune                  убрать просроченные ключи повтора"
	@echo "  make account-export TENANT=<uuid> OUT=<файл>   полная выгрузка аккаунта"
	@echo "  make account-delete TENANT=<uuid>              удалить практику целиком"
	@echo "  make practice-queue                            кто ждёт разбора публикации"
	@echo "  make ps          что сейчас запущено"
	@echo
	@echo "Уровни тестов и куда писать новый — docs/testing.md"
	@echo "Профиль: ENV_PROFILE=$(ENV_PROFILE) (файл $(ENV_FILE))"

check-env:
	@test -f $(ENV_FILE) || { \
		echo "нет $(ENV_FILE)"; \
		echo "скопируйте пример и поменяйте пароль:"; \
		echo "    cp $(ENV_FILE).example $(ENV_FILE)"; \
		exit 1; \
	}

up: check-env
	$(COMPOSE) up --detach --wait
	@set -a; . ./$(ENV_FILE); set +a; \
	if [ "$$MIGRATE_ON_START" = "1" ]; then \
		$(MAKE) --no-print-directory migrate; \
	else \
		echo "MIGRATE_ON_START=$$MIGRATE_ON_START — не применяю, только сверяю"; \
		$(MAKE) --no-print-directory migrate-verify; \
	fi
	@echo
	@echo "поднято. Что дальше:"
	@echo "    make ps      посмотреть состояние"
	@echo "    make logs    посмотреть логи"
	@echo "    make down    погасить"

down: check-env
	$(COMPOSE) down --remove-orphans $(if $(VOLUMES),--volumes,)
	@test -z "$(VOLUMES)" && echo "тома остались на месте; убрать: make down VOLUMES=1" || \
		echo "тома удалены вместе с данными"

# Применяет то, чего нет в реестре, и сверяет суммы уже применённого.
# Применённая миграция никогда не редактируется: расхождение суммы — отказ,
# а не предупреждение (docs/adr/0010-applied-migrations-are-never-edited.md).
migrate: check-env
	@$(PG_ENV) python3 scripts/migrate.py apply --dir $(MIGRATIONS)

migrate-verify: check-env
	@$(PG_ENV) python3 scripts/migrate.py verify --dir $(MIGRATIONS)

migrate-status: check-env
	@$(PG_ENV) python3 scripts/migrate.py status --dir $(MIGRATIONS)

# Изоляция арендаторов на живой базе: главная проверка всей схемы. Требует
# поднятой установки (make up) — в отличие от make test, которому база не нужна.
# Тест пишет в базу профиля и убирает за собой.
test-isolation: check-env
	@$(PG_ENV) python3 scripts/check_isolation.py

# Одиночные задания на живой базе: блокировку берёт один, потеря блокировки не
# приводит к двойному действию, возраст последнего прогона растёт. Тому же
# требуется поднятая установка; unit-часть механизма гоняется без базы (ctest).
test-jobs: check-env
	@$(PG_ENV) python3 scripts/check_jobs.py

# Идемпотентность: повтор с тем же ключом операцию не выполняет, одновременный
# повтор ждёт. Проверять на фейке недостаточно — весь смысл в том, что делает
# база, когда два обращения приходят одновременно на разные реплики.
test-idempotency: check-env
	@$(PG_ENV) python3 scripts/check_idempotency.py

# Планы горячих запросов на живой базе. Два шага, и первый обязателен: на
# пустой базе любой план — перебор, и он правильный. Засев повторяем, поэтому
# цель можно звать сколько угодно раз подряд.
test-plans: check-env
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -qtA -f db/explain/seed.sql >/dev/null
	@$(PG_ENV) python3 scripts/check_plans.py

# Матрица прав не пишется руками — она собирается из самих политик опросом по
# всем действиям, ролям и отношениям. Написанная руками, она расходится с кодом
# на первой правке и после этого хуже, чем её отсутствие: по ней принимают
# решения, а она врёт. Сверяет её тот же прогон, что и всё остальное (make test).
permissions-lock:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) --target pdr_unit_tests --parallel
	@$(BUILD_DIR)/pdr_unit_tests \
		--gtest_filter='PermissionsMatrix.TheFileInDocsSaysWhatTheCodeDoes' >/dev/null || true
	@cp $(BUILD_DIR)/permissions.md docs/architecture/permissions.md
	@echo "матрица прав перезаписана: docs/architecture/permissions.md"

# Документ схемы не пишется руками — он собирается из миграций.
schema-doc:
	python3 scripts/gen_schema_doc.py

# Снимок опубликованных схем продуктовых событий. Пишет машина; поле, пропавшее
# из уже опубликованной пары «тип + версия», цель записать откажется — обход
# правила не состоит из одной команды (docs/architecture/product-events.md).
product-events-lock:
	@python3 scripts/check_product_events.py --update

# Выгрузка обезличенного потока для анализа. Идёт под административной ролью, а
# не под pdr_app: политика для выгрузки исключений не делает, и читать поток
# целиком может только тот, кто и так может всё.
product-events-export: check-env
	@test -n "$(OUT)" || { \
		echo "куда выгружать: make product-events-export OUT=product-events.csv"; \
		exit 1; \
	}
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -qtA \
		-f db/observability/export.sql > $(OUT)
	@echo "выгружено: $(OUT), строк вместе с заголовком: $$(wc -l < $(OUT))"

# Полная выгрузка аккаунта одним действием: обещание «данные ваши» проверяется
# командой, а не абзацем в оферте. Идёт под ролью приложения с объявленным
# арендатором — то есть отдаёт ровно то, что видит сам аккаунт, и ни строкой
# больше (docs/architecture/openness.md).
account-export: check-env
	@test -n "$(TENANT)" || { \
		echo "чей аккаунт: make account-export TENANT=<uuid> OUT=account.json"; \
		exit 1; \
	}
	@test -n "$(OUT)" || { \
		echo "куда выгружать: make account-export TENANT=<uuid> OUT=account.json"; \
		exit 1; \
	}
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -qtA -v tenant=$(TENANT) \
		-f db/account/export.sql > $(OUT)
	@echo "выгружено: $(OUT), частей: $$(grep -c '": \[' $(OUT))"

# Удаление практики целиком. Переезжать к нам будут ровно настолько охотно,
# насколько легко уехать обратно, поэтому удаление есть с первого дня и делается
# одной командой. Идёт под ролью миграций: удалять приходится и то, что роли
# приложения не отдаётся (сессии, отпечатки ссылок, счётчики попыток).
#
# Выгрузку делают ДО этого: make account-export TENANT=... OUT=...
account-delete: check-env
	@test -n "$(TENANT)" || { \
		echo "какую практику: make account-delete TENANT=<uuid>"; \
		echo "сначала выгрузите: make account-export TENANT=<uuid> OUT=account.json"; \
		exit 1; \
	}
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -qtA -v tenant=$(TENANT) \
		-f db/account/delete.sql

# Очередь на разбор публикации: кто попросил показывать себя в подборе. Общая
# поверх всех практик, поэтому под ролью миграций, а не под ролью приложения.
practice-queue: check-env
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -qtA -f db/practice/queue.sql

# Уборка по сроку жизни. Числа по умолчанию у цели нет намеренно: срок живёт в
# PDR_PRODUCT_EVENTS.retention_days, и второго источника правды не заводится.
product-events-prune: check-env
	@test -n "$(DAYS)" || { \
		echo "сколько дней хранить: make product-events-prune DAYS=730"; \
		echo "значение — PDR_PRODUCT_EVENTS.retention_days из configs/dynamic/registry.yaml"; \
		exit 1; \
	}
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -tA -v days=$(DAYS) \
		-f db/observability/prune.sql

# Уборка просроченных ключей идемпотентности. Под ролью МИГРАЦИЙ и по всем
# практикам сразу: построчная защита отвечает на вопрос «чьи это данные», а у
# уборки такого вопроса нет — она удаляет по сроку.
idempotency-prune:
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -tA -f db/http/prune.sql
	@echo "просроченные ключи убраны"

ps: check-env
	$(COMPOSE) ps

logs: check-env
	$(COMPOSE) logs --follow --tail=100 $(SERVICE)

# Тесты не требуют ни базы, ни докера, ни сети — поэтому цель не зависит от up.
# Уровни пирамиды и куда писать новый тест — docs/testing.md.
#
# Кроме cmake и python3 цели нужен node: правила речи проверяются по файлам
# локализации, а они живут в clients/ (docs/product/voice.md).
test:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) --parallel
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	python3 scripts/check_layers.py --selftest
	python3 scripts/check_layers.py
	python3 scripts/check_table_owners.py --selftest
	python3 scripts/check_table_owners.py
	python3 scripts/check_migrations.py --selftest
	python3 scripts/check_migrations.py
	python3 scripts/check_rls.py --selftest
	python3 scripts/check_rls.py
	python3 scripts/check_testsuite.py --selftest
	python3 scripts/check_testsuite.py
	python3 scripts/check_handmade.py --selftest
	python3 scripts/check_handmade.py
	python3 scripts/check_dynamic_configs.py --selftest
	python3 scripts/check_dynamic_configs.py
	python3 scripts/check_guardian_notice.py --selftest
	python3 scripts/check_guardian_notice.py
	python3 scripts/check_adult_student.py --selftest
	python3 scripts/check_adult_student.py
	python3 scripts/check_http_form.py --selftest
	python3 scripts/check_http_form.py
	python3 scripts/check_product_events.py --selftest
	python3 scripts/check_product_events.py
	python3 scripts/check_glossary.py --selftest
	python3 scripts/check_glossary.py
	node scripts/check-copy.mjs --selftest
	node scripts/check-copy.mjs
	python3 scripts/check_openness.py --selftest
	python3 scripts/check_openness.py
	python3 scripts/check_diagrams.py --selftest
	python3 scripts/check_diagrams.py
	python3 scripts/check_integrations.py --selftest
	python3 scripts/check_integrations.py
	python3 scripts/check_sovereignty.py --selftest
	python3 scripts/check_sovereignty.py
	python3 scripts/compare_quality.py --selftest
	python3 scripts/check_debts.py --selftest
	python3 scripts/check_debts.py
	python3 scripts/check_secrets.py --selftest
	python3 scripts/check_secrets.py
	python3 scripts/detect_changes.py --selftest
	python3 scripts/check_format.py --selftest
	python3 scripts/check_comments.py --selftest
	python3 scripts/check_comments.py
	python3 scripts/check_plans.py --selftest
	python3 scripts/gen_schema_doc.py --check
	python3 scripts/verify_env_parity.py --selftest
	python3 scripts/verify_env_parity.py

# Самый частый прогон рабочего дня: домен и сценарии на фейках. У этой цели нет
# доступа к базе — не по договорённости, а потому, что она не линкуется ни с
# адаптерами, ни с драйвером (проверяется конфигурацией CMake).
test-unit:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) --target pdr_unit_tests --parallel
	ctest --test-dir $(BUILD_DIR) --output-on-failure -R '^unit$$'

fmt:
	@python3 scripts/check_format.py --fix

# Ту же цель зовут и CI (джоба lint), и хук githooks/pre-commit — не три похожие
# команды, а буквально одна. Проверяется ВЕРСИЯ форматтера, а не его наличие:
# clang-format не байт-стабилен между версиями, и чужая версия отформатирует
# «чисто», а CI покраснеет. Версия закреплена в .clang-format-version.
fmt-check:
	@python3 scripts/check_format.py

# Комментарии: в коде остаётся только то, без чего не соберётся или не проверится
# сборка (docs/comments.md). Ту же цель зовут и CI (джоба comments), и хук
# githooks/pre-commit — не три похожие команды, а буквально одна.
comments:
	@python3 scripts/check_comments.py

comments-fix:
	@python3 scripts/check_comments.py --fix

# Хуки лежат в githooks/ и включаются одной командой: копировать их в .git/hooks
# нельзя — копия перестаёт обновляться вместе с репозиторием в тот же день.
hooks:
	git config core.hooksPath githooks
	@echo "хуки включены: githooks/. Обойти в исключительном случае — git commit --no-verify"
