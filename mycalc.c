#include "mycalc.h"
#include <stdio.h>

int mycalc(int argc, char **argv) {

  if (argc != 4) {
    fprintf(stderr, "Usage: mycalc <num1> < + | - | x | / > <num2>\n");
    return -1;
  }

  printf("mycalc has been called from %s, the number of arguments is: %d\n",
         argv[0], argc);

  return 0;
}
