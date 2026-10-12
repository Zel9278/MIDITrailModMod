#include "MTNativeAlsaMidi.h"

#include <alsa/seqmid.h>

#include <algorithm>
#include <cstdio>

namespace miditrail::linux_native {

std::string AlsaMidiPort::address() const {
    return std::to_string(client) + ":" + std::to_string(port);
}

NativeAlsaMidi::NativeAlsaMidi() = default;

NativeAlsaMidi::~NativeAlsaMidi() {
    close();
}

void NativeAlsaMidi::enumerate(std::vector<AlsaMidiPort>* inputs,
                               std::vector<AlsaMidiPort>* outputs) const {
    if (inputs) inputs->clear();
    if (outputs) outputs->clear();
    snd_seq_t* sequence = nullptr;
    if (snd_seq_open(&sequence, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return;
    snd_seq_client_info_t* client_info = nullptr;
    snd_seq_port_info_t* port_info = nullptr;
    snd_seq_client_info_alloca(&client_info);
    snd_seq_port_info_alloca(&port_info);
    snd_seq_client_info_set_client(client_info, -1);
    while (snd_seq_query_next_client(sequence, client_info) >= 0) {
        const int client = snd_seq_client_info_get_client(client_info);
        const std::string client_name = snd_seq_client_info_get_name(client_info);
        snd_seq_port_info_set_client(port_info, client);
        snd_seq_port_info_set_port(port_info, -1);
        while (snd_seq_query_next_port(sequence, port_info) >= 0) {
            const unsigned int capability = snd_seq_port_info_get_capability(port_info);
            const unsigned int type = snd_seq_port_info_get_type(port_info);
            if ((type & SND_SEQ_PORT_TYPE_MIDI_GENERIC) == 0) continue;
            const int port = snd_seq_port_info_get_port(port_info);
            const std::string port_name = snd_seq_port_info_get_name(port_info);
            if (inputs && (capability & SND_SEQ_PORT_CAP_READ)) {
                inputs->push_back({client, port, true, client_name + ":" + port_name});
            }
            if (outputs && (capability & SND_SEQ_PORT_CAP_WRITE)) {
                outputs->push_back({client, port, false, client_name + ":" + port_name});
            }
        }
    }
    snd_seq_close(sequence);
}

bool NativeAlsaMidi::open_sequence(snd_seq_t** sequence, int mode, std::string* error) {
    if (snd_seq_open(sequence, "default", mode, 0) < 0) {
        if (error) *error = "ALSA sequencer open failed";
        return false;
    }
    snd_seq_set_client_name(*sequence, "MIDITrail Mod Mod");
    return true;
}

bool NativeAlsaMidi::parse_address(snd_seq_t* sequence, const AlsaMidiPort& port,
                                   snd_seq_addr_t* address, std::string* error) {
    address->client = port.client;
    address->port = port.port;
    if (port.client >= 0 && port.port >= 0) return true;
    if (snd_seq_parse_address(sequence, address, port.address().c_str()) < 0) {
        if (error) *error = "Invalid ALSA port address";
        return false;
    }
    return true;
}

bool NativeAlsaMidi::open_input(const AlsaMidiPort& source, std::string* error) {
    close_input();
    if (!open_sequence(&input_, SND_SEQ_OPEN_INPUT, error)) return false;
    input_port_ = snd_seq_create_simple_port(
        input_, "MIDI IN", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    snd_seq_addr_t address = {};
    if (input_port_ < 0 || !parse_address(input_, source, &address, error)
        || snd_seq_connect_from(input_, input_port_, address.client, address.port) < 0) {
        close_input();
        if (error && error->empty()) *error = "ALSA MIDI IN connection failed";
        return false;
    }
    snd_seq_nonblock(input_, 1);
    return true;
}

bool NativeAlsaMidi::open_output(const AlsaMidiPort& destination, std::string* error) {
    close_output();
    if (!open_sequence(&output_, SND_SEQ_OPEN_OUTPUT, error)) return false;
    output_port_ = snd_seq_create_simple_port(
        output_, "MIDI OUT", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    snd_seq_addr_t address = {};
    if (output_port_ < 0 || !parse_address(output_, destination, &address, error)
        || snd_seq_connect_to(output_, output_port_, address.client, address.port) < 0) {
        close_output();
        if (error && error->empty()) *error = "ALSA MIDI OUT connection failed";
        return false;
    }
    output_client_ = address.client;
    output_destination_ = address.port;
    output_address_ = destination.address();
    last_error_.clear();
    sent_event_count_ = 0;
    return true;
}

void NativeAlsaMidi::close_input() {
    if (input_) snd_seq_close(input_);
    input_ = nullptr;
    input_port_ = -1;
}

void NativeAlsaMidi::close_output() {
    if (output_) all_notes_off();
    if (output_) snd_seq_close(output_);
    output_ = nullptr;
    output_port_ = -1;
    output_client_ = -1;
    output_destination_ = -1;
    output_address_.clear();
}

void NativeAlsaMidi::close() {
    close_input();
    close_output();
}

void NativeAlsaMidi::poll(const EventCallback& callback) {
    if (!input_ || !callback) return;
    while (snd_seq_event_input_pending(input_, 1) > 0) {
        snd_seq_event_t* event = nullptr;
        if (snd_seq_event_input(input_, &event) < 0 || !event) break;
        callback(*event);
    }
}

bool NativeAlsaMidi::note_on(int channel, int note, int velocity) {
    if (!output_) return false;
    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    snd_seq_ev_set_direct(&event);
    snd_seq_ev_set_source(&event, output_port_);
    snd_seq_ev_set_dest(&event, output_client_, output_destination_);
    snd_seq_ev_set_noteon(&event, channel & 0xf, note & 0x7f, velocity & 0x7f);
    const int result = snd_seq_event_output_direct(output_, &event);
    if (result < 0) {
        last_error_ = snd_strerror(result);
        return false;
    }
    ++sent_event_count_;
    last_error_.clear();
    return true;
}

bool NativeAlsaMidi::note_off(int channel, int note) {
    if (!output_) return false;
    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    snd_seq_ev_set_direct(&event);
    snd_seq_ev_set_source(&event, output_port_);
    snd_seq_ev_set_dest(&event, output_client_, output_destination_);
    snd_seq_ev_set_noteoff(&event, channel & 0xf, note & 0x7f, 0);
    const int result = snd_seq_event_output_direct(output_, &event);
    if (result < 0) {
        last_error_ = snd_strerror(result);
        return false;
    }
    ++sent_event_count_;
    last_error_.clear();
    return true;
}

bool NativeAlsaMidi::send_bytes(const std::vector<unsigned char>& bytes) {
    if (!output_ || bytes.empty()) return false;
    const unsigned char status = bytes[0];
    const unsigned char kind = status & 0xf0;
    const int channel = status & 0x0f;
    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    snd_seq_ev_set_direct(&event);
    snd_seq_ev_set_source(&event, output_port_);
    snd_seq_ev_set_dest(&event, output_client_, output_destination_);
    if (kind == 0x80 && bytes.size() >= 3) {
        snd_seq_ev_set_noteoff(&event, channel, bytes[1] & 0x7f, bytes[2] & 0x7f);
    } else if (kind == 0x90 && bytes.size() >= 3) {
        snd_seq_ev_set_noteon(&event, channel, bytes[1] & 0x7f, bytes[2] & 0x7f);
    } else if (kind == 0xa0 && bytes.size() >= 3) {
        snd_seq_ev_set_keypress(&event, channel, bytes[1] & 0x7f, bytes[2] & 0x7f);
    } else if (kind == 0xb0 && bytes.size() >= 3) {
        snd_seq_ev_set_controller(&event, channel, bytes[1] & 0x7f, bytes[2] & 0x7f);
    } else if (kind == 0xc0 && bytes.size() >= 2) {
        snd_seq_ev_set_pgmchange(&event, channel, bytes[1] & 0x7f);
    } else if (kind == 0xd0 && bytes.size() >= 2) {
        snd_seq_ev_set_chanpress(&event, channel, bytes[1] & 0x7f);
    } else if (kind == 0xe0 && bytes.size() >= 3) {
        const int value = ((bytes[1] & 0x7f) | ((bytes[2] & 0x7f) << 7)) - 8192;
        snd_seq_ev_set_pitchbend(&event, channel, value);
    } else if (status == 0xf0 && bytes.size() >= 2
               && bytes.back() == 0xf7) {
        snd_seq_ev_set_sysex(&event, bytes.size(), const_cast<unsigned char*>(bytes.data()));
    } else {
        last_error_ = "Unsupported MIDI event";
        return false;
    }
    const int result = snd_seq_event_output_direct(output_, &event);
    if (result < 0) {
        last_error_ = snd_strerror(result);
        return false;
    }
    ++sent_event_count_;
    last_error_.clear();
    return true;
}

void NativeAlsaMidi::all_notes_off() {
    if (!output_) return;
    for (int channel = 0; channel < 16; ++channel) {
        send_bytes({static_cast<unsigned char>(0xb0 | channel), 64, 0});
        send_bytes({static_cast<unsigned char>(0xb0 | channel), 120, 0});
        send_bytes({static_cast<unsigned char>(0xb0 | channel), 123, 0});
    }
}

} // namespace miditrail::linux_native
