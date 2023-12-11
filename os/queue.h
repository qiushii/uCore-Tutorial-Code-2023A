#ifndef QUEUE_H
#define QUEUE_H
#define QUEUE_SIZE (1024)

// TODO: change the queue to a priority queue sorted by priority

typedef struct {
    int value;
    int stride;
} Node;

struct PriorityQueue{
    Node data[QUEUE_SIZE];
    int size;
};

void init_queue(struct PriorityQueue *);
void push_queue(struct PriorityQueue *, int, int);
int pop_queue(struct PriorityQueue *);
int update_stride(struct PriorityQueue *, int, int);
int get_MinStride(struct PriorityQueue *);

#endif // QUEUE_H
