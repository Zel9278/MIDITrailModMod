#include "MTLinuxCompat.h"
#include "../../YNBaseLib/YNPathUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace YNBaseLib {
int YNPathUtil::GetModuleDirPath(TCHAR* buffer, unsigned long size) {
    if (!buffer || size == 0) return -1;
    char path[MAX_PATH] = {};
    const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0) return -1;
    path[length] = '\0';
    char* slash = std::strrchr(path, '/');
    if (slash) slash[1] = '\0';
    std::snprintf(buffer, size, "%s", path);
    return 0;
}

int YNPathUtil::GetAppDataDirPath(TCHAR* buffer, unsigned long size) {
    const char* home = std::getenv("HOME");
    if (!home) return -1;
    std::snprintf(buffer, size, "%s/.config/MIDITrail/", home);
    return 0;
}

bool YNPathUtil::IsFileExtMatch(const TCHAR* path, const TCHAR* extension) {
    if (!path || !extension) return false;
    std::string actual(path);
    std::replace(actual.begin(), actual.end(), '\\', '/');
    const auto dot = actual.rfind('.');
    return dot != std::string::npos && strcasecmp(actual.c_str() + dot, extension) == 0;
}

int YNPathUtil::GetTempFilePath(TCHAR* buffer, unsigned long size, const TCHAR* prefix) {
    if (!buffer || size == 0) return -1;
    std::snprintf(buffer, size, "/tmp/%sXXXXXX", prefix ? prefix : "MT_");
    const int fd = mkstemp(buffer);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}
}
