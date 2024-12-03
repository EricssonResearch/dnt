#include "../json.h"

#include <stdio.h>
#include <stdlib.h>

// compile: gcc -Wall -g jslint.c ../json.c ../hashmap.c -o jslint -lm

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

        char *error = NULL;
        struct JsonValue *js = json_parse(jsb, len, &error);
        free(jsb);
        if (js == NULL) {
            printf("\033[31mJSON is invalid\033[0m '%s'\n", error);
            free(error);
        } else {
            printf("\033[32mJSON is valid\033[0m\n");
        }

        // for fuzz testing
        if (js) {
            unsigned len_fuzz;
            char *string_fuzz = json_serialize(js, &len_fuzz);
            struct JsonValue *js_fuzz = json_parse(string_fuzz, len_fuzz, &error);
            free(string_fuzz);
            string_fuzz = json_serialize_pretty(js_fuzz, &len_fuzz, 5);
            json_delete(js_fuzz);
            js_fuzz = json_parse(string_fuzz, len_fuzz, &error);
            json_delete(js_fuzz);
            free(string_fuzz);
        }

        json_delete(js);
    }

    return EXIT_SUCCESS;
}
