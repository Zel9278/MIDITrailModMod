#pragma once

#include <cstdint>

namespace miditrail::linux_native {

// play status index used by the enable table (MIDITrailApp::_ChangeMenuStyle):
// 0 NoData, 1 Stop, 2 Play, 3 Pause, 4 MonitorOFF, 5 MonitorON
struct LinuxMenuState {
    int play_status = 0;
    int scene = 0;
    bool repeat = false;
    bool folder_playback = false;
    bool single_keyboard = true;
    bool piano_keyboard = true;
    bool ripple = true;
    bool lyrics = true;
    bool pitch_bend = true;
    bool pitch_bend_all_notes = false;
    bool stars = true;
    bool counter = true;
    bool background_image = true;
    bool time_indicator = true;
    bool grid = true;
    bool auto_save_viewpoint = true;
    bool auto_save_view_settings = false;
    bool fullscreen = false;
    bool has_file_list = false;
};

class LinuxMenu {
public:
    LinuxMenu();

    // enabled = the Menu Bar setting; visible = drawn this frame (also shown
    // temporarily while the mouse is at the top edge, like the Windows app)
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    void set_visible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }
    void draw(const LinuxMenuState& state);
    std::int32_t consume_action();

private:
    void queue_action(std::int32_t action);

    bool enabled_;
    bool visible_;
    std::int32_t pending_action_;
};

} // namespace miditrail::linux_native
