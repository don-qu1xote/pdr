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
.PHONY: help up down test test-unit test-isolation test-jobs test-plans fmt fmt-check \
        comments comments-fix hooks logs migrate migrate-verify migrate-status schema-doc \
        ps check-env

help:
	@echo "Цели:"
	@echo "  make up          поднять всё с нуля: база, заглушка ml, миграции"
	@echo "  make down        погасить (make down VOLUMES=1 — вместе с томами)"
	@echo "  make test        собрать, прогнать тесты и все проверки границ"
	@echo "  make test-unit   только unit-прогон: без базы, без докера, за миллисекунды"
	@echo "  make test-isolation   проверить изоляцию арендаторов на живой базе"
	@echo "  make test-jobs   проверить одиночные задания на живой базе"
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

# Планы горячих запросов на живой базе. Два шага, и первый обязателен: на
# пустой базе любой план — перебор, и он правильный. Засев повторяем, поэтому
# цель можно звать сколько угодно раз подряд.
test-plans: check-env
	@$(PG_ENV) psql --no-psqlrc -v ON_ERROR_STOP=1 -qtA -f db/explain/seed.sql >/dev/null
	@$(PG_ENV) python3 scripts/check_plans.py

# Документ схемы не пишется руками — он собирается из миграций.
schema-doc:
	python3 scripts/gen_schema_doc.py

ps: check-env
	$(COMPOSE) ps

logs: check-env
	$(COMPOSE) logs --follow --tail=100 $(SERVICE)

# Тесты не требуют ни базы, ни докера, ни сети — поэтому цель не зависит от up.
# Уровни пирамиды и куда писать новый тест — docs/testing.md.
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
