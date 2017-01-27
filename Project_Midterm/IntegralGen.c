/****************************************************
 *													*
 *     BIL244 System Programming Midterm Project    *
 * 					 IntegralGen.c					*
 * 			Student Name: Mehmed Mustafa            *
 * 			Student ID  : 131044084                 *
 * 			Date        : 15/04/2016                *
 *                                                  *
 ****************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <matheval.h>
#include <math.h>

#define MAX_NAME_SIZE		255
#define MAX_FUNCTION_SIZE	1024
#define MAIN_FIFO_NAME_CS	"ClientToServerFifo"
#define MAIN_FIFO_NAME_SC	"ServerToClientFifo"
#define SERVER_LOG_NAME		"ServerLog"
#define SIGUSR3 			SIGURG

typedef struct FunctionFiles{
	char fileName[MAX_NAME_SIZE];
	char function[MAX_FUNCTION_SIZE];
} FunctionFile_t;

typedef struct Functions{
	char fi[MAX_NAME_SIZE];
	char fj[MAX_NAME_SIZE];
	char funcI[MAX_FUNCTION_SIZE];
	char funcJ[MAX_FUNCTION_SIZE];
	char operation[2];
	char combinedFunc[MAX_FUNCTION_SIZE];
} Functions_t;

FunctionFile_t functions[7];
pid_t *childPids;
pid_t childServerPid = 0, clientPid = 0, integralGenPid = 0;
int iChildCounter = 0;
int iClientCounter = 0;
int maxNumberOfClients = 0;
void* func = NULL;
FILE *serverLog;

void initializeChildArray();
int addChildPid(pid_t childPid);
int removeChildPid(pid_t childPid);
void killAllChilds();
void quit(int result);

void getArgumentValues(int argc, char **argv, double *resolution, int *maxClients);
void readLine(FILE *file, char *buffer);
void readFunctionsFromFiles(FunctionFile_t *functions);
void getFunctionFromStruct(char *fileName, char *func);

void combineFunctions(char *func1, char *func2, char *operator, char *combined);
void exchangeTwithX(char *function);
int isOperator(char value);

double solveIntegral(void *func, double upperLimit, double lowerLimit, double resolution,  int *error);

/* Signal Handlers */
void server_SIGINT_handler(int sig);
void server_SIGTERM_handler(int sig);

int main(int argc, char **argv)
{
	time_t serverStartingTime = time(NULL);
	struct tm *sTimeL = localtime(&serverStartingTime);
	struct tm *cTimeL;
	double resolution = 0.0; 
	double clientConnectTime = 0.0;
	double integralResult = 0.0;
	integralGenPid = getpid();
	pid_t error = -1;
	int i = 0;

	getArgumentValues(argc, argv, &resolution, &maxNumberOfClients);
	readFunctionsFromFiles(functions);

	if(mkfifo(MAIN_FIFO_NAME_CS, 0666) == -1 || mkfifo(MAIN_FIFO_NAME_SC, 0666) == -1)
	{
		fprintf(stdout, "IntegralGen failed to create main fifos!\n");
		fprintf(stdout, "Error[%s]\n", strerror(errno));		
		exit(1);
	}

	if((serverLog = fopen(SERVER_LOG_NAME, "a")) == NULL)
	{
		fprintf(stdout, "IntegralGen failed to open log file!\n");
		exit(1);
	}

	system("clear");
	fprintf(stdout, "IntegralGen started at: %s", asctime(sTimeL));
	fprintf(stdout, "IntegralGen: Waiting for a client.\n");
	fprintf(serverLog, "IntegralGen started at: %s", asctime(sTimeL));
	fprintf(serverLog, "IntegralGen: Waiting for a client.\n");
	fclose(serverLog);

	int clientToServerFD = 0, serverToClientFD = 0;
	if((clientToServerFD = open(MAIN_FIFO_NAME_CS, O_RDWR)) == -1)
	{
		fprintf(stdout, "IntegralGen failed to open clientToServer for reading!\n");
		fprintf(stdout, "Error[%s]\n", strerror(errno));
		exit(1);
	}
	if((serverToClientFD = open(MAIN_FIFO_NAME_SC, O_RDWR)) == -1)
	{
		fprintf(stdout, "IntegralGen failed to open serverToClient for writing!\n");
		fprintf(stdout, "Error[%s]\n", strerror(errno));
		exit(1);
	}

	childPids = (pid_t *)malloc(maxNumberOfClients*sizeof(pid_t));
	initializeChildArray();

	while(1)
	{
		signal(SIGINT, server_SIGINT_handler);

		if(read(clientToServerFD, &clientPid, sizeof(pid_t)) > 0)
		{
			time_t currentTime = time(NULL);
			cTimeL = localtime(&currentTime);
			clientConnectTime = (double)(currentTime - serverStartingTime);
			if(iClientCounter >= maxNumberOfClients)
			{
				fprintf(stdout, "IntegralGen: I have reached my client limit!\n");
				fprintf(stdout, "IntegralGen: Client request canceled!\n");

				write(serverToClientFD, &error, sizeof(pid_t));
			}
			else
			{
				fprintf(stdout, "%2d. Client[%ld]", ++iClientCounter, (long)clientPid);
				fprintf(stdout, " has been connected ");
				fprintf(stdout, "at: %s", asctime(cTimeL));
				serverLog = fopen(SERVER_LOG_NAME, "a");
				fprintf(serverLog, "%2d. Client[%ld]", iClientCounter, (long)clientPid);
				fprintf(serverLog, " has been connected ");
				fprintf(serverLog, "at: %s", asctime(cTimeL));
				fclose(serverLog);

				if((childServerPid = fork()) == -1)
				{
					fprintf(stdout, "IntegralGen failed to fork!\n");
					exit(1);
				}
				if(childServerPid == 0)
				{
					free(childPids); /* Free allocated memory, child doesn't need it */
					childPids = NULL;

					pid_t clientPidC = clientPid;
					pid_t myPid = getpid();
					char childToClientFifo[MAX_NAME_SIZE] = "\0";
					char clientToChildFifo[MAX_NAME_SIZE] = "\0";

					sprintf(childToClientFifo, "./FIFOS/ch_to_cl_%lu", (long)myPid);
					sprintf(clientToChildFifo, "./FIFOS/cl_to_ch_%lu", (long)myPid);

					if(mkfifo(childToClientFifo, 0666) == -1 || 
						mkfifo(clientToChildFifo, 0666) == -1)
					{
						fprintf(stdout, "Child failed to create his fifos!\n");
						fprintf(stdout, "Error[%s]\n", strerror(errno));
						exit(1);
					}

					int clientToChildFD = 0, childToClientFD = 0;
					if((clientToChildFD = open(clientToChildFifo, O_RDWR)) == -1)
					{
						fprintf(stdout, "Child failed to open clientToChild");
						fprintf(stdout, " for reading!\n");
						fprintf(stdout, "Error[%s]\n", strerror(errno));
						exit(1);	
					}

					Functions_t f;
					strcpy(f.fi, ""); strcpy(f.fj, ""); 
					strcpy(f.funcI, ""); strcpy(f.funcJ, "");
					strcpy(f.combinedFunc, ""); strcpy(f.operation, "");;

					double time_interval = 0.0;

					read(clientToChildFD, f.fi, MAX_NAME_SIZE*sizeof(char));
					read(clientToChildFD, f.fj, MAX_NAME_SIZE*sizeof(char));
					read(clientToChildFD, f.operation, 2*sizeof(char));
					read(clientToChildFD, &time_interval, sizeof(double));
					close(clientToChildFD);

					if((childToClientFD = open(childToClientFifo, O_RDWR)) == -1)
					{
						fprintf(stdout, "Child failed to open childToClient");
						fprintf(stdout, " for writing!\n");
						fprintf(stdout, "Error[%s]\n", strerror(errno));
						exit(1);
					}

					getFunctionFromStruct(f.fi, f.funcI);
					getFunctionFromStruct(f.fj, f.funcJ);
					combineFunctions(f.funcI, f.funcJ, f.operation, f.combinedFunc);
/* 0 errors here */			
					write(childToClientFD, f.combinedFunc, MAX_FUNCTION_SIZE*sizeof(char));
					write(childToClientFD, &resolution, sizeof(double));
					write(childToClientFD, &clientConnectTime, sizeof(double));
/* 1 errors here */
					exchangeTwithX(f.combinedFunc);

					if((func = evaluator_create(f.combinedFunc)) == NULL)
					{
						kill(clientPidC, SIGUSR2);
						exit(1);
					}
					
					double upperLimit = time_interval+clientConnectTime;
					double lowerLimit = clientConnectTime;
					double integralResult = 0.0;
					int integralError = 0;
while(1)
{
					signal(SIGTERM, server_SIGTERM_handler);
					
					integralResult = solveIntegral(func, upperLimit, lowerLimit, resolution, &integralError);
					if(integralError == -1)
						kill(clientPidC, SIGUSR1);
					else if(integralError == -2)
						kill(clientPidC, SIGFPE);
					else if(integralError == -3)
						kill(clientPidC, SIGUSR3);

					write(childToClientFD, &integralResult, sizeof(double));
					write(childToClientFD, &lowerLimit, sizeof(double));
					write(childToClientFD, &upperLimit, sizeof(double));
					lowerLimit = upperLimit;
					upperLimit += time_interval;
					
sleep(time_interval);
}

					evaluator_destroy(func);
					func = NULL;
					exit(0);
					
				}
				else // Parent
				{
					write(serverToClientFD, &childServerPid, sizeof(pid_t));
					addChildPid(childServerPid);
				}

			}
		}
	usleep(500);
	}

	free(childPids);

	close(clientToServerFD);
	close(serverToClientFD);

	unlink(MAIN_FIFO_NAME_CS);
	unlink(MAIN_FIFO_NAME_SC);
	return 0;
}

void initializeChildArray()
{
	int i;

	for(i=0; i<maxNumberOfClients; ++i)
		childPids[i] = -1;
}

int addChildPid(pid_t childPid)
{
	if(childPids == NULL)
		return 0;

	int i;

	for(i=0; i<maxNumberOfClients; ++i)
		if(childPids[i] == -1)
		{
			childPids[i] = childPid;
			++iChildCounter;
			return 1;
		}

	return 0;
}

int removeChildPid(pid_t childPid)
{
	if(childPids == NULL)
		return 0;

	int i;

	for(i=0; i<maxNumberOfClients; ++i)
		if(childPids[i] == childPid)
		{
			childPids[i] = -1;
			--iChildCounter;
			return 1;
		}

	return 0;
}

void killAllChilds()
{
	if(childPids == NULL)
		return;

	int i;
	for(i=0; i<maxNumberOfClients; ++i)
		if(childPids[i] != -1)
			kill(childPids[i], SIGTERM);
}

void quit(int result)
{
	unlink(MAIN_FIFO_NAME_CS);
	unlink(MAIN_FIFO_NAME_SC);
	exit(result);
}

void getArgumentValues(int argc, char **argv, double *resolution, int *maxClients)
{
	if(argc != 3 || argv[1][0] != '-' || argv[2][0] != '-')
	{
		fprintf(stderr, "Error: Illegal command!\n");
		fprintf(stderr, "Usage: ./IntegralGen -<resolution> -<max # of clients>\n");
		fprintf(stderr, "Example: ./IntegralGen -3.5 -5\n");
		exit(1);
	}

	*resolution = atof(&argv[1][1])/1000.0;
	*maxClients = atoi(&argv[2][1]);
}

void readLine(FILE *file, char *buffer)
{
	int lineLenght = 0;

	fgets(buffer, MAX_FUNCTION_SIZE, file);
	lineLenght = strlen(buffer);
	if(lineLenght > 0 && buffer[lineLenght-1] == '\n')
		buffer[lineLenght-1] = '\0';
}

void readFunctionsFromFiles(FunctionFile_t *functions)
{
	FILE *file1, *file2, *file3, *file4, *file5, *file6;
	int lineLenght = 0;

	strcpy(functions[1].fileName, "f1"); strcpy(functions[1].function, "NoFunction!");
	strcpy(functions[2].fileName, "f2"); strcpy(functions[2].function, "NoFunction!");
	strcpy(functions[3].fileName, "f3"); strcpy(functions[3].function, "NoFunction!");
	strcpy(functions[4].fileName, "f4"); strcpy(functions[4].function, "NoFunction!");
	strcpy(functions[5].fileName, "f5"); strcpy(functions[5].function, "NoFunction!");
	strcpy(functions[6].fileName, "f6"); strcpy(functions[6].function, "NoFunction!");

	file1 = fopen("f1.txt", "r");
	if(file1 != NULL)
	{
		readLine(file1, functions[1].function);
		fclose(file1);
	}

	file2 = fopen("f2.txt", "r");
	if(file2 != NULL)
	{
		readLine(file2, functions[2].function);
		fclose(file2);
	}

	file3 = fopen("f3.txt", "r");
	if(file3 != NULL)
	{
		readLine(file3, functions[3].function);
		fclose(file3);
	}

	file4 = fopen("f4.txt", "r");
	if(file4 != NULL)
	{
		readLine(file4, functions[4].function);
		fclose(file4);
	}

	file5 = fopen("f5.txt", "r");
	if(file5 != NULL)
	{
		readLine(file5, functions[5].function);
		fclose(file5);
	}

	file6 = fopen("f6.txt", "r");
	if(file6 != NULL)
	{
		readLine(file6, functions[6].function);
		fclose(file6);
	}
}

void getFunctionFromStruct(char *fileName, char *func)
{
	
	if(strcmp(fileName, "f1") == 0)	strcpy(func, functions[1].function);
	else if(strcmp(fileName, "f2") == 0) strcpy(func, functions[2].function);
	else if(strcmp(fileName, "f3") == 0) strcpy(func, functions[3].function);
	else if(strcmp(fileName, "f4") == 0) strcpy(func, functions[4].function);
	else if(strcmp(fileName, "f5") == 0) strcpy(func, functions[5].function);
	else if(strcmp(fileName, "f6") == 0) strcpy(func, functions[6].function);
}

void combineFunctions(char *func1, char *func2, char *operator, char *combined)
{
	char *f1 = func1, 
		 *f2 = func2, 
		 *op = operator, 
		 *com = combined;

	strcpy(com, "(");
	strcat(com, f1);
	strcat(com, ")");
	strcat(com, op);
	strcat(com, "(");
	strcat(com, f2);
	strcat(com, ")");

	combined = com;
}

void exchangeTwithX(char *function)
{
	int lenght = 0;
	int i = 0;

	lenght = strlen(function);
	for(i=0; i<lenght; ++i)
		if(function[i] == 't')
			if(i!=0 && i!=lenght-1) 
				if(isOperator(function[i-1]) || isOperator(function[i+1]))
					function[i] = 'x';
				else if(function[i-1] == '(' && function[i+1] == ')')
					function[i] = 'x';
}

int isOperator(char value)
{
	if(value == '+' || value == '-' || value == '*' || value == '/')
		return 1;

	return 0;
}

double solveIntegral(void *func, double upperLimit, double lowerLimit, double resolution, int *error)
{
	double result = 0.0;

	if(resolution > (lowerLimit+upperLimit))
	{
		*error = -1;
		return 0.0;
	}

	result += evaluator_evaluate_x(func, lowerLimit);
	lowerLimit += resolution;

	while(lowerLimit < upperLimit)
	{
		result += 2*evaluator_evaluate_x(func, lowerLimit);
		lowerLimit += resolution;
	}
	result += evaluator_evaluate_x(func, lowerLimit);
	result = result*(resolution/2.0);
	
	if(isinf(result))
	{
		*error = -2;
		return 0.0;
	}

	if(isnan(result))
	{
		*error = -3;
		return 0.0;
	}

	return result;
}

void server_SIGINT_handler(int sig)
{
	FILE *fLog;
	fLog = fopen(SERVER_LOG_NAME, "a");
	fprintf(fLog, "Server: <CTRL-C> signal has arrived. All processes will be terminated!\n");
	fclose(fLog);

	killAllChilds();

	fprintf(stdout, "\nIntegralGen has got a SIGINT signal!\n");

	free(childPids);

    quit(EXIT_FAILURE);
}
void server_SIGTERM_handler(int sig)
{
	kill(clientPid, SIGTERM);
	if(func != NULL)
		evaluator_destroy(func);

	quit(EXIT_FAILURE);
}














