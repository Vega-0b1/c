#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  char line[512]; // line size
  char *argv[64]; // arguement size
  for (;;) {
    write(STDOUT_FILENO, "more$ ", 6); // writes 6 bytes for more$ prompt
    if (!fgets(line, sizeof line, stdin))
      break;
    size_t n = strlen(line);
    if (n && line[n - 1] ==
                 '\n') // if there's a tab in n-1 line then insert null byte
      line[n - 1] = '\0';
    if (line[0] == '\0') // if null byte is detected in the start of line then
                         // continue loop
      continue;
    if (!strcmp(line, "exit")) // if compare string "exit" returns 0, then 1
      break;

    int ac = 0;
    char *tok = strtok(line, " \t");
    while (tok && ac < 63) {
      argv[ac++] = tok;
      tok = strtok(NULL, " \t");
    }
    argv[ac] = NULL;
    if (ac == 0)
      continue;

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      continue;
    }
    if (pid == 0) {
      execvp(argv[0], argv);
      fprintf(stderr, "exec fail: %s\n", strerror(errno));
      _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0)
      perror("waitpid");
  }
  return 0;
}
