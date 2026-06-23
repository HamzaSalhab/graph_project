#include "graph.h"

#include <math.h>
#include <stdio.h>

#include "raylib.h"

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 760
#define NODE_RADIUS 24.0f
#define EDGE_MARGIN 36.0f
#define JUMP_SECONDS 0.3f
#define NODE_WAIT_SECONDS 1.0f

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

static Vector2 add(Vector2 a, Vector2 b) {
    return (Vector2){a.x + b.x, a.y + b.y};
}

static Vector2 subtract(Vector2 a, Vector2 b) {
    return (Vector2){a.x - b.x, a.y - b.y};
}

static Vector2 scale(Vector2 v, float factor) {
    return (Vector2){v.x * factor, v.y * factor};
}

static float length(Vector2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static Vector2 normalize(Vector2 v) {
    float len = length(v);
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
    for (int i = 0; i < path->length - 1; i++) {
        if (path->vertices[i] == src && path->vertices[i + 1] == dst) {
            return true;
        }
    }
    return false;
}

static void draw_graph(const Graph *graph, const Path *path, const Vector2 *positions) {
    for (int i = 0; i < graph->m; i++) {
        Edge edge = graph->edges[i];
        bool highlighted = path->found && edge_is_in_path(path, edge.src, edge.dst);
        Color color = highlighted ? BLUE : DARKGRAY;
        draw_arrow(positions[edge.src], positions[edge.dst], color);

        Vector2 middle = lerp_vec(positions[edge.src], positions[edge.dst], 0.5f);
        DrawCircleV(middle, 15.0f, RAYWHITE);
        DrawText(TextFormat("%d", edge.weight), (int)middle.x - 6, (int)middle.y - 9, 18, color);
    }

    for (int i = 0; i < graph->n; i++) {
        Color fill = LIGHTGRAY;
        if (i == graph->source) {
            fill = GREEN;
        } else if (i == graph->target) {
            fill = ORANGE;
        }

        DrawCircleV(positions[i], NODE_RADIUS, fill);
        DrawCircleLines((int)positions[i].x, (int)positions[i].y, NODE_RADIUS, BLACK);
        DrawText(TextFormat("%d", i), (int)positions[i].x - 6, (int)positions[i].y - 10, 22, BLACK);
    }
}

static Rectangle play_button(void) {
    return (Rectangle){36.0f, 34.0f, 122.0f, 44.0f};
}

static void reset_animation(Animation *animation, const Graph *graph, const Path *path, const Vector2 *positions) {
    animation->playing = false;
    animation->phase = path->found && path->length > 1 ? ANIM_MOVING : ANIM_DONE;
    animation->path_index = 0;
    animation->jump_index = 0;
    animation->jump_count = 1;
    animation->timer = 0.0f;
    animation->position = positions[graph->source];

    if (path->found && path->length > 1) {
        int weight = edge_weight_between(graph, path->vertices[0], path->vertices[1]);
        animation->jump_count = weight > 0 ? weight : 1;
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

static void update_animation(Animation *animation, const Graph *graph, const Path *path, const Vector2 *positions) {
    if (!animation->playing || !path->found || animation->phase == ANIM_DONE) {
        return;
    }

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

static void draw_button(const Animation *animation) {
    Rectangle button = play_button();
    DrawRectangleRounded(button, 0.12f, 8, animation->playing ? RED : DARKGREEN);
    DrawText(animation->playing ? "Stop" : "Play", (int)button.x + 35, (int)button.y + 11, 24, WHITE);
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
    char error[128];
    if (!load_graph(argv[1], &graph, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    if (graph.n > 15) {
        fprintf(stderr, "Invalid input: sim supports up to 15 vertices\n");
        free_graph(&graph);
        return 1;
    }

    Path path = dijkstra_shortest_path(&graph, graph.source, graph.target);

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Graph Traffic Simulation");
    SetTargetFPS(60);

    Vector2 positions[32];
    calculate_positions(&graph, positions);

    Animation animation;
    reset_animation(&animation, &graph, &path, positions);

    while (!WindowShouldClose()) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), play_button())) {
            if (path.found) {
                if (animation.phase == ANIM_DONE) {
                    reset_animation(&animation, &graph, &path, positions);
                }
                animation.playing = !animation.playing;
            }
        }

        update_animation(&animation, &graph, &path, positions);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawText("Operating Systems Graph Simulation", 188, 38, 30, BLACK);
        draw_button(&animation);
        draw_graph(&graph, &path, positions);

        if (path.found) {
            DrawText(TextFormat("Shortest path weight: %d", path.total_weight), 36, WINDOW_HEIGHT - 64, 24, BLACK);
            DrawCircleV(animation.position, 12.0f, MAROON);
            if (animation.phase == ANIM_DONE) {
                DrawText("Arrived at destination", WINDOW_WIDTH - 292, WINDOW_HEIGHT - 64, 24, DARKGREEN);
            }
        } else {
            DrawText("No path found", 36, WINDOW_HEIGHT - 64, 28, RED);
        }

        EndDrawing();
    }

    CloseWindow();
    free_path(&path);
    free_graph(&graph);
    return 0;
}
