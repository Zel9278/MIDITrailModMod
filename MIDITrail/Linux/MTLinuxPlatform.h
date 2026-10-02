#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace miditrail::linux_native {

enum LinuxAction : std::uint32_t {
    LinuxActionNone = 0,
    LinuxActionToggleMouseLook = 1u << 0,
    LinuxActionToggleAutoRoll = 1u << 1,
    LinuxActionViewpoint1 = 1u << 2,
    LinuxActionViewpoint2 = 1u << 3,
    LinuxActionViewpoint3 = 1u << 4,
    LinuxActionPlayPause = 1u << 5,
    LinuxActionStop = 1u << 6,           // Esc: stop monitoring, else stop
    LinuxActionSkipBack = 1u << 7,
    LinuxActionSkipForward = 1u << 8,
    LinuxActionSpeedDown = 1u << 9,
    LinuxActionSpeedUp = 1u << 10,
    LinuxActionToggleMenuBar = 1u << 11,
    LinuxActionStartMonitoring = 1u << 12,
    LinuxActionOpenFile = 1u << 13,
    LinuxActionFullscreen = 1u << 14,
    LinuxActionStopOnly = 1u << 15,      // numpad Enter
};

// gamepad buttons pressed since the last poll (MIDITrailApp::_GamePadProc)
enum LinuxGamepadButton : std::uint32_t {
    LinuxGamepadA = 1u << 0,
    LinuxGamepadB = 1u << 1,
    LinuxGamepadStart = 1u << 2,
    LinuxGamepadLeftShoulder = 1u << 3,
    LinuxGamepadRightShoulder = 1u << 4,
    LinuxGamepadLeftTrigger = 1u << 5,
    LinuxGamepadRightTrigger = 1u << 6,
};

class LinuxPlatform {
public:
    LinuxPlatform();
    ~LinuxPlatform();

    LinuxPlatform(const LinuxPlatform&) = delete;
    LinuxPlatform& operator=(const LinuxPlatform&) = delete;

    bool initialize(const char* title, int width, int height);
    bool poll_events();
    std::uint32_t consume_actions();
    std::uint32_t consume_gamepad_buttons();
    std::vector<std::string> consume_dropped_files();
    bool key_down_f2() const;
    bool set_relative_mouse_mode(bool enabled);
    void request_quit();
    bool set_fullscreen(bool enabled);
    bool set_window_size(int width, int height);
    bool fullscreen() const { return fullscreen_; }
    void get_drawable_size(std::uint32_t* width, std::uint32_t* height) const;
    void* native_window_handle() const;
    SDL_Window* sdl_window() const { return window_; }
    const char* last_error() const;

private:
    SDL_Window* window_;
    std::uint32_t pending_actions_;
    std::uint32_t pending_gamepad_;
    std::vector<std::string> dropped_files_;
    bool quit_requested_;
    bool fullscreen_;
    bool left_trigger_down_;
    bool right_trigger_down_;
};

} // namespace miditrail::linux_native
