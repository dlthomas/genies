#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

struct config {
	const char *user;
	int edit;
	int msg_start;
	const char *name;
	const char *socket_name;
} config = {
	.user = "!reply",
	.edit = 0,
	.name = "msgg",
	.socket_name = 0,
};

struct global {
	const char *socket_name;
} global = {
	.socket_name = "test.sock",
};


void configure(int argc, char *argv[]);
void initialize(void);

int main(int argc, char *argv[]) {
	configure(argc, argv);
	initialize();

	char message[1024], buffer[1024];
	unsigned int i = 0;

	for(int arg = config.msg_start; arg < argc; arg++) {
		if(i) message[i++] = ' ';

		for(unsigned int j = 0; argv[arg][j]; j++) {
			message[i] = argv[arg][j];
			i++;
			if(i > sizeof(message) - 1)
				error(1, ENOMEM, "no space in buffer");
		}
	}

	message[i] = 0;

	if(config.edit) {
		char filename[] = "/tmp/msg.XXXXXX";
		int tmpfd = mkstemp(filename);
		struct stat stat_pre, stat_post;
		const char *editor = getenv("EDITOR");

		write(tmpfd, message, i);

		snprintf(buffer, sizeof(buffer), "%s %s", editor, filename);
		fstat(tmpfd, &stat_pre);
		system(buffer);
		fstat(tmpfd, &stat_post);
		if(stat_pre.st_mtime == stat_post.st_mtime) {
			return 0;
		}

		lseek(tmpfd, 0, SEEK_SET);

		int bytes = read(tmpfd, message, sizeof(message) - 1);
		if(bytes < 0)
			error(1, errno, "failed to read file");
		message[bytes] = 0;

		unlink(filename);
		
	} else if(config.msg_start == argc) {
		int bytes = read(0, message, sizeof(message) - 1);
		if(bytes < 0) error(1, errno, "failed to read message");
		message[bytes] = 0;
	}

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = { 0 },
	};

	strncpy(addr.sun_path, global.socket_name, sizeof(addr.sun_path));

	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock < 0) error(1, errno, "failed to create socket");

	if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)))
		error(1, errno, "failed to connect to socket %s", global.socket_name);

	if(0 > dprintf(sock, "message\n%s\n%s\n%s", getenv("GENIE_COOKIE"), config.user, message))
		error(1, errno, "failed to send message");

	if(close(sock))
		error(1, errno, "error sending message\n");

	return 0;
}

void configure(int argc, char *argv[]) {
	config.msg_start = argc;
	for(int arg = 1; arg < argc; arg++) {
		if(!strcmp("--user", argv[arg])) {
			config.user = argv[++arg];
		} else if(!strcmp("--edit", argv[arg])) {
			config.edit = 1;
		} else if(!strcmp("--name", argv[arg])) {
			config.name = argv[++arg];
		} else {
			config.msg_start = arg;
			break;
		}
	}
}

void initialize(void) {
	if(config.socket_name) {
		global.socket_name = strdup(config.socket_name);
	} else {
		global.socket_name = 0;

		char *genies = getenv("GENIES");
		char *saveptr, *genie;

		while((genie = strtok_r(genies, ":", &saveptr))) {
			genies = 0;
			const char *value;
			char *saveptr2;
			int match = 0;
			for(int i = 0; (value = strtok_r(genie, ",", &saveptr2)); i++) {
				genie = 0;
				switch(i) {
					case 0:
						if(!strcmp(config.name, value)) match = 1;
						break;
					case 1:
						if(match) global.socket_name = strdup(value);
						break;
				}
			}
		}
	}

	if(!global.socket_name)
		error(1, 0, "unable to determine socket name");
}
