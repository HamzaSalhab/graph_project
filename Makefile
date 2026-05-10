CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null)
ifeq ($(strip $(RAYLIB_LIBS)),)
RAYLIB_LIBS = -lraylib -lm
endif

.PHONY: milestone1 milestone2 milestone3 clean

milestone1: dijkstra

milestone2: sim

milestone3: sim

dijkstra: dijkstra_main.o graph.o
	$(CC) $(CFLAGS) -o dijkstra dijkstra_main.o graph.o

sim: sim.o graph.o
	$(CC) $(CFLAGS) -o sim sim.o graph.o $(RAYLIB_LIBS)

dijkstra_main.o: dijkstra_main.c graph.h
	$(CC) $(CFLAGS) -c dijkstra_main.c

graph.o: graph.c graph.h
	$(CC) $(CFLAGS) -c graph.c

sim.o: sim.c graph.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c sim.c

clean:
	rm -f dijkstra sim *.o
