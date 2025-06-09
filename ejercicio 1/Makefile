# Makefile para ejercicio 1

CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=ejercicio1

all: $(TARGET)

$(TARGET): ejercicio1.o
	$(CC) $(CFLAGS) -o $(TARGET) ejercicio1.o

ejercicio1.o: ejercicio1.c
	$(CC) $(CFLAGS) -c ejercicio1.c

clean:
	rm -f *.o $(TARGET)