
#include <arpa/inet.h>
#include <bits/types/idtype_t.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BLOCK_SIZE 128    // bytes per block
#define MAX_LINE 4096     // max size for incoming command
#define PORT_DEFAULT 7780 // listening port

static ssize_t recv_all(int fd, void *buf,
                        size_t bytes_requested); // read exact count
static ssize_t send_all(int fd, const void *buf,
                        size_t bytes_to_send); // send full buffer
static ssize_t recv_line(int fd, char *buf,
                         size_t buffer_capacity); // read up to '\n'

static off_t blk_offset(int cylinders, int sectors, int cylinder_request,
                        int sector_request);
static void sleep_tracks(int tracks, int delay_us); // simulate seek time
static void serve_client(int client_fd, int cylinders, int sectors,
                         int delay_us, int backing_fd); // handle one connection

int main(int argc, char *argv[]) {

  if (argc < 4 || argc > 5) {
    fprintf(
        stderr,
        "usage: %s <cylinders> <sectors> <track_delay_us> <backing_file> \n",
        argv[0]);
    return 1;
  }

  int cylinders = atoi(argv[1]);
  int sectors = atoi(argv[2]);
  int delay_us = atoi(argv[3]);

  if (cylinders <= 0 || sectors <= 0) {
    fprintf(stderr, "bad geometry\n");
    return 1;
  }

  // disk data lives in this file
  int backing_fd = open(argv[4], O_RDWR | O_CREAT, 0644);
  if (backing_fd < 0) {
    perror("couldn't open backing_file");
    return 1;
  }

  // make sure file big enough for all blocks
  off_t total_size = (off_t)cylinders * sectors * BLOCK_SIZE;
  if (ftruncate(backing_fd, total_size) < 0) {
    perror("ftruncate fail");
    close(backing_fd);
    return 1;
  }

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("bad socket");
    return 1;
  }

  int yes = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
      .sin_port = htons(PORT_DEFAULT),
  };

  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;

  if (bind(listen_fd, sock_addr, sizeof(srv_addr)) < 0) {
    perror("bind");
    close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, 16) < 0) {
    perror("listen");
    close(listen_fd);
    return 1;
  }

  fprintf(stderr,
          "disk_server: Cylinders=%d Sectors=%d Delay=%dus file=%s port=%d\n",
          cylinders, sectors, delay_us, argv[4], PORT_DEFAULT);

  // accept clients one at a time (no fork/threads here)
  while (1) {

    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR)
        continue;
      perror("accept failed");
      continue;
    }

    serve_client(client_fd, cylinders, sectors, delay_us, backing_fd);
  }
}

static ssize_t recv_all(int fd, void *buf, size_t bytes_requested) {
  size_t bytes_filled = 0;
  char *buf_pointer = buf;

  while (bytes_filled < bytes_requested) {
    ssize_t bytes_received =
        recv(fd, buf_pointer + bytes_filled, bytes_requested - bytes_filled, 0);

    if (bytes_received == 0)
      return 0; // peer closed

    if (bytes_received < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }

    bytes_filled += (size_t)bytes_received;
  }

  return (ssize_t)bytes_filled;
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
      break; // stop at newline
  }

  return (ssize_t)n;
}

static off_t blk_offset(int cylinders, int sectors, int cylinder_request,
                        int sector_request) {
  // convert (c,s) into byte offset in backing file
  return ((off_t)cylinder_request * sectors + sector_request) * BLOCK_SIZE;
}

static void sleep_tracks(int tracks, int delay_us) {
  if (tracks <= 0 || delay_us <= 0)
    return;

  long long total = (long long)tracks * delay_us;
  usleep((useconds_t)total); // simulate seek latency
}

static void serve_client(int client_fd, int cylinders, int sectors,
                         int delay_us, int backing_fd) {

  char line[MAX_LINE];
  int current_cyl = 0; // remember last cylinder head was on

  while (1) {

    ssize_t n = recv_line(client_fd, line, sizeof(line));

    if (n <= 0)
      break;

    if (n == sizeof(line))
      n--; // leave room for '\0'

    line[n] = '\0';

    if (n > 0 && line[n - 1] == '\n')
      line[n - 1] = '\0'; // trim newline

    char cmd;
    int c, s, l;

    if (sscanf(line, " %c", &cmd) != 1) {
      send_all(client_fd, "0\n", 2);
      continue;
    }

    /* ----- I: geometry ----- */
    if (cmd == 'I') {

      char out[64];
      int m = snprintf(out, sizeof(out), "%d %d\n", cylinders, sectors);

      if (send_all(client_fd, out, (size_t)m) < 0)
        break;

      continue;
    }

    /* ----- R: read block ----- */
    if (cmd == 'R') {

      if (sscanf(line, " R %d %d", &c, &s) != 2 || c < 0 || s < 0 ||
          c >= cylinders || s >= sectors) {

        send_all(client_fd, "0\n", 2); // invalid request
        continue;
      }

      int tracks = abs(current_cyl - c);
      sleep_tracks(tracks, delay_us); // simulate moving head
      current_cyl = c;

      unsigned char block[BLOCK_SIZE];

      if (lseek(backing_fd, blk_offset(cylinders, sectors, c, s), SEEK_SET) <
          0) {
        send_all(client_fd, "0\n", 2);
        continue;
      }

      ssize_t r = read(backing_fd, block, BLOCK_SIZE);
      if (r < 0) {
        send_all(client_fd, "0\n", 2);
        continue;
      }

      if (r < BLOCK_SIZE)
        memset(block + r, 0, BLOCK_SIZE - r); // pad short reads

      if (send_all(client_fd, "1", 1) < 0)
        break;

      if (send_all(client_fd, block, BLOCK_SIZE) < 0)
        break;

      continue;
    }

    /* ----- W: write block ----- */
    if (cmd == 'W') {

      if (sscanf(line, " W %d %d %d", &c, &s, &l) != 3 || c < 0 || s < 0 ||
          c >= cylinders || s >= sectors || l < 0 || l > BLOCK_SIZE) {

        send_all(client_fd, "0\n", 2);
        continue;
      }

      unsigned char block[BLOCK_SIZE];
      memset(block, 0, BLOCK_SIZE); // default zeros

      if (recv_all(client_fd, block, (size_t)l) <= 0) {
        send_all(client_fd, "0\n", 2);
        continue;
      }

      char nl;
      if (recv_all(client_fd, &nl, 1) <= 0 || nl != '\n') {
        send_all(client_fd, "0\n", 2);
        continue;
      }

      int tracks = abs(current_cyl - c);
      sleep_tracks(tracks, delay_us);
      current_cyl = c;

      if (lseek(backing_fd, blk_offset(cylinders, sectors, c, s), SEEK_SET) <
          0) {
        send_all(client_fd, "0\n", 2);
        continue;
      }

      if (write(backing_fd, block, BLOCK_SIZE) != BLOCK_SIZE) {
        send_all(client_fd, "0\n", 2);
        continue;
      }

      (void)fsync(backing_fd); // flush write to disk

      if (send_all(client_fd, "1\n", 2) < 0)
        break;

      continue;
    }

    /* ----- invalid command ----- */
    send_all(client_fd, "0\n", 2);
  }

  close(client_fd);
}
