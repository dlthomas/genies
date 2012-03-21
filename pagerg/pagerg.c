
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

struct config {
	const char *name;
	const char *genie_dir;
} config = {
	.name = "pager",
	.genie_dir = "/home/dthomas/.genies",
};

struct global {
	FILE *infile;
	const char *socket_name;
	int listen_socket;
	FILE *logfile;
} global = {
	.infile = 0,
	.socket_name = 0,
	.listen_socket = -1,
	.logfile = 0,
};

void configure(int argc, char *argv[]);
void initialize(void);

int main(int argc, char *argv[]) {
	configure(argc, argv);
	initialize();

	char *lineptr = 0;
	size_t len = 0;
	while(!feof(global.infile)) {
		struct sockaddr addr;
		socklen_t socklen = sizeof(addr);
		errno = 0;
		int sock = accept(global.listen_socket, &addr, &socklen);
		if(sock < 0) error(1, errno, "error on socket accept");

		for(int i = 0; i < 8 && !feof(global.infile); i++) {
			int line_length = 0;

			if(0 < (line_length = getline(&lineptr, &len, global.infile)))
				dprintf(sock, "%.*s", line_length, lineptr);
		}

		close(sock);
	}
	
	return 0;
}

void configure(int argc, char *argv[]) {
	for(int i = 1; i < argc; i++) {
		printf("%s: argv[%d]: %s\n", config.name, i, argv[i]);
	}
}

void initialize(void) {
	global.infile = stdin;
	global.logfile = fopen("logfile", "w");

	global.socket_name = tempnam(config.genie_dir, "pager_");
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

	if(listen(global.listen_socket, 10))
		error(1, 0, "failed to listen on socket");

	
	printf("%s\n", config.name);
	printf("%s\n", global.socket_name);

	fflush(stdout);

	daemon(0, 1);

	stderr = global.logfile;
	
}
