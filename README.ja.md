# TreeLocks — 分散型マルチ粒度ツリーロックマネージャー

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![Language](https://img.shields.io/badge/language-C11-green)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()

> 汎用N分木構造に対して一貫性保護、デッドロック防止、分散コーディネーションを提供する**分散型マルチ粒度ツリーロック**システム。

**GitHub**: https://github.com/xyz2871309705-star/TREE_LOCKS

🌐 **言語**: [简体中文](README.md) | [English](README.en.md) | **日本語** | [Français](README.fr.md) | [Русский](README.ru.md)

---

## 目次

- [クイックスタート](#クイックスタート)
- [プロジェクト構造](#プロジェクト構造)
- [ロックプロトコル](#ロックプロトコル)
- [API概要](#api概要)
- [モジュール説明](#モジュール説明)
- [実装フェーズ](#実装フェーズ)
- [テスト](#テスト)
- [ドキュメント索引](#ドキュメント索引)
- [参考文献](#参考文献)

---

## クイックスタート

### 環境要件

| 依存 | バージョン | 説明 |
|------|------|------|
| CMake | >= 3.16 | ビルドシステム |
| GCC / Clang / MSVC | C11対応 | MinGW GCC 13.1 検証済み |
| pthread | システム標準 | スレッドセーフ |
| Python | >= 3.6 (オプション) | clangd 補助 |

### クローンとビルド

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

### CMake オプション

| オプション | デフォルト | 説明 |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Debug | Debug / Release / RelWithDebInfo / MinSizeRel |
| `TREELOCK_BUILD_SHARED` | OFF | ON=共有ライブラリ (.dll/.so)、OFF=静的ライブラリ (.a) |
| `TREELOCK_BUILD_EXAMPLES` | ON | サンプルプログラムをビルド |
| `TREELOCK_BUILD_TESTS` | ON | テストプログラムをビルド |

### テストの実行

```bash
cd build
ctest --output-on-failure
# または個別に実行
./tests/test_protocol
./tests/test_concurrent
```

### サンプルの実行

```bash
./examples/basic_usage          # 4つの基本的な使用例
./examples/log_callback_demo    # ログコールバック登録デモ
./examples/tree_usage           # 3つのツリー構造管理の例
```

---

## プロジェクト構造

```
TREE_LOCKS/
├── CMakeLists.txt                  # トップレベル CMake (モジュール集約)
├── README.md                       # このファイル
├── .gitignore
│
├── docs/                           # ドキュメント
│   ├── 设计.md                     # 設計ドキュメント
│   ├── DEVELOPER.md                # 開発者ドキュメント (詳細)
│   ├── ROADMAP.md                  # イテレーション計画
│   ├── 树结构管理方案.md            # ツリー構造提案書
│   └── tree-json-format.md         # JSONツリー定義フォーマット仕様
│
├── modules/                        # ★ モジュール (独立した include/ + src/)
│   ├── treelock_log/               # モジュール0: 統合ログ [基盤依存]
│   │   ├── include/treelock_log.h      # ログAPI (6レベル + コールバック登録)
│   │   └── src/log_core.c              # スレッドセーフ + 再入保護
│   │
│   ├── treelock_core/              # モジュール1: コアライブラリ [フェーズ1 ✅]
│   │   ├── include/
│   │   │   ├── treelock.h              # 公開API
│   │   │   ├── treelock_types.h        # 型ラッパー (INT_32/IN/OUT など)
│   │   │   └── treelock_platform.h     # プラットフォーム抽象 (時刻/DLL/TLS)
│   │   └── src/
│   │       ├── protocol.c              # 互換マトリックス、モード変換、プロトコル検証
│   │       ├── lock_table.c            # ロックテーブル、FIFO待機キュー
│   │       └── client.c                # クライアント実装 (スレッドセーフ+参照カウント)
│   │
│   ├── treelock_tree/              # モジュール1.5: ツリー構造管理 [イテレーション1.5 ✅]
│   │   ├── include/treelock_tree.h     # 公開API (ツリー読込/パスロック/クエリ)
│   │   └── src/
│   │       ├── tree_internal.h         # 内部データ構造 (ハッシュテーブル/ツリーインデックス)
│   │       ├── tree_core.c             # ツリーノード管理 + ハッシュテーブル操作
│   │       ├── tree_json.c             # JSON解析 (フラット/ネスト デュアルフォーマット)
│   │       ├── tree_validate.c         # ツリー検証 (ID一意性/単一ルート/循環なし)
│   │       ├── tree_path.c             # パス解決 + 祖先ロック導出
│   │       └── tree_api.c              # 公開APIブリッジ実装
│   │
│   ├── cjson/                      # サードパーティ: cJSON v1.7.18 (MIT License)
│   │   ├── cJSON.h
│   │   └── cJSON.c
│   │
│   ├── treelock_comm/              # モジュール2: 通信層 [フェーズ2/3]
│   └── treelock_server/            # モジュール3: サーバー [フェーズ3]
│
├── cmake/                          # CMake ヘルパー
│   ├── CompilerWarnings.cmake          # コンパイラ警告設定
│   └── expand_cc.py                    # clangd: @rsp 応答ファイル展開
│
├── proto/
│   └── treelock.proto              # Protobuf メッセージ定義
│
├── tests/
│   ├── test_protocol.c             # プロトコル正当性 (12ケース)
│   ├── test_concurrent.c           # 並行ストレス (3シナリオ)
│   └── test_tree.c                 # ツリー構造管理 (51ケース)
│
└── examples/
    ├── src/
    │   ├── basic_usage.c               # 基本的な使用法 (4例)
    │   ├── log_callback_demo.c         # ログコールバック登録
    │   └── tree_usage.c                # ツリー構造管理 (4例)
    └── json/
        ├── filesystem_tree.json        # ネスト形式 JSONツリー定義 (9ノード)
        └── filesystem_tree_flat.json   # フラット形式 JSONツリー定義 (9ノード)
```

---

## ロックプロトコル

### ロックモード

| モード | シンボル | 意味 |
|------|------|------|
| `TREELOCK_NL`  | NL  | ロックなし |
| `TREELOCK_IS`  | IS  | インテンション共有 — サブツリー内にSロックあり |
| `TREELOCK_IX`  | IX  | インテンション排他 — サブツリー内にXロックあり |
| `TREELOCK_S`   | S   | 共有 — サブツリー全体の読み取り |
| `TREELOCK_SIX` | SIX | S + IX — サブツリー読み取り + 一部子ノード排他書込 |
| `TREELOCK_X`   | X   | 排他 — サブツリー全体の排他アクセス |

### 互換マトリックス

```
              NL   IS   IX    S   SIX   X
      NL  |   ✅   ✅   ✅   ✅   ✅   ✅
      IS  |   ✅   ✅   ✅   ✅   ✅   ❌
      IX  |   ✅   ✅   ✅   ❌   ❌   ❌
      S   |   ✅   ✅   ❌   ✅   ❌   ❌
      SIX |   ✅   ✅   ❌   ❌   ❌   ❌
      X   |   ✅   ❌   ❌   ❌   ❌   ❌
```

### ロックプロトコル (4ルール)

1. **S / IS** 取得 → 親ノードに **IS** 以上のロックが必要
2. **X / IX / SIX** 取得 → 親ノードに **IX** 以上のロックが必要
3. ロック解放 → 子ノードから先に解放し、親ノードは後 (**ボトムアップ**)
4. ルートノード → 親なし、直接取得可能

### 使用例

```c
/* パスロック: ディレクトリ読取 + ファイル書込 */
treelock_lock(tl, root, TREELOCK_IS);    // ルート: インテンション共有
treelock_lock(tl, dir,  TREELOCK_IS);    // ディレクトリ: インテンション共有
treelock_lock(tl, file, TREELOCK_X);     // ファイル: 排他

/* ... 書き込み操作を実行 ... */

treelock_unlock(tl, file);               // ボトムアップで解放
treelock_unlock(tl, dir);
treelock_unlock(tl, root);
```

```c
/* ツリー構造管理: JSONからツリー読込 + パスロック (★ 新機能) */
treelock_load_tree_from_file(tl, "my_tree.json");

// 一行ロック: 自動 root(IX) → /home(IX) → /home/alice(X)
treelock_lock_path(tl, "/home/alice", TREELOCK_X);

// ... aliceのデータを安全に書き込み ...

treelock_unlock_path(tl, "/home/alice");
```

---

## API概要

```c
/* ========== ライフサイクル ========== */
treelock_t *treelock_create (const treelock_config_t *config);
void        treelock_destroy(treelock_t *tl);

/* ========== ロック操作 ========== */
int treelock_lock    (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode);
int treelock_try_lock(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode, int timeout_ms);
int treelock_unlock  (treelock_t *tl, treelock_node_id_t node_id);
int treelock_unlock_all(treelock_t *tl);

/* ========== 昇格/降格 ========== */
int treelock_escalate (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);
int treelock_downgrade(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);

/* ========== クエリ ========== */
treelock_mode_t treelock_get_mode  (treelock_t *tl, treelock_node_id_t node_id);
int             treelock_query_node(treelock_t *tl, treelock_node_id_t node_id, char **json_result);

/* ========== コールバック ========== */
int treelock_set_lost_callback(treelock_t *tl, treelock_lost_cb cb, void *user_data);

/* ========== ツリー構造管理 (libtreelock_tree) ========== */
int  treelock_load_tree_from_file(treelock_t *tl, const char *filepath);
int  treelock_load_tree_from_string(treelock_t *tl, const char *json_string);
int  treelock_register_node(treelock_t *tl, uint64_t node_id, uint64_t parent_id, const char *label);
int  treelock_lock_path(treelock_t *tl, const char *path, treelock_mode_t mode);
int  treelock_unlock_path(treelock_t *tl, const char *path);
int  treelock_get_parent(treelock_t *tl, uint64_t node_id, uint64_t *parent_id);
int  treelock_lookup_path(treelock_t *tl, const char *path, uint64_t *node_id);
```

---

## モジュール説明

### treelock_log — ログモジュール

統合ログインターフェース。プロジェクト内のすべての出力はこのモジュールを通す必要があります。

```c
#include "treelock_log.h"

// 6段階のログマクロ
TREELOCK_LOG_FATAL("TAG", "msg: %s", detail);
TREELOCK_LOG_ERROR("TAG", "error: %d", code);
TREELOCK_LOG_WARN ("TAG", "warning");
TREELOCK_LOG_INFO ("TAG", "client created");
TREELOCK_LOG_DEBUG("TAG", "mode=%s", name);
TREELOCK_LOG_TRACE("TAG", "--> enter func()");

// 外部コールバック登録 (独自のログシステムと統合)
treelock_log_set_callback(my_logger, my_context);
treelock_log_set_level(TREELOCK_LOG_WARN);  // 実行時フィルタリング
```

### treelock_core — コアライブラリ

`libtreelock_core.a` を生成。`treelock_log` + `pthread` に依存。

主な特徴:
- **スレッドセーフ**: ノード単位のミューテックス + グローバルロックテーブルロック
- **参照カウント**: 同一クライアントの再入ロック、ref_count がゼロで解放
- **FIFO ウェイクアップ**: 条件変数 + 互換性チェック、待機者を餓死させない
- **インプレース昇格/降格**: escalate/downgrade がロックテーブルを直接操作、重複エントリ回避

---

## 実装フェーズ

| フェーズ | 状態 | 内容 | コード量 |
|------|------|------|--------|
| フェーズ1 | 🚧 80% | スタンドアロン版: プロトコル + ロックテーブル + クライアント + ログ + ツリー構造 + クロスプラットフォーム | 約3500行 |
| フェーズ2 | 📋 計画中 | ZKコーディネート版: 分散ロックコーディネーション + Watchコールバック | +約2500行 |
| フェーズ3 | 📋 計画中 | 自社開発サービス版: Raft + gRPC + リース管理 | +約6000行 |

### フェーズ1 進捗

- [x] 6種類のロックモード + 完全互換マトリックス
- [x] ロック昇格/降格パス検証
- [x] FIFO待機キュー + 条件変数ウェイクアップ
- [x] 同一クライアント再入ロック (参照カウント)
- [x] スレッドセーフ (pthread mutex)
- [x] 統合ログモジュール (6段階 + 外部コールバック)
- [x] クロスプラットフォームビルド (Windows/Linux/macOS)
- [x] 12 プロトコルテスト + 3 並行テスト
- [x] **ツリー構造管理** (JSON読込 + パスロック + 自動プロトコル検証) ← **イテレーション1.5 完了**
- [ ] リースと障害復旧
- [ ] パフォーマンスベンチマークとメモリ検出

---

## テスト

| テスト | ケース数 | フレームワーク | コマンド |
|------|--------|------|------|
| プロトコル正当性 | 12 | Google Test | `./build/tests/test_protocol` |
| ログモジュール | 12 | Google Test | `./build/tests/test_log` |
| 並行ストレス | 3 | Google Test | `./build/tests/test_concurrent` |
| ツリー構造管理 | 9 | Google Test | `./build/tests/test_tree` |

> **テストフレームワーク**: Google Test v1.15.2 (C++)、ローカルコピー `_deps/googletest/` 経由、CMake `FetchContent` で統合。

```
$ ctest --output-on-failure
100% tests passed, 0 tests failed out of 4

$ ./tests/test_protocol
[==========] 12 tests from 5 test suites ran. (0 ms total)
[  PASSED  ] 12 tests.
```

---

## ドキュメント索引

| ドキュメント | 説明 |
|------|------|
| [设计.md](docs/设计.md) | 完全な設計思想、プロトコル導出、アーキテクチャ概要 |
| [DEVELOPER.md](docs/DEVELOPER.md) | 開発者ガイド: オンボーディング、アーキテクチャ、コーディング規約、モジュール詳細、FAQ |
| [ROADMAP.md](docs/ROADMAP.md) | イテレーション計画: 14イテレーション、推定工数、優先度、受入基準 |
| [树结构管理方案.md](docs/树结构管理方案.md) | ツリー構造提案書: 実現可能性分析、設計決定、実装記録 |
| [tree-json-format.md](docs/tree-json-format.md) | JSONフォーマット仕様: フィールド定義、2形式、検証ルール、完全な例 |

---

## 参考文献

- Gray, J. et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*
- Mohan, C. et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*
- Ongaro, D. & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm (Raft)*
