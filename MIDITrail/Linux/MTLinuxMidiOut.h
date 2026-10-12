#pragma once

#include "MTNativeAlsaMidi.h"
#include "MTNativeKdmapi.h"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace miditrail::linux_native {

// MIDI OUT ports A-F (SM_MIDIOUT_PORT_NUM_MAX on Windows). Each port is bound to
// one device: an ALSA sequencer destination or KDMAPI (libOmniMIDI.so). Events are
// routed by the SMF port number, like the Windows SMOutDevCtrl.
class LinuxMidiOut {
public:
    static constexpr int kPortCount = 6;
    static constexpr const char* kKdmapiName = "KDMAPI (OmniMIDI)";

    LinuxMidiOut();
    ~LinuxMidiOut();

    // device names selectable for a port (ALSA outputs + KDMAPI)
    std::vector<std::string> enumerate_devices() const;

    // bind a port to a device by name ("" = none). Returns false (with error) on failure.
    bool open_port(int port, const std::string& device, std::string* error);
    void close_port(int port);
    void close_all();
    const std::string& port_device(int port) const { return ports_[port].device; }
    bool port_open(int port) const;
    bool any_open() const;

    bool send(unsigned char port, const unsigned char* bytes, std::size_t size);
    bool send(unsigned char port, const std::vector<unsigned char>& bytes) {
        return send(port, bytes.data(), bytes.size());
    }
    void all_notes_off();
    unsigned long sent_event_count(int port) const;
    std::string last_error(int port) const;
    // libOmniMIDI.so in use (empty while no port uses KDMAPI)
    std::string kdmapi_library_path() const;

private:
    struct Port {
        std::string device;
        std::unique_ptr<NativeAlsaMidi> alsa;
        bool kdmapi = false;
    };

    std::array<Port, kPortCount> ports_;
    NativeKdmapi kdmapi_;
    int kdmapi_users_ = 0;
    mutable std::recursive_mutex mutex_;
};

} // namespace miditrail::linux_native
