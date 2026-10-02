#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace miditrail::linux_native {

class LinuxPlatform;

// command line switches (MTCmdLineParser: -p play, -q quit at end, -d dump)
struct LinuxCommandLine {
    std::string midi_path;
    bool play = false;
    bool quit_at_end = false;
    bool debug_dump = false;
};

class LinuxDX11Context {
public:
    LinuxDX11Context();
    ~LinuxDX11Context();

    LinuxDX11Context(const LinuxDX11Context&) = delete;
    LinuxDX11Context& operator=(const LinuxDX11Context&) = delete;

    bool initialize(LinuxPlatform& platform, const LinuxCommandLine& command_line);
    void handle_actions(LinuxPlatform& platform, std::uint32_t actions);
    void open_dropped_file(const std::string& path);
    bool render(LinuxPlatform& platform);
    long last_result() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace miditrail::linux_native
