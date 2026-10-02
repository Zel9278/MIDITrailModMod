// ImGui replacements for the Win32 dialogs of MIDITrailApp:
// MTColorCfgDlg / MTColorPaletteCfgDlg / MTColorParam{Export,Import}Dlg,
// MTGraphicCfgDlg, MTMIDIOUTCfgDlg, MTMIDIINCfgDlg, MTWindowSizeCfgDlg,
// MTVideoExportDlg, MTHowToViewDlg, MTAboutDlg and the message boxes.

#include "MTLinuxDX11ContextImpl.h"

#include "../DXColorUtil.h"
#include "../MTConfFile.h"
#include "../MIDITrailVersion.h"
#include "../stb_image.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>

namespace miditrail::linux_native {

namespace {

const char* const kCodecNames[] = {
    "H.264  (NVENC, NVIDIA GPU)",
    "H.265 / HEVC  (NVENC, NVIDIA GPU)",
    "H.264  (QSV, Intel GPU)",
    "H.265 / HEVC  (QSV, Intel GPU)",
    "H.264  (libx264, CPU)",
    "H.265 / HEVC  (libx265, CPU)",
    "Transparent .mov  (QuickTime RLE)",
    "Transparent .mov  (ProRes 4444)",
    "Transparent .mkv  (FFV1, lossless)",
    "H.264  (VAAPI, AMD/Intel GPU)",
    "H.265 / HEVC  (VAAPI, AMD/Intel GPU)",
};
constexpr int kCodecCount = static_cast<int>(sizeof(kCodecNames) / sizeof(kCodecNames[0]));

bool color_edit(const char* label, D3DXCOLOR* color, bool alpha = true) {
    float rgba[4] = {color->r, color->g, color->b, color->a};
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_AlphaBar;
    if (!alpha) flags |= ImGuiColorEditFlags_NoAlpha;
    if (!ImGui::ColorEdit4(label, rgba, flags)) return false;
    *color = D3DXCOLOR(rgba[0], rgba[1], rgba[2], alpha ? rgba[3] : 1.0f);
    return true;
}

ImVec4 to_imvec(const D3DXCOLOR& color) {
    return ImVec4(color.r, color.g, color.b, color.a);
}

void palette_preview(MTColorPalette& palette) {
    for (unsigned int ch = 0; ch < SM_MAX_CH_NUM; ++ch) {
        D3DXCOLOR color;
        palette.GetChColor(ch, &color);
        char id[32];
        std::snprintf(id, sizeof(id), "##ch%u", ch);
        ImGui::ColorButton(id, to_imvec(color), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
        if (ch + 1 < SM_MAX_CH_NUM) ImGui::SameLine(0, 2);
    }
}

std::string make_color_param_text(MTColorPalette& palette) {
    std::string text;
    char hex[16];
    char line[64];
    D3DXCOLOR color;
    for (unsigned int ch = 0; ch < SM_MAX_CH_NUM; ++ch) {
        palette.GetChColor(ch, &color);
        DXColorUtil::MakeHexRGBAFromColor(color, hex, sizeof(hex));
        std::snprintf(line, sizeof(line), "Ch-%02u-NoteRGBA=%s\n", ch + 1, hex);
        text += line;
    }
    palette.GetBackgroundColor(&color);
    DXColorUtil::MakeHexRGBAFromColor(color, hex, sizeof(hex));
    text += std::string("BackGroundRGBA=") + hex + "\n";
    palette.GetGridLineColor(&color);
    DXColorUtil::MakeHexRGBAFromColor(color, hex, sizeof(hex));
    text += std::string("GridLineRGBA=") + hex + "\n";
    palette.GetCounterColor(&color);
    DXColorUtil::MakeHexRGBAFromColor(color, hex, sizeof(hex));
    text += std::string("CounterRGBA=") + hex + "\n";
    return text;
}

// MTColorPaletteCfgDlg::_MakeImportKeyValueMap + _LoadParam
void import_color_param_text(const std::string& text, MTColorPalette* palette) {
    std::map<std::string, std::string> values;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line, '\n')) {
        const std::size_t left = line.find_first_not_of(" \t\r\n");
        const std::size_t right = line.find_last_not_of(" \t\r\n");
        if (left == std::string::npos || right == std::string::npos) continue;
        std::string trimmed = line.substr(left, right - left + 1);
        if (trimmed.front() == '#' || trimmed.front() == ';') continue;
        const std::size_t equal = trimmed.find('=');
        if (equal == std::string::npos || equal == 0) continue;
        std::string key = trimmed.substr(0, equal);
        std::string value = trimmed.substr(equal + 1);
        if (value.size() >= 2 && ((value.front() == '\'' && value.back() == '\'')
                                  || (value.front() == '"' && value.back() == '"'))) {
            value = value.substr(1, value.size() - 2);
        }
        values[key] = value;
    }
    auto find = [&values](const char* key) -> const std::string* {
        auto it = values.find(key);
        return it == values.end() ? nullptr : &it->second;
    };
    for (unsigned int ch = 0; ch < SM_MAX_CH_NUM; ++ch) {
        char key[32];
        std::snprintf(key, sizeof(key), "Ch-%02u-NoteRGBA", ch + 1);
        const std::string* value = find(key);
        if (value != nullptr && value->size() == 8) palette->SetChColor(ch, DXColorUtil::MakeColorFromHexRGBA(value->c_str()));
    }
    if (const std::string* value = find("BackGroundRGBA"); value != nullptr && value->size() == 8)
        palette->SetBackgroundColor(DXColorUtil::MakeColorFromHexRGBA(value->c_str()));
    if (const std::string* value = find("BackGroundRGB"); value != nullptr && value->size() == 6)
        palette->SetBackgroundColor(D3DXCOLOR(DXColorUtil::MakeColorFromHexRGB(value->c_str())));
    if (const std::string* value = find("GridLineRGBA"); value != nullptr && value->size() == 8)
        palette->SetGridLineColor(DXColorUtil::MakeColorFromHexRGBA(value->c_str()));
    if (const std::string* value = find("CounterRGBA"); value != nullptr && value->size() == 8)
        palette->SetCounterColor(DXColorUtil::MakeColorFromHexRGBA(value->c_str()));
}

void center_next_window() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

} // namespace

//******************************************************************************
// frame UI
//******************************************************************************
void LinuxDX11Context::Impl::draw_ui() {
    LinuxMenuState state;
    switch (play_status) {
    case PlayStatus::NoData: state.play_status = 0; break;
    case PlayStatus::Stop: state.play_status = 1; break;
    case PlayStatus::Play: state.play_status = 2; break;
    case PlayStatus::Pause: state.play_status = 3; break;
    case PlayStatus::MonitorOFF: state.play_status = 4; break;
    case PlayStatus::MonitorON: state.play_status = 5; break;
    }
    state.scene = static_cast<int>(scene_type);
    state.repeat = repeat;
    state.folder_playback = folder_playback;
    state.single_keyboard = single_keyboard;
    state.piano_keyboard = keyboard_visible;
    state.ripple = ripple_visible;
    state.lyrics = lyrics_visible;
    state.pitch_bend = pitch_bend_enabled;
    state.pitch_bend_all_notes = pitch_bend_all_notes;
    state.stars = stars_visible;
    state.counter = dashboard_visible;
    state.background_image = background_image_visible;
    state.time_indicator = time_indicator_visible;
    state.grid = grid_visible;
    state.auto_save_viewpoint = auto_save_viewpoint;
    state.auto_save_view_settings = auto_save_view_settings;
    state.fullscreen = fullscreen;
    state.has_file_list = !file_list.empty();
    menu.draw(state);
    // the menu bar takes the top of the view only while it is permanently shown
    renderer.SetSceneTopInset(menu.enabled() && !fullscreen ? ImGui::GetFrameHeight() : 0.0f);

    draw_color_dialog();
    draw_color_export_import();
    draw_graphic_dialog();
    draw_midi_out_dialog();
    draw_midi_in_dialog();
    draw_window_size_dialog();
    draw_video_dialog();
    draw_howto_dialog();
    draw_about_dialog();
    draw_message_boxes();
}

//******************************************************************************
// Color (MTColorCfgDlg + MTColorPaletteCfgDlg)
//******************************************************************************
void LinuxDX11Context::Impl::open_color_dialog() {
    color_dlg.conf = std::make_unique<MTColorConf>();
    color_dlg.scene = scene_name();
    if (color_dlg.conf->Initialize(color_dlg.scene.c_str()) != 0) {
        show_error("Could not read the color configuration.");
        color_dlg.conf.reset();
        return;
    }
    color_dlg.palette_no = color_dlg.conf->GetSelectedColorPaletteNo();
    color_dlg.conf->GetColorPalette(color_dlg.palette_no, &color_dlg.palette);
    color_dlg.editing = false;
    color_dlg.visible = true;
}

void LinuxDX11Context::Impl::draw_color_dialog() {
    if (!color_dlg.visible || !color_dlg.conf) return;
    center_next_window();
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Color configuration", &color_dlg.visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::Text("Scene: %s", color_dlg.scene.c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(color_dlg.editing);
    for (unsigned long no = 0; no < MT_COLOR_PALETTE_NUM_MAX; ++no) {
        char label[32];
        if (no == 0) std::snprintf(label, sizeof(label), "Default");
        else std::snprintf(label, sizeof(label), "Palette %lu", no);
        ImGui::PushID(static_cast<int>(no));
        if (ImGui::RadioButton(label, color_dlg.palette_no == no)) {
            color_dlg.palette_no = no;
            color_dlg.conf->GetColorPalette(no, &color_dlg.palette);
        }
        ImGui::SameLine(110);
        MTColorPalette preview;
        color_dlg.conf->GetColorPalette(no, &preview);
        palette_preview(preview);
        if (no != 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit")) {
                color_dlg.palette_no = no;
                color_dlg.conf->GetColorPalette(no, &color_dlg.palette);
                color_dlg.editing = true;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    if (color_dlg.editing) {
        ImGui::Separator();
        ImGui::Text("Edit Palette %lu", color_dlg.palette_no);
        MTColorPalette& palette = color_dlg.palette;
        if (ImGui::BeginTable("##colors", 2)) {
            for (unsigned int ch = 0; ch < SM_MAX_CH_NUM; ++ch) {
                ImGui::TableNextColumn();
                D3DXCOLOR color;
                palette.GetChColor(ch, &color);
                char label[24];
                std::snprintf(label, sizeof(label), "Ch.%u", ch + 1);
                if (color_edit(label, &color)) palette.SetChColor(ch, color);
            }
            D3DXCOLOR color;
            ImGui::TableNextColumn();
            palette.GetBackgroundColor(&color);
            if (color_edit("BG", &color)) palette.SetBackgroundColor(color);
            ImGui::TableNextColumn();
            palette.GetGridLineColor(&color);
            if (color_edit("GL", &color)) palette.SetGridLineColor(color);
            ImGui::TableNextColumn();
            palette.GetCounterColor(&color);
            if (color_edit("CT", &color)) palette.SetCounterColor(color);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Gradation tool");
        static D3DXCOLOR gradation_start(1, 0, 0, 1);
        static D3DXCOLOR gradation_end(0, 0, 1, 1);
        const char* channels[] = {"Ch.1", "Ch.2", "Ch.3", "Ch.4", "Ch.5", "Ch.6", "Ch.7", "Ch.8",
                                  "Ch.9", "Ch.10", "Ch.11", "Ch.12", "Ch.13", "Ch.14", "Ch.15", "Ch.16"};
        ImGui::SetNextItemWidth(80);
        ImGui::Combo("Start", &color_dlg.gradation_start, channels, 16);
        ImGui::SameLine();
        color_edit("##gstart", &gradation_start, false);
        ImGui::SetNextItemWidth(80);
        ImGui::Combo("End", &color_dlg.gradation_end, channels, 16);
        ImGui::SameLine();
        color_edit("##gend", &gradation_end, false);
        if (ImGui::Button("Set Gradation Colors")) {
            // MTColorPaletteCfgDlg::_SetGradationColor
            int first = color_dlg.gradation_start;
            int last = color_dlg.gradation_end;
            D3DXCOLOR from = gradation_start;
            D3DXCOLOR to = gradation_end;
            if (first > last) {
                std::swap(first, last);
                std::swap(from, to);
            }
            for (int ch = first; ch <= last && first != last; ++ch) {
                const float ratio = static_cast<float>(ch - first) / static_cast<float>(last - first);
                palette.SetChColor(static_cast<unsigned long>(ch),
                                   D3DXCOLOR((to.r - from.r) * ratio + from.r, (to.g - from.g) * ratio + from.g,
                                             (to.b - from.b) * ratio + from.b, 1.0f));
            }
        }

        ImGui::SeparatorText("Parameter setup tool");
        if (ImGui::Button("Set Default Colors")) {
            MTColorPalette defaults;
            color_dlg.conf->GetColorPalette(0, &defaults);
            palette.CopyFrom(&defaults);
        }
        ImGui::SameLine();
        if (ImGui::Button("Export Color Parameters")) {
            color_dlg.export_text = make_color_param_text(palette);
            color_dlg.export_visible = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Color Parameters")) {
            std::fill(color_dlg.import_text.begin(), color_dlg.import_text.end(), 0);
            color_dlg.import_visible = true;
        }
        ImGui::Separator();
        if (ImGui::Button("Save Palette")) {
            color_dlg.conf->SetColorPalette(color_dlg.palette_no, &palette);
            color_dlg.editing = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            color_dlg.conf->GetColorPalette(color_dlg.palette_no, &color_dlg.palette);
            color_dlg.editing = false;
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(color_dlg.editing);
    if (ImGui::Button("OK", ImVec2(80, 0))) {
        color_dlg.conf->SetSelectedColorPaletteNo(color_dlg.palette_no);
        if (color_dlg.conf->Save() != 0) show_error("Could not save the color configuration.");
        color_dlg.visible = false;
        // MTNoteDesign re-reads the selected palette: rebuild the scene
        if (family != Family::None) {
            setup_scene();
            feed_tick(is_monitor() ? 0 : snapshot_tick.load(), is_monitor() ? 0 : snapshot_ms.load());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) color_dlg.visible = false;
    ImGui::EndDisabled();
    ImGui::End();
    if (!color_dlg.visible) color_dlg.conf.reset();
}

void LinuxDX11Context::Impl::draw_color_export_import() {
    if (color_dlg.export_visible) {
        center_next_window();
        ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_Appearing);
        if (ImGui::Begin("Export Color Parameters", &color_dlg.export_visible)) {
            ImGui::InputTextMultiline("##export", color_dlg.export_text.data(), color_dlg.export_text.size() + 1,
                                      ImVec2(-1, -ImGui::GetFrameHeightWithSpacing()), ImGuiInputTextFlags_ReadOnly);
            if (ImGui::Button("Copy to Clipboard")) SDL_SetClipboardText(color_dlg.export_text.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Close")) color_dlg.export_visible = false;
        }
        ImGui::End();
    }
    if (color_dlg.import_visible) {
        center_next_window();
        ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_Appearing);
        if (ImGui::Begin("Import Color Parameters", &color_dlg.import_visible)) {
            ImGui::InputTextMultiline("##import", color_dlg.import_text.data(), color_dlg.import_text.size(),
                                      ImVec2(-1, -ImGui::GetFrameHeightWithSpacing()));
            if (ImGui::Button("Paste")) {
                char* clipboard = SDL_GetClipboardText();
                if (clipboard != nullptr) {
                    std::snprintf(color_dlg.import_text.data(), color_dlg.import_text.size(), "%s", clipboard);
                    SDL_free(clipboard);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Import")) {
                import_color_param_text(color_dlg.import_text.data(), &color_dlg.palette);
                color_dlg.import_visible = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) color_dlg.import_visible = false;
        }
        ImGui::End();
    }
}

//******************************************************************************
// Graphic (MTGraphicCfgDlg)
//******************************************************************************
void LinuxDX11Context::Impl::open_graphic_dialog() {
    graphic_dlg.msaa = multi_sample_type;
    graphic_dlg.ssaa = super_sample;
    graphic_conf.SetCurSection("Background-image");
    graphic_conf.GetStr("ImageFilePath", graphic_dlg.background_path, sizeof(graphic_dlg.background_path), "");
    graphic_conf.SetCurSection("QuarterNote");
    graphic_conf.GetInt("LengthMagnification", &graphic_dlg.quarter_note_mag, 100);
    for (int samples = 0; samples <= 16; ++samples) {
        graphic_dlg.supported[samples] = samples >= 2 && renderer.IsMultiSampleSupported(static_cast<unsigned int>(samples));
    }
    graphic_dlg.visible = true;
}

void LinuxDX11Context::Impl::draw_graphic_dialog() {
    if (!graphic_dlg.visible) return;
    center_next_window();
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Graphic configuration", &graphic_dlg.visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    char preview[16];
    if (graphic_dlg.msaa < 2) std::snprintf(preview, sizeof(preview), "OFF");
    else std::snprintf(preview, sizeof(preview), "%dx", graphic_dlg.msaa);
    ImGui::SetNextItemWidth(100);
    if (ImGui::BeginCombo("Anti-aliasing", preview)) {
        if (ImGui::Selectable("OFF", graphic_dlg.msaa < 2)) graphic_dlg.msaa = 0;
        for (int samples = 2; samples <= 16; ++samples) {
            if (!graphic_dlg.supported[samples]) continue;
            char label[16];
            std::snprintf(label, sizeof(label), "%dx", samples);
            if (ImGui::Selectable(label, graphic_dlg.msaa == samples)) graphic_dlg.msaa = samples;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const char* ssaa_names[] = {"OFF", "2x", "3x", "4x"};
    int ssaa_index = std::clamp(graphic_dlg.ssaa, 1, 4) - 1;
    ImGui::SetNextItemWidth(80);
    if (ImGui::Combo("SSAA", &ssaa_index, ssaa_names, 4)) graphic_dlg.ssaa = ssaa_index + 1;

    ImGui::TextUnformatted("Background image file path");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("##bgpath", graphic_dlg.background_path, sizeof(graphic_dlg.background_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) show_open_dialog(PendingBackgroundImage);

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Quarter note length magnification (%)", &graphic_dlg.quarter_note_mag);
    graphic_dlg.quarter_note_mag = std::clamp(graphic_dlg.quarter_note_mag, 1, 1000);

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(80, 0))) {
        graphic_conf.SetCurSection("Anti-aliasing");
        graphic_conf.SetInt("MultiSampleType", graphic_dlg.msaa);
        graphic_conf.SetInt("SuperSample", graphic_dlg.ssaa);
        graphic_conf.SetCurSection("Background-image");
        graphic_conf.SetStr("ImageFilePath", graphic_dlg.background_path);
        graphic_conf.SetCurSection("QuarterNote");
        graphic_conf.SetInt("LengthMagnification", graphic_dlg.quarter_note_mag);
        graphic_dlg.visible = false;
        load_graphic_conf();
        rebuild_renderer();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) graphic_dlg.visible = false;
    ImGui::End();
}

//******************************************************************************
// MIDI OUT (MTMIDIOUTCfgDlg) / MIDI IN (MTMIDIINCfgDlg)
//******************************************************************************
void LinuxDX11Context::Impl::open_midi_out_dialog() {
    midi_dlg.out_devices = midi_out.enumerate_devices();
    midi_conf.SetCurSection("MIDIOUT");
    for (int port = 0; port < LinuxMidiOut::kPortCount; ++port) {
        char key[8];
        std::snprintf(key, sizeof(key), "Port%c", 'A' + port);
        char device[256] = {};
        midi_conf.GetStr(key, device, sizeof(device), "");
        midi_dlg.out_selected[port] = device;
    }
    midi_dlg.error.clear();
    midi_dlg.out_visible = true;
}

void LinuxDX11Context::Impl::draw_midi_out_dialog() {
    if (!midi_dlg.out_visible) return;
    center_next_window();
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("MIDI OUT configuration", &midi_dlg.out_visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Refresh")) midi_dlg.out_devices = midi_out.enumerate_devices();
    for (int port = 0; port < LinuxMidiOut::kPortCount; ++port) {
        if (port == 0) ImGui::SeparatorText("Default Port");
        if (port == 1) ImGui::SeparatorText("Optional Port");
        char label[16];
        std::snprintf(label, sizeof(label), "Port %c", 'A' + port);
        const std::string& selected = midi_dlg.out_selected[port];
        ImGui::SetNextItemWidth(-140);
        if (ImGui::BeginCombo(label, selected.empty() ? "(none)" : selected.c_str())) {
            if (ImGui::Selectable("(none)", selected.empty())) midi_dlg.out_selected[port].clear();
            for (const std::string& device : midi_dlg.out_devices) {
                if (ImGui::Selectable(device.c_str(), device == selected)) midi_dlg.out_selected[port] = device;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled(midi_out.port_open(port) ? "connected" : "-");
        if (!midi_out.last_error(port).empty()) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", midi_out.last_error(port).c_str());
        }
    }
    if (midi_dlg.out_selected[0].find("Through") != std::string::npos) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "MIDI Through is not a synthesizer and produces no audio.");
    }
    if (!midi_dlg.error.empty()) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", midi_dlg.error.c_str());
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(80, 0))) {
        midi_dlg.error.clear();
        midi_conf.SetCurSection("MIDIOUT");
        for (int port = 0; port < LinuxMidiOut::kPortCount; ++port) {
            char key[8];
            std::snprintf(key, sizeof(key), "Port%c", 'A' + port);
            midi_conf.SetStr(key, midi_dlg.out_selected[port].c_str());
            if (midi_out.port_device(port) == midi_dlg.out_selected[port] && midi_out.port_open(port)) continue;
            std::string error;
            std::lock_guard<std::recursive_mutex> lock(midi_output_mutex);
            if (!midi_out.open_port(port, midi_dlg.out_selected[port], &error)) {
                midi_dlg.error += std::string(key) + ": " + error + "\n";
            }
            midi_output_sync_pending = true;
        }
        if (midi_dlg.error.empty()) midi_dlg.out_visible = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) midi_dlg.out_visible = false;
    ImGui::SameLine();
    ImGui::BeginDisabled(!midi_out.port_open(0));
    if (ImGui::Button("Test Note (Port A, C4)")) {
        const unsigned char on[] = {0x90, 60, 100};
        midi_out.send(0, on, 3);
        SDL_AddTimer(500, [](void* user, SDL_TimerID, Uint32) -> Uint32 {
            const unsigned char off[] = {0x80, 60, 0};
            static_cast<LinuxMidiOut*>(user)->send(0, off, 3);
            return 0;
        }, &midi_out);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void LinuxDX11Context::Impl::open_midi_in_dialog() {
    midi_in.enumerate(&midi_dlg.in_devices, nullptr);
    load_midi_conf();
    midi_dlg.in_selected = midi_in_device;
    midi_dlg.thru = midi_thru;
    midi_dlg.in_visible = true;
}

void LinuxDX11Context::Impl::draw_midi_in_dialog() {
    if (!midi_dlg.in_visible) return;
    center_next_window();
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("MIDI IN configuration", &midi_dlg.in_visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Refresh")) midi_in.enumerate(&midi_dlg.in_devices, nullptr);
    const std::string& selected = midi_dlg.in_selected;
    ImGui::SetNextItemWidth(-80);
    if (ImGui::BeginCombo("Port A", selected.empty() ? "(none)" : selected.c_str())) {
        if (ImGui::Selectable("(none)", selected.empty())) midi_dlg.in_selected.clear();
        for (const AlsaMidiPort& device : midi_dlg.in_devices) {
            if (ImGui::Selectable(device.label.c_str(), device.label == selected)) midi_dlg.in_selected = device.label;
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("MIDITHRU (forward to MIDI OUT Port A)", &midi_dlg.thru);
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(80, 0))) {
        midi_conf.SetCurSection("MIDIIN");
        midi_conf.SetStr("PortA", midi_dlg.in_selected.c_str());
        midi_conf.SetInt("MIDITHRU", midi_dlg.thru ? 1 : 0);
        load_midi_conf();
        midi_dlg.in_visible = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) midi_dlg.in_visible = false;
    ImGui::End();
}

//******************************************************************************
// Window size (MTWindowSizeCfgDlg)
//******************************************************************************
void LinuxDX11Context::Impl::open_window_size_dialog() {
    window_dlg.sizes.clear();
    const SDL_DisplayID display = SDL_GetDisplayForWindow(platform->sdl_window());
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    for (int index = 0; modes != nullptr && index < count; ++index) {
        const std::pair<int, int> size(modes[index]->w, modes[index]->h);
        if (std::find(window_dlg.sizes.begin(), window_dlg.sizes.end(), size) == window_dlg.sizes.end()) {
            window_dlg.sizes.push_back(size);
        }
    }
    SDL_free(modes);
    std::sort(window_dlg.sizes.begin(), window_dlg.sizes.end());
    load_window_size_conf();
    window_dlg.width = window_width;
    window_dlg.height = window_height;
    window_dlg.apply_to_view = apply_to_view_area;
    window_dlg.selected = -1;
    for (int index = 0; index < static_cast<int>(window_dlg.sizes.size()); ++index) {
        if (window_dlg.sizes[index] == std::make_pair(window_width, window_height)) window_dlg.selected = index;
    }
    window_dlg.visible = true;
}

void LinuxDX11Context::Impl::draw_window_size_dialog() {
    if (!window_dlg.visible) return;
    center_next_window();
    if (!ImGui::Begin("Window size configuration", &window_dlg.visible,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Window size");
    if (ImGui::BeginListBox("##sizes", ImVec2(140, 220))) {
        for (int index = 0; index < static_cast<int>(window_dlg.sizes.size()); ++index) {
            char label[32];
            std::snprintf(label, sizeof(label), "%d x %d", window_dlg.sizes[index].first, window_dlg.sizes[index].second);
            if (ImGui::Selectable(label, window_dlg.selected == index)) {
                window_dlg.selected = index;
                window_dlg.width = window_dlg.sizes[index].first;
                window_dlg.height = window_dlg.sizes[index].second;
            }
        }
        ImGui::EndListBox();
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Width", &window_dlg.width, 0);
    ImGui::SetNextItemWidth(100);
    ImGui::InputInt("Height", &window_dlg.height, 0);
    ImGui::Checkbox("Apply to the view area", &window_dlg.apply_to_view);
    ImGui::EndGroup();
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(80, 0))) {
        if (window_dlg.width > 0 && window_dlg.height > 0) {
            view_conf.SetCurSection("WindowSize");
            view_conf.SetInt("Width", window_dlg.width);
            view_conf.SetInt("Height", window_dlg.height);
            view_conf.SetInt("ApplyToViewArea", window_dlg.apply_to_view ? 1 : 0);
            load_window_size_conf();
            window_dlg.visible = false;
            change_window_size();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) window_dlg.visible = false;
    ImGui::End();
}

//******************************************************************************
// Video export settings (MTVideoExportDlg)
//******************************************************************************
void LinuxDX11Context::Impl::draw_video_dialog() {
    if (!video_dlg.visible) return;
    center_next_window();
    bool open = true;
    if (!ImGui::Begin("Video Export Settings", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    MTVideoExportParams& params = video_dlg.params;
    int codec = std::clamp(static_cast<int>(params.codec), 0, kCodecCount - 1);
    ImGui::SetNextItemWidth(300);
    if (ImGui::Combo("Codec", &codec, kCodecNames, kCodecCount)) params.codec = static_cast<MTVideoCodec>(codec);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Width", &params.width, 0);
    ImGui::SetNextItemWidth(120);
    ImGui::BeginDisabled(params.equirect360);
    ImGui::InputInt("Height", &params.height, 0);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("FPS", &params.fps, 0);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Quality (0 = default)", &params.quality, 0);
    ImGui::Checkbox("360 (equirectangular 2:1)", &params.equirect360);
    if (params.equirect360) params.height = params.width / 2;
    params.width = std::clamp(params.width, 16, 16384) & ~1;
    params.height = std::clamp(params.height, 16, 16384) & ~1;
    params.fps = std::clamp(params.fps, 1, 240);
    params.quality = std::clamp(params.quality, 0, 63);
    ImGui::TextDisabled("ffmpeg must be installed and on PATH.");
    ImGui::Separator();
    bool accepted = false;
    if (ImGui::Button("OK", ImVec2(80, 0))) accepted = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) open = false;
    ImGui::End();
    if (accepted) {
        video_dlg.visible = false;
        params.transparent = MTVideoCodecIsAlpha(params.codec);
        MTConfFile conf;
        if (conf.Initialize("Video") == 0) {
            conf.SetCurSection("Video");
            conf.SetInt("Codec", static_cast<int>(params.codec));
            conf.SetInt("Width", params.width);
            conf.SetInt("Height", params.height);
            conf.SetInt("Fps", params.fps);
            conf.SetInt("Quality", params.quality);
            conf.SetInt("Transparent", params.transparent ? 1 : 0);
            conf.SetInt("Equirect360", params.equirect360 ? 1 : 0);
        }
        show_open_dialog(PendingExportSave);
    } else if (!open) {
        video_dlg.visible = false;
        unload_song();   // back to the title screen
    }
}

//******************************************************************************
// How to view (MTHowToViewDlg)
//******************************************************************************
void LinuxDX11Context::Impl::release_howto_textures() {
    for (ID3D11ShaderResourceView*& page : howto_dlg.pages) {
        if (page != nullptr) page->Release();
        page = nullptr;
    }
}

void LinuxDX11Context::Impl::open_howto_dialog() {
    if (howto_dlg.pages[0] == nullptr && renderer.GetDevice() != nullptr) {
        char dir[MAX_PATH] = {};
        YNBaseLib::YNPathUtil::GetModuleDirPath(dir, sizeof(dir));
        for (int page = 0; page < 3; ++page) {
            const std::string path = std::string(dir) + "data/HowToView" + std::to_string(page + 1) + ".bmp";
            int w = 0, h = 0, channels = 0;
            unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
            if (pixels == nullptr) continue;
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(w);
            desc.Height = static_cast<UINT>(h);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data = {pixels, static_cast<UINT>(w * 4), 0};
            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(renderer.GetDevice()->CreateTexture2D(&desc, &data, &texture))) {
                renderer.GetDevice()->CreateShaderResourceView(texture, nullptr, &howto_dlg.pages[page]);
                texture->Release();
                howto_dlg.page_w[page] = w;
                howto_dlg.page_h[page] = h;
            }
            stbi_image_free(pixels);
        }
    }
    howto_dlg.page = 0;
    howto_dlg.visible = true;
}

void LinuxDX11Context::Impl::disp_how_to_view_first_time() {
    view_conf.SetCurSection("HowToView");
    int count = 0;
    view_conf.GetInt("DispCount", &count, 0);
    if (count != 2) open_howto_dialog();
    view_conf.SetInt("DispCount", 2);
}

void LinuxDX11Context::Impl::draw_howto_dialog() {
    if (!howto_dlg.visible) return;
    center_next_window();
    if (!ImGui::Begin("How to view", &howto_dlg.visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const int page = howto_dlg.page;
    if (howto_dlg.pages[page] != nullptr) {
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(howto_dlg.pages[page])),
                     ImVec2(static_cast<float>(howto_dlg.page_w[page]), static_cast<float>(howto_dlg.page_h[page])));
    } else {
        ImGui::TextUnformatted("data/HowToView*.bmp not found.");
    }
    ImGui::BeginDisabled(page == 0);
    if (ImGui::Button("Previous", ImVec2(80, 0))) --howto_dlg.page;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(page >= 2);
    if (ImGui::Button("Next", ImVec2(80, 0))) ++howto_dlg.page;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(80, 0))) howto_dlg.visible = false;
    ImGui::End();
}

//******************************************************************************
// About (MTAboutDlg) / message boxes
//******************************************************************************
void LinuxDX11Context::Impl::draw_about_dialog() {
    if (!about_visible) return;
    center_next_window();
    if (ImGui::Begin("About MIDITrail", &about_visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("MIDITrail");
        ImGui::Text("Version %s", version_string.c_str());
        static const char* copyright = MIDITRAIL_COPYRIGHT   // the macro carries its own ';'
        ImGui::TextUnformatted(copyright);
        ImGui::TextDisabled("Linux build: SDL3 + DXVK Native + ImGui");
        if (ImGui::Button("OK", ImVec2(80, 0))) about_visible = false;
    }
    ImGui::End();
}

void LinuxDX11Context::Impl::draw_message_boxes() {
    if (message.pending && !ImGui::IsPopupOpen("##message")) ImGui::OpenPopup("##message");
    center_next_window();
    if (ImGui::BeginPopupModal("##message", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextUnformatted(message.title.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(560.0f);
        ImGui::TextUnformatted(message.text.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK", ImVec2(80, 0))) {
            message.pending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace miditrail::linux_native
