# Слои

**Зависимости направлены только внутрь: `core` не знает никого, `application`
знает `core`, `infrastructure` знает обоих.**

```mermaid
flowchart RL
    subgraph infrastructure["infrastructure — адаптеры"]
        component["TariffRepositoryComponent<br/>тонкий компонент userver"]
        adapter["PostgresTariffRepository<br/>обычный класс"]
    end

    subgraph testing["pdr-testing — только в тестах"]
        fakes["FakeClock · FakeIdGenerator"]
    end

    subgraph application["application — сценарии и порты"]
        usecase["QuoteLessonPackage<br/>класс на сценарий"]
        port["порты: TariffRepository<br/>Clock · IdGenerator"]
    end

    subgraph core["core — домен"]
        domain["Money · Tariff · правило цены пакета<br/>StrongId&lt;Tag&gt; · Instant · TimeZone<br/>Error · Result"]
    end

    component --> adapter
    adapter -- "реализует" --> port
    fakes -- "реализуют" --> port
    usecase --> port
    usecase --> domain
    port --> domain
```

Стрелка — «знает о». Наружу не идёт ни одна.

## Что кому запрещено

| Слой | Не может включать |
| --- | --- |
| `core/` | `userver`, `pqxx`/`libpq`, `<ctime>`/`<time.h>`, `application/`, `infrastructure/` |
| `application/` | `userver`, `pqxx`/`libpq`, `<ctime>`/`<time.h>`, `infrastructure/` |
| `infrastructure/` | — внешний слой, ему можно всё |

Там же, в `core/` и `application/`, запрещён и сам вызов «который час»:
`system_clock::now()`, `gettimeofday`, `std::time(nullptr)`. «Сейчас» приходит
портом `Clock`, а новый идентификатор — портом `IdGenerator`. Тип и правило
принадлежат домену (`core/types/ids.hpp`, `core/types/time.hpp`), а получение
значения снаружи — порту: это ровно та граница, ради которой всё остальное.

Порт объявляет `application`, реализует `infrastructure`. Это и есть разворот
зависимости: сценарий формулирует, что ему нужно, а не подстраивается под то,
что умеет драйвер базы. Порты узкие — `TariffRepository` с одним вопросом, а не
`Repository` с двадцатью методами, потому что фейк узкого порта пишется в
четыре строки, а фейк широкого не пишется никогда.

## Чем это обеспечено

Правило проверяется двумя способами, и оба ломают сборку, а не портят настроение:

* `scripts/check_layers.py` разбирает директивы `#include` и ищет обращения к
  системному времени (комментарии и литералы вырезаются, эвристик по именам
  файлов нет), печатает нарушение как `файл:строка`. Ненулевой код возврата.
  Отрицательные случаи проверяются самой проверкой:
  `python3 scripts/check_layers.py --selftest`.
* `libs/pdr-core/CMakeLists.txt` в конце конфигурации читает зависимости цели
  `pdr_core` и падает с `FATAL_ERROR`, если там появился `userver`, `pqxx` или
  `PostgreSQL`. Прилинковать userver к домену не «не принято» — это не
  собирается.

Оба шага гоняются в `.github/workflows/architecture.yml`, там же `pdr_core` и
`pdr_application` собираются в окружении, где userver вообще нет.

## Почему так строго

На предыдущем проекте на этом же стеке репозиторий был объявлен как
`final : ComponentBase` — конкретный класс, сросшийся с userver. Следствие: ни
одного изолированного unit-теста бэкенда, сто процентов тестов гоняли реальный
бинарник и реальный Postgres, а значит, минуты вместо секунд и «проверим на
стенде» вместо «проверим сразу».

Здесь адаптер — обычный класс с обычным конструктором, а компонент userver
только создаёт его и отдаёт ссылку на порт. Сценарий подставляет фейк порта и
проверяется без базы, без докера и без сети.

Фейки часов и генератора идентификаторов лежат в `libs/pdr-testing` в
единственном экземпляре на весь проект: свой фейк часов в каждом тестовом файле
— это пять разных представлений о том, что такое «сейчас».
