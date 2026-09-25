#include <stdio.h>

// Static memory pool
#define MAX_NODES 16

typedef struct TreeNode {
    int data;
    struct TreeNode* left;
    struct TreeNode* right;
} TreeNode;

TreeNode pool[MAX_NODES];
int pool_idx = 0;

TreeNode* alloc_node(int data) {
    if (pool_idx >= MAX_NODES) return (TreeNode*)0;
    TreeNode* n = &pool[pool_idx++];
    n->data = data;
    n->left = (TreeNode*)0;
    n->right = (TreeNode*)0;
    return n;
}

// BST insert
TreeNode* insert(TreeNode* root, int data) {
    if (!root) return alloc_node(data);
    if (data < root->data)
        root->left = insert(root->left, data);
    else
        root->right = insert(root->right, data);
    return root;
}

// Inorder traversal (sorted output)
void inorder(TreeNode* root) {
    if (!root) return;
    inorder(root->left);
    printf("%d ", root->data);
    inorder(root->right);
}

// Preorder traversal
void preorder(TreeNode* root) {
    if (!root) return;
    printf("%d ", root->data);
    preorder(root->left);
    preorder(root->right);
}

// Find in BST
int find(TreeNode* root, int data) {
    if (!root) return 0;
    if (data == root->data) return 1;
    if (data < root->data) return find(root->left, data);
    return find(root->right, data);
}

// Tree height
int height(TreeNode* root) {
    if (!root) return 0;
    int lh = height(root->left);
    int rh = height(root->right);
    return 1 + (lh > rh ? lh : rh);
}

int main(void) {
    TreeNode* root = (TreeNode*)0;
    
    printf("=== Binary Search Tree Test ===\n\n");
    
    // Insert: 50, 30, 70, 20, 40, 60, 80
    root = insert(root, 50);
    root = insert(root, 30);
    root = insert(root, 70);
    root = insert(root, 20);
    root = insert(root, 40);
    root = insert(root, 60);
    root = insert(root, 80);
    
    printf("Tree structure:\n");
    printf("       50\n");
    printf("      /  \\\n");
    printf("    30    70\n");
    printf("   / \\   / \\\n");
    printf("  20 40 60 80\n\n");
    
    printf("Inorder (sorted): ");
    inorder(root);
    printf("\n");
    
    printf("Preorder: ");
    preorder(root);
    printf("\n");
    
    printf("Height: %d\n", height(root));
    printf("Find 40: %s\n", find(root, 40) ? "found" : "not found");
    printf("Find 99: %s\n", find(root, 99) ? "found" : "not found");
    
    printf("\nDone!\n");
    return 0;
}
