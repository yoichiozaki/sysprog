O = mysh
CC = gcc

mysh: mysh.o command.o utils.o builtins.o
	$(CC) -o $(O) mysh.o command.o utils.o builtins.o

clean:
	rm *.o $(O)

mysh.o: mysh.c
	$(CC) -o mysh.o -c mysh.c

command.o: command.c command.h
	$(CC) -o command.o -c command.c

utils.o: utils.c utils.h
	$(CC) -o utils.o -c utils.c

builtins.o: builtins.c builtins.h
	$(CC) -o builtins.o -c builtins.c