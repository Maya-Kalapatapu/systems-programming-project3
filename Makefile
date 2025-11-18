CC       = gcc
CFLAGS   = -Wall -g -std=c99 -fsanitize=address,undefined
LDFLAGS  =

TARGET       = mysh
TEST_TARGET  = test

SRCS = mysh_core.c mysh_cmds.c
OBJS = mysh_core.o mysh_cmds.o

TEST_OBJS = mysh_core_test.o mysh_cmds_test.o test.o

# Default build
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

mysh_core.o: mysh_core.c mysh.h
	$(CC) $(CFLAGS) -c -o $@ $<

mysh_cmds.o: mysh_cmds.c mysh.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Test objects (compiled with -DTESTING)
mysh_core_test.o: mysh_core.c mysh.h
	$(CC) $(CFLAGS) -DTESTING -c -o $@ $<

mysh_cmds_test.o: mysh_cmds.c mysh.h
	$(CC) $(CFLAGS) -DTESTING -c -o $@ $<

test.o: test.c mysh.h
	$(CC) $(CFLAGS) -DTESTING -c -o $@ $<


$(TEST_TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJS) $(TEST_OBJS) out_* test_ls.txt

.PHONY: all clean
