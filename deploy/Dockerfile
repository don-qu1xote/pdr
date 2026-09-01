# syntax=docker/dockerfile:1

# Образ первого процесса ПДР.
#
# ОДИН БАЗОВЫЙ ОБРАЗ НА СБОРКУ И НА ЗАПУСК, а не «сборочный потолще, рабочий
# потоньше». userver — статическая библиотека с настоящими зависимостями
# (libpq, boost, c-ares, libargon2), и собирать тонкий рабочий слой значило бы
# перечислять их вторым списком, который разъедется с первым в первый же день.
# Живого прода нет и не планируется (ADR-0007) — платить за это размером образа
# на машине разработчика мы согласны.
#
# Тот же образ, что в CI (джоба adapters-with-userver): «у меня собирается» и
# «в CI собирается» обязаны значить одно и то же.
FROM ghcr.io/userver-framework/ubuntu-22.04-userver:latest AS build

# Argon2id считается ЭТАЛОННОЙ БИБЛИОТЕКОЙ, а не своей схемой (ADR-0013). В
# образе лежит только libargon2.so.1 — без заголовков цель счёта паролей
# пропускается конфигурацией, и сервис собрался бы без входа.
RUN apt-get update -qq --allow-releaseinfo-change \
    && apt-get install -y -qq --no-install-recommends libargon2-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# Release, а не Debug: в образе не место ни санитайзерам, ни отладочным
# символам на четыреста мегабайт. Тесты выключены — их гоняет CI, а не сборка
# образа.
RUN cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release -DPDR_BUILD_TESTS=OFF \
    && cmake --build /build --target pdr_scheduling_service --parallel "$(nproc)"

FROM ghcr.io/userver-framework/ubuntu-22.04-userver:latest

RUN apt-get update -qq --allow-releaseinfo-change \
    && apt-get install -y -qq --no-install-recommends libargon2-1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /build/services/scheduling/pdr_scheduling_service \
                  /usr/local/bin/pdr_scheduling_service

# Конфигурация едет В ОБРАЗЕ, а секреты — нет: файл секретов монтируется извне
# с правами 0600 (deploy/README.md). Схема тела запроса тоже часть образа: она
# читается при старте, и сервис без неё не поднимается.
COPY services/scheduling/configs/static_config.yaml /etc/pdr/static_config.yaml
COPY services/scheduling/configs/config_vars.yaml /etc/pdr/config_vars.yaml
COPY services/scheduling/configs/dynamic_config_fallback.json \
     /etc/pdr/dynamic_config_fallback.json
COPY libs/pdr-identity/schemas/sign_in.json /etc/pdr/schemas/sign_in.json

# Процесс работает не от root: контейнер и так запускается с read_only и без
# добавочных возможностей, и последнее, чего ему не хватает, — своего
# пользователя.
RUN useradd --system --uid 10001 --no-create-home pdr
USER 10001

ENTRYPOINT ["/usr/local/bin/pdr_scheduling_service", \
            "--config", "/etc/pdr/static_config.yaml", \
            "--config_vars", "/etc/pdr/config_vars.yaml"]
