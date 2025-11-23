
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_LINE 4096
#define PORT_DEFAULT 7777
#define PROTOCOL_DEFAULT 0
#define BACKLOG 16

// helper to read a single line from a socket
static int read_line(int fd, char *buffer, int max);
// helper to reverse a string in-place
static void reverse_string(char *s, int len);
// thread function for each client
static void *client_thread(void *arg);

// small struct to pass client fd into the thread
struct client_info {
  int fd;
};

int main(int argc, char *argv[]) {

  // this server doesn't expect any command line args
  if (argc > 1) {
    fprintf(stderr, "no arguments required: %s\n", argv[0]);
    return 1;
  }

  // create TCP listening socket
  int listen_fd = socket(AF_INET, SOCK_STREAM, PROTOCOL_DEFAULT);
  if (listen_fd < 0) {
    perror("socket");
    return 1;
  }

  int yes = 1;
  // allow quick reuse of the port after restart
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    perror("setsockopt");
    close(listen_fd);
    return 1;
  }

  // listen on all interfaces on PORT_DEFAULT
  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_ANY),
      .sin_port = htons(PORT_DEFAULT),
  };

  struct sockaddr *sock_addr = (struct sockaddr *)&srv_addr;
  socklen_t addr_len = sizeof(srv_addr);

  // bind socket to address and port
  if (bind(listen_fd, sock_addr, addr_len) < 0) {
    perror("bind");
    close(listen_fd);
    return 1;
  }

  // start listening for incoming connections
  if (listen(listen_fd, BACKLOG) < 0) {
    perror("listen");
    close(listen_fd);
    return 1;
  }

  fprintf(stderr, "reverse server listening on port %d\n", PORT_DEFAULT);

  // main accept loop
  while (1) {

    // block until a client connects
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR)
        continue;
      perror("accept");
      continue;
    }

    // allocate per-client info struct
    struct client_info *info = malloc(sizeof(*info));
    if (!info) {
      perror("malloc");
      close(client_fd);
      continue;
    }

    info->fd = client_fd;

    pthread_t tid;
    // create a new thread to handle this client
    int err = pthread_create(&tid, NULL, client_thread, info);
    if (err != 0) {
      fprintf(stderr, "pthread_create: %s\n", strerror(err));
      close(client_fd);
      free(info);
      continue;
    }

    /* one thread per client, detached */
    pthread_detach(tid);
  }

  /* not reached */
  close(listen_fd);
  return 0;
}

// thread entry point: handle one client connection
static void *client_thread(void *arg) {
  struct client_info *info = (struct client_info *)arg;
  int client_fd = info->fd;
  free(info); // no longer need the struct

  char line[MAX_LINE];
  // read a single line from the client
  int n = read_line(client_fd, line, MAX_LINE);

  if (n < 0) {
    perror("read_line");
    close(client_fd);
    return NULL;
  }

  if (n == 0) {
    /* client closed without sending anything */
    close(client_fd);
    return NULL;
  }

  /* strip trailing newline if present */
  if (line[n - 1] == '\n') {
    n--;
  }

  /* reverse just the characters, not the newline */
  reverse_string(line, n);

  line[n] = '\n';
  ssize_t total = n + 1;

  // send reversed line back to client
  if (send(client_fd, line, total, 0) < 0) {
    perror("send");
  }

  close(client_fd);
  return NULL;
}

// read up to max bytes or until newline from socket
static int read_line(int fd, char *buffer, int max) {
  int n = 0;

  for (; n < max; n++) {
    char c;
    int r = recv(fd, &c, 1, 0);

    if (r == 0)
      break; /* connection closed */

    if (r < 0) {
      if (errno == EINTR)
        continue; /* retry on interrupt */
      return -1;  /* real error */
    }

    buffer[n] = c;

    if (c == '\n')
      break;
  }

  return n;
}

// simple in-place string reverse using two indices
static void reverse_string(char *s, int len) {
  int i = 0;
  int j = len - 1;

  while (i < j) {
    char tmp = s[i];
    s[i] = s[j];
    s[j] = tmp;
    i++;
    j--;
  }
}
