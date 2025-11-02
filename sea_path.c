#include <stdlib.h>
#include "sea_path.h"
#include "config.h"

/*
   Breadth-First Search (BFS) pathfinding restricted to sea tiles.
   Allowed to step through: TERRAIN_SEA and the destination port tile.
*/

typedef struct {
    int x, y;
    int parent_idx;
} Node;

int find_sea_path(const World* w, int sx, int sy, int dx, int dy, RoutePath* out) {
    static const int dxs[4] = { 0, 1, 0, -1 };
    static const int dys[4] = { -1, 0, 1, 0 };

    int max_nodes = w->width * w->height;

    /* visited[y][x] flags */
    int** visited = (int**)malloc(sizeof(int*) * w->height);
    for (int i = 0; i < w->height; ++i) {
        visited[i] = (int*)calloc(w->width, sizeof(int));
    }

    Node* queue = (Node*)malloc(sizeof(Node) * max_nodes);
    Node* parents = (Node*)malloc(sizeof(Node) * max_nodes);

    int front = 0, back = 0;
    int parent_count = 0;

    visited[sy][sx] = 1;
    queue[back++] = (Node){ sx, sy, -1 };

    while (front < back) {
        Node curr = queue[front++];

        /* Reached destination */
        if (curr.x == dx && curr.y == dy) {
            out->length = 0;
            while (curr.parent_idx != -1 && out->length < MAX_ROUTE_PATH) {
                out->x[out->length] = curr.x;
                out->y[out->length] = curr.y;
                curr = parents[curr.parent_idx];
                out->length++;
            }
            /* include start point */
            out->x[out->length] = sx;
            out->y[out->length] = sy;
            out->length++;

            /* reverse path order */
            for (int i = 0; i < out->length / 2; ++i) {
                int tx = out->x[i], ty = out->y[i];
                out->x[i] = out->x[out->length - 1 - i];
                out->y[i] = out->y[out->length - 1 - i];
                out->x[out->length - 1 - i] = tx;
                out->y[out->length - 1 - i] = ty;
            }

            for (int i = 0; i < w->height; ++i) free(visited[i]);
            free(visited); free(queue); free(parents);
            return 1;
        }

        int my_index = parent_count;
        parents[parent_count++] = curr;

        /* Explore 4 directions */
        for (int d = 0; d < 4; ++d) {
            int nx = curr.x + dxs[d];
            int ny = curr.y + dys[d];
            if (nx < 0 || nx >= w->width || ny < 0 || ny >= w->height) continue;
            if (visited[ny][nx]) continue;

            /* Only walk on sea tiles or allow the destination tile */
            if (w->grid[ny][nx].terrain != TERRAIN_SEA && !(nx == dx && ny == dy)) continue;

            visited[ny][nx] = 1;
            queue[back++] = (Node){ nx, ny, my_index };
            if (back >= max_nodes) break;
        }
    }

    /* no path found */
    out->length = 0;
    for (int i = 0; i < w->height; ++i) free(visited[i]);
    free(visited); free(queue); free(parents);
    return 0;
}
