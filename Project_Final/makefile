all:
	gcc -c -g FileShareServer.c
	gcc -c -g Client.c
	gcc -c -g restart.c
	gcc FileShareServer.o restart.o -o ./Server/FileShareServer -lpthread
	gcc Client.o restart.o -o Client -lpthread

	rm *.o
