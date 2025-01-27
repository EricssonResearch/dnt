# example makefile for compiling the library

CFLAGS += -g -std=c99
CFLAGS += -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wvla -Wc++-compat -Wdouble-promotion -Wfloat-conversion
CFLAGS += -Werror
CFLAGS += -O2

OBJECTS = hashmap.o inifile.o json.o
HEADERS = hashmap.h inifile.h json.h parserutils.h

LIBNAME = libparser

default: $(LIBNAME).a $(LIBNAME).so

%.o : %.c $(HEADERS) Makefile
	$(CC) $(CFLAGS) -fPIC -c $<

$(LIBNAME).a: $(OBJECTS)
	$(AR) rcs $@ $^

$(LIBNAME).so: $(OBJECTS)
	$(CC) -shared -fPIC $^ -o $@

.PHONY: clean
clean:
	rm -f $(OBJECTS) $(LIBNAME).a $(LIBNAME).so
