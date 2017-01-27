all:
	gcc -c -g IntegralGen.c
	gcc -c -g Client.c
	gcc IntegralGen.o -o IntegralGen -lm -lmatheval
	gcc Client.o -o Client

	rm *.o
