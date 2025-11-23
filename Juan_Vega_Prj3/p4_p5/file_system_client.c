
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LINE 4096
#define FS_PORT_DEFAULT 7790

// small helpers for reliable I/O
static ssize_t send_all(int fd, const void *buf, size_t n);
static ssize_t recv_all(int fd, void *buf, size_t n);
static ssize_t recv_line(int fd, char *buf, size_t cap);

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <fs_server_ip>\n", argv[0]);
    return 1;
  }

  const char *ip = argv[1];
  int port = FS_PORT_DEFAULT;

  // connect to filesystem server
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);

  if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
    fprintf(stderr, "bad ip\n");
    close(sock);
    return 1;
  }

  if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    perror("connect");
    close(sock);
    return 1;
  }

  // simple help text for interactive use
  fprintf(stderr, "Filesystem commands:\n"
                  "  F\n"
                  "  C name\n"
                  "  D name\n"
                  "  L 0|1\n"
                  "  R name\n"
                  "  W name len\n"
                  "  mkdir name\n"
                  "  cd name|..|/\n"
                  "  pwd\n"
                  "  rmdir name\n");

  char line[MAX_LINE];

  // main REPL loop: read command, decide how to talk to server
  while (1) {
    fprintf(stderr, "fs> ");
    fflush(stderr);

    if (!fgets(line, sizeof(line), stdin))
      break; // EOF on stdin

    if (line[0] == '\n' || line[0] == '\0')
      continue; // skip blanks

    char cmd[16] = {0};
    if (sscanf(line, " %15s", cmd) != 1)
      continue;

    // W name len: send header, then raw bytes, then newline
    if (strcmp(cmd, "W") == 0) {
      char name[128];
      int len;

      if (sscanf(line, " W %127s %d", name, &len) != 2 || len < 0) {
        fprintf(stderr, "usage: W <name> <len>\n");
        continue;
      }

      if (send_all(sock, line, strlen(line)) < 0) {
        perror("send W header");
        break;
      }

      unsigned char *buf = NULL;
      if (len > 0) {
        buf = malloc((size_t)len);
        if (!buf) {
          fprintf(stderr, "oom\n");
          continue;
        }

        fprintf(stderr, "enter %d bytes:\n", len);
        size_t got = fread(buf, 1, (size_t)len, stdin);
        if (got != (size_t)len) {
          fprintf(stderr, "needed %d bytes\n", len);
          free(buf);
          continue;
        }

        if (send_all(sock, buf, (size_t)len) < 0) {
          perror("send W data");
          free(buf);
          break;
        }
        free(buf);
      }

      // final newline after data
      if (send_all(sock, "\n", 1) < 0) {
        perror("send W nl");
        break;
      }

      // server sends one-line status
      char ans[MAX_LINE];
      ssize_t n = recv_line(sock, ans, sizeof(ans));
      if (n <= 0) {
        perror("recv W reply");
        break;
      }
      fwrite(ans, 1, (size_t)n, stdout);
      continue;
    }

    // R name: header "rc len" then optional len bytes + '\n'
    if (strcmp(cmd, "R") == 0) {
      if (send_all(sock, line, strlen(line)) < 0) {
        perror("send R");
        break;
      }

      char hdr[64];
      ssize_t h = recv_line(sock, hdr, sizeof(hdr));
      if (h <= 0) {
        perror("recv R hdr");
        break;
      }
      hdr[h] = '\0';

      int rc = 0, len = 0;
      if (sscanf(hdr, "%d %d", &rc, &len) != 2) {
        fprintf(stderr, "bad R reply: %s\n", hdr);
        continue;
      }

      fputs(hdr, stdout);

      if (rc != 0 || len <= 0)
        continue;

      unsigned char *buf = malloc((size_t)len);
      if (!buf) {
        fprintf(stderr, "oom\n");
        continue;
      }

      if (recv_all(sock, buf, (size_t)len) <= 0) {
        perror("recv R data");
        free(buf);
        break;
      }

      char nl;
      if (recv_all(sock, &nl, 1) <= 0) {
        perror("recv R nl");
        free(buf);
        break;
      }

      fwrite(buf, 1, (size_t)len, stdout);
      putchar('\n');
      free(buf);
      continue;
    }

    // L flag: header "rc count" then exactly count lines of entries
    if (strcmp(cmd, "L") == 0) {
      int verbose = 0;
      if (sscanf(line, " L %d", &verbose) != 1) {
        fprintf(stderr, "usage: L 0|1\n");
        continue;
      }

      if (send_all(sock, line, strlen(line)) < 0) {
        perror("send L");
        break;
      }

      char hdr[64];
      ssize_t h = recv_line(sock, hdr, sizeof(hdr));
      if (h <= 0) {
        perror("recv L hdr");
        break;
      }
      hdr[h] = '\0';

      int rc = 0, count = 0;
      if (sscanf(hdr, "%d %d", &rc, &count) != 2) {
        fprintf(stderr, "bad L reply: %s\n", hdr);
        continue;
      }

      fputs(hdr, stdout);

      if (rc != 0 || count <= 0)
        continue;

      // read exactly "count" listing lines so nothing is left in socket
      for (int i = 0; i < count; i++) {
        char ent[MAX_LINE];
        ssize_t e = recv_line(sock, ent, sizeof(ent));
        if (e <= 0) {
          perror("recv L entry");
          break;
        }
        fwrite(ent, 1, (size_t)e, stdout);
      }
      continue;
    }

    // other commands (F, C, D, mkdir, cd, pwd, rmdir) -> one-line reply
    if (send_all(sock, line, strlen(line)) < 0) {
      perror("send cmd");
      break;
    }

    char ans[MAX_LINE];
    ssize_t n = recv_line(sock, ans, sizeof(ans));
    if (n <= 0) {
      perror("recv reply");
      break;
    }
    fwrite(ans, 1, (size_t)n, stdout);
  }

  close(sock);
  return 0;
}

// send until the whole buffer is written or an error
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

// keep reading until exactly n bytes or EOF/error
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

// read a single line (including '\n') from the socket
static ssize_t recv_line(int fd, char *buf, size_t cap) {
  size_t n = 0;

  while (n < cap) {
    char c;
    ssize_t r = recv(fd, &c, 1, 0);
    if (r == 0)
      break;
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    buf[n++] = c;
    if (c == '\n')
      break;
  }
  return (ssize_t)n;
}
