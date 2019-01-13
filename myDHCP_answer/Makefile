CLIENT = mydhcpc
SERVER = mydhcpd
CC = gcc

all: mydhcpc mydhcpd

mydhcpc: mydhcpc.o
	        $(CC) -o mydhcpc mydhcpc.o

mydhcpc.o: mydhcpc.c
	        $(CC) -o mydhcpc.o -c mydhcpc.c

mydhcpd: mydhcps.o
	        $(CC) -o mydhcpd mydhcps.o

mydhcp.o: mydhcps.c
	        $(CC) -o mydhcps.o -c mydhcps.c

clean:
	        rm *.o $(CLIENT) $(SERVER)

