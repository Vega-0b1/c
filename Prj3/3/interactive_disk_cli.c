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
#include <unistd.h>

#define MAX_LINE 4096
#define BLOCK_SIZE 128
#define PORT_DEFAULT 7780

static ssize_t send_all(int fd, const void *buf, size_t n);
static ssize_t recv_all(int fd, void *buf, size_t n);

int main(int argc, char *argv[]) {

  if (argc != 2) {
    fprintf(stderr, "usage: %s <server_ip>\n", argv[0]);
    return 1;
  }

  const char *server_ip = argv[1];

  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(PORT_DEFAULT),
      .sin_addr.s_addr = 0,
  };

  if (inet_pton(AF_INET, server_ip, &srv_addr.sin_addr) != 1) {
    fprintf(stderr, "bad ip\n");
    close(sock_fd);
    return 1;
  }

  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t addr_length = sizeof(srv_addr);

  if (connect(sock_fd, sock_addr, addr_length) < 0) {
    perror("connect");
    close(sock_fd);
    return 1;
  }

  fprintf(stderr, "Commands:\n"
                  "  I\n"
                  "  R c s\n"
                  "  W c s l  (you will be prompted for l bytes)\n"
                  "Ctrl+D to quit\n");

  char line[MAX_LINE];

  while (1) {

    fprintf(stderr, "> ");
    fflush(stderr);

    if (!fgets(line, sizeof(line), stdin))
      break;

    int c, s, l;

    /* ----- W command ----- */
    if (sscanf(line, " W %d %d %d", &c, &s, &l) == 3) {

      if (l < 0 || l > BLOCK_SIZE) {
        fprintf(stderr, "l must be 0..128\n");
        continue;
      }

      if (send_all(sock_fd, line, strlen(line)) < 0) {
        perror("send");
        break;
      }

      unsigned char buf[BLOCK_SIZE];
      memset(buf, 0, sizeof(buf));

      size_t got = fread(buf, 1, (size_t)l, stdin);
      if (got != (size_t)l) {
        fprintf(stderr, "expected %d bytes, got %zu\n", l, got);
        continue;
      }

      if (send_all(sock_fd, buf, (size_t)l) < 0) {
        perror("send data");
        break;
      }

      if (send_all(sock_fd, "\n", 1) < 0) {
        perror("send nl");
        break;
      }

      char ans[2];
      ssize_t r = recv(sock_fd, ans, sizeof(ans), 0);
      if (r <= 0) {
        perror("recv");
        break;
      }

      write(STDOUT_FILENO, ans, (size_t)r);
      continue;
    }

    /* ----- Other commands (I or R) ----- */
    if (send_all(sock_fd, line, strlen(line)) < 0) {
      perror("send");
      break;
    }

    if (line[0] == 'R') {

      char tag = 0;
      if (recv_all(sock_fd, &tag, 1) <= 0) {
        perror("recv");
        break;
      }

      if (tag != '1') {
        write(STDOUT_FILENO, "0\n", 2);
        continue;
      }

      unsigned char block[BLOCK_SIZE];
      if (recv_all(sock_fd, block, BLOCK_SIZE) <= 0) {
        perror("recv blk");
        break;
      }

      for (int i = 0; i < BLOCK_SIZE; i++)
        printf("%02x%s", block[i], (i % 16 == 15) ? "\n" : " ");

      if (BLOCK_SIZE % 16)
        puts("");

    } else {

      char buf[256];
      ssize_t r = recv(sock_fd, buf, sizeof(buf), 0);

      if (r <= 0) {
        perror("recv");
        break;
      }

      write(STDOUT_FILENO, buf, (size_t)r);
    }
  }

  close(sock_fd);
  return 0;
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
