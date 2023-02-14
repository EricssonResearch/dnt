#TODO we should be using cmake

CC = gcc
#CC = clang
CFLAGS += -g -Og -std=gnu99
CFLAGS += -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings
CFLAGS += -Werror

#TODO gnu libc doesn't need this
LFLAGS += -lpthread

OBJECTS = main.o action.o delay.o header.o interface.o packet.o parsetree.o pipeline.o protocol.o utils.o
OBJECTS += if_eth.o
OBJECTS += conf_actions.o conf_packet.o conf_utils.o
HEADERS = action.h delay.h header.h interface.h packet.h parsetree.h pipeline.h protocol.h utils.h
HEADERS += if_eth.h
HEADERS += conf_actions.h conf_packet.h conf_utils.h

default: r2dthree

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

r2dthree: $(OBJECTS)
	$(CC) $^ -o $@ $(LFLAGS)

.PHONY: clean
clean:
	rm -f *.o
	rm -f r2dthree

