#include "MTLinuxMidiOut.h"

namespace miditrail::linux_native {

LinuxMidiOut::LinuxMidiOut() = default;

LinuxMidiOut::~LinuxMidiOut() {
    close_all();
}

std::vector<std::string> LinuxMidiOut::enumerate_devices() const {
    std::vector<AlsaMidiPort> outputs;
    NativeAlsaMidi().enumerate(nullptr, &outputs);
    std::vector<std::string> names;
    for (const AlsaMidiPort& output : outputs) names.push_back(output.label);
    names.push_back(kKdmapiName);
    return names;
}

bool LinuxMidiOut::open_port(int port, const std::string& device, std::string* error) {
    if (port < 0 || port >= kPortCount) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    close_port(port);
    if (device.empty()) return true;

    if (device == kKdmapiName) {
        if (kdmapi_users_ == 0 && !kdmapi_.open(error)) return false;
        ++kdmapi_users_;
        ports_[port].kdmapi = true;
        ports_[port].device = device;
        return true;
    }

    std::vector<AlsaMidiPort> outputs;
    NativeAlsaMidi().enumerate(nullptr, &outputs);
    for (const AlsaMidiPort& output : outputs) {
        if (output.label != device) continue;
        auto alsa = std::make_unique<NativeAlsaMidi>();
        if (!alsa->open_output(output, error)) return false;
        ports_[port].alsa = std::move(alsa);
        ports_[port].device = device;
        return true;
    }
    if (error) *error = "MIDI OUT device not found: " + device;
    return false;
}

void LinuxMidiOut::close_port(int port) {
    if (port < 0 || port >= kPortCount) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Port& entry = ports_[port];
    if (entry.alsa) entry.alsa->close_output();
    entry.alsa.reset();
    if (entry.kdmapi) {
        entry.kdmapi = false;
        if (--kdmapi_users_ == 0) {
            kdmapi_.all_notes_off();
            kdmapi_.close();
        }
    }
    entry.device.clear();
}

void LinuxMidiOut::close_all() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (int port = 0; port < kPortCount; ++port) close_port(port);
}

bool LinuxMidiOut::port_open(int port) const {
    if (port < 0 || port >= kPortCount) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const Port& entry = ports_[port];
    return (entry.alsa && entry.alsa->output_open()) || (entry.kdmapi && kdmapi_.ready());
}

bool LinuxMidiOut::any_open() const {
    for (int port = 0; port < kPortCount; ++port) {
        if (port_open(port)) return true;
    }
    return false;
}

bool LinuxMidiOut::send(unsigned char port, const unsigned char* bytes, std::size_t size) {
    if (port >= kPortCount || bytes == nullptr || size == 0) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Port& entry = ports_[port];
    if (entry.alsa) return entry.alsa->send_bytes(std::vector<unsigned char>(bytes, bytes + size));
    if (entry.kdmapi) return kdmapi_.send_bytes(std::vector<unsigned char>(bytes, bytes + size));
    return false;
}

void LinuxMidiOut::all_notes_off() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    bool kdmapi_done = false;
    for (Port& entry : ports_) {
        if (entry.alsa) entry.alsa->all_notes_off();
        if (entry.kdmapi && !kdmapi_done) {
            kdmapi_.all_notes_off();
            kdmapi_done = true;
        }
    }
}

unsigned long LinuxMidiOut::sent_event_count(int port) const {
    if (port < 0 || port >= kPortCount) return 0;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const Port& entry = ports_[port];
    if (entry.alsa) return entry.alsa->sent_event_count();
    if (entry.kdmapi) return kdmapi_.sent_event_count();
    return 0;
}

std::string LinuxMidiOut::last_error(int port) const {
    if (port < 0 || port >= kPortCount) return {};
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const Port& entry = ports_[port];
    if (entry.alsa) return entry.alsa->last_error();
    if (entry.kdmapi) return kdmapi_.last_error();
    return {};
}

} // namespace miditrail::linux_native
