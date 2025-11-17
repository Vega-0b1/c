
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PORT_DEFAULT 7780
#define BLOCK 128
#define MAX 256

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

static int connect_to(const char *ip, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  struct sockaddr_in sa = {0};
  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
    fprintf(stderr, "bad ip\n");
    close(fd);
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    perror("connect");
    close(fd);
    return -1;
  }
  return fd;
}

int main(int argc, char **argv) {
  if (argc < 4 || argc > 5) {
    fprintf(stderr, "usage: %s <server_ip> <ops> <seed> [port]\n", argv[0]);
    return 1;
  }
  const char *ip = argv[1];
  long ops = strtol(argv[2], NULL, 10);
  unsigned int seed = (unsigned int)strtoul(argv[3], NULL, 10);
  int port = (argc == 5) ? atoi(argv[4]) : PORT_DEFAULT;
  if (ops <= 0) {
    fprintf(stderr, "ops must be >0\n");
    return 1;
  }

  srand(seed);

  int fd = connect_to(ip, port);
  if (fd < 0)
    return 1;

  // Get geometry: send "I\n"
  if (send_all(fd, "I\n", 2) < 0) {
    perror("send I");
    close(fd);
    return 1;
  }
  char geo[64] = {0};
  ssize_t gr = recv(fd, geo, sizeof(geo) - 1, 0);
  if (gr <= 0) {
    perror("recv I");
    close(fd);
    return 1;
  }
  int C = 0, S = 0;
  if (sscanf(geo, "%d %d", &C, &S) != 2 || C <= 0 || S <= 0) {
    fprintf(stderr, "bad geometry reply: %s\n", geo);
    close(fd);
    return 1;
  }
  fprintf(stderr, "geometry: C=%d S=%d  (seed=%u, ops=%ld)\n", C, S, seed, ops);

  unsigned char wbuf[BLOCK], rbuf[BLOCK];
  for (long i = 1; i <= ops; i++) {
    int c = rand() % C;
    int s = rand() % S;

    if ((rand() & 1) == 0) {
      // READ
      char cmd[MAX];
      int n = snprintf(cmd, sizeof(cmd), "R %d %d\n", c, s);
      if (send_all(fd, cmd, (size_t)n) < 0) {
        perror("send R");
        break;
      }

      char tag;
      if (recv_all(fd, &tag, 1) <= 0) {
        perror("recv R tag");
        break;
      }
      if (tag != '1') {
        fprintf(stderr, "R invalid at (%d,%d)\n", c, s);
        break;
      }
      if (recv_all(fd, rbuf, BLOCK) <= 0) {
        perror("recv R data");
        break;
      }

    } else {
      // WRITE 128 bytes of pseudorandom data
      for (int j = 0; j < BLOCK; j++)
        wbuf[j] = (unsigned char)rand();

      char cmd[MAX];
      int n = snprintf(cmd, sizeof(cmd), "W %d %d %d\n", c, s, BLOCK);
      if (send_all(fd, cmd, (size_t)n) < 0) {
        perror("send W");
        break;
      }
      if (send_all(fd, wbuf, BLOCK) < 0) {
        perror("send W data");
        break;
      }
      if (send_all(fd, "\n", 1) < 0) {
        perror("send W nl");
        break;
      }

      char ans[2];
      ssize_t r = recv(fd, ans, sizeof(ans), 0);
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

  close(fd);
  return 0;
}
