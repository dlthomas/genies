#include "client.h"

#include <search.h>
#include <string.h>
#include <malloc.h>

struct client {
	char *id;
	unsigned int sequence_number;
	const char *reply_user;
};

struct client_local {
	void *clients_by_id;
} local = {
	.clients_by_id = 0,
};

static int compare_client_by_id(const void *v1, const void *v2) {
	const client_t *c1 = v1, *c2 = v2;
	return strcmp(c1->id, c2->id);
}

client_t *client_find(const char *id) {
	client_t client = {
		.id = (char*)id,
	};

	client_t **found = tsearch(&client, &local.clients_by_id, compare_client_by_id);

	if(*found == &client) {
		client_t *new = malloc(sizeof(*new));
		new->id = strdup(id);
		new->sequence_number = 0;
		*found = new;
	}

	return *found;
}

unsigned int client_get_sequence_number(client_t *client) {
	return client->sequence_number;
}

void client_set_sequence_number(client_t *client, unsigned int sequence_number) {
	client->sequence_number = sequence_number;
}

const char *client_get_reply_user(client_t *client) {
	return client->reply_user;
}

void client_set_reply_user(client_t *client, const char *reply_user) {
	client->reply_user = reply_user;
}
