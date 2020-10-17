#ifndef svc_h
#define svc_h

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define COMMIT_ID_LEN 7
#define FILE_NAME_LEN 261
#define BRANCH_NAME_LEN 51

typedef struct resolution {
    // NOTE: DO NOT MODIFY THIS STRUCT
    char *file_name;
    char *resolved_file;
}resolution;

typedef enum action_type {
    ACTION_ADD = 1,
    ACTION_REMOVE = 2,
    ACTION_MODIFY = 3
}action_type_t;

typedef struct action_info {
    action_type_t action;
    char *file_name; 
    unsigned int hash;
    unsigned int old_hash;
}action_info_t;

typedef struct tracked_file {
    char *file_name; 
    unsigned char *content;
    unsigned int hash;
}tracked_file_t;

typedef struct commit_node {
    char *branch_name;
    char *message;
    char *commit_id;
    tracked_file_t *tracked_files;
    size_t n_tracked_files;
    struct commit_node **next;
    size_t n_next_commit;
    struct commit_node *prev;
    action_info_t *actions;
    size_t n_actions;
}commit_node_t;

typedef struct commit_table{
    char *commit_id;
    commit_node_t *commit_address;
}commit_table_t;

typedef struct branch_table{
    char *branch_name;
    commit_node_t *branch_address;
}branch_table_t;

typedef struct project {
    commit_node_t *current_node;
    commit_node_t *head;
    commit_node_t *root_node;
    commit_table_t *commit_table;
    size_t n_total_commit;
    branch_table_t *branch_table;
    size_t n_total_branch;
}project_t;


void *svc_init(void);

void cleanup(void *helper);

int hash_file(void *helper, char *file_path);

char *svc_commit(void *helper, char *message);

void *get_commit(void *helper, char *commit_id);

char **get_prev_commits(void *helper, void *commit, int *n_prev);

void print_commit(void *helper, char *commit_id);

int svc_branch(void *helper, char *branch_name);

int svc_checkout(void *helper, char *branch_name);

char **list_branches(void *helper, int *n_branches);

int svc_add(void *helper, char *file_name);

int svc_rm(void *helper, char *file_name);

int svc_reset(void *helper, char *commit_id);

char *svc_merge(void *helper, char *branch_name, resolution *resolutions, int n_resolutions);

#endif
