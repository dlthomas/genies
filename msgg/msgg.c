
#include "purple.h"

#include <glib.h>

#include <errno.h>
#include <error.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <bsd/readpassphrase.h>

#include <sys/socket.h>
#include <sys/un.h>

#include "message.h"
#include "client.h"
#include "config.h"

#define CUSTOM_PLUGIN_PATH	 ""
#define PLUGIN_SAVE_PREF	 "/purple/msgg/plugins/saved"
#define UI_ID			"msgg"

/*
 * The following eventloop functions are used in both pidgin and purple-text. If your
 * application uses glib mainloop, you can safely use this verbatim.
 */
#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

struct config {
	const char *genie_dir;
	const char *username;
} config = {
	.username = "davidleothomas@gmail.com",
	.genie_dir = "/home/dthomas/.genies",
};

struct global {
	PurpleAccount *account;
	int listen_socket;
	char *socket_name;
	GMainLoop *loop;
	FILE *logfile;
	char password[1024];
} global = {
	.account = 0,
	.listen_socket = -1,
	.socket_name = 0,
	.loop = 0,
	.logfile = 0,
	.password = { 0 },
};

typedef struct _PurpleGLibIOClosure {
	PurpleInputFunction function;
	guint result;
	gpointer data;
} PurpleGLibIOClosure;

static void purple_glib_io_destroy(gpointer data) {
	g_free(data);
}

static gboolean purple_glib_io_invoke(GIOChannel *source, GIOCondition condition, gpointer data) {
	PurpleGLibIOClosure *closure = data;
	PurpleInputCondition purple_cond = 0;

	if (condition & PURPLE_GLIB_READ_COND)
		purple_cond |= PURPLE_INPUT_READ;
	if (condition & PURPLE_GLIB_WRITE_COND)
		purple_cond |= PURPLE_INPUT_WRITE;

	closure->function(closure->data, g_io_channel_unix_get_fd(source), purple_cond);

	return TRUE;
}

static guint glib_input_add(gint fd, PurpleInputCondition condition, PurpleInputFunction function, gpointer data) {
	PurpleGLibIOClosure *closure = g_new0(PurpleGLibIOClosure, 1);
	GIOChannel *channel;
	GIOCondition cond = 0;

	closure->function = function;
	closure->data = data;

	if (condition & PURPLE_INPUT_READ)
		cond |= PURPLE_GLIB_READ_COND;
	if (condition & PURPLE_INPUT_WRITE)
		cond |= PURPLE_GLIB_WRITE_COND;

	channel = g_io_channel_unix_new(fd);
	closure->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond, purple_glib_io_invoke, closure, purple_glib_io_destroy);

	g_io_channel_unref(channel);
	return closure->result;
}

static PurpleEventLoopUiOps glib_eventloops = {
	g_timeout_add,
	g_source_remove,
	glib_input_add,
	g_source_remove,
	NULL,
#if GLIB_CHECK_VERSION(2,14,0)
	g_timeout_add_seconds,
#else
	NULL,
#endif

	/* padding */
	NULL,
	NULL,
	NULL
};
/*** End of the eventloop functions. ***/


/*** Conversation uiops ***/

static void msgg_write_conv(PurpleConversation *conv, const char *who, const char *alias __attribute__((unused)), const char *message, PurpleMessageFlags flags __attribute__((unused)),
		time_t mtime) {
	const char *name;
	if (who && *who)
		name = who;
	else
		name = NULL;

	msg(mtime, conv->account, name, "(%s) %s %s: %s", purple_conversation_get_name(conv), purple_utf8_strftime("(%H:%M:%S)", localtime(&mtime)), name, message);
}

void *msgg_notify_email(PurpleConnection *gc __attribute__((unused)),
		const char *subject, const char *from,
		const char *to __attribute__((unused)),
		const char *url __attribute__((unused))) {

	msg(time(0), 0, 0, "~~ email ~~ %s: %s", from, subject);
	return 0;
}

static PurpleNotifyUiOps msgg_notify_uiops = {
	NULL, //  *(*notify_message)(PurpleNotifyMsgType type, const char *title,
			// const char *primary, const char *secondary);

	msgg_notify_email, //  *(*notify_email)(PurpleConnection *gc,
			// const char *subject, const char *from,
			// const char *to, const char *url);

	NULL, //  *(*notify_emails)(PurpleConnection *gc,
			// size_t count, gboolean detailed,
			// const char **subjects, const char **froms,
			// const char **tos, const char **urls);

	NULL, //  *(*notify_formatted)(const char *title, const char *primary,
			// const char *secondary, const char *text);

	NULL, //  *(*notify_searchresults)(PurpleConnection *gc, const char *title,
			// const char *primary, const char *secondary,
			// PurpleNotifySearchResults *results, gpointer user_data);

	NULL, //  (*notify_searchresults_new_rows)(PurpleConnection *gc,
			// PurpleNotifySearchResults *results,
			// void *data);

	NULL, //  *(*notify_userinfo)(PurpleConnection *gc, const char *who,
			// PurpleNotifyUserInfo *user_info);

	NULL, //  *(*notify_uri)(const char *uri);

	NULL, //  (*close_notify)(PurpleNotifyType type, void *ui_handle);

	NULL, //  (*_purple_reserved1)(void);
	NULL, //  (*_purple_reserved2)(void);
	NULL, //  (*_purple_reserved3)(void);
	NULL, //  (*_purple_reserved4)(void);

};

static PurpleConversationUiOps msgg_conv_uiops = {
	NULL,				/* create_conversation  */
	NULL,				/* destroy_conversation */
	NULL,				/* write_chat		 */
	NULL,				/* write_im		 */
	msgg_write_conv,		/* write_conv		 */
	NULL,				/* chat_add_users	 */
	NULL,				/* chat_rename_user	 */
	NULL,				/* chat_remove_users	*/
	NULL,				/* chat_update_user	 */
	NULL,				/* present		  */
	NULL,				/* has_focus		*/
	NULL,				/* custom_smiley_add	*/
	NULL,				/* custom_smiley_write  */
	NULL,				/* custom_smiley_close  */
	NULL,				/* send_confirm	   */
	NULL,
	NULL,
	NULL,
	NULL
};

static void msgg_ui_init(void) {
	/*
	 * This should initialize the UI components for all the modules. Here we
	 * just initialize the UI for conversations.
	 */
	purple_conversations_set_ui_ops(&msgg_conv_uiops);
}

static PurpleCoreUiOps msgg_core_uiops = {
	NULL,
	NULL,
	msgg_ui_init,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

static void init_libpurple(void) {
	/* We do not want any debugging for now to keep the noise to a minimum. */
	purple_debug_set_enabled(FALSE);

	purple_core_set_ui_ops(&msgg_core_uiops);
	purple_notify_set_ui_ops(&msgg_notify_uiops);

	/* Set the uiops for the eventloop. If your client is glib-based, you can safely copy this verbatim. */
	purple_eventloop_set_ui_ops(&glib_eventloops);

	/*
	 * Set path to search for plugins. The core (libpurple) takes care of loading the
	 * core-plugins, which includes the protocol-plugins. So it is not essential to add
	 * any path here, but it might be desired, especially for ui-specific plugins.
	 */

	purple_plugins_add_search_path(CUSTOM_PLUGIN_PATH);

	/*
	 * Now that all the essential stuff has been set, let's try to init the core. It's
	 * necessary to provide a non-NULL name for the current ui to the core. This name
	 * is used by stuff that depends on this ui, for example the ui-specific plugins.
	 */

	if (!purple_core_init(UI_ID)) {
		/* Initializing the core failed. Terminate. */
		fprintf(stderr,
				"libpurple initialization failed. Dumping core.\n"
				"Please report this!\n");
		abort();
	}

	/* Create and load the buddylist. */
	purple_set_blist(purple_blist_new());
	purple_blist_load();

	/* Load the preferences. */
	purple_prefs_load();

	/*
	 * Load the desired plugins. The client should save the list of loaded plugins in
	 * the preferences using purple_plugins_save_loaded(PLUGIN_SAVE_PREF)
	 */

	purple_plugins_load_saved(PLUGIN_SAVE_PREF);

	/* Load the pounces. */
	purple_pounces_load();
}

static void signed_on(PurpleConnection *gc, gpointer null __attribute__((unused))) {
	PurpleAccount *account = purple_connection_get_account(gc);
	msg(time(0), 0, "Account connected: %s %s", account->username, account->protocol_id);
}

static void connect_to_signals_for_demonstration_purposes_only(void) {
	static int handle;
	purple_signal_connect(purple_connections_get_handle(), "signed-on", &handle,
				PURPLE_CALLBACK(signed_on), NULL);
}

void send_message(const char *message, const char *user, int user_len) {
	GList *conv_list = purple_get_conversations();
	while(conv_list) {
		PurpleConversation *conv = g_list_nth_data(conv_list, 0);
		if(!strncmp(user, purple_conversation_get_name(conv), user_len))
			break;
		conv_list = g_list_next(conv_list);
	}

	PurpleConversation *conv = 0;
	if(conv_list) conv = g_list_nth_data(conv_list, 0);
	else conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, global.account, user);

	if(conv) {
		PurpleConversationType type = purple_conversation_get_type(conv);
		switch(type) {
			case PURPLE_CONV_TYPE_IM:
				purple_conv_im_send(PURPLE_CONV_IM(conv), message);
				break;
			default: break; // TODO
		}
	}
}

void msgg_accept(gpointer user_data __attribute__((unused)), gint fd, PurpleInputCondition condition) {
	purple_idle_touch();
	fprintf(global.logfile, "accept\n");
	fflush(global.logfile);

	if(condition == PURPLE_INPUT_READ) {
		int socket = accept(fd, 0, 0);
		if(socket < 0)
			error(0, errno, "failed to accept socket");

		char buffer[1024];
		int bytes = read(socket, buffer, sizeof(buffer) - 1);
		if(bytes < 0) {
			error(0, errno, "error reading from socket");
			return;
		}



		error(0, 0, "read %d bytes\n", bytes);
		buffer[bytes] = 0;

		if(bytes > 0) { 
			int name_start = 0, name_end = 0,
			    id_start = 0, id_end = 0,
			    message_start = 0;
			errno = 0;
			if(0 > sscanf(buffer, "message\n%n%*s%n\n%n%*s%n\n%n",
						&id_start, &id_end,
						&name_start, &name_end,
						&message_start)) {
				error(0, errno, "failed to read request: %s", buffer);
			}

			if(message_start) {
				buffer[name_end] = 0;
				buffer[id_end] = 0;

				client_t *client = client_find(buffer + id_start);

				const char *message = buffer + message_start;

				const char *name = 0;
				int name_len = 0;

				if(!strcmp("!reply", buffer + name_start)) {
					name = client_get_reply_user(client);
					name_len = strlen(name);
				} else {
					name = buffer + name_start;
					name_len = name_end - name_start;
					client_set_reply_user(client, name);
				}

				send_message(message, name, name_len);
			}
		}

		{ 
			int id_start = 0, id_end = 0;
			if(0 > sscanf(buffer, "poll\n%n%*s%n", &id_start, &id_end))
				error(0, errno, "failed to read request: %.*s", id_end, buffer);

			if(id_end) {
				buffer[id_end] = 0;
				client_t *client = client_find(buffer + id_start);
				print_since_sequence_number(socket, client_get_sequence_number(client), client);
			}
		}

		{ 
			int request_end = 0;
			if(0 > sscanf(buffer, "buddies\n%n", &request_end))
				error(0, errno, "failed to read request");

			if(request_end) {
				GSList *blist = purple_blist_get_buddies();
				while(blist) {
					PurpleBlistNode *bnode = PURPLE_BLIST_NODE(g_slist_nth_data(blist, 0));
					if(PURPLE_BLIST_NODE_IS_BUDDY(bnode)) {
						PurpleBuddy *buddy = PURPLE_BUDDY(bnode);
						if(purple_presence_is_online(buddy->presence))
							dprintf(socket, "%s\n", buddy->name);
					}
					blist = g_slist_next(blist);
				}
			}
		}

		close(socket);
	}
}


int initialize(void);

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
	/*
	 * libpurple's built-in DNS resolution forks processes to perform
	 * blocking lookups without blocking the main process.  It does not
	 * handle SIGCHLD itself, so if the UI does not you quickly get an army
	 * of zombie subprocesses marching around.
	 */

	initialize();

	fprintf(global.logfile, "looping\n");
	fflush(global.logfile);
	g_main_loop_run(global.loop);

	unlink(global.socket_name);

	return 0;
}

void init_genie(void);

int initialize(void) {
	char prompt[64] = { 0 };
	snprintf(prompt, sizeof(prompt), "password for %.48s: ", config.username);
	readpassphrase(prompt, global.password, sizeof(global.password), RPP_SEVENBIT);

	global.logfile = fopen("logfile", "w");
	fprintf(global.logfile, "initializing");
	fflush(global.logfile);

	GList *iter;
	GList *names = NULL;
	const char *prpl;
	PurpleSavedStatus *status;

	signal(SIGCHLD, SIG_IGN);

	fprintf(global.logfile, "initializing genie\n");
	fflush(global.logfile);

	init_genie();

	global.loop = g_main_loop_new(NULL, FALSE);

	fprintf(global.logfile, "initializing libpurple\n");
	fflush(global.logfile);
	init_libpurple();

	iter = purple_plugins_get_protocols();
	while(iter) {
		PurplePlugin *plugin = iter->data;
		PurplePluginInfo *info = plugin->info;
		if (info && info->name) {
			names = g_list_append(names, info->id);
		}
		iter = iter->next;
	}

	prpl = g_list_nth_data(names, 11 /* XMPP */);

	/* Create the account */
	global.account = purple_account_new(config.username, prpl);

	/* Get the password for the account */
	purple_account_set_password(global.account, global.password);

	/* It's necessary to enable the account first. */
	purple_account_set_enabled(global.account, UI_ID, TRUE);

	/* Now, to connect the account(s), create a status and activate it. */
	status = purple_savedstatus_new(NULL, PURPLE_STATUS_AVAILABLE);
	purple_savedstatus_activate(status);

	connect_to_signals_for_demonstration_purposes_only();

	purple_input_add(global.listen_socket, PURPLE_INPUT_READ, msgg_accept, 0);

	fprintf(global.logfile, "initialized\n");
	fflush(global.logfile);
	return 0;
}

void init_genie(void) {
	global.socket_name = tempnam(config.genie_dir, "msg");
	unlink(global.socket_name);

	global.listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if(global.listen_socket < 0)
		error(1, 0, "failed to create socket");

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};

	strcpy(addr.sun_path, global.socket_name);
	
	if(bind(global.listen_socket, (struct sockaddr *)&addr, sizeof(addr)))
		error(1, 0, "failed to bind socket");

	if(listen(global.listen_socket, 3))
		error(1, 0, "failed to listen on socket");

	
	printf("msgg\n");
	printf("%s\n", global.socket_name);

	fflush(stdout);

	daemon(0, 0);

	stderr = global.logfile;
}

