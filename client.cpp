#include "client.h"

client::client(const char* addr, const char* port) : socket_client_fd(socket(AF_INET, SOCK_STREAM, 0)) {
    if (socket_client_fd.get_fd() == -1) {
        throw std::runtime_error("Can't create socket");
    }
    
    memset(&socket_addr, 0, sizeof(sockaddr_in));
    
    try {
        socket_addr.sin_port = std::stoul(port);
    } catch(const std::exception &e) {
        throw std::runtime_error(e.what());
    }

    socket_addr.sin_family = AF_INET;
    socket_addr.sin_addr.s_addr = inet_addr(addr);

    if (connect(socket_client_fd.get_fd(), (sockaddr*)(&socket_addr), sizeof(sockaddr_in)) == -1) {
        throw std::runtime_error("Can't connect to server");
    }
}

void client::run() {
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event cur_event; 

    fd_wrapper epoll_fd_wrapper(epoll_create1(0));
    if (epoll_fd_wrapper.get_fd() == -1) {
        throw std::runtime_error("Can't create epoll");
    }

    cur_event.events = EPOLLIN;
    cur_event.data.fd = socket_client_fd.get_fd();

    if (epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_ADD, socket_client_fd.get_fd(), &cur_event) == -1) {
        throw std::runtime_error("Epoll_ctl failed with socket_client_fd");
    }

    cur_event.data.fd = 0;
    cur_event.events = EPOLLIN;

    if (epoll_ctl(epoll_fd_wrapper.get_fd(), EPOLL_CTL_ADD, cur_event.data.fd, &cur_event) == -1) {
        throw std::runtime_error("Epoll_ctl failed with stdin");
    }
    
    bool alive = true;
    std::cout << "Request:  ";
    std::cout.flush();
    
    while (alive) {
        int cnt = epoll_wait(epoll_fd_wrapper.get_fd(), events, MAX_EVENTS, -1);
        if (cnt == -1) {
            throw std::runtime_error("Epoll_wait failed");
        }
        
        for (int i = 0; i < cnt; ++i) {
            if (events[i].data.fd == socket_client_fd.get_fd()) {
                std::vector<char> response(BUF_SIZE);
                ssize_t read_ = recv(socket_client_fd.get_fd(), response.data(), BUF_SIZE, 0);
                if (read_ == -1) {
                    throw std::runtime_error("Can't read fd");
                } else if (read_ == 0) {
                    alive = false;
                    continue;
                }
                response.resize(read_);

                std::cout << "Response: " << response.data() << std::endl;
                std::cout << "Request:  ";
                std::cout.flush();
            } else if (events[i].data.fd == 0) {
                std::string message;
                std::cin >> message;

                if (message == "-exit") {
                    break;
                } else if (message == "\n" || message == "") {
                    continue;
                }

                ssize_t message_length = (ssize_t) (message.length());
                if (send(socket_client_fd.get_fd(), message.c_str(), message.length(), 0) != message_length) {
                    throw std::runtime_error("Can't send request");
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc == 2 && argv[1] != nullptr && argv[1] == "-help") {
        std::cerr << HELP << std::endl;
        return 0;
    } else if (argc == 2 && argv[1] != nullptr && argv[1] == "-exit") {
        return 0;
    }

    if (argc > 3) {
        error("Wrong usage", true, true);
    }

    try {
        std::string adress = "127.0.0.1";
        std::string port = "10005";

        if (argc == 2) {
            adress = argv[1];        
        }

        if (argc == 3) {
            port = argv[2];
        }

        client client(adress.c_str(), port.c_str());
        std::cout << "Echo-client: type message" << std::endl;
        
        client.run();
    } catch (std::runtime_error& e) {
        error(e.what(), false, true);
    }

    return 0;
}


