
#include <arpa/inet.h>
#include <bits/types/idtype_t.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 4096
#define MAX_ARGS 64
#define PORT_DEFAULT 7777
#define PROTOCOL_DEFAULT 0

// read one line (up to newline or max) from fd
static int read_line(int fd, char *buffer, int max);
// split a line into whitespace-separated args
static int split_whitespace(char *line, char *argv_out[], int n);

int main(int argc, char *argv[]) {

  // server takes no command-line args
  if (argc > 1) {
    fprintf(stderr, "no arguments required: %s\n", argv[0]);
    return 1;
  }

  // create listening TCP socket
  int listen_fd = socket(AF_INET, SOCK_STREAM, PROTOCOL_DEFAULT);

  if (listen_fd < 0) {
    perror("bad socket");
    return 1;
  }

  int yes = 1;
  // allow quick reuse of port after restart
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  // listen on all interfaces on PORT_DEFAULT
  struct sockaddr_in srv_addr = {.sin_family = AF_INET,
                                 .sin_addr.s_addr = htonl(INADDR_ANY),
                                 .sin_port = htons(PORT_DEFAULT)};

  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t addr_length = sizeof(srv_addr);

  // bind socket to address/port
  if (bind(listen_fd, sock_addr, addr_length) < 0) {
    perror("bad bind");
    return 1;
  }

  // start listening for connections
  if (listen(listen_fd, 10) < 0) {
    perror("listen");
    return 1;
  }

  // avoid zombies: let kernel reap child processes
  signal(SIGCHLD, SIG_IGN);

  // main accept loop
  while (1) {
    struct sockaddr_in client;
    struct sockaddr *client_address = (struct sockaddr *)&client;
    socklen_t client_length = sizeof(client);

    // wait for client to connect
    int client_fd = accept(listen_fd, client_address, &client_length);

    if (client_fd < 0) {
      perror("client zero");
      continue;
    }

    // fork child to handle this client
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      close(client_fd);
      continue;
    } else if (pid == 0) {
      // child process: handle one request
      close(listen_fd);
      char line[MAX_LINE];
      int read_attempt = read_line(client_fd, line, MAX_LINE);

      if (read_attempt < 0) {
        dprintf(client_fd, "bad read \n");
        close(client_fd);
        _exit(1);
      }

      // strip newline at end if present
      if (read_attempt > 0 && line[read_attempt - 1] == '\n') {
        line[read_attempt - 1] = '\0';
      }

      // build argv array: ["ls", arg1, arg2, ..., NULL]
      char *argv_list[MAX_ARGS + 2];
      argv_list[0] = "ls";
      int argc_list = split_whitespace(line, &argv_list[1], MAX_ARGS);
      argv_list[1 + argc_list] = NULL;

      // send ls output/errors back over socket
      dup2(client_fd, STDOUT_FILENO);
      dup2(client_fd, STDERR_FILENO);

      // replace child with ls program
      execvp("ls", argv_list);
      // only get here if exec failed
      perror("execvp error \n");
      _exit(127);
    } else {
      // parent: done with this client fd
      close(client_fd);
    }
  }
}

// simple line reader using recv(2)
static int read_line(int fd, char *buffer, int max) {
  int n = 0;

  for (; n < max; n++) {
    char c;
    int recieve_socket = recv(fd, &c, 1, 0);
    if (recieve_socket == 0)
      break; // connection closed
    if (recieve_socket < 0)
      return -1; // error
    buffer[n] = c;
    if (c == '\n')
      break; // stop at newline
  }
  return n;
}

// split input line into tokens separated by whitespace
static int split_whitespace(char *line, char *argv_out[], int n) {

  int count = 0;
  for (char *p = strtok(line, " \t\r\n"); p && count < n;
       p = strtok(NULL, " \t\r\n")) {
    argv_out[count++] = p;
  }

  return count;
}
