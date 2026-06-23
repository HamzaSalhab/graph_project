#define _DEFAULT_SOURCE
#include "graph.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <semaphore.h> // Include standard named semaphore library for synchronization
#include <string.h>
#include <sys/mman.h>
#include "raylib.h"

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 760
#define NODE_RADIUS 24.0f
#define EDGE_MARGIN 36.0f

// Max limit of concurrent travelers supported by our static UI arrays
#define MAX_TRAVELERS 32
#define MAX_NODES 32

typedef enum {
    SCHD_FCFS,
    SCHD_SJF
} SchedulerType;

typedef struct {
    int traveler_id;
    int remaining_work;
    unsigned long arrival_seq;
} WaitRequest;

typedef struct {
    char mutex_name[80];
    int occupied_by[MAX_NODES];
    int queue_count[MAX_NODES];
    WaitRequest queues[MAX_NODES][MAX_TRAVELERS];
    unsigned long next_seq;
} SharedScheduler;

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

// Calculates the remaining route work from the current path index to the destination.
// SJF uses this value to prefer the traveler with the shortest remaining trip.
static int remaining_work_from_index(const Graph *graph, const Path *path, int index) {
    int total = 0;
    for (int j = index; j < path->length - 1; j++) {
        int weight = edge_weight_between(graph, path->vertices[j], path->vertices[j + 1]);
        if (weight > 0) total += weight;
    }
    return total;
}
static void lock_scheduler(SharedScheduler *shared);
static void unlock_scheduler(SharedScheduler *shared);
static void enqueue_wait_request(SharedScheduler *shared, int node, int traveler_id, int remaining_work) {
    lock_scheduler(shared);

    int count = shared->queue_count[node];
    if (count < MAX_TRAVELERS) {
        shared->queues[node][count].traveler_id = traveler_id;
        shared->queues[node][count].remaining_work = remaining_work;
        shared->queues[node][count].arrival_seq = shared->next_seq++;
        shared->queue_count[node]++;
    }

    unlock_scheduler(shared);
}

static void release_node(SharedScheduler *shared, int node, int traveler_id) {
    lock_scheduler(shared);
    if (shared->occupied_by[node] == traveler_id) {
        shared->occupied_by[node] = -1;
    }
    unlock_scheduler(shared);
}

static void build_grant_sem_name(char *buffer, size_t size, const char *prefix, int traveler_id) {
    snprintf(buffer, size, "%s_%d", prefix, traveler_id);
}

static sem_t *open_scheduler_mutex(SharedScheduler *shared) {
    sem_t *mutex = sem_open(shared->mutex_name, 0);
    if (mutex == SEM_FAILED) {
        perror("scheduler mutex sem_open failed");
        exit(1);
    }
    return mutex;
}

static void lock_scheduler(SharedScheduler *shared) {
    sem_t *mutex = open_scheduler_mutex(shared);
    sem_wait(mutex);
    sem_close(mutex);
}

static void unlock_scheduler(SharedScheduler *shared) {
    sem_t *mutex = open_scheduler_mutex(shared);
    sem_post(mutex);
    sem_close(mutex);
}

// Parent-side scheduler: chooses who enters each free node according to FCFS or SJF.
static void process_scheduler(SharedScheduler *shared, SchedulerType scheduler, sem_t **grant_sems, int num_nodes) {
    lock_scheduler(shared);

    for (int node = 0; node < num_nodes; node++) {
        if (shared->occupied_by[node] != -1 || shared->queue_count[node] == 0) {
            continue;
        }

        int selected_index = 0;
        if (scheduler == SCHD_SJF) {
            for (int i = 1; i < shared->queue_count[node]; i++) {
                if (shared->queues[node][i].remaining_work < shared->queues[node][selected_index].remaining_work) {
                    selected_index = i;
                }
            }
        }

        int traveler_id = shared->queues[node][selected_index].traveler_id;
        shared->occupied_by[node] = traveler_id;

        for (int i = selected_index; i < shared->queue_count[node] - 1; i++) {
            shared->queues[node][i] = shared->queues[node][i + 1];
        }
        shared->queue_count[node]--;

        sem_post(grant_sems[traveler_id]);
    }

    unlock_scheduler(shared);
}

// Autonomous Child Behavior Logic with Parent-Managed Scheduling
void run_child_behavior(int id, Traveler traveler, const Graph *graph, int write_fd,
                        SharedScheduler *shared, const char *grant_prefix) {
    Path my_path = dijkstra_shortest_path(graph, traveler.source, traveler.dest);

    char grant_name[80];
    build_grant_sem_name(grant_name, sizeof(grant_name), grant_prefix, id);
    sem_t *grant_sem = sem_open(grant_name, 0);
    if (grant_sem == SEM_FAILED) {
        perror("grant sem_open failed");
        free_path(&my_path);
        return;
    }

    if (!my_path.found || my_path.length == 0) {
        PositionUpdate update = { id, traveler.source, -1, STATUS_ARRIVED };
        write(write_fd, &update, sizeof(PositionUpdate));
        sem_close(grant_sem);
        free_path(&my_path);
        return;
    }

    for (int i = 0; i < my_path.length; i++) {
        int target_node = my_path.vertices[i];

        if (i > 0) {
            int weight = edge_weight_between(graph, my_path.vertices[i-1], target_node);
            usleep(weight * 250000); // Simulate edge transit time

            PositionUpdate wait_update = { id, my_path.vertices[i-1], target_node, STATUS_WAITING_FOR_NODE };
            write(write_fd, &wait_update, sizeof(PositionUpdate));

            int remaining_work = remaining_work_from_index(graph, &my_path, i);
            enqueue_wait_request(shared, target_node, id, remaining_work);

            // The child blocks here until the parent scheduler selects it.
            sem_wait(grant_sem);
        } else {
            // Source node is considered occupied immediately by this traveler.
            lock_scheduler(shared);
            shared->occupied_by[target_node] = id;
            unlock_scheduler(shared);
        }

        PositionUpdate arrive_update;
        arrive_update.traveler_id = id;
        arrive_update.current_node = target_node;
        arrive_update.next_node = (i == my_path.length - 1) ? -1 : my_path.vertices[i + 1];
        arrive_update.status = (i == my_path.length - 1) ? STATUS_ARRIVED : STATUS_INSIDE_NODE;
        write(write_fd, &arrive_update, sizeof(PositionUpdate));

        sleep(1); // Critical section: traveler stays inside the node for 1 second
        release_node(shared, target_node, id);
    }

    sem_close(grant_sem);
    free_path(&my_path);
}

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[1], "-schd") != 0) {
        fprintf(stderr, "Usage: %s -schd [fcfs|sjf] <file_name>\n", argv[0]);
        return 1;
    }

    SchedulerType scheduler;
    if (strcmp(argv[2], "fcfs") == 0) {
        scheduler = SCHD_FCFS;
    } else if (strcmp(argv[2], "sjf") == 0) {
        scheduler = SCHD_SJF;
    } else {
        fprintf(stderr, "Unknown scheduler: %s. Use fcfs or sjf.\n", argv[2]);
        return 1;
    }

    Graph graph;
    Traveler *travelers = NULL;
    int num_travelers = 0;
    char error[128];

    if (!load_graph_extended(argv[3], &graph, &travelers, &num_travelers, error, sizeof(error))) {
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

    SharedScheduler *shared = mmap(NULL, sizeof(SharedScheduler),
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) {
        perror("mmap failed");
        free_graph(&graph);
        free(travelers);
        return 1;
    }

    char mutex_name[80];
    snprintf(mutex_name, sizeof(mutex_name), "/scheduler_mutex_%ld", (long)getpid());
    sem_unlink(mutex_name);
    sem_t *scheduler_mutex = sem_open(mutex_name, O_CREAT, 0644, 1);
    if (scheduler_mutex == SEM_FAILED) {
        perror("scheduler mutex sem_open failed");
        munmap(shared, sizeof(SharedScheduler));
        free_graph(&graph);
        free(travelers);
        return 1;
    }
    sem_close(scheduler_mutex);
    strncpy(shared->mutex_name, mutex_name, sizeof(shared->mutex_name) - 1);
    shared->mutex_name[sizeof(shared->mutex_name) - 1] = '\0';

    shared->next_seq = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        shared->occupied_by[i] = -1;
        shared->queue_count[i] = 0;
    }

    char grant_prefix[64];
    snprintf(grant_prefix, sizeof(grant_prefix), "/traveler_grant_%ld", (long)getpid());

    sem_t *grant_sems[MAX_TRAVELERS];
    for (int i = 0; i < num_travelers; i++) {
        char grant_name[80];
        build_grant_sem_name(grant_name, sizeof(grant_name), grant_prefix, i);
        sem_unlink(grant_name);
        grant_sems[i] = sem_open(grant_name, O_CREAT, 0644, 0);
        if (grant_sems[i] == SEM_FAILED) {
            perror("grant sem_open failed");
            return 1;
        }
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
            run_child_behavior(i, travelers[i], &graph, fd[1], shared, grant_prefix);
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
        process_scheduler(shared, scheduler, grant_sems, graph.n);

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

        DrawText(TextFormat("Milestone 7: Scheduler = %s", scheduler == SCHD_FCFS ? "FCFS" : "SJF"), 250, 25, 26, MAROON);
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

    for (int i = 0; i < num_travelers; i++) {
        char grant_name[80];
        build_grant_sem_name(grant_name, sizeof(grant_name), grant_prefix, i);
        sem_close(grant_sems[i]);
        sem_unlink(grant_name);
    }

    sem_unlink(shared->mutex_name);
    munmap(shared, sizeof(SharedScheduler));

    free_graph(&graph);
    free(travelers);
    return 0;
}
