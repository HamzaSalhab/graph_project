#ifndef GRAPH_H
#define GRAPH_H

#include <stdbool.h>

typedef struct {
    int src;
    int dst;
    int weight;
} Edge;

typedef struct {
    int n;
    int m;
    Edge *edges;
    int source;
    int target;
} Graph;

typedef struct {
    int *vertices;
    int length;
    int total_weight;
    bool found;
} Path;

// New data structure to store the source and destination for each traveler
typedef struct {
    int source;
    int dest;
} Traveler;

// Updated parsing function (or a new one dedicated to advanced milestones)
// This function uses pointers to return the array of travelers and their count
bool load_graph_extended(const char *file_name,
                         Graph *graph,
                         Traveler **travelers,
                         int *num_travelers,
                         char *error,
                         int error_size);

bool load_graph(const char *file_name, Graph *graph, char *error, int error_size);
void free_graph(Graph *graph);

Path dijkstra_shortest_path(const Graph *graph, int source, int target);
void free_path(Path *path);

int edge_weight_between(const Graph *graph, int src, int dst);

#endif
