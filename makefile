all:
	gcc -Wall -Werror -lpthread -c tls.c -o tls.o

clean:
	rm test-threads
