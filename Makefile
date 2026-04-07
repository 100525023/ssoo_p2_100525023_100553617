# Executables
TARGETS = uc3mshell mycp

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -O2

# Header files
DEPS = mycalc.h

# Objects
OBJ_SHELL = uc3mshell.o mycalc.o
OBJ_MYCP = mycp.o

# Defined targets
all: $(TARGETS)

uc3mshell: $(OBJ_SHELL)
	$(CC) $(CFLAGS) -o $@ $^

mycp: $(OBJ_MYCP)
	$(CC) $(CFLAGS) -o $@ $^

# Generic rule
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ_SHELL) $(OBJ_MYCP)  uc3mshell mycp

.PHONY: all clean