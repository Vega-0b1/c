// this program compresses a file in parallel using threads
// it splits the file into chunks, each thread compresses a chunk,
// and then the main thread joins results into the output file

#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
  int in_fd;      // input file
  off_t off;      // offset in file
  size_t len;     // length of chunk
  char *out_buf;  // compressed result
  size_t out_len; // length of result
} job_t;

static void compress_mem(const char *data, size_t len, char **outp,
                         size_t *outlen);
static void *worker(void *arg);

int main(int argc, char **argv) {
  if (argc != 4) { // check arguments
    fprintf(stderr, "usage: %s in out nthreads\n", argv[0]);
    return 2;
  }

  // open input file
  int in = open(argv[1], O_RDONLY);
  if (in < 0) {
    perror("open input");
    return 1;
  }

  // open or create output file
  int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) {
    perror("open output");
    close(in);
    return 1;
  }

  // number of threads
  int nthr = atoi(argv[3]);
  if (nthr < 1)
    nthr = 1;

  // get input size
  struct stat st;
  if (fstat(in, &st) < 0) {
    perror("fstat");
    return 1;
  }
  off_t size = st.st_size;
  off_t chunk = (size + nthr - 1) / nthr;

  // allocate jobs and threads
  pthread_t *t = calloc(nthr, sizeof *t);
  job_t *jobs = calloc(nthr, sizeof *jobs);

  // create worker threads
  for (int i = 0; i < nthr; i++) {
    jobs[i].in_fd = in;
    jobs[i].off = i * chunk;
    jobs[i].len = (jobs[i].off + chunk <= size) ? chunk : (size - jobs[i].off);
    pthread_create(&t[i], NULL, worker, &jobs[i]);
  }

  // wait for all threads
  for (int i = 0; i < nthr; i++)
    pthread_join(t[i], NULL);

  // write results in order
  for (int i = 0; i < nthr; i++) {
    if (jobs[i].out_buf && jobs[i].out_len) {
      if (write(out, jobs[i].out_buf, jobs[i].out_len) !=
          (ssize_t)jobs[i].out_len)
        perror("write output");
      free(jobs[i].out_buf);
    }
  }

  free(jobs);
  free(t);
  close(in);
  close(out);
  return 0;
}

static void compress_mem(const char *data, size_t len, char **outp,
                         size_t *outlen) {
  size_t cap = len * 2 + 32; // enough space to hold output
  char *out = malloc(cap);
  size_t w = 0;
  char prev = 0;
  unsigned run = 0;

  for (size_t i = 0; i < len; i++) {
    char c = data[i];

    if (c == '0' || c == '1') {
      if (run == 0 || c == prev)
        run++; // continue run
      else {
        // flush previous run
        if (run >= 16)
          w += snprintf(out + w, cap - w, "%c%u%c", prev == '1' ? '+' : '-',
                        run, prev == '1' ? '+' : '-');
        else
          for (unsigned j = 0; j < run; j++)
            out[w++] = prev;
        run = 1;
      }
      prev = c;
    } else if (c == ' ' || c == '\n') {
      // flush run before space or newline
      if (run > 0) {
        if (run >= 16)
          w += snprintf(out + w, cap - w, "%c%u%c", prev == '1' ? '+' : '-',
                        run, prev == '1' ? '+' : '-');
        else
          for (unsigned j = 0; j < run; j++)
            out[w++] = prev;
        run = 0;
      }
      out[w++] = c; // copy delimiter
      prev = 0;
    }

    // grow buffer if almost full
    if (w + 64 > cap) {
      cap *= 2;
      out = realloc(out, cap);
    }
  }

  // flush leftover run at end
  if (run > 0) {
    if (run >= 16)
      w += snprintf(out + w, cap - w, "%c%u%c", prev == '1' ? '+' : '-', run,
                    prev == '1' ? '+' : '-');
    else
      for (unsigned j = 0; j < run; j++)
        out[w++] = prev;
  }

  *outp = out;
  *outlen = w;
}

static void *worker(void *arg) {
  job_t *j = arg;

  // read chunk from file
  char *buf = malloc(j->len);
  if (j->len && pread(j->in_fd, buf, j->len, j->off) != (ssize_t)j->len) {
    perror("pread");
    j->out_buf = NULL;
    j->out_len = 0;
    free(buf);
    return NULL;
  }

  // compress chunk into output buffer
  compress_mem(buf, j->len, &j->out_buf, &j->out_len);
  free(buf);
  return NULL;
}
