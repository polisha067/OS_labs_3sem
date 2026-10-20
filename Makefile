CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS = -pthread -lm

.PHONY: all clean clean-lab3

all: lab3 benchmark

lab3: main.c
	$(CC) $(CFLAGS) main.c -o lab3 $(LDLIBS)

benchmark: benchmark.c lab3
	$(CC) $(CFLAGS) benchmark.c -o benchmark -lm

clean: clean-lab3

clean-lab3:
	rm -f lab3 benchmark
