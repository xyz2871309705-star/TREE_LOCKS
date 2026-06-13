/*
 * TreeLocks - 基础类型封装与参数方向宏
 *
 * 此头文件定义了项目统一的类型系统和参数传递语义。
 * 所有模块必须使用这些宏，禁止直接使用原始 C 类型。
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#ifndef TREELOCK_TYPES_H
#define TREELOCK_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 参数方向宏
 *
 * 用于函数声明和注释中表明参数的传递语义：
 *   IN     — 只入不出：调用者传入值，函数只读不写
 *   OUT    — 只出不入：函数写入结果，调用者读取
 *   INOUT  — 入且出：调用者传入初始值，函数可能修改
 * ========================================================================= */

#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

#ifndef INOUT
#define INOUT
#endif

/* =========================================================================
 * 基础数值类型封装
 *
 * 命名规则：{SIGNED}_{BIT_WIDTH}
 *   UINT  — 无符号整数
 *   INT   — 有符号整数
 *   FLOAT — 浮点数
 * ========================================================================= */

/* 8 位 */
typedef int8_t   INT_8;
typedef uint8_t  UINT_8;

/* 16 位 */
typedef int16_t  INT_16;
typedef uint16_t UINT_16;

/* 32 位 */
typedef int32_t  INT_32;
typedef uint32_t UINT_32;

/* 64 位 */
typedef int64_t  INT_64;
typedef uint64_t UINT_64;

/* 布尔类型 */
typedef INT_32   BOOL;

#ifndef TRUE
#define TRUE  (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

/* 字符类型 */
typedef char     CHAR;

/* 空类型（用于函数指针返回值和参数） */
#ifndef VOID
#define VOID void
#endif

/* 浮点数 */
typedef float    FLOAT_32;
typedef double   FLOAT_64;

/* 指针类型别名 */
#ifndef NULL
#define NULL ((VOID *)0)
#endif

/* 无类型指针（通用数据传递） */
typedef VOID*    PTR_VOID;

/* 常量指针 */
typedef const CHAR*  CSTR_PTR;
typedef const VOID*  CVOID_PTR;

/* 函数指针返回值类型 */
typedef INT_32    RET_CODE;

/* 字符串长度类型 */
typedef UINT_32   STR_LEN;

/* 缓冲区大小类型 */
typedef UINT_64   BUF_SIZE;

/* =========================================================================
 * 节点 ID 类型（项目特化）
 * ========================================================================= */

typedef UINT_64   TREE_NODE_ID;

/* =========================================================================
 * 时间戳类型（毫秒）
 * ========================================================================= */

typedef INT_64    TIMESTAMP_MS;

/* =========================================================================
 * 常用比较结果
 * ========================================================================= */

#define EQUAL     (0)
#define NOT_EQUAL (1)

/* =========================================================================
 * 工具宏
 * ========================================================================= */

/** 获取数组元素个数 */
#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

/** 安全释放内存并置空 */
#define SAFE_FREE(ptr) do { \
    if ((ptr) != NULL) { \
        free((ptr)); \
        (ptr) = NULL; \
    } \
} while (0)

/** 忽略未使用参数 */
#define UNUSED_PARAM(p) ((VOID)(p))

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_TYPES_H */
