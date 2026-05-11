#include "graph.h"
#include <stdio.h>

static void print_path(const Path *path) {
    for (int i = 0; i < path->length; i++) {
        if (i > 0) {
            printf(" -> ");
        }
        printf("%d", path->vertices[i]);
    }
    printf("\n%d\n", path->total_weight);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
        return 1;
    }
    Graph graph;
    char error[128];
    if (!load_graph(argv[1], &graph, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    Path path = dijkstra_shortest_path(&graph, graph.source, graph.target);
    if (!path.found) {
        printf("No path found\n");
        free_graph(&graph);
        return 0;
    }

    print_path(&path);
    free_path(&path);
    free_graph(&graph);
    return 0;
}
