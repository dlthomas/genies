CFLAGS=-Wall -Wextra -Werror -g --std=gnu99 -D_GNU_SOURCE
LDLIBS+=$(shell ncurses5-config --libs)

all: genie_poll

