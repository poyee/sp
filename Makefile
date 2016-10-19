all: server.c
	gcc -g server.c -o write_server
	gcc -g server.c -D READ_SERVER -o read_server

clean:
	rm -f read_server write_server
