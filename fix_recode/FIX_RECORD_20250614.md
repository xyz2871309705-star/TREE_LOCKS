# 代码风险修复记录

**日期**: 2026-06-14  
**分支**: dev  
**修复范围**: 6 个风险项（3 高 + 3 中），涉及 3 个源文件 + 1 个测试文件

---

## 修复 1 (🔴 高风险): `realloc` 包含 `pthread_cond_t` 的数组 — 未定义行为

**影响文件**: `modules/treelock_core/src/internal.h`, `lock_table.c`, `client.c`  
**问题**: `wait_queue` 是 `treelock_wait_entry_t` 结构体数组，`realloc` 扩容时可能移动内存，对已有的 `pthread_cond_t` 做 bitwise copy → POSIX UB。

### 修复方案

将 `wait_queue` 从**结构体数组**改为**指针数组**，每个条目独立 `malloc`，`realloc` 只移动指针（安全）。

### 代码对比

#### `internal.h:150` — 类型变更

```diff
-    treelock_wait_entry_t  *wait_queue;      /**< 等待队列（动态数组）       */
+    treelock_wait_entry_t **wait_queue;      /**< 等待队列（指针数组，避免 realloc 拷贝 pthread_cond_t） */
```

#### `lock_table.c:340-377` — `add_waiter`: 独立分配条目

```diff
-    /* 扩展队列 */
+    /* 扩展队列（指针数组，realloc 只移动指针，不拷贝 pthread_cond_t） */
     if (node->wait_count >= node->wait_capacity) {
-        treelock_wait_entry_t *new_queue;
+        treelock_wait_entry_t **new_queue;
         ...
-        new_queue = (treelock_wait_entry_t *)realloc(
-            node->wait_queue,
-            (size_t)(new_cap * sizeof(treelock_wait_entry_t)));
+        new_queue = (treelock_wait_entry_t **)realloc(
+            node->wait_queue,
+            (size_t)(new_cap * sizeof(treelock_wait_entry_t *)));
         ...
     }

-    /* 添加等待者 */
+    /* 分配等待者条目并加入队列 */
     {
-        treelock_wait_entry_t *entry = &node->wait_queue[node->wait_count];
+        treelock_wait_entry_t *entry;
+
+        entry = (treelock_wait_entry_t *)malloc(sizeof(treelock_wait_entry_t));
+        if (entry == NULL) { ... return TREELOCK_ERR_INVAL; }
+
         ...
-        if (pthread_cond_init(&entry->cond, NULL) != 0) { ... }
+        if (pthread_cond_init(&entry->cond, NULL) != 0) {
+            free(entry);  // ← 新增：cond 初始化失败时释放 entry
+            return TREELOCK_ERR_INVAL;
+        }
+
+        node->wait_queue[node->wait_count] = entry;
     }
```

#### `lock_table.c:423-468` — `wake_waiters`: 简化 swap-remove

```diff
     for (i = 0; i < node->wait_count; /* 循环内更新 */) {
-        treelock_wait_entry_t *entry = &node->wait_queue[i];
+        treelock_wait_entry_t *entry = node->wait_queue[i];
         ...
         pthread_cond_signal(&entry->cond);
-        /* swap-remove: 销毁末尾条目的 cond 后逐字段拷贝 */
+        /* swap-remove：移动最后一个指针到当前位置 */
         if (i < node->wait_count - 1) {
-            pthread_cond_destroy(
-                &node->wait_queue[node->wait_count - 1].cond);
-            memcpy(node->wait_queue[i].client_id, ...);
-            node->wait_queue[i].requested_mode = ...;
-            node->wait_queue[i].enqueue_time = ...;
+            node->wait_queue[i] = node->wait_queue[node->wait_count - 1];
         }
         node->wait_count--;
```

#### `client.c:343-436` — `_do_lock_core`: 超时/唤醒路径适配指针

```diff
-    entry = &node->wait_queue[node->wait_count - 1];
+    entry = node->wait_queue[node->wait_count - 1];

     /* 超时路径 */
-    for (j = 0; j < node->wait_count; j++) {
-        if (&node->wait_queue[j] == entry) {
+    for (j = 0; j < node->wait_count; j++) {
+        if (node->wait_queue[j] == entry) {
             pthread_cond_destroy(&entry->cond);
-            /* swap-remove：销毁末尾 cond 后逐字段拷贝 */
+            free(entry);  // ← 新增
             if (j < node->wait_count - 1) {
-                pthread_cond_destroy(...);
-                memcpy(...);
-                node->wait_queue[j].requested_mode = ...;
-                node->wait_queue[j].enqueue_time = ...;
+                node->wait_queue[j] = node->wait_queue[node->wait_count - 1];
             }
             node->wait_count--;
             break;
         }
     }

     /* 唤醒后 stale 路径 */
     if (!granted) {
+        /* 从队列中移除自身 */
+        for (j = 0; j < node->wait_count; j++) {
+            if (node->wait_queue[j] == entry) {
+                if (j < node->wait_count - 1)
+                    node->wait_queue[j] = node->wait_queue[node->wait_count - 1];
+                node->wait_count--;
+                break;
+            }
+        }
         pthread_cond_destroy(&entry->cond);
+        free(entry);  // ← 新增
         ...
     }
     /* 成功获取 */
     pthread_cond_destroy(&entry->cond);
+    free(entry);  // ← 新增
```

#### `client.c:711-716` — `treelock_destroy`: 清理适配指针

```diff
     for (i = 0; i < cleanup_node->wait_count; i++) {
-        pthread_cond_destroy(&cleanup_node->wait_queue[i].cond);
+        pthread_cond_destroy(&cleanup_node->wait_queue[i]->cond);
+        free(cleanup_node->wait_queue[i]);  // ← 新增
     }
```

---

## 修复 2 (🔴 高风险): `pthread_cond_destroy` 对未初始化的 condvar

**影响文件**: `modules/treelock_core/src/client.c:692-695`  
**问题**: `treelock_destroy` 的清理循环上限是 `wait_capacity`（数组容量），但只有 `wait_count` 个槽位真正初始化了 cond，超出部分为垃圾数据。

### 代码对比

```diff
-            /* 销毁等待队列中的所有条件变量（含 swap-remove 残留的） */
-            for (i = 0; i < cleanup_node->wait_capacity; i++) {
-                pthread_cond_destroy(
-                    &cleanup_node->wait_queue[i].cond);
+            /* 销毁等待队列中已初始化的条件变量（仅 wait_count 个已使用） */
+            for (i = 0; i < cleanup_node->wait_count; i++) {
+                pthread_cond_destroy(
+                    &cleanup_node->wait_queue[i].cond);
```

> **注意**: 此修复后续被修复 1 覆盖（改为指针数组后，循环内增加了 `free(entry)`）。

---

## 修复 3 (🔴 高风险): `treelock_escalate` 缺少协议校验

**影响文件**: `modules/treelock_core/src/client.c:987-1000`  
**问题**: `treelock_escalate` 不调用 `treelock_validate_protocol()`，子节点从 IS 升级到 X 时不会检查父节点是否有足够的锁。

### 代码对比

```diff
     if (!treelock_escalate_valid(old_mode, new_mode)) {
         return TREELOCK_ERR_PROTOCOL;
     }

+    /*
+     * 协议校验（树结构管理）：
+     * 升级可能改变子节点锁模式，从而改变对父节点锁的要求。
+     * 例如 IS→X：子节点 IS 只需父节点 IS，但子节点 X 需要父节点 IX。
+     */
+    rc = treelock_validate_protocol(tl, node_id, new_mode);
+    if (rc != TREELOCK_OK) {
+        return rc;
+    }
+
     cid = (tl->config.client_id != NULL) ? tl->config.client_id : "local";
```

### 测试影响

原有测试 `MemoryTest.RepeatedCreateLockDestroy100` 在 `lock_path("/c", S)`（根节点仅 IS）后直接 `escalate(4, SIX)`，违反协议。修复后将父节点先升级为 IX：

```diff
     EXPECT_EQ(treelock_lock_path(tl, "/c", TREELOCK_S), TREELOCK_OK);
+    /* escalate 目标节点前需先升级根节点（S→SIX 要求父节点持有 IX 或更强） */
+    EXPECT_EQ(treelock_escalate(tl, 1, TREELOCK_IX), TREELOCK_OK);
     /* escalate + downgrade on target node */
     EXPECT_EQ(treelock_escalate(tl, 4, TREELOCK_SIX), TREELOCK_OK);
```

---

## 修复 4 (🟠 中等风险): `treelock_unlock` 与 `escalate` 的 TOCTOU 竞争

**影响文件**: `modules/treelock_core/src/client.c:873-891`  
**问题**: unlock 读取 `held_mode` 后释放 `held_mutex`，escalate 在此期间修改了锁表和 held entry 的 mode。unlock 的 `release_lock(old_mode)` 找不到 grant 后，无条件 `_remove_held_lock` 会错误地删除 escalated 后的 held entry。

### 代码对比

```diff
     if (rc == TREELOCK_OK) {
         _remove_held_lock(tl, node_id);
         ...
     } else {
-        /*
-         * grant 未找到：可能是并发线程已经代为释放（竞争窗口）。
-         * 仍须清理 held entry，否则残留的悬空引用会导致后续
-         * _get_held_mode 误判为仍持有锁。
-         */
-        TREELOCK_LOG_DEBUG("CORE",
-            "lock already released by concurrent thread: ...");
-        _remove_held_lock(tl, node_id);
-        rc = TREELOCK_OK;
+        /*
+         * grant 未找到：可能是并发 escalate 已经代为释放了旧模式。
+         * 必须重新检查 held entry 的模式是否已变更：
+         * - 若 mode 已变（escalate 更新了 held entry）→ 不删除，返回 OK
+         * - 若 mode 未变（其他原因）→ 清理悬空 held entry
+         */
+        BOOL should_remove = TRUE;
+        pthread_mutex_lock(&tl->held_mutex);
+        {
+            treelock_held_lock_t *held = _find_held_lock(tl, node_id);
+            if (held != NULL && held->mode != held_mode) {
+                should_remove = FALSE;
+                TREELOCK_LOG_DEBUG("CORE",
+                    "lock mode changed by escalate: ...");
+            }
+        }
+        pthread_mutex_unlock(&tl->held_mutex);
+
+        if (should_remove) {
+            TREELOCK_LOG_DEBUG("CORE",
+                "lock already released by concurrent thread: ...");
+            _remove_held_lock(tl, node_id);
+        }
+        rc = TREELOCK_OK;
     }
```

---

## 修复 5 (🟠 中等风险): concurrent grant 路径 realloc 失败静默丢数据

**影响文件**: `modules/treelock_core/src/client.c:507-518`  
**问题**: `_do_lock_core` 的 "already_granted" 路径中，若 `realloc` 扩容失败且数组已满，held 记录不会被创建但函数返回 `TREELOCK_OK`，导致锁表中 grant 存在但 held_locks 无追踪。

### 代码对比

```diff
     if (tl->held_count < tl->held_capacity) {
         tl->held_locks[tl->held_count].node_id     = node_id;
         ...
         tl->held_count++;
+    } else {
+        /*
+         * 致命错误：realloc 失败且数组已满，
+         * 无法记录 held entry。锁表中 grant 已存在，
+         * 但客户端无法追踪该锁。
+         * 保守策略：返回错误而非静默丢失追踪记录。
+         */
+        TREELOCK_LOG_ERROR("CORE",
+            "FATAL: cannot record held lock after concurrent grant: ...");
+        pthread_mutex_unlock(&tl->held_mutex);
+        return TREELOCK_ERR_INVAL;
     }
```

---

## 修复 6 (🟠 中等风险): `downgrade` 短暂双 grant 中间态

**风险等级**: 低（在实际代码中路径安全）  
**判断**: 降级期间短暂存在新旧两个 grant，但同一 `client_id` 之间不冲突（`check_conflict` 跳过自身），且 `release_lock` + `wake_waiters` 的调用顺序保证了正确性。**无需修改代码**，仅添加注释说明。

---

## 测试结果

```
test_protocol:  30/30 PASSED
test_concurrent: 34/34 PASSED
test_tree:      41/41 PASSED
test_log:       15/15 PASSED
─────────────────────────
合计:          120/120 PASSED (零警告编译)
```

## 变更文件清单

| 文件 | 变更类型 |
|------|----------|
| `modules/treelock_core/src/internal.h` | 类型变更: `wait_queue` 改为指针数组 |
| `modules/treelock_core/src/lock_table.c` | `add_waiter` 独立分配条目, `wake_waiters` 简化 swap-remove |
| `modules/treelock_core/src/client.c` | `_do_lock_core` 超时/唤醒路径 + `treelock_destroy` + `escalate` 协议校验 + `unlock` TOCTOU 修复 + concurrent grant 错误处理 |
| `tests/test_protocol.cc` | `RepeatedCreateLockDestroy100` 测试适配新协议校验 |
