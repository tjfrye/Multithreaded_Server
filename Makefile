LIBS = -lpthread

appserver : main.c Bank.c
	gcc -o appserver main.c Bank.c $(LIBS) -w

