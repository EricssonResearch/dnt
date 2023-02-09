#TODO we should be using cmake

CC = gcc
#CC = clang
CFLAGS += -g -Og -std=gnu99
CFLAGS += -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings
CFLAGS += -Werror

#TODO gnu libc doesn't need this
LFLAGS += -lpthread

OBJECTS = main.o action.o delay.o header.o if_eth.o interface.o packet.o parsetree.o pipeline.o utils.o
HEADERS = action.h delay.h header.h if_eth.h interface.h packet.h parsetree.h pipeline.h utils.h

default: r2dthree

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

r2dthree: $(OBJECTS)
	$(CC) $^ -o $@ $(LFLAGS)

.PHONY: clean
clean:
	rm -f *.o
	rm -f r2dthree

