#pragma once

#include <string>
#include <vector>

namespace miditrail::linux_native {

// Player.ini [PlayerControl] AllowMultipleInstances=0: the first instance listens
// on an abstract Unix socket; a second launch forwards its file path there and
// exits (MIDITrailApp's named mutex + mailslot on Windows).
class LinuxSingleInstance {
public:
    LinuxSingleInstance() = default;
    ~LinuxSingleInstance();

    static bool multiple_instances_allowed();

    // true: another instance is running and received `path` (may be empty)
    bool forward_to_running_instance(const std::string& path);
    bool listen();
    std::vector<std::string> poll();

private:
    int listen_fd_ = -1;
    std::vector<int> clients_;
    std::vector<std::string> buffers_;
};

} // namespace miditrail::linux_native
