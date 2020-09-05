CC := gcc
CFLAGS := -Wall

all: server client

server: multi_server.c
	$(CC) $(CFLAGS) -o server multi_server.c

client: multi_client.c
	$(CC) $(CFLAGS) -o client multi_client.c

clean:
	rm -f server client
