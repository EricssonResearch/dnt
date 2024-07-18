#include "../json.h"

#include <stdio.h>
#include <stdlib.h>

// compile: gcc -Wall -g -I../inifile jslint.c ../json.c ../inifile/hashmap.c -o jslint -lm

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s some.json [other.json]\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int i=1; i<argc; i++) {
        printf("loading '%s'\n", argv[i]);

        FILE *jsf = fopen(argv[i], "r");
        if (jsf == NULL) {
            fprintf(stderr, "could not open '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
        if (fseek(jsf, 0, SEEK_END)) {
            fprintf(stderr, "could not seek in '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
        int len = ftell(jsf);
        if (fseek(jsf, 0, SEEK_SET)) {
            fprintf(stderr, "could not seek in '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
        char *jsb = (char*)malloc(len*sizeof(char));
        if (jsb == NULL) {
            fprintf(stderr, "memory allocation error\n");
            return EXIT_FAILURE;
        }
        int rlen = fread(jsb, 1, len, jsf);
        if (rlen != len) {
            fprintf(stderr, "from '%s' wanted to read %d actually read %d\n",
                    argv[i], len, rlen);
            return EXIT_FAILURE;
        }
        fclose(jsf);

        struct JsonValue *js = json_parse(jsb, len);
        if (js == NULL) {
            printf("\033[31mJSON is invalid\033[0m\n");
        } else {
            printf("\033[32mJSON is valid\033[0m\n");
        }
        json_delete(js);

    }

    return EXIT_SUCCESS;
}
