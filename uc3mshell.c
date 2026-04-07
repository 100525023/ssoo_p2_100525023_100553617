#include "mycalc.h" // Includes mycalc.h
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const int max_line = 1024;
const int max_commands = 10;
#define max_redirections 3 // stdin, stdout, stderr
#define max_args 15

/* VARS TO BE USED FOR THE STUDENTS */
char *argvv[max_args];
char *filev[max_redirections];
int background = 0;

/**
 * This function splits a char* line into different tokens based on a given
 * character
 * @return Number of tokens
 */
int tokenizar_linea(char *linea, char *delim, char *tokens[], int max_tokens) {
  int i = 0;
  char *token = strtok(linea, delim);
  while (token != NULL && i < max_tokens - 1) {
    tokens[i++] = token;
    token = strtok(NULL, delim);
  }
  tokens[i] = NULL;
  return i;
}

/**
 * This function processes the command line to evaluate if there are
 * redirections. If any redirection is detected, the destination file is
 * indicated in filev[i] array. filev[0] for STDIN filev[1] for STDOUT filev[2]
 * for STDERR
 */
void procesar_redirecciones(char *args[]) {
  int i = 0, first_red = -1;

  // Store the pointer to the filename if needed.
  for (i = 0; args[i] != NULL; i++) {

    if (strcmp(args[i], "<") == 0) {
      filev[0] = args[i + 1];
      if (first_red == -1)
        first_red = i;
    } else if (strcmp(args[i], ">") == 0) {
      filev[1] = args[i + 1];
      if (first_red == -1)
        first_red = i;
    } else if (strcmp(args[i], "!>") == 0) {
      filev[2] = args[i + 1];
      if (first_red == -1)
        first_red = i;
    }
  }

  // starting from the first redirectorion, all fields are set to NULL
  if (first_red != -1)
    for (i = first_red; args[i] != NULL; i++) {
      args[i] = NULL;
    }
}

/**
 * This function processes the input command line and returns in global
 * variables: argvv -- command an args as argv filev -- files for redirections.
 * NULL value means no redirection. background -- 0 means foreground; 1
 * background.
 */
int procesar_linea(char *linea) {

  char *comandos[max_commands];
  int num_comandos = tokenizar_linea(linea, "|", comandos, max_commands);
  background = 0;

  // Check if background is indicated
  if (strchr(comandos[num_comandos - 1], '&')) {
    background = 1;
    char *pos = strchr(comandos[num_comandos - 1], '&');
    // removes character &
    *pos = '\0';
  }

  filev[0] = NULL;
  filev[1] = NULL;
  filev[2] = NULL;
  // Finish processing
  for (int i = 0; i < num_comandos; i++) {

    int args_count = tokenizar_linea(comandos[i], " \t\n", argvv, max_args);
    procesar_redirecciones(argvv);

    /*********
      This piece of code prints the command, args, redirections and
      background.
      Make the required modifications according to the practice statment.
    **********/
    printf("Command \"%s\" has %d tokens\n", comandos[i], args_count);
    for (int arg = 1; arg < max_args; arg++)
      if (argvv[arg] != NULL)
        printf("Args = %s\n", argvv[arg]);

    printf("Background = %d\n", background);
    printf("Redir [IN] = %s\n", filev[0]);
    printf("Redir [OUT] = %s\n", filev[1]);
    printf("Redir [ERR] = %s\n", filev[2]);
    printf("-------\n");
    /**********************************************************************************************/
  }

  return num_comandos;
}

int main(int argc, char *argv[]) {

  printf("Running %s with %d arguments\n", argv[0], argc - 1);

  /* STUDENTS CODE MUST BE HERE */
  char example_line[][1024] = {
      "ls -l | grep scripter | wc -l > redir_out.txt &",
      "cat a < input.txt > output.txt",
      "cat < file.out | tail -x10 > tail.out !> tail.err",
      "echo \"uc3mshell example\" &"};

  // to get the number of entries on "example_line".
  int lines_num = sizeof(example_line) / sizeof(example_line[0]);
  // to process defined lines.
  for (int i = 0; i < lines_num; i++) {
    printf("** Example line: %s\n", example_line[i]);
    int n_commands = procesar_linea(example_line[i]);
    printf("Number of commands = %d\n", n_commands);
    printf("*******************\n");
  }

  mycalc(argc, argv);

  return 0;
}
