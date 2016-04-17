#include "user.h"

#define BUF_SIZE 128
#define MAX_CHILDREN 61

int numOfIdleChildren;
int n;
int table[3][MAX_CHILDREN]; // for each column: row 0 is the child's pid, row 1 indicates busy\idle, row 2 is the number sent to the child via sigsend
char buf[BUF_SIZE];

int findPrim(int value){
	// int sol = value;
	int i;
	while (1) {
		value++;
		int limit = value / 2;
		for (i = 2; i < limit; i++) {
			if (value % i == 0)
				break;
		}
		if (i == limit) // value is a prime number
			return value;
	}
}

void workerPrimHandler(int fatherPid, int value){
	if (value == 0){
		printf(1, "worker %d exits\n", getpid());
		exit();
	}
	int primNum = findPrim(value);
	sigsend(fatherPid, primNum);
}

void fatherHandler(int childPid, int value){
	int i;
	for (i = 0; i < n; i++){
		if (table[0][i] == childPid)
			break;
	}
	numOfIdleChildren++;
	printf(1, "worker %d returned %d as a result for %d\n", childPid, value, table[2][i]);
	table[1][i] = 0; // mark child as idle
}

int main(int argc, char *argv[]) {
	if (argc != 2){
				printf(1, "Usage: primsrv <n>\n");
				exit();
	}
	// Set initial signal handler to be that of the children. When the children
	// are created with fork(), this signal handler is duplicated as well.
	// After spawning all children, set the father's signal handler to its
	// desired handler.
	sigset((sig_handler)&workerPrimHandler);
	n = atoi(argv[1]);
	if (n > MAX_CHILDREN)	{
				printf(1, "61 is the maximum number of children\n");
				exit();
	}
	numOfIdleChildren = n;
	int i;
	int numRead;
	printf(1, "workers pids:\n");
	for (i = 0; i < n; i++){
			table[0][i] = fork();
			if (table[0][i] == 0) { // child code
				while(1)
					sigpause();
				printf(1, "ERROR\n");
				exit();
			}
			else { // father code
				table[1][i] = 0; // mark child as idle
				printf(1, "pid: %d\n", table[0][i]);
			}
	}
	printf(1, "\n\n");
	sigset((sig_handler)&fatherHandler);

	while (1) {
		printf(1, "please enter a number: ");
		gets(buf, BUF_SIZE);
		if(strlen(buf) == 1 && buf[0] == '\n')
			continue;
		numRead = atoi(buf);
		memset(buf, '\0', BUF_SIZE);
		if (numRead == 0)
			break; // commence graceful shutdown
		if (numOfIdleChildren == 0){
			printf(1, "no idle workers\n");
			continue;
		}
		for (i = 0; i < n; i++)
			if (table[1][i] == 0){ // idle child
				table[1][i] = 1;	// mark child as busy
				table[2][i] = numRead;
				numOfIdleChildren--;
				sigsend(table[0][i], numRead);
				break;
			}
	}

	for (i = 0; i < n; i++)
		sigsend(table[0][i], 0); // signal children to self-terminate

	for (i = 0; i < n; i++){
		if(table[1][i] == 1)
			sigpause(); // wait for a result
		wait();
	}

	printf(1, "primesrv exit\n");
	exit();
}
