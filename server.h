#include <utility>

#ifndef OS_NET_MULTIPLEXING_SERVER_H
#define OS_NET_MULTIPLEXING_SERVER_H

#include <iostream>
#include <vector>
#include <queue>
#include <cstring>
#include <errno.h>
#include <arpa/inet.h>


#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "fd.h"

const std::string HELP = R"SEQ(Usage:
    -help                       - usage this program
    <address> <port>			- address and port, default address = 127.0.0.1, port = 10005
)SEQ";

static const size_t EVENTS = 1 << 10;

struct server_data {
    int fd;
    bool is_correct;

    size_t message_length;
    size_t message_length_read;
    size_t message_length_sent;
    bool have_sent_length;
    std::string message;

    server_data() = default;

    server_data(int fd, bool is_correct = true)
            : fd(fd),
              is_correct(is_correct),
              message_length(0),
              message_length_read(0),
              message_length_sent(0),
              have_sent_length(false),
              message("") {}
};

void error(const std::string &message, bool with_errno = true, bool help = false, bool need_exit = false) {
    std::cerr << message;

    if (with_errno) {
        std::cerr << ". " << strerror(errno);
    }
    std::cerr << std::endl;

    if (help) {
        std::cerr << HELP << std::endl;
    }

    if (need_exit) {
        exit(EXIT_FAILURE);
    }
}

struct server {
    explicit server(const char *address, const char *port);

    void run();

private:
    sockaddr_in socket_addr{};
    fd_wrapper socket_fd;
};

int main(int argc, char *argv[]);

#endif // OS_NET__MULTIPLEXING_SERVER_H


