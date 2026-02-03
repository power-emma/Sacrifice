CC = gcc
CFLAGS = -fdiagnostics-color=always -g -Wall -Wextra
LDFLAGS = -lncurses -lpthread -lm
SRCS = main.c rules.c boardchecks.c evaluation.c recursion.c puzzles.c input.c output.c tui.c rewards.c training.c gamestate.c recursion_threadsafe.c puzzles_mt.c
OBJS = $(SRCS:.c=.o)
TARGET = main
TEST_OBJS = test_accuracy.o rules.o boardchecks.o evaluation.o recursion.o puzzles.o input.o output.o tui.o rewards.o training.o gamestate.o recursion_threadsafe.o puzzles_mt.o

all: $(TARGET)

test_puzzles_mt: test_puzzles_mt.o rules.o boardchecks.o evaluation.o recursion.o puzzles.o output.o rewards.o gamestate.o recursion_threadsafe.o puzzles_mt.o tui_stubs.o
	$(CC) $(CFLAGS) -o test_puzzles_mt test_puzzles_mt.o rules.o boardchecks.o evaluation.o recursion.o puzzles.o output.o rewards.o gamestate.o recursion_threadsafe.o puzzles_mt.o tui_stubs.o $(LDFLAGS)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

test_accuracy: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o test_accuracy $(TEST_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

test: test_accuracy
	./test_accuracy

clean:
	rm -f $(OBJS) $(TARGET) test_accuracy.o test_accuracy

.PHONY: all clean run test

