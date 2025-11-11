#include <arpa/inet.h> // // htons, htonl, inet_pton (not used here), sockaddr_in
#include <bits/types/idtype_t.h>
#include <netinet/in.h> // // struct sockaddr_in
#include <signal.h>     // // signal(), SIGCHLD
#include <stdio.h>      // // fprintf, perror, dprintf
#include <stdlib.h>     // // atoi, exit
#include <string.h>     // // memset, strtok
#include <sys/socket.h> // // socket, bind, listen, accept, send, recv
#include <sys/types.h>  // // pid_t
#include <sys/wait.h>   // // waitpid (optional if not ignoring SIGCHLD)
#include <unistd.h>     // // close, dup2, fork, execvp

// --- constants --- //
#define MAX_LINE 4096 // // max bytes to read for the parameter line
#define MAX_ARGS 64   // // max number of ls arguments we will accept
#define PORT_DEFAULT 777
#define PROTOCOL_DEFAULT 0

// --- forward decls (so main comes first) --- //
static int read_line(int fd, char *buffer, int max); // // read up to '\n'
static int split_whitespace(char *line, char *argv_out[],
                            int n); // // split line into argv tokens

// --- main: listen, accept, fork child to exec ls with client-provided args ---
// //
int main(int argc, char *argv[]) {                  // // entry point
  if (argc != 2) {                                  // // need 1 arg: <port>
    fprintf(stderr, "usage: %s <port>\n", argv[0]); // // print usage
    return 1;                                       // // exit error
  }

  int listen_fd = socket(AF_INET, SOCK_STREAM, PROTOCOL_DEFAULT);

  if (listen_fd < 0) {
    perror("bad socket");
    return 1;
  }

  struct sockaddr_in srv_addr = {.sin_family = AF_INET,
                                 .sin_addr.s_addr = htonl(INADDR_ANY),
                                 .sin_port = htons(PORT_DEFAULT)};

  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t addr_length = sizeof(srv_addr);

  if (bind(listen_fd, sock_addr, addr_length < 0)) {
    perror("bad bind");
    return 1;
  }

  if (listen(listen_fd, 10) < 0) { // // start listening
    perror("listen");              // // show error
    return 1;                      // // exit
  } // // end listen

  signal(SIGCHLD, SIG_IGN);

  while (1) {
    struct sockaddr_in client;
    struct sockaddr *client_address = (struct sockaddr *)&client;
    socklen_t client_length = sizeof(client);

    int client_fd = accept(listen_fd, client_address, &client_length);

    if (client_fd < 0) {
      perror("cli zero");
      continue;
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      close(client_fd);
      continue;
    } else if (pid == 0) {
      close(listen_fd);
      char line[MAX_LINE];
      int read_attempt = read_line(client_fd, line, MAX_LINE);

      if (read_attempt < 0) {
        dprintf(client_fd, "bad read\n");
        close(client_fd);
        _exit(1);
      }

      if (read_attempt > 0 && line[read_attempt - 1] == '\n') {
        line[read_attempt - 1] = '\0';
      }

      char *argv_list[MAX_ARGS + 2];
      argv_list[0] = "ls";
      int argc_list = split_whitespace(line, &argv_list[1], MAX_ARGS);
      argv_list[1 + argc_list] = NULL;

      dup2(client_fd, STDOUT_FILENO);
      dup2(client_fd, STDERR_FILENO);

      execvp("ls", argv_list);
      perror("execvp error\n");
      _exit(127);
    } else {
      close(client_fd);
    }
  }
}

static int readline(int fd, char *buffer, int max) {
  int n = 0;

  for (; n < max; n++) {
    char c;
    int recieve_socket = recv(fd, &c, 1, 0);
    if (recieve_socket == 0)
      break;
    if (recieve_socket < 0)
      return -1;
    buffer[n] = c;
    if (c == '\n')
      break;
  }
  return n;
}

static int split_whitespace(char *line, char *argv_out[], int n) {

  int count = 0;
  for (char *p = strtok(line, " \t\r\n"); p && count < n;
       p = strtok(NULL, " \t\r\n")) {

    argv_out[count++] = p;
  }

  return count;
}
