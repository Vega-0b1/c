
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

#define BLOCK_SIZE 128    // one disk block
#define MAX_LINE 4096     // max input line
#define PORT_DEFAULT 7780 // disk server port

static ssize_t send_all(int fd, const void *buf,
                        size_t n);                    // write whole buffer
static ssize_t recv_all(int fd, void *buf, size_t n); // read exact n bytes

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
      .sin_addr.s_addr = 0,
      .sin_port = htons(PORT_DEFAULT),
  };

  if (inet_pton(AF_INET, server_ip, &srv_addr.sin_addr) != 1) {
    fprintf(stderr, "bad ip\n");
    close(sock_fd);
    return 1;
  }

  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t addr_len = sizeof(srv_addr);

  if (connect(sock_fd, sock_addr, addr_len) < 0) {
    perror("connect");
    close(sock_fd);
    return 1;
  }

  fprintf(stderr, "Commands:\n"
                  "  I\n"
                  "  R c s\n"
                  "  W c s l <enter l bytes>\n");

  char line[MAX_LINE];

  while (1) {

    fprintf(stderr, "> "); // simple prompt
    fflush(stderr);

    if (!fgets(line, sizeof(line), stdin))
      break; // EOF on stdin

    /* handle write command specially */
    int c, s, l;

    if (sscanf(line, " W %d %d %d", &c, &s, &l) == 3) {

      if (l < 0 || l > BLOCK_SIZE) {
        fprintf(stderr, "l must be 0..128\n");
        continue;
      }

      /* send the whole command line, including its newline */
      if (send_all(sock_fd, line, strlen(line)) < 0)
        break;

      unsigned char buf[BLOCK_SIZE] = {0};
      size_t got = fread(buf, 1, (size_t)l, stdin); // read data for W

      if (got != (size_t)l) {
        fprintf(stderr, "needed %d bytes\n", l);
        continue;
      }

      if (send_all(sock_fd, buf, (size_t)l) < 0)
        break;

      if (send_all(sock_fd, "\n", 1) < 0)
        break;

      char ans[2];
      ssize_t r = recv(sock_fd, ans, sizeof(ans), 0);
      if (r > 0)
        write(STDOUT_FILENO, ans, (size_t)r); // print status

      continue;
    }

    /* normal commands (I, R, etc.) */
    if (send_all(sock_fd, line, strlen(line)) < 0)
      break;

    /* read command needs special handling */
    if (line[0] == 'R') {

      char tag;

      if (recv_all(sock_fd, &tag, 1) <= 0)
        break;

      if (tag != '1') {
        write(STDOUT_FILENO, "0\n", 2); // invalid read
        continue;
      }

      unsigned char block[BLOCK_SIZE];

      if (recv_all(sock_fd, block, BLOCK_SIZE) <= 0)
        break;

      // hex dump of the 128-byte block
      for (int i = 0; i < BLOCK_SIZE; i++) {
        printf("%02x%s", block[i], (i % 16 == 15) ? "\n" : " ");
      }
      if (BLOCK_SIZE % 16)
        puts("");

    } else {

      // for I and any other small replies
      char buf[256];
      ssize_t r = recv(sock_fd, buf, sizeof(buf), 0);

      if (r <= 0)
        break;

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
      if (r < 0 && errno == EINTR) // retry on signal
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
      return 0; // connection closed

    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }

    off += (size_t)r;
  }

  return (ssize_t)off;
}
