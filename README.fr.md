# TreeLocks — Gestionnaire de Verrous Arborescents Multi-Granularité Distribué

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](CHANGELOG.md)
[![Language](https://img.shields.io/badge/language-C11-green)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()

> Un système de **verrouillage arborescent multi-granularité distribué** offrant une protection de cohérence, une prévention des interblocages et une coordination distribuée pour les structures arborescentes N-aires génériques.

**GitHub**: https://github.com/xyz2871309705-star/TREE_LOCKS

🌐 **Langues**: [简体中文](README.md) | [English](README.en.md) | [日本語](README.ja.md) | **Français** | [Русский](README.ru.md)

---

## Table des Matières

- [Démarrage Rapide](#démarrage-rapide)
- [Structure du Projet](#structure-du-projet)
- [Protocole de Verrouillage](#protocole-de-verrouillage)
- [Aperçu de l'API](#aperçu-de-lapi)
- [Description des Modules](#description-des-modules)
- [Phases d'Implémentation](#phases-dimplémentation)
- [Tests](#tests)
- [Index de Documentation](#index-de-documentation)
- [Références](#références)

---

## Démarrage Rapide

### Prérequis

| Dépendance | Version | Notes |
|------|------|------|
| CMake | >= 3.16 | Système de build |
| GCC / Clang / MSVC | Compatible C11 | MinGW GCC 13.1 vérifié |
| pthread | Fourni par le système | Sécurité des threads |
| Python | >= 3.6 (optionnel) | Assistant clangd |

### Cloner & Compiler

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

### Options CMake

| Option | Défaut | Description |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | Debug | Debug / Release / RelWithDebInfo / MinSizeRel |
| `TREELOCK_BUILD_SHARED` | OFF | ON=bibliothèque partagée (.dll/.so), OFF=bibliothèque statique (.a) |
| `TREELOCK_BUILD_EXAMPLES` | ON | Compiler les programmes d'exemple |
| `TREELOCK_BUILD_TESTS` | ON | Compiler les programmes de test |

### Exécuter les Tests

```bash
cd build
ctest --output-on-failure
# Ou individuellement
./tests/test_protocol
./tests/test_concurrent
```

### Exécuter les Exemples

```bash
./examples/basic_usage          # 4 exemples d'utilisation de base
./examples/log_callback_demo    # Démo d'enregistrement de callback de log
./examples/tree_usage           # 3 exemples de gestion de structure arborescente
```

---

## Structure du Projet

```
TREE_LOCKS/
├── CMakeLists.txt                  # CMake de niveau supérieur (agrège les modules)
├── README.md                       # Ce fichier
├── .gitignore
│
├── docs/                           # Documentation
│   ├── 设计.md                     # Document de conception
│   ├── DEVELOPER.md                # Guide du développeur (détaillé)
│   ├── ROADMAP.md                  # Feuille de route des itérations
│   ├── 树结构管理方案.md            # Proposition de structure arborescente
│   └── tree-json-format.md         # Spécification du format JSON pour les arbres
│
├── modules/                        # ★ Modules (include/ + src/ indépendants)
│   ├── treelock_log/               # Module 0: Journalisation unifiée [dépendance fondamentale]
│   │   ├── include/treelock_log.h      # API de journalisation (6 niveaux + enregistrement callback)
│   │   └── src/log_core.c              # Thread-safe + protection de réentrance
│   │
│   ├── treelock_core/              # Module 1: Bibliothèque principale [Phase 1 ✅]
│   │   ├── include/
│   │   │   ├── treelock.h              # API publique
│   │   │   ├── treelock_types.h        # Wrappers de types (INT_32/IN/OUT etc.)
│   │   │   └── treelock_platform.h     # Abstraction de plateforme (temps/DLL/TLS)
│   │   └── src/
│   │       ├── protocol.c              # Matrice de compatibilité, conversion de modes, validation de protocole
│   │       ├── lock_table.c            # Table de verrous, files d'attente FIFO
│   │       └── client.c                # Implémentation client (thread-safe + comptage de références)
│   │
│   ├── treelock_tree/              # Module 1.5: Gestion de structure arborescente [Iteration 1.5 ✅]
│   │   ├── include/treelock_tree.h     # API publique (chargement arbre/verrouillage par chemin/requête)
│   │   └── src/
│   │       ├── tree_internal.h         # Structures de données internes (table de hachage/index d'arbre)
│   │       ├── tree_core.c             # Gestion des nœuds + opérations de table de hachage
│   │       ├── tree_json.c             # Analyse JSON (format plat/imbriqué double)
│   │       ├── tree_validate.c         # Validation d'arbre (IDs uniques/racine unique/pas de cycles)
│   │       ├── tree_path.c             # Résolution de chemin + dérivation de verrous ancêtres
│   │       └── tree_api.c              # Implémentation du pont API publique
│   │
│   ├── cjson/                      # Tiers: cJSON v1.7.18 (Licence MIT)
│   │   ├── cJSON.h
│   │   └── cJSON.c
│   │
│   ├── treelock_comm/              # Module 2: Couche de communication [Phase 2/3]
│   └── treelock_server/            # Module 3: Serveur [Phase 3]
│
├── cmake/                          # Assistants CMake
│   ├── CompilerWarnings.cmake          # Configuration des avertissements du compilateur
│   └── expand_cc.py                    # clangd: expansion des fichiers de réponse @rsp
│
├── proto/
│   └── treelock.proto              # Définitions de messages Protobuf
│
├── tests/
│   ├── test_protocol.c             # Correction du protocole (12 cas)
│   ├── test_concurrent.c           # Stress concurrent (3 scénarios)
│   └── test_tree.c                 # Gestion de structure arborescente (51 cas)
│
└── examples/
    ├── src/
    │   ├── basic_usage.c               # Utilisation de base (4 exemples)
    │   ├── log_callback_demo.c         # Enregistrement de callback de log
    │   └── tree_usage.c                # Gestion de structure arborescente (4 exemples)
    └── json/
        ├── filesystem_tree.json        # Définition d'arbre JSON format imbriqué (9 nœuds)
        └── filesystem_tree_flat.json   # Définition d'arbre JSON format plat (9 nœuds)
```

---

## Protocole de Verrouillage

### Modes de Verrouillage

| Mode | Symbole | Signification |
|------|------|------|
| `TREELOCK_NL`  | NL  | Aucun verrou |
| `TREELOCK_IS`  | IS  | Intention Partagée — verrous S dans le sous-arbre |
| `TREELOCK_IX`  | IX  | Intention eXclusive — verrous X dans le sous-arbre |
| `TREELOCK_S`   | S   | Partagé — lecture de tout le sous-arbre |
| `TREELOCK_SIX` | SIX | S + IX — lecture du sous-arbre mais écriture exclusive sur certains enfants |
| `TREELOCK_X`   | X   | eXclusif — accès exclusif à tout le sous-arbre |

### Matrice de Compatibilité

```
              NL   IS   IX    S   SIX   X
      NL  |   ✅   ✅   ✅   ✅   ✅   ✅
      IS  |   ✅   ✅   ✅   ✅   ✅   ❌
      IX  |   ✅   ✅   ✅   ❌   ❌   ❌
      S   |   ✅   ✅   ❌   ✅   ❌   ❌
      SIX |   ✅   ✅   ❌   ❌   ❌   ❌
      X   |   ✅   ❌   ❌   ❌   ❌   ❌
```

### Protocole de Verrouillage (4 Règles)

1. Acquérir **S / IS** → doit d'abord détenir **IS** ou plus fort sur le parent
2. Acquérir **X / IX / SIX** → doit d'abord détenir **IX** ou plus fort sur le parent
3. Libérer les verrous → doit libérer les enfants d'abord, puis le parent (**de bas en haut**)
4. Nœud racine → pas de parent, peut être verrouillé directement

### Exemples d'Utilisation

```c
/* Verrouillage par chemin : lire répertoire + écrire fichier */
treelock_lock(tl, root, TREELOCK_IS);    // Racine: intention partagée
treelock_lock(tl, dir,  TREELOCK_IS);    // Répertoire: intention partagée
treelock_lock(tl, file, TREELOCK_X);     // Fichier: exclusif

/* ... effectuer les opérations d'écriture ... */

treelock_unlock(tl, file);               // Libération de bas en haut
treelock_unlock(tl, dir);
treelock_unlock(tl, root);
```

```c
/* Gestion de structure arborescente: charger arbre depuis JSON + verrouillage par chemin (★ nouveau) */
treelock_load_tree_from_file(tl, "my_tree.json");

// Verrouillage en une ligne: auto root(IX) → /home(IX) → /home/alice(X)
treelock_lock_path(tl, "/home/alice", TREELOCK_X);

// ... écrire les données d'alice en toute sécurité ...

treelock_unlock_path(tl, "/home/alice");
```

---

## Aperçu de l'API

```c
/* ========== Cycle de Vie ========== */
treelock_t *treelock_create (const treelock_config_t *config);
void        treelock_destroy(treelock_t *tl);

/* ========== Opérations de Verrouillage ========== */
int treelock_lock    (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode);
int treelock_try_lock(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t mode, int timeout_ms);
int treelock_unlock  (treelock_t *tl, treelock_node_id_t node_id);
int treelock_unlock_all(treelock_t *tl);

/* ========== Promotion / Rétrogradation ========== */
int treelock_escalate (treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);
int treelock_downgrade(treelock_t *tl, treelock_node_id_t node_id, treelock_mode_t new_mode);

/* ========== Requêtes ========== */
treelock_mode_t treelock_get_mode  (treelock_t *tl, treelock_node_id_t node_id);
int             treelock_query_node(treelock_t *tl, treelock_node_id_t node_id, char **json_result);

/* ========== Callbacks ========== */
int treelock_set_lost_callback(treelock_t *tl, treelock_lost_cb cb, void *user_data);

/* ========== Gestion de Structure Arborescente (libtreelock_tree) ========== */
int  treelock_load_tree_from_file(treelock_t *tl, const char *filepath);
int  treelock_load_tree_from_string(treelock_t *tl, const char *json_string);
int  treelock_register_node(treelock_t *tl, uint64_t node_id, uint64_t parent_id, const char *label);
int  treelock_lock_path(treelock_t *tl, const char *path, treelock_mode_t mode);
int  treelock_unlock_path(treelock_t *tl, const char *path);
int  treelock_get_parent(treelock_t *tl, uint64_t node_id, uint64_t *parent_id);
int  treelock_lookup_path(treelock_t *tl, const char *path, uint64_t *node_id);
```

---

## Description des Modules

### treelock_log — Module de Journalisation

Interface de journalisation unifiée, toute sortie dans le projet doit passer par ce module.

```c
#include "treelock_log.h"

// Macros de journalisation à 6 niveaux
TREELOCK_LOG_FATAL("TAG", "msg: %s", detail);
TREELOCK_LOG_ERROR("TAG", "error: %d", code);
TREELOCK_LOG_WARN ("TAG", "warning");
TREELOCK_LOG_INFO ("TAG", "client created");
TREELOCK_LOG_DEBUG("TAG", "mode=%s", name);
TREELOCK_LOG_TRACE("TAG", "--> enter func()");

// Enregistrement de callback externe (intégration avec votre propre système de log)
treelock_log_set_callback(my_logger, my_context);
treelock_log_set_level(TREELOCK_LOG_WARN);  // Filtrage à l'exécution
```

### treelock_core — Bibliothèque Principale

Produit `libtreelock_core.a`, dépend de `treelock_log` + `pthread`.

Caractéristiques clés:
- **Thread-safe**: Mutex par nœud + verrou global de la table de verrous
- **Comptage de références**: Verrouillage réentrant pour le même client, libéré quand ref_count atteint zéro
- **Réveil FIFO**: Variable de condition + vérification de compatibilité, pas de famine des attentes
- **Promotion sur place**: escalate/downgrade opèrent directement sur la table de verrous, évitant les entrées en double

---

## Phases d'Implémentation

| Phase | Statut | Contenu | Lignes de Code |
|------|------|------|--------|
| Phase 1 | 🚧 80% | Bibliothèque autonome: protocole + table de verrous + client + journalisation + structure arborescente + multiplateforme | ~3500 lignes |
| Phase 2 | 📋 Planifié | Version coordonnée ZK: coordination de verrous distribués + callbacks Watch | +~2500 lignes |
| Phase 3 | 📋 Planifié | Service auto-développé: Raft + gRPC + gestion de baux | +~6000 lignes |

### Progression Phase 1

- [x] 6 modes de verrouillage + matrice de compatibilité complète
- [x] Validation des chemins de promotion/rétrogradation de verrous
- [x] Files d'attente FIFO + réveil par variable de condition
- [x] Verrouillage réentrant même client (comptage de références)
- [x] Sécurité des threads (mutex pthread)
- [x] Module de journalisation unifié (6 niveaux + callbacks externes)
- [x] Build multiplateforme (Windows/Linux/macOS)
- [x] 12 tests de protocole + 3 tests de concurrence
- [x] **Gestion de structure arborescente** (chargement JSON + verrouillage par chemin + validation automatique du protocole) ← **Iteration 1.5 terminée**
- [ ] Baux et reprise sur panne
- [ ] Benchmarks de performance et détection de mémoire

---

## Tests

| Test | Cas | Framework | Commande |
|------|--------|------|------|
| Correction du Protocole | 12 | Google Test | `./build/tests/test_protocol` |
| Module de Journalisation | 12 | Google Test | `./build/tests/test_log` |
| Stress Concurrent | 3 | Google Test | `./build/tests/test_concurrent` |
| Structure Arborescente | 9 | Google Test | `./build/tests/test_tree` |

> **Framework de test**: Google Test v1.15.2 (C++), via copie locale dans `_deps/googletest/`, intégré par CMake `FetchContent`.

```
$ ctest --output-on-failure
100% tests passed, 0 tests failed out of 4

$ ./tests/test_protocol
[==========] 12 tests from 5 test suites ran. (0 ms total)
[  PASSED  ] 12 tests.
```

---

## Index de Documentation

| Document | Description |
|------|------|
| [设计.md](docs/设计.md) | Conception complète, dérivation du protocole, aperçu de l'architecture |
| [DEVELOPER.md](docs/DEVELOPER.md) | Guide développeur: intégration, architecture, conventions de codage, détails des modules, FAQ |
| [ROADMAP.md](docs/ROADMAP.md) | Feuille de route: 14 itérations, effort estimé, priorités, critères d'acceptation |
| [树结构管理方案.md](docs/树结构管理方案.md) | Proposition de structure arborescente: analyse de faisabilité, décisions de conception, enregistrement d'implémentation |
| [tree-json-format.md](docs/tree-json-format.md) | Spécification du format JSON: définitions de champs, deux formats, règles de validation, exemples complets |

---

## Références

- Gray, J. et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*
- Mohan, C. et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*
- Ongaro, D. & Ousterhout, J. (2014). *In Search of an Understandable Consensus Algorithm (Raft)*
