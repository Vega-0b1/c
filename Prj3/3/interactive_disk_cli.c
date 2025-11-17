
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT_DEFAULT 7780
#define BLOCK 128
#define MAX 4096

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

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: %s <server_ip> [port]\n", argv[0]);
    return 1;
  }
  int port = (argc == 3) ? atoi(argv[2]) : PORT_DEFAULT;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in sa = {0};
  sa.sin_family = AF_INET;
  sa.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, argv[1], &sa.sin_addr) != 1) {
    fprintf(stderr, "bad ip\n");
    return 1;
  }

  if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    perror("connect");
    return 1;
  }

  fprintf(stderr, "Commands:\n  I\n  R c s\n  W c s l  (you will be prompted "
                  "for l bytes)\nCtrl+D to quit\n");

  char line[MAX];
  for (;;) {
    fprintf(stderr, "> ");
    fflush(stderr);
    if (!fgets(line, sizeof(line), stdin))
      break;

    // Is this a W? If yes, parse c s l, then send "W c s l\n" + data + "\n"
    int c, s, l;
    if (sscanf(line, " W %d %d %d", &c, &s, &l) == 3) {
      if (l < 0 || l > BLOCK) {
        fprintf(stderr, "l must be 0..128\n");
        continue;
      }
      // Send the command line as-is (with newline)
      if (send_all(fd, line, strlen(line)) < 0) {
        perror("send");
        break;
      }

      // Read exactly l bytes from stdin (can be binary)
      unsigned char buf[BLOCK] = {0};
      size_t got = fread(buf, 1, (size_t)l, stdin);
      if (got != (size_t)l) {
        fprintf(stderr, "expected %d bytes, got %zu\n", l, got);
        continue;
      }

      if (send_all(fd, buf, (size_t)l) < 0) {
        perror("send data");
        break;
      }
      if (send_all(fd, "\n", 1) < 0) {
        perror("send nl");
        break;
      }

      char ans[2] = {0};
      ssize_t r = recv(fd, ans, sizeof(ans), 0);
      if (r <= 0) {
        perror("recv");
        break;
      }
      write(STDOUT_FILENO, ans, (size_t)r);
      continue;
    }

    // For I/R/other, just send the line and read a small reply or block
    if (send_all(fd, line, strlen(line)) < 0) {
      perror("send");
      break;
    }

    if (line[0] == 'R') {
      char tag;
      if (recv_all(fd, &tag, 1) <= 0) {
        perror("recv");
        break;
      }
      if (tag != '1') {
        write(STDOUT_FILENO, "0\n", 2);
        continue;
      }
      unsigned char blk[BLOCK];
      if (recv_all(fd, blk, BLOCK) <= 0) {
        perror("recv blk");
        break;
      }
      // Show as hex
      for (int i = 0; i < BLOCK; i++) {
        printf("%02x%s", blk[i], (i % 16 == 15) ? "\n" : " ");
      }
      if (BLOCK % 16)
        puts("");
    } else {
      char buf[256];
      ssize_t r = recv(fd, buf, sizeof(buf), 0);
      if (r <= 0) {
        perror("recv");
        break;
      }
      write(STDOUT_FILENO, buf, (size_t)r);
    }
  }

  close(fd);
  return 0;
}
