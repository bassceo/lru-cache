CC = gcc
CFLAGS = -Wall -O2 -fPIC
LDFLAGS = -shared

all: liblab2.so lab2_test load_tester

liblab2.so: lib/lab2.o
	$(CC) $(LDFLAGS) -o liblab2.so lib/lab2.o

lib/lab2.o: lib/lab2.c lib/lab2.h
	$(CC) $(CFLAGS) -c lib/lab2.c -o lib/lab2.o

lab2_test: test/lab2_test.c liblab2.so
	$(CC) -Wall -O2 test/lab2_test.c -L. -llab2 -o lab2_test

load_tester: test/load_tester.cpp
	g++ test/load_tester.cpp -o load_tester -O2 -std=c++17

load_tester_with_user_cache: test/load_tester_with_user_cache.cpp liblab2.so
	g++ test/load_tester_with_user_cache.cpp -o load_tester_with_user_cache -O2 -std=c++17 -L. -llab2

clean:
	rm -f lib/*.o *.so lab2_test load_tester load_tester_with_user_cache
