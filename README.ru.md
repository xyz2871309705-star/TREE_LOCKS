# TreeLocks — Распределённый Менеджер Блокировок Древовидной Структуры

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![Language](https://img.shields.io/badge/language-C11-green)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()

> Система **распределённых многоуровневых блокировок для древовидных структур**, обеспечивающая защиту согласованности, предотвращение взаимных блокировок и распределённую координацию для общих N-арных деревьев.

**GitHub**: https://github.com/xyz2871309705-star/TREE_LOCKS

🌐 **Языки**: [简体中文](README.md) | [English](README.en.md) | [日本語](README.ja.md) | [Français](README.fr.md) | **Русский**

---

## Содержание

- [Быстрый Старт](#быстрый-старт)
- [Структура Проекта](#структура-проекта)
- [Протокол Блокировок](#протокол-блокировок)
- [Обзор API](#обзор-api)
- [Описание Модулей](#описание-модулей)
- [Этапы Реализации](#этапы-реализации)
- [Тесты](#тесты)
- [Указатель Документации](#указатель-документации)
- [Литература](#литература)

---

## Быстрый Старт

### Требования

| Зависимость | Версия | Примечания |
|------|------|------|
| CMake | >= 3.16 | Система сборки |
| GCC / Clang / MSVC | Поддержка C11 | MinGW GCC 13.1 проверен |
| pthread | Системная | Потокобезопасность |
| Python | >= 3.6 (опционально) | Помощник clangd |

### Клонирование и Сборка

```bash
git clone git@github.com:xyz2871309705-star/TREE_LOCKS.git
cd TREE_LOCKS

# Linux / macOS / MinGW
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Windows MSVC
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

### Параметры CMake

| Параметр | По умолчанию | Описание |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Debug | Debug / Release / RelWithDebInfo / MinSizeRel |
| `TREELOCK_BUILD_SHARED` | OFF | ON=динамическая библиотека (.dll/.so), OFF=статическая (.a) |
| `TREELOCK_BUILD_EXAMPLES` | ON | Сборка примеров |
| `TREELOCK_BUILD_TESTS` | ON | Сборка тестов |

### Запуск Тестов

```bash
cd build
ctest --output-on-failure
# Или по отдельности
./tests/test_protocol
./tests/test_concurrent
```

### Запуск Примеров

```bash
./examples/basic_usage          # 4 базовых примера использования
./examples/log_callback_demo    # Демо регистрации обратного вызова логирования
./examples/tree_usage           # 3 примера управления древовидной структурой
```

---

## Структура Проекта

```
TREE_LOCKS/
├── CMakeLists.txt                  # Верхний CMake (агрегирует модули)
├── README.md                       # Этот файл
├── .gitignore
│
├── docs/                           # Документация
│   ├── 设计.md                     # Проектный документ
│   ├── DEVELOPER.md                # Руководство разработчика (подробное)
│   ├── ROADMAP.md                  # План итераций
│   ├── 树结构管理方案.md            # Предложение по древовидной структуре
│   └── tree-json-format.md         # Спецификация формата JSON для деревьев
│
├── modules/                        # ★ Модули (независимые include/ + src/)
│   ├── treelock_log/               # Модуль 0: Единое логирование [базовая зависимость]
│   │   ├── include/treelock_log.h      # API логирования (6 уровней + регистрация обратных вызовов)
│   │   └── src/log_core.c              # Потокобезопасность + защита от реентерабельности
│   │
│   ├── treelock_core/              # Модуль 1: Основная библиотека [Этап 1 ✅]
│   │   ├── include/
│   │   │   ├── treelock.h              # Публичное API
│   │   │   ├── treelock_types.h        # Обёртки типов (INT_32/IN/OUT и др.)
│   │   │   └── treelock_platform.h     # Абстракция платформы (время/DLL/TLS)
│   │   └── src/
│   │       ├── protocol.c              # Матрица совместимости, преобразование режимов, проверка протокола
│   │       ├── lock_table.c            # Таблица блокировок, очереди ожидания FIFO
│   │       └── client.c                # Реализация клиента (потокобезопасность + подсчёт ссылок)
│   │
│   ├── treelock_tree/              # Модуль 1.5: Управление древовидной структурой [Итерация 1.5 ✅]
│   │   ├── include/treelock_tree.h     # Публичное API (загрузка дерева/блокировка по пути/запросы)
│   │   └── src/
│   │       ├── tree_internal.h         # Внутренние структуры данных (хеш-таблица/индекс дерева)
│   │       ├── tree_core.c             # Управление узлами + операции хеш-таблицы
│   │       ├── tree_json.c             # Разбор JSON (плоский/вложенный двойной формат)
│   │       ├── tree_validate.c         # Проверка дерева (уникальность ID/один корень/без циклов)
│   │       ├── tree_path.c             # Разрешение путей + вывод блокировок предков
│   │       └── tree_api.c              # Реализация моста публичного API
│   │
│   ├── cjson/                      # Стороннее: cJSON v1.7.18 (Лицензия MIT)
│   │   ├── cJSON.h
│   │   └── cJSON.c
│   │
│   ├── treelock_comm/              # Модуль 2: Коммуникационный слой [Этап 2/3]
│   └── treelock_server/            # Модуль 3: Сервер [Этап 3]
│
├── cmake/                          # Помощники CMake
│   ├── CompilerWarnings.cmake          # Конфигурация предупреждений компилятора
│   └── expand_cc.py                    # clangd: развернуть @rsp файлы ответов
│
├── proto/
│   └── treelock.proto              # Определения сообщений Protobuf
│
├── tests/
│   ├── test_protocol.c             # Корректность протокола (12 тестов)
│   ├── test_concurrent.c           # Конкурентная нагрузка (3 сценария)
│   └── test_tree.c                 # Управление древовидной структурой (51 тест)
│
└── examples/
    ├── src/
    │   ├── basic_usage.c               # Базовое использование (4 примера)
    │   ├── log_callback_demo.c         # Регистрация обратного вызова логирования
    │   └── tree_usage.c                # Управление древовидной структурой (4 примера)
    └── json/
        ├── filesystem_tree.json        # Вложенный формат JSON определения дерева (9 узлов)
        └── filesystem_tree_flat.json   # Плоский формат JSON определения дерева (9 узлов)
```

---

## Протокол Блокировок

### Режимы Блокировок

| Режим | Символ | Значение |
|------|------|------|
| `TREELOCK_NL`  | NL  | Нет блокировки |
| `TREELOCK_IS`  | IS  | Намерение Совместного доступа — S-блокировки в поддереве |
| `TREELOCK_IX`  | IX  | Намерение Монопольного доступа — X-блокировки в поддереве |
| `TREELOCK_S`   | S   | Совместный — чтение всего поддерева |
| `TREELOCK_SIX` | SIX | S + IX — чтение поддерева, но эксклюзивная запись некоторых потомков |
| `TREELOCK_X`   | X   | Монопольный — эксклюзивный доступ ко всему поддереву |

### Матрица Совместимости

```
              NL   IS   IX    S   SIX   X
      NL  |   ✅   ✅   ✅   ✅   ✅   ✅
      IS  |   ✅   ✅   ✅   ✅   ✅   ❌
      IX  |   ✅   ✅   ✅   ❌   ❌   ❌
      S   |   ✅   ✅   ❌   ✅   ❌   ❌
      SIX |   ✅   ✅   ❌   ❌   ❌   ❌
      X   |   ✅   ❌   ❌   ❌   ❌   ❌
```

### Протокол Блокировок (4 Правила)

1. Получение **S / IS** → сначала нужно удерживать **IS** или сильнее на родителе
2. Получение **X / IX / SIX** → сначала нужно удерживать **IX** или сильнее на родителе
3. Снятие блокировок → сначала снять с потомков, затем с родителя (**снизу вверх**)
4. Корневой узел → нет родителя, можно блокировать напрямую

### Примеры Использования

```c
/* Блокировка по пути: чтение каталога + запись файла */
treelock_lock(tl, root, TREELOCK_IS);    // Корень: намерение совместного
treelock_lock(tl, dir,  TREELOCK_IS);    // Каталог: намерение совместного
treelock_lock(tl, file, TREELOCK_X);     // Файл: монопольный

/* ... выполнение операций записи ... */

treelock_unlock(tl, file);               // Снятие снизу вверх
treelock_unlock(tl, dir);
treelock_unlock(tl, root);
```

```c
/* Управление древовидной структурой: загрузка из JSON + блокировка по пути (★ новое) */
treelock_load_tree_from_file(tl, "my_tree.json");

// Блокировка одной строкой: авто root(IX) → /home(IX) → /home/alice(X)
treelock_lock_path(tl, "/home/alice", TREELOCK_X);

// ... безопасная запись данных alice ...

treelock_unlock_path(tl, "/home/alice");
```

---

## Обзор API

```c
/* ========== Жизненный Цикл ========== */
treelock_t *treelock_create (const treelock_config_t *config);
void        treelock_destroy(treelock_t *tl);

/* ========== Операции Блокировки ========== */
int treelock_lock    (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode);
int treelock_try_lock(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode, int timeout_ms);
int treelock_unlock  (treelock_t *tl, treelock_node_id_t node_id);
int treelock_unlock_all(treelock_t *tl);

/* ========== Повышение / Понижение ========== */
int treelock_escalate (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);
int treelock_downgrade(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);

/* ========== Запросы ========== */
treelock_mode_t treelock_get_mode  (treelock_t *tl, treelock_node_id_t node_id);
int             treelock_query_node(treelock_t *tl, treelock_node_id_t node_id, char **json_result);

/* ========== Обратные Вызовы ========== */
int treelock_set_lost_callback(treelock_t *tl, treelock_lost_cb cb, void *user_data);

/* ========== Управление Древовидной Структурой (libtreelock_tree) ========== */
int  treelock_load_tree_from_file(treelock_t *tl, const char *filepath);
int  treelock_load_tree_from_string(treelock_t *tl, const char *json_string);
int  treelock_register_node(treelock_t *tl, uint64_t node_id, uint64_t parent_id, const char *label);
int  treelock_lock_path(treelock_t *tl, const char *path, treelock_mode_t mode);
int  treelock_unlock_path(treelock_t *tl, const char *path);
int  treelock_get_parent(treelock_t *tl, uint64_t node_id, uint64_t *parent_id);
int  treelock_lookup_path(treelock_t *tl, const char *path, uint64_t *node_id);
```

---

## Описание Модулей

### treelock_log — Модуль Логирования

Единый интерфейс логирования, весь вывод в проекте должен проходить через этот модуль.

```c
#include "treelock_log.h"

// Макросы 6 уровней логирования
TREELOCK_LOG_FATAL("TAG", "msg: %s", detail);
TREELOCK_LOG_ERROR("TAG", "error: %d", code);
TREELOCK_LOG_WARN ("TAG", "warning");
TREELOCK_LOG_INFO ("TAG", "client created");
TREELOCK_LOG_DEBUG("TAG", "mode=%s", name);
TREELOCK_LOG_TRACE("TAG", "--> enter func()");

// Регистрация внешнего обратного вызова (интеграция с вашей системой логирования)
treelock_log_set_callback(my_logger, my_context);
treelock_log_set_level(TREELOCK_LOG_WARN);  // Фильтрация во время выполнения
```

### treelock_core — Основная Библиотека

Создаёт `libtreelock_core.a`, зависит от `treelock_log` + `pthread`.

Ключевые особенности:
- **Потокобезопасность**: Мьютекс на узел + глобальная блокировка таблицы
- **Подсчёт ссылок**: Реентерабельные блокировки для одного клиента, освобождение при ref_count = 0
- **Пробуждение FIFO**: Условная переменная + проверка совместимости, без голодания ожидающих
- **Повышение на месте**: escalate/downgrade работают напрямую с таблицей, избегая дублирующих записей

---

## Этапы Реализации

| Этап | Статус | Содержание | Строк Кода |
|------|------|------|--------|
| Этап 1 | 🚧 80% | Автономная библиотека: протокол + таблица блокировок + клиент + логирование + древовидная структура + кроссплатформенность | ~3500 строк |
| Этап 2 | 📋 План | ZK-координация: распределённая координация блокировок + Watch обратные вызовы | +~2500 строк |
| Этап 3 | 📋 План | Собственный сервис: Raft + gRPC + управление арендой | +~6000 строк |

### Прогресс Этапа 1

- [x] 6 режимов блокировок + полная матрица совместимости
- [x] Проверка путей повышения/понижения блокировок
- [x] Очереди ожидания FIFO + пробуждение по условной переменной
- [x] Реентерабельные блокировки одного клиента (подсчёт ссылок)
- [x] Потокобезопасность (pthread mutex)
- [x] Единый модуль логирования (6 уровней + внешние обратные вызовы)
- [x] Кроссплатформенная сборка (Windows/Linux/macOS)
- [x] 12 тестов протокола + 3 теста конкурентности
- [x] **Управление древовидной структурой** (загрузка JSON + блокировка по пути + автоматическая проверка протокола) ← **Итерация 1.5 завершена**
- [ ] Аренда и восстановление после сбоев
- [ ] Бенчмарки производительности и детектор памяти

---

## Тесты

| Тест | Случаев | Фреймворк | Команда |
|------|--------|------|------|
| Корректность Протокола | 12 | Google Test | `./build/tests/test_protocol` |
| Модуль Логирования | 12 | Google Test | `./build/tests/test_log` |
| Конкурентная Нагрузка | 3 | Google Test | `./build/tests/test_concurrent` |
| Древовидная Структура | 9 | Google Test | `./build/tests/test_tree` |

> **Тестовый фреймворк**: Google Test v1.15.2 (C++), через локальную копию в `_deps/googletest/`, интегрирован CMake `FetchContent`.

```
$ ctest --output-on-failure
100% tests passed, 0 tests failed out of 4

$ ./tests/test_protocol
[==========] 12 tests from 5 test suites ran. (0 ms total)
[  PASSED  ] 12 tests.
```

---

## Указатель Документации

| Документ | Описание |
|------|------|
| [设计.md](docs/设计.md) | Полное проектное обоснование, вывод протокола, обзор архитектуры |
| [DEVELOPER.md](docs/DEVELOPER.md) | Руководство разработчика: введение, архитектура, соглашения по кодированию, детали модулей, FAQ |
| [ROADMAP.md](docs/ROADMAP.md) | План итераций: 14 итераций, оценка трудозатрат, приоритеты, критерии приёмки |
| [树结构管理方案.md](docs/树结构管理方案.md) | Предложение по древовидной структуре: анализ осуществимости, проектные решения, запись реализации |
| [tree-json-format.md](docs/tree-json-format.md) | Спецификация формата JSON: определения полей, два формата, правила проверки, полные примеры |

---

## Литература

- Gray, J. et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*
- Mohan, C. et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*
- Ongaro, D. & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm (Raft)*
