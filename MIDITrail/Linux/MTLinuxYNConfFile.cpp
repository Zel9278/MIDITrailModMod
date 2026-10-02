#include "MTLinuxCompat.h"
#include "../../YNBaseLib/YNConfFile.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string normalize_path(const char* value) {
    std::string path = value ? value : "";
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool read_value(const char* file, const char* section, const char* key, std::string* value) {
    std::ifstream stream(normalize_path(file));
    if (!stream) return false;
    std::string line;
    std::string current;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            current = trim(line.substr(1, line.size() - 2));
            continue;
        }
        if (current != (section ? section : "")) continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        if (trim(line.substr(0, equal)) == (key ? key : "")) {
            *value = trim(line.substr(equal + 1));
            return true;
        }
    }
    return false;
}

bool write_value(const char* file, const char* section, const char* key, const std::string& value) {
    const std::string path = normalize_path(file);
    std::vector<std::string> lines;
    {
        std::ifstream stream(path);
        std::string line;
        while (std::getline(stream, line)) lines.push_back(line);
    }

    const std::string target_section = section ? section : "";
    const std::string target_key = key ? key : "";
    std::size_t section_begin = lines.size();
    std::size_t section_end = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string current = trim(lines[index]);
        if (current.size() < 2 || current.front() != '[' || current.back() != ']') continue;
        if (section_begin != lines.size()) {
            section_end = index;
            break;
        }
        if (trim(current.substr(1, current.size() - 2)) == target_section) section_begin = index;
    }

    const std::string assignment = target_key + "=" + value;
    if (section_begin == lines.size()) {
        if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
        lines.push_back("[" + target_section + "]");
        lines.push_back(assignment);
    } else {
        bool replaced = false;
        for (std::size_t index = section_begin + 1; index < section_end; ++index) {
            const std::string current = trim(lines[index]);
            const auto equal = current.find('=');
            if (equal != std::string::npos && trim(current.substr(0, equal)) == target_key) {
                lines[index] = assignment;
                replaced = true;
                break;
            }
        }
        if (!replaced) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(section_end), assignment);
    }

    std::error_code error;
    const std::filesystem::path file_path(path);
    if (file_path.has_parent_path()) std::filesystem::create_directories(file_path.parent_path(), error);
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) return false;
    for (const std::string& line : lines) stream << line << '\n';
    return static_cast<bool>(stream);
}
}

namespace YNBaseLib {
YNConfFile::YNConfFile() { m_FilePath[0] = '\0'; m_Section[0] = '\0'; }
YNConfFile::~YNConfFile() = default;

int YNConfFile::Initialize(const TCHAR* path) {
    std::snprintf(m_FilePath, _MAX_PATH, "%s", normalize_path(path).c_str());
    return 0;
}

int YNConfFile::SetCurSection(const TCHAR* section) {
    std::snprintf(m_Section, _MAX_PATH, "%s", section ? section : "");
    return 0;
}

int YNConfFile::GetInt(const TCHAR* key, int* value, int default_value) {
    std::string text;
    *value = read_value(m_FilePath, m_Section, key, &text) ? std::strtol(text.c_str(), nullptr, 10) : default_value;
    return 0;
}

int YNConfFile::SetInt(const TCHAR* key, int value) {
    return write_value(m_FilePath, m_Section, key, std::to_string(value)) ? 0 : -1;
}

int YNConfFile::GetFloat(const TCHAR* key, float* value, float default_value) {
    std::string text;
    *value = read_value(m_FilePath, m_Section, key, &text) ? std::strtof(text.c_str(), nullptr) : default_value;
    return 0;
}

int YNConfFile::SetFloat(const TCHAR* key, float value) {
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return write_value(m_FilePath, m_Section, key, stream.str()) ? 0 : -1;
}

int YNConfFile::GetStr(const TCHAR* key, TCHAR* buffer, unsigned long size, const TCHAR* default_value) {
    std::string text;
    const std::string result = read_value(m_FilePath, m_Section, key, &text) ? text : (default_value ? default_value : "");
    if (size == 0) return -1;
    std::snprintf(buffer, size, "%s", result.c_str());
    return 0;
}

int YNConfFile::SetStr(const TCHAR* key, const TCHAR* value) {
    return write_value(m_FilePath, m_Section, key, value ? value : "") ? 0 : -1;
}

int YNConfFile::GetWStr(const TCHAR* key, WCHAR* buffer, unsigned long size, const WCHAR* default_value) {
    if (size == 0) return -1;
    std::string text;
    const std::string result = read_value(m_FilePath, m_Section, key, &text) ? text : "";
    const char* source = result.empty() && default_value ? "" : result.c_str();
    std::mbstowcs(buffer, source, size - 1);
    buffer[size - 1] = L'\0';
    return 0;
}

int YNConfFile::SetWStr(const TCHAR* key, const WCHAR* value) {
    if (!value) return SetStr(key, "");
    std::mbstate_t state = {};
    const WCHAR* source = value;
    const std::size_t size = std::wcsrtombs(nullptr, &source, 0, &state);
    if (size == static_cast<std::size_t>(-1)) return -1;
    std::string text(size, '\0');
    state = {};
    source = value;
    std::wcsrtombs(text.data(), &source, text.size(), &state);
    return SetStr(key, text.c_str());
}
}
