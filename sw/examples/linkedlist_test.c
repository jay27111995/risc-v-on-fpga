#include <stdio.h>

// Static memory pool (no malloc)
#define MAX_NODES 16

typedef struct Node {
    int data;
    struct Node* next;
} Node;

Node pool[MAX_NODES];
int pool_idx = 0;

Node* alloc_node(int data) {
    if (pool_idx >= MAX_NODES) return (Node*)0;
    Node* n = &pool[pool_idx++];
    n->data = data;
    n->next = (Node*)0;
    return n;
}

void print_list(Node* head) {
    while (head) {
        printf("%d -> ", head->data);
        head = head->next;
    }
    printf("NULL\n");
}

Node* append(Node* head, int data) {
    Node* new_node = alloc_node(data);
    if (!new_node) return head;
    if (!head) return new_node;
    Node* curr = head;
    while (curr->next) curr = curr->next;
    curr->next = new_node;
    return head;
}

Node* reverse(Node* head) {
    Node* prev = (Node*)0;
    Node* curr = head;
    while (curr) {
        Node* next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
    }
    return prev;
}

int main(void) {
    Node* list = (Node*)0;
    
    printf("=== Linked List Test ===\n\n");
    
    list = append(list, 10);
    list = append(list, 20);
    list = append(list, 30);
    list = append(list, 40);
    printf("List: ");
    print_list(list);
    
    list = reverse(list);
    printf("Reversed: ");
    print_list(list);
    
    printf("\nDone!\n");
    return 0;
}
