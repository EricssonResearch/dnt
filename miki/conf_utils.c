
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

bool parse_assignment(char *assign, char **key, char **val)
{
    char *eq = strchr(assign, '=');
    if (eq) {
        char *eq2 = strchr(eq+1, '=');
        if (eq2)
            return false;
        *key = assign;
        *val = eq+1;
        *eq = 0;
        return true;
    } else {
        return false;
    }
}

bool parse_fieldname(char *field, char **headername, char **fieldname)
{
    char *dot = strchr(field, '.');
    if (dot) {
        char *dot2 = strchr(dot+1, '.');
        if (dot2)
            return false;
        *headername = field;
        *fieldname = dot+1;
        *dot = 0;
        return true;
    } else {
        return false;
    }
}

char *header_type_from_name(const char *name)
{
    char *under = strchr(name, '_');
    if (under) {
        return strndup(name, under-name);
    } else {
        return strdup(name);
    }
}

int read_boolean(const char *val)
{
    if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0 || strcmp(val, "yes") == 0)
        return 1;
    if (strcmp(val, "0") == 0 || strcmp(val, "false") == 0 || strcmp(val, "no") == 0)
        return 0;
    return -1;
}
