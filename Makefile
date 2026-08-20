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

.DEFAULT_GOAL := help
.PHONY: help up down test fmt logs migrate ps check-env

help:
	@echo "Цели:"
	@echo "  make up          поднять всё с нуля: база, заглушка ml, миграции"
	@echo "  make down        погасить (make down VOLUMES=1 — вместе с томами)"
	@echo "  make test        собрать, прогнать тесты и все проверки границ"
	@echo "  make fmt         привести C++ к .clang-format"
	@echo "  make logs        смотреть логи (make logs SERVICE=postgres)"
	@echo "  make migrate     применить миграции из $(MIGRATIONS)"
	@echo "  make ps          что сейчас запущено"
	@echo
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
	@$(MAKE) --no-print-directory migrate
	@echo
	@echo "поднято. Что дальше:"
	@echo "    make ps      посмотреть состояние"
	@echo "    make logs    посмотреть логи"
	@echo "    make down    погасить"

down: check-env
	$(COMPOSE) down --remove-orphans $(if $(VOLUMES),--volumes,)
	@test -z "$(VOLUMES)" && echo "тома остались на месте; убрать: make down VOLUMES=1" || \
		echo "тома удалены вместе с данными"

# Миграции применяются по порядку имён. Учёта уже применённых здесь нет и не
# будет: это дело области DB, вместе с первой же миграцией. Пока файлов нет,
# применять нечего — и цель честно об этом говорит, а не делает вид.
migrate: check-env
	@if [ ! -d $(MIGRATIONS) ] || [ -z "$$(ls $(MIGRATIONS)/*.sql 2>/dev/null)" ]; then \
		echo "миграций нет — применять нечего"; \
	else \
		for file in $$(ls $(MIGRATIONS)/*.sql | sort); do \
			echo "применяю $$file"; \
			$(COMPOSE) exec -T postgres \
				sh -c 'psql -v ON_ERROR_STOP=1 -U "$$POSTGRES_USER" -d "$$POSTGRES_DB"' < $$file || exit 1; \
		done; \
	fi

ps: check-env
	$(COMPOSE) ps

logs: check-env
	$(COMPOSE) logs --follow --tail=100 $(SERVICE)

# Тесты не требуют ни базы, ни докера, ни сети — поэтому цель не зависит от up.
test:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) --parallel
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	python3 scripts/check_layers.py --selftest
	python3 scripts/check_layers.py
	python3 scripts/check_table_owners.py --selftest
	python3 scripts/check_table_owners.py
	python3 scripts/verify_env_parity.py --selftest
	python3 scripts/verify_env_parity.py

fmt:
	@command -v clang-format >/dev/null || { echo "нет clang-format"; exit 1; }
	clang-format -i $$(find libs -name '*.hpp' -o -name '*.cpp')
	@echo "формат приведён к .clang-format"
