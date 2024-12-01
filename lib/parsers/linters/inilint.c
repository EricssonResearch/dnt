#include "../inifile.h"

#include <stdio.h>
#include <stdlib.h>

// gcc -Wall -g inilint.c ../hashmap.c ../inifile.c -o inilint -lm

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s some.ini [other.ini]\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int i=1; i<argc; i++) {
        printf("loading '%s'\n", argv[i]);

        char *error = NULL;
        struct IniSection *sec = read_inifile(argv[i], &error);

        if (sec == NULL) {
            printf("\033[31mINI is invalid\033[0m '%s'\n", error);
            free(error);
        } else {
            printf("\033[32mINI is valid\033[0m\n");
        }

        delete_inisection(sec);
    }
    return EXIT_SUCCESS;
}
