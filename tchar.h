#pragma once

#ifdef MIDITRAIL_LINUX_NATIVE
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdlib>

#ifndef _tcscat_s
#define _tcscat_s(destination, size, source) \
    (std::strncat((destination), (source), (size) - std::strlen(destination) - 1), 0)
#endif
#ifndef _tcscpy_s
#define _tcscpy_s(destination, size, source) \
    (std::strncpy((destination), (source), (size) - 1), (destination)[(size) - 1] = '\0', 0)
#endif
#ifndef _tcslen
#define _tcslen std::strlen
#endif
#ifndef _tcscmp
#define _tcscmp std::strcmp
#endif
#ifndef _tcsicmp
#define _tcsicmp strcasecmp
#endif
#ifndef _tfopen
#define _tfopen std::fopen
#endif
#ifndef _stprintf_s
#define _stprintf_s std::snprintf
#endif
#ifndef _ttoi
#define _ttoi std::atoi
#endif
#endif
