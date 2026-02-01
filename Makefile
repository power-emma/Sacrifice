CC = gcc
CFLAGS = -fdiagnostics-color=always -g -Wall -Wextra
LDFLAGS = -lncurses
SRCS = main.c rules.c boardchecks.c evaluation.c recursion.c puzzles.c input.c output.c tui.c
OBJS = $(SRCS:.c=.o)
TARGET = main

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean run
