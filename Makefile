CC = clang
CFLAGS = -Og -g -std=c11 -Wall -pedantic -fsanitize=address
LDFLAGS = -Og -g -std=c11 -Wall -pedantic -fsanitize=address
CREATED_OBJS = mlpt.o libmlpt.a

all: libmlpt.a

libmlpt.a: mlpt.o
	ar -rcs libmlpt.a mlpt.o

mlpt.o: mlpt.c mlpt.h config.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(CREATED_OBJS)
