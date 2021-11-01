/*	
*	HOWTO:
*	Run make to compile
*	Usage: appserver <# of worker threads> <# of accounts> <output file name>
*	Program should run as expected
*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#define BUFFER_SIZE 64

typedef struct node {
	int id;
	int tokens;
	char *data[21];
	struct timeval start;
	struct node *next;
} node_t;

typedef struct account {
	pthread_mutex_t lock;
	int value;
} account_t;

//gloabals
account_t *accounts;
pthread_mutex_t list_lock;
pthread_mutex_t worker_lock;
node_t *head = NULL;
char *line;
char **args;
int arg_count;
int id = 1;
FILE *file;
struct timeval request_time;

//worker thread prototype
void *worker(void *);

int main(int argc, char *argv[]) {
	int i;
	//allocate space for accounts array for locking
	int num_accounts = (int)strtol(argv[2], (char **)NULL, 10);
	accounts = malloc(num_accounts * sizeof(account_t));
	//initialize all mutex
	pthread_mutex_init(&worker_lock, NULL);
	pthread_mutex_init(&list_lock, NULL);
	for(i = 0; i < num_accounts; i++) {
		pthread_mutex_init(&accounts[i].lock, NULL);
		accounts[i].value = i + 1;
	}
	//initialize head
	head = malloc(sizeof(node_t));
	head->next = NULL;
	//initialize all 
	initialize_worker_threads((int)strtol(argv[1], (char **)NULL, 10));
	initialize_accounts(num_accounts);
	file = fopen(argv[3], "w");
	initialize_console();
	fclose(file);
	free(head);
	return 0;
}

//create n amount of worker threads
void initialize_worker_threads(int n) {
	pthread_t worker_tid[n];
  	int thread_index[n];
	int i;
	pthread_t tid_worker;
	for(i = 0; i < n; i++) {
		thread_index[i] = i;
		pthread_create(&worker_tid[i], NULL, worker, (void*)&thread_index[i]);
	}
}

//worker thread
void *worker(void *arg) {
	node_t *request_node = NULL;
	while(1) {
		//lock
		pthread_mutex_lock(&worker_lock);
		if(head->next != NULL) {
			//copy first request to temp node
			request_node = malloc(sizeof(node_t));
			copy_node(request_node, head->next);
			//free frist request on linked list
			pthread_mutex_lock(&list_lock);
			pop();
			pthread_mutex_unlock(&list_lock);
			//check for CHECK or TRANS
			if(strcmp(request_node->data[0], "CHECK") == 0) {
				check_balance(request_node);
			} else if (strcmp(request_node->data[0], "TRANS") == 0) {
				perform_transaction(request_node);
			}
			//free temp node
			free(request_node);
		}
		//unlock
		pthread_mutex_unlock(&worker_lock);
	}	
}

//free first element of list and connect head to the next element
void pop() {
	node_t *next_node = head->next;
	head->next = head->next->next;
	free(next_node);
}

//performs a full copy to node n1 or node n2
void copy_node(node_t *n1, node_t *n2) {
	int i;
	n1->id = n2->id;
	n1->tokens = n2->tokens;
	for(i = 0; i < n2->tokens; i++) {
		n1->data[i] = malloc(sizeof(n2->data[i]));
		strcpy(n1->data[i], n2->data[i]);
	}
	n1->start = n2->start;
	n1->next = n2->next;
}

//performs the CHECK requests
void check_balance(node_t *n) {
	int account = (int)strtol(n->data[1], (char **)NULL, 10);
	pthread_mutex_lock(&accounts[account-1].lock);
	int result = read_account(account);
	pthread_mutex_unlock(&accounts[account-1].lock);
	gettimeofday(&request_time, NULL);
	fprintf(file, "%d BAL %d TIME %d.%06d %d.%06d\n", n->id, result, n->start.tv_sec, n->start.tv_usec, request_time.tv_sec, request_time.tv_usec);
}

//performs the TRANS requests
int perform_transaction(node_t *n) {
	int i = 0;
	int isf = 0;
	int account;
	int balance;
	int amount;
	//checks for isf
	while(i < n->tokens-2 && !isf) {
		account = (int)strtol(n->data[i+1], (char **)NULL, 10);
		pthread_mutex_lock(&accounts[account-1].lock);
		balance = read_account(account);
		pthread_mutex_unlock(&accounts[account-1].lock);
		amount = (int)strtol(n->data[i+2], (char **)NULL, 10);
		if(balance + amount < 0) {
			gettimeofday(&request_time, NULL);
			fprintf(file, "%d ISF %d TIME %d.%06d %d.%06d\n", n->id, account, n->start.tv_sec, n->start.tv_usec, request_time.tv_sec, request_time.tv_usec);
			isf = 1;						
		}
		i+=2;
	}
	//writes to accounts if no isf
	if(!isf) {
		for(i = 0; i < n->tokens-2; i+=2) {
			account = (int)strtol(n->data[i+1], (char **)NULL, 10);
			amount = (int)strtol(n->data[i+2], (char **)NULL, 10);
			pthread_mutex_lock(&accounts[account-1].lock);
			balance = read_account(account);
			write_account(account, balance + amount);
			pthread_mutex_unlock(&accounts[account-1].lock);
		}
		gettimeofday(&request_time, NULL);
		fprintf(file, "%d OK TIME %d.%06d %d.%06d\n", n->id, n->start.tv_sec, n->start.tv_usec, request_time.tv_sec, request_time.tv_usec);
	}
}

//console loop
void initialize_console() {
	int status;	
	do {
		printf("> ");
		read_line();
		parse_line();
		status = launch();
		free(line);
		free(args);
	} while(status);
}

//reads user input
void read_line() {
	ssize_t buffer = 0;
	getline(&line, &buffer, stdin);
}

//seperates the user input into tokens and stores them in args
void parse_line() {
	char *pos;
	char *token = strtok(line, " ");
	//allocates space for the arguments
	args = malloc(BUFFER_SIZE * sizeof(char*));
	arg_count = 0;
	while(token != NULL) {
		//elimates new line characters from the tokens
		if((pos = strchr(token, '\n')) != NULL) {
			*pos = '\0';
		}
		args[arg_count] = token;
		arg_count++;
		token = strtok(NULL, " ");
	}
	args[arg_count] = NULL;
	arg_count;
}

//check console input for request or END
int launch() {
	if(strcmp(args[0], "CHECK") == 0 || strcmp(args[0], "TRANS") == 0) {
		if(arg_count > 21) {
			printf("< TOO MANY ARGUMENTS\n");
			return 1;
		}
		gettimeofday(&request_time, NULL);
		printf("< ID %d\n", id);
		pthread_mutex_lock(&list_lock);
		//push request
		push(id, arg_count, args, request_time);
		pthread_mutex_unlock(&list_lock);
		id++;
		return 1;
	} else if(strcmp(args[0], "END") == 0) {
		return 0;
	} else {
		printf("< INVALID REQUEST\n");
		return 1;
	}
}

//push request to the list
void push(int id, int tokens, char **data, struct timeval start) {
	int i;
	node_t *current = head;
	//finds last node
	while(current->next != NULL) {
		current = current->next;
	}
	//creats next node after the last
	current->next = malloc(sizeof(node_t));
	current->next->id = id;
	current->next->tokens = tokens;
	for(i = 0; i < tokens; i++) {
		current->next->data[i] = malloc(sizeof(data[i]));
		strcpy(current->next->data[i], data[i]);
	}
	current->next->start = start;
	current->next->next = NULL;
}

