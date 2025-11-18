
#include <arpa/inet.h>
#include <bits/types/idtype_t.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BLOCK_SIZE 128
#define MAX_LINE 256
#define PORT_DEFAULT 7780

static ssize_t send_all(int fd, const void *buf, size_t n);
static ssize_t recv_all(int fd, void *buf, size_t n);
static int connect_to_server(const char *ip, int port);

int main(int argc, char *argv[]) {

  if (argc < 4 || argc > 5) {
    fprintf(stderr, "usage: %s <server_ip> <ops> <seed> [port]\n", argv[0]);
    return 1;
  }

  const char *server_ip = argv[1];
  long ops = strtol(argv[2], NULL, 10);
  unsigned int seed = (unsigned int)strtoul(argv[3], NULL, 10);
  int port = (argc == 5) ? atoi(argv[4]) : PORT_DEFAULT;

  if (ops <= 0) {
    fprintf(stderr, "ops must be >0\n");
    return 1;
  }

  srand(seed);

  int sock_fd = connect_to_server(server_ip, port);
  if (sock_fd < 0)
    return 1;

  /* request disk geometry */
  if (send_all(sock_fd, "I\n", 2) < 0) {
    perror("send I");
    close(sock_fd);
    return 1;
  }

  char geo_buf[64] = {0};
  ssize_t geo_read = recv(sock_fd, geo_buf, sizeof(geo_buf) - 1, 0);

  if (geo_read <= 0) {
    perror("recv I");
    close(sock_fd);
    return 1;
  }

  int cylinders = 0;
  int sectors = 0;

  if (sscanf(geo_buf, "%d %d", &cylinders, &sectors) != 2 || cylinders <= 0 ||
      sectors <= 0) {
    fprintf(stderr, "bad geometry reply: %s\n", geo_buf);
    close(sock_fd);
    return 1;
  }

  fprintf(stderr, "geometry: C=%d S=%d  (seed=%u ops=%ld)\n", cylinders,
          sectors, seed, ops);

  unsigned char write_buf[BLOCK_SIZE];
  unsigned char read_buf[BLOCK_SIZE];

  for (long i = 1; i <= ops; i++) {

    int c = rand() % cylinders;
    int s = rand() % sectors;

    /* RANDOMLY READ OR WRITE */
    if ((rand() & 1) == 0) {

      char cmd[MAX_LINE];
      int n = snprintf(cmd, sizeof(cmd), "R %d %d\n", c, s);

      if (send_all(sock_fd, cmd, (size_t)n) < 0) {
        perror("send R");
        break;
      }

      char tag;
      if (recv_all(sock_fd, &tag, 1) <= 0) {
        perror("recv R tag");
        break;
      }

      if (tag != '1') {
        fprintf(stderr, "R invalid at (%d,%d)\n", c, s);
        break;
      }

      if (recv_all(sock_fd, read_buf, BLOCK_SIZE) <= 0) {
        perror("recv R data");
        break;
      }

    } else {

      for (int j = 0; j < BLOCK_SIZE; j++)
        write_buf[j] = (unsigned char)rand();

      char cmd[MAX_LINE];
      int n = snprintf(cmd, sizeof(cmd), "W %d %d %d\n", c, s, BLOCK_SIZE);

      if (send_all(sock_fd, cmd, (size_t)n) < 0) {
        perror("send W");
        break;
      }

      if (send_all(sock_fd, write_buf, BLOCK_SIZE) < 0) {
        perror("send W data");
        break;
      }

      if (send_all(sock_fd, "\n", 1) < 0) {
        perror("send W nl");
        break;
      }

      char ans[2];
      ssize_t r = recv(sock_fd, ans, sizeof(ans), 0);

      if (r <= 0) {
        perror("recv W");
        break;
      }

      if (!(r >= 1 && ans[0] == '1')) {
        fprintf(stderr, "W failed at (%d,%d)\n", c, s);
        break;
      }
    }

    if ((i % 1000) == 0)
      fprintf(stderr, "progress: %ld/%ld\n", i, ops);
  }

  close(sock_fd);
  return 0;
}

static int connect_to_server(const char *ip, int port) {

  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (sock_fd < 0) {
    perror("socket");
    return -1;
  }

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET, .sin_addr.s_addr = 0, .sin_port = htons(port)};

  if (inet_pton(AF_INET, ip, &srv_addr.sin_addr) != 1) {
    fprintf(stderr, "bad ip\n");
    close(sock_fd);
    return -1;
  }

  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t addr_len = sizeof(srv_addr);

  if (connect(sock_fd, sock_addr, addr_len) < 0) {
    perror("connect");
    close(sock_fd);
    return -1;
  }

  return sock_fd;
}

static ssize_t send_all(int fd, const void *buf, size_t n) {

  size_t off = 0;
  const char *p = buf;

  while (off < n) {

    ssize_t r = send(fd, p + off, n - off, 0);

    if (r <= 0) {
      if (r < 0 && errno == EINTR)
        continue;
      return -1;
    }

    off += (size_t)r;
  }

  return (ssize_t)off;
}

static ssize_t recv_all(int fd, void *buf, size_t n) {

  size_t off = 0;
  char *p = buf;

  while (off < n) {

    ssize_t r = recv(fd, p + off, n - off, 0);

    if (r == 0)
      return 0;

    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }

    off += (size_t)r;
  }

  return (ssize_t)off;
}
