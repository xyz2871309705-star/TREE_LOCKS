# TreeLocks 应用于 BMC I2C 拓扑 — 可行方案书

> 版本: 0.1.0  
> 日期: 2026-06-14  
> 状态: 草案  
> 目标: 将 TreeLocks 多粒度树锁协议应用于 BMC I2C 拓扑，实现多 daemon 并发 I2C 访问的安全协调

---

## 目录

1. [背景与问题](#1-背景与问题)
2. [方案概览](#2-方案概览)
3. [拓扑映射设计](#3-拓扑映射设计)
4. [I2C 操作锁语义](#4-i2c-操作锁语义)
5. [架构设计](#5-架构设计)
6. [API 设计](#6-api-设计)
7. [与现有代码的集成](#7-与现有代码的集成)
8. [实施路线](#8-实施路线)
9. [可行性评估](#9-可行性评估)
10. [风险与缓解](#10-风险与缓解)
11. [参考资料](#11-参考资料)
12. [附录](#12-附录)

---

## 1. 背景与问题

### 1.1 BMC I2C 拓扑特征

BMC（Baseboard Management Controller）通过多条 I2C 总线管理服务器主板上的各类设备：

```
BMC SoC (AST2600 / AST2500)
│
├── I2C Bus 0 (i2c-0) ─── 100 kHz, 主机板传感器
│   ├── PCA9548 MUX @ 0x70
│   │   ├── Channel 0 → TMP421 Temp Sensor @ 0x1F
│   │   ├── Channel 1 → M24C64 EEPROM (FRU) @ 0x50
│   │   ├── Channel 2 → TPS53679 VRM @ 0x60
│   │   └── Channel 3 → INA230 Power Monitor @ 0x40
│   ├── PCA9546 MUX @ 0x71
│   │   ├── Channel 0 → DDR5 SPD @ 0x50 (DIMM A0)
│   │   ├── Channel 1 → DDR5 SPD @ 0x52 (DIMM A1)
│   │   └── Channel 2 → DDR5 SPD @ 0x50 (DIMM B0)
│   └── EMC1412 Temp Sensor @ 0x4C
│
├── I2C Bus 1 (i2c-1) ─── 400 kHz, FRU/PSU
│   ├── PCA9548 MUX @ 0x72
│   │   ├── Channel 0 → PSU0 FRU @ 0x50
│   │   ├── Channel 1 → PSU1 FRU @ 0x50
│   │   └── Channel 2 → PSU2 FRU @ 0x50
│   └── AT24C256 EEPROM (BMC FRU) @ 0x57
│
├── I2C Bus 2 (i2c-2) ─── 100 kHz, 机箱管理
│   ├── PCA9545 MUX @ 0x73
│   │   ├── Channel 0 → Fan Controller EMC2305 @ 0x2D
│   │   └── Channel 1 → Hot-Swap Controller @ 0x22
│   └── RTC DS3231 @ 0x68
│
└── I2C Bus 3 (i2c-3) ─── 400 kHz, 高速设备
    └── FPGA Config @ 0x30
```

**关键特征：**

| 特征 | 描述 |
|------|------|
| 层级深度 | 3–5 层（BMC → Bus → MUX → Channel → Device） |
| 节点数量 | 20–100 个设备（中型平台），大型平台可达 200+ |
| MUX 级联 | 常见 1–2 级 MUX，部分平台存在 MUX 后接 MUX 的 3 级拓扑 |
| 地址复用 | 同一 MUX 不同通道后面可以有相同 I2C 地址的设备（如多个 SPD @ 0x50） |
| 并发来源 | Redfish daemon、IPMI daemon、Web UI、KCS、host mailbox 同时触发 I2C 操作 |

### 1.2 当前痛点

```
┌─────────────────────────────────────────────────────────────────┐
│  时间线：三个 daemon 同时操作 MUX 后面的设备                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  phosphor-hwmon:  切 MUX ch0 → 读 temp@0x1F (10ms)              │
│  ipmid:           切 MUX ch1 → 读 fru@0x50  (50ms)  ← 冲突！    │
│  entity-manager:  切 MUX ch2 → 读 vrm@0x60  (15ms)  ← 冲突！    │
│                                                                 │
│  结果：                                                          │
│  - hwmon 读到的是 VRM 的数据（mux 被切到了 ch2）                │
│  - ipmid 读到的是 Temp 的数据（mux 被切到了 ch0）                │
│  - 上层 sysfs 暴露了错误的 sensor 值 → 风扇失控 / 告警误报       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Linux 内核 `i2c-mux` 子系统的局限：**

| 内核能力 | 能解决的 | 不能解决的 |
|----------|---------|-----------|
| `i2c_lock_adapter()` | 同 bus 上的设备互斥 | **不同 bus 的协调**（如同时操作两个 bus 上的级联 MUX） |
| `i2c_mux_lock()` | 单个 MUX 的通道切换原子性 | **多 MUX 级联**的原子性（切 MUX A 的 ch0 → 切 MUX B 的 ch1） |
| `i2c_transfer()` | 单次 I2C 传输的原子性 | **多帧协议**（如 PMBus 需连续多帧读写，中间不能切 MUX） |
| 内核 mutex | 同进程内互斥 | **跨进程**（phosphor-hwmon 和 ipmid 是不同进程） |

**核心矛盾：多 daemon 共享 I2C 拓扑，但现有机制只在单个 bus 或单个 MUX 层面提供保护，缺乏全局的、树状层级的并发控制。**

### 1.3 TreeLocks 的匹配度

| TreeLocks 能力 | BMC I2C 需求 | 匹配度 |
|:--|:--|:--:|
| N 叉树节点模型 | I2C 拓扑天然是 N 叉树 | ⭐⭐⭐⭐⭐ |
| 意向锁 (IS/IX) | MUX 通道切换需要"宣告意图" | ⭐⭐⭐⭐⭐ |
| 路径加锁 `lock_path()` | 访问 MUX 后面的设备 = 沿路径加锁 | ⭐⭐⭐⭐⭐ |
| 兼容矩阵 (36 组合) | 多个 daemon 同时读不同设备的并发控制 | ⭐⭐⭐⭐⭐ |
| 死锁预防 (自顶向下) | 消除 I2C 访问顺序导致的死锁 | ⭐⭐⭐⭐⭐ |
| 引用计数 (同客户端重入) | 同 daemon 嵌套访问同一设备 | ⭐⭐⭐⭐ |
| 超时锁 `try_lock()` | I2C 操作有超时需求 | ⭐⭐⭐⭐⭐ |
| 锁升级/降级 | 先读后写 (S → X) 的 sensor 更新场景 | ⭐⭐⭐⭐ |
| 租约机制 (1.6 规划中) | daemon 崩溃不永久阻塞 I2C | ⭐⭐⭐⭐ |
| 分布式协调 (Phase 2/3) | 多 daemon 跨进程共享锁状态 | ⭐⭐⭐⭐⭐ |

---

## 2. 方案概览

### 2.1 一句话方案

**将 BMC I2C 拓扑映射为 TreeLocks 的 N 叉树，在每个 I2C 访问操作前后调用 TreeLocks 的路径加锁/解锁 API，由意向锁协议自动保证 MUX 通道切换的原子性和跨 daemon 的并发安全。**

### 2.2 核心思路

```
现状：每个 daemon 各自用 /dev/i2c-N ioctl，互不知晓
      → 引入 TreeLocks 作为统一的"I2C 交通灯"


方案：
                      ┌─────────────────────────────────┐
  phosphor-hwmon ────┤                                 │
  ipmid ─────────────┤  libtreelock + libbmc_i2c_lock   │
  entity-manager ────┤  (shared memory / Unix socket)   │──→ /dev/i2c-N
  webui ─────────────┤                                 │
  kcsbridge ─────────┤                                 │
                      └─────────────────────────────────┘
                              ↑
                     TreeLocks 协调层
                  (进程间共享锁表)
```

### 2.3 分层架构

```
┌──────────────────────────────────────────────────────────────┐
│  Layer 3: BMC Daemons                                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │ hwmon    │ │ ipmid    │ │ entity-m │ │ websrv   │       │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘       │
│       └─────────────┴────────────┴────────────┘              │
│                         │                                     │
├─────────────────────────┼─────────────────────────────────────┤
│  Layer 2: I2C Lock Wrapper (本次新增)                         │
│                         │                                     │
│              ┌──────────▼──────────┐                         │
│              │  libbmc_i2c_lock    │  封装：拓扑加载 +       │
│              │  - i2c_read()       │  路径加锁 + I2C 操作     │
│              │  - i2c_write()      │                         │
│              │  - i2c_smbus_*()    │                         │
│              └──────────┬──────────┘                         │
│                         │                                     │
├─────────────────────────┼─────────────────────────────────────┤
│  Layer 1: TreeLocks Core (现有 + 扩展)                        │
│                         │                                     │
│              ┌──────────▼──────────┐                         │
│              │  libtreelock.a      │  锁协议引擎              │
│              │  + libtreelock_tree │  树结构管理              │
│              │  + 共享内存扩展     │  (Phase 2 新增)          │
│              └─────────────────────┘                         │
│                         │                                     │
├─────────────────────────┼─────────────────────────────────────┤
│  Layer 0: Kernel I2C Subsystem                               │
│                         │                                     │
│              ┌──────────▼──────────┐                         │
│              │  /dev/i2c-N         │  ioctl I2C_RDWR         │
│              │  i2c-mux driver     │  内核级 mux 切换         │
│              └─────────────────────┘                         │
└──────────────────────────────────────────────────────────────┘
```

### 2.4 关键设计决策

| 决策点 | 选择 | 理由 |
|:--|:--|:--|
| 锁协调方式 | Phase 1: 共享内存 + pthread mutex；Phase 2: Unix socket + 本地 RPC | 单 BMC 场景下共享内存性能最优，延迟 < 1μs |
| 节点 ID 编码 | `(bus << 40) \| (mux_addr << 32) \| (channel << 24) \| device_addr << 16 \| type` | 直接从 I2C 地址反推 node_id，无需查表 |
| 拓扑来源 | MRB (Machine Readable Board) JSON → TreeLocks JSON 自动转换 | 复用 OpenBMC entity-manager 的拓扑描述 |
| MUX 通道建模 | 每个 MUX 通道作为独立节点（mux_ch0, mux_ch1...） | 不同通道后的设备需要独立加锁 |
| 与内核关系 | 互补不替代：TreeLocks 管并发策略，内核管硬件访问 | 内核 i2c-dev 仍负责 ioctl 级别的原子性 |

---

## 3. 拓扑映射设计

### 3.1 节点 ID 编码方案

```c
/*
 * BMC I2C 节点 ID 编码 (64-bit)
 *
 * ┌────────────┬────────────┬────────────┬────────────┬────────────┐
 * │ 63......48 │ 47......32 │ 31......16 │ 15........0│
 * ├────────────┼────────────┼────────────┼────────────┼────────────┤
 * │ 预留       │ bus+addr   │ channel    │ device     │
 * │ (type)     │ (mux/raw)  │            │ addr+type  │
 * └────────────┴────────────┴────────────┴────────────┴────────────┘
 *
 * 编码规则：
 *   - byte 5 (bits 47..40): I2C bus 编号 (0–15)
 *   - byte 4 (bits 39..32): MUX 地址 7-bit << 1 (如 0x70 → 0xE0)
 *   - byte 3 (bits 31..24): MUX 通道号 (0–7) 或 0xFF 表示非 MUX 设备
 *   - byte 2 (bits 23..16): 设备 I2C 地址 7-bit << 1
 *   - byte 1 (bits 15..8):  节点类型标记
 *   - byte 0 (bits 7..0):   保留
 */

#define BMC_I2C_NODE_TYPE_BUS     0x01  // I2C bus 节点
#define BMC_I2C_NODE_TYPE_MUX     0x02  // MUX 芯片节点
#define BMC_I2C_NODE_TYPE_CHANNEL 0x03  // MUX 通道节点
#define BMC_I2C_NODE_TYPE_DEVICE  0x04  // 终端设备节点

// 构造 node_id 的宏
#define BMC_I2C_MAKE_BUS_ID(bus) \
    (((UINT_64)(bus) << 40) | ((UINT_64)BMC_I2C_NODE_TYPE_BUS << 8))

#define BMC_I2C_MAKE_MUX_ID(bus, mux_addr_7bit) \
    (((UINT_64)(bus) << 40) | ((UINT_64)((mux_addr_7bit) << 1) << 32) | \
     ((UINT_64)BMC_I2C_NODE_TYPE_MUX << 8))

#define BMC_I2C_MAKE_CHANNEL_ID(bus, mux_addr_7bit, channel) \
    (((UINT_64)(bus) << 40) | ((UINT_64)((mux_addr_7bit) << 1) << 32) | \
     ((UINT_64)(channel) << 24) | ((UINT_64)BMC_I2C_NODE_TYPE_CHANNEL << 8))

#define BMC_I2C_MAKE_DEVICE_ID(bus, mux_addr_7bit, channel, dev_addr_7bit) \
    (((UINT_64)(bus) << 40) | ((UINT_64)((mux_addr_7bit) << 1) << 32) | \
     ((UINT_64)(channel) << 24) | ((UINT_64)((dev_addr_7bit) << 1) << 16) | \
     ((UINT_64)BMC_I2C_NODE_TYPE_DEVICE << 8))
```

**编码示例（基于 1.1 节拓扑）：**

| 设备 | node_id (hex) | 说明 |
|:--|:--|:--|
| I2C Bus 0 | `0x00_0000_0001_0000` | bus=0, type=BUS |
| MUX @ 0x70 on Bus 0 | `0x00_E000_0002_0000` | bus=0, addr=0xE0, type=MUX |
| MUX Ch0 on Bus 0 | `0x00_E000_0003_0000` | bus=0, addr=0xE0, ch=0, type=CHANNEL |
| Temp @ 0x1F on Bus 0 MUX Ch0 | `0x00_E000_3E04_0000` | bus=0, mux=0xE0, ch=0, dev=0x3E |
| DDR5 SPD @ 0x50 on Bus 0 MUX Ch0 | `0x00_E200_A004_0000` | bus=0, mux2=0xE2, ch=0, dev=0xA0 |
| I2C Bus 1 | `0x01_0000_0001_0000` | bus=1, type=BUS |

### 3.2 路径命名约定

```
路径格式: /bus<N>/mux_<addr>/ch<C>/<device_label>

示例:
  /bus0/mux_0x70/ch0/temp_0x1F       → MUX ch0 后面的温度传感器
  /bus0/mux_0x70/ch1/eeprom_fru_0x50 → MUX ch1 后面的 FRU EEPROM
  /bus0/mux_0x71/ch0/spd_dimm_a0     → 第二个 MUX ch0 后面的 DIMM SPD
  /bus0/emc1412_0x4c                 → Bus 0 上直连的温度传感器（无 MUX）
  /bus1/mux_0x72/ch0/psu0_fru       → Bus 1 MUX ch0 后面的 PSU0 FRU
```

### 3.3 拓扑 JSON 定义

为 BMC I2C 设计的专用 JSON 格式（兼容 TreeLocks 现有 JSON 解析器）：

```json
{
  "version": 1,
  "name": "ast2600_evb_i2c_topology",
  "platform": "AST2600_EVB",
  "description": "AST2600 EVB 完整 I2C 拓扑 — 4 bus, 5 mux, 23 devices",
  "root_id": 0,
  "bus_list": [
    {
      "bus": 0,
      "freq_khz": 100,
      "label": "bus0",
      "devices": [
        {
          "type": "mux",
          "chip": "pca9548",
          "addr_7bit": 112,
          "label": "mux_0x70",
          "channels": [
            {
              "ch": 0,
              "label": "ch0",
              "devices": [
                { "type": "temp", "chip": "tmp421",  "addr_7bit": 31,  "label": "temp_inlet_0x1F" },
                { "type": "temp", "chip": "tmp421",  "addr_7bit": 29,  "label": "temp_outlet_0x1D" }
              ]
            },
            {
              "ch": 1,
              "label": "ch1",
              "devices": [
                { "type": "eeprom", "chip": "m24c64", "addr_7bit": 80, "label": "fru_mb_0x50" }
              ]
            },
            {
              "ch": 2,
              "label": "ch2",
              "devices": [
                { "type": "vrm", "chip": "tps53679", "addr_7bit": 96, "label": "vrm_vcore_0x60" },
                { "type": "pmon", "chip": "ina230",   "addr_7bit": 64, "label": "pmon_vcore_0x40" }
              ]
            }
          ]
        },
        {
          "type": "mux",
          "chip": "pca9546",
          "addr_7bit": 113,
          "label": "mux_0x71",
          "channels": [
            {
              "ch": 0,
              "label": "ch0",
              "devices": [
                { "type": "spd", "chip": "spd5118", "addr_7bit": 80, "label": "spd_dimm_a0" }
              ]
            },
            {
              "ch": 1,
              "label": "ch1",
              "devices": [
                { "type": "spd", "chip": "spd5118", "addr_7bit": 82, "label": "spd_dimm_a1" }
              ]
            }
          ]
        },
        {
          "type": "temp",
          "chip": "emc1412",
          "addr_7bit": 76,
          "label": "temp_board_0x4C"
        }
      ]
    }
  ]
}
```

### 3.4 JSON → TreeLocks 节点树自动展开

**算法：** 解析 BMC I2C JSON → 调用 `treelock_register_node()` 逐节点注册。

```
输入: BMC I2C JSON 文件路径
输出: TreeLocks 内部树索引完全构建

过程:
  1. 创建 ROOT 节点 (node_id=0, label="bmc")
  
  2. 遍历 bus_list:
     2.1 创建 BUS 节点 (parent=ROOT, label="busN")
     2.2 遍历该 bus 的 devices:
         2.2.1 如果 device.type == "mux":
               - 创建 MUX 节点 (parent=BUS)
               - 遍历 mux.channels:
                 - 创建 CHANNEL 节点 (parent=MUX, label="chN")
                 - 遍历 channel.devices:
                   - 创建 DEVICE 节点 (parent=CHANNEL)
         2.2.2 如果 device.type != "mux":
               - 创建 DEVICE 节点 (parent=BUS)  // 直连设备，无 MUX

复杂度: O(n), n = 设备总数
```

### 3.5 拓扑自动发现（可选扩展）

```c
/**
 * 从 /sys/bus/i2c/devices 和 /dev/i2c-N 自动探测 BMC I2C 拓扑
 *
 * 通过 i2cdetect 扫描每个 bus，识别 MUX 和终端设备，自动构建树。
 * 适合开发/调试阶段，生产环境建议用 JSON 声明式配置。
 */
RET_CODE bmc_i2c_topology_discover(
    IN  treelock_t  *tl,
    OUT UINT_32     *device_count
);

// 实现策略:
//   1. 遍历 /dev/i2c-0 .. i2c-15
//   2. 对每个 bus 执行 i2cdetect -y <N>
//   3. 对 UU 地址（驱动占用）和数字地址分别处理
//   4. 对 MUX (pca954x, i2c-mux) 设备:
//      - 读取其通道数
//      - 递归探测每个通道后面的设备
//   5. 调用 treelock_register_node() 注册所有发现的节点
```

---

## 4. I2C 操作锁语义

### 4.1 典型 I2C 操作 → 锁模式映射

| BMC I2C 操作 | 所需锁模式 | 锁路径 | 自动派生的祖先锁 |
|:--|:--|:--|:--|
| **读 Sensor 值** (1-2 字节) | `S` | `/bus0/mux_0x70/ch0/temp_inlet` | ROOT(IS) → bus0(IS) → mux_0x70(IS) |
| **读 FRU EEPROM** (256 字节) | `S` | `/bus0/mux_0x70/ch1/fru_mb` | ROOT(IS) → bus0(IS) → mux_0x70(IS) |
| **写 FRU EEPROM** | `X` | `/bus0/mux_0x70/ch1/fru_mb` | ROOT(IX) → bus0(IX) → mux_0x70(IX) |
| **PMBus 多帧读写** (VOUT_COMMAND) | `X` | `/bus0/mux_0x70/ch2/vrm_vcore` | ROOT(IX) → bus0(IX) → mux_0x70(IX) |
| **扫描整条 Bus** | `S` | `/bus0` | ROOT(IS) → bus0(S) |
| **MUX 通道探测** | `IX` | `/bus0/mux_0x70` | ROOT(IX) → bus0(IX) → mux_0x70(IX) |
| **热插拔处理** (扫描新设备) | `X` | `/bus0/mux_0x70` | ROOT(IX) → bus0(IX) → mux_0x70(X) |
| **SPD 读取** (512 字节 EEPROM) | `S` | `/bus0/mux_0x71/ch0/spd_dimm_a0` | ROOT(IS) → bus0(IS) → mux_0x71(IS) |

### 4.2 并发安全示例

**场景：** 三个 daemon 同时访问同一个 MUX 的不同通道

```
时间 ──────────────────────────────────────────────────────►

phosphor-hwmon: lock_path("/bus0/mux_0x70/ch0/temp", S)
                → ROOT(IS)✅  bus0(IS)✅  mux_0x70(IS)✅  temp(S)✅
                → 读温度值... (10ms)
                → unlock_path(...)

ipmid:          lock_path("/bus0/mux_0x70/ch1/fru", X)      ← S 与 X 在 mux_0x70 上冲突！
                → ROOT(IS)✅  bus0(IS)✅  mux_0x70(IX)❌  ← 等待 hwmon 释放...

entity-manager: lock_path("/bus0/mux_0x70/ch2/vrm", S)
                → ROOT(IS)✅  bus0(IS)✅  mux_0x70(IS)✅  ← S 与 IS 兼容，与 hwmon 并行！
                → 读 VRM... (15ms)
                → unlock_path(...)
```

**关键点：**
- `hwmon`(S on ch0) 与 `entity-manager`(S on ch2) **可以并发**，因为它们都在 mux 上只有 IS
- `ipmid`(X on ch1) **必须等待**，因为 X 要求 mux 上有 IX，与 hwmon 的 IS **不兼容**
- **并发读不阻塞，写操作排队** — 这正是 I2C 的正确语义

### 4.3 MUX 通道切换的原子性保证

```
┌──────────────────────────────────────────────────────────┐
│  不加锁的 MUX 访问（危险）                                  │
│                                                          │
│  1. ioctl(mux_fd, I2C_MUX_SELECT, ch1)   ← 切到 ch1      │
│  2. [其他线程此时切到 ch2]                  ← 被抢走！     │
│  3. read(dev_fd)                          ← 读的是 ch2 的数据！│
│                                                          │
├──────────────────────────────────────────────────────────┤
│  加锁后的 MUX 访问（安全）                                  │
│                                                          │
│  1. lock_path("/bus0/mux_0x70/ch1/fru", S)               │
│     → mux 上自动获得 IS                                  │
│     → 其他线程尝试写 → mux 上需要 IX → 与 IS 冲突 → 等待   │
│  2. ioctl(mux_fd, I2C_MUX_SELECT, ch1)   ← 安全切通道    │
│  3. read(dev_fd)                          ← 正确数据      │
│  4. unlock_path("/bus0/mux_0x70/ch1/fru")                │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

意向锁的妙处：**你不需要显式锁 MUX。** 锁 path 到设备时，MUX 自动被 IS/IX 锁保护，任何试图切 MUX 的操作都必须经过 MUX 节点的兼容性检查。

---

## 5. 架构设计

### 5.1 模块分解

```
modules/
├── treelock_core/          # 现有：锁协议引擎
├── treelock_tree/          # 现有：树结构管理
├── treelock_log/           # 现有：日志
├── cjson/                  # 现有：JSON 解析
│
├── bmc_i2c_lock/           # ★ 新增：BMC I2C 锁封装层
│   ├── include/
│   │   └── bmc_i2c_lock.h       # 公共 API
│   └── src/
│       ├── bmc_i2c_internal.h   # 内部数据结构
│       ├── bmc_i2c_topology.c   # I2C JSON → TreeLocks 树 转换
│       ├── bmc_i2c_ops.c        # 带锁的 I2C 读写操作
│       ├── bmc_i2c_discover.c   # (可选) I2C 拓扑自动探测
│       └── bmc_i2c_shm.c        # Phase 2: 共享内存锁表
│
└── CMakeLists.txt           # +add_subdirectory(bmc_i2c_lock)
```

### 5.2 编译依赖

```
bmc_i2c_lock ──→ treelock_core ──→ treelock_log
     │                 │                 │
     │                 └──→ pthread       │
     ├──→ treelock_tree                  │
     ├──→ cjson                          │
     └──→ libi2c (Linux i2c-dev)  ← 仅 I2C 操作需要
```

### 5.3 数据流

```
                          ┌────────────────────────────┐
  Application (daemon)    │   bmc_i2c_lock 模块         │     TreeLocks Core
                          │                             │
  ──────────────────────► │ bmc_i2c_read(tl, path, ...) │
                          │   │                         │
                          │   ├─ 1. lock_path(path, S)──┼──► 沿路径加 IS/IS/S
                          │   │                         │     协议校验 (父节点检查)
                          │   │                         │     兼容性检查 (矩阵查表)
                          │   │                         │
                          │   ├─ 2. 定位设备:             │
                          │   │   path → bus/mux/ch/addr│
                          │   │                         │
                          │   ├─ 3. MUX 导航:            │
                          │   │   如果设备在 MUX 后,      │
                          │   │   ioctl(MUX_SELECT)      │
                          │   │                         │
                          │   ├─ 4. I2C 传输:            │
                          │   │   ioctl(I2C_RDWR, msg)  │
                          │   │                         │
                          │   ├─ 5. unlock_path(path) ──┼──► 自底向上释放
                          │   │                         │
                          │   └─ return result          │
  ◄────────────────────── │                             │
                          └────────────────────────────┘
```

### 5.4 共享内存锁表（Phase 2 跨进程方案）

```
┌─────────────────────────────────────────────────────────┐
│  共享内存区域 (/dev/shm/treelock_i2c)                    │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │  Header: magic, version, size, mutex, condition │   │
│  ├─────────────────────────────────────────────────┤   │
│  │  Lock Table: treelock_node_t[] hash buckets      │   │
│  │  Held Locks:  per-client held list                │   │
│  │  Client Registry: client_id → state               │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  所有 daemon 进程 mmap() 同一块共享内存                   │
│  使用 pthread_mutexattr_setpshared() 实现跨进程互斥       │
│  条件变量用 pthread_condattr_setpshared() 支持跨进程唤醒   │
│                                                         │
│  优势: 零拷贝、延迟 < 1μs、无需额外服务进程                │
│  局限: 单 BMC 芯片内有效（不跨网络）                      │
└─────────────────────────────────────────────────────────┘
```

---

## 6. API 设计

### 6.1 拓扑加载 API

```c
/* bmc_i2c_lock.h — BMC I2C 锁封装层公共 API */

/**
 * 函数名称：bmc_i2c_load_topology
 *
 * 功能描述：从 BMC I2C JSON 配置文件加载 I2C 拓扑到 TreeLocks 树结构。
 *           自动展开 bus → mux → channel → device 的层级关系，
 *           调用 treelock_register_node() 逐节点注册。
 *
 * @param[IN] tl       - TreeLocks 实例
 * @param[IN] json_file - BMC I2C 拓扑 JSON 文件路径
 *
 * @return TREELOCK_OK 加载成功
 * @return TREELOCK_ERR_INVAL JSON 解析失败或拓扑无效
 */
RET_CODE bmc_i2c_load_topology(
    IN treelock_t *tl,
    IN CSTR_PTR    json_file
);

/**
 * 函数名称：bmc_i2c_load_topology_from_string
 *
 * 功能描述：从 JSON 字符串加载拓扑（适用于编译进固件的静态配置）
 *
 * @param[IN] tl          - TreeLocks 实例
 * @param[IN] json_string - JSON 字符串
 *
 * @return TREELOCK_OK 加载成功
 */
RET_CODE bmc_i2c_load_topology_from_string(
    IN treelock_t *tl,
    IN CSTR_PTR    json_string
);

/**
 * 函数名称：bmc_i2c_lookup_device
 *
 * 功能描述：根据人类可读的标签查找设备的 node_id
 *
 * @param[IN]  tl       - TreeLocks 实例
 * @param[IN]  label    - 设备标签（如 "temp_inlet_0x1F"）
 * @param[OUT] node_id  - 输出的 node_id
 *
 * @return TREELOCK_OK 找到
 * @return TREELOCK_ERR_INVAL 标签不存在
 */
RET_CODE bmc_i2c_lookup_device(
    IN  treelock_t         *tl,
    IN  CSTR_PTR            label,
    OUT treelock_node_id_t *node_id
);
```

### 6.2 I2C 操作 API（带锁保护）

```c
/**
 * 函数名称：bmc_i2c_read
 *
 * 功能描述：带 TreeLocks 保护的 I2C 字节读取。
 *           自动按路径加 S 锁（读操作），完成后释放。
 *
 * @param[IN]  tl       - TreeLocks 实例
 * @param[IN]  path     - 设备路径（如 "/bus0/mux_0x70/ch0/temp_inlet"）
 * @param[IN]  reg      - 寄存器地址
 * @param[OUT] buf      - 输出缓冲区
 * @param[IN]  len      - 读取长度
 *
 * @return 成功返回读取的字节数，失败返回负值错误码
 */
INT_32 bmc_i2c_read(
    IN  treelock_t *tl,
    IN  CSTR_PTR    path,
    IN  UINT_8      reg,
    OUT UINT_8     *buf,
    IN  UINT_32     len
);

/**
 * 函数名称：bmc_i2c_write
 *
 * 功能描述：带 TreeLocks 保护的 I2C 字节写入。
 *           自动按路径加 X 锁（写操作），完成后释放。
 *
 * @param[IN] tl    - TreeLocks 实例
 * @param[IN] path  - 设备路径
 * @param[IN] reg   - 寄存器地址
 * @param[IN] buf   - 写入数据
 * @param[IN] len   - 写入长度
 *
 * @return 成功返回写入的字节数，失败返回负值错误码
 */
INT_32 bmc_i2c_write(
    IN treelock_t *tl,
    IN CSTR_PTR    path,
    IN UINT_8      reg,
    IN UINT_8     *buf,
    IN UINT_32     len
);

/**
 * 函数名称：bmc_i2c_smbus_read_word
 *
 * 功能描述：SMBus 读 Word 操作（带锁保护）
 *
 * @param[IN]  tl    - TreeLocks 实例
 * @param[IN]  path  - 设备路径
 * @param[IN]  cmd   - SMBus 命令码
 * @param[OUT] val   - 输出的 16 位值
 *
 * @return TREELOCK_OK 成功
 */
RET_CODE bmc_i2c_smbus_read_word(
    IN  treelock_t *tl,
    IN  CSTR_PTR    path,
    IN  UINT_8      cmd,
    OUT UINT_16    *val
);

/**
 * 函数名称：bmc_i2c_smbus_write_word
 *
 * 功能描述：SMBus 写 Word 操作（带锁保护）
 *
 * @param[IN] tl    - TreeLocks 实例
 * @param[IN] path  - 设备路径
 * @param[IN] cmd   - SMBus 命令码
 * @param[IN] val   - 16 位值
 *
 * @return TREELOCK_OK 成功
 */
RET_CODE bmc_i2c_smbus_write_word(
    IN treelock_t *tl,
    IN CSTR_PTR    path,
    IN UINT_8      cmd,
    IN UINT_16     val
);
```

### 6.3 手动锁 API（用于复杂场景）

当需要在一个锁保护下执行多个 I2C 操作时（如 PMBus 的多寄存器事务），提供手动锁/解锁：

```c
/**
 * 函数名称：bmc_i2c_lock_device
 *
 * 功能描述：手动锁定 I2C 设备（用于多操作事务）
 *
 * @param[IN] tl    - TreeLocks 实例
 * @param[IN] path  - 设备路径
 * @param[IN] mode  - 锁模式（TREELOCK_S 读 / TREELOCK_X 写）
 *
 * @return TREELOCK_OK 加锁成功
 */
RET_CODE bmc_i2c_lock_device(
    IN treelock_t     *tl,
    IN CSTR_PTR        path,
    IN treelock_mode_t mode
);

/**
 * 函数名称：bmc_i2c_unlock_device
 *
 * 功能描述：手动解锁 I2C 设备
 *
 * @param[IN] tl    - TreeLocks 实例
 * @param[IN] path  - 设备路径
 *
 * @return TREELOCK_OK 解锁成功
 */
RET_CODE bmc_i2c_unlock_device(
    IN treelock_t *tl,
    IN CSTR_PTR    path
);

/**
 * 函数名称：bmc_i2c_transfer_raw
 *
 * 功能描述：在已加锁的前提下，执行原始 I2C 传输
 *           （调用者必须先锁设备）
 *
 * @param[IN] path     - 设备路径
 * @param[IN] msgs     - i2c_msg 数组
 * @param[IN] msg_count - 消息数量
 *
 * @return 成功返回处理的消息数，失败返回负值
 */
INT_32 bmc_i2c_transfer_raw(
    IN CSTR_PTR               path,
    IN struct i2c_msg        *msgs,
    IN UINT_32                msg_count
);
```

### 6.4 使用示例

```c
#include "treelock.h"
#include "treelock_tree.h"
#include "bmc_i2c_lock.h"

// ── 示例 1: 简单读 sensor ──
void read_inlet_temp(treelock_t *tl)
{
    UINT_8 buf[2];
    INT_32 ret = bmc_i2c_read(tl, "/bus0/mux_0x70/ch0/temp_inlet",
                              0x00, buf, 2);
    if (ret == 2) {
        FLOAT_32 temp = (FLOAT_32)((buf[0] << 8) | buf[1]) / 256.0f;
        TREELOCK_LOG_INFO("SENSOR", "inlet=%0.1f°C", temp);
    }
}

// ── 示例 2: 批量写 FRU EEPROM (一页 16 字节) ──
void write_fru_data(treelock_t *tl, UINT_8 *data, UINT_32 len)
{
    // PMBus 风格：多寄存器更新，用 X 锁保护整段事务
    bmc_i2c_lock_device(tl, "/bus0/mux_0x70/ch1/fru_mb", TREELOCK_X);

    // 1. 写地址指针
    bmc_i2c_transfer_raw("/bus0/mux_0x70/ch1/fru_mb", ...);
    // 2. 写数据
    for (UINT_32 i = 0; i < len; i += 16) {
        bmc_i2c_transfer_raw("/bus0/mux_0x70/ch1/fru_mb", ...);
    }

    bmc_i2c_unlock_device(tl, "/bus0/mux_0x70/ch1/fru_mb");
}

// ── 示例 3: daemon 初始化 ──
int main(int argc, char *argv[])
{
    treelock_t *tl = treelock_create(NULL);

    // 1. 加载 I2C 拓扑
    bmc_i2c_load_topology(tl, "/etc/bmc/i2c_topology.json");

    // 2. 所有 I2C 操作自动获得锁保护
    read_inlet_temp(tl);
    write_fru_data(tl, fru_buffer, 256);

    treelock_destroy(tl);
    return 0;
}
```

---

## 7. 与现有代码的集成

### 7.1 对 TreeLocks 核心的改动

**改动量：接近于零。** BMC I2C Lock 模块是 TreeLocks 的**上层消费者**，不修改核心协议。

| 模块 | 改动 | 说明 |
|:--|:--|:--|
| `treelock_core` | **无** | 锁协议、锁表、客户端 API 完全不变 |
| `treelock_tree` | **无** | 树加载、路径加锁 API 完全复用 |
| `treelock_log` | **无** | 日志宏直接使用 |
| `cjson` | **无** | JSON 解析复用现有 cjson 模块 |
| `bmc_i2c_lock` | **全新增** | ~800 行新增代码 |

### 7.2 Phase 2 需要的核心改动

当跨进程协调需要时，对 `treelock_core` 增加共享内存后端：

```c
// treelock.h 新增创建方式
treelock_t *treelock_create_shm(
    IN CSTR_PTR shm_path    // 共享内存路径，如 "/dev/shm/treelock_i2c"
);

// 内部: treelock_s 新增字段
struct treelock_s {
    // ... 现有字段 ...
    INT_32     use_shm;       // 是否使用共享内存模式
    CSTR_PTR   shm_path;      // 共享内存路径
    PTR_VOID   shm_ptr;       // mmap 指针
};
```

**改动量：** `client.c` 增加 ~80 行，`lock_table.c` 增加 ~60 行（替换 mutex 初始化方式）。

### 7.3 与 OpenBMC 的集成点

```
OpenBMC 仓库中需要修改的地方：

1. phosphor-hwmon
   ├── hwmon_main.cpp    ← 替换 i2c_smbus_read_word_data() 为 bmc_i2c_smbus_read_word()
   └── meson.build       ← 添加 libtreelock、libbmc_i2c_lock 依赖

2. entity-manager
   ├── perform_scan.cpp  ← 在 FRU 扫描前加锁
   └── meson.build

3. ipmid
   ├── storage_commands.cpp ← IPMI FRU 读写命令加锁
   └── meson.build
```

---

## 8. 实施路线

### 8.1 阶段划分

```
阶段 A (本迭代)          阶段 B (下迭代)           阶段 C (Phase 2)
████████████████████░░  ░░░░░░░░░░░░░░░░░░░░░░░  ░░░░░░░░░░░░░░░░░░
    bmc_i2c_lock 层         跨进程共享内存              生产部署
```

### 8.2 详细任务分解

| 步骤 | 内容 | 文件 | 预估工时 | 依赖 |
|:--|:--|:--|:--|:--|
| **A.1** | I2C 节点 ID 编码实现 | `bmc_i2c_internal.h` | 1h | 无 |
| **A.2** | BMC I2C JSON → TreeLocks 树转换 | `bmc_i2c_topology.c` | 4h | A.1, treelock_tree |
| **A.3** | 带锁的 I2C 读写封装 | `bmc_i2c_ops.c` | 4h | A.2 |
| **A.4** | SMBus 协议封装 | `bmc_i2c_ops.c` | 2h | A.3 |
| **A.5** | 公共 API 头文件 | `bmc_i2c_lock.h` | 1h | A.3 |
| **A.6** | CMake 构建集成 | `CMakeLists.txt` | 1h | A.5 |
| **A.7** | 单元测试 (JSON 拓扑 → 树结构) | `tests/test_bmc_i2c.cc` | 3h | A.2 |
| **A.8** | 单元测试 (I2C 操作锁语义) | `tests/test_bmc_i2c.cc` | 3h | A.3 |
| **A.9** | 示例程序 | `examples/bmc_i2c_demo.c` | 2h | A.5 |
| **A.10** | 拓扑自动探测 (可选) | `bmc_i2c_discover.c` | 4h | A.2 |
| **A.11** | 文档编写 | `docs/BMC-I2C-方案.md` | 2h | — |
| | | **合计** | **27h** (~3.5 天) | |

### 8.3 阶段 B（跨进程共享内存）

| 步骤 | 内容 | 预估工时 |
|:--|:--|:--|
| B.1 | `treelock_create_shm()` 共享内存创建 | 4h |
| B.2 | 跨进程 mutex/condvar (PTHREAD_PROCESS_SHARED) | 3h |
| B.3 | 客户端注册/心跳/超时踢出 | 4h |
| B.4 | 租约机制 (Iteration 1.6) → 共享内存场景适配 | 3h |
| B.5 | 多进程并发测试 (3 个 daemon 模拟) | 4h |
| B.6 | crash 恢复测试 (SIGKILL → 锁自动回收) | 3h |
| | **合计** | **21h** |

### 8.4 阶段 C（OpenBMC 集成 + 生产部署）

| 步骤 | 内容 | 预估工时 |
|:--|:--|:--|
| C.1 | OpenBMC bitbake recipe 编写 | 4h |
| C.2 | phosphor-hwmon 适配 | 4h |
| C.3 | entity-manager 适配 | 4h |
| C.4 | ipmid 适配 | 3h |
| C.5 | QEMU 仿真环境测试 | 6h |
| C.6 | 真实硬件 (AST2600 EVB) 验证 | 8h |
| C.7 | 性能基准 (对比无锁方案的开销) | 4h |
| | **合计** | **33h** |

---

## 9. 可行性评估

### 9.1 技术可行性 — ⭐⭐⭐⭐⭐

| 维度 | 评价 | 依据 |
|:--|:--|:--|
| **核心协议匹配度** | 极高 | Gray 6-mode 协议的树状层级天然对应 I2C 拓扑的 Bus→MUX→Ch→Device 层级 |
| **代码改动范围** | 极小 | TreeLocks 核心零改动；新增模块 ~800 行，完全独立 |
| **向下兼容** | 完全兼容 | 所有现有 API 不变，不加拓扑文件时行为不变 |
| **性能开销** | 可忽略 | 共享内存单机版：lock_path 操作 O(d)树深度 hash 查找，d≤5，< 1μs；I2C 传输本身 100–400kHz 是绝对瓶颈 |
| **依赖引入** | 极轻 | 仅依赖 Linux i2c-dev 头文件，无新第三方库 |
| **构建系统** | 简单 | CMake 加一个 `add_subdirectory()`，支持条件编译 (`-DTREELOCK_BMC_I2C=ON`) |

### 9.2 I2C 操作耗时分析（锁开销 vs 传输开销）

```
操作耗时分解 (以 100kHz I2C 读 2 字节为例):

  锁获取 (lock_path, d=4)     ~0.5 μs   ← 1 次 hash × 4 层
  ioctl MUX_SELECT            ~100 μs   ← I2C 写 MUX 控制寄存器
  I2C START + addr + reg      ~200 μs   ← 20 bits × 10μs/bit
  I2C REPEATED START + read   ~200 μs
  I2C STOP                    ~10 μs
  锁释放 (unlock_path)         ~0.3 μs
  ─────────────────────────────────────
  总耗时                       ~511 μs

  锁开销占比: 0.8 / 511 ≈ 0.15%  ← 完全可忽略
```

**结论：** I2C 的物理传输速度（100–400 kHz）是整个操作的绝对瓶颈。TreeLocks 的内存级锁操作（纳秒级）引入的开销可忽略不计。

### 9.3 与现有方案的对比

| 方案 | 跨进程 | MUX 原子性 | 死锁防护 | 读并行 | 部署复杂度 |
|:--|:--:|:--:|:--:|:--:|:--|
| 无保护（现状） | ❌ | ❌ | ❌ | — | — |
| 全局 I2C mutex (单 bus) | ❌ | ❌ | ✅ | ❌ (全串行) | 低 |
| `flock()` 文件锁 | ✅ | ❌ | ❌ | ❌ | 低 |
| 内核 i2c-mux 子系统 | ❌ | 单层 ✅ | ✅ | ✅ | 零 |
| **TreeLocks (本次方案)** | ✅ (Phase 2) | ✅ (任意层级) | ✅ (协议级) | ✅ (S/S 兼容) | 中 |
| ZK 分布式锁 | ✅ | ✅ | ✅ | ✅ | 高 |

### 9.4 结论

**方案可行，建议推进。** TreeLocks 的树状意向锁协议能够完美解决 BMC I2C 拓扑的并发访问问题。Phase 1（单机多线程）即可覆盖相当一部分场景（单 daemon 多线程），Phase 2（共享内存）实现后将覆盖全部 BMC 用户态 I2C 协调需求。

---

## 10. 风险与缓解

| 风险 | 概率 | 影响 | 缓解措施 |
|:--|:--|:--|:--|
| **性能回归** — 每次 I2C 读都加锁导致延迟增加 | 低 | 中 | 锁开销 < 1μs vs I2C 传输 > 200μs，占比 < 0.5%；基准测试验证 |
| **死锁** — 锁协议实现 bug 导致 daemon hang | 低 | 高 | TreeLocks 协议层已通过 30 个 protocol 测试 + 27 个并发测试；增加 I2C 专用死锁测试 |
| **daemon 崩溃不释放锁** | 中 | 高 | Phase 2 租约机制（心脏跳超时自动释放）；Phase 1 用 `treelock_destroy()` 注册 `atexit()` handler |
| **共享内存损坏** — 非正常重启导致共享内存数据不一致 | 低 | 中 | 共享内存 magic number + version 校验；损坏时自动重建 |
| **内核 i2c-mux 与用户态锁的交互** — 两套机制产生意外行为 | 低 | 中 | TreeLocks 作为**应用层**协调器，不影响内核驱动；内核 `i2c_lock_adapter()` 保护单次 `i2c_transfer()` 原子性，TreeLocks 保护**多次传输的事务**原子性 — 职责不重叠 |
| **JSON 拓扑配置错误** — 配置与真实硬件不匹配 | 中 | 低 | 拓扑自动探测（`bmc_i2c_discover`）作为交叉验证；树校验（ID 唯一/单根/无环）在加载时执行 |
| **MUX 通道建模过于复杂** — 5 层深度影响锁获取性能 | 低 | 极低 | 锁获取 O(d)，d=树深度，I2C 拓扑 d≤5，hash 查找开销可忽略 |

---

## 11. 参考资料

### 11.1 协议基础

- Gray, J. et al. (1976). *Granularity of Locks and Degrees of Consistency in a Shared Data Base*. IBM System R. — TreeLocks 锁协议的理论基础
- Mohan, C. et al. (1992). *ARIES/IM: An Efficient and High Concurrency Index Management Method*. IBM Almaden. — 意向锁在树索引中的实际应用

### 11.2 I2C / BMC 相关

- Linux Kernel Documentation. *I2C MUX (PCA954x) Subsystem*. https://docs.kernel.org/i2c/muxes/i2c-mux-pca954x.html
- Linux Kernel Documentation. *Implementing I2C device drivers*. https://docs.kernel.org/i2c/writing-clients.html
- OpenBMC Project. *Entity Manager Design*. https://github.com/openbmc/entity-manager
- OpenBMC Project. *phosphor-hwmon*. https://github.com/openbmc/phosphor-hwmon
- NXP Semiconductors. *PCA9548A — 8-channel I2C-bus switch with reset*. Datasheet.
- SMBus Specification v3.2. *System Management Bus (SMBus)*.

### 11.3 共享内存并发

- Stevens, W. R. & Rago, S. A. (2013). *Advanced Programming in the UNIX Environment (3rd ed.)*. Chapter 15: Interprocess Communication — Shared Memory.
- Kerrisk, M. (2010). *The Linux Programming Interface*. Chapter 54: POSIX Shared Memory. — `shm_open` / `mmap` / `PTHREAD_PROCESS_SHARED`

---

## 12. 附录

### 12.1 节点 ID 编码完整示例

见 [§3.1 节点 ID 编码方案](#31-节点-id-编码方案)。

### 12.2 完整 I2C 拓扑 JSON 示例

见 [§3.3 拓扑 JSON 定义](#33-拓扑-json-定义)。

### 12.3 与 TreeLocks 现有 API 的映射关系

| bmc_i2c_lock API | 内部调用的 TreeLocks API |
|:--|:--|
| `bmc_i2c_load_topology(tl, file)` | `cJSON_Parse(file)` → 遍历 bus_list → `treelock_register_node(tl, id, parent, label)` × N |
| `bmc_i2c_read(tl, path, reg, buf, len)` | `treelock_lock_path(tl, path, TREELOCK_S)` → `ioctl(I2C_RDWR)` → `treelock_unlock_path(tl, path)` |
| `bmc_i2c_write(tl, path, reg, buf, len)` | `treelock_lock_path(tl, path, TREELOCK_X)` → `ioctl(I2C_RDWR)` → `treelock_unlock_path(tl, path)` |
| `bmc_i2c_lock_device(tl, path, mode)` | `treelock_lock_path(tl, path, mode)` |
| `bmc_i2c_unlock_device(tl, path)` | `treelock_unlock_path(tl, path)` |
| `bmc_i2c_lookup_device(tl, label, &id)` | `treelock_lookup_path(tl, label, &id)` |

### 12.4 OpenBMC 集成示例

```cpp
// phosphor-hwmon 改造示例 (hwmon_main.cpp)
// 原有:
//   int ret = i2c_smbus_read_word_data(fd, cmd);
//
// 改造后:

#include "bmc_i2c_lock.h"

static treelock_t *g_i2c_lock = nullptr;  // 全局锁实例

int main(int argc, char **argv)
{
    // 初始化 TreeLocks (共享内存模式)
    g_i2c_lock = treelock_create_shm("/dev/shm/treelock_i2c");
    bmc_i2c_load_topology(g_i2c_lock, "/usr/share/bmc/i2c_topology.json");

    // ... 原有逻辑，但 I2C 读写改用 bmc_i2c_* API ...
}

// Sensor 读取函数
double read_temp_sensor(const std::string &dev_path, uint8_t reg)
{
    uint16_t val;
    int ret = bmc_i2c_smbus_read_word(g_i2c_lock, dev_path.c_str(),
                                       reg, &val);
    if (ret == TREELOCK_OK) {
        return val / 256.0;
    }
    return NAN;
}
```

---

> **下一步**：确认方案可行后，从 **A.1 (I2C 节点 ID 编码)** + **A.2 (JSON → TreeLocks 转换)** 开始实现。Phase 1 单机多线程版预计 3.5 天可交付，Phase 2 跨进程共享内存版再加 2.5 天。
