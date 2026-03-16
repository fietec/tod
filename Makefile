CFLAGS = -Wall -Wextra -Werror -Icwalk -I.

tod : tod.c cwalk/cwalk.c
	$(CC) $(CFLAGS) -o tod tod.c cwalk/cwalk.c
