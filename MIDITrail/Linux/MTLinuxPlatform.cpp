#include "MTLinuxPlatform.h"
#include "MTLinuxInput.h"
#include "../../XInput.h"

#include <SDL3/SDL.h>
#include <wsi/native_sdl3.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>

namespace {

SDL_Gamepad* g_gamepad = nullptr;

SDL_Gamepad* current_gamepad() {
    if (g_gamepad != nullptr && SDL_GamepadConnected(g_gamepad)) return g_gamepad;
    if (g_gamepad != nullptr) {
        SDL_CloseGamepad(g_gamepad);
        g_gamepad = nullptr;
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr && count > 0) g_gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
    return g_gamepad;
}

constexpr int kTriggerThreshold = 16000;

} // namespace

// XInput emulation for MTGamePadCtrl (camera stick / D-pad / X / Y control)
unsigned long XInputGetState(unsigned long userIndex, XINPUT_STATE* pState) {
    if (pState == nullptr || userIndex != 0) return ERROR_DEVICE_NOT_CONNECTED;
    SDL_Gamepad* gamepad = current_gamepad();
    if (gamepad == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    static std::uint32_t packet = 0;
    XINPUT_GAMEPAD& pad = pState->Gamepad;
    pad = {};
    const struct { SDL_GamepadButton button; std::uint16_t mask; } map[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT},
        {SDL_GAMEPAD_BUTTON_START, XINPUT_GAMEPAD_START},
        {SDL_GAMEPAD_BUTTON_BACK, XINPUT_GAMEPAD_BACK},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, XINPUT_GAMEPAD_LEFT_THUMB},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, XINPUT_GAMEPAD_RIGHT_THUMB},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {SDL_GAMEPAD_BUTTON_SOUTH, XINPUT_GAMEPAD_A},
        {SDL_GAMEPAD_BUTTON_EAST, XINPUT_GAMEPAD_B},
        {SDL_GAMEPAD_BUTTON_WEST, XINPUT_GAMEPAD_X},
        {SDL_GAMEPAD_BUTTON_NORTH, XINPUT_GAMEPAD_Y},
    };
    for (const auto& entry : map) {
        if (SDL_GetGamepadButton(gamepad, entry.button)) pad.wButtons |= entry.mask;
    }
    auto trigger = [gamepad](SDL_GamepadAxis axis) {
        return static_cast<std::uint8_t>(std::clamp(SDL_GetGamepadAxis(gamepad, axis) / 128, 0, 255));
    };
    // SDL Y axes point down; XInput Y axes point up
    auto inverted = [gamepad](SDL_GamepadAxis axis) {
        return static_cast<std::int16_t>(std::clamp(-static_cast<int>(SDL_GetGamepadAxis(gamepad, axis)), -32768, 32767));
    };
    pad.bLeftTrigger = trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    pad.bRightTrigger = trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    pad.sThumbLX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    pad.sThumbLY = inverted(SDL_GAMEPAD_AXIS_LEFTY);
    pad.sThumbRX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    pad.sThumbRY = inverted(SDL_GAMEPAD_AXIS_RIGHTY);
    pState->dwPacketNumber = ++packet;
    return ERROR_SUCCESS;
}

namespace miditrail::linux_native {

LinuxPlatform::LinuxPlatform()
    : window_(nullptr),
      pending_actions_(LinuxActionNone),
      pending_gamepad_(0),
      quit_requested_(false),
      fullscreen_(false),
      left_trigger_down_(false),
      right_trigger_down_(false) {
}

LinuxPlatform::~LinuxPlatform() {
    if (g_gamepad != nullptr) SDL_CloseGamepad(g_gamepad);
    g_gamepad = nullptr;
    if (window_ != nullptr) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool LinuxPlatform::initialize(const char* title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) return false;
    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    return window_ != nullptr;
}

bool LinuxPlatform::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        const bool has_imgui = ImGui::GetCurrentContext() != nullptr;
        if (has_imgui) ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) return false;
        const bool capture_mouse = has_imgui && ImGui::GetIO().WantCaptureMouse;
        const bool capture_keyboard = has_imgui && ImGui::GetIO().WantCaptureKeyboard;
        switch (event.type) {
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data != nullptr) dropped_files_.emplace_back(event.drop.data);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!capture_mouse && event.button.button == SDL_BUTTON_LEFT) pending_actions_ |= LinuxActionToggleMouseLook;
            if (!capture_mouse && event.button.button == SDL_BUTTON_MIDDLE) pending_actions_ |= LinuxActionToggleAutoRoll;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (!capture_mouse) add_mouse_wheel_delta(event.wheel.y);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            switch (event.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_SOUTH: pending_gamepad_ |= LinuxGamepadA; break;
            case SDL_GAMEPAD_BUTTON_EAST: pending_gamepad_ |= LinuxGamepadB; break;
            case SDL_GAMEPAD_BUTTON_START: pending_gamepad_ |= LinuxGamepadStart; break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: pending_gamepad_ |= LinuxGamepadLeftShoulder; break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: pending_gamepad_ |= LinuxGamepadRightShoulder; break;
            default: break;
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const bool down = event.gaxis.value > kTriggerThreshold;
            if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                if (down && !left_trigger_down_) pending_gamepad_ |= LinuxGamepadLeftTrigger;
                left_trigger_down_ = down;
            } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                if (down && !right_trigger_down_) pending_gamepad_ |= LinuxGamepadRightTrigger;
                right_trigger_down_ = down;
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (event.key.repeat || capture_keyboard) break;
            const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
            switch (event.key.key) {
            case SDLK_SPACE:
            case SDLK_KP_0:
                pending_actions_ |= shift ? LinuxActionStartMonitoring : LinuxActionPlayPause;
                break;
            case SDLK_ESCAPE: pending_actions_ |= LinuxActionStop; break;
            case SDLK_KP_ENTER: pending_actions_ |= LinuxActionStopOnly; break;
            case SDLK_1: case SDLK_KP_1: pending_actions_ |= LinuxActionSkipBack; break;
            case SDLK_2: case SDLK_KP_2: pending_actions_ |= LinuxActionSkipForward; break;
            case SDLK_4: case SDLK_KP_4: pending_actions_ |= LinuxActionSpeedDown; break;
            case SDLK_5: case SDLK_KP_5: pending_actions_ |= LinuxActionSpeedUp; break;
            case SDLK_7: case SDLK_KP_7: pending_actions_ |= LinuxActionViewpoint1; break;
            case SDLK_8: case SDLK_KP_8: pending_actions_ |= LinuxActionViewpoint2; break;
            case SDLK_9: case SDLK_KP_9: pending_actions_ |= LinuxActionViewpoint3; break;
            case SDLK_O: if (ctrl) pending_actions_ |= LinuxActionOpenFile; break;
            case SDLK_F10: pending_actions_ |= LinuxActionToggleMenuBar; break;
            case SDLK_F11: pending_actions_ |= LinuxActionFullscreen; break;
            default: break;
            }
            break;
        }
        default:
            break;
        }
    }
    return !quit_requested_;
}

std::uint32_t LinuxPlatform::consume_actions() {
    const std::uint32_t actions = pending_actions_;
    pending_actions_ = LinuxActionNone;
    return actions;
}

std::uint32_t LinuxPlatform::consume_gamepad_buttons() {
    const std::uint32_t buttons = pending_gamepad_;
    pending_gamepad_ = 0;
    return buttons;
}

std::vector<std::string> LinuxPlatform::consume_dropped_files() {
    std::vector<std::string> files;
    files.swap(dropped_files_);
    return files;
}

bool LinuxPlatform::key_down_f2() const {
    if (window_ == nullptr || SDL_GetKeyboardFocus() != window_) return false;
    const bool* state = SDL_GetKeyboardState(nullptr);
    return state != nullptr && state[SDL_SCANCODE_F2];
}

bool LinuxPlatform::set_relative_mouse_mode(bool enabled) {
    return SDL_SetWindowRelativeMouseMode(window_, enabled);
}

void LinuxPlatform::request_quit() {
    quit_requested_ = true;
}

bool LinuxPlatform::set_fullscreen(bool enabled) {
    if (!SDL_SetWindowFullscreen(window_, enabled)) return false;
    fullscreen_ = enabled;
    return true;
}

bool LinuxPlatform::set_window_size(int width, int height) {
    if (window_ == nullptr || width < 160 || height < 120) return false;
    return SDL_SetWindowSize(window_, width, height);
}

void LinuxPlatform::get_drawable_size(std::uint32_t* width, std::uint32_t* height) const {
    int pixel_width = 1;
    int pixel_height = 1;
    SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height);
    *width = static_cast<std::uint32_t>(pixel_width > 0 ? pixel_width : 1);
    *height = static_cast<std::uint32_t>(pixel_height > 0 ? pixel_height : 1);
}

void* LinuxPlatform::native_window_handle() const {
    return dxvk::wsi::toHwnd(window_);
}

const char* LinuxPlatform::last_error() const {
    return SDL_GetError();
}

} // namespace miditrail::linux_native
