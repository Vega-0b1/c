#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "this program takes 3 arguements itself, input, output\n");
    return 2;
  }

  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "what the fork: %s\n", strerror(errno));
    return 1;
  }

  if (pid == 0) {
    execl("./compress", "compress", argv[1], argv[2], (char *)NULL);
    exit(1);
  } else {

    int status;

    if (waitpid(pid, &status, 0) < 0) {
      fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
      return 1;
    }

    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    } else {
      fprintf(stderr, "child acting weird\n");
      return 1;
    }
  }
}
