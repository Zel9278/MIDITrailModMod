#pragma once

#include <alsa/asoundlib.h>

#include <functional>
#include <string>
#include <vector>

namespace miditrail::linux_native {

struct AlsaMidiPort {
    int client = -1;
    int port = -1;
    bool input = false;
    std::string label;

    std::string address() const;
};

class NativeAlsaMidi {
public:
    using EventCallback = std::function<void(const snd_seq_event_t&)>;

    NativeAlsaMidi();
    ~NativeAlsaMidi();

    void enumerate(std::vector<AlsaMidiPort>* inputs, std::vector<AlsaMidiPort>* outputs) const;
    bool open_input(const AlsaMidiPort& source, std::string* error);
    bool open_output(const AlsaMidiPort& destination, std::string* error);
    void close_input();
    void close_output();
    void close();
    void poll(const EventCallback& callback);
    bool note_on(int channel, int note, int velocity);
    bool note_off(int channel, int note);
    bool send_bytes(const std::vector<unsigned char>& bytes);
    void all_notes_off();
    bool input_open() const { return input_ != nullptr; }
    bool output_open() const { return output_ != nullptr; }
    const std::string& output_address() const { return output_address_; }
    const std::string& last_error() const { return last_error_; }
    unsigned long sent_event_count() const { return sent_event_count_; }

private:
    static bool open_sequence(snd_seq_t** sequence, int mode, std::string* error);
    static bool parse_address(snd_seq_t* sequence, const AlsaMidiPort& port,
                              snd_seq_addr_t* address, std::string* error);

    snd_seq_t* input_ = nullptr;
    snd_seq_t* output_ = nullptr;
    int input_port_ = -1;
    int output_port_ = -1;
    int output_client_ = -1;
    int output_destination_ = -1;
    std::string output_address_;
    std::string last_error_;
    unsigned long sent_event_count_ = 0;
};

} // namespace miditrail::linux_native
