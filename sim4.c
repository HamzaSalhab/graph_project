#include "graph.h"

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "raylib.h"

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 760
#define NODE_RADIUS 24.0f
#define EDGE_MARGIN 36.0f
#define JUMP_SECONDS 0.3f
#define NODE_WAIT_SECONDS 1.0f
#define MAX_VERTICES 32

typedef enum {
    ANIM_MOVING,
    ANIM_WAITING,
    ANIM_DONE
} AnimationPhase;

typedef struct {
    bool playing;
    AnimationPhase phase;
    int path_index;
    int jump_index;
    int jump_count;
    float timer;
    Vector2 position;
} Animation;

typedef struct {
    Traveler traveler;
    Path path;
    Animation animation;
    Color color;
    pid_t pid;
    bool child_finished;
} Passenger;

static Vector2 add(Vector2 a, Vector2 b) {
    return (Vector2){a.x + b.x, a.y + b.y};
}

static Vector2 subtract(Vector2 a, Vector2 b) {
    return (Vector2){a.x - b.x, a.y - b.y};
}

static Vector2 scale(Vector2 v, float factor) {
    return (Vector2){v.x * factor, v.y * factor};
}

static float vec_length(Vector2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static Vector2 normalize(Vector2 v) {
    float len = vec_length(v);
    if (len == 0.0f) {
        return (Vector2){0.0f, 0.0f};
    }
    return scale(v, 1.0f / len);
}

static Vector2 lerp_vec(Vector2 a, Vector2 b, float t) {
    return add(a, scale(subtract(b, a), t));
}

static float clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static void calculate_positions(const Graph *graph, Vector2 *positions) {
    Vector2 center = {WINDOW_WIDTH * 0.5f, WINDOW_HEIGHT * 0.46f};
    float radius = graph->n <= 6 ? 230.0f : 285.0f;

    for (int i = 0; i < graph->n; i++) {
        float angle = -PI / 2.0f + (2.0f * PI * i) / graph->n;
        positions[i] = (Vector2){
            center.x + cosf(angle) * radius,
            center.y + sinf(angle) * radius
        };
    }
}

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

static bool edge_is_in_path(const Path *path, int src, int dst) {
    if (!path->found) {
        return false;
    }

    for (int i = 0; i < path->length - 1; i++) {
        if (path->vertices[i] == src && path->vertices[i + 1] == dst) {
            return true;
        }
    }

    return false;
}

static bool edge_is_in_any_path(const Passenger *passengers, int passenger_count, int src, int dst) {
    for (int i = 0; i < passenger_count; i++) {
        if (edge_is_in_path(&passengers[i].path, src, dst)) {
            return true;
        }
    }

    return false;
}

static void draw_graph(const Graph *graph,
                       const Passenger *passengers,
                       int passenger_count,
                       const Vector2 *positions) {
    for (int i = 0; i < graph->m; i++) {
        Edge edge = graph->edges[i];

        bool highlighted = edge_is_in_any_path(passengers, passenger_count, edge.src, edge.dst);
        Color color = highlighted ? BLUE : DARKGRAY;

        draw_arrow(positions[edge.src], positions[edge.dst], color);

        Vector2 middle = lerp_vec(positions[edge.src], positions[edge.dst], 0.5f);
        DrawCircleV(middle, 15.0f, RAYWHITE);
        DrawText(TextFormat("%d", edge.weight),
                 (int)middle.x - 6,
                 (int)middle.y - 9,
                 18,
                 color);
    }

    for (int i = 0; i < graph->n; i++) {
        DrawCircleV(positions[i], NODE_RADIUS, LIGHTGRAY);
        DrawCircleLines((int)positions[i].x, (int)positions[i].y, NODE_RADIUS, BLACK);
        DrawText(TextFormat("%d", i),
                 (int)positions[i].x - 6,
                 (int)positions[i].y - 10,
                 22,
                 BLACK);
    }
}

static Rectangle play_button(void) {
    return (Rectangle){36.0f, 34.0f, 122.0f, 44.0f};
}

static void draw_button(bool playing) {
    Rectangle button = play_button();
    DrawRectangleRounded(button, 0.12f, 8, playing ? RED : DARKGREEN);
    DrawText(playing ? "Stop" : "Play",
             (int)button.x + 35,
             (int)button.y + 11,
             24,
             WHITE);
}

static void reset_animation(Animation *animation, const Path *path, const Vector2 *positions) {
    animation->playing = false;
    animation->phase = path->found && path->length > 1 ? ANIM_MOVING : ANIM_DONE;
    animation->path_index = 0;
    animation->jump_index = 0;
    animation->jump_count = 1;
    animation->timer = 0.0f;

    if (path->found && path->length > 0) {
        animation->position = positions[path->vertices[0]];
    } else {
        animation->position = (Vector2){0.0f, 0.0f};
    }
}

static void start_next_edge(Animation *animation, const Graph *graph, const Path *path) {
    if (animation->path_index >= path->length - 1) {
        animation->phase = ANIM_DONE;
        animation->playing = false;
        return;
    }

    int src = path->vertices[animation->path_index];
    int dst = path->vertices[animation->path_index + 1];
    int weight = edge_weight_between(graph, src, dst);

    animation->jump_count = weight > 0 ? weight : 1;
    animation->jump_index = 0;
    animation->timer = 0.0f;
    animation->phase = ANIM_MOVING;
}

static void update_animation(Animation *animation,
                             const Graph *graph,
                             const Path *path,
                             const Vector2 *positions,
                             bool global_playing) {
    if (!global_playing || !path->found || animation->phase == ANIM_DONE) {
        return;
    }

    animation->playing = true;
    animation->timer += GetFrameTime();

    if (animation->phase == ANIM_MOVING) {
        int src = path->vertices[animation->path_index];
        int dst = path->vertices[animation->path_index + 1];

        float jump_progress = clamp01(animation->timer / JUMP_SECONDS);
        float jump_start = (float)animation->jump_index / (float)animation->jump_count;
        float jump_end = (float)(animation->jump_index + 1) / (float)animation->jump_count;
        float t = jump_start + (jump_end - jump_start) * jump_progress;

        animation->position = lerp_vec(positions[src], positions[dst], t);

        if (animation->timer >= JUMP_SECONDS) {
            animation->timer = 0.0f;
            animation->jump_index++;

            if (animation->jump_index >= animation->jump_count) {
                animation->position = positions[dst];
                animation->path_index++;

                if (animation->path_index >= path->length - 1) {
                    animation->phase = ANIM_DONE;
                    animation->playing = false;
                } else {
                    animation->phase = ANIM_WAITING;
                }
            }
        }
    } else if (animation->phase == ANIM_WAITING) {
        animation->position = positions[path->vertices[animation->path_index]];

        if (animation->timer >= NODE_WAIT_SECONDS) {
            start_next_edge(animation, graph, path);
        }
    }
}

static bool all_passengers_done(const Passenger *passengers, int passenger_count) {
    for (int i = 0; i < passenger_count; i++) {
        if (passengers[i].animation.phase != ANIM_DONE) {
            return false;
        }
    }

    return true;
}

static void terminate_child_if_needed(Passenger *passenger) {
    if (!passenger->child_finished && passenger->pid > 0 && passenger->animation.phase == ANIM_DONE) {
        kill(passenger->pid, SIGTERM);
        waitpid(passenger->pid, NULL, 0);
        passenger->child_finished = true;
    }
}

static void terminate_all_children(Passenger *passengers, int passenger_count) {
    for (int i = 0; i < passenger_count; i++) {
        if (!passengers[i].child_finished && passengers[i].pid > 0) {
            kill(passengers[i].pid, SIGTERM);
        }
    }

    for (int i = 0; i < passenger_count; i++) {
        if (!passengers[i].child_finished && passengers[i].pid > 0) {
            waitpid(passengers[i].pid, NULL, 0);
            passengers[i].child_finished = true;
        }
    }
}

static Color passenger_color(int index) {
    Color colors[] = {
        MAROON,
        DARKGREEN,
        PURPLE,
        ORANGE,
        PINK,
        BROWN,
        DARKBLUE,
        LIME,
        GOLD,
        VIOLET
    };

    int count = (int)(sizeof(colors) / sizeof(colors[0]));
    return colors[index % count];
}

static void child_process_loop(void) {
    printf("[%d] started\n", getpid());
    fflush(stdout);

    while (1) {
        pause();
    }
}

static void print_usage(const char *program) {
    fprintf(stderr, "Usage: %s <file_name>\n", program);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    Graph graph;
    Traveler *travelers = NULL;
    int num_travelers = 0;
    char error[128];

    if (!load_graph_extended(argv[1], &graph, &travelers, &num_travelers, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    if (graph.n > 15) {
        fprintf(stderr, "Invalid input: sim4 supports up to 15 vertices\n");
        free(travelers);
        free_graph(&graph);
        return 1;
    }

    if (num_travelers <= 0) {
        fprintf(stderr, "Invalid input: no travelers found\n");
        free(travelers);
        free_graph(&graph);
        return 1;
    }

    Passenger *passengers = calloc((size_t)num_travelers, sizeof(Passenger));
    if (passengers == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        free(travelers);
        free_graph(&graph);
        return 1;
    }

    Vector2 positions[MAX_VERTICES];
    calculate_positions(&graph, positions);

    for (int i = 0; i < num_travelers; i++) {
        passengers[i].traveler = travelers[i];
        passengers[i].color = passenger_color(i);
        passengers[i].pid = -1;
        passengers[i].child_finished = false;

        if (travelers[i].source < 0 || travelers[i].source >= graph.n ||
            travelers[i].dest < 0 || travelers[i].dest >= graph.n) {
            fprintf(stderr, "Invalid traveler %d: source or destination out of range\n", i);
            terminate_all_children(passengers, i);
            free(passengers);
            free(travelers);
            free_graph(&graph);
            return 1;
        }

        passengers[i].path = dijkstra_shortest_path(&graph, travelers[i].source, travelers[i].dest);
        reset_animation(&passengers[i].animation, &passengers[i].path, positions);
    }

    for (int i = 0; i < num_travelers; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            terminate_all_children(passengers, i);
            for (int j = 0; j < num_travelers; j++) {
                free_path(&passengers[j].path);
            }
            free(passengers);
            free(travelers);
            free_graph(&graph);
            return 1;
        }

        if (pid == 0) {
            child_process_loop();
            return 0;
        }

        passengers[i].pid = pid;
    }

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Milestone 4 - Multi Process Graph Simulation");
    SetTargetFPS(60);

    bool global_playing = false;

    while (!WindowShouldClose()) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), play_button())) {
            global_playing = !global_playing;
        }

        for (int i = 0; i < num_travelers; i++) {
            update_animation(&passengers[i].animation,
                             &graph,
                             &passengers[i].path,
                             positions,
                             global_playing);

            terminate_child_if_needed(&passengers[i]);
        }

        if (all_passengers_done(passengers, num_travelers)) {
            global_playing = false;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawText("Milestone 4: Parent GUI + fork() Children", 205, 38, 30, BLACK);
        draw_button(global_playing);

        draw_graph(&graph, passengers, num_travelers, positions);

        for (int i = 0; i < num_travelers; i++) {
            if (passengers[i].path.found) {
                DrawCircleV(passengers[i].animation.position, 12.0f, passengers[i].color);
                DrawText(TextFormat("T%d", i + 1),
                         (int)passengers[i].animation.position.x + 14,
                         (int)passengers[i].animation.position.y - 10,
                         18,
                         passengers[i].color);
            }
        }

        int y = WINDOW_HEIGHT - 110;
        for (int i = 0; i < num_travelers; i++) {
            if (passengers[i].path.found) {
                DrawText(TextFormat("T%d: %d -> %d | weight: %d | PID: %d",
                                    i + 1,
                                    passengers[i].traveler.source,
                                    passengers[i].traveler.dest,
                                    passengers[i].path.total_weight,
                                    passengers[i].pid),
                         36,
                         y,
                         18,
                         passengers[i].color);
            } else {
                DrawText(TextFormat("T%d: %d -> %d | No path | PID: %d",
                                    i + 1,
                                    passengers[i].traveler.source,
                                    passengers[i].traveler.dest,
                                    passengers[i].pid),
                         36,
                         y,
                         18,
                         RED);
            }

            y += 24;
        }

        if (all_passengers_done(passengers, num_travelers)) {
            DrawText("All passengers arrived", WINDOW_WIDTH - 300, WINDOW_HEIGHT - 64, 24, DARKGREEN);
        }

        EndDrawing();
    }

    CloseWindow();

    terminate_all_children(passengers, num_travelers);

    for (int i = 0; i < num_travelers; i++) {
        free_path(&passengers[i].path);
    }

    free(passengers);
    free(travelers);
    free_graph(&graph);

    return 0;
}
