CFLAGS = -Wall -Wextra -Werror -Icwalk

tod : tod.c cwalk/cwalk.c
	$(CC) $(CFLAGS) -o tod tod.c cwalk/cwalk.c
