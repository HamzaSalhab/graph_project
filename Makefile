CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS := $(shell pkg-config --libs raylib 2>/dev/null)
ifeq ($(strip $(RAYLIB_LIBS)),)
RAYLIB_LIBS = -lraylib -lm
endif

.PHONY: milestone1 milestone2 milestone3 milestone4 milestone5 milestone6 milestone7 clean

milestone1: dijkstra

milestone2: sim

milestone3: sim

milestone4: graph.o sim4.o
	$(CC) $(CFLAGS) -o sim sim4.o graph.o $(RAYLIB_LIBS)

milestone5: graph.o sim5.o
	$(CC) $(CFLAGS) -o sim sim5.o graph.o $(RAYLIB_LIBS)

milestone6: graph.o sim5.o
	$(CC) $(CFLAGS) -o sim sim5.o graph.o $(RAYLIB_LIBS)

milestone7: graph.o milestone7.o
	$(CC) $(CFLAGS) -o sim milestone7.o graph.o $(RAYLIB_LIBS)

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

sim4.o: sim4.c graph.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c sim4.c

sim5.o: sim5.c graph.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c sim5.c

milestone7.o: milestone7.c graph.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c milestone7.c

clean:
	rm -f dijkstra sim *.o
