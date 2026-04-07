#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("Usage: %s <source_file> <destination_file>\n", argv[0]);
    return -1;
  }

  return 0;
}
