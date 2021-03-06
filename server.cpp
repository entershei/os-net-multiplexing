#include "server.h"

server::server(const char *address, const char *port) : socket_fd(socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) {
    if (socket_fd.get_fd() == -1) {
        throw std::runtime_error("Can't create socket");
    }

    try {
        socket_addr.sin_port = (uint16_t) (std::stoul(port));
    } catch (const std::exception &e) {
        throw std::runtime_error(e.what());
    }

    socket_addr.sin_family = AF_INET;
    socket_addr.sin_addr.s_addr = inet_addr(address);

    if (bind(socket_fd.get_fd(), (sockaddr *) (&socket_addr), sizeof(sockaddr)) == -1) {
        throw std::runtime_error("Can't bind socket");
    }

    if (listen(socket_fd.get_fd(), 5) == -1) {
        throw std::runtime_error("Can't listen socket");
    }
}

size_t find_new_ind(std::queue<size_t> &queue_free_ind, std::vector<server_data> &data_for_epoll) {
    if (!queue_free_ind.empty()) {
        size_t ret = queue_free_ind.front();
        queue_free_ind.pop();
        return ret;
    }

    data_for_epoll.emplace_back();
    return data_for_epoll.size() - 1;
}

void index_release(size_t ind, std::queue<size_t> &queue_free_ind, std::vector<server_data> &data_for_epoll) {
    data_for_epoll[ind] = {0, false};
    queue_free_ind.push(ind);
}

void close_fd(int fd) {
    if (fd != -1) {
        if (close(fd) == -1) {
            error("Can't close fd");
        }
    }
}

void server::run() {
    std::cout << "Server is running..." << std::endl;
    struct epoll_event events[EVENTS];
    struct epoll_event cur_event{};

    fd_wrapper epoll_fd_wrapper(epoll_create1(0));
    if (epoll_fd_wrapper.get_fd() == -1) {
        error("Can't create epoll");
    }

    std::queue<size_t> queue_free_ind;
    std::vector<server_data> data_for_epoll;

    data_for_epoll.emplace_back(socket_fd.get_fd());
    cur_event.data.u32 = 0;

    cur_event.events = EPOLLIN;
    if (epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_ADD, socket_fd.get_fd(), &cur_event) == -1) {
        throw std::runtime_error("Epoll_ctl failed with socket_fd");
    }

    while (true) {
        int cnt = epoll_wait(epoll_fd_wrapper.get_fd(), events, EVENTS, -1);
        if (cnt == -1) {
            throw std::runtime_error("Epoll_waiting failed");
        }

        for (int i = 0; i < cnt; ++i) {
            if (!data_for_epoll[events[i].data.u32].is_correct) {
                continue;
            } else if (data_for_epoll[events[i].data.u32].fd == socket_fd.get_fd()) {
                int client_fd = accept(socket_fd.get_fd(), nullptr, nullptr);
                if (client_fd == -1) {
                    error("Can't accept client address");
                    continue;
                }

                std::cout << "Client connected" << std::endl;

                fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);
                cur_event = {};
                cur_event.events = EPOLLIN;
                cur_event.data.u32 = find_new_ind(queue_free_ind, data_for_epoll);
                data_for_epoll[cur_event.data.u32] = {client_fd};

                if (epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_ADD, client_fd, &cur_event) == -1) {
                    index_release(cur_event.data.u32, queue_free_ind, data_for_epoll);

                    error("Can't add client to epoll_ctl");

                    close_fd(client_fd);
                }
                continue;
            }

            bool error_occurred = false;
            bool want_set_out = false;
            size_t ind_events = events[i].data.u32;
            size_t &message_length = data_for_epoll[ind_events].message_length;
            size_t &message_length_read = data_for_epoll[ind_events].message_length_read;
            size_t &message_length_sent = data_for_epoll[ind_events].message_length_sent;
            int client_fd = data_for_epoll[ind_events].fd;
            if (events[i].events & EPOLLIN) {
                if (message_length == 0) { // read length
                    std::vector<uint8_t> for_read_len(1);
                    ssize_t read_message_length = recv(client_fd, for_read_len.data(), 1, 0);

                    if (read_message_length > 0) {
                        message_length = for_read_len[0];
                        data_for_epoll[ind_events].message.resize(for_read_len[0], ' ');
                    } else {
                        if (read_message_length == -1) {
                            error("Can't receive message length");
                        }
                        error_occurred = true;
                    }
                } else { // read part message
                    size_t left_read = message_length - message_length_read;

                    if (left_read > 0) {
                        ssize_t cur_read = recv(client_fd,
                                                (void *) (data_for_epoll[ind_events].message.data() +
                                                          message_length_read), left_read,
                                                0);

                        if (cur_read > 0) {
                            message_length_read += cur_read;

                        if (message_length_read == message_length) {
                                events[i].events = EPOLLOUT;
                                epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_MOD, client_fd, &events[i]);
                        }
                        } else {
                            if (cur_read == -1) {
                                error("Can't receive message");
                            }
                            error_occurred = true;
                        }
                    }
                }

                if (!error_occurred) {
                    want_set_out = true;
                }
            }
            if (events[i].events & EPOLLOUT) {
                if (!data_for_epoll[ind_events].have_sent_length && data_for_epoll[ind_events].message_length > 0) {
                    std::vector<uint8_t> for_send_len(1, message_length);

                    ssize_t send_message_length = send(client_fd, for_send_len.data(), 1, MSG_NOSIGNAL);

                    if (send_message_length > 0) {
                        data_for_epoll[ind_events].have_sent_length = true;
                    } else {
                        if (send_message_length == -1) {
                            error("Can't send message length");
                        }
                        error_occurred = true;
                    }
                } else if (message_length_read > 0 && message_length_read > message_length_sent) { // sending data
                    size_t left_send = message_length_read - message_length_sent;
                    ssize_t cur_sent = send(client_fd, data_for_epoll[ind_events].message.c_str() +
                                                        message_length_sent, left_send, MSG_NOSIGNAL);

                    if (cur_sent <= 0) {
                        if (cur_sent == -1) {
                            error("Can't send message part");
                        }
                        error_occurred = true;
                    }
                    message_length_sent += (size_t) cur_sent;

                    if (message_length_sent == message_length_read) {
                        events[i].events = EPOLLIN;
                        epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_MOD, client_fd, &events[i]);
                        want_set_out = false;
                    }

                    if (message_length_sent == message_length) {
                        message_length = 0;
                        message_length_read = 0;
                        data_for_epoll[ind_events].have_sent_length = false;
                        message_length_sent = 0;
                        data_for_epoll[ind_events].message = "";
                    }
                }
            }

            if (error_occurred) {
                std::cout << "Client disconnected" << std::endl;

                if (epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_DEL, client_fd, nullptr) == -1) {
                    error("Can't delete fd");
                }

                index_release(ind_events, queue_free_ind, data_for_epoll);

                close_fd(client_fd);
            } else if (want_set_out && !(events[i].events & EPOLLOUT)) {
                events[i].events |= EPOLLOUT;
                epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_MOD, client_fd, &events[i]);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc == 2 && argv[1] != nullptr && (strcmp(argv[1], "-help") == 0)) {
        std::cerr << HELP << std::endl;
        return 0;
    }
    if (argc > 3) {
        error("Wrong usage", false, true, true);
    }

    try {
        std::string address = "127.0.0.1";
        std::string port = "10005";

        if (argc == 2) {
            address = argv[1];
        }

        if (argc == 3) {
            port = argv[2];
        }

        server server(address.c_str(), port.c_str());
        server.run();
    } catch (const std::invalid_argument &e) {
        error(e.what(), true, true, true);
    } catch (const std::runtime_error &e) {
        error(e.what(), true, false, true);
    }
}
