CC      = gcc
CFLAGS  = -Iinclude -Wall -Wextra -O2
LDFLAGS = -lm
SRC     = $(wildcard src/*.c)
OUT     = plague_sim

all:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: all
	./$(OUT)

clean:
	rm -f $(OUT)
