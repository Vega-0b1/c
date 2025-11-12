#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_SIZE 4096
#define PORT_DEFAULT 7777
int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "usage %s \n", argv[0]);
    return 1;
  }

  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in srv_addr = {.sin_family = AF_INET,
                                 .sin_port = htons(PORT_DEFAULT)};

  if (inet_pton(AF_INET, argv[1], &srv_addr.sin_addr) != 1) {
    fprintf(stderr, "bad ip\n");
    close(socket_fd);
    return 1;
  }

  if (connect(socket_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    perror("connect");
    return 1;
  }

  char out[MAX_SIZE];
  size_t memory_length = strlen(argv[2]);

  if (memory_length + 1 > sizeof(out)) {
    fprintf(stderr, "msg too long\n");
    return 1;
  }

  memcpy(out, argv[2], memory_length);
  out[memory_length] = '\n';

  if (send(socket_fd, out, (int)memory_length + 1, 0) < 0) {
    perror("send");
    return 1;
  }

  char in[MAX_SIZE + 1];
  int n = 0;
  for (; n < MAX_SIZE; n++) {
    char c;
    int r = recv(socket_fd, &c, 1, 0);
    if (r <= 0)
      break;
    in[n] = c;
    if (c == '\n')
      break;
  }

  in[n] = '\0';
  fputs(in, stdout);
  close(socket_fd);
  return 0;
}
