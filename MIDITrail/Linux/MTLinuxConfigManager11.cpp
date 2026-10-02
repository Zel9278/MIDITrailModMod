#include "../StdAfx.h"
#include "../MTConfigManager11.h"
#include "../../YNBaseLib/YNPathUtil.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

struct EnumOption { const char* value; const char* label; };
struct EnumKey { const char* key; const EnumOption* options; int count; };

const EnumOption note_color_options[] = {
    {"CHANNEL", "CHANNEL (per MIDI channel)"},
    {"SCALE", "SCALE (per pitch class)"},
    {"CHANNELTRACK", "CHANNELTRACK (channel x track)"},
};
const EnumOption active_key_options[] = {
    {"STANDARD", "STANDARD (fixed pressed-key color)"},
    {"NOTE", "NOTE (use the played note's color)"},
};
const EnumOption blend_options[] = {
    {"1", "ZERO (1)"}, {"2", "ONE (2)"},
    {"5", "SRCALPHA (5)"}, {"6", "INVSRCALPHA (6)"},
    {"7", "DESTALPHA (7)"}, {"8", "INVDESTALPHA (8)"},
};
const EnumKey enum_keys[] = {
    {"NoteColorType", note_color_options, 3},
    {"ActiveKeyColorType", active_key_options, 2},
    {"SrcBlend", blend_options, 6},
    {"DestBlend", blend_options, 6},
};

bool equals_ci(const std::string& left, const char* right)
{
    if (right == nullptr || left.size() != std::strlen(right)) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index]))
            != std::tolower(static_cast<unsigned char>(right[index]))) return false;
    }
    return true;
}

const EnumKey* find_enum(const std::string& key)
{
    for (const EnumKey& entry : enum_keys) {
        if (equals_ci(key, entry.key)) return &entry;
    }
    return nullptr;
}

bool ends_with_ci(const std::string& value, const char* suffix)
{
    const std::size_t suffix_size = std::strlen(suffix);
    if (suffix_size > value.size()) return false;
    return equals_ci(value.substr(value.size() - suffix_size), suffix);
}

int color_hex_digits(const std::string& key, const char* value)
{
    if (!(ends_with_ci(key, "color") || ends_with_ci(key, "rgb") || ends_with_ci(key, "rgba"))) return 0;
    const std::size_t size = std::strlen(value);
    if (size != 6 && size != 8) return 0;
    for (std::size_t index = 0; index < size; ++index) {
        if (!std::isxdigit(static_cast<unsigned char>(value[index]))) return 0;
    }
    return static_cast<int>(size);
}

void hex_to_color(const char* hex, float color[4])
{
    color[0] = color[1] = color[2] = 0.0f;
    color[3] = 1.0f;
    const int component_count = static_cast<int>(std::strlen(hex) / 2);
    char component[3] = {};
    for (int index = 0; index < component_count; ++index) {
        component[0] = hex[index * 2];
        component[1] = hex[index * 2 + 1];
        color[index] = static_cast<float>(std::strtol(component, nullptr, 16)) / 255.0f;
    }
}

void color_to_hex(const float color[4], int digits, char* output, std::size_t output_size)
{
    int components[4];
    for (int index = 0; index < 4; ++index) {
        components[index] = std::clamp(static_cast<int>(color[index] * 255.0f + 0.5f), 0, 255);
    }
    if (digits == 6) {
        std::snprintf(output, output_size, "%02X%02X%02X", components[0], components[1], components[2]);
    } else {
        std::snprintf(output, output_size, "%02X%02X%02X%02X",
                      components[0], components[1], components[2], components[3]);
    }
}

} // namespace

MTConfigManager11::MTConfigManager11()
    : m_Visible(false), m_ApplyRequested(false), m_SelectedFile(-1), m_HasBOM(false)
{
    m_ConfDir[0] = '\0';
}

MTConfigManager11::~MTConfigManager11() = default;

int MTConfigManager11::Initialize()
{
    char module_dir[MAX_PATH] = {};
    if (YNBaseLib::YNPathUtil::GetModuleDirPath(module_dir, MAX_PATH) != 0) return -1;
    std::snprintf(m_ConfDir, MAX_PATH, "%sconf/", module_dir);
    _RefreshFileList();
    return 0;
}

void MTConfigManager11::Toggle()
{
    m_Visible = !m_Visible;
    if (m_Visible) {
        _RefreshFileList();
        _ReloadCurrent();
    }
}

bool MTConfigManager11::ConsumeApplyRequest()
{
    const bool requested = m_ApplyRequested;
    m_ApplyRequested = false;
    return requested;
}

void MTConfigManager11::_RefreshFileList()
{
    static const char* allowed[] = {
        "PianoRoll2D.ini", "PianoRoll2DLive.ini",
        "PianoRoll3D.ini", "PianoRoll3DLive.ini",
        "PianoRollRain.ini", "PianoRollRain2D.ini",
        "PianoRollRain2DLive.ini", "PianoRollRainLive.ini",
        "PianoRollRing.ini", "PianoRollRingLive.ini",
    };

    m_FileList.clear();
    std::error_code error;
    for (const std::filesystem::directory_entry& entry
         : std::filesystem::directory_iterator(m_ConfDir, error)) {
        if (error || !entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        for (const char* candidate : allowed) {
            if (equals_ci(name, candidate)) {
                m_FileList.push_back(name);
                break;
            }
        }
    }
    std::sort(m_FileList.begin(), m_FileList.end());
    if (m_FileList.empty()) m_SelectedFile = -1;
    else if (m_SelectedFile < 0 || m_SelectedFile >= static_cast<int>(m_FileList.size())) m_SelectedFile = 0;
}

void MTConfigManager11::_ReloadCurrent()
{
    m_Lines.clear();
    if (m_SelectedFile < 0 || m_SelectedFile >= static_cast<int>(m_FileList.size())) return;
    _LoadFile(m_FileList[m_SelectedFile].c_str());
}

int MTConfigManager11::_LoadFile(const char* file_name)
{
    m_Lines.clear();
    m_HasBOM = false;
    const std::filesystem::path path = std::filesystem::path(m_ConfDir) / file_name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        m_Status = "Open failed.";
        return -1;
    }
    std::string buffer((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (buffer.size() >= 3 && static_cast<unsigned char>(buffer[0]) == 0xEF
        && static_cast<unsigned char>(buffer[1]) == 0xBB
        && static_cast<unsigned char>(buffer[2]) == 0xBF) {
        m_HasBOM = true;
        buffer.erase(0, 3);
    }

    std::size_t start = 0;
    while (start < buffer.size()) {
        const std::size_t newline = buffer.find('\n', start);
        std::string raw = buffer.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        start = newline == std::string::npos ? buffer.size() : newline + 1;

        Line line;
        line.type = LT_BLANK;
        line.value[0] = '\0';
        const std::size_t first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) {
            line.text.clear();
        } else if (raw[first] == ';' || raw[first] == '#') {
            line.type = LT_COMMENT;
            line.text = raw;
        } else if (raw[first] == '[' && raw.find(']', first) != std::string::npos) {
            const std::size_t close = raw.find(']', first);
            line.type = LT_SECTION;
            line.text = raw.substr(first + 1, close - first - 1);
        } else {
            const std::size_t equal = raw.find('=');
            if (equal == std::string::npos) {
                line.type = LT_COMMENT;
                line.text = raw;
            } else {
                line.type = LT_KEYVAL;
                line.key = raw.substr(0, equal);
                std::snprintf(line.value, sizeof(line.value), "%s", raw.substr(equal + 1).c_str());
            }
        }
        m_Lines.push_back(line);
    }
    m_Status = "Loaded.";
    return 0;
}

int MTConfigManager11::_SaveFile()
{
    if (m_SelectedFile < 0 || m_SelectedFile >= static_cast<int>(m_FileList.size())) return -1;
    const std::filesystem::path path = std::filesystem::path(m_ConfDir) / m_FileList[m_SelectedFile];
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        m_Status = "Save failed.";
        return -1;
    }
    if (m_HasBOM) stream.write("\xEF\xBB\xBF", 3);
    for (const Line& line : m_Lines) {
        if (line.type == LT_SECTION) stream << '[' << line.text << ']';
        else if (line.type == LT_KEYVAL) stream << line.key << '=' << line.value;
        else stream << line.text;
        stream << '\n';
    }
    if (!stream.good()) {
        m_Status = "Save failed.";
        return -1;
    }
    m_Status = "Saved. (applied to the current scene)";
    m_ApplyRequested = true;
    return 0;
}

void MTConfigManager11::RenderImGui()
{
    if (!m_Visible) return;
    ImGui::SetNextWindowSize(ImVec2(560, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Config Manager", &m_Visible)) {
        ImGui::End();
        return;
    }

    const char* current = m_SelectedFile >= 0 && m_SelectedFile < static_cast<int>(m_FileList.size())
        ? m_FileList[m_SelectedFile].c_str() : "(no file)";
    ImGui::SetNextItemWidth(280);
    if (ImGui::BeginCombo("File", current)) {
        for (int index = 0; index < static_cast<int>(m_FileList.size()); ++index) {
            const bool selected = index == m_SelectedFile;
            if (ImGui::Selectable(m_FileList[index].c_str(), selected) && !selected) {
                m_SelectedFile = index;
                _ReloadCurrent();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) _ReloadCurrent();
    ImGui::SameLine();
    if (ImGui::Button("Save")) _SaveFile();
    if (!m_Status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_Status.c_str());
    }

    ImGui::Separator();
    ImGui::BeginChild("config_body", ImVec2(0, 0), true);
    bool section_open = true;
    for (std::size_t index = 0; index < m_Lines.size(); ++index) {
        Line& line = m_Lines[index];
        ImGui::PushID(static_cast<int>(index));
        if (line.type == LT_SECTION) {
            section_open = ImGui::CollapsingHeader(line.text.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        } else if (line.type == LT_KEYVAL && section_open) {
            if (const EnumKey* entry = find_enum(line.key)) {
                ImGui::SetNextItemWidth(220);
                if (ImGui::BeginCombo(line.key.c_str(), line.value)) {
                    for (int option = 0; option < entry->count; ++option) {
                        const bool selected = equals_ci(line.value, entry->options[option].value);
                        if (ImGui::Selectable(entry->options[option].label, selected)) {
                            std::snprintf(line.value, sizeof(line.value), "%s", entry->options[option].value);
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } else if (const int digits = color_hex_digits(line.key, line.value)) {
                float color[4];
                hex_to_color(line.value, color);
                ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_Uint8;
                flags |= digits == 8 ? ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf
                                     : ImGuiColorEditFlags_NoAlpha;
                ImGui::SetNextItemWidth(220);
                if (ImGui::ColorEdit4(line.key.c_str(), color, flags)) {
                    color_to_hex(color, digits, line.value, sizeof(line.value));
                }
            } else {
                ImGui::SetNextItemWidth(220);
                ImGui::InputText(line.key.c_str(), line.value, sizeof(line.value));
            }
        } else if (line.type == LT_COMMENT && section_open) {
            ImGui::TextDisabled("%s", line.text.c_str());
        } else if (line.type == LT_BLANK && section_open) {
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}
