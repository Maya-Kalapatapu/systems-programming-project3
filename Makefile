CC       = gcc
CFLAGS   = -Wall -g -std=c99 -fsanitize=address,undefined
LDFLAGS  =

TARGET       = mysh
TEST_TARGET  = test_cmds

SRCS = mysh_core.c mysh_cmds.c
OBJS = $(SRCS:.c=.o)

TEST_SRCS = test_cmds.c
TEST_OBJS = $(TEST_SRCS:.c=.o)


all: $(TARGET)

# Default build
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# Test build
$(TEST_TARGET): mysh_cmds.o $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ mysh_cmds.o $(TEST_OBJS)

%.o: %.c mysh.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJS) $(TEST_OBJS) out_*

.PHONY: all clean
