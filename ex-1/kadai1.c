// 61604239 尾崎耀一
#include <stdio.h>
#include <stdlib.h>
#define NNODE 6 // number of nodes
#define INF 100 //infinity
int *dist, *prev, *visited;
int linkcost[NNODE][NNODE] = {
    {  0,   2, 5,   1, INF, INF},  // from node-A to other nodes
    {  2,   0, 3,   2, INF, INF},  // from node-B to other nodes
    {  5,   3, 0,   3,   1,   5},  // from node-C to other nodes
    {  1,   2, 3,   0,   1, INF},  // from node-D to other nodes
    {INF, INF, 1,   1,   0,   2},  // from node-E to other nodes
    {INF, INF, 5, INF,   2,   0}   // from node-F to other nodes
};
char nodeName[NNODE] = {'A', 'B', 'C', 'D', 'E', 'F'};
void Dijkstra(int);
int main(int argc, char *argv[]) {
    int i;
    if ((dist = malloc(sizeof(int) * NNODE)) == NULL) {
		printf("Cannot allocate memory \n"); exit(1);
	}
	if ((prev = malloc(sizeof(int) * NNODE)) == NULL) {
		printf("Cannot allocate memory \n"); exit(1);
	}
	if ((visited = malloc(sizeof(int) * NNODE)) == NULL) {
		printf("Cannot allocate memory \n"); exit(1);
	}

    for (i = 0; i < NNODE; i++) {
        printf("root node: %c\n", nodeName[i]);
        Dijkstra(i);
    }
    free(dist);
	free(prev);
	free(visited);
}

void Dijkstra(int root) {
    int i;
    for (i = 0; i < NNODE; i++) {
        dist[i] = INF;
        prev[i] = -1;
        visited[i] = 0;
    }
    dist[root] = 0;
    while(1) {
        int minNode = -1;
        int min = INF;
        for (i = 0; i < NNODE; i++) {
            if (visited[i] == 0 && dist[i] < min) {
                min = dist[i];
                minNode = i;
            }
        }
        if (minNode == -1) {
            break;
        }
        visited[minNode] = 1;
        for (i = 0; i < NNODE; i++) {
            if (linkcost[minNode][i] > 0) {
                int w = linkcost[minNode][i];
                if (dist[minNode] + w < dist[i]) {
                    dist[i] = dist[minNode] + w;
                    prev[i] = minNode;
                }
            }
        }
    }

    printf("\t");
    for (i = 0; i < NNODE; i++) {
        if (root == i) {
            printf("[%c, %c, %d]", nodeName[i], nodeName[root], dist[i]);
        } else {
        printf("[%c, %c, %d]",nodeName[i], nodeName[prev[i]], dist[i]);
        }
        if (i == NNODE-1) {
            printf("\n");
        }
    }

}

