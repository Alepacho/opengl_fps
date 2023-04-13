CC       = clang++
LDFLAGS  = \
	-framework OpenGL \
	-lGLEW \
	`sdl2-config --libs` `sdl2-config --cflags`
CPPFLAGS = -std=gnu++0x -pedantic -O2 -W -Wall -g

SRC = \
	src/main.cpp

.PHONY: demo

all: demo

demo:
	$(CC) $(SRC) $(CPPFLAGS) $(LDFLAGS) -o bin/demo