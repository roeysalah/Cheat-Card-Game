all: servergame.out clientgame.out

servergame.out:	server.c
	gcc -o servergame server.c -lpthread

clientgame.out:	client.c
	gcc -o clientgame client.c -lpthread

clean:
	rm -f  *o client
	rm -f  *o server
