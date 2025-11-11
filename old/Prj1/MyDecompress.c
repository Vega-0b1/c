#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUF_SIZE 1024

void decompress(int input_file_descriptor, int output_file_descriptor);

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "error %s", argv[0]);
    return 2;
  }

  int in = open(argv[1], O_RDONLY);
  if (in < 0) {
    perror("open source fail");
    return 1;
  }

  int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) {
    perror("open destination failed");
    close(out);
    return 1;
  }

  decompress(in, out);

  close(in);
  close(out);
  return 0;
}

void decompress(int input_file_descriptor, int output_file_descriptor) {
  int in = input_file_descriptor;
  int out = output_file_descriptor;

  char buf[BUF_SIZE];
  ssize_t bytes_read;
  char num_buf[64];
  int num_id = 0;
  int state = 0;

  for (;;) {
    bytes_read = read(in, buf, BUF_SIZE);
    if (bytes_read < 0) {
      perror("read");
      exit(1);
    }
    if (bytes_read == 0) {
      break;
    }

    for (ssize_t i = 0; i < bytes_read; i++) {
      char c = buf[i];

      if (c == '-') {
        if (state == 0) {
          state = 1;
          num_id = 0;
        } else if (state == 1) {
          num_buf[num_id] = '\0';
          int count = atoi(num_buf);
          for (int j = 0; j < count; j++)
            write(out, "0", 1);

          state = 0;
          num_id = 0;

        } else {
          write(out, "+", 1);
          if (num_id > 0)
            write(out, num_buf, num_id);
          state = 1;
          num_id = 0;
        }
      } else if (c == '+') {
        if (state == 0) {
          state = 2;
          num_id = 0;

        } else if (state == 2) {
          num_buf[num_id] = '\0';
          int count = atoi(num_buf);
          for (int j = 0; j < count; j++)
            write(out, "1", 1);
          state = 0;
          num_id = 0;
        } else {
          write(out, "+", 1);
          if (num_id > 0)
            write(out, num_buf, num_id);
          state = 2;
          num_id = 0;
        }
      } else if ((state == 1 || state == 2) && c >= '0' && c <= '9') {
        if (num_id < (int)sizeof(num_buf) - 1) {
          num_buf[num_id++] = c;
        }
      } else {
        if (state == 1) {
          write(out, "-", 1);
          if (num_id > 0)
            write(out, num_buf, num_id);
          state = 0;
          num_id = 0;
        } else if (state == 2) {
          write(out, "+", 1);
          if (num_id > 0)
            write(out, num_buf, num_id);
          state = 0;
          num_id = 0;
        }
        write(out, &c, 1);
      }
    }
  }
}
