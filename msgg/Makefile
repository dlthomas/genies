CFLAGS=-Wall -Wextra -Werror -I/usr/include/libpurple -g --std=gnu99 -D_GNU_SOURCE

all: msgg msg_send

msgg: LDLIBS+=-lpurple -lbsd
msgg: CFLAGS+=$(shell pkg-config --cflags glib-2.0)

msg_send: LDLIBS+=$(shell ncurses5-config --libs)

msgg: msgg.o message.o client.o

msgg.o: message.h client.h
message.o: message.h
client.o: client.h

clean:
	rm *.o
