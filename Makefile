
main: main.c
	gcc main.c -o zuuftp

install:
	cp zuuftp /usr/local/bin
	chmod ugo+x /usr/local/bin/zuuftp

