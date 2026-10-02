#pragma once

#ifdef MIDITRAIL_LINUX_NATIVE

#include <sys/stat.h>

inline int PathFileExists(const char* path)
{
    struct stat info;
    return path != nullptr && stat(path, &info) == 0;
}

#else
#include_next <shlwapi.h>
#endif
