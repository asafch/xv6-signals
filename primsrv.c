#include "user.h"



int numOfIdleChildren;
int n;
int table[3][62];//this table holds the idle childs   (pid, idle=0/busy=1, value from shell)


int
findPrim(int value){
	int sol = value;
	int i;

	while(1){
		sol++;
		for (i = 2; i < sol; i++){
			if (sol%i==0)
				break;
		}
		if (i==sol)//we didnt find any divder meaning sol is a prim
			return sol;
	}
}


void //sigHandler of the children workerking
workerPrimHandler(int fatherPid, int value){
	if (value == 0){
		printf(1, "worker %d exits\n", (getpid()) );
		exit();
	}
	int primNum = findPrim(value);
	sigsend(fatherPid, primNum);
}



void //sig handler for the primsrv
fatherHandler(int childPid, int value){
	int i;
	for (i = 0; i < n; i++){
		if (table[0][i] == childPid)
			break; //we found the column of this child
	}
	numOfIdleChildren++;
	printf(1, "**  worker %d returned %d as a resault for %d  ***\n", childPid, value, table[2][i]);
	table[1][i] = 0; //child is idle again;
}


int
main(int argc, char *argv[])
{
	if (argc != 2){
				printf(1, "Usage: primsrv <n>\n");
				exit();
 		}
	sigset( (sig_handler)&workerPrimHandler ); //this handler will be used by all the children
	n = atoi(argv[1]);

	numOfIdleChildren = n;
	int i;
	if (n>62){
				printf(1, "62 is the maximum num of children!\n");
				exit();
 		}
	int numRead;
	char buf[128];

	printf(1, "workers pids:\n");
	for (i = 0; i < n; i++){
			table[0][i] = fork();
			if (table[0][i] == 0) { // child
				while(1)
					sigpause();	//the handler itself suposed to exit

				printf(1, "ERROR!!!!!!!!!!!!!!!!!!!!!\n");
				exit(); //children wxit here?
			}

			else{ //father
				table[1][i] = 0; //child is idle
				printf(1, "pid: %d\n", table[0][i]);
			}
	}
	printf(1, "				***       \n");

	//father code;
	sigset((sig_handler)&fatherHandler);

	while(1){
		printf(1, "please enter a number:");
		gets (buf, 128);
		if(strlen(buf)==1 && buf[0]=='\n')
			continue; //used to wake up the process

		numRead = atoi(buf);
		if (numRead ==0)
			break;//preparing to exit

		if (numOfIdleChildren==0){
			printf(1, "no idle workers\n");
			continue;
		}
		for (i = 0; i < n; i++){
			if (table[1][i] == 0){//found idle child
				table[1][i] = 1;
				table[2][i] = numRead;
				numOfIdleChildren--;
				sigsend(table[0][i], numRead);
				break;
			}
		}
	}


	for (i = 0; i < n; i++){
		sigsend(table[0][i], 0);
	}
	for (i = 0; i < n; i++){
		if(table[1][i] == 1)//its still busy
			sigpause();
		wait();
	}

	printf(1, "primesrv exit!!:-)\n");
	exit();
}
