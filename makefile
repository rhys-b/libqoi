.PHONY: all install

all: libqoi.so libqoi.a

libqoi.so: qoi.c qoi.h
	gcc -o libqoi.so -shared -fPIC qoi.c

libqoi.a: qoi.c qoi.h
	gcc -c -o qoi.o qoi.c
	ar rcs libqoi.a qoi.o
	rm qoi.o

test: tester.c libqoi.a libqoi.so
	gcc -o test tester.c -L . `pkg-config --cflags gdk-pixbuf-2.0` -l:libqoi.a `pkg-config --libs gdk-pixbuf-2.0`
	mkdir qoi_images
	./test

install: libqoi.so
	cp libqoi.so /usr/local/lib/libqoi.so
