#include "mycalc.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

/* How long a line can be and how many commands we support */
const int max_line       = 1024;
const int max_commands   = 10;
#define   max_redirections 3
#define   max_args         15
#define   MAX_PIPE_CMDS    10

/* These get populated by procesar_linea and read by execute_commands */
char *argvv[max_args];         /* the current command's arguments */
char *filev[max_redirections]; /* files to redirect: [0]=in [1]=out [2]=err */
int   background = 0;          /* should we run this in the background? */

/* Stores the args and redirections for each command in a pipeline */
static char *cmd_argv[MAX_PIPE_CMDS][max_args];
static char *cmd_filev[MAX_PIPE_CMDS][max_redirections];

/*
 * Splits 'linea' by 'delim' and fills 'tokens' with the pieces.
 * Returns how many tokens we found.
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
 * If a string is wrapped in double quotes, pull them off.
 * This way echo "Hello world" works the same as echo Hello world.
 */
static void strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

/*
 * Do a quick non-blocking sweep for any background children that have
 * finished, so they don't pile up as zombies.
 */
static void reap_background(void) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

/*
 * Triggered whenever a child process dies. Cleans up immediately so
 * we never end up with zombie processes hanging around.
 */
static void sigchld_handler(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}

/*
 * Parses one segment of a pipeline (the stuff between two pipes).
 * Pulls out any redirections (<, >, !>) and their filenames, then
 * leaves cmd_argv with just the actual command and its arguments.
 * Returns the number of arguments.
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
    /* Chop off the redirection tokens so execvp never sees them */
    if (first_red != -1) {
        for (k = first_red; cmd_argv[idx][k] != NULL; k++)
            cmd_argv[idx][k] = NULL;
        argc = first_red;
    }

    return argc;
}

/*
 * The heart of the shell — runs a pipeline of num_comandos commands.
 *
 * For a single command we just fork once. For a pipeline we set up N-1
 * pipes and fork one child per command. Each child reads from the pipe
 * on its left and writes to the pipe on its right.
 *
 * stdin redirection only affects the first command; stdout and stderr
 * redirections only affect the last one.
 *
 * Background jobs get their PID printed and we move on immediately.
 * Foreground jobs block until the last command finishes.
 */
static void execute_commands(char *comandos[], int num_comandos) {
    int i;
    int pipes[MAX_PIPE_CMDS - 1][2];
    pid_t pids[MAX_PIPE_CMDS];

    /* Break each pipe segment into a clean argv + redirection list */
    for (i = 0; i < num_comandos; i++)
        parse_one_command(i, comandos[i]);

    /* Build all the pipes we need upfront */
    for (i = 0; i < num_comandos - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("uc3mshell: pipe");
            return;
        }
    }

    for (i = 0; i < num_comandos; i++) {

        /* Nothing to run here, skip it */
        if (cmd_argv[i][0] == NULL) {
            if (i < num_comandos - 1) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
            pids[i] = -1;
            continue;
        }

        /* Built-in: mycalc — handle it directly, no fork needed */
        if (strcmp(cmd_argv[i][0], "mycalc") == 0) {
            int ac = 0;
            while (cmd_argv[i][ac] != NULL) ac++;
            mycalc(ac, cmd_argv[i]);
            pids[i] = -1;
            if (i > 0)                { close(pipes[i-1][0]); close(pipes[i-1][1]); }
            if (i < num_comandos - 1) { close(pipes[i][0]);   close(pipes[i][1]);   }
            continue;
        }

        /* Built-in: exit — validate the code, wait for background jobs, then leave */
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

        /* Everything else: fork a child and let execvp take over */
        pid_t pid = fork();
        if (pid < 0) {
            perror("uc3mshell: fork");
            /* Something went wrong — close remaining pipes and bail */
            int j;
            for (j = i; j < num_comandos - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return;
        }

        if (pid == 0) {
            /* Child process starts here */

            /* Let execvp'd programs handle SIGCHLD their own way */
            signal(SIGCHLD, SIG_DFL);

            /* Wire up stdin to the previous pipe (skip for the first command) */
            if (i > 0) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) {
                    perror("uc3mshell: dup2 pipe stdin");
                    exit(-1);
                }
            }

            /* Wire up stdout to the next pipe (skip for the last command) */
            if (i < num_comandos - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("uc3mshell: dup2 pipe stdout");
                    exit(-1);
                }
            }

            /* Close every pipe end — we've already dup2'd the ones we need */
            int j;
            for (j = 0; j < num_comandos - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Hook up any file redirections.
             * Only the first command can redirect stdin.
             * Only the last command can redirect stdout/stderr. */
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

            /* echo "Hello world" quirk: because we split on spaces, the quoted
             * string arrives as two tokens '"Hello' and 'world"'. We glue them
             * back together and strip the quotes before calling execvp. */
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

        /* Back in the parent — save the PID and close the pipe we no longer need */
        pids[i] = pid;
        if (i > 0) {
            close(pipes[i-1][0]);
            close(pipes[i-1][1]);
        }
    }

    /* Clean up the last open pipe pair */
    if (num_comandos > 1) {
        close(pipes[num_comandos - 2][0]);
        close(pipes[num_comandos - 2][1]);
    }

    if (background) {
        /* Print the PID of the last process and let it run on its own */
        for (i = num_comandos - 1; i >= 0; i--) {
            if (pids[i] > 0) {
                printf("%d", pids[i]);
                fflush(stdout);
                break;
            }
        }
        /* SIGCHLD will take care of reaping it when it's done */
    } else {
        /* Foreground: block on the last command, do a quick wait on the rest */
        for (i = 0; i < num_comandos; i++) {
            if (pids[i] > 0) {
                int status;
                if (i == num_comandos - 1)
                    waitpid(pids[i], &status, 0);
                else
                    waitpid(pids[i], &status, WNOHANG);
            }
        }
        /* Sweep up any background zombies that snuck in while we were waiting */
        reap_background();
    }
}

/*
 * Splits the line on '|' to find pipeline segments, checks for a
 * trailing '&' (background), then hands everything off to execute_commands.
 * Returns how many commands were in the pipeline.
 */
int procesar_linea(char *linea) {
    char *comandos[max_commands];
    int num_comandos = tokenizar_linea(linea, "|", comandos, max_commands);
    background = 0;

    /* A '&' anywhere in the last segment means "run in the background" */
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
 * Entry point. Opens the script file, checks that it starts with the
 * right header, then reads it line by line and runs each one.
 * Blank lines and lines starting with '#' are ignored.
 */
int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return -1;
    }

    /* Catch child exits automatically so background jobs clean themselves up */
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

    /* Every valid script must start with "## Uc3mshell P2" */
    char header[max_line];
    int  hlen = 0;
    char ch;
    ssize_t n;

    while ((n = read(fd, &ch, 1)) == 1 && ch != '\n' && hlen < max_line - 1)
        header[hlen++] = ch;
    header[hlen] = '\0';

    /* Trim the '\r' in case this came from a Windows machine */
    if (hlen > 0 && header[hlen - 1] == '\r')
        header[--hlen] = '\0';

    if (strcmp(header, "## Uc3mshell P2") != 0) {
        fprintf(stderr, "uc3mshell: invalid script header\n");
        close(fd);
        return -1;
    }

    /* Read the rest of the file one character at a time, building up lines */
    char line[max_line];
    int  llen = 0;

    while ((n = read(fd, &ch, 1)) > 0) {
        if (ch == '\n') {
            line[llen] = '\0';

            /* Again, handle Windows line endings gracefully */
            if (llen > 0 && line[llen - 1] == '\r')
                line[--llen] = '\0';

            /* Skip blank lines and comments */
            if (llen == 0 || line[0] == '#') {
                llen = 0;
                continue;
            }

            procesar_linea(line);
            llen = 0;
        } else {
            /* Don't overflow the buffer */
            if (llen < max_line - 1)
                line[llen++] = ch;
        }
    }

    /* Run the last line even if there's no newline at the end of the file */
    if (llen > 0) {
        line[llen] = '\0';
        if (line[0] != '#')
            procesar_linea(line);
    }

    close(fd);
    return 0;
}
