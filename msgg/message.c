#include "message.h"
#include "client.h"

#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

struct message {
	char *string;
	const char *user;
	PurpleAccount *account;
	unsigned int sequence_number;
	time_t time;

	struct {
		message_t *next;
		message_t *prev;
	} by_time, by_sequence_number;
};

static struct message_local {
	unsigned int sequence_number;

	struct {
		message_t *first, *last;
	} messages_by_time, messages_by_sequence_number;

	unsigned int message_count;

} local = {
	.sequence_number = 1,
	.messages_by_time = { 0, 0 },
	.messages_by_sequence_number = { 0, 0 },
	.message_count = 0,
};

static void msg_destroy(message_t *message) {
	if(message->by_time.next)
		message->by_time.next->by_time.prev = message->by_time.prev;
	else
		local.messages_by_time.last = message->by_time.prev;

	if(message->by_time.prev)
		message->by_time.prev->by_time.next = message->by_time.next;
	else
		local.messages_by_time.first = message->by_time.next;

	if(message->by_sequence_number.next)
		message->by_sequence_number.next->by_sequence_number.prev = message->by_sequence_number.prev;
	else
		local.messages_by_sequence_number.last = message->by_sequence_number.prev;

	if(message->by_sequence_number.prev)
		message->by_sequence_number.prev->by_sequence_number.next = message->by_sequence_number.next;
	else
		local.messages_by_sequence_number.first = message->by_sequence_number.next;


	free(message->string);
	free(message);

}


void msg(time_t time, PurpleAccount *account, const char *user, const char *format, ...) {
	if(local.message_count < 5000) local.message_count++;
	else msg_destroy(local.messages_by_time.first);

	va_list ap;
	message_t *new = malloc(sizeof(*new));
	new->sequence_number = local.sequence_number++;
	new->time = time;
	new->string = 0;
	new->user = g_intern_string(user);
	new->account = account;
	new->by_time.next = 0;
	new->by_time.prev = 0;
	new->by_sequence_number.next = 0;
	new->by_sequence_number.prev = 0;

	va_start(ap, format);
	vasprintf(&new->string, format, ap);
	va_end(ap);

	message_t *iter;
	/* add message to time list */
	for(iter = local.messages_by_time.last; iter; iter = iter->by_time.prev) {
		if(iter->time <= new->time) {
			new->by_time.prev = iter;
			new->by_time.next = iter->by_time.next;
			if(iter->by_time.next) iter->by_time.next->by_time.prev = new;
			else local.messages_by_time.last = new;
			iter->by_time.next = new;
			break;
		}
	}

	if(!iter) {
		if(!local.messages_by_time.first) {
			local.messages_by_time.first = new;
			local.messages_by_time.last = new;
		} else {
			local.messages_by_time.first->by_time.prev = new;
			new->by_time.next = local.messages_by_time.first;
			local.messages_by_time.first = new;
		}
	}

	/* add message to sequence list */
	for(iter = local.messages_by_sequence_number.last; iter; iter = iter->by_sequence_number.prev) {
		if(iter->sequence_number <= new->sequence_number) {
			new->by_sequence_number.prev = iter;
			new->by_sequence_number.next = iter->by_sequence_number.next;
			if(iter->by_sequence_number.next) iter->by_sequence_number.next->by_sequence_number.prev = new;
			else local.messages_by_sequence_number.last = new;
			iter->by_sequence_number.next = new;
			break;
		}
	}
	if(!iter) {
		if(!local.messages_by_sequence_number.first) {
			local.messages_by_sequence_number.first = new;
			local.messages_by_sequence_number.last = new;
		} else {
			local.messages_by_sequence_number.first->by_sequence_number.prev = new;
			new->by_sequence_number.next = local.messages_by_sequence_number.first;
			local.messages_by_sequence_number.first = new;
		}
	}
}

void print_since_sequence_number(int fd, unsigned int sequence_number, client_t *client) {
	message_t *iter;
	for(iter = local.messages_by_sequence_number.last; iter; iter = iter->by_sequence_number.prev)
		if(iter->sequence_number <= sequence_number) break;

	if(iter) iter = iter->by_sequence_number.next;
	else iter = local.messages_by_sequence_number.first;

	while(iter) {
		dprintf(fd, "%s\n", iter->string);
		if(iter->user) client_set_reply_user(client, iter->user);
		iter = iter->by_sequence_number.next;
	}

	client_set_sequence_number(client, local.sequence_number - 1);
}

void print_since_time(int fd, time_t time, client_t *client) {
	message_t *iter;
	for(iter = local.messages_by_time.last; iter; iter = iter->by_time.prev)
		if(iter->time <= time) break;

	if(!iter) iter = local.messages_by_time.first;

	while(iter) {
		dprintf(fd, "%s\n", iter->string);
		if(iter->user && iter->account && strcmp(iter->user, iter->account->username)) client_set_reply_user(client, iter->user);
		iter = iter->by_time.next;
	}

	client_set_sequence_number(client, local.sequence_number - 1);
}
