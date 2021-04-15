CC=gcc
	
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic

all: proj2

proj2.o: proj2.c
	$(CC) $(CFLAGS) -c proj2.c -o proj2.o

proj2: proj2.o
	$(CC) $(CFLAGS) proj2.o -o proj2 -lpthread

pack:
	zip proj2.zip proj2.c Makefile

clean:
	rm -f proj2
	rm -f *.o
	rm -f proj2.zip
