#include "mycalc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * mycalc: internal command that performs basic arithmetic.
 * Syntax: mycalc <num1> <+|-|x|/> <num2>
 * argc and argv follow the same convention as main():
 *   argv[0] = "mycalc", argv[1] = num1, argv[2] = operator, argv[3] = num2
 * Returns 0 on success, -1 on error.
 */
int mycalc(int argc, char **argv) {

    if (argc != 4) {
        fprintf(stderr, "Usage: mycalc <num1> < + | - | x | / > <num2>\n");
        return -1;
    }

    /* Convert both operands to long and reject non-numeric input */
    char *end1, *end2;
    long op1 = strtol(argv[1], &end1, 10);
    long op2 = strtol(argv[3], &end2, 10);

    if (*end1 != '\0') {
        fprintf(stderr, "mycalc: first operand '%s' is not a valid integer\n", argv[1]);
        return -1;
    }
    if (*end2 != '\0') {
        fprintf(stderr, "mycalc: second operand '%s' is not a valid integer\n", argv[3]);
        return -1;
    }

    /* The operator must be a single character */
    if (strlen(argv[2]) != 1) {
        fprintf(stderr, "mycalc: unknown operator '%s'\n", argv[2]);
        return -1;
    }

    char op = argv[2][0];
    long result = 0;

    switch (op) {
        case '+': result = op1 + op2; break;
        case '-': result = op1 - op2; break;
        case 'x': result = op1 * op2; break;
        case '/':
            if (op2 == 0) {
                fprintf(stderr, "mycalc: division by zero\n");
                return -1;
            }
            result = op1 / op2;
            break;
        default:
            fprintf(stderr, "mycalc: unknown operator '%s'\n", argv[2]);
            return -1;
    }

    printf("Operation: %ld %s %ld = %ld\n", op1, argv[2], op2, result);
    return 0;
}
