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

bool load_graph(const char *file_name, Graph *graph, char *error, int error_size);
void free_graph(Graph *graph);

Path dijkstra_shortest_path(const Graph *graph, int source, int target);
void free_path(Path *path);

int edge_weight_between(const Graph *graph, int src, int dst);

#endif
