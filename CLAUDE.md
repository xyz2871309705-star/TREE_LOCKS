# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure and build (Debug)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Windows: if using CLion's MinGW has libssp-0.dll issues, switch to WinLibs toolchain:
# MINGW="/c/Users/28713/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64"
# export PATH="$MINGW/bin:$PATH"

# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run all tests
cd build && ctest --output-on-failure

# Run a single test
./build/tests/test_protocol
./build/tests/test_concurrent
./build/tests/test_tree

# Run examples
./build/examples/basic_usage
./build/examples/log_callback_demo
./build/examples/tree_usage
```

### CMake Options

| Option | Default | Purpose |
|--------|---------|---------|
| `CMAKE_BUILD_TYPE` | Debug | `Debug` / `Release` / `RelWithDebInfo` / `MinSizeRel` |
| `TREELOCK_BUILD_SHARED` | OFF | Build shared library (`.dll`/`.so`) instead of static (`.a`) |
| `TREELOCK_BUILD_EXAMPLES` | ON | Build example programs |
| `TREELOCK_BUILD_TESTS` | ON | Build test programs |

## Architecture

TreeLocks is a **distributed multi-granularity tree-lock manager** implementing the classic Gray 6-mode locking protocol (NL/IS/IX/S/SIX/X) for N-ary tree structures. Currently in Phase 1 (~80% complete): single-machine library with lock protocol, client API, logging, and tree-structure management.

### Module Dependency Graph

```
treelock_log (zero external deps: C stdlib + pthread)
    |
    ├──→ treelock_core ──→ treelock_tree
    │         (locks)        (JSON tree loading + path-based locking)
    │              \          /  depends on cjson (third-party, vendored)
    │               \        /
    └──→ cjson ─────→ treelock_tree
```

- **`treelock_log`** — Mandatory logging layer. All output must use `TREELOCK_LOG_*` macros; `printf`/`fprintf` are forbidden outside of the log module itself.
- **`treelock_core`** — Lock protocol engine: compatibility matrix, lock table (linked list indexed by `node_id`), per-node mutex + condition variable FIFO wait queues, client-side reference counting for re-entrant locks.
- **`treelock_tree`** — Tree structure management (Iteration 1.5): loads tree definitions from JSON (flat or nested format), builds an internal hash table (256-bucket chained), validates tree integrity (cycle detection, single-root, parent validity), resolves path strings to node IDs, and auto-derives ancestor intention locks (`lock_path("/a/b/c", X)` acquires `IX` on root, `IX` on `/a`, `IX` on `/a/b`, `X` on `/a/b/c`).
- **`treelock_comm` / `treelock_server`** — Placeholders for Phases 2/3 (distributed coordination).

### Internal Layering (treelock_core)

```
API layer    → treelock.h        (lock/unlock/try_lock/escalate/downgrade/query)
Protocol     → protocol.c        (compatibility matrix, mode conversion, upgrade paths)
Lock table   → lock_table.c      (grant lists, FIFO wait queues, CV-based wake)
Client       → client.c          (thread-safe wrapper, ref-counting, protocol validation)
Platform     → treelock_platform.h (time, DLL export, TLS, cross-platform sleep)
```

The tree module injects a parent-lookup callback (`tree_get_parent`) into `treelock_s` so that protocol validation (`treelock_validate_protocol()`) can enforce the "lock parent before child" rules automatically during `_do_lock_core()`.

## Coding Conventions (Mandatory)

### Types: never use raw C types

All code must use the wrapper macros defined in `treelock_types.h`:

| Raw type | Use instead |
|----------|-------------|
| `int32_t` | `INT_32` |
| `uint64_t` | `UINT_64` |
| `char` | `CHAR` |
| `void*` | `PTR_VOID` |
| `const char*` | `CSTR_PTR` |
| `int` return / bool | `RET_CODE` / `BOOL` |
| `uint64_t` node ID | `TREE_NODE_ID` |

### Parameter annotations: `IN`, `OUT`, `INOUT`

Every function parameter in both declarations and doc comments must use one of these direction macros — they expand to nothing, serving as documentation.

### Function header comments

Every function (public, internal static, or test helper) must have a doc block:

```c
/**
 * 函数名称：function_name
 *
 * 功能描述：What this function does.
 *
 * @param[IN]    param_name - description
 * @param[OUT]   param_name - description
 * @param[INOUT] param_name - description
 *
 * @return return value description
 * @retval VALUE - specific meaning
 */
```

### Naming conventions

- Public functions: `treelock_<verb>_<noun>` (e.g., `treelock_lock`, `treelock_get_mode`)
- Module-internal functions: `_<verb>_<noun>` (e.g., `_do_lock_core`, `_remove_held_lock`)
- Static globals: `g_<name>` (e.g., `g_log_ctx`)
- Macros: `TREELOCK_<MODULE>_<DESC>` (e.g., `TREELOCK_GRANT_INIT_CAP`)

### Logging

Never use `printf`/`fprintf`. Use:
```c
TREELOCK_LOG_INFO("TAG", "message: %s", var);
TREELOCK_LOG_ERROR("TAG", "failed: %d", code);
```

### Compiler flags

- `-Wall -Wextra -Wshadow -Wconversion` on GCC/Clang (no `-Wpedantic` — log macros rely on `##__VA_ARGS__`)
- C11 with GNU extensions (`CMAKE_C_EXTENSIONS ON`)
- Must compile without warnings on GCC 13+/16+

## Test Framework

A lightweight inline framework (no external test library). Each test binary is standalone:

```c
test_begin("description of the test");
if (condition) {
    test_pass("optional detail");
} else {
    test_fail("reason for failure");
}
```

Tests link against module static libs and are granted access to `src/` private headers via `target_include_directories`. Test JSON fixture files are copied to the build directory at configure time.

## Key Design Decisions

- **Tree index is injected, not embedded**: The `treelock_tree` module stores its hash table in `treelock_s.tree_data` (opaque `void*`) and registers a callback. This avoids a hard circular dependency between `treelock_core` and `treelock_tree`.
- **Backward compatible**: When no tree is loaded, protocol validation skips parent checks — existing Phase 1 code continues to work.
- **No `<windows.h>`**: The platform layer uses C runtime headers (`sys/timeb.h`) to avoid type-name collisions with the project's type macros.
