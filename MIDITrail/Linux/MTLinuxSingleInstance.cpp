#include "MTLinuxSingleInstance.h"

#include "MTLinuxCompat.h"
#include "../MTConfFile.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace miditrail::linux_native {

namespace {

socklen_t make_address(sockaddr_un* address) {
    std::memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    const std::string name = "MIDITrailModMod-" + std::to_string(getuid());
    // abstract namespace: sun_path[0] == '\0', nothing is left on disk
    std::memcpy(address->sun_path + 1, name.data(), name.size());
    return static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());
}

} // namespace

LinuxSingleInstance::~LinuxSingleInstance() {
    for (int fd : clients_) close(fd);
    if (listen_fd_ >= 0) close(listen_fd_);
}

bool LinuxSingleInstance::multiple_instances_allowed() {
    MTConfFile conf;
    int allow = 0;
    if (conf.Initialize("Player") == 0 && conf.SetCurSection("PlayerControl") == 0) {
        conf.GetInt("AllowMultipleInstances", &allow, 0);
    }
    return allow > 0;
}

bool LinuxSingleInstance::forward_to_running_instance(const std::string& path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    sockaddr_un address;
    const socklen_t length = make_address(&address);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), length) != 0) {
        close(fd);
        return false;
    }
    std::string message = path;
    if (!message.empty() && message[0] != '/') {
        char resolved[PATH_MAX];
        if (realpath(path.c_str(), resolved) != nullptr) message = resolved;
    }
    message += '\n';
    std::size_t sent = 0;
    while (sent < message.size()) {
        const ssize_t written = send(fd, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (written <= 0) break;
        sent += static_cast<std::size_t>(written);
    }
    close(fd);
    return true;
}

bool LinuxSingleInstance::listen() {
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) return false;
    sockaddr_un address;
    const socklen_t length = make_address(&address);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), length) != 0 || ::listen(listen_fd_, 4) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    return true;
}

std::vector<std::string> LinuxSingleInstance::poll() {
    std::vector<std::string> paths;
    if (listen_fd_ < 0) return paths;
    for (;;) {
        const int client = accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) break;
        clients_.push_back(client);
        buffers_.emplace_back();
    }
    for (std::size_t index = 0; index < clients_.size();) {
        char chunk[1024];
        const ssize_t received = recv(clients_[index], chunk, sizeof(chunk), 0);
        if (received > 0) {
            buffers_[index].append(chunk, static_cast<std::size_t>(received));
            continue;
        }
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            ++index;
            continue;
        }
        // connection closed: one path per line
        std::string& buffer = buffers_[index];
        std::size_t start = 0;
        for (std::size_t end; (end = buffer.find('\n', start)) != std::string::npos; start = end + 1) {
            paths.push_back(buffer.substr(start, end - start));
        }
        close(clients_[index]);
        clients_.erase(clients_.begin() + static_cast<long>(index));
        buffers_.erase(buffers_.begin() + static_cast<long>(index));
    }
    return paths;
}

} // namespace miditrail::linux_native
