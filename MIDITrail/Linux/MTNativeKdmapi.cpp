#include "MTNativeKdmapi.h"

#include <dlfcn.h>
#include <unistd.h>
#include <climits>
#include <cstdlib>
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
    library_path_.clear();
    // search order: $MIDITRAIL_OMNIMIDI, next to the executable, then the system
    // library path (LD_LIBRARY_PATH / ld.so.cache / /usr/lib64 ...)
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("MIDITRAIL_OMNIMIDI"); env != nullptr && env[0] != '\0') {
        candidates.push_back(env);
    }
    {
        char exe[PATH_MAX] = {};
        const ssize_t length = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (length > 0) {
            std::string dir(exe, static_cast<std::size_t>(length));
            dir.resize(dir.find_last_of('/') + 1);
            candidates.push_back(dir + "libOmniMIDI.so");
        }
    }
    candidates.push_back("libOmniMIDI.so");
    std::string tried;
    for (const std::string& candidate : candidates) {
        library_ = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (library_ != nullptr) break;
        const char* reason = dlerror();
        tried += "\n  " + candidate + ": " + (reason != nullptr ? reason : "not found");
    }
    if (library_ == nullptr) {
        last_error_ = "libOmniMIDI.so was not found (set MIDITRAIL_OMNIMIDI or put it next to MIDITrailLinux)" + tried;
        if (error) *error = last_error_;
        return false;
    }

    const auto available = symbol<IsAvailableFn>(library_, "IsKDMAPIAvailable");
    const auto initialize = symbol<InitializeFn>(library_, "InitializeKDMAPIStream");
    {
        // the file actually loaded (also when it came from the system search path)
        Dl_info info = {};
        if (initialize != nullptr && dladdr(reinterpret_cast<void*>(initialize), &info) != 0
            && info.dli_fname != nullptr) {
            char resolved[PATH_MAX];
            library_path_ = realpath(info.dli_fname, resolved) != nullptr ? resolved : info.dli_fname;
        }
    }
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
