#include "graph.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <semaphore.h> // Include standard named semaphore library for synchronization
#include "raylib.h"

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 760
#define NODE_RADIUS 24.0f
#define EDGE_MARGIN 36.0f

// Max limit of concurrent travelers supported by our static UI arrays
#define MAX_TRAVELERS 32

// Traveler status states to control rendering colors in the GUI
typedef enum {
    STATUS_WAITING_FOR_NODE, // Traveler is waiting outside the node (occupied)
    STATUS_INSIDE_NODE,      // Traveler is inside the node (successfully locked)
    STATUS_ARRIVED           // Traveler reached their final destination
} TravelerStatus;

// Updated IPC payload structure to transmit transit telemetry through pipes
typedef struct {
    int traveler_id;
    int current_node;
    int next_node;
    TravelerStatus status;
} PositionUpdate;

// Tracker for parent process UI state models
typedef struct {
    int current_node;
    int next_node;
    TravelerStatus status;
    Vector2 visual_position;
} VisualTraveler;

// --- Raylib Vector Math Helpers ---
static Vector2 add(Vector2 a, Vector2 b) { return (Vector2){a.x + b.x, a.y + b.y}; }
static Vector2 subtract(Vector2 a, Vector2 b) { return (Vector2){a.x - b.x, a.y - b.y}; }
static Vector2 scale(Vector2 v, float factor) { return (Vector2){v.x * factor, v.y * factor}; }
static float length(Vector2 v) { return sqrtf(v.x * v.x + v.y * v.y); }
static Vector2 normalize(Vector2 v) {
    float len = length(v);
    if (len == 0.0f) return (Vector2){0.0f, 0.0f};
    return scale(v, 1.0f / len);
}
static Vector2 lerp_vec(Vector2 a, Vector2 b, float t) { return add(a, scale(subtract(b, a), t)); }

// Arranges graph nodes in a clean circle layout
static void calculate_positions(const Graph *graph, Vector2 *positions) {
    Vector2 center = {WINDOW_WIDTH * 0.5f, WINDOW_HEIGHT * 0.5f};
    float radius = graph->n <= 6 ? 230.0f : 285.0f;

    for (int i = 0; i < graph->n; i++) {
        float angle = -PI / 2.0f + (2.0f * PI * i) / graph->n;
        positions[i] = (Vector2){
                center.x + cosf(angle) * radius,
                center.y + sinf(angle) * radius
        };
    }
}

// Renders directed graph edges with clean visual arrows
static void draw_arrow(Vector2 from, Vector2 to, Color color) {
    Vector2 direction = normalize(subtract(to, from));
    Vector2 start = add(from, scale(direction, EDGE_MARGIN));
    Vector2 end = add(to, scale(direction, -EDGE_MARGIN));
    DrawLineEx(start, end, 2.0f, color);

    Vector2 back = scale(direction, -1.0f);
    Vector2 normal = {-direction.y, direction.x};
    Vector2 left = add(end, add(scale(back, 14.0f), scale(normal, 8.0f)));
    Vector2 right = add(end, add(scale(back, 14.0f), scale(normal, -8.0f)));
    DrawTriangle(end, left, right, color);
}

// Renders the layout framework of the graph
static void draw_graph(const Graph *graph, const Vector2 *positions) {
    for (int i = 0; i < graph->m; i++) {
        Edge edge = graph->edges[i];
        draw_arrow(positions[edge.src], positions[edge.dst], DARKGRAY);

        Vector2 middle = lerp_vec(positions[edge.src], positions[edge.dst], 0.5f);
        DrawCircleV(middle, 15.0f, RAYWHITE);
        DrawText(TextFormat("%d", edge.weight), (int)middle.x - 6, (int)middle.y - 9, 18, DARKGRAY);
    }

    for (int i = 0; i < graph->n; i++) {
        Color fill = LIGHTGRAY;
        if (i == graph->source) fill = GREEN;
        else if (i == graph->target) fill = ORANGE;

        DrawCircleV(positions[i], NODE_RADIUS, fill);
        DrawCircleLines((int)positions[i].x, (int)positions[i].y, NODE_RADIUS, BLACK);
        DrawText(TextFormat("%d", i), (int)positions[i].x - 6, (int)positions[i].y - 10, 22, BLACK);
    }
}

// Autonomous Child Behavior Logic with Node Synchronization
void run_child_behavior(int id, Traveler traveler, const Graph *graph, int write_fd) {
    Path my_path = dijkstra_shortest_path(graph, traveler.source, traveler.dest);

    if (!my_path.found || my_path.length == 0) {
    fprintf(stderr, "[child %d] NO PATH from %d to %d\n",
            id, traveler.source, traveler.dest);

    PositionUpdate update = { id, traveler.source, -1, STATUS_ARRIVED };
    write(write_fd, &update, sizeof(PositionUpdate));

    free_path(&my_path);
    return;

    }

    for (int i = 0; i < my_path.length; i++) {
        int target_node = my_path.vertices[i];

        if (i > 0) {
            int weight = edge_weight_between(graph, my_path.vertices[i-1], target_node);
            usleep(weight * 250000); // Simulate edge transit time

            // 1. Notify Parent: Child reached the entrance of target_node and is now waiting outside
            PositionUpdate wait_update = { id, my_path.vertices[i-1], target_node, STATUS_WAITING_FOR_NODE };
            write(write_fd, &wait_update, sizeof(PositionUpdate));

            // 2. Request locking the node (Critical Section Entry) via Named Semaphore
            char sem_name[64];
            snprintf(sem_name, sizeof(sem_name), "/node_sem_%d", target_node);
            sem_t *sem = sem_open(sem_name, 0);

            sem_wait(sem); // Process will block here automatically if the node is currently occupied
            sem_close(sem);
        }

        // 3. Notify Parent: Lock acquired successfully, child is now inside the node
        PositionUpdate arrive_update;
        arrive_update.traveler_id = id;
        arrive_update.current_node = target_node;
        arrive_update.next_node = (i == my_path.length - 1) ? -1 : my_path.vertices[i + 1];
        arrive_update.status = (i == my_path.length - 1) ? STATUS_ARRIVED : STATUS_INSIDE_NODE;
        write(write_fd, &arrive_update, sizeof(PositionUpdate));

        // 4. Mandatory sleep for 1 entire second inside the node as a Critical Section
        sleep(1);

        // 5. Leaving the node, release the lock for other waiting child processes
        if (i > 0) {
            char sem_name[64];
            snprintf(sem_name, sizeof(sem_name), "/node_sem_%d", target_node);
            sem_t *sem = sem_open(sem_name, 0);
            sem_post(sem); // Release lock
            sem_close(sem);
        }
    }

    free_path(&my_path);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
        return 1;
    }

    Graph graph;
    Traveler *travelers = NULL;
    int num_travelers = 0;
    char error[128];

    if (!load_graph_extended(argv[1], &graph, &travelers, &num_travelers, error, sizeof(error))) {
        fprintf(stderr, "Parsing Error: %s\n", error);
        return 1;
    }

    if (graph.n > 15) {
        fprintf(stderr, "Invalid input: Core engine limits processing to 15 vertices\n");
        free_graph(&graph);
        if (travelers) free(travelers);
        return 1;
    }

    if (num_travelers > MAX_TRAVELERS) {
        num_travelers = MAX_TRAVELERS;
    }

    // Initialize named semaphores for each node to prevent race conditions
    sem_t *node_semaphores[32];
    for (int i = 0; i < graph.n; i++) {
        char sem_name[64];
        snprintf(sem_name, sizeof(sem_name), "/node_sem_%d", i);
        sem_unlink(sem_name); // Unlink any old dead locks stuck in the system memory
        node_semaphores[i] = sem_open(sem_name, O_CREAT, 0644, 1); // Value 1 means node is available
    }

    int parent_read_fds[MAX_TRAVELERS];
    pid_t child_pids[MAX_TRAVELERS];
    VisualTraveler visual_travelers[MAX_TRAVELERS];
    Vector2 node_positions[32];
    calculate_positions(&graph, node_positions);

    for (int i = 0; i < num_travelers; i++) {
        visual_travelers[i].current_node = travelers[i].source;
        visual_travelers[i].next_node = -1;
        visual_travelers[i].status = STATUS_INSIDE_NODE;
        visual_travelers[i].visual_position = node_positions[travelers[i].source];
        child_pids[i] = -1;
    }

    // Forking loop and IPC pipe channel setups
    for (int i = 0; i < num_travelers; i++) {
        int fd[2];
        if (pipe(fd) == -1) { perror("Pipe building failure"); return 1; }

        pid_t pid = fork();
        if (pid < 0) { perror("Fork generation crash"); return 1; }

        if (pid == 0) {
            close(fd[0]);
            run_child_behavior(i, travelers[i], &graph, fd[1]);
            close(fd[1]);
            free_graph(&graph);
            free(travelers);
            exit(0);
        } else {
            close(fd[1]);
            parent_read_fds[i] = fd[0];
            child_pids[i] = pid; // Keep track of child PIDs for parent logging terminal outputs

            int flags = fcntl(parent_read_fds[i], F_GETFL, 0);
            fcntl(parent_read_fds[i], F_SETFL, flags | O_NONBLOCK);
        }
    }

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "OS Multi-Traveler Synchronization Graph Simulation");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        for (int i = 0; i < num_travelers; i++) {
            if (visual_travelers[i].status == STATUS_ARRIVED) continue;

            PositionUpdate update;
            ssize_t bytes = read(parent_read_fds[i], &update, sizeof(PositionUpdate));

            if (bytes == sizeof(PositionUpdate)) {
                visual_travelers[update.traveler_id].current_node = update.current_node;
                visual_travelers[update.traveler_id].next_node = update.next_node;
                visual_travelers[update.traveler_id].status = update.status;

                // Execute strict required terminal logging metrics exclusively from the parent process context
                if (update.status == STATUS_ARRIVED) {
                    printf("[%d] arrived at node %d | DESTINATION\n", child_pids[update.traveler_id], update.current_node);
                } else if (update.status == STATUS_WAITING_FOR_NODE) {
                    printf("[%d] WAITING outside node %d | currently occupied\n", child_pids[update.traveler_id], update.next_node);
                } else {
                    printf("[%d] arrived at node %d | next node: %d\n", child_pids[update.traveler_id], update.current_node, update.next_node);
                }
                fflush(stdout);
            } else if (bytes < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                // Anomaly handling
            }
        }

        // Apply smooth visual position vector interpolation mapping
        for (int i = 0; i < num_travelers; i++) {
            Vector2 target_pos;
            if (visual_travelers[i].status == STATUS_WAITING_FOR_NODE) {
                // If waiting outside, render the passenger circle slightly offset on the edge boundary
                Vector2 src_pos = node_positions[visual_travelers[i].current_node];
                Vector2 dst_pos = node_positions[visual_travelers[i].next_node];
                target_pos = lerp_vec(src_pos, dst_pos, 0.76f);
            } else {
                target_pos = node_positions[visual_travelers[i].current_node];
            }
            visual_travelers[i].visual_position = lerp_vec(visual_travelers[i].visual_position, target_pos, 0.08f);
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawText("Milestone 6: Node Synchronization (POSIX Semaphores)", 150, 25, 26, MAROON);
        draw_graph(&graph, node_positions);

        // Blit and color passenger tracking nodes dynamically based on lock state
        for (int i = 0; i < num_travelers; i++) {
            Color runner_color;
            if (visual_travelers[i].status == STATUS_WAITING_FOR_NODE) {
                runner_color = RED; // Red color clearly reflects queueing state outside a blocked node
            } else {
                runner_color = GetColor(0x2080A0FF + (i * 0x3A2C1F00));
            }

            DrawCircleV(visual_travelers[i].visual_position, 10.0f, runner_color);
            DrawCircleLines((int)visual_travelers[i].visual_position.x, (int)visual_travelers[i].visual_position.y, 10.0f, BLACK);

            if (visual_travelers[i].status == STATUS_WAITING_FOR_NODE) {
                DrawText(TextFormat("T%d (WAIT)", i), (int)visual_travelers[i].visual_position.x - 22, (int)visual_travelers[i].visual_position.y - 24, 14, RED);
            } else {
                DrawText(TextFormat("T%d", i), (int)visual_travelers[i].visual_position.x - 10, (int)visual_travelers[i].visual_position.y - 24, 14, runner_color);
            }
        }

        EndDrawing();
    }

    CloseWindow();

    for (int i = 0; i < num_travelers; i++) {
        close(parent_read_fds[i]);
    }

    while (wait(NULL) > 0 );

    // Clean up resources and close semaphores properly on runtime teardown
    for (int i = 0; i < graph.n; i++) {
        char sem_name[64];
        snprintf(sem_name, sizeof(sem_name), "/node_sem_%d", i);
        sem_close(node_semaphores[i]);
        sem_unlink(sem_name);
    }

    free_graph(&graph);
    free(travelers);
    return 0;
}
