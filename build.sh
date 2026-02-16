#!/bin/bash
cc -Wall -Wextra -Werror --std=c99 -I. -Icwalk -o tod tod.c cwalk/cwalk.c
