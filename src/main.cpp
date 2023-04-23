#include <iostream>
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

static void die(const std::string &message) {
  std::cerr << message << std::endl;
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: port" << std::endl;
    exit(EXIT_SUCCESS);
  }
  const int openMax = sysconf(_SC_OPEN_MAX);
  const int port = atoi(argv[1]);
  if (port <= 0 || port > 65535)
    die("Invalid port number");

  const int serverFd = socket(AF_INET6, SOCK_STREAM, 0);
  if (serverFd < 0)
    die("Failed to create socket");

  {
    int on = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, (char *)&on,
                   sizeof(on)) < 0)
      die("Failed to set socket option");
  }
  if (fcntl(serverFd, F_SETFL, fcntl(serverFd, F_GETFL, 0) | O_NONBLOCK) == -1)
    die("Failed to make fd nonblock");
  {
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    memcpy(&addr.sin6_addr, &in6addr_any, sizeof(in6addr_any));
    addr.sin6_port = htons(port);
    if (bind(serverFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
      die("Failed to bind");
  }
  if (listen(serverFd, 32) < 0)
    die("Failed to listen");

  struct pollfd fds[openMax];
  memset(fds, 0, sizeof(fds));

  fds[0].fd = serverFd;
  fds[0].events = POLLIN;
  nfds_t nfds = 1;
  const int timeout = (3 * 60 * 1000);

  bool end_server = false, need_pack = false;
  char buffer[80];
  do {
    const ssize_t pollSize = poll(fds, nfds, timeout);

    if (pollSize < 0)
      die("Error on poll");

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
              die("Error on accept");
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
