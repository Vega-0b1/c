#include <arpa/inet.h>  // // inet_pton, htons
#include <netinet/in.h> // // sockaddr_in
#include <stdio.h>      // // fprintf, perror, printf
#include <stdlib.h>     // // atoi, exit
#include <string.h>     // // strlen, memcpy
#include <sys/socket.h> // // socket, connect, send, recv
#include <unistd.h>     // // close, write

#define MAX_LINE 4096
#define RX_BUFFER 4096
#define PORT_DEFAULT 7777

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s [server_ip] [ls_args..]\n", argv[0]);
    return 1;
  }

  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (socket_fd < 0) {
    perror("bad socket");
    return 1;
  }

  struct sockaddr_in srv_addr = {.sin_family = AF_INET,
                                 .sin_addr.s_addr = htonl(INADDR_ANY),
                                 .sin_port = htons(PORT_DEFAULT)};

  if (inet_pton(AF_INET, argv[1], &srv_addr.sin_addr) != 1) {
    fprintf(stderr, "bad ip");
    close(socket_fd);
    return 1;
  }
  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t srv_len = sizeof(srv_addr);

  if (connect(socket_fd, sock_addr, srv_len) < 0) {
    perror("connect");
    close(socket_fd);
    return 1;
  }

  char line[MAX_LINE];
  size_t offset = 0;

  if (argc >= 3) {
    for (int i = 2; i < argc; i++) {
      size_t len = strlen(argv[i]);

      if ((offset + len + 1) >= sizeof(line)) {
        fprintf(stderr, "too long args \n");
        close(socket_fd);
        return 1;
      }
      memcpy(line + offset, argv[i], len);
      offset += len;

      if (i != argc - 1) {
        line[offset++] = ' ';
      }
    }
  }

  if (offset + 1 >= sizeof(line)) {
    fprintf(stderr, "args too long \n");
    close(socket_fd);
    return 1;
  }

  line[offset++] = '\n';

  if (send(socket_fd, line, (int)offset, 0) < 0) {
    perror("send");
    close(socket_fd);
    return 1;
  }

  char buf[RX_BUFFER];
  while (1) {
    int r = recv(socket_fd, buf, sizeof(buf), 0);

    if (r < 0) {
      perror("recv");
      break;
    }
    if (r == 0)
      break;

    if (write(STDOUT_FILENO, buf, r) < 0) {
      perror("write");
      break;
    }
  }

  close(socket_fd);
  return 0;
}
