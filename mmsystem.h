#pragma once

#ifdef MIDITRAIL_LINUX_NATIVE

#include <cstdint>
#include <chrono>

using MMRESULT = unsigned int;
using UINT = unsigned int;
using DWORD_PTR = std::uintptr_t;
using HMIDIIN = void*;
using HMIDIOUT = void*;
using HMMIO = void*;
using LPMMIOINFO = void*;
using LPSTR = char*;
using HPSTR = char*;

struct MIDIHDR {
    LPSTR lpData = nullptr;
    unsigned long dwBufferLength = 0;
    unsigned long dwBytesRecorded = 0;
    std::uintptr_t dwUser = 0;
    unsigned long dwFlags = 0;
    void* lpNext = nullptr;
    std::uintptr_t reserved = 0;
    unsigned long dwOffset = 0;
    std::uintptr_t dwReserved[8] = {};
};

struct MMTIME {
    unsigned int wType = 0;
    union {
        unsigned long ms;
        unsigned long sample;
        unsigned long cb;
        unsigned long ticks;
        struct { unsigned char hour, min, sec, frame; } smpte;
        struct { unsigned long songptrpos; } midi;
    } u = {};
};

#define TIME_MS 0x0001
#define TIME_SAMPLES 0x0002
#define TIME_BYTES 0x0004
#define TIME_SMPTE 0x0008
#define TIME_MIDI 0x0010
#define TIME_TICKS 0x0020

inline unsigned long timeGetTime()
{
    using namespace std::chrono;
    return static_cast<unsigned long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

#ifndef MAXPNAMELEN
#define MAXPNAMELEN 32
#endif

#ifndef CALLBACK
#define CALLBACK
#endif

#endif
