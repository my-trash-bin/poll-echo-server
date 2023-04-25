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

namespace ft {

class exception : public std::exception {
private:
  std::string message;

public:
  exception(const std::string &message) : message(message) {}
  const char *what() const noexcept override { return this->message.c_str(); }
};

class Server {
private:
  int fd;

public:
  Server(int port) noexcept(false) {
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
  ~Server() {
    if (this->fd >= 0)
      close(this->fd);
  }
  Server(const ft::Server &copy) = delete;
  Server(Server &&move) noexcept : fd(move.fd) { move.fd = -1; }
  Server &operator=(const ft::Server &copy) = delete;
  Server &operator=(Server &&other) noexcept {
    close(this->fd);
    this->fd = other.fd;
    other.fd = -1;
    return *this;
  }

  int getFd() const { return fd; } // TODO: remove

  void listen(int queueSize) {
    if (::listen(this->fd, queueSize) < 0)
      throw ft::exception("listen()");
  }
};

} // namespace ft

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: port" << std::endl;
    exit(EXIT_SUCCESS);
  }
  const int openMax = sysconf(_SC_OPEN_MAX);
  ft::Server server(atoi(argv[1]));
  server.listen(32);

  struct pollfd *fds = new struct pollfd[openMax];
  memset(fds, 0, sizeof(fds[0]) * openMax);

  const int serverFd = server.getFd();
  fds[0].fd = serverFd;
  fds[0].events = POLLIN;
  nfds_t nfds = 1;
  const int timeout = (3 * 60 * 1000);

  bool end_server = false, need_pack = false;
  char buffer[80];
  do {
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

      if (fds[i].fd == serverFd) {
        int newClientFd;
        while (1) {
          newClientFd = accept(serverFd, NULL, NULL);
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
          const ssize_t bytes_read = recv(fds[i].fd, buffer, sizeof(buffer), 0);
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
  } while (!end_server);

  for (nfds_t i = 0; i < nfds; i++) {
    if (fds[i].fd >= 0)
      close(fds[i].fd);
  }
  return 0;
}
