#include "MTNativeKdmapi.h"

#include <dlfcn.h>
#include <cstring>
#include <cstdint>

namespace miditrail::linux_native {

namespace {
using IsAvailableFn = int (*)();
using InitializeFn = int (*)();
using TerminateFn = int (*)();
using ResetFn = void (*)();
using SendDirectDataFn = unsigned int (*)(unsigned long);

template <typename T>
T symbol(void* library, const char* name) {
    return reinterpret_cast<T>(dlsym(library, name));
}
}

NativeKdmapi::~NativeKdmapi() {
    close();
}

bool NativeKdmapi::open(std::string* error) {
    close();
    last_error_.clear();
    const char* candidates[] = {
        "libOmniMIDI.so",
        "/home/ced/Downloads/libOmniMIDI.so",
        "/home/ced/Downloads/zugg/libOmniMIDI.so"
    };
    for (const char* candidate : candidates) {
        library_ = dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
        if (library_ != nullptr) break;
    }
    if (library_ == nullptr) {
        last_error_ = "libOmniMIDI.so was not found";
        if (error) *error = last_error_;
        return false;
    }

    const auto available = symbol<IsAvailableFn>(library_, "IsKDMAPIAvailable");
    const auto initialize = symbol<InitializeFn>(library_, "InitializeKDMAPIStream");
    send_direct_data_ = symbol<SendDirectDataFn>(library_, "SendDirectData");
    send_direct_long_data_ = symbol<SendDirectLongDataFn>(library_, "SendDirectLongData");
    if (initialize == nullptr || symbol<TerminateFn>(library_, "TerminateKDMAPIStream") == nullptr
        || symbol<ResetFn>(library_, "ResetKDMAPIStream") == nullptr
        || send_direct_data_ == nullptr
        || send_direct_long_data_ == nullptr
        || (available != nullptr && available() == 0)) {
        last_error_ = "libOmniMIDI.so does not provide a usable KDMAPI stream";
        close();
        if (error) *error = last_error_;
        return false;
    }
    if (initialize() == 0) {
        last_error_ = "InitializeKDMAPIStream failed";
        close();
        if (error) *error = last_error_;
        return false;
    }
    initialized_ = true;
    sent_event_count_ = 0;
    return true;
}

void NativeKdmapi::close() {
    if (library_ != nullptr && initialized_) {
        if (const auto terminate = symbol<TerminateFn>(library_, "TerminateKDMAPIStream")) terminate();
    }
    initialized_ = false;
    if (library_ != nullptr) dlclose(library_);
    library_ = nullptr;
    send_direct_data_ = nullptr;
    send_direct_long_data_ = nullptr;
}

bool NativeKdmapi::send_bytes(const std::vector<unsigned char>& bytes) {
    if (!initialized_ || bytes.empty()) return false;
    if ((bytes[0] & 0xf0) == 0xf0) {
        MidiHeader header;
        header.data = reinterpret_cast<char*>(const_cast<unsigned char*>(bytes.data()));
        header.buffer_length = static_cast<unsigned long>(bytes.size());
        header.bytes_recorded = header.buffer_length;
        if (send_direct_long_data_ == nullptr
            || send_direct_long_data_(&header, sizeof(header)) == 0) return false;
    } else {
        if (send_direct_data_ == nullptr || bytes.size() > 3) return false;
        unsigned long message = bytes[0];
        if (bytes.size() > 1) message |= static_cast<unsigned long>(bytes[1]) << 8;
        if (bytes.size() > 2) message |= static_cast<unsigned long>(bytes[2]) << 16;
        if (send_direct_data_(message) == 0) return false;
    }
    ++sent_event_count_;
    return true;
}

void NativeKdmapi::all_notes_off() {
    if (!initialized_) return;
    for (int channel = 0; channel < 16; ++channel) {
        send_bytes({static_cast<unsigned char>(0xb0 | channel), 123, 0});
        send_bytes({static_cast<unsigned char>(0xb0 | channel), 64, 0});
        send_bytes({static_cast<unsigned char>(0xb0 | channel), 120, 0});
    }
}

} // namespace miditrail::linux_native
