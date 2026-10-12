#pragma once

#include <cstddef>
#include <cstring>
#include <cwchar>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <strings.h>
#include <cstdint>
#include <pthread.h>
#include <cstdarg>
#include <algorithm>

using BYTE = unsigned char;
#ifdef NULL
#undef NULL
#endif
#define NULL nullptr
using HFONT = void*;
struct GLYPHMETRICS { long gmBlackBoxX, gmBlackBoxY, gmptGlyphOriginX, gmptGlyphOriginY, gmCellIncX, gmCellIncY; };
struct TEXTMETRIC { long tmHeight = 0; };
#ifndef LF_FACESIZE
#define LF_FACESIZE 32
#endif

#ifndef __declspec
#define __declspec(...)
#endif

#ifndef WINAPI
#define WINAPI
#endif

typedef wchar_t WCHAR;
typedef char TCHAR;
typedef void* HWND;
typedef void* HMODULE;
typedef std::uint32_t DWORD;
typedef std::uint64_t DWORD64;
typedef const TCHAR* LPCTSTR;
using errno_t = int;
using BOOL = int;
using HINSTANCE = void*;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

inline int ShowCursor(BOOL) { return 0; }

using CRITICAL_SECTION = pthread_mutex_t;

inline void InitializeCriticalSection(CRITICAL_SECTION* section)
{
    pthread_mutex_init(section, nullptr);
}

inline void DeleteCriticalSection(CRITICAL_SECTION* section)
{
    pthread_mutex_destroy(section);
}

inline void EnterCriticalSection(CRITICAL_SECTION* section)
{
    pthread_mutex_lock(section);
}

inline void LeaveCriticalSection(CRITICAL_SECTION* section)
{
    pthread_mutex_unlock(section);
}

#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef _MAX_PATH
#define _MAX_PATH MAX_PATH
#endif

// Keep these token-identical to DXVK Native's windows_base.h so including both is warning-free.
#ifndef RtlZeroMemory
#define RtlZeroMemory(Destination,Length) memset((Destination),0,(Length))
#endif
#ifndef ZeroMemory
#define ZeroMemory RtlZeroMemory
#endif

#ifndef _T
#define _T(value) value
#endif

#ifndef _TRUNCATE
#define _TRUNCATE static_cast<std::size_t>(-1)
#endif

#ifndef _tcslen
#define _tcslen std::strlen
#endif
#ifndef _tcstol
#define _tcstol std::strtol
#endif
#ifndef _tcsicmp
#define _tcsicmp strcasecmp
#endif
#ifndef _tcscmp
#define _tcscmp std::strcmp
#endif
#ifndef _tcsncmp
#define _tcsncmp std::strncmp
#endif
#ifndef _ttoi
#define _ttoi std::atoi
#endif
#ifndef _stprintf_s
#define _stprintf_s std::snprintf
#endif

inline errno_t strncpy_s(char* destination, std::size_t destination_size,
                         const char* source, std::size_t count)
{
    if (!destination || destination_size == 0) return EINVAL;
    const std::size_t source_length = source ? std::strlen(source) : 0;
    const std::size_t copy_length = count == _TRUNCATE
        ? std::min(source_length, destination_size - 1)
        : std::min({source_length, count, destination_size - 1});
    if (copy_length > 0) std::memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';
    return 0;
}

inline errno_t strncat_s(char* destination, std::size_t destination_size,
                         const char* source, std::size_t count)
{
    if (!destination || destination_size == 0) return EINVAL;
    const std::size_t current_length = std::strlen(destination);
    if (current_length >= destination_size) return ERANGE;
    return strncpy_s(destination + current_length, destination_size - current_length, source, count);
}

inline errno_t _tcscpy_s(char* destination, std::size_t destination_size,
                         const char* source)
{
    return strncpy_s(destination, destination_size, source, _TRUNCATE);
}

inline errno_t _tcscat_s(char* destination, std::size_t destination_size,
                         const char* source)
{
    return strncat_s(destination, destination_size, source, _TRUNCATE);
}

inline int _snprintf_s(char* destination, std::size_t destination_size,
                       std::size_t, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vsnprintf(destination, destination_size, format, arguments);
    va_end(arguments);
    if (destination_size > 0) destination[destination_size - 1] = '\0';
    return result;
}

inline int sprintf_s(char* destination, std::size_t destination_size,
                     const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vsnprintf(destination, destination_size, format, arguments);
    va_end(arguments);
    if (destination_size > 0) destination[destination_size - 1] = '\0';
    return result;
}
