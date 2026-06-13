/*
 * TreeLocks - 平台抽象层
 *
 * 统一 Windows / Linux 平台差异，提供：
 *   - DLL 导入导出宏
 *   - 时间函数封装
 *   - 线程局部存储
 *
 * 版本: 0.1.0
 * 日期: 2026-06-13
 */

#ifndef TREELOCK_PLATFORM_H
#define TREELOCK_PLATFORM_H

#include "treelock_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 平台检测
 * ========================================================================= */

#if defined(_WIN32) || defined(_WIN64)
#  define TREELOCK_PLATFORM_WINDOWS  1
#  if defined(_MSC_VER)
#    define TREELOCK_COMPILER_MSVC   1
#  elif defined(__MINGW32__) || defined(__MINGW64__)
#    define TREELOCK_COMPILER_MINGW  1
#  endif
#elif defined(__linux__)
#  define TREELOCK_PLATFORM_LINUX    1
#  if defined(__GNUC__)
#    define TREELOCK_COMPILER_GCC    1
#  endif
#elif defined(__APPLE__)
#  define TREELOCK_PLATFORM_MACOS    1
#  define TREELOCK_COMPILER_CLANG    1
#else
#  error "Unsupported platform"
#endif

/* =========================================================================
 * 符号导出 / 导入宏（跨平台动态库）
 * ========================================================================= */

#if defined(TREELOCK_PLATFORM_WINDOWS)
#  if defined(TREELOCK_STATIC)
#    define TREELOCK_API
#  elif defined(TREELOCK_BUILD_DLL)
#    define TREELOCK_API  __declspec(dllexport)
#  else
#    define TREELOCK_API  __declspec(dllimport)
#  endif
#else
#  if defined(TREELOCK_STATIC) || !defined(TREELOCK_BUILD_DLL)
#    define TREELOCK_API
#  else
#    define TREELOCK_API  __attribute__((visibility("default")))
#  endif
#endif

/* =========================================================================
 * 线程局部存储
 * ========================================================================= */

#if __STDC_VERSION__ >= 201112L && !defined(TREELOCK_COMPILER_MSVC)
#  define TREELOCK_THREAD_LOCAL  _Thread_local
#elif defined(TREELOCK_COMPILER_MSVC)
#  define TREELOCK_THREAD_LOCAL  __declspec(thread)
#elif defined(__GNUC__)
#  define TREELOCK_THREAD_LOCAL  __thread
#else
#  define TREELOCK_THREAD_LOCAL
#endif

/* =========================================================================
 * 内联函数
 * ========================================================================= */

#if defined(TREELOCK_COMPILER_MSVC)
#  define TREELOCK_INLINE  static __inline
#else
#  define TREELOCK_INLINE  static inline
#endif

/* =========================================================================
 * 平台特定头文件
 *
 * 注意：不使用 <windows.h>（会与 treelock_types.h 的类型宏冲突）
 *       改用轻量级 C 运行库头文件：
 *         Windows: <sys/timeb.h>  ( _ftime64_s )
 *         POSIX:   <sys/time.h>   ( gettimeofday / localtime_r )
 * ========================================================================= */

#ifdef TREELOCK_PLATFORM_WINDOWS
#  include <sys/timeb.h>   /* _ftime64_s, struct __timeb64 */
#  include <time.h>         /* localtime_s, strftime */
#else
#  include <sys/time.h>     /* gettimeofday */
#  include <time.h>         /* localtime_r, strftime */
#endif
#include <string.h>         /* strlen */
#include <stdio.h>          /* snprintf */

/* =========================================================================
 * 跨平台时间函数
 * ========================================================================= */

/**
 * 函数名称：treelock_platform_time_ms
 *
 * 功能描述：获取当前 Unix 毫秒时间戳
 *
 *          Windows: _ftime64_s（轻量级，无需 Windows.h）
 *          POSIX:   gettimeofday
 *
 * @return 毫秒级 Unix 时间戳
 */
TREELOCK_INLINE TIMESTAMP_MS treelock_platform_time_ms(VOID)
{
#ifdef TREELOCK_PLATFORM_WINDOWS
    struct __timeb64 tb;
    _ftime64_s(&tb);
    return (TIMESTAMP_MS)tb.time * 1000 + (TIMESTAMP_MS)tb.millitm;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (TIMESTAMP_MS)tv.tv_sec * 1000 + (TIMESTAMP_MS)(tv.tv_usec / 1000);
#endif
}

/**
 * 函数名称：treelock_platform_local_time
 *
 * 功能描述：获取本地时间并格式化为字符串
 *
 *          格式: "YYYY-MM-DD HH:MM:SS.mmm"
 *
 * @param[OUT] buf      - 输出缓冲区（>= 24 字节）
 * @param[IN]  buf_size - 缓冲区大小
 */
TREELOCK_INLINE VOID treelock_platform_local_time(
    OUT CHAR   *buf,
    IN  UINT_32 buf_size)
{
    if (buf == NULL || buf_size < 24) {
        return;
    }

#ifdef TREELOCK_PLATFORM_WINDOWS
    {
        struct __timeb64 tb;
        struct tm        tm_info;
        _ftime64_s(&tb);
        _localtime64_s(&tm_info, &tb.time);
        strftime(buf, (size_t)buf_size, "%Y-%m-%d %H:%M:%S", &tm_info);
        {
            UINT_32 len = (UINT_32)strlen(buf);
            snprintf(buf + len, (size_t)(buf_size - len),
                     ".%03hu", tb.millitm);
        }
    }
#else
    {
        struct timeval tv;
        struct tm      tm_info;

        gettimeofday(&tv, NULL);
        localtime_r(&tv.tv_sec, &tm_info);

        strftime(buf, (size_t)buf_size, "%Y-%m-%d %H:%M:%S", &tm_info);
        {
            UINT_32 len = (UINT_32)strlen(buf);
            snprintf(buf + len, (size_t)(buf_size - len),
                     ".%03ld", (long)(tv.tv_usec / 1000));
        }
    }
#endif
}

/* =========================================================================
 * 跨平台 sleep（毫秒）
 *
 * Windows: Sleep() 来自 <synchapi.h>，但通过 <time.h> 可能不可用。
 *          改用 nanosleep POSIX 兼容方式或 _sleep()。
 *          此处使用 C11 thrd_sleep 或简单的循环方式。
 *
 * 注：本模块当前未使用此函数，保留供后续使用。
 * ========================================================================= */

#ifdef __cplusplus
}
#endif

#endif /* TREELOCK_PLATFORM_H */
