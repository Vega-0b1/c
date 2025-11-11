
// client_ls.c — minimal client: send ls-parameters line; print server's ls
// output. //

#include <arpa/inet.h>  // // inet_pton, htons
#include <netinet/in.h> // // sockaddr_in
#include <stdio.h>      // // fprintf, perror, printf
#include <stdlib.h>     // // atoi, exit
#include <string.h>     // // strlen, memcpy
#include <sys/socket.h> // // socket, connect, send, recv
#include <unistd.h>     // // close, write

#define MAX_LINE 4096 // // max outbound line length
#define RX_BUF 4096   // // recv buffer

int main(int argc, char **argv) { // // entry point
  if (argc < 3) {                 // // need at least server-ip and port
    fprintf(stderr, "usage: %s <server-ip> <port> [ls-args...]\n",
            argv[0]); // // usage note
    return 1;         // // exit error
  } // // end arg check

  int sockfd = socket(AF_INET, SOCK_STREAM, 0); // // create IPv4 TCP socket
  if (sockfd < 0) {
    perror("socket");
    return 1;
  } // // fail if cannot create

  struct sockaddr_in srv;                                // // server address
  memset(&srv, 0, sizeof(srv));                          // // zero init
  srv.sin_family = AF_INET;                              // // IPv4
  srv.sin_port = htons((unsigned short)atoi(argv[2]));   // // port from CLI
  if (inet_pton(AF_INET, argv[1], &srv.sin_addr) != 1) { // // parse server IP
    fprintf(stderr, "bad ip\n");                         // // invalid IP string
    close(sockfd);                                       // // close socket
    return 1;                                            // // exit
  } // // end IP parse

  if (connect(sockfd, (struct sockaddr *)&srv, sizeof(srv)) <
      0) {             // // connect to server
    perror("connect"); // // error if fails
    close(sockfd);     // // close socket
    return 1;          // // exit
  } // // end connect

  char line[MAX_LINE]; // // outbound line buffer
  size_t off = 0;      // // current length

  if (argc > 3) {                    // // if we have ls args
    for (int i = 3; i < argc; i++) { // // append each arg with spaces
      size_t len = strlen(argv[i]);  // // length of this arg
      if (off + len + 1 >=
          sizeof(line)) { // // check capacity (space + maybe final '\n')
        fprintf(stderr, "args too long\n"); // // error if too big
        close(sockfd);                      // // close socket
        return 1;                           // // exit
      } // // end capacity check
      memcpy(line + off, argv[i], len); // // copy arg
      off += len;                       // // advance length
      if (i != argc - 1)
        line[off++] = ' '; // // add space between args
    } // // end for
  } // // end if args
  if (off + 1 >= sizeof(line)) {        // // ensure room for newline
    fprintf(stderr, "args too long\n"); // // still too big
    close(sockfd);                      // // close
    return 1;                           // // exit
  } // // end newline capacity check
  line[off++] = '\n'; // // terminate line with newline

  if (send(sockfd, line, (int)off, 0) < 0) { // // send the parameter line
    perror("send");                          // // report error
    close(sockfd);                           // // close
    return 1;                                // // exit
  } // // end send

  char buf[RX_BUF]; // // buffer for receiving ls output
  for (;;) {        // // read until EOF
    int r = recv(sockfd, buf, sizeof(buf), 0); // // receive some bytes
    if (r < 0) {
      perror("recv");
      break;
    } // // error?
    if (r == 0)
      break; // // EOF from server
    if (write(STDOUT_FILENO, buf, r) < 0) {
      perror("write");
      break;
    } // // print to stdout
  } // // end recv loop

  close(sockfd); // // close socket
  return 0;      // // success
} // // end main
