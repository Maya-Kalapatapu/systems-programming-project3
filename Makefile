# Simple Makefile for mysh

CC      = gcc
CFLAGS = -Wall -g -std=c99 -fsanitize=address,undefined
LDFLAGS =

OBJS = mysh_core.o mysh_cmds.o

all: mysh

mysh: $(OBJS)
	$(CC) $(CFLAGS) -o mysh $(OBJS) $(LDFLAGS)

mysh_core.o: mysh_core.c mysh.h
	$(CC) $(CFLAGS) -c mysh_core.c

mysh_cmds.o: mysh_cmds.c mysh.h
	$(CC) $(CFLAGS) -c mysh_cmds.c

clean:
	rm -f mysh $(OBJS)

.PHONY: all clean
