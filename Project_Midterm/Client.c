/****************************************************
 *													*
 *     BIL244 System Programming Midterm Project    *
 * 					 Client.c						*
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

#define MAX_NAME_SIZE		255
#define MAX_FUNCTION_SIZE	1024
#define MAIN_FIFO_NAME_CS	"ClientToServerFifo"
#define MAIN_FIFO_NAME_SC	"ServerToClientFifo"
#define LOG_NAME			"./CLIENT_LOGS/log_%ld"
#define SIGUSR3 			SIGURG

FILE *clientLog;
char CLIENT_LOG_NAME[MAX_NAME_SIZE] = "\0";
char childToClientFifo[MAX_NAME_SIZE] = "\0";
char clientToChildFifo[MAX_NAME_SIZE] = "\0";
pid_t clientPid = 0, childServerPid = 0;

void getArgumentValues(int argc, char **argv, char *fi, char *fj, double *time, char *op);

void startSignals();
void client_SIGINT_handler(int sig);
void client_SIGQUIT_handler(int sig);
void client_SIGFPE_handler(int sig);
void client_SIGTERM_handler(int sig);
void client_SIGUSR1_handler(int sig);
void client_SIGUSR2_handler(int sig);
void client_SIGUSR3_handler(int sig);

int main(int argc, char **argv)
{
	char fi[MAX_FUNCTION_SIZE] = "\0";
	char fj[MAX_FUNCTION_SIZE] = "\0";
	char combinedFunc[MAX_FUNCTION_SIZE] = "\0";
	char operation[2] = "\0";
	double resolution = 0.0;
	double time_interval = 0.0;
	double clientConnectTime = 0.0;
	double integralResult = 0.0;
	clientPid = getpid();

	sprintf(CLIENT_LOG_NAME, LOG_NAME, (long)clientPid);
	clientLog = fopen(CLIENT_LOG_NAME, "w");

	getArgumentValues(argc, argv, fi, fj, &time_interval, operation);

	int clientToServerFD, serverToClientFD;
	if((clientToServerFD = open(MAIN_FIFO_NAME_CS, O_RDWR)) == -1)
	{
		fprintf(clientLog, "Client[%ld] failed to open clientToServer", (long)clientPid);
		fprintf(clientLog, " for writing!\n");
		fclose(clientLog);
		exit(1);
	}

	write(clientToServerFD, &clientPid, sizeof(pid_t));
	close(clientToServerFD);

	if((serverToClientFD = open(MAIN_FIFO_NAME_SC, O_RDWR)) == -1)
	{
		fprintf(clientLog, "Client[%ld] failed to open serverToClient", (long)clientPid);
		fprintf(clientLog, " for reading!\n");
		fclose(clientLog);
		exit(1);
	}	

	read(serverToClientFD, &childServerPid, sizeof(pid_t));
	close(serverToClientFD);
	
	if(childServerPid == -1)
	{
		fprintf(clientLog, "Server has reached the maximum client number!\n");
		fprintf(clientLog, "Your request is cancelled!\n");
		fprintf(clientLog, "<~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~>\n");
		exit(1);
	}

	sprintf(childToClientFifo, "./FIFOS/ch_to_cl_%lu", (long)childServerPid);
	sprintf(clientToChildFifo, "./FIFOS/cl_to_ch_%lu", (long)childServerPid);	

	sleep(1);

	int clientToChildFD, childToClientFD;
	if((clientToChildFD = open(clientToChildFifo, O_RDWR)) == -1)
	{
		fprintf(clientLog, "Client[%ld] failed to open clientToChild", (long)clientPid);
		fprintf(clientLog, " for writing!\n");
		fclose(clientLog);
		exit(1);
	}

	write(clientToChildFD, fi, MAX_NAME_SIZE*sizeof(char));
	write(clientToChildFD, fj, MAX_NAME_SIZE*sizeof(char));
	write(clientToChildFD, operation, 2*sizeof(char));
	write(clientToChildFD, &time_interval, sizeof(double));
	close(clientToChildFD);

	if((childToClientFD = open(childToClientFifo, O_RDWR)) == -1)
	{
		fprintf(clientLog, "Client[%ld] failed to open childToClient", (long)clientPid);
		fprintf(clientLog, " for reading!\n");
		fclose(clientLog);
		exit(1);	
	}

	read(childToClientFD, combinedFunc, MAX_FUNCTION_SIZE*sizeof(char));
	read(childToClientFD, &resolution, sizeof(double));
	read(childToClientFD, &clientConnectTime, sizeof(double));

	fprintf(clientLog, "The integral of %s\n", combinedFunc);
	fprintf(clientLog, "Time interval: %.3lf(s)\n", time_interval);
	fprintf(clientLog, "Client Connection time: %.3lf(s)\n", clientConnectTime);
	fprintf(clientLog, "Resolution: %.12lf(s)\n", resolution);
	fprintf(clientLog, "<~~~~~~~~~~~~~~~~~~~~RESULTS~~~~~~~~~~~~~~~~~~~~>\n");
	fclose(clientLog);

	double upperLimit = 0.0, lowerLimit = 0.0;

while(1)
{
	signal(SIGINT, client_SIGINT_handler);
	signal(SIGFPE, client_SIGFPE_handler);
	signal(SIGUSR1, client_SIGUSR1_handler);
	signal(SIGUSR2, client_SIGUSR2_handler);
	signal(SIGUSR3, client_SIGUSR3_handler);
	signal(SIGTERM, client_SIGTERM_handler);

	if(read(childToClientFD, &integralResult, sizeof(double)) > 0)
	{
		read(childToClientFD, &lowerLimit, sizeof(double));
		read(childToClientFD, &upperLimit, sizeof(double));

		clientLog = fopen(CLIENT_LOG_NAME, "a");
		fprintf(clientLog, "lowerLimit[%.3lf], upperLimit[%.3lf]\n", lowerLimit, upperLimit);
		fprintf(clientLog, "Has result[%.6lf]\n", integralResult);
		fclose(clientLog);
	}
}

	return 0;
}

void getArgumentValues(int argc, char **argv, char *fi, char *fj, double *time, char *op)
{
	if(argc != 5 || argv[1][0] != '-' || argv[2][0] != '-' 
				 || argv[3][0] != '-' || argv[4][0] != '-')
	{
		fprintf(stderr, "Error: Illegal command!\n");
		fprintf(stderr, "Usage: ./Client -<fi> -<fj> -<time_interval> -<operation>\n");
		fprintf(stderr, "Example: ./Client -f2 -f3 -5 -*\n");
		exit(1);
	}
	if(strlen(argv[1]) > 3 || strlen(argv[2]) > 3)
	{
		fprintf(stderr, "Error: Illegal command!\n");
		fprintf(stderr, "You shouldn't enter the type of the input files!\n");
		exit(1);
	}
	if(atoi(&argv[1][2]) < 1 || atoi(&argv[1][2]) > 6 ||
	   atoi(&argv[2][2]) < 1 || atoi(&argv[2][2]) > 6)
	{
		fprintf(stderr, "Error: Illegal command!\n");
		fprintf(stderr, "You can use only f1-f6 as input files name!\n");
		exit(1);
	}

	strcpy(fi, &argv[1][1]);
	strcpy(fj, &argv[2][1]);
	*time = atof(&argv[3][1]);
	strcpy(op, &argv[4][1]);

	if(strcmp(op, "+") != 0 && strcmp(op, "-") != 0 && strcmp(op, "*") != 0 && strcmp(op, "/") != 0)
	{
		fprintf(stderr, "Error: %s is not an operation!\n", op);
		fprintf(stderr, "Available operations: <+> <-> </> <*>\n");
		exit(1);
	}

}

void client_SIGINT_handler(int sig)
{
	FILE *fLog;
	fLog = fopen(CLIENT_LOG_NAME, "a");
	fprintf(fLog, "<CTRL-C> occured for the client!\n");
	fprintf(fLog, "<~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~>\n");
	fclose(fLog);

	fprintf(stdout, "\nClient has got a SIGINT signal!\n");
    exit(1);
}
void client_SIGFPE_handler(int sig)
{
	FILE *fLog;
	fLog = fopen(CLIENT_LOG_NAME, "a");
	fprintf(fLog, "ERROR: Division by zero occured!\n");
	fprintf(fLog, "<~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~>\n");
	fclose(fLog);

    fprintf(stdout, "\nClient has got a SIGFPE signal!\n");
    exit(1);
}
void client_SIGTERM_handler(int sig)
{
	FILE *fLog;
	fLog = fopen(CLIENT_LOG_NAME, "a");
	fprintf(fLog, "Server stopped respoding!\n");
	fprintf(fLog, "<~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~>\n");
	fclose(fLog);

    fprintf(stdout, "\nClient[%ld] has got a SIGTERM signal!\n", (long)getpid());
    exit(1);	
}
void client_SIGUSR1_handler(int sig)
{
	FILE *fLog;
	fLog = fopen(CLIENT_LOG_NAME, "a");
	fprintf(fLog, "ERROR: Resolution is bigger than expected!\n");
	fprintf(fLog, "<~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~>\n");
	fclose(fLog);

    fprintf(stdout, "\nClient has got a SIGUSR1 signal!\n");
    exit(1);
}
void client_SIGUSR2_handler(int sig)
{
	FILE *fLog;
	fLog = fopen(CLIENT_LOG_NAME, "a");
	fprintf(fLog, "ERROR: Syntax error in the integrating function or missing file function!\n");
	fprintf(fLog, "<~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~>\n");
	fclose(fLog);

    fprintf(stdout, "\nClient has got a SIGUSR1 signal!\n");
    exit(1);	
}
void client_SIGUSR3_handler(int sig)
{
	FILE *fLog;
	fLog = fopen(CLIENT_LOG_NAME, "a");
	fprintf(fLog, "ERROR: Imaginary number at some point occured!\n");
	fprintf(fLog, "<~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~>\n");
	fclose(fLog);

    fprintf(stdout, "\nClient[%ld] has got a SIGUSR3 signal!\n", (long)getpid());
    exit(1);
	
}











