CFLAGS=-Wall -Wextra -Werror -I/usr/include/libpurple -I/usr/include/glib-2.0 -I/usr/lib/i386-linux-gnu/glib-2.0/include -g --std=gnu99 -D_GNU_SOURCE

all: msgg msg_send

msgg: LDLIBS+=-lpurple -lbsd
msg_send: LDLIBS+=$(shell ncurses5-config --libs)

msgg: msgg.o message.o client.o

msgg.o: message.h client.h
message.o: message.h
client.o: client.h

clean:
	rm *.o
