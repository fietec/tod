#!/bin/bash
cc -Wall -Wextra -Werror -I. -Icwalk -o tod tod.c cwalk/cwalk.c
