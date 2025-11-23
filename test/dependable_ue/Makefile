CFLAGS += -g -std=gnu99

#TODO clang ignores -Wmissing-declarations
CFLAGS += -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wvla -Wc++-compat
CFLAGS += -Werror
CFLAGS += -I../../lib/parsers
CFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3

LFLAGS += -lm
#TODO gnu libc doesn't need this
LFLAGS += -lpthread

OBJECTS = telnet_control.o
OBJECTS += ../../lib/parsers/hashmap.o ../../lib/parsers/json.o

HEADERS += ../../lib/parsers/json.h

EXE = telnet_control

default: release

__rel:
	$(eval CFLAGS += -O2)

release: __rel $(EXE)

#support both gdb and uftrace
__deb:
	$(eval CFLAGS += -O0 -pg -ggdb)

debug: __deb $(EXE)

%.o : %.c $(HEADERS) Makefile
	$(CC) $(CFLAGS) -c $<

$(EXE): $(OBJECTS)
	$(CC) $^ -o $@ $(LFLAGS)

.PHONY: clean
clean:
	rm -f *.o
	rm -f lib/parsers/*.o
	rm -f $(EXE)
