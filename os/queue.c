#include "queue.h"
#include "defs.h"

void init_queue(struct PriorityQueue *pq) {
    pq->size = 0;
}

int is_empty(struct PriorityQueue *pq) {
    return pq->size == 0;
}

void push_queue(struct PriorityQueue *pq, int value, int stride) {
    if (pq->size >= QUEUE_SIZE) {
        printf("Priority queue is full.\n");
        return;
    }
    Node node = {value, stride};
    int i = pq->size - 1;
    while (i >= 0 && pq->data[i].stride > stride) {
        pq->data[i + 1] = pq->data[i];
        i--;
    }
    pq->data[i + 1] = node;
    pq->size++;
}

int pop_queue(struct PriorityQueue *pq) {
    if (is_empty(pq)) {
        printf("Priority queue is empty.\n");
        return -1;
    }
    int value = pq->data[0].value;
    for (int i = 1; i < pq->size; i++) {
        pq->data[i - 1] = pq->data[i];
    }
    pq->size--;
    return value;
}

Node* find_node(struct PriorityQueue *pq, int value) {
    for (int i = 0; i < pq->size; i++) {
        if (pq->data[i].value == value) {
            return &pq->data[i];
        }
    }
    return NULL;
}

int update_stride(struct PriorityQueue *pq, int value, int new_stride) {
    Node *node = find_node(pq, value);
    if (node == NULL) {
        return -1;
    }
    int stride = node->stride;
    node->stride = new_stride;
    if (new_stride < stride) {
        int i = 0;
        while (i < pq->size && pq->data[i].value != value) {
            i++;
        }
        while (i > 0 && pq->data[i - 1].stride > new_stride) {
            pq->data[i] = pq->data[i - 1];
            i--;
        }
        pq->data[i] = *node;
    } else {
        int i = pq->size - 1;
        while (i > 0 && pq->data[i - 1].stride > new_stride) {
            pq->data[i] = pq->data[i - 1];
            i--;
        }
        pq->data[i] = *node;
    }
	printf("[debuf]:update *%d, new_stride = %d\n", value, new_stride);
	return new_stride;
}

int get_MinStride(struct PriorityQueue *pq) {
    if(pq->size <= 0)return 0;
	return pq->data[0].stride;
}
