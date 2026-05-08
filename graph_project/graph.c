#include "graph.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static bool read_non_negative_int(FILE *file, int *value, char *error, int error_size) {
    if (fscanf(file, "%d", value) != 1) {
        snprintf(error, error_size, "Invalid input format");
        return false;
    }

    if (*value < 0) {
        snprintf(error, error_size, "Invalid input: negative numbers are not allowed");
        return false;
    }

    return true;
}

bool load_graph(const char *file_name, Graph *graph, char *error, int error_size) {
    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
        snprintf(error, error_size, "Could not open file");
        return false;
    }

    graph->n = 0;
    graph->m = 0;
    graph->edges = NULL;
    graph->source = 0;
    graph->target = 0;

    if (!read_non_negative_int(file, &graph->n, error, error_size) ||
        !read_non_negative_int(file, &graph->m, error, error_size)) {
        fclose(file);
        return false;
    }

    if (graph->n == 0) {
        snprintf(error, error_size, "Invalid input: graph must contain at least one vertex");
        fclose(file);
        return false;
    }

    graph->edges = malloc((size_t)graph->m * sizeof(Edge));
    if (graph->m > 0 && graph->edges == NULL) {
        snprintf(error, error_size, "Memory allocation failed");
        fclose(file);
        return false;
    }

    for (int i = 0; i < graph->m; i++) {
        Edge edge;
        if (!read_non_negative_int(file, &edge.src, error, error_size) ||
            !read_non_negative_int(file, &edge.dst, error, error_size) ||
            !read_non_negative_int(file, &edge.weight, error, error_size)) {
            free_graph(graph);
            fclose(file);
            return false;
        }

        if (edge.src >= graph->n || edge.dst >= graph->n) {
            snprintf(error, error_size, "Invalid input: vertex index out of range");
            free_graph(graph);
            fclose(file);
            return false;
        }

        graph->edges[i] = edge;
    }

    if (!read_non_negative_int(file, &graph->source, error, error_size) ||
        !read_non_negative_int(file, &graph->target, error, error_size)) {
        free_graph(graph);
        fclose(file);
        return false;
    }

    if (graph->source >= graph->n || graph->target >= graph->n) {
        snprintf(error, error_size, "Invalid input: source or target out of range");
        free_graph(graph);
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

void free_graph(Graph *graph) {
    free(graph->edges);
    graph->edges = NULL;
    graph->n = 0;
    graph->m = 0;
    graph->source = 0;
    graph->target = 0;
}

Path dijkstra_shortest_path(const Graph *graph, int source, int target) {
    Path path = {0};

    int *dist = malloc((size_t)graph->n * sizeof(int));
    int *prev = malloc((size_t)graph->n * sizeof(int));
    bool *visited = calloc((size_t)graph->n, sizeof(bool));
    if (dist == NULL || prev == NULL || visited == NULL) {
        free(dist);
        free(prev);
        free(visited);
        return path;
    }

    for (int i = 0; i < graph->n; i++) {
        dist[i] = INT_MAX;
        prev[i] = -1;
    }
    dist[source] = 0;

    for (int step = 0; step < graph->n; step++) {
        int current = -1;
        int best_distance = INT_MAX;

        for (int v = 0; v < graph->n; v++) {
            if (!visited[v] && dist[v] < best_distance) {
                best_distance = dist[v];
                current = v;
            }
        }

        if (current == -1) {
            break;
        }
        if (current == target) {
            break;
        }

        visited[current] = true;

        for (int i = 0; i < graph->m; i++) {
            Edge edge = graph->edges[i];
            if (edge.src != current || dist[current] == INT_MAX) {
                continue;
            }

            if (edge.weight <= INT_MAX - dist[current] &&
                dist[current] + edge.weight < dist[edge.dst]) {
                dist[edge.dst] = dist[current] + edge.weight;
                prev[edge.dst] = current;
            }
        }
    }

    if (dist[target] == INT_MAX) {
        free(dist);
        free(prev);
        free(visited);
        return path;
    }

    int count = 1;
    for (int v = target; v != source; v = prev[v]) {
        if (v == -1) {
            free(dist);
            free(prev);
            free(visited);
            return path;
        }
        count++;
    }

    path.vertices = malloc((size_t)count * sizeof(int));
    if (path.vertices == NULL) {
        free(dist);
        free(prev);
        free(visited);
        return path;
    }

    int index = count - 1;
    for (int v = target; v != -1; v = prev[v]) {
        path.vertices[index--] = v;
        if (v == source) {
            break;
        }
    }

    path.length = count;
    path.total_weight = dist[target];
    path.found = true;

    free(dist);
    free(prev);
    free(visited);
    return path;
}

void free_path(Path *path) {
    free(path->vertices);
    path->vertices = NULL;
    path->length = 0;
    path->total_weight = 0;
    path->found = false;
}

int edge_weight_between(const Graph *graph, int src, int dst) {
    for (int i = 0; i < graph->m; i++) {
        if (graph->edges[i].src == src && graph->edges[i].dst == dst) {
            return graph->edges[i].weight;
        }
    }
    return -1;
}
