CC     = gcc
CFLAGS = -Wall -g -Iinclude
LIBS   = -lpthread

all: pipeline warehouse scheduler

pipeline: src/problem1.c src/helpers.c
	$(CC) $(CFLAGS) src/problem1.c src/helpers.c -o pipeline $(LIBS)

warehouse: src/problem2.c src/helpers.c
	$(CC) $(CFLAGS) src/problem2.c src/helpers.c -o warehouse $(LIBS)

scheduler: src/problem3.c
	$(CC) $(CFLAGS) src/problem3.c -o scheduler

clean:
	rm -f pipeline warehouse scheduler
