#ifndef MESSAGE_H
#define MESSAGE_H

#include <time.h>
#include <libpurple/account.h>

typedef struct message message_t;
typedef struct client client_t;

void msg(time_t time, PurpleAccount *account, const char *user, const char *format, ...);

void print_since_sequence_number(int fd, unsigned int, client_t *client);
void print_since_time(int fd, time_t, client_t *client);

#endif
