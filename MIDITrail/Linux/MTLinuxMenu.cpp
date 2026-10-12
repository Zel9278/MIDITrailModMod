#include "MTLinuxMenu.h"

#include "../resource.h"

#include <imgui.h>

namespace miditrail::linux_native {

namespace {

enum : int { NoData, Stop, Play, Pause, MonitorOFF, MonitorON };

// MIDITrailApp::_ChangeMenuStyle enable table
bool enabled_for(std::int32_t id, int status) {
    switch (id) {
    case IDM_ADD_FILE:
    case IDM_START_MONITORING:
    case IDM_VIEW_3DPIANOROLL: case IDM_VIEW_2DPIANOROLL: case IDM_VIEW_PIANOROLLRAIN:
    case IDM_VIEW_PIANOROLLRAIN2D: case IDM_VIEW_PIANOROLLRING: case IDM_VIEW_SINGLEKEYBOARD:
    case IDM_WINDOWSIZE:
    case IDM_OPTION_MIDIOUT: case IDM_OPTION_MIDIIN: case IDM_OPTION_GRAPHIC:
        return status == NoData || status == Stop || status == MonitorOFF;
    case IDM_PLAY: return status == Stop || status == Play || status == Pause;
    case IDM_STOP: return status == Play || status == Pause;
    case IDM_REPEAT: return status <= Pause;
    case IDM_SKIP_BACK: case IDM_SKIP_FORWARD: return status == Play;
    case IDM_PLAY_SPEED_DOWN: case IDM_PLAY_SPEED_UP: return status == Stop || status == Play || status == Pause;
    case IDM_STOP_MONITORING: return status == MonitorON;
    case IDM_RESET_VIEWPOINT: case IDM_VIEWPOINT2: case IDM_VIEWPOINT3:
    case IDM_SAVE_VIEWPOINT:
    case IDM_MYVIEWPOINT1: case IDM_MYVIEWPOINT2: case IDM_MYVIEWPOINT3:
    case IDM_SAVE_MYVIEWPOINT1: case IDM_SAVE_MYVIEWPOINT2: case IDM_SAVE_MYVIEWPOINT3:
        return status != NoData;
    case IDM_OPTION_COLOR: return status != Play && status != MonitorON;
    default: return true;
    }
}

} // namespace

LinuxMenu::LinuxMenu()
    : enabled_(true),
      visible_(true),
      pending_action_(0) {
}

void LinuxMenu::queue_action(std::int32_t action) {
    pending_action_ = action;
}

std::int32_t LinuxMenu::consume_action() {
    const std::int32_t action = pending_action_;
    pending_action_ = 0;
    return action;
}

void LinuxMenu::draw(const LinuxMenuState& state) {
    if (!visible_ || !ImGui::BeginMainMenuBar()) return;
    const int status = state.play_status;
    auto item = [this, status](const char* label, std::int32_t id, const char* shortcut = nullptr,
                               bool checked = false) {
        if (ImGui::MenuItem(label, shortcut, checked, enabled_for(id, status))) queue_action(id);
    };

    if (ImGui::BeginMenu("File")) {
        item("Open...", IDM_OPEN_FILE, "Ctrl+O");
        item("Add...", IDM_ADD_FILE);
        ImGui::Separator();
        item("Open Folder...", IDM_OPEN_FOLDER);
        if (ImGui::MenuItem("Previous File", nullptr, false, state.has_file_list)) queue_action(IDM_PREVIOUS_FILE);
        if (ImGui::MenuItem("Next File", nullptr, false, state.has_file_list)) queue_action(IDM_NEXT_FILE);
        item("Folder Playback", IDM_FOLDER_PLAYBACK, nullptr, state.folder_playback);
        ImGui::Separator();
        if (ImGui::MenuItem("Export Video...", nullptr, false, status != Play && status != MonitorON))
            queue_action(IDM_EXPORT_VIDEO);
        ImGui::Separator();
        item("Exit", IDM_EXIT);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Play")) {
        item(status == Play ? "Pause" : "Play", IDM_PLAY, "Space");
        item("Stop", IDM_STOP, "Esc");
        item("Repeat", IDM_REPEAT, nullptr, state.repeat);
        ImGui::Separator();
        item("Skip Back", IDM_SKIP_BACK, "1");
        item("Skip Forward", IDM_SKIP_FORWARD, "2");
        ImGui::Separator();
        item("Speed Down", IDM_PLAY_SPEED_DOWN, "4");
        item("Speed Up", IDM_PLAY_SPEED_UP, "5");
        ImGui::Separator();
        item("Start Monitoring", IDM_START_MONITORING, "Shift+Space");
        item("Stop Monitoring", IDM_STOP_MONITORING, "Esc");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        item("Piano Roll 3D", IDM_VIEW_3DPIANOROLL, nullptr, state.scene == 0);
        item("Piano Roll 2D", IDM_VIEW_2DPIANOROLL, nullptr, state.scene == 1);
        item("Piano Roll Rain", IDM_VIEW_PIANOROLLRAIN, nullptr, state.scene == 2);
        item("Piano Roll Rain 2D", IDM_VIEW_PIANOROLLRAIN2D, nullptr, state.scene == 3);
        item("Piano Roll Ring", IDM_VIEW_PIANOROLLRING, nullptr, state.scene == 4);
        ImGui::Separator();
        item("Single Keyboard", IDM_VIEW_SINGLEKEYBOARD, nullptr, state.single_keyboard);
        ImGui::Separator();
        item("Piano Keyboard", IDM_ENABLE_PIANOKEYBOARD, nullptr, state.piano_keyboard);
        item("Ripple", IDM_ENABLE_RIPPLE, nullptr, state.ripple);
        item("Lyrics", IDM_ENABLE_LYRICS, nullptr, state.lyrics);
        item("Pitch Bend Motion", IDM_ENABLE_PITCHBEND, nullptr, state.pitch_bend);
        item("Pitch Bend Whole Channel", IDM_ENABLE_PITCHBEND_ALLNOTES, nullptr, state.pitch_bend_all_notes);
        item("Stars", IDM_ENABLE_STARS, nullptr, state.stars);
        item("Counter", IDM_ENABLE_COUNTER, nullptr, state.counter);
        item("Background Image", IDM_ENABLE_BACKGROUNDIMAGE, nullptr, state.background_image);
        item("Time Indicator", IDM_ENABLE_TIMEINDICATOR, nullptr, state.time_indicator);
        item("Grid Box", IDM_ENABLE_GRIDBOX, nullptr, state.grid);
        ImGui::Separator();
        item("Viewpoint 1", IDM_RESET_VIEWPOINT, "7");
        item("Viewpoint 2", IDM_VIEWPOINT2, "8");
        item("Viewpoint 3", IDM_VIEWPOINT3, "9");
        item("Save Viewpoint", IDM_SAVE_VIEWPOINT);
        item("Auto save viewpoint", IDM_AUTO_SAVE_VIEWPOINT, nullptr, state.auto_save_viewpoint);
        item("Auto save view settings", IDM_AUTO_SAVE_VIEWSETTINGS, nullptr, state.auto_save_view_settings);
        ImGui::Separator();
        if (ImGui::BeginMenu("My Viewpoint", status != NoData)) {
            item("My Viewpoint 1", IDM_MYVIEWPOINT1);
            item("My Viewpoint 2", IDM_MYVIEWPOINT2);
            item("My Viewpoint 3", IDM_MYVIEWPOINT3);
            ImGui::Separator();
            item("Save My Viewpoint 1", IDM_SAVE_MYVIEWPOINT1);
            item("Save My Viewpoint 2", IDM_SAVE_MYVIEWPOINT2);
            item("Save My Viewpoint 3", IDM_SAVE_MYVIEWPOINT3);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        item("Window Size...", IDM_WINDOWSIZE);
        item("Full Screen", IDM_FULLSCREEN, "F11", state.fullscreen);
        item("Menu Bar", IDM_MENUBAR, "F10", enabled_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Option")) {
        item("MIDI OUT...", IDM_OPTION_MIDIOUT);
        item("MIDI IN...", IDM_OPTION_MIDIIN);
        item("Graphic...", IDM_OPTION_GRAPHIC);
        item("Color...", IDM_OPTION_COLOR);
        item("Config Manager...", IDM_OPTION_CONFIGMANAGER);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        item("How to view...", IDM_HOWTOVIEW);
        item("Manual...", IDM_MANUAL);
        ImGui::Separator();
        item("About MIDITrail...", IDM_ABOUT);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

} // namespace miditrail::linux_native
