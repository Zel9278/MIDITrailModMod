#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace miditrail::linux_native {

class NativeKdmapi {
public:
    NativeKdmapi() = default;
    ~NativeKdmapi();

    NativeKdmapi(const NativeKdmapi&) = delete;
    NativeKdmapi& operator=(const NativeKdmapi&) = delete;

    bool open(std::string* error);
    void close();
    bool send_bytes(const std::vector<unsigned char>& bytes);
    void all_notes_off();
    bool ready() const { return initialized_; }
    const std::string& last_error() const { return last_error_; }
    const std::string& library_path() const { return library_path_; }
    unsigned long sent_event_count() const { return sent_event_count_; }

private:
    using SendDirectDataFn = unsigned int (*)(unsigned long);

    struct MidiHeader {
        char* data = nullptr;
        unsigned long buffer_length = 0;
        unsigned long bytes_recorded = 0;
        std::uintptr_t user = 0;
        unsigned long flags = 0;
        void* next = nullptr;
        std::uintptr_t reserved = 0;
        unsigned long offset = 0;
        std::uintptr_t reserved2[8] = {};
    };

    using SendDirectLongDataFn = unsigned int (*)(MidiHeader*, unsigned int);

    void* library_ = nullptr;
    bool initialized_ = false;
    std::string last_error_;
    std::string library_path_;
    unsigned long sent_event_count_ = 0;
    SendDirectDataFn send_direct_data_ = nullptr;
    SendDirectLongDataFn send_direct_long_data_ = nullptr;
};

} // namespace miditrail::linux_native
