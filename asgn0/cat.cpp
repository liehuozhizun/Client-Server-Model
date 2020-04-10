/*
 * Hang Yuan
 * 1564348, hyuan3
 *
 * ASGN 0: mycat
 * This program implements the basic functions of cat on linux, without
 * support for any flags and functional arguments.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 65536 /* 64 KiB maximum buffer size*/

/* error message generator */
void error_message(char *buffer, char *arg_v, int32_t error_number) {
  memset(buffer, 0, BUFFER_SIZE);
  strcat(buffer, "mycat: ");
  strcat(buffer, arg_v);
  strcat(buffer, ": ");
  strcat(buffer, strerror(error_number));
  strcat(buffer, "\n");
}

int32_t main(int32_t argc, char *argv[]) {
  char buf[BUFFER_SIZE];
  char tem_symble[2] = "-";

  /* no more arguments rather than "mycat" -> standard Input and Output */
  if (argc == 1) {
    while (true) {
      int32_t read_result;
      read_result = read(0, buf, sizeof(buf));
      /* if no argument is recieved */
      if (read_result == 0) {
        break;
      }
      /* failed to read file -> print error message */
      if (read_result == -1) {
        error_message(buf, tem_symble, errno);
        write(1, buf, sizeof(buf));
        break;
      };
      write(1, buf, read_result);
    }
  }

  /* with other arguments -> do file_validation_check */
  for (int32_t i = 1; i < argc; i++) {
    int32_t open_result = open(argv[i], O_RDONLY);
    /* failed to open file -> print error message */
    if (open_result == -1) {
      error_message(buf, argv[i], errno);
      write(1, buf, sizeof(buf));
    } else {
      int32_t read_result;
      /* control the exit conditions of loop for reading file more than 64KiB
       * read_result isn't 0 -> finish reading or have an error
       */
      do {
        read_result = read(open_result, buf, sizeof(buf));
        /* fail to read file -> print error message */
        if (read_result == -1) {
          error_message(buf, argv[i], errno);
          write(1, buf, sizeof(buf));
        }
        /* successfully read file -> write standard output */
        else {
          write(1, buf, read_result);
        }
      } while (read_result != 0 && read_result != -1);
    }
  }
  return 0;
}
