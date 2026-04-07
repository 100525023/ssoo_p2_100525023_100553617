#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* Buffer size used for read/write transfers */
#define BUF_SIZE 4096

/*
 * mycp: copies a source file to a destination file.
 * Syntax: mycp <source_file> <destination_file>
 * Uses only POSIX system calls (open, read, write, close).
 * If the destination exists it is overwritten (O_TRUNC).
 * Returns 0 on success, exits with -1 on error.
 */
int main(int argc, char **argv) {

    /* Validate argument count */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source_file> <destination_file>\n", argv[0]);
        return -1;
    }

    /* Open the source file for reading */
    int fd_src = open(argv[1], O_RDONLY);
    if (fd_src < 0) {
        perror("mycp: cannot open source file");
        return -1;
    }

    /* Open / create the destination file for writing.
     * O_CREAT  – create if it does not exist.
     * O_WRONLY – write only.
     * O_TRUNC  – truncate if it already exists.
     * 0644     – rw-r--r-- permissions for the new file. */
    int fd_dst = open(argv[2],
                      O_WRONLY | O_CREAT | O_TRUNC,
                      (mode_t)0644);
    if (fd_dst < 0) {
        perror("mycp: cannot open destination file");
        (void)close(fd_src);
        return -1;
    }

    /* Copy data in chunks */
    char buf[BUF_SIZE];
    ssize_t bytes_read;

    while ((bytes_read = read(fd_src, buf, BUF_SIZE)) > 0) {
        ssize_t bytes_written = 0;
        ssize_t total_written = 0;

        /* Handle partial writes */
        while (total_written < bytes_read) {
            bytes_written = write(fd_dst,
                                  buf + total_written,
                                  (size_t)(bytes_read - total_written));
            if (bytes_written < 0) {
                perror("mycp: write error");
                (void)close(fd_src);
                (void)close(fd_dst);
                return -1;
            }
            total_written += bytes_written;
        }
    }

    /* Check that the final read did not return an error */
    if (bytes_read < 0) {
        perror("mycp: read error");
        (void)close(fd_src);
        (void)close(fd_dst);
        return -1;
    }

    /* Close both file descriptors */
    if (close(fd_src) < 0) {
        perror("mycp: error closing source file");
        (void)close(fd_dst);
        return -1;
    }
    if (close(fd_dst) < 0) {
        perror("mycp: error closing destination file");
        return -1;
    }

    /* Verify that the destination file now exists */
    struct stat st;
    if (stat(argv[2], &st) < 0) {
        perror("mycp: destination file not found after copy");
        return -1;
    }

    return 0;
}
