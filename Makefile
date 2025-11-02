CC = gcc
CFLAGS = -Iinclude -Wall -Wextra -O2
SRC = $(wildcard src/*.c)
OUT = plague_sim

all:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT)

run: all
	./$(OUT)

clean:
	rm -f $(OUT)
