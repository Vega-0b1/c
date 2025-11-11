
// 5) ParFork.c
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void compress_mem_to_fd(const char *data, size_t len,
                               int out); // prototype

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s in out nprocs\n", argv[0]);
    return 2;
  }

  int in = open(argv[1], O_RDONLY);
  if (in < 0) {
    perror("open in");
    return 1;
  }
  int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) {
    perror("open out");
    close(in);
    return 1;
  }

  int nprocs = atoi(argv[3]);
  if (nprocs < 1)
    nprocs = 1;

  struct stat st;
  if (fstat(in, &st) < 0) {
    perror("fstat");
    return 1;
  }
  off_t size = st.st_size;
  off_t chunk = (size + nprocs - 1) / nprocs;

  char tmpl[] = "/tmp/pfXXXXXX";
  int *tfd = calloc(nprocs, sizeof(int));
  char **paths = calloc(nprocs, sizeof(char *));

  for (int i = 0; i < nprocs; i++) {
    paths[i] = strdup(tmpl);
    int fd = mkstemp(paths[i]);
    if (fd < 0) {
      perror("mkstemp");
      return 1;
    }
    tfd[i] = fd;
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      return 1;
    }
    if (pid == 0) {
      off_t off = i * chunk;
      off_t len = (off + chunk <= size) ? chunk : (size - off);
      if (len < 0)
        len = 0;
      char *buf = malloc(len);
      if (len && pread(in, buf, len, off) != len) {
        perror("pread");
        _exit(1);
      }
      compress_mem_to_fd(buf, len, fd);
      free(buf);
      close(fd);
      _exit(0);
    }
  }

  for (int i = 0; i < nprocs; i++) {
    int stt;
    if (wait(&stt) < 0) {
      perror("wait");
      return 1;
    }
  }

  for (int i = 0; i < nprocs; i++) {
    lseek(tfd[i], 0, SEEK_SET);
    char buf[8192];
    ssize_t n;
    while ((n = read(tfd[i], buf, sizeof buf)) > 0)
      if (write(out, buf, n) != n) {
        perror("write out");
        return 1;
      }
    close(tfd[i]);
    unlink(paths[i]);
    free(paths[i]);
  }

  free(paths);
  free(tfd);
  close(in);
  close(out);
  return 0;
}

static void compress_mem_to_fd(const char *data, size_t len, int out) {
  char prev = 0;
  unsigned run = 0;
  for (size_t i = 0; i < len; i++) {
    char c = data[i];
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
  if (run > 0) {
    if (run >= 16)
      dprintf(out, "%c%u%c", prev == '1' ? '+' : '-', run,
              prev == '1' ? '+' : '-');
    else
      for (unsigned j = 0; j < run; j++)
        write(out, &prev, 1);
  }
}
