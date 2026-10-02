#include <windows.h>
#include "MTLinuxCompat.h"
#include "MTLinuxInput.h"
#include "../DIKeyCtrl.h"
#include "../DIMouseCtrl.h"
#include <SDL3/SDL.h>
#include <cstring>

namespace {

int mouse_wheel_delta = 0;

void set_key(unsigned char* keys, const bool* state, unsigned char dik, SDL_Scancode scancode) {
    keys[dik] = state[scancode] ? 0x80 : 0;
}

} // namespace

namespace miditrail::linux_native {

void add_mouse_wheel_delta(float delta) {
    mouse_wheel_delta += static_cast<int>(delta * 120.0f);
}

} // namespace miditrail::linux_native

DIKeyCtrl::DIKeyCtrl() : m_pDI(nullptr), m_pDIDevice(nullptr) { std::memset(m_KeyStatus, 0, sizeof(m_KeyStatus)); }
DIKeyCtrl::~DIKeyCtrl() = default;
int DIKeyCtrl::Initialize(HWND) { return 0; }
void DIKeyCtrl::Terminate() { m_pDI = nullptr; m_pDIDevice = nullptr; }
int DIKeyCtrl::Acquire() { return 0; }
int DIKeyCtrl::Unacquire() { return 0; }
int DIKeyCtrl::GetKeyStatus() {
    std::memset(m_KeyStatus, 0, sizeof(m_KeyStatus));
    const bool* state = SDL_GetKeyboardState(nullptr);
    set_key(m_KeyStatus, state, DIK_W, SDL_SCANCODE_W);
    set_key(m_KeyStatus, state, DIK_A, SDL_SCANCODE_A);
    set_key(m_KeyStatus, state, DIK_S, SDL_SCANCODE_S);
    set_key(m_KeyStatus, state, DIK_D, SDL_SCANCODE_D);
    set_key(m_KeyStatus, state, DIK_Q, SDL_SCANCODE_Q);
    set_key(m_KeyStatus, state, DIK_E, SDL_SCANCODE_E);
    set_key(m_KeyStatus, state, DIK_Z, SDL_SCANCODE_Z);
    set_key(m_KeyStatus, state, DIK_C, SDL_SCANCODE_C);
    set_key(m_KeyStatus, state, DIK_COMMA, SDL_SCANCODE_COMMA);
    set_key(m_KeyStatus, state, DIK_PERIOD, SDL_SCANCODE_PERIOD);
    set_key(m_KeyStatus, state, DIK_UP, SDL_SCANCODE_UP);
    set_key(m_KeyStatus, state, DIK_DOWN, SDL_SCANCODE_DOWN);
    set_key(m_KeyStatus, state, DIK_LEFT, SDL_SCANCODE_LEFT);
    set_key(m_KeyStatus, state, DIK_RIGHT, SDL_SCANCODE_RIGHT);
    set_key(m_KeyStatus, state, DIK_PRIOR, SDL_SCANCODE_PAGEUP);
    set_key(m_KeyStatus, state, DIK_NEXT, SDL_SCANCODE_PAGEDOWN);
    set_key(m_KeyStatus, state, DIK_LSHIFT, SDL_SCANCODE_LSHIFT);
    set_key(m_KeyStatus, state, DIK_RSHIFT, SDL_SCANCODE_RSHIFT);
    set_key(m_KeyStatus, state, DIK_LCONTROL, SDL_SCANCODE_LCTRL);
    set_key(m_KeyStatus, state, DIK_RCONTROL, SDL_SCANCODE_RCTRL);
    return 0;
}
bool DIKeyCtrl::IsKeyDown(unsigned char key) { return m_KeyStatus[key] != 0; }

DIMouseCtrl::DIMouseCtrl() : m_pDI(nullptr), m_pDIDevice(nullptr) { std::memset(&m_MouseState, 0, sizeof(m_MouseState)); }
DIMouseCtrl::~DIMouseCtrl() = default;
int DIMouseCtrl::Initialize(HWND) { return 0; }
void DIMouseCtrl::Terminate() { m_pDI = nullptr; m_pDIDevice = nullptr; }
int DIMouseCtrl::Acquire() { return 0; }
int DIMouseCtrl::Unacquire() { return 0; }
int DIMouseCtrl::GetMouseStatus() {
    std::memset(&m_MouseState, 0, sizeof(m_MouseState));
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetRelativeMouseState(&delta_x, &delta_y);
    m_MouseState.lX = static_cast<std::int32_t>(delta_x);
    m_MouseState.lY = static_cast<std::int32_t>(delta_y);
    m_MouseState.lZ = mouse_wheel_delta;
    mouse_wheel_delta = 0;
    m_MouseState.rgbButtons[0] = (buttons & SDL_BUTTON_LMASK) ? 0x80 : 0;
    m_MouseState.rgbButtons[1] = (buttons & SDL_BUTTON_RMASK) ? 0x80 : 0;
    m_MouseState.rgbButtons[2] = (buttons & SDL_BUTTON_MMASK) ? 0x80 : 0;
    return 0;
}
bool DIMouseCtrl::IsBtnDown(MouseButton button) { return m_MouseState.rgbButtons[static_cast<int>(button)] != 0; }
int DIMouseCtrl::GetDelta(MouseAxis axis) { return axis == AxisX ? m_MouseState.lX : axis == AxisY ? m_MouseState.lY : m_MouseState.lZ; }
int DIMouseCtrl::GetBuffer(bool* exists, MouseEvent*, int*) { if (exists) *exists = false; return 0; }
