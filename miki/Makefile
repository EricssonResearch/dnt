#TODO we should be using cmake

CC = gcc
#CC = clang
CFLAGS += -g -Og -std=gnu99
CFLAGS += -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings
CFLAGS += -Werror
CFLAGS += -Iinifile

#TODO gnu libc doesn't need this
LFLAGS += -lpthread

OBJECTS = main.o action.o delay.o header.o interface.o packet.o parsetree.o pipeline.o protocol.o utils.o
OBJECTS += if_eth.o
OBJECTS += seq_gen.o seq_recov.o
OBJECTS += configfile.o conf_actions.o conf_interface.o conf_object.o conf_packet.o conf_streams.o conf_utils.o
OBJECTS += inifile/hashmap.o inifile/inifile.o

HEADERS = action.h delay.h header.h interface.h packet.h parsetree.h pipeline.h protocol.h utils.h
HEADERS += if_eth.h
HEADERS += seq_gen.h seq_recov.h
HEADERS += configfile.h conf_actions.h conf_interface.h conf_object.h conf_packet.h conf_streams.h conf_utils.h
HEADERS += inifile/hashmap.h inifile/inifile.h inifile/iniutils.h

EXE = r2dthree

default: $(EXE)

inifile/%.o : inifile/%.c $(HEADERS)
	cd inifile && $(CC) $(CFLAGS) -c $(<F)

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

$(EXE): $(OBJECTS)
	$(CC) $^ -o $@ $(LFLAGS)

.PHONY: clean
clean:
	rm -f *.o
	rm -f inifile/*.o
	rm -f $(EXE)

