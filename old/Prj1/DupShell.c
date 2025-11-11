#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int parse(char *line, char **left, char **right);
static int split_args(char *s, char **argv, int max);

int main(void) {
  char line[512];
  char *argv1[64], *argv2[64];

  for (;;) {
    write(STDOUT_FILENO, "dup$ ", 5);
    if (!fgets(line, sizeof line, stdin))
      break;
    size_t n = strlen(line);
    if (n && line[n - 1] == '\n')
      line[n - 1] = '\0';
    if (!strcmp(line, "exit"))
      break;
    if (line[0] == '\0')
      continue;

    char *l = NULL, *r = NULL;
    if (!parse(line, &l, &r)) {
      int ac = split_args(line, argv1, 64);
      if (ac == 0)
        continue;
      pid_t p = fork();
      if (p == 0) {
        execvp(argv1[0], argv1);
        perror("exec");
        _exit(127);
      }
      int st;
      waitpid(p, &st, 0);
      continue;
    }

    int pfd[2];
    if (pipe(pfd) < 0) {
      perror("pipe");
      continue;
    }

    split_args(l, argv1, 64);
    split_args(r, argv2, 64);

    pid_t cmd1 = fork();
    if (cmd1 == 0) {
      dup2(pfd[1], STDOUT_FILENO);
      close(pfd[0]);
      close(pfd[1]);
      execvp(argv1[0], argv1);
      perror("exec1");
      _exit(127);
    }

    pid_t cmd2 = fork();
    if (cmd2 == 0) {
      dup2(pfd[0], STDIN_FILENO);
      close(pfd[1]);
      close(pfd[0]);
      execvp(argv2[0], argv2);
      perror("exec2");
      _exit(127);
    }

    close(pfd[0]);
    close(pfd[1]);
    int st;
    waitpid(cmd1, &st, 0);
    waitpid(cmd2, &st, 0);
  }
  return 0;
}

static int parse(char *line, char **left, char **right) {
  char *null_byte = strchr(line, '|'); // scan line for the first byte equal to
                                       // |
  if (!null_byte)
    return 0;             // if no | was found just return
  *null_byte = '\0';      // sets | byte to null byte
  *left = line;           // last byte read before | point to the left
  *right = null_byte + 1; // byte after | points to the right
  return 1;
}

static int split_args(char *s, char **argv, int max) {
  int ac = 0;
  char *tok = strtok(s, " \t");
  while (tok && ac < max - 1) {
    argv[ac++] = tok;
    tok = strtok(NULL, " \t");
  }
  argv[ac] = NULL;
  return ac;
}
