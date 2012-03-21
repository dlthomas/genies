
#include <errno.h>
#include <error.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <ncurses.h>
#include <term.h>

#include <sys/socket.h>
#include <sys/un.h>


int main() {
	char *genies = getenv("GENIES");
	const char *cookie = getenv("GENIE_COOKIE");
	char *genie = 0;

	if(!genies) return 0;

	char buffer[1024];

	initscr();
	endwin();
	setupterm(0, 1, 0);

	char *saveptr;

	char name[128] = { 0 };
	char sockname[128] = { 0 };

	int got_data = 0;

	char *magenta = strdup(tiparm(tigetstr("setaf"), 5));
	char *cyan = strdup(tiparm(tigetstr("setaf"), 6));
	char *white = strdup(tiparm(tigetstr("setaf"), 7));

	while((genie = strtok_r(genies, ":", &saveptr))) {
		genies = 0;

		char *value;
		char *saveptr2;

		sockname[0] = 0;

		for(int i = 0; (value = strtok_r(genie, ",", &saveptr2)); i++) {
			genie = 0;

			switch(i) {
				case 0:
					strncpy(name, value, 127);
					break;
				case 1:
					strncpy(sockname, value, 127);
					break;
			}
		}

		if(sockname[0]) {
			int header_printed = 0;
			int sock = socket(AF_UNIX, SOCK_STREAM, 0);
			if(sock < 0)
				error(1, errno, "failed to create socket");

			struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };
			strncpy(addr.sun_path, sockname, sizeof(addr.sun_path)); 

			if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)))
				error(1, errno, "failed to connect socket at %s", sockname);

			dprintf(sock, "poll\n%s\n", cookie);

			int ret;

			while(0 < (ret = read(sock, buffer, sizeof(buffer)))) {
				if(!header_printed) {
					header_printed = 1;
					printf("\n%s~~~%s\n", cyan, white);
				}

				printf("%s%s%s: %.*s", magenta, name, white, ret, buffer);
				got_data = 1;
			}

		}

	}

	if(got_data) printf("\n");

	return 0;
}
