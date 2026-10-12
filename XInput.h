#pragma once

#ifdef MIDITRAIL_LINUX_NATIVE
#include <cstdint>

typedef std::uint32_t DWORD;

using XINPUT_USER_INDEX = unsigned long;
typedef struct _XINPUT_GAMEPAD {
    std::uint16_t wButtons;
    std::uint8_t bLeftTrigger;
    std::uint8_t bRightTrigger;
    std::int16_t sThumbLX;
    std::int16_t sThumbLY;
    std::int16_t sThumbRX;
    std::int16_t sThumbRY;
} XINPUT_GAMEPAD;
typedef struct _XINPUT_STATE {
    std::uint32_t dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

#define XUSER_MAX_COUNT 4
#define ERROR_SUCCESS 0L
#define ERROR_DEVICE_NOT_CONNECTED 1167L
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB 0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB 0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE 7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689

// Linux: implemented with SDL3 gamepads in MIDITrail/Linux/MTLinuxPlatform.cpp
unsigned long XInputGetState(unsigned long userIndex, XINPUT_STATE* pState);
#endif
