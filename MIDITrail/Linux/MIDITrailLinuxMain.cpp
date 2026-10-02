#include "MTLinuxDX11Context.h"
#include "MTLinuxPlatform.h"
#include "MTLinuxSingleInstance.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <strings.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t exit_signal = 0;

void handle_exit_signal(int signal_number) {
    exit_signal = signal_number;
}

void install_exit_signal_handlers() {
    struct sigaction action = {};
    action.sa_handler = handle_exit_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

bool is_midi_path(std::string_view argument) {
    if (argument.size() <= 4) return false;
    const std::size_t dot = argument.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    const std::string extension(argument.substr(dot));
    return strcasecmp(extension.c_str(), ".mid") == 0 || strcasecmp(extension.c_str(), ".midi") == 0;
}

} // namespace

int main(int argc, char** argv) {
    install_exit_signal_handlers();

    // MTCmdLineParser: first *.mid argument, -p play, -q quit at the end, -d dump.
    // --autoplay / --smoke-frames N are Linux test switches.
    miditrail::linux_native::LinuxCommandLine command_line;
    int smoke_frames = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "-p" || argument == "--autoplay") command_line.play = true;
        else if (argument == "-q") command_line.quit_at_end = true;
        else if (argument == "-d") command_line.debug_dump = true;
        else if (argument == "--smoke-frames" && index + 1 < argc) smoke_frames = std::max(0, std::atoi(argv[++index]));
        else if (command_line.midi_path.empty() && is_midi_path(argument)) command_line.midi_path = argv[index];
    }
    if (command_line.midi_path.empty()) {
        command_line.play = false;
        command_line.quit_at_end = false;
    }

    // single instance: hand the file to the running MIDITrail and exit
    miditrail::linux_native::LinuxSingleInstance single_instance;
    if (smoke_frames == 0 && !miditrail::linux_native::LinuxSingleInstance::multiple_instances_allowed()) {
        if (single_instance.forward_to_running_instance(command_line.midi_path)) return 0;
        single_instance.listen();
    }

    miditrail::linux_native::LinuxPlatform platform;
    if (!platform.initialize("MIDITrail", 800, 600)) {
        std::fprintf(stderr, "SDL3 initialization failed: %s\n", platform.last_error());
        return 1;
    }

    miditrail::linux_native::LinuxDX11Context graphics;
    if (!graphics.initialize(platform, command_line)) {
        std::fprintf(stderr, "DXVK Native initialization failed: 0x%08lx\n",
                     static_cast<unsigned long>(graphics.last_result()));
        return 2;
    }

    int rendered_frames = 0;
    while (exit_signal == 0 && platform.poll_events()) {
        for (const std::string& path : platform.consume_dropped_files()) graphics.open_dropped_file(path);
        for (const std::string& path : single_instance.poll()) {
            SDL_RaiseWindow(platform.sdl_window());
            if (!path.empty()) graphics.open_dropped_file(path);
        }
        graphics.handle_actions(platform, platform.consume_actions());
        if (!graphics.render(platform)) {
            std::fprintf(stderr, "DXVK Native rendering failed: 0x%08lx\n",
                         static_cast<unsigned long>(graphics.last_result()));
            return 3;
        }
        ++rendered_frames;
        if (smoke_frames > 0 && rendered_frames >= smoke_frames) break;
    }
    return exit_signal == 0 ? 0 : 128 + exit_signal;
}
