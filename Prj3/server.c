#include <arpa/inet.h>  // // inet_ntop/htonl/htons structures and helpers
#include <netinet/in.h> // // sockaddr_in definition
#include <pthread.h>    // // pthreads for thread-per-request
#include <stdio.h>      // // printf, fprintf, perror
#include <stdlib.h>     // // atoi, malloc, free, exit
#include <string.h>     // // memset, memcpy
#include <sys/socket.h> // // socket, bind, listen, accept, send, recv
#include <unistd.h>     // // close

#define MAX_SIZE 4096 // // maximum bytes we read/write for a single line
#define PORT_DEFAULT 777

static void reverse_string(char *client_string, int bytes_read);
static void *serve(void *arg);

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage 2 arguements %s", argv[0]);
    return 1;
  }

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("failed socket");
    return 1;
  }
  struct sockaddr_in srv_addr = {.sin_family = AF_INET,
                                 .sin_addr.s_addr = htonl(INADDR_ANY),
                                 .sin_port = htons(PORT_DEFAULT)};

  if (bind(listen_fd, (struct sockaddr *)&srv_addr,
           sizeof(srv_addr)) < 0) { // // bind address to socket
    perror("bind");                 // // print error reason
    return 1;                       // // exit on bind failure
  } // // end bind block

  if (listen(listen_fd, 10) < 0) { // // start listening with small backlog
    perror("listen");              // // print error reason
    return 1;                      // // exit on listen failure
  } // // end listen block

  while (1) {                       // // main accept loop: run forever
    struct sockaddr_in client_addr; // // where accept() stores client address
    socklen_t client_len = sizeof(client_addr); // // size of that structure
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr,
                           &client_len); // // wait for a client
    if (client_fd < 0) {
      perror("accept");
      continue;
    } // // if accept failed, log and keep going

    int *fd_heap = (int *)malloc(
        sizeof(int)); // // allocate heap memory to pass fd to thread
    if (!fd_heap) {
      close(client_fd);
      continue;
    } // // if malloc failed, close client and skip
    *fd_heap = client_fd; // // store client fd in heap memory

    pthread_t thread_id; // // thread handle
    if (pthread_create(&thread_id, NULL, serve, fd_heap) ==
        0)                       // // start a worker to serve this client
      pthread_detach(thread_id); // // detach so it cleans up on exit
    else {                       // // thread creation failed
      perror("pthread_create");  // // report the error
      close(client_fd);          // // close the client socket
      free(fd_heap);             // // free the heap memory
    } // // end thread creation branch
  }
}

static void reverse_string(char *client_string, int bytes_read) {
  int string_length = (bytes_read > 0 && client_string[bytes_read - 1] == '\n')
                          ? bytes_read - 1
                          : bytes_read;
  for (int i = 0, j = string_length - 1; i < j; i++, j--) {
    char tmp = client_string[i];
    client_string[i] = client_string[j];
    client_string[j] = tmp;
  }
}

static void *serve(void *arg) {
  int fd = *(int *)arg;
  free(arg);
  char buffer[MAX_SIZE];
  int n = 0;
  for (; n < MAX_SIZE; n++) {
    char c;
    int r = recv(fd, &c, 1, 0);
    buffer[n] = c;
    if (c == '\n')
      break;
  }

  if (n > 0) {
    reverse_string(buffer, n);
    send(fd, buffer, n, 0);
  }
  close(fd);
  return NULL;
}
