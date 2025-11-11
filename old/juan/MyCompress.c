#include <errno.h> //for error readability
#include <fcntl.h> //open
#include <stdio.h>
#include <string.h> //strerror, memset
#include <unistd.h> // read, write, close

static void compress_stream(int input_file_descriptor,
                            int output_file_descriptor) {
  char read_buffer[4096];
  char previous_bit = 0;
  unsigned run_length = 0;
  ssize_t bytes_read;

  int in = input_file_descriptor;
  int out = output_file_descriptor;

  while ((bytes_read = read(in, read_buffer, sizeof(read_buffer))) > 0) {
    for (ssize_t i = 0; i < bytes_read; i++) {
      char current_char = read_buffer[i];

      if (current_char == '0' || current_char == '1') {

        if (run_length == 0 || current_char == previous_bit) {
          run_length++;
        } else {
          if (run_length >= 16) {
            dprintf(out, "%c%u%c", previous_bit == '1' ? '+' : '-', run_length,
                    previous_bit == '1' ? '+' : '-');
          } else {
            for (unsigned j = 0; j < run_length; j++) {
              write(out, &previous_bit, 1);
            }
          }
          run_length = 1;
        }
        previous_bit = current_char;
      } else if (current_char == ' ' || current_char == '\n') {
        if (run_length > 0) {
          if (run_length >= 16) {
            dprintf(out, "%c%u%c", previous_bit == '1' ? '+' : '-', run_length,
                    previous_bit == '1' ? '+' : '-');
          } else {
            for (unsigned j = 0; j < run_length; j++) {
              write(out, &previous_bit, 1);
            }
          }
          run_length = 0;
        }
        write(out, &current_char, 1);
        previous_bit = 0;
      }
    }
  }

  if (run_length > 0) {
    if (run_length >= 16) {
      dprintf(out, "%c%u%c", previous_bit == '1' ? '+' : '-', run_length,
              previous_bit == '1' ? '+' : '-');
    } else {
      for (unsigned j = 0; j < run_length; j++) {
        write(out, &previous_bit, 1);
      }
    }
  }
}
int main(int argument_count, char **argument_value) {
  if (argument_count != 3) {
    fprintf(stderr,
            "You must provide a input file and an output file in order to "
            "compress %s",
            argument_value[0]);
    return 2; // 2 for usage error
  }

  int input_file_descriptor = open(argument_value[1], O_RDONLY);

  if (input_file_descriptor < 0) {
    fprintf(stderr, "file descripter less than zero open failed! %s, %s",
            argument_value[1], strerror(errno));
    return 1; // file error
  }

  int output_file_descriptor =
      open(argument_value[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (output_file_descriptor < 0) {
    fprintf(stderr, "file descriptor less than zero open failed! %s,%s",
            argument_value[2], strerror(errno));
    close(input_file_descriptor);
    return 1; // file error
  }

  compress_stream(input_file_descriptor, output_file_descriptor);
  close(input_file_descriptor);
  close(output_file_descriptor);
}
