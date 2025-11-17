
// disk_server.c
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h> // nanosleep
#include <unistd.h>

#define BLOCK 128
#define PORT_DEFAULT 7780
#define MAX_LINE 4096

static ssize_t recv_all(int fd, void *buf, size_t need) {
  size_t off = 0;
  while (off < need) {
    ssize_t r = recv(fd, (char *)buf + off, need - off, 0);
    if (r == 0)
      return 0; // peer closed
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    off += (size_t)r;
  }
  return (ssize_t)off;
}

static ssize_t send_all(int fd, const void *buf, size_t need) {
  size_t off = 0;
  while (off < need) {
    ssize_t r = send(fd, (const char *)buf + off, need - off, 0);
    if (r <= 0) {
      if (r < 0 && errno == EINTR)
        continue;
      return -1;
    }
    off += (size_t)r;
  }
  return (ssize_t)off;
}

static ssize_t recv_line(int fd, char *buf, size_t cap) {
  // reads up to and including '\n' (or fills buffer); returns bytes read
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

static off_t blk_offset(int C, int S, int c, int s) {
  return ((off_t)c * S + s) * BLOCK;
}

static void sleep_tracks(int tracks, int delay_us) {
  if (tracks <= 0 || delay_us <= 0)
    return;
  long long total_us = (long long)tracks * (long long)delay_us;
  struct timespec ts;
  ts.tv_sec = total_us / 1000000LL;
  ts.tv_nsec = (long)((total_us % 1000000LL) * 1000LL);
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry */
  }
}

static int serve_client(int cfd, int C, int S, int delay_us, int backing_fd) {
  int cur_cyl = 0;
  char line[MAX_LINE];

  for (;;) {
    ssize_t n = recv_line(cfd, line, sizeof(line));
    if (n <= 0)
      break;

    // NUL-terminate for parsing
    if (n == sizeof(line))
      n--;
    line[n] = '\0';
    if (n > 0 && line[n - 1] == '\n')
      line[n - 1] = '\0';

    // Protocol:
    //  I                -> reply: "<C> <S>\n"
    //  R c s            -> reply: '1' + 128 bytes, or "0\n" if invalid
    //  W c s l\n <data> '\n'
    //     (we read the numbers from the command line, then read exactly l
    //     bytes,
    //      then one trailing newline; reply "1\n" on success, else "0\n")
    char cmd;
    int c, s, l;
    if (sscanf(line, " %c", &cmd) != 1) {
      send_all(cfd, "0\n", 2);
      continue;
    }

    if (cmd == 'I') {
      char out[64];
      int m = snprintf(out, sizeof(out), "%d %d\n", C, S);
      if (send_all(cfd, out, (size_t)m) < 0)
        break;

    } else if (cmd == 'R') {
      if (sscanf(line, " R %d %d", &c, &s) != 2 || c < 0 || s < 0 || c >= C ||
          s >= S) {
        send_all(cfd, "0\n", 2);
        continue;
      }
      int tracks = (cur_cyl > c) ? (cur_cyl - c) : (c - cur_cyl);
      sleep_tracks(tracks, delay_us);
      cur_cyl = c;

      unsigned char block[BLOCK];
      if (lseek(backing_fd, blk_offset(C, S, c, s), SEEK_SET) < 0) {
        send_all(cfd, "0\n", 2);
        continue;
      }
      ssize_t rd = read(backing_fd, block, BLOCK);
      if (rd < 0) {
        send_all(cfd, "0\n", 2);
        continue;
      }
      if (rd < BLOCK)
        memset(block + rd, 0, (size_t)(BLOCK - rd));

      if (send_all(cfd, "1", 1) < 0)
        break;
      if (send_all(cfd, block, BLOCK) < 0)
        break;

    } else if (cmd == 'W') {
      if (sscanf(line, " W %d %d %d", &c, &s, &l) != 3 || c < 0 || s < 0 ||
          c >= C || s >= S || l < 0 || l > BLOCK) {
        send_all(cfd, "0\n", 2);
        continue;
      }

      unsigned char block[BLOCK];
      memset(block, 0, sizeof(block));
      if (recv_all(cfd, block, (size_t)l) <= 0) {
        send_all(cfd, "0\n", 2);
        continue;
      }

      // Consume the trailing newline after data
      char nl;
      if (recv_all(cfd, &nl, 1) <= 0 || nl != '\n') {
        send_all(cfd, "0\n", 2);
        continue;
      }

      int tracks = (cur_cyl > c) ? (cur_cyl - c) : (c - cur_cyl);
      sleep_tracks(tracks, delay_us);
      cur_cyl = c;

      if (lseek(backing_fd, blk_offset(C, S, c, s), SEEK_SET) < 0) {
        send_all(cfd, "0\n", 2);
        continue;
      }
      if (write(backing_fd, block, BLOCK) != BLOCK) {
        send_all(cfd, "0\n", 2);
        continue;
      }
      (void)fsync(backing_fd); // optional durability

      if (send_all(cfd, "1\n", 2) < 0)
        break;

    } else {
      send_all(cfd, "0\n", 2);
    }
  }
  close(cfd);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 5 || argc > 6) {
    fprintf(stderr,
            "usage: %s <cylinders> <sectors> <track_delay_us> <backing_file> "
            "[port]\n",
            argv[0]);
    return 1;
  }
  int C = atoi(argv[1]), S = atoi(argv[2]), delay_us = atoi(argv[3]);
  if (C <= 0 || S <= 0) {
    fprintf(stderr, "bad geometry\n");
    return 1;
  }
  int port = (argc == 6) ? atoi(argv[5]) : PORT_DEFAULT;

  int fd = open(argv[4], O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    perror("open backing");
    return 1;
  }
  off_t need = (off_t)C * S * BLOCK;
  if (ftruncate(fd, need) < 0) {
    perror("ftruncate");
    close(fd);
    return 1;
  }

  int sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd < 0) {
    perror("socket");
    return 1;
  }
  int yes = 1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);

  if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }
  if (listen(sfd, 16) < 0) {
    perror("listen");
    return 1;
  }

  fprintf(stderr, "disk_server: C=%d S=%d delay=%dus file=%s port=%d\n", C, S,
          delay_us, argv[4], port);

  for (;;) {
    int cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) {
      if (errno == EINTR)
        continue;
      perror("accept");
      continue;
    }
    serve_client(cfd, C, S, delay_us, fd);
  }
}
