#pragma once
#ifdef MIDITRAIL_LINUX_NATIVE
#include <cstdint>
using LPDIRECTINPUT8 = void*;
using LPDIRECTINPUTDEVICE8 = void*;
struct DIMOUSESTATE2 { std::int32_t lX, lY, lZ; std::uint8_t rgbButtons[8]; };
struct DIDEVICEOBJECTDATA { std::uint32_t dwOfs; std::uint32_t dwData; std::uint32_t dwTimeStamp; std::uintptr_t dwSequence; std::uintptr_t uAppData; };
#define DI_OK 0
#define DIERR_NOTACQUIRED (-1)
#define DIERR_INPUTLOST (-2)
#define DIERR_OTHERAPPHASPRIO (-3)
#define DIERR_GENERIC (-4)
#define DIERR_DEVICENOTREG (-5)
#define DIERR_INVALIDPARAM (-6)
#define DIMOFS_BUTTON0 0
#define DIMOFS_BUTTON1 1
#define DIMOFS_BUTTON2 2
#define DIMOFS_X 3
#define DIMOFS_Y 4
#define DIMOFS_Z 5
#define DIK_COMMA 0x33
#define DIK_PERIOD 0x34
#define DIK_LEFT 0xcb
#define DIK_RIGHT 0xcd
#define DIK_UP 0xc8
#define DIK_DOWN 0xd0
#define DIK_PRIOR 0xc9
#define DIK_NEXT 0xd1
#define DIK_RSHIFT 0x36
#define DIK_LCONTROL 0x1d
#define DIK_RCONTROL 0x9d
#define DIK_A 0x1e
#define DIK_D 0x20
#define DIK_S 0x1f
#define DIK_W 0x11
#define DIK_C 0x2e
#define DIK_E 0x12
#define DIK_LSHIFT 0x2a
#define DIK_Q 0x10
#define DIK_Z 0x2c
#endif
