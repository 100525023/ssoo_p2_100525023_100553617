#include "mycalc.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

/* Limits for line length, number of piped commands, redirections and args */
const int max_line       = 1024;
const int max_commands   = 10;
#define   max_redirections 3
#define   max_args         15
#define   MAX_PIPE_CMDS    10

/* Global structures filled by procesar_linea and used by execute_commands */
char *argvv[max_args];         /* argv-like array for the current command */
char *filev[max_redirections]; /* redirection targets: [0]=in [1]=out [2]=err */
int   background = 0;          /* 1 = run in background, 0 = foreground */

/* Per-command storage: argv and redirections for each pipe segment */
static char *cmd_argv[MAX_PIPE_CMDS][max_args];
static char *cmd_filev[MAX_PIPE_CMDS][max_redirections];

/*
 * tokenizar_linea: splits 'linea' on 'delim' and stores the resulting
 * pointers in 'tokens'. Returns the number of tokens found.
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

/*
 * strip_quotes: removes surrounding double-quotes from a string in-place.
 * Used so that echo "Hello world" produces the same output as echo Hello world.
 */
static void strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

/*
 * reap_background: non-blocking wait to collect finished background children
 * and prevent zombie processes. Called after every foreground command.
 */
static void reap_background(void) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

/*
 * sigchld_handler: called automatically when a child process finishes.
 * Reaps all available children so we never accumulate zombies.
 */
static void sigchld_handler(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

/*
 * parse_one_command: tokenises one pipe segment and fills cmd_argv[idx] and
 * cmd_filev[idx]. Redirection tokens (<, >, !>) and their filenames are
 * stripped from the argument list so that execvp gets a clean argv.
 * Returns the number of arguments in the command.
 */
static int parse_one_command(int idx, char *segment) {
    int k;

    for (k = 0; k < max_redirections; k++)
        cmd_filev[idx][k] = NULL;

    int argc = tokenizar_linea(segment, " \t\n", cmd_argv[idx], max_args);

    int first_red = -1;
    for (k = 0; cmd_argv[idx][k] != NULL; k++) {
        if (strcmp(cmd_argv[idx][k], "<") == 0) {
            cmd_filev[idx][0] = cmd_argv[idx][k + 1];
            if (first_red == -1) first_red = k;
        } else if (strcmp(cmd_argv[idx][k], ">") == 0) {
            cmd_filev[idx][1] = cmd_argv[idx][k + 1];
            if (first_red == -1) first_red = k;
        } else if (strcmp(cmd_argv[idx][k], "!>") == 0) {
            cmd_filev[idx][2] = cmd_argv[idx][k + 1];
            if (first_red == -1) first_red = k;
        }
    }
    if (first_red != -1) {
        for (k = first_red; cmd_argv[idx][k] != NULL; k++)
            cmd_argv[idx][k] = NULL;
        argc = first_red;
    }

    return argc;
}

/*
 * execute_commands: runs num_comandos commands connected via pipes.
 *
 * For a single command a single fork is done. For N piped commands, N-1 pipes
 * are created and N children are forked. Child i reads from pipe[i-1] and
 * writes to pipe[i]. Stdin redirection applies only to the first command;
 * stdout and stderr redirections apply only to the last command.
 *
 * If background is set, the PID of the last process is printed and the shell
 * does not wait. Otherwise the shell waits for the last command to finish.
 */
static void execute_commands(char *comandos[], int num_comandos) {
    int i;
    int pipes[MAX_PIPE_CMDS - 1][2];
    pid_t pids[MAX_PIPE_CMDS];

    /* Parse each pipe segment into per-command argv and redirections */
    for (i = 0; i < num_comandos; i++)
        parse_one_command(i, comandos[i]);

    /* Create the N-1 pipes needed to connect the commands */
    for (i = 0; i < num_comandos - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("uc3mshell: pipe");
            return;
        }
    }

    for (i = 0; i < num_comandos; i++) {

        /* Skip completely empty segments */
        if (cmd_argv[i][0] == NULL) {
            if (i < num_comandos - 1) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
            pids[i] = -1;
            continue;
        }

        /* --- Internal command: mycalc --- */
        if (strcmp(cmd_argv[i][0], "mycalc") == 0) {
            int ac = 0;
            while (cmd_argv[i][ac] != NULL) ac++;
            mycalc(ac, cmd_argv[i]);
            pids[i] = -1;
            if (i > 0)                { close(pipes[i-1][0]); close(pipes[i-1][1]); }
            if (i < num_comandos - 1) { close(pipes[i][0]);   close(pipes[i][1]);   }
            continue;
        }

        /* --- Internal command: exit --- */
        if (strcmp(cmd_argv[i][0], "exit") == 0) {
            if (cmd_argv[i][1] == NULL) {
                fprintf(stderr, "[ERROR] Missing exit code\n");
                pids[i] = -1;
                if (i > 0)                { close(pipes[i-1][0]); close(pipes[i-1][1]); }
                if (i < num_comandos - 1) { close(pipes[i][0]);   close(pipes[i][1]);   }
                continue;
            }
            char *endptr;
            long code = strtol(cmd_argv[i][1], &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "[ERROR] The exit code must be an integer\n");
                pids[i] = -1;
                if (i > 0)                { close(pipes[i-1][0]); close(pipes[i-1][1]); }
                if (i < num_comandos - 1) { close(pipes[i][0]);   close(pipes[i][1]);   }
                continue;
            }
            /* Wait for all background children before exiting cleanly */
            int st;
            while (waitpid(-1, &st, 0) > 0)
                ;
            printf("Goodbye %ld\n", code);
            exit((int)code);
        }

        /* --- Fork a child for external commands --- */
        pid_t pid = fork();
        if (pid < 0) {
            perror("uc3mshell: fork");
            int j;
            for (j = i; j < num_comandos - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return;
        }

        if (pid == 0) {
            /* Child: restore default SIGCHLD so execvp'd programs behave normally */
            signal(SIGCHLD, SIG_DFL);

            /* Connect stdin to the previous pipe (not for the first command) */
            if (i > 0) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) {
                    perror("uc3mshell: dup2 pipe stdin");
                    exit(-1);
                }
            }

            /* Connect stdout to the next pipe (not for the last command) */
            if (i < num_comandos - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("uc3mshell: dup2 pipe stdout");
                    exit(-1);
                }
            }

            /* Close all pipe ends in the child – they have been dup2'd already */
            int j;
            for (j = 0; j < num_comandos - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Apply file redirections.
             * Stdin redirection applies only to the first command.
             * Stdout and stderr redirections apply only to the last command. */
            if (i == 0 && cmd_filev[i][0] != NULL) {
                int fd = open(cmd_filev[i][0], O_RDONLY);
                if (fd < 0) { perror("uc3mshell: open stdin"); exit(-1); }
                if (dup2(fd, STDIN_FILENO) < 0) { perror("uc3mshell: dup2 stdin"); exit(-1); }
                close(fd);
            }
            if (i == num_comandos - 1) {
                if (cmd_filev[i][1] != NULL) {
                    int fd = open(cmd_filev[i][1], O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0644);
                    if (fd < 0) { perror("uc3mshell: open stdout"); exit(-1); }
                    if (dup2(fd, STDOUT_FILENO) < 0) { perror("uc3mshell: dup2 stdout"); exit(-1); }
                    close(fd);
                }
                if (cmd_filev[i][2] != NULL) {
                    int fd = open(cmd_filev[i][2], O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0644);
                    if (fd < 0) { perror("uc3mshell: open stderr"); exit(-1); }
                    if (dup2(fd, STDERR_FILENO) < 0) { perror("uc3mshell: dup2 stderr"); exit(-1); }
                    close(fd);
                }
            }

            /* Special case: echo "Hello world" must behave like echo Hello world.
             * When the line is tokenised on spaces, "Hello world" becomes two
             * tokens: '"Hello' and 'world"'. We reassemble them and strip quotes. */
            if (strcmp(cmd_argv[i][0], "echo") == 0) {
                int k;
                for (k = 1; cmd_argv[i][k] != NULL; k++) {
                    if (cmd_argv[i][k][0] == '"') {
                        int end = k;
                        while (cmd_argv[i][end] != NULL) {
                            size_t el = strlen(cmd_argv[i][end]);
                            if (el > 0 && cmd_argv[i][end][el - 1] == '"')
                                break;
                            end++;
                        }
                        if (cmd_argv[i][end] != NULL && end > k) {
                            int m;
                            for (m = k + 1; m <= end; m++) {
                                size_t cur_len = strlen(cmd_argv[i][k]);
                                cmd_argv[i][k][cur_len] = ' ';
                                size_t mlen = strlen(cmd_argv[i][m]);
                                memmove(cmd_argv[i][k] + cur_len + 1,
                                        cmd_argv[i][m], mlen + 1);
                                cmd_argv[i][m] = NULL;
                            }
                        }
                        strip_quotes(cmd_argv[i][k]);
                    }
                }
            }

            execvp(cmd_argv[i][0], cmd_argv[i]);
            perror("uc3mshell: execvp");
            exit(-1);
        }

        /* Parent: record the child PID and close pipe ends no longer needed */
        pids[i] = pid;
        if (i > 0) {
            close(pipes[i-1][0]);
            close(pipes[i-1][1]);
        }
    }

    /* Close the last remaining pipe pair (if any) */
    if (num_comandos > 1) {
        close(pipes[num_comandos - 2][0]);
        close(pipes[num_comandos - 2][1]);
    }

    if (background) {
        /* Print the PID of the last forked process as required by the spec */
        for (i = num_comandos - 1; i >= 0; i--) {
            if (pids[i] > 0) {
                printf("%d", pids[i]);
                fflush(stdout);
                break;
            }
        }
        /* Do not wait – the SIGCHLD handler will reap the child when it finishes */
    } else {
        /* Foreground: wait for the last command to finish, reap the rest */
        for (i = 0; i < num_comandos; i++) {
            if (pids[i] > 0) {
                int status;
                if (i == num_comandos - 1)
                    waitpid(pids[i], &status, 0);
                else
                    waitpid(pids[i], &status, WNOHANG);
            }
        }
        /* Clean up any background zombies that finished while we were waiting */
        reap_background();
    }
}

/*
 * procesar_linea: splits a shell line on '|', detects a trailing '&',
 * then calls execute_commands to run the pipeline.
 * Returns the number of pipe-separated commands found.
 */
int procesar_linea(char *linea) {
    char *comandos[max_commands];
    int num_comandos = tokenizar_linea(linea, "|", comandos, max_commands);
    background = 0;

    /* A trailing '&' anywhere in the last segment means background execution */
    if (num_comandos > 0) {
        char *pos = strchr(comandos[num_comandos - 1], '&');
        if (pos != NULL) {
            background = 1;
            *pos = '\0';
        }
    }

    filev[0] = NULL;
    filev[1] = NULL;
    filev[2] = NULL;

    execute_commands(comandos, num_comandos);

    return num_comandos;
}

/*
 * main: opens the script file, validates the mandatory header line, then
 * reads the file character by character dispatching each line to
 * procesar_linea. Empty lines and lines starting with '#' are skipped.
 */
int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return -1;
    }

    /* Install the SIGCHLD handler to reap background children automatically */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("uc3mshell: sigaction");
        return -1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("uc3mshell: cannot open input file");
        return -1;
    }

    /* Read and validate the mandatory header line "## Uc3mshell P2" */
    char header[max_line];
    int  hlen = 0;
    char ch;
    ssize_t n;

    while ((n = read(fd, &ch, 1)) == 1 && ch != '\n' && hlen < max_line - 1)
        header[hlen++] = ch;
    header[hlen] = '\0';

    /* Tolerate Windows-style line endings */
    if (hlen > 0 && header[hlen - 1] == '\r')
        header[--hlen] = '\0';

    if (strcmp(header, "## Uc3mshell P2") != 0) {
        fprintf(stderr, "uc3mshell: invalid script header\n");
        close(fd);
        return -1;
    }

    /* Main loop: read lines character by character */
    char line[max_line];
    int  llen = 0;

    while ((n = read(fd, &ch, 1)) > 0) {
        if (ch == '\n') {
            line[llen] = '\0';

            /* Strip trailing '\r' for Windows-style line endings */
            if (llen > 0 && line[llen - 1] == '\r')
                line[--llen] = '\0';

            /* Skip empty lines and comment lines */
            if (llen == 0 || line[0] == '#') {
                llen = 0;
                continue;
            }

            procesar_linea(line);
            llen = 0;
        } else {
            /* Guard against line buffer overflow */
            if (llen < max_line - 1)
                line[llen++] = ch;
        }
    }

    /* Handle a final line with no trailing newline */
    if (llen > 0) {
        line[llen] = '\0';
        if (line[0] != '#')
            procesar_linea(line);
    }

    close(fd);
    return 0;
}
