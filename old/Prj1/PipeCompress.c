#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void compress_stream(int in, int out); // prototype

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s in out\n", argv[0]);
    return 2;
  }

  int in_fd = open(argv[1], O_RDONLY);
  if (in_fd < 0) {
    perror("open in");
    return 1;
  }
  int out_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out_fd < 0) {
    perror("open out");
    close(in_fd);
    return 1;
  }

  int pfd[2];
  if (pipe(pfd) < 0) {
    perror("pipe");
    close(in_fd);
    close(out_fd);
    return 1;
  }

  pid_t r = fork();
  if (r < 0) {
    perror("fork reader");
    return 1;
  }
  if (r == 0) {
    close(pfd[0]);
    char buf[4096];
    ssize_t n;
    while ((n = read(in_fd, buf, sizeof buf)) > 0)
      if (write(pfd[1], buf, n) != n) {
        perror("write pipe");
        _exit(1);
      }
    if (n < 0) {
      perror("read in");
      _exit(1);
    }
    close(pfd[1]);
    _exit(0);
  }

  pid_t w = fork();
  if (w < 0) {
    perror("fork writer");
    return 1;
  }
  if (w == 0) {
    close(pfd[1]);
    compress_stream(pfd[0], out_fd);
    close(pfd[0]);
    _exit(0);
  }

  close(pfd[0]);
  close(pfd[1]);
  close(in_fd);
  close(out_fd);

  int st;
  if (waitpid(r, &st, 0) < 0) {
    perror("wait reader");
    return 1;
  }
  if (waitpid(w, &st, 0) < 0) {
    perror("wait writer");
    return 1;
  }
  return 0;
}

static void compress_stream(int in, int out) {
  char buf[4096], prev = 0;
  unsigned run = 0;
  ssize_t n;
  while ((n = read(in, buf, sizeof buf)) > 0) {
    for (ssize_t i = 0; i < n; i++) {
      char c = buf[i];
      if (c == '0' || c == '1') {
        if (run == 0 || c == prev)
          run++;
        else {
          if (run >= 16)
            dprintf(out, "%c%u%c", prev == '1' ? '+' : '-', run,
                    prev == '1' ? '+' : '-');
          else
            for (unsigned j = 0; j < run; j++)
              write(out, &prev, 1);
          run = 1;
        }
        prev = c;
      } else if (c == ' ' || c == '\n') {
        if (run > 0) {
          if (run >= 16)
            dprintf(out, "%c%u%c", prev == '1' ? '+' : '-', run,
                    prev == '1' ? '+' : '-');
          else
            for (unsigned j = 0; j < run; j++)
              write(out, &prev, 1);
          run = 0;
        }
        write(out, &c, 1);
        prev = 0;
      }
    }
  }
  if (run > 0) {
    if (run >= 16)
      dprintf(out, "%c%u%c", prev == '1' ? '+' : '-', run,
              prev == '1' ? '+' : '-');
    else
      for (unsigned j = 0; j < run; j++)
        write(out, &prev, 1);
  }
}
