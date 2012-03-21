#ifndef CLIENT_H
#define CLIENT_H

typedef struct client client_t;

client_t *client_find(const char *);

unsigned int client_get_sequence_number(client_t *);
void client_set_sequence_number(client_t *, unsigned int);

const char *client_get_reply_user(client_t *);
void client_set_reply_user(client_t *, const char *);

#endif
