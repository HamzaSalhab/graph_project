# Operating Systems Graph Simulation

This project implements a multi-milestone graph simulation in C, utilizing Linux system programming, Raylib for GUI, and IPC/Synchronization mechanisms.

## Input Format

All numbers must be non-negative. Vertex indexes must be in the range 0..N-1.

## Milestones Overview & Execution

### Milestone 1 & 2: Graph Parsing and Dijkstra's Algorithm

* **Implementation**: Reads graph data from a text file, builds the graph in memory, and implements Dijkstra's algorithm to calculate and print the shortest path and total weight.
* **Build**: `make milestone1`
* **Run**: `./dijkstra sample_graph.txt`

### Milestone 3: Basic GUI Animation

* **Implementation**: Uses the Raylib library to visualize the directed graph. A single traveler moves along edges based on weights and waits exactly 1 second at each node.
* **Build**: `make milestone3`
* **Run**: `./sim sample_graph.txt`

### Milestone 4: Multi-Processing (fork)

* **Implementation**: Utilizes `fork()` to create autonomous child processes for multiple travelers. The parent process manages GUI rendering while children move independently.
* **Build**: `make milestone4`
* **Run**: `./sim4 sample_graph.txt`

### Milestone 5: Inter-Process Communication (IPC)

* **Implementation**: Establishes IPC using pipes. Child processes calculate their own paths and stream real-time telemetry (current node, next node) to the parent. The parent uses non-blocking reads to update the GUI and print required terminal logs.
* **Build**: `make milestone5`
* **Run**: `./sim5 sample_graph.txt`

### Milestone 6: Node Synchronization (Critical Section)

* **Implementation**: Introduces POSIX Named Semaphores to enforce mutual exclusion at graph nodes. Only one child process can occupy a node at a time. If an intersection is occupied, waiting processes yield, turn **RED** in the GUI, and display a **WAIT** label until the critical section is cleared.
* **Build**: `make milestone6`
* **Run**: `./sim5 sample_graph.txt`

## Clean

* **Command**: `make clean` (Removes all generated object files and binaries).
