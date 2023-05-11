CC=gcc
CFLAGS=-Wall -Werror -pedantic -g -I.
LDFLAGS=-Lbuild -lmujit
INCLUDES=backend.h
LIB=build/libmujit.a
LIBOBJECTS=build/x86_64.o

.PHONY: clean

all: $(LIB) build/helloworld build/ack

build/helloworld: build/helloworld.o $(INCLUDES) $(LIB)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

build/ack: build/ack.o $(INCLUDES) $(LIB)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

$(LIB): $(LIBOBJECTS) | build
	ar r $@ $(LIBOBJECTS)

clean:
	rm -rf build

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o build/$*.o

build:
	mkdir -p build
