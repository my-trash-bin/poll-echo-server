#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

#include <cerrno>
#include <cstring>

#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

namespace ft {

class exception : public std::exception {
private:
  std::string message;

public:
  exception(const std::string &message) : message(message) {}
  const char *what() const noexcept override { return this->message.c_str(); }
};

class ServerSocket {
private:
  int fd;
  bool listening;

public:
  ServerSocket(int port) noexcept(false) : listening(false) {
    if (port <= 0 || port > 65535)
      throw ft::exception("Invalid port");
    this->fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (this->fd < 0) {
      throw ft::exception("socket()");
    }
    try {
      {
        int on = 1;
        if (setsockopt(this->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
          throw ft::exception("setsockopt()");
      }

      if (fcntl(this->fd, F_SETFL, fcntl(this->fd, F_GETFL, 0) | O_NONBLOCK) ==
          -1)
        throw ft::exception("fcntl()");

      {
        struct sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
        memcpy(&addr.sin6_addr, &in6addr_any, sizeof(in6addr_any));
        addr.sin6_port = htons(port);
        if (bind(this->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
          throw ft::exception("bind()");
      }
    } catch (const std::exception &e) {
      close(this->fd);
      throw e;
    }
  }
  ~ServerSocket() {
    if (this->fd >= 0)
      close(this->fd);
  }
  ServerSocket(const ft::ServerSocket &copy) = delete;
  ServerSocket(ServerSocket &&move) noexcept : fd(move.fd) { move.fd = -1; }
  ServerSocket &operator=(const ft::ServerSocket &copy) = delete;
  ServerSocket &operator=(ServerSocket &&other) noexcept {
    close(this->fd);
    this->fd = other.fd;
    other.fd = -1;
    return *this;
  }

  void listen(int queueSize) noexcept(false) {
    if (this->listening)
      throw ft::exception("Already listening");
    if (::listen(this->fd, queueSize) < 0)
      throw ft::exception("listen()");
    this->listening = true;
  }

  int getFd() const noexcept { return this->fd; }
};

class Server {
private:
  ServerSocket socket;
  bool started;

public:
  Server(int port) noexcept(false) : socket(port), started(false) {}
  ~Server() {}
  Server(const ft::Server &copy) = delete;
  Server(Server &&move) noexcept = delete;
  Server &operator=(const ft::Server &copy) = delete;
  Server &operator=(Server &&other) noexcept = delete;

  void start(int queueSize) {
    if (started)
      throw ft::exception("Already started");
    this->socket.listen(queueSize);
    this->started = true;

    const int openMax = sysconf(_SC_OPEN_MAX);
    const int timeout = (3 * 60 * 1000);
    struct pollfd *fds = new struct pollfd[openMax];
    memset(fds, 0, sizeof(fds[0]) * openMax);
    fds[0].fd = this->socket.getFd();
    fds[0].events = POLLIN;
    nfds_t nfds = 1;
    char buffer[BUFFER_SIZE];
    bool running = true, need_pack = false;
    while (running) {
      const ssize_t pollSize = poll(fds, nfds, timeout);

      if (pollSize < 0)
        throw ft::exception("Error on poll");

      if (pollSize == 0) {
        std::cout << "Timeout occurred. bye!" << std::endl;
        break;
      }

      nfds_t current_size = nfds;
      for (nfds_t i = 0; i < current_size; i++) {
        if (fds[i].revents == 0)
          continue;

        if (fds[i].fd == socket.getFd()) {
          int newClientFd;
          while (1) {
            newClientFd = accept(socket.getFd(), NULL, NULL);
            if (newClientFd < 0) {
              if (errno != EWOULDBLOCK)
                throw ft::exception("Error on accept");
              break;
            }
            fds[nfds].fd = newClientFd;
            fds[nfds].events = POLLIN;
            nfds++;
          }
        } else {
          bool close_conn = false;
          while (true) {
            const ssize_t bytes_read =
                recv(fds[i].fd, buffer, sizeof(buffer), 0);
            if (bytes_read < 0) {
              if (errno != EWOULDBLOCK) {
                perror("  recv() failed");
                close_conn = true;
              }
              break;
            }
            if (bytes_read == 0) {
              close_conn = true;
              break;
            }

            if (send(fds[i].fd, buffer, bytes_read, 0) < 0) {
              close_conn = true;
              break;
            }
          }

          if (close_conn) {
            close(fds[i].fd);
            fds[i].fd = -1;
            need_pack = true;
          }
        }
      }

      if (need_pack) {
        need_pack = false;
        for (nfds_t i = 0; i < nfds; i++) {
          if (fds[i].fd == -1) {
            for (nfds_t j = i; j < nfds; j++) {
              fds[j] = fds[j + 1];
            }
            i--;
            nfds--;
          }
        }
      }
    }
    for (nfds_t i = 0; i < nfds; i++) {
      if (fds[i].fd >= 0)
        close(fds[i].fd);
    }
  }
};

} // namespace ft

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: port" << std::endl;
    return EXIT_SUCCESS;
  }
  try {
    ft::Server server(atoi(argv[1]));
    server.start(32);
  } catch (const ft::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
