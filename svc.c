#include "svc.h"

static commit_node_t *commit_node_init() {
	commit_node_t *node = (commit_node_t*)malloc(sizeof(commit_node_t));
	node->branch_name =  (char*)malloc(sizeof(char) * BRANCH_NAME_LEN);
	strcpy(node->branch_name, "master");
	node->message = NULL;
	node->commit_id = NULL;
	node->tracked_files = NULL;
	node->n_tracked_files = 0;
	node->n_next_commit = 0;
	node->prev = NULL;
	node->actions = 0;
	node->n_actions = 0;
	return node;
}

static unsigned char *file_content_copy(char *file_name) {
	if (file_name == NULL) {
		return NULL;
	}
	
	FILE * fptr;
	unsigned char *file_content = NULL;
	unsigned int file_content_len = 0;
	
	fptr = fopen(file_name, "r");
	
	if (fptr == NULL) {
    	return NULL;
	}
	
	// Find number of characters in the file.
	fseek(fptr, 0, SEEK_END);
	file_content_len = ftell(fptr);
	fseek(fptr, 0, SEEK_SET);	
	
	file_content = (unsigned char*)malloc(sizeof(unsigned char) * file_content_len + 1);	
	
	if (file_content == NULL) {
    	return NULL;
	}
	
	fread(file_content, sizeof(unsigned char), file_content_len, fptr);
	
	fclose(fptr);
	
	return file_content;	
}

static tracked_file_t *tracked_files_copy(size_t size, tracked_file_t *src_files) {
	tracked_file_t *new_files = malloc(sizeof(tracked_file_t) * size);
	
	for (int file_idx = 0; file_idx < size; file_idx++) {
		new_files[file_idx].file_name = malloc(sizeof(char) * FILE_NAME_LEN);
		strcpy(new_files[file_idx].file_name, src_files[file_idx].file_name);
		new_files[file_idx].content = file_content_copy(new_files[file_idx].file_name);
		memcpy(&new_files[file_idx].hash, &src_files[file_idx].hash, sizeof(new_files[file_idx].hash));
	}
	
	return new_files;
}

static commit_node_t *node_copy(commit_node_t *src_node) {
	commit_node_t *new_node = (commit_node_t *)malloc(sizeof(commit_node_t));
	new_node->branch_name = src_node->branch_name;
	new_node->message = NULL;
	new_node->commit_id = NULL;
	new_node->tracked_files = tracked_files_copy(src_node->n_tracked_files, src_node->tracked_files);
	new_node->n_tracked_files = src_node->n_tracked_files;
	new_node->n_next_commit = 0;
	new_node->prev = src_node;
	new_node->actions = NULL;
	new_node->n_actions = 0;
	return new_node;
}

static commit_node_t *branch_node_copy(commit_node_t *src_node, char *branch_name) {
	commit_node_t *new_node = (commit_node_t *)malloc(sizeof(commit_node_t));
	new_node->branch_name = malloc(sizeof(char) * BRANCH_NAME_LEN);
	strcpy(new_node->branch_name, branch_name);
	new_node->message = NULL;
	new_node->commit_id = NULL;
	new_node->tracked_files = tracked_files_copy(src_node->n_tracked_files, src_node->tracked_files);
	new_node->n_tracked_files = src_node->n_tracked_files;
	new_node->n_next_commit = 0;
	new_node->prev = src_node;
	new_node->actions = NULL;
	new_node->n_actions = 0;
	return new_node;
}

void *svc_init(void) {
	project_t *project = (project_t*)malloc(sizeof(project_t));
	project->current_node = commit_node_init();
	project->head = NULL;
	project->root_node = project->current_node;
	project->commit_table = NULL;
	project->n_total_commit = 0;
	project->n_total_branch = 1;
	project->branch_table = (branch_table_t *)malloc(sizeof(branch_table_t) * project->n_total_branch);
	project->branch_table[0].branch_name = project->root_node->branch_name;
	project->branch_table[0].branch_address = project->root_node;
	return project;
}

// Clean up file's name and its content.
static void cleanup_files(size_t size, tracked_file_t *src_files) {
	for (int file_idx = 0; file_idx < size; file_idx++) {
		free(src_files[file_idx].file_name);
		free(src_files[file_idx].content);
	}
}

// Clean up branches.
static void cleanup_branch(void *helper) {
	project_t *project = (project_t*)helper;
	commit_node_t *current_node = NULL;
	commit_node_t *prev_node = NULL;
	commit_node_t * temp_node = NULL;
	// Total number of previous commits before staging area.
	size_t n_prev_node = 0;
	
	// Find the branch's root node from the branch table.
	for (int branch_idx = 0; branch_idx < project->n_total_branch; branch_idx++) {
		current_node = project->branch_table[branch_idx].branch_address;
		
		// Move to the staging area
		while (current_node->commit_id != NULL) {
			if (current_node->next[0] != NULL) {
				n_prev_node++;
				current_node = current_node->next[0];
			}
		}
		
		// The latest commit node in the branch.
		prev_node = current_node->prev; 
		
		// Free staging area.
		free(current_node->branch_name);
		cleanup_files(current_node->n_tracked_files, current_node->tracked_files);
		free(current_node->tracked_files);
		free(current_node);
		
		// Free every commit nodes in the branch.
		for (int node_idx = 0; node_idx < n_prev_node; node_idx++) {
			temp_node = prev_node->prev;
			cleanup_files(prev_node->n_tracked_files, prev_node->tracked_files);
			free(prev_node->tracked_files);
			free(prev_node->actions);
			free(prev_node->next);
			free(prev_node->message);
			free(prev_node->commit_id);
			free(prev_node);
			prev_node = temp_node;
		}
		n_prev_node = 0;
	}
}

void cleanup(void *helper) {
	project_t *project = (project_t*)helper;
	cleanup_branch(helper);
	free(project->commit_table);
	free(project->branch_table);
	free(project);
}

int hash_file(void *helper, char *file_path) {
	// If file_path is NULL, return -1.
	if (file_path == NULL) {
		return -1;
	}
	
	FILE * fptr;
    // Make sure that characters in the file always treated as unsigned value. (e.g. special characters)
	unsigned char * file_content;
	unsigned int file_path_len = strlen(file_path);
	unsigned int file_content_len = 0;
	unsigned int hash = 0;
	
	fptr = fopen(file_path, "r");
	
	// If no file exists at the given path, return -2
	if (fptr == NULL) {
    	return -2;
	}
 
	// Find number of characters in the file.
	fseek(fptr, 0, SEEK_END);
	file_content_len = ftell(fptr);
	fseek(fptr, 0, SEEK_SET);	
	
	file_content = (unsigned char*)malloc(sizeof(unsigned char) * file_content_len + 1);	
    
	if (file_content == NULL) {
    	return 1;
	}
 
	fread(file_content, sizeof(unsigned char), file_content_len, fptr);
	fclose(fptr);
	
	// Calculate hash value of the file_path.
	for (int i = 0; i < file_path_len; i++) {
        hash += file_path[i];
        hash = (hash % 1000);

	}
	
	// Calculate hash value of the file content.
	for (int i = 0; i < file_content_len; i++) {
		hash += file_content[i];
        hash = (hash % 2000000000);
	}
	
	free(file_content);
	
	return hash;
}

// Check if there is a change in tracked files.
// Return 1 if there is a change (addition, deletion, modification).
// Return 0 if there is no change .
static int check_change(void *helper) {
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;
	commit_node_t *head = project->head;
	
	if (head != NULL) {
		unsigned int total_hash_node = 0;
		unsigned int total_hash_head = 0;
		
		// Find total hash value for the current node (stage area).
		for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
			// Update hash value if there was a modification of the file.
			node->tracked_files[file_idx].hash = hash_file(helper, node->tracked_files[file_idx].file_name);
			total_hash_node += node->tracked_files[file_idx].hash;
		}
		// Find total hash value for the head.
		for (int head_file_idx = 0; head_file_idx < head->n_tracked_files; head_file_idx++) {
			total_hash_head += head->tracked_files[head_file_idx].hash;
		}
		// If two total hash values are equal, there was no change and return 0.
		if (total_hash_node == total_hash_head) {
			return 0;
		}
	}
	// Otherwise, return 1
	return 1;
}

// Check if the file is locally (or manually) removed from SVC. 
static void check_local_deletion(void *helper) {
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;
	FILE * fptr;
	
	for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
		fptr = fopen(node->tracked_files[file_idx].file_name, "r");	
		// If the file does not exist at the given path, remove it from the SVC.
		if (fptr == NULL) {
			svc_rm(helper, node->tracked_files[file_idx].file_name);
		}
	}
	
	if (fptr != NULL) {
		fclose(fptr);
	}
}

// Determine if the change is addition, deletion or modification.
static void determine_action(void *helper) {
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;
	commit_node_t *head = project->head;
	
	// If head is NULL, it means initial commit.
	// Every tracked files will be determined as addition.
	if (head == NULL) {
		node->n_actions = node->n_tracked_files;
		node->actions = (action_info_t*)malloc(sizeof(action_info_t) * node->n_actions);
		for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
			node->actions[file_idx].file_name = node->tracked_files[file_idx].file_name;
			node->actions[file_idx].action = ACTION_ADD;
			node->actions[file_idx].hash = node->tracked_files[file_idx].hash;
			node->actions[file_idx].old_hash = 0;
		}
	}else {
		int is_matched = -1;
		// Check and determine if the file is added or modified in SVC.
		for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
			for (int head_file_idx = 0; head_file_idx < head->n_tracked_files; head_file_idx++) {
				is_matched = strcmp(node->tracked_files[file_idx].file_name, head->tracked_files[head_file_idx].file_name);
				if (is_matched == 0) {
					// Update hash value if there was a modification of the file
					unsigned int new_hash = hash_file(project, node->tracked_files[file_idx].file_name);
					// If equal, there was no modification.
					if (new_hash == head->tracked_files[head_file_idx].hash) {
						break;
					}else {
						node->n_actions++;
				
						if (node->n_actions == 1) {
							node->actions = (action_info_t*)malloc(sizeof(action_info_t) * node->n_actions);
						}else {
							node->actions = (action_info_t*)realloc(node->actions, sizeof(action_info_t) * node->n_actions);
						}
						// Free the previous file content.
						free(node->tracked_files[file_idx].content);
						// Copy the new file content.
						node->tracked_files[file_idx].content = file_content_copy(node->tracked_files[file_idx].file_name);
						node->tracked_files[file_idx].hash = new_hash;
						node->actions[node->n_actions - 1].file_name = node->tracked_files[file_idx].file_name;
						node->actions[node->n_actions - 1].action = ACTION_MODIFY;
						node->actions[node->n_actions - 1].hash = node->tracked_files[file_idx].hash;
						node->actions[node->n_actions - 1].old_hash = head->tracked_files[file_idx].hash;
						break;
					}
				}
			}
			if (is_matched != 0) {
				node->n_actions++;
				
				if (node->n_actions == 1) {
					node->actions = (action_info_t*)malloc(sizeof(action_info_t) * node->n_actions);
				}else {
					node->actions = (action_info_t*)realloc(node->actions, sizeof(action_info_t) * node->n_actions);
				}
				node->actions[node->n_actions - 1].file_name = node->tracked_files[file_idx].file_name;
				node->actions[node->n_actions - 1].action = ACTION_ADD;
				node->actions[node->n_actions - 1].hash = node->tracked_files[file_idx].hash;
				node->actions[node->n_actions - 1].old_hash = 0;
			}
		}
		// Check and determine if the file is removed from SVC.
		for (int head_file_idx = 0; head_file_idx < head->n_tracked_files; head_file_idx++) {
			for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
				is_matched = strcmp(node->tracked_files[file_idx].file_name, head->tracked_files[head_file_idx].file_name);
				if (is_matched == 0) {
					break;
				}
			}
			if (is_matched != 0) {
				node->n_actions++;
				
				if (node->n_actions == 1) {
					node->actions = (action_info_t*)malloc(sizeof(action_info_t) * node->n_actions);
				}else {
					node->actions = (action_info_t*)realloc(node->actions, sizeof(action_info_t) * node->n_actions);
				}
				node->actions[node->n_actions - 1].file_name = head->tracked_files[head_file_idx].file_name;
				node->actions[node->n_actions - 1].action = ACTION_REMOVE;
				node->actions[node->n_actions - 1].hash = head->tracked_files[head_file_idx].hash;
				node->actions[node->n_actions - 1].old_hash = 0;
			}
		}		
	}
}

// Comparator function for qsort in order to sort file names by increasing alphabetical order.
static int compare_str(const void *pa, const void * pb) {
	const action_info_t *p1 = (action_info_t *) pa;
	const action_info_t *p2 = (action_info_t *) pb;
	unsigned int p1_ascii = tolower(p1->file_name[0]);
	unsigned int p2_ascii = tolower(p2->file_name[0]);
	
 	if (p1_ascii < p2_ascii){
		return -1;	
	} 
  	if (p1_ascii == p2_ascii){
		return 0;
	} 
  	if (p1_ascii > p2_ascii){
		return 1;
	} 
	
	return 0;
}

// Calculate commit id.
static unsigned int get_commit_id(void *helper, char *message) {
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;
	
	unsigned int commit_id = 0;
	unsigned int message_len = strlen(message);
		
	// Calculate commit_id from message.
	for (int i = 0; i < message_len; i++) {
        commit_id += message[i];
        commit_id = (commit_id % 1000);
	}
	
	// Sort file names by increasing alphabetical order.
	qsort(node->actions, node->n_actions, sizeof(action_info_t), compare_str);
	
	// Calculate commit_id from action array.
	for (int action_idx = 0; action_idx < node->n_actions; action_idx++) {
		if(node->actions[action_idx].action == ACTION_ADD){
			commit_id += 376591;
		}
		if (node->actions[action_idx].action == ACTION_REMOVE) {
			commit_id += 85973;
		}
		if (node->actions[action_idx].action == ACTION_MODIFY) {
			commit_id += 9573681;
		}
		unsigned int file_path_len = strlen(node->actions[action_idx].file_name);
		for (int file_idx = 0; file_idx < file_path_len; file_idx++) {
			commit_id *= (node->actions[action_idx].file_name[file_idx] % 37);
			commit_id = (commit_id % 15485863) + 1;
		}
	}
	
	return commit_id;
}

// Fill up the commit table that keeps track of commit addresses.
static void add_commit_table(void *helper) {
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;
	
	project->n_total_commit++;
	if (project->n_total_commit == 1) {
		project->commit_table = (commit_table_t *)malloc(sizeof(commit_table_t) * project->n_total_commit);
	}else {
		project->commit_table = (commit_table_t *)realloc(project->commit_table, sizeof(commit_table_t) * project->n_total_commit);
	}
	
	project->commit_table[project->n_total_commit - 1].commit_id = node->commit_id;
	project->commit_table[project->n_total_commit - 1].commit_address = node;
}

char *svc_commit(void *helper, char *message) {
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;
	commit_node_t *head = project->head;
	
	// If message is NULL, return NULL
	if (message == NULL) {
		return NULL;
	}
			
	// Return NULL if commit is empty (when SVC is not tracking any files).
	if (head == NULL) {
		if(node->n_tracked_files == 0){
			return NULL;
		}
	}
	
	// Return NULL if there was no change in tracked files
	int is_changed = check_change(helper);
	if (head != NULL) {
		if (is_changed == 0) {
			return NULL;
		}
	}
	
	// Check if file was locally(or manually) deleted.
	check_local_deletion(helper);
	
	// Determine if the change is addition, deletion or modification.
	determine_action(helper);
		
	// Calculate commit_id.
	unsigned int commit_id = get_commit_id(helper, message);
	unsigned int message_len = strlen(message) + 1;
			
	// Convert commit_id to hexadecimal.
	char commit_id_hex[COMMIT_ID_LEN];
	sprintf(commit_id_hex, "%06x", commit_id);	
	
	// Commit current stage - set message and commit id to the node.
	node->message = (char *)malloc(sizeof(char) * message_len);
	strcpy(node->message, message);
	node->commit_id = (char *)malloc(sizeof(char) * COMMIT_ID_LEN);
	strcpy(node->commit_id, commit_id_hex);
	
	// Update head.
	project->head = node;
	
	// Fill up the commit_table.
	add_commit_table(helper);
	
	// Create next node.
	node->n_next_commit++;
	if (node->n_next_commit == 1) {
		node->next = (commit_node_t **)malloc(sizeof(commit_node_t *) * node->n_next_commit);
	}else {
		node->next = (commit_node_t **)realloc(node->next, sizeof(commit_node_t *) * node->n_next_commit);
	}
	
	// Copy node to next node.
	node->next[node->n_next_commit - 1] = node_copy(node);

	// Update current node (which will be used as staging area).
	project->current_node = node->next[node->n_next_commit - 1];
	
	return node->commit_id;
}

void *get_commit(void *helper, char *commit_id) {
	project_t *project = (project_t*)helper;
	int is_matched = 1;
	
	// If commit_id is NULL, this function should return NULL.
	if (commit_id == NULL) {
		return NULL;
	}
	
	// If a commit with the given id does exist in the commit table, return its address.
	for (int commit_idx = 0; commit_idx < project->n_total_commit; commit_idx++) {
		is_matched = strcmp(project->commit_table[commit_idx].commit_id, commit_id);
		if (is_matched == 0) {
			return project->commit_table[commit_idx].commit_address;
		}
	}
	
	// Otherwise, return NULL.
    return NULL;
}

char **get_prev_commits(void *helper, void *commit, int *n_prev) {
	// If n_prev is NULL, return NULL.
	if (n_prev == NULL) {
		return NULL;
	}
	
	// If commit is NULL, or it is the very first commit,
	// this function should set the contents of n_prev to 0 and return NULL.
	project_t *project = (project_t*)helper;
	if (commit == NULL || project->n_total_commit == 1){
		*n_prev = 0;
		return NULL;
	}
	
    commit_node_t *node = (commit_node_t*)commit;
	char **prev_commits = NULL;
	int item_count = 0;
	
	while (node != NULL && node->prev != NULL) {
		node = node->prev;
		item_count++;
		int malloc_size = sizeof(char*) * item_count;
		
		if (item_count == 1) {
			prev_commits = malloc(malloc_size);
		}else {
			prev_commits = realloc(prev_commits, malloc_size);
		}
		
		// Allocate memory for commit_id string.
		prev_commits[item_count - 1] = node->commit_id;
	}
	*n_prev = item_count;
	
	return prev_commits;
}

void print_commit(void *helper, char *commit_id) {
	commit_node_t *commit = get_commit(helper, commit_id);

	if (commit == NULL || commit_id == NULL) {
		printf("Invalid commit id\n");
	}else {		
		printf("%s [%s]: %s\n", commit->commit_id, commit->branch_name, commit->message);
		for (int action_idx = 0; action_idx < commit->n_actions; action_idx++) {
			const action_info_t *action_inf = &commit->actions[action_idx];
			if (action_inf->action == ACTION_ADD) {
				printf("    + %s\n", action_inf->file_name);
			}else if (action_inf->action == ACTION_REMOVE) {
				printf("    - %s\n", action_inf->file_name);
			}else if (action_inf->action == ACTION_MODIFY){
				printf("    / %s [%10d -> %10d]\n", action_inf->file_name, action_inf->old_hash, action_inf->hash);
			}
		}
		printf("\n");
		printf("    Tracked files (%ld):\n", commit->n_tracked_files);
		for (int file_idx = 0; file_idx < commit->n_tracked_files; file_idx++) {
			printf("    [%10d] %s\n", commit->tracked_files[file_idx].hash, commit->tracked_files[file_idx].file_name);
		}
	}
}

// Check if the given branch name is valid.
static int check_valid_barnch_name(char *branch_name) {
	if (branch_name == NULL) {
		return 0;
	}
	
	int branch_name_len = strlen(branch_name);
	int is_valid = 0;
	
	for (int idx = 0; idx < branch_name_len; idx++) {
		if ((branch_name[idx] >= 'a' && branch_name[idx] <= 'z') || 
		   (branch_name[idx] >= 'A' && branch_name[idx] <= 'Z') || 
		   (branch_name[idx] >= '0' && branch_name[idx] <= '9') ||
		   (branch_name[idx] == '_') ||
		   (branch_name[idx] == '/') ||
		   (branch_name[idx] == '-') 
	  	){
			is_valid = 1;
		}else {
			is_valid = 0;
			break;
		}
	}
	
	return is_valid;
}

// Fill up the branch table that keeps track of the address of the root node for each branches.
static void add_branch_table(void *helper) {
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node->prev;

	project->n_total_branch++;
	if (project->n_total_branch == 1) {
		project->branch_table = (branch_table_t *)malloc(sizeof(branch_table_t) * project->n_total_branch);
	}else {
		project->branch_table = (branch_table_t *)realloc(project->branch_table, sizeof(branch_table_t) * project->n_total_branch);
	}
	
	project->branch_table[project->n_total_branch - 1].branch_name = node->next[node->n_next_commit - 1]->branch_name;
	project->branch_table[project->n_total_branch - 1].branch_address = node->next[node->n_next_commit - 1];
}

// Check if the given branch name is already existing.
static int check_exist_branch(void *helper, char *branch_name) {
	project_t *project = (project_t*)helper;
	int is_exist = 1;
	
	for (int branch_idx = 0; branch_idx < project->n_total_branch; branch_idx++) {
		is_exist = strcmp(project->branch_table[branch_idx].branch_name, branch_name);
		if (is_exist == 0) {
			break;
		}
	}
	
	return is_exist;
}

int svc_branch(void *helper, char *branch_name) {
	// If the given branch name is NULL, return -1.
	if (branch_name == NULL) {
		return -1;
	}
	
	// If the given branch name is invalid, return -1.
	int is_valid_branch_name = check_valid_barnch_name(branch_name);
	if (is_valid_branch_name == 0) {
		return -1;
	}
	
	// If the branch name already exists, return -2.
	int is_exist = check_exist_branch(helper, branch_name);
	if (is_exist == 0) {
		return -2;
	}
	
	// If there are uncommitted changes, return -3.
	int is_changed = check_change(helper);
	if (is_changed == 1) {
		return -3;
	}
	
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node->prev;
	
	node->n_next_commit++;
	if (node->n_next_commit == 1) {
		node->next = (commit_node_t **)malloc(sizeof(commit_node_t *) * node->n_next_commit);
	}else {
		node->next = (commit_node_t **)realloc(node->next, sizeof(commit_node_t *) * node->n_next_commit);
	}
	
	node->next[node->n_next_commit - 1] = branch_node_copy(node, branch_name); // get_node_copy returns copy with 'malloc' performed
	add_branch_table(helper);
	
    return 0;
}

int svc_checkout(void *helper, char *branch_name) {
	// If branch_name is NULL, return -1.
	if (branch_name == NULL) {
		return -1;
	}
	
	// If no such branch exists, return -1.
	int check_exist = check_exist_branch(helper, branch_name);
	if (check_exist != 0) {
		return -1;
	}
	
	// If there are uncommitted changes, return -2.
	int is_changed = check_change(helper);
	if (is_changed == 1) {
		return -2;
	}
	
	project_t *project = (project_t*)helper;
	commit_node_t *current_node = project->current_node;
	int is_exist = 0;
	
	// Find the branch's root node from the branch table.
	for (int branch_idx = 0; branch_idx < project->n_total_branch; branch_idx++) {
		is_exist = strcmp(project->branch_table[branch_idx].branch_name, branch_name);
		if (is_exist == 0) {
			current_node = project->branch_table[branch_idx].branch_address;
		}
	}
	
	// Set current node to the staging area of the branch.
	while (current_node->commit_id != NULL) {
		if (current_node->next[0] != NULL) {
			current_node = current_node->next[0];
		}
	}
	
	// Make it the active branch.
	project->current_node = current_node;
	
    return 0;
}

char **list_branches(void *helper, int *n_branches) {
	// If n_branches is NULL, return NULL
	if (n_branches ==NULL) {
		return NULL;
	}
	
	project_t *project = (project_t*)helper;
	char **list_branches = NULL;
	size_t total_branch = project->n_total_branch;
	
	int malloc_size = (sizeof(char*) * total_branch);
	list_branches = malloc(malloc_size);
	
	for (int branch_idx = 0; branch_idx < total_branch; branch_idx++) {
		char * branch_name = project->branch_table[branch_idx].branch_name;
		printf("%s\n", branch_name);
		list_branches[branch_idx] = branch_name;
	}
	*n_branches = total_branch;
	
	return list_branches;
}

int svc_add(void *helper, char *file_name) {
	// If file_name is NULL, return -1 and do not add it to version control.
	if (file_name == NULL) {
		return -1;
	}
	
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;

	// If a file with this name is already being tracked in the current branch, return -2.
	if (node->tracked_files != NULL) {
		for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
			int is_exist = strcmp(node->tracked_files[file_idx].file_name, file_name);
			if (is_exist == 0) {
				return -2;
			}
		}
	}
	
	// If this file does not exist, return -3.
	FILE * fptr;	
	fptr = fopen(file_name, "r");
	if (fptr == NULL) {
		return -3;
	}
	fclose(fptr);
	
	node->n_tracked_files++;
	if (node->n_tracked_files == 1) {
		node->tracked_files = (tracked_file_t*)malloc(sizeof(tracked_file_t) * node->n_tracked_files);
	}else {
		node->tracked_files = (tracked_file_t*)realloc(node->tracked_files, sizeof(tracked_file_t) * node->n_tracked_files);
	}
	tracked_file_t *tracked_files = &node->tracked_files[node->n_tracked_files - 1];
	tracked_files->file_name = malloc(sizeof(char) * FILE_NAME_LEN);
	strcpy(tracked_files->file_name, file_name);
	tracked_files->content = file_content_copy(file_name);
	unsigned int hash = hash_file(helper, file_name);
	tracked_files->hash = hash;
	
	return hash;
}

int svc_rm(void *helper, char *file_name) {
	// If file_name is NULL, return -1.
	if (file_name == NULL) {
		return -1;
	}
	
	project_t *project = (project_t*)helper;
	commit_node_t *node = project->current_node;
		
	// If the file with the given name is not being tracked in the current branch and SVC is not tracking any files, return -2.
	if (node->n_tracked_files == 0 && node->tracked_files == NULL) {
		return -2;
	}

	// If the file with the given name is not being tracked in the current branch, return -2. 
	if (node->tracked_files != NULL) {
		int is_exist = -1;
		for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
			is_exist = strcmp(node->tracked_files[file_idx].file_name, file_name);
			if (is_exist == 0) {
				break;
			}
		}
		if (is_exist != 0) {
			return -2;
		}
	}
	
	// Allocate memroy for temp array that holds copy of tracked files.
	tracked_file_t *temp = (tracked_file_t*)malloc(sizeof(tracked_file_t) * node->n_tracked_files);
	// Index of the file that will be removed.
	int matched_file_index = -1;
	// Keep the number of tracked files before removing the file.
	int original_len = node->n_tracked_files;
	unsigned int last_knwon_hash = 0;
	
	// Find index of the file that will be removed and keep its hash value.
	for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
		matched_file_index = strcmp(node->tracked_files[file_idx].file_name, file_name);
		if (matched_file_index == 0) {
			matched_file_index = file_idx;
			last_knwon_hash = node->tracked_files[file_idx].hash;
			// Free file name.
			free(node->tracked_files[file_idx].file_name);
			// Free file content.
			free(node->tracked_files[file_idx].content);
			break;
		}
	}
	
	// Copy tracked files into the temp array.
	for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
		memcpy(&temp[file_idx], &project->current_node->tracked_files[file_idx], sizeof(tracked_file_t));
	}

	// Reallocate a new tracked files array.
	node->n_tracked_files--;
	node->tracked_files = (tracked_file_t*)realloc(node->tracked_files, sizeof(tracked_file_t) * node->n_tracked_files);	
	int file_idx = 0;
	
	// Copy temp array into the new tracked files array except the index of the file that will be removed.
	if (node->n_tracked_files > 0) {
		for (int temp_idx = 0; temp_idx < original_len; temp_idx++) {
			if (temp_idx == matched_file_index) {
				continue;
			}
			memcpy(&project->current_node->tracked_files[file_idx], &temp[temp_idx], sizeof(tracked_file_t));
			file_idx++;
		}
	}
	free(temp);
	
    return last_knwon_hash;
}

int svc_reset(void *helper, char *commit_id) {
	// If commit_id is NULL, return -1.
	if (commit_id == NULL) {
		return -1;
	}
	
	commit_node_t *commit = get_commit(helper, commit_id);
	
	// If no commit with the given id exists, return -2. 
	if (commit == NULL) {
		return -2;
	}

	project_t *project = (project_t*)helper;
	project->current_node = commit;
	
    return 0;
}

char *svc_merge(void *helper, char *branch_name, struct resolution *resolutions, int n_resolutions) {
	project_t *project = (project_t*)helper;
	
	if (branch_name == NULL) {
		printf("Invalid branch name\n");
		return NULL;
	}
	
	int check_exist = check_exist_branch(helper, branch_name);
	if (check_exist != 0) {
		printf("Branch not found\n");
		return NULL;
	}
	
	char *cur_branch = project->current_node->branch_name;
	int check_cur_branch = strcmp(cur_branch, branch_name);
	if (check_cur_branch == 0) {
		printf("Cannot merge a branch with itself\n");
		return NULL;
	}
	
	int is_changed = check_change(helper);
	if (is_changed == 1) {
		printf("Changes must be committed\n");
		return NULL;
	}
	
    return NULL;
}

void dump_node(commit_node_t *node) {
	if (node == NULL) {
		return;
	}

	printf("Commit[%s]: %s\n", node->commit_id, node->message == NULL ? "null" : node->message);
	printf("branch: %s\n", node->branch_name);
	//printf("address: %p\n", node);
	for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
		const tracked_file_t *file = &node->tracked_files[file_idx];
		printf("	File[%d]: [Hash:%04u] %s\n", file_idx, file->hash, file->file_name);
	}
	
	
	for (int action_idx = 0; action_idx < node->n_actions; action_idx++) {
		const action_info_t *action_inf = &node->actions[action_idx];
		char action_char = '+';
		if (action_inf->action == ACTION_REMOVE) {
			action_char = '-';
		} else if (action_inf->action == ACTION_MODIFY) {
			action_char = '/';
		}

		printf("	Act[%d]: %c %s\n", action_idx, action_char, action_inf->file_name);
	}
	printf("\n");
	
	for (int next_idx = 0; next_idx < node->n_next_commit; next_idx++) {
		dump_node(node->next[next_idx]);
	}

}

void dump_head(commit_node_t *node) {
	if (node == NULL) {
		return;
	}

	printf("Commit[%s]: %s\n", node->commit_id, node->message == NULL ? "null" : node->message);
	printf("branch: %s\n", node->branch_name);
	
	for (int file_idx = 0; file_idx < node->n_tracked_files; file_idx++) {
		const tracked_file_t *file = &node->tracked_files[file_idx];
		printf("	File[%d]: [Hash:%04u] %s\n", file_idx, file->hash, file->file_name);
	}
	
	
	for (int action_idx = 0; action_idx < node->n_actions; action_idx++) {
		const action_info_t *action_inf = &node->actions[action_idx];
		char action_char = '+';
		if (action_inf->action == ACTION_REMOVE) {
			action_char = '-';
		} else if (action_inf->action == ACTION_MODIFY) {
			action_char = '/';
		}

		printf("	Act[%d]: %c %s\n", action_idx, action_char, action_inf->file_name);
	}
	printf("\n");
}

void dump(project_t *helper) {
	printf("=====================================\n");
	commit_node_t *node = ((project_t *)helper)->root_node;
	dump_node(node);
	
	//printf("HEAD:\n");
	//dump_head(helper->head);
	
	printf("\n");
	
}

int main(){
	void *helper = svc_init();
	project_t *project = helper;
	
	int a = svc_add(helper, "hello.py");
	int b = svc_add(helper, "Tests/test1.in");
	svc_commit(helper, "Initial commit");

	//int d = remove("hello.py"); // REMOVE MANUALLY
	//int c = svc_rm(helper, "Tests/test1.in");
	//svc_commit(helper, "removed test1.in");
	
	//svc_commit(helper, "added test1.in");
	
	//svc_commit(helper, "Initial commit1332333");

	//print_commit(helper, "74cde7");
	//print_commit(helper, "00006f");
	
	/* MODIFY FILE TEST CASE
   int num=5;
   FILE *fptr;

   // use appropriate location if you are using MacOS or Linux
   fptr = fopen("hello.py","w");

   if(fptr == NULL)
   {
      printf("Error!");   
      exit(1);             
   }

   fprintf(fptr,"%d",num);
   fclose(fptr);
	*/
	
	//commit_node_t *commit = get_commit(helper, "d85ba1");
	//int prev_n;
	//char **prev_commit_ids = get_prev_commits(helper, commit, &prev_n);
	
	// Dump prev_commit_ids
	/*
	printf("prev_n=%d\n", prev_n);
	for (int i = 0; i < prev_n; i++) {
		printf("prev[%d]: %s\n", i, prev_commit_ids[i]);
	}
	*/
	
	//dump(helper);
	
	//svc_branch(helper, "feature");
	//svc_checkout(helper, "feature");	

	
	//int c = svc_rm(helper, "Tests/test1.in");
	//svc_commit(helper, "feature branch removed test1.in");
	
	//int d = svc_add(helper, "Tests/test1.in");
	//svc_commit(helper, "feature branch add test1.in");
	
	//svc_checkout(helper, "master");	
	//int d = svc_add(helper, "Tests/test1.in");
	//svc_commit(helper, "master branch added test1.in");

	dump(helper);

	//free(prev_commit_ids);
	cleanup(helper);
}
