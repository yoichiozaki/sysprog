CLIENT = mydhcpc
SERVER = mydhcpd
CC = gcc

all: mydhcpc mydhcpd

mydhcpc: mydhcpc.o
	$(CC) -o mydhcpc mydhcpc.o

mydhcpc.o: client.c
	$(CC) -o mydhcpc.o -c client.c

mydhcpd: mydhcpd.o
	$(CC) -o mydhcpd mydhcpd.o

mydhcpd.o: server.c
	$(CC) -o mydhcpd.o -c server.c

clean:
	rm *.o $(CLIENT) $(SERVER)
