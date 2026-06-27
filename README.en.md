# TreeLocks — Distributed Multi-Granularity Tree Lock Manager

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![Language](https://img.shields.io/badge/language-C11-green)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()

> A **distributed multi-granularity tree-lock** system providing consistency protection, deadlock prevention, and distributed coordination for general N-ary tree structures.

**GitHub**: https://github.com/xyz2871309705-star/TREE_LOCKS

🌐 **Languages**: [简体中文](README.md) | **English** | [日本語](README.ja.md) | [Français](README.fr.md) | [Русский](README.ru.md)

---

## Table of Contents

- [Quick Start](#quick-start)
- [Project Structure](#project-structure)
- [Lock Protocol](#lock-protocol)
- [API Overview](#api-overview)
- [Module Descriptions](#module-descriptions)
- [Implementation Phases](#implementation-phases)
- [Tests](#tests)
- [Documentation Index](#documentation-index)
- [References](#references)

---

## Quick Start

### Requirements

| Dependency | Version | Notes |
|------|------|------|
| CMake | >= 3.16 | Build system |
| GCC / Clang / MSVC | C11-capable | MinGW GCC 13.1 verified |
| pthread | System-provided | Thread safety |
| Python | >= 3.6 (optional) | clangd helper |

### Clone & Build

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

### CMake Options

| Option | Default | Description |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Debug | Debug / Release / RelWithDebInfo / MinSizeRel |
| `TREELOCK_BUILD_SHARED` | OFF | ON=shared library (.dll/.so), OFF=static library (.a) |
| `TREELOCK_BUILD_EXAMPLES` | ON | Build example programs |
| `TREELOCK_BUILD_TESTS` | ON | Build test programs |

### Run Tests

```bash
cd build
ctest --output-on-failure
# Or individually
./tests/test_protocol
./tests/test_concurrent
```

### Run Examples

```bash
./examples/basic_usage          # 4 basic usage examples
./examples/log_callback_demo    # Log callback registration demo
./examples/tree_usage           # 3 tree structure management examples
```

---

## Project Structure

```
TREE_LOCKS/
├── CMakeLists.txt                  # Top-level CMake (aggregates modules)
├── README.md                       # This file
├── .gitignore
│
├── docs/                           # Documentation
│   ├── 设计.md                     # Design doc
│   ├── DEVELOPER.md                # Developer guide (detailed)
│   ├── ROADMAP.md                  # Iteration roadmap
│   ├── 树结构管理方案.md            # Tree structure proposal
│   └── tree-json-format.md         # JSON tree definition format spec
│
├── modules/                        # ★ Modules (independent include/ + src/)
│   ├── treelock_log/               # Module 0: Unified logging [foundational dependency]
│   │   ├── include/treelock_log.h      # Log API (6 levels + callback registration)
│   │   └── src/log_core.c              # Thread-safe + reentrancy protection
│   │
│   ├── treelock_core/              # Module 1: Core library [Phase 1 ✅]
│   │   ├── include/
│   │   │   ├── treelock.h              # Public API
│   │   │   ├── treelock_types.h        # Type wrappers (INT_32/IN/OUT etc.)
│   │   │   └── treelock_platform.h     # Platform abstraction (time/DLL/TLS)
│   │   └── src/
│   │       ├── protocol.c              # Compatibility matrix, mode conversion, protocol validation
│   │       ├── lock_table.c            # Lock table, FIFO wait queues
│   │       └── client.c                # Client implementation (thread-safe + ref-counting)
│   │
│   ├── treelock_tree/              # Module 1.5: Tree structure management [Iteration 1.5 ✅]
│   │   ├── include/treelock_tree.h     # Public API (tree loading/path locking/query)
│   │   └── src/
│   │       ├── tree_internal.h         # Internal data structures (hash table/tree index)
│   │       ├── tree_core.c             # Tree node management + hash table operations
│   │       ├── tree_json.c             # JSON parsing (flat/nested dual format)
│   │       ├── tree_validate.c         # Tree validation (unique IDs/single root/no cycles)
│   │       ├── tree_path.c             # Path resolution + ancestor lock derivation
│   │       └── tree_api.c              # Public API bridge implementation
│   │
│   ├── cjson/                      # Third-party: cJSON v1.7.18 (MIT License)
│   │   ├── cJSON.h
│   │   └── cJSON.c
│   │
│   ├── treelock_comm/              # Module 2: Communication layer [Phase 2/3]
│   └── treelock_server/            # Module 3: Server [Phase 3]
│
├── cmake/                          # CMake helpers
│   ├── CompilerWarnings.cmake          # Compiler warning configuration
│   └── expand_cc.py                    # clangd: expand @rsp response files
│
├── proto/
│   └── treelock.proto              # Protobuf message definitions
│
├── tests/
│   ├── test_protocol.c             # Protocol correctness (12 test cases)
│   ├── test_concurrent.c           # Concurrent stress (3 scenarios)
│   └── test_tree.c                 # Tree structure management (51 test cases)
│
└── examples/
    ├── src/
    │   ├── basic_usage.c               # Basic usage (4 examples)
    │   ├── log_callback_demo.c         # Log callback registration
    │   └── tree_usage.c                # Tree structure management (4 examples)
    └── json/
        ├── filesystem_tree.json        # Nested format JSON tree definition (9 nodes)
        └── filesystem_tree_flat.json   # Flat format JSON tree definition (9 nodes)
```

---

## Lock Protocol

### Lock Modes

| Mode | Symbol | Meaning |
|------|------|------|
| `TREELOCK_NL`  | NL  | No lock |
| `TREELOCK_IS`  | IS  | Intention Shared — S locks exist in subtree |
| `TREELOCK_IX`  | IX  | Intention eXclusive — X locks exist in subtree |
| `TREELOCK_S`   | S   | Shared — read entire subtree |
| `TREELOCK_SIX` | SIX | S + IX — read subtree but some child is exclusively written |
| `TREELOCK_X`   | X   | eXclusive — exclusive access to entire subtree |

### Compatibility Matrix

```
              NL   IS   IX    S   SIX   X
      NL  |   ✅   ✅   ✅   ✅   ✅   ✅
      IS  |   ✅   ✅   ✅   ✅   ✅   ❌
      IX  |   ✅   ✅   ✅   ❌   ❌   ❌
      S   |   ✅   ✅   ❌   ✅   ❌   ❌
      SIX |   ✅   ✅   ❌   ❌   ❌   ❌
      X   |   ✅   ❌   ❌   ❌   ❌   ❌
```

### Locking Protocol (4 Rules)

1. Acquiring **S / IS** → must first hold **IS** or stronger on parent
2. Acquiring **X / IX / SIX** → must first hold **IX** or stronger on parent
3. Releasing locks → must release children first, then parent (**bottom-up**)
4. Root node → no parent, can be locked directly

### Usage Examples

```c
/* Path locking: read directory + write file */
treelock_lock(tl, root, TREELOCK_IS);    // Root: intention shared
treelock_lock(tl, dir,  TREELOCK_IS);    // Directory: intention shared
treelock_lock(tl, file, TREELOCK_X);     // File: exclusive

/* ... perform write operations ... */

treelock_unlock(tl, file);               // Release bottom-up
treelock_unlock(tl, dir);
treelock_unlock(tl, root);
```

```c
/* Tree structure management: load tree from JSON + path locking (★ new) */
treelock_load_tree_from_file(tl, "my_tree.json");

// One-line lock: auto root(IX) → /home(IX) → /home/alice(X)
treelock_lock_path(tl, "/home/alice", TREELOCK_X);

// ... safely write alice's data ...

treelock_unlock_path(tl, "/home/alice");
```

---

## API Overview

```c
/* ========== Lifecycle ========== */
treelock_t *treelock_create (const treelock_config_t *config);
void        treelock_destroy(treelock_t *tl);

/* ========== Lock Operations ========== */
int treelock_lock    (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode);
int treelock_try_lock(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode, int timeout_ms);
int treelock_unlock  (treelock_t *tl, treelock_node_id_t node_id);
int treelock_unlock_all(treelock_t *tl);

/* ========== Escalate / Downgrade ========== */
int treelock_escalate (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);
int treelock_downgrade(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);

/* ========== Query ========== */
treelock_mode_t treelock_get_mode  (treelock_t *tl, treelock_node_id_t node_id);
int             treelock_query_node(treelock_t *tl, treelock_node_id_t node_id, char **json_result);

/* ========== Callbacks ========== */
int treelock_set_lost_callback(treelock_t *tl, treelock_lost_cb cb, void *user_data);

/* ========== Tree Structure Management (libtreelock_tree) ========== */
int  treelock_load_tree_from_file(treelock_t *tl, const char *filepath);
int  treelock_load_tree_from_string(treelock_t *tl, const char *json_string);
int  treelock_register_node(treelock_t *tl, uint64_t node_id, uint64_t parent_id, const char *label);
int  treelock_lock_path(treelock_t *tl, const char *path, treelock_mode_t mode);
int  treelock_unlock_path(treelock_t *tl, const char *path);
int  treelock_get_parent(treelock_t *tl, uint64_t node_id, uint64_t *parent_id);
int  treelock_lookup_path(treelock_t *tl, const char *path, uint64_t *node_id);
```

---

## Module Descriptions

### treelock_log — Logging Module

Unified logging interface; all output in the project must go through this module.

```c
#include "treelock_log.h"

// 6 log level macros
TREELOCK_LOG_FATAL("TAG", "msg: %s", detail);
TREELOCK_LOG_ERROR("TAG", "error: %d", code);
TREELOCK_LOG_WARN ("TAG", "warning");
TREELOCK_LOG_INFO ("TAG", "client created");
TREELOCK_LOG_DEBUG("TAG", "mode=%s", name);
TREELOCK_LOG_TRACE("TAG", "--> enter func()");

// External callback registration (integrate with your own logging system)
treelock_log_set_callback(my_logger, my_context);
treelock_log_set_level(TREELOCK_LOG_WARN);  // Runtime filtering
```

### treelock_core — Core Library

Produces `libtreelock_core.a`, dependent on `treelock_log` + `pthread`.

Key features:
- **Thread-safe**: Per-node mutex + global lock table lock
- **Reference counting**: Reentrant locking for the same client, released when ref_count reaches zero
- **FIFO wake-up**: Condition variable + compatibility check, no waiter starvation
- **In-place upgrade**: escalate/downgrade operate directly on the lock table, avoiding duplicate entries

---

## Implementation Phases

| Phase | Status | Contents | LOC |
|------|------|------|--------|
| Phase 1 | 🚧 80% | Standalone library: protocol + lock table + client + logging + tree structure + cross-platform | ~3500 lines |
| Phase 2 | 📋 Planned | ZK-coordinated: distributed lock coordination + Watch callbacks | +~2500 lines |
| Phase 3 | 📋 Planned | Self-developed service: Raft + gRPC + lease management | +~6000 lines |

### Phase 1 Progress

- [x] 6 lock modes + complete compatibility matrix
- [x] Lock escalate/downgrade path validation
- [x] FIFO wait queues + condition variable wake-up
- [x] Same-client reentrant locking (reference counting)
- [x] Thread safety (pthread mutex)
- [x] Unified logging module (6 levels + external callbacks)
- [x] Cross-platform build (Windows/Linux/macOS)
- [x] 12 protocol tests + 3 concurrency tests
- [x] **Tree structure management** (JSON loading + path locking + automatic protocol validation) ← **Iteration 1.5 complete**
- [ ] Lease & failure recovery
- [ ] Performance benchmarks & memory detection

---

## Tests

| Test | Cases | Framework | Command |
|------|--------|------|------|
| Protocol Correctness | 12 | Google Test | `./build/tests/test_protocol` |
| Logging Module | 12 | Google Test | `./build/tests/test_log` |
| Concurrency Stress | 3 | Google Test | `./build/tests/test_concurrent` |
| Tree Structure | 9 | Google Test | `./build/tests/test_tree` |

> **Test framework**: Google Test v1.15.2 (C++), via local copy in `_deps/googletest/`, integrated by CMake `FetchContent`.

```
$ ctest --output-on-failure
100% tests passed, 0 tests failed out of 4

$ ./tests/test_protocol
[==========] 12 tests from 5 test suites ran. (0 ms total)
[  PASSED  ] 12 tests.
```

---

## Documentation Index

| Document | Description |
|------|------|
| [设计.md](docs/设计.md) | Full design rationale, protocol derivation, architecture overview |
| [DEVELOPER.md](docs/DEVELOPER.md) | Developer guide: onboarding, architecture, coding conventions, module details, FAQ |
| [ROADMAP.md](docs/ROADMAP.md) | Iteration roadmap: 14 iterations, estimated effort, priorities, acceptance criteria |
| [树结构管理方案.md](docs/树结构管理方案.md) | Tree structure proposal: feasibility analysis, design decisions, implementation record |
| [tree-json-format.md](docs/tree-json-format.md) | JSON format spec: field definitions, two formats, validation rules, complete examples |

---

## References

- Gray, J. et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*
- Mohan, C. et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*
- Ongaro, D. & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm (Raft)*
