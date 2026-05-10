# Operating Systems Graph Simulation

This project implements milestones 1, 2 and 3 of the operating systems graph project.

## Input format

```text
N M
src dst weight
...
source target
```

All numbers must be non-negative. Vertex indexes must be in the range `0..N-1`.

Example input files included in this repository:

- `sample_graph.txt` - connected graph with shortest path `0 -> 2 -> 5`.
- `sample_same_vertex.txt` - source and target are the same vertex.
- `sample_no_path.txt` - no path exists between source and target.
- `sample_invalid_negative.txt` - invalid input with a negative number.

## Milestone 1

Build:

```sh
make milestone1
```

Run:

```sh
./dijkstra sample_graph.txt
```

The program reads the graph and query from the same file, runs Dijkstra, and prints the shortest path and total weight. If no path exists, it prints:

```text
No path found
```

## Milestone 2

Build:

```sh
make milestone2
```

Run:

```sh
./sim sample_graph.txt
```

The program opens a raylib window and displays the directed weighted graph. Each vertex is drawn as a numbered circle, each directed edge is drawn as an arrow, and edge weights are shown on the screen.

## Milestone 3

Build:

```sh
make milestone3
```

Run:

```sh
./sim sample_graph.txt
```

The simulator computes the shortest path with Dijkstra and shows a moving entity near the source. The Play/Stop button starts and pauses the animation. Movement over an edge with weight `W` is split into `W` equal jumps, each taking 300 milliseconds. At every intermediate vertex, the entity waits for one second. After reaching the destination, the screen displays an arrival message.

## Clean

```sh
make clean
```
