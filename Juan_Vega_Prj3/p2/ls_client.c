
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LINE 4096
#define RX_BUFFER 4096
#define PORT_DEFAULT 7777

int main(int argc, char *argv[]) {
  // need at least server ip
  if (argc < 2) {
    fprintf(stderr, "usage: %s [server_ip] [ls_args..]\n", argv[0]);
    return 1;
  }

  // create TCP socket
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (socket_fd < 0) {
    perror("bad socket");
    return 1;
  }

  // set up server address
  struct sockaddr_in srv_addr = {.sin_family = AF_INET,
                                 .sin_addr.s_addr = htonl(INADDR_ANY),
                                 .sin_port = htons(PORT_DEFAULT)};

  // convert string IP to binary
  if (inet_pton(AF_INET, argv[1], &srv_addr.sin_addr) != 1) {
    fprintf(stderr, "bad ip");
    close(socket_fd);
    return 1;
  }
  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t srv_len = sizeof(srv_addr);

  // connect to ls server
  if (connect(socket_fd, sock_addr, srv_len) < 0) {
    perror("connect");
    close(socket_fd);
    return 1;
  }

  char line[MAX_LINE];
  size_t offset = 0;

  // build command line from remaining args: "arg2 arg3 ..."
  if (argc >= 3) {
    for (int i = 2; i < argc; i++) {
      size_t len = strlen(argv[i]);

      // make sure we don't overflow line buffer
      if ((offset + len + 1) >= sizeof(line)) {
        fprintf(stderr, "too long args \n");
        close(socket_fd);
        return 1;
      }
      memcpy(line + offset, argv[i], len);
      offset += len;

      // add space between args
      if (i != argc - 1) {
        line[offset++] = ' ';
      }
    }
  }

  // need room for final newline
  if (offset + 1 >= sizeof(line)) {
    fprintf(stderr, "args too long \n");
    close(socket_fd);
    return 1;
  }

  // end command with newline for server's read_line()
  line[offset++] = '\n';

  // send whole line to server
  if (send(socket_fd, line, (int)offset, 0) < 0) {
    perror("send");
    close(socket_fd);
    return 1;
  }

  char buf[RX_BUFFER];
  // read all output from server and write to stdout
  while (1) {
    int r = recv(socket_fd, buf, sizeof(buf), 0);

    if (r < 0) {
      perror("recv");
      break;
    }
    if (r == 0)
      break; // server closed connection

    if (write(STDOUT_FILENO, buf, r) < 0) {
      perror("write");
      break;
    }
  }

  close(socket_fd);
  return 0;
}
