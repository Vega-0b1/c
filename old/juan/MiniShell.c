// this program is a very simple shell
// it shows a prompt, runs one command with no arguments, and stops when you
// type exit

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  char line[256]; // input buffer

  for (;;) {
    // print prompt
    write(STDOUT_FILENO, "mini$ ", 6);

    // read input line
    if (!fgets(line, sizeof line, stdin))
      break;

    // cut newline if present
    size_t n = strlen(line);
    if (n && line[n - 1] == '\n')
      line[n - 1] = '\0';

    // skip empty input
    if (line[0] == '\0')
      continue;

    // exit command
    if (!strcmp(line, "exit"))
      break;

    // fork child
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      continue;
    }

    if (pid == 0) {
      // child runs command (no arguments)
      execlp(line, line, (char *)NULL);
      // only runs if exec fails
      fprintf(stderr, "exec fail: %s\n", strerror(errno));
      _exit(127);
    }

    // parent waits for child
    int st;
    if (waitpid(pid, &st, 0) < 0)
      perror("waitpid");
  }

  return 0;
}
