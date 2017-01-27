/*****************************************************************************
 *	HW6_131044084_Mehmed_Mustafa											 *
 *	System Programming - GrepFromDir using Message Queues and Shared Memory  *
 *	Date: 12.05.2016														 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#include "restart.h"
#include <sys/msg.h>
#include <sys/shm.h> 
#include <sys/ipc.h>

#define MAX_PATH_LENGHT 4096 /* Maximum number in path lenght */
#define BLKSIZE 65536
#define SHMSIZE 65536 
#define PERM (S_IRUSR | S_IWUSR)  /* Message queue and Shared memory permissions */
 /* Semaphore permissions and flags */
#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS (O_CREAT | O_EXCL)
#define SEM_NAME "MAIN_SEMAPHORE"

typedef struct threadParams{
	char *strToSearch;
	char dirPath[MAX_PATH_LENGHT];
}threadParams_t;

threadParams_t *threadDataArray = NULL; /* Thread data Array */
pthread_t threadIDs[4096]; /* Thread ID's array */
pid_t childPids[4096]; /* Child ID's array */
sem_t *semlockp = NULL;

static int thread_index = 0;
static int child_index = 0;

typedef struct Message{
	long mType;
	char mText[BLKSIZE];
}message_t;

static int messageQueueId = -1;
static int messageKeyValue = 20000; /* Starting from 20000 */

static int sharedMemoryId = -1;
static key_t sharedMemoryKeyValue = 10000; /* Starting from 10000 */
char *sharedMemoryBuffer;


void getFileParameters(FILE *inputFilePtr, int *iColumnsNumPtr, int *iLongestLineSizePtr);
int strFileCounterOcc(char *fileName, char *chStrToSearch, int *toMessageQueueId);
int strDirCounter(char *path, char *chStrToSearch);
void strDirCounterHelper(char *path, char *chStrToSearch, char *shBuffer);
int isDir(char *path);
int isRegFile(char *path);
int copyfileFromFDtoFD(int fromfd, int tofd);
int convertLogFile(FILE *fileFrom, FILE *fileTo);
void freeAndClose(threadParams_t *threadsData, DIR *dir);
int getNamedSemaphore(char *semName, sem_t **sem, int val);
int destroyNamedSemaphore(char *semName, sem_t *sem);
void signal_SIGINT_handler(int signo);

int initMessageQueue(int key, int *messageQueueId);
int writeMessage(void *buffer, int bufferLenght, int *toMessageQueueId);
int removeMessageQueue(int *messageQueueId);


int detachandremove(int shmid, void *shmaddr);

/* Thread function */
void *threadFunc(void *arg){
	threadParams_t *currFile = (threadParams_t *)arg;

	strFileCounterOcc(currFile->dirPath, currFile->strToSearch, &messageQueueId);

	currFile->strToSearch = NULL;
	currFile = NULL;
	pthread_exit(NULL);
}

int main(int argc, char *argv[]){
	int totalStrCounter = 0;

	if(argc != 3){
		printf("Wrong number of arguments[%d]!\n", argc);
		printf("Usage: ./execName dirName stringToSearch\n");
		printf("Example: ./grepfromDir ../Desktop ece\n");
		return -1;
	}

	totalStrCounter = strDirCounter(argv[1], argv[2]);
	fprintf(stdout, "%d\n", totalStrCounter);

	return 0;
}

/* Wrapper for the recursive function */
int strDirCounter(char *path, char *chStrToSearch){
	FILE *tempLog = NULL, *logOutputFile = NULL;
	int totalStrCounter = 0; /* Counter for string in the directory */
	pid_t childpid = 0;

	signal(SIGINT, signal_SIGINT_handler);

	/* Create shared memory segment */
	if((sharedMemoryId = shmget(sharedMemoryKeyValue, SHMSIZE, IPC_CREAT | 0666)) == -1){
		perror("Failed to create shared memory segment");
		return 1;
	}

	/* Attach shared memory */
	if((sharedMemoryBuffer = (char *)shmat(sharedMemoryId, NULL, 0)) == (void *)-1){
		perror("Failed to attach shared memory segment");

		if(shmctl(sharedMemoryId, IPC_RMID, NULL) == -1)
			perror("Failed to remove memory segment");

		return 1;
	}

	/* Create the named semaphore for the program */
	if(getNamedSemaphore(SEM_NAME, &semlockp, 1) == -1){
		perror("Failed to create named semaphore!\n");
		if(detachandremove(sharedMemoryKeyValue, sharedMemoryBuffer) == -1)
			perror("Failed to destroy shared memory segment!\n");
		exit(1);
	}	

	/* Create a child */
	if((childpid = fork()) == -1){
		perror("Failed to fork!\n");
		if(detachandremove(sharedMemoryKeyValue, sharedMemoryBuffer) == -1);
			perror("Failed to destroy shared memory segment!\n");
		semlockp = NULL;
		exit(1);
	}

	if(childpid == 0) /* Child process */
	{
		strDirCounterHelper(path, chStrToSearch, sharedMemoryBuffer);
		semlockp = NULL;
		exit(0);
	}

	/* Parent process  continues */

	while((childpid = r_wait(NULL)) > 0); //Wait for the child process to finish

	tempLog = fopen("./tempLog.log", "w");
	write(fileno(tempLog), sharedMemoryBuffer, strlen(sharedMemoryBuffer));
	fclose(tempLog);

	tempLog = fopen("./tempLog.log", "r");
	logOutputFile = fopen("./GrepFromDir.log", "a");

	totalStrCounter = convertLogFile(tempLog, logOutputFile);

	fclose(logOutputFile);
	fclose(tempLog);

	remove("./tempLog.log");

	if(destroyNamedSemaphore(SEM_NAME, semlockp) == -1){
		perror("Failed to destroy named semaphore!\n");
		exit(1);
	}

	/* Remove the shared memory segment */
	if(detachandremove(sharedMemoryId, sharedMemoryBuffer) == -1)
		perror("Failed to destroy shared memory segment!\n");
	
	return totalStrCounter;
}

/* Recursive function */
void strDirCounterHelper(char *path, char *chStrToSearch, char *shBuffer)
{
	struct stat dirInfo; /* Directory Info */
	struct stat fileInfo; /* File Info */
	struct dirent *dirFiles; /* To reach names of the files, d_name */
    char fullFileName[MAX_PATH_LENGHT];	/* For file names */
    char currDirName[MAX_PATH_LENGHT];	/* Directory name */
    DIR *dirInp = NULL; /* Opened dir */
    pid_t childpid = 0; /* To check when the process is child */
	int dirsInDir = 0; /* Saves the number of the files in a dir */
	int filesInDir = 0;
	int i=0; /* For counters */
	int error; /* To indicate thread error */
	strncpy(currDirName, path, MAX_PATH_LENGHT-1);

	/* If the linking is unsuccessful */
    if(lstat(currDirName, &dirInfo) == -1){
        perror(currDirName);
        return;
    }

    /* Oper directory and check if the opening is successful */
    if((dirInp = opendir(currDirName)) == NULL) {
        perror(currDirName);
        return;
    }

    /* Count total dirs and regular files in the current directory */
	while((dirFiles = readdir(dirInp)) != NULL){
		sprintf(fullFileName, "%s/%s", currDirName, dirFiles->d_name);

		if(fullFileName[strlen(fullFileName)-1] == '~')
			continue;

		if((strcmp(dirFiles->d_name, "..") == 0) || (strcmp(dirFiles->d_name, ".")==0))
			continue;

		/* If the linking is unsuccessful */
		if(lstat(fullFileName, &fileInfo) == -1)
			perror(fullFileName); /* Error */

		if(S_ISDIR(fileInfo.st_mode)){
			++dirsInDir;
		}
		else{
			++filesInDir;
			//fprintf(stderr, "Current File: %s\n", fullFileName);
		}
	}

	rewinddir(dirInp); /* Go to the beggining of the directory */

	/* If the foler is empty kill this process */
	if(filesInDir == 0 && dirsInDir == 0){
		closedir(dirInp);
		exit(1);
	}

	/* Allocate thread data for every regular file in the current directory and create message queue */
	if(filesInDir > 0){
		threadDataArray = (threadParams_t *)calloc(filesInDir, sizeof(threadParams_t));
		messageKeyValue = (int)getpid();
		initMessageQueue(messageKeyValue, &messageQueueId);
	}

	while((dirFiles = readdir(dirInp)) != NULL)
	{
		sprintf(fullFileName, "%s/%s", currDirName, dirFiles->d_name);
		
		/* If the last char of the file name is ~ jump to next iteration */
		/* Doesn't look into removed temporary files */
		/* For example: B.txt~ */
		if(fullFileName[strlen(fullFileName)-1] == '~')
			continue;
		
		/* If the linking is unsuccessful */
		if(lstat(fullFileName, &fileInfo) == -1)
			perror(fullFileName); /* Error */

        if((strcmp(dirFiles->d_name, "..")) && (strcmp(dirFiles->d_name, ".")))
		{			
			if(isDir(fullFileName) != 0) /* The file is directory */
			{
				/* Create a child */
				if((childpid = fork()) == -1)
				{
					fprintf(stderr, "Failed to fork!\n");
					freeAndClose(threadDataArray, dirInp);
					return;
				}

				if(childpid == 0) /* Child here to open fifo for writing and continue recursively */
				{
					thread_index = 0;
					child_index = 0;
					messageQueueId = -1;
					free(threadDataArray);
					dirFiles = NULL;
					strDirCounterHelper(fullFileName, chStrToSearch, shBuffer);
					exit(0); /* Child process ends here */
				}
				else
				{
					childPids[child_index] = childpid;
					++child_index;
				}
			}
			else if(isRegFile(fullFileName) != 0) /* The file is a regular */
			{

				strcpy(threadDataArray[thread_index].dirPath, fullFileName);
				threadDataArray[thread_index].strToSearch = chStrToSearch;
				/* Create thread */
				error = pthread_create(&(threadIDs[thread_index]), NULL, threadFunc, &(threadDataArray[thread_index]));
				if(error)
				{
					fprintf(stderr, "Failed to create thread!\n");
					freeAndClose(threadDataArray, dirInp);
					return;
				}
				++thread_index;
			}
		}
	} /* While loop ends here */
	dirFiles = NULL;

	/* Parent process continue from here after the loop to read the data childs wrote */

	/* Wait for the threads to finish their work and send the information to the message queue */
	for(i = 0; i < thread_index; ++i){
		error = pthread_join(threadIDs[i], NULL);
		if(error)
		{
			fprintf(stderr, "Failed to join thread %d, ID: %d, ERROR: %s\n", i, (int)threadIDs[i], strerror(errno));
			fprintf(stderr, "MyPid[%d] --> ParentPid[%d]\n", getpid(), getppid());
			return;
		}
	}

	while((childpid = r_wait(NULL)) > 0);

	/* Entry to the semaphore area */
	while(sem_wait(semlockp) == -1){
		if(errno != EINTR){
			perror("Failed to lock the semlock!\n");
			return;
		}
	}	

	/* Critical section */
	message_t myMSG;
	int messageSize;

	for(i = 0; i < thread_index; ++i){
		if ((messageSize = msgrcv(messageQueueId, &myMSG, BLKSIZE, 0, 0)) == -1) {
			perror("Failed to read message queue");
		}
		strncat(shBuffer, myMSG.mText, messageSize);
	}

	/* Exit from the semaphore area */
	while(sem_post(semlockp) == -1){
		perror("Failed to unlock the semlock!\n");
		return;
	}
	
	removeMessageQueue(&messageQueueId);
	freeAndClose(threadDataArray, dirInp);
}

/* Finds and return the number of columns in the file */
void getFileParameters(FILE *inputFilePtr, int *iColumnsNumPtr, int *iLongestLineSizePtr){
	char chFromFile;
	int iLineSize = 0;
	
	/* Sets the file position of the stream at the beggining of the file. */
	fseek(inputFilePtr, 0, SEEK_SET);
	
	/* Read the file char by char until the end */
	while(fscanf(inputFilePtr, "%c", &chFromFile) != EOF)
	{
		if(chFromFile != '\n') /* if the char is not backslash n */
			++iLineSize; /* Increase line size */
		else if(chFromFile == '\n') /* if backslash n is found */
		{
			/* If current line size is bigger than longest saved line size */
			if(iLineSize>*iLongestLineSizePtr)
				*iLongestLineSizePtr = iLineSize; /* Save the current line lenght */
			iLineSize = 0; /* Reset line size counter */
			++(*iColumnsNumPtr); /* Increase columns number */
		}
	}
}

/* Finds how many times the searched word occurs in a regular file */
/* Sends the line and column information to the message queue with ID - toMessageQueueId */
int strFileCounterOcc(char *fileName, char *chStrToSearch, int *toMessageQueueId){
	FILE *inputFilePtr = fopen(fileName, "r");
	int iSearchedStrSize = strlen(chStrToSearch); /* Size of the searched string */ 
	char lineBufferPtr[BLKSIZE] = "\0"; /* buffer where is saved every line read from the file */
	char messageBuffer[BLKSIZE] = "\0"; /* buffer where is saved the message for sending */
	char buffer[100] = "\0";
	/* Indexes */
	int iIndexOfBuffer, iIndexOfSearchedStr, iLineBufferIndex;
	/* char readed from the file */
	char chFromFile = '\0';
	int iSameCharCounter = 0;
 	/* Counter to count how many times the string occured in the file*/
	int	iSearchedStrCounter = 0; 
	/* Loop counters */
	int i, j;

	int iColumnsSize = 0,		/* Holds how many columns are in the file */
	    iLongestLineSize = 0;	/* Holds the size of the longest line in the file */

	/* Get column and line information from the file */
	getFileParameters(inputFilePtr, &iColumnsSize, &iLongestLineSize);
	/* Sets the file position of the stream at the beggining of the file. */
	fseek(inputFilePtr, 0, SEEK_SET);

	/* Print the file name on the log file */
	sprintf(messageBuffer, "%s\n", fileName);

	for(i=0; i<iColumnsSize; ++i)
	{
		/* Reset the buffer index */
		iIndexOfBuffer = 0;
		/* Read a line from the file into the buffer */

		while(fscanf(inputFilePtr, "%c", &chFromFile) && chFromFile != '\n')
		{
			lineBufferPtr[iIndexOfBuffer] = chFromFile;
			++iIndexOfBuffer;
		}

		for(j=0; j<iIndexOfBuffer; ++j)
		{
			/* If the char from the buffer is same as 
			the first char of the searched string */
			if(lineBufferPtr[j] == chStrToSearch[0])
			{	
				/* Save the index of that char from the buffer */
				iLineBufferIndex = j;
				/* Reset the index of the searched string */
				iIndexOfSearchedStr = 0;
				/* while the second, third, fourth.... character 
				   is same as the searched string chars */
				while(lineBufferPtr[iLineBufferIndex] == chStrToSearch[iIndexOfSearchedStr])
				{
					/* Increase both indexes */
					++iLineBufferIndex;
					++iIndexOfSearchedStr;
					/* Increase the same char counter */
					++iSameCharCounter;
				}
				/* If same characters number is as the size of the searched string, 
				   the string is found */
				if(iSameCharCounter == iSearchedStrSize)
				{	
					/* Increase the number of the found searched string */
					++iSearchedStrCounter;

					sprintf(buffer, "%d %d\n", i+1, j+1);			
					strncat(messageBuffer, buffer, strlen(buffer));		
				}

				/* Reset the same char counter */
				iSameCharCounter = 0;
			}
		}		
	}

	sprintf(buffer, "Total times: %d\n\n", iSearchedStrCounter);
	strncat(messageBuffer, buffer, strlen(buffer));

	writeMessage(messageBuffer, strlen(messageBuffer), toMessageQueueId);

	/* Close oppened file */
	fclose(inputFilePtr);
	inputFilePtr = NULL;

	return iSearchedStrCounter;
}

/* Checks if a path is directory */
int isDir(char *path){

	struct stat statbuf;
	if(stat(path, &statbuf) == -1)
		return 0;
	else
		return S_ISDIR(statbuf.st_mode);
}

/* Checks if a path is regular file */
int isRegFile(char *path){

	struct stat statbuf;
	if(stat(path, &statbuf) == -1)
		return 0;
	else
		return S_ISREG(statbuf.st_mode);
}

/* Convert the log file in format that teacher wants */
int convertLogFile(FILE *fileFrom, FILE *fileTo){
	char buffer[BLKSIZE];
	int tempInt = 0, tempInt2 = 0;
	int numOfoccurence = 1;
	int totalStrCounter = 0;

	while(fgets(buffer, BLKSIZE, fileFrom) != NULL)
	{

		if(buffer[0] == '\n')
			continue;

		else if(buffer[0] == 'T'){
			sscanf(buffer, "Total times: %d", &tempInt);
			totalStrCounter += tempInt;
			fprintf(fileTo, "%s\n", buffer);
		}
		else if(isdigit(buffer[0])){
			sscanf(buffer, "%d %d", &tempInt, &tempInt2);
			fprintf(fileTo, "\t%2d. line# %d", numOfoccurence, tempInt);
			fprintf(fileTo, "    column# %d\n", tempInt2);

			++numOfoccurence;
		}
		else{
			fprintf(fileTo, "%s", buffer);
			numOfoccurence = 1;
		}

	}
	
	return totalStrCounter;
}

void freeAndClose(threadParams_t *threadsData, DIR *dir){
	if(threadsData != NULL)
	{
		free(threadsData);
		threadsData = NULL;
	}
	closedir(dir);
	dir = NULL;
}

/* Partially referenced from book */
int getNamedSemaphore(char *semName, sem_t **sem, int val){

	while(((*sem = sem_open(semName, FLAGS, PERMS, val)) == SEM_FAILED) &&
														(errno == EINTR));
	if(*sem != SEM_FAILED)
		return 0;
	if(errno != EEXIST)
		return -1;
}

/* Partially referenced from book */
int destroyNamedSemaphore(char *semName, sem_t *sem){
	int error = 0;

	if(sem_close(sem) == -1)
		error = errno;
	sem = NULL;
	if((sem_unlink(semName) != -1) && !error)
		return 0;

	/* Set errno to first error that occurred */
	if(error)
		errno = error;
	return -1;
}

/* Initializes message queue */
int initMessageQueue(int key, int *messageQueueId){
	*messageQueueId = msgget(key, PERM | IPC_CREAT);
	if(*messageQueueId == -1)
		return -1;

	return 0;
}

/* Partially referenced from the book */
int writeMessage(void *buffer, int bufferLenght, int *toMessageQueueId){
	int error = 0;

	message_t *myMessage;
	if((myMessage = (message_t *)malloc(sizeof(message_t) + bufferLenght-1)) == NULL)
		return -1;
	memcpy(myMessage->mText, buffer, bufferLenght);
	myMessage->mType = 1; /* Message type is always 1 */

	if(msgsnd(*toMessageQueueId, myMessage, bufferLenght, 0) == -1)
		error = errno;
	free(myMessage);

	if(error){
		errno = error;
		return -1;
	}

	return 0;
}

/* Removes message queue */
int removeMessageQueue(int *messageQueueId){
	return msgctl(*messageQueueId, IPC_RMID, NULL);
}

/* Detach and removes shared memory */
int detachandremove(int shmid, void *shmaddr){
	int error = 0;

	if (shmdt(shmaddr) == -1)
		error = errno;

	if ((shmctl(shmid, IPC_RMID, NULL) == -1) && !error)
		error = errno;

	if (!error)
		return 0;

	errno = error;
	return -1;
}

/* ^C signal handler */
void signal_SIGINT_handler(int signo){
	int counter = 0;

	for(counter = 0; counter<thread_index; ++counter)
		kill(threadIDs[counter], SIGINT);

	for(counter = 0; counter<child_index; ++counter)
		kill(childPids[counter], SIGINT);

	free(threadDataArray); /* Free allocated data */
	removeMessageQueue(&messageQueueId);
	detachandremove(sharedMemoryId, sharedMemoryBuffer);
	destroyNamedSemaphore(SEM_NAME, semlockp);

	fprintf(stderr, "\nCTRL-C has been pressed! Properly closing the program...\n");
	exit(signo);
}
