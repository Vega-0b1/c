// this program measures time for compress, ParFork, and ParThread
// it runs each with different process/thread counts and prints the times

#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static double now(void);
static int run_cmd(char *const argv[]);
static double timed(char *const argv[]);

int main(int argc, char **argv) {
  if (argc < 3) { // need input file and output prefix
    fprintf(stderr, "usage: %s input output_prefix\n", argv[0]);
    return 2;
  }
  char *in = argv[1];
  char *outp = argv[2];

  // output filenames
  char out1[256], out2[256], out3[256];
  snprintf(out1, sizeof out1, "%s.seq", outp);
  snprintf(out2, sizeof out2, "%s.pf", outp);
  snprintf(out3, sizeof out3, "%s.pt", outp);

  // run sequential
  char *seqv[] = {"./mycompress", in, out1, NULL};
  double t_seq = timed(seqv);

  int np_list[] = {2, 4, 8};
  int nt_list[] = {2, 4, 8};

  printf("Sequential\t%.6f s\n", t_seq);

  // run ParFork with different process counts
  for (size_t i = 0; i < sizeof(np_list) / sizeof(np_list[0]); i++) {
    int np = np_list[i];
    char nbuf[16];
    snprintf(nbuf, sizeof nbuf, "%d", np);
    char *pfv[] = {"./parfork", in, out2, nbuf, NULL};
    double t = timed(pfv);
    printf("ParFork np=%d\t%.6f s\n", np, t);
  }

  // run ParThread with different thread counts
  for (size_t i = 0; i < sizeof(nt_list) / sizeof(nt_list[0]); i++) {
    int nt = nt_list[i];
    char nbuf[16];
    snprintf(nbuf, sizeof nbuf, "%d", nt);
    char *ptv[] = {"./parthread", in, out3, nbuf, NULL};
    double t = timed(ptv);
    printf("ParThread nt=%d\t%.6f s\n", nt, t);
  }
  return 0;
}

// get current time in seconds
static double now(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

// run one command and wait for it
static int run_cmd(char *const argv[]) {
  pid_t p = fork();
  if (p < 0)
    return -1;
  if (p == 0) {
    execvp(argv[0], argv);
    perror("execvp");
    _exit(127);
  }
  int st;
  if (waitpid(p, &st, 0) < 0)
    return -1;
  if (WIFEXITED(st))
    return WEXITSTATUS(st);
  return -1;
}

// time a command by running it and measuring elapsed time
static double timed(char *const argv[]) {
  double t0 = now();
  int rc = run_cmd(argv);
  double t1 = now();
  if (rc != 0)
    fprintf(stderr, "cmd failed: %s\n", argv[0]);
  return t1 - t0;
}
