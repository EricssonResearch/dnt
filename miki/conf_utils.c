
#include "conf_utils.h"

#include <string.h>
#include <ctype.h>

void foreach_stages(char *line, foreach_callback *cb, void *userdata)
{
    char *l = line;
    while (1) {
        char *sc = strchr(l, ';');
        if (sc) {
            *sc = 0;
            if (!cb(l, userdata)) return;
            l = sc + 1;
            if (*l == 0) return;
        } else {
            cb(l, userdata);
            return;
        }
    }
}

void foreach_tokens(char *stage, foreach_callback *cb, void *userdata)
{
    char *t = stage;
    while (*t && isspace(*t)) t++; // skip leading whitespace
    if (*t == 0) return;
    while (1) {
        char *sp = t;
        while (*sp && !isspace(*sp)) sp++;
        if (*sp) {
            *sp = 0;
            if (!cb(t, userdata)) return;
            t = sp + 1;
            while (*t && isspace(*t)) t++; // skip leading whitespace
            if (*t == 0) return;
        } else {
            cb(t, userdata);
            return;
        }
    }
}
