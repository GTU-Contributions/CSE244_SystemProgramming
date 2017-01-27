/*****************************************************************************
 *	HW5_131044084_Mehmed_Mustafa											 *
 *	System Programming - GrepFromDir using PIPEs, Threads and one FIFO		 *
 *	Date: 30.04.2016														 *
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

#define MAX_PATH_LENGHT 4096 /* Maximum number in path lenght */
#define BLKSIZE 4096
 /* Semaphore permissions and flags */
#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS (O_CREAT | O_EXCL)
#define SEM_NAME "MAIN_SEMAPHORE"

typedef struct threadParams{
	char *strToSearch;
	char dirPath[MAX_PATH_LENGHT];
	int pipeFD[2];
}threadParams_t;

threadParams_t *threadDataArray = NULL; /* Thread data Array */
pthread_t threadIDs[256]; /* Thread ID's array */
pid_t childPids[256]; /* Child ID's array */
sem_t *semlockp = NULL;

static int thread_index = 0;
static int child_index = 0;

void getFileParameters(FILE *inputFilePtr, int *iColumnsNumPtr, int *iLongestLineSizePtr);
int strFileCounterOcc(char *fileName, char *chStrToSearch, int toFd);
int strDirCounter(char *path, char *chStrToSearch);
void strDirCounterHelper(char *path, char *chStrToSearch, int collectFifoFd);
int isDir(char *path);
int isRegFile(char *path);
int copyfileFromFDtoFD(int fromfd, int tofd);
int convertLogFile(FILE *fileFrom, FILE *fileTo);
void freeAndClose(threadParams_t *threadsData, DIR *dir);
int getNamedSemaphore(char *semName, sem_t **sem, int val);
int destroyNamedSemaphore(char *semName, sem_t *sem);
void signal_SIGINT_handler(int signo);

void *threadFunc(void *arg){
	threadParams_t *currFile = (threadParams_t *)arg;
	strFileCounterOcc(currFile->dirPath, currFile->strToSearch, currFile->pipeFD[1]);
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
	char mainFifoName[20];
	char buf[BLKSIZE];
	int fdFifoWr, fdFifoRd;
	pid_t childpid = 0;
	int mTest = 0;

	signal(SIGINT, signal_SIGINT_handler);

	/* Create the only fifo for the program */
	sprintf(mainFifoName, "MAIN_FIFO_%lu\n", (long)getpid());
	if(mkfifo(mainFifoName, 0666) == -1)
	{
		perror("Failed to create fifo!!\n");
		exit(1);
	}

	/* Create the named semaphore for the program */
	if(getNamedSemaphore(SEM_NAME, &semlockp, 1) == -1){
		perror("Failed to create named semaphore!\n");
		exit(1);
	}	

	/* Create a child */
	if((childpid = fork()) == -1){
		perror("Failed to fork!\n");
		exit(1);
	}

	if(childpid == 0) /* Child process */
	{
		fdFifoWr = open(mainFifoName, O_WRONLY);
		strDirCounterHelper(path, chStrToSearch, fdFifoWr);
		r_close(fdFifoWr);
		exit(0);
	}

	/* Parent process  continues */
	fdFifoRd = open(mainFifoName, O_RDONLY);

	while((childpid = r_wait(NULL)) > 0); //Wait for the child process to finish

	tempLog = fopen("./tempLog.log", "w");
	copyfileFromFDtoFD(fdFifoRd, fileno(tempLog));
	r_close(fdFifoRd);
	fclose(tempLog);
	unlink(mainFifoName);

	tempLog = fopen("./tempLog.log", "r");
	logOutputFile = fopen("./GrepFromDir.log", "a");

	totalStrCounter = convertLogFile(tempLog, logOutputFile);

	fclose(logOutputFile);
	fclose(tempLog);

	remove("./tempLog.log");

	if(destroyNamedSemaphore(SEM_NAME, semlockp) == -1){
		//perror("Failed to destroy named semaphore!\n");
		exit(1);
	}

	return totalStrCounter;
}

/* Recursive function */
void strDirCounterHelper(char *path, char *chStrToSearch, int collectFifoFd)
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
    if(lstat(currDirName, &dirInfo) == -1)
    {
        perror(currDirName);
        return;
    }

    /* Oper directory and check if the opening is successful */
    if((dirInp = opendir(currDirName)) == NULL) 
    {
        perror(currDirName);
        return;
    }

    /* Count total dirs and regular files in the current directory */
	while((dirFiles = readdir(dirInp)) != NULL)
	{
		sprintf(fullFileName, "%s/%s", currDirName, dirFiles->d_name);

		if(fullFileName[strlen(fullFileName)-1] == '~')
			continue;

		if((strcmp(dirFiles->d_name, "..") == 0) || (strcmp(dirFiles->d_name, ".")==0))
			continue;

		/* If the linking is unsuccessful */
		if(lstat(fullFileName, &fileInfo) == -1)
			perror(fullFileName); /* Error */

		if(S_ISDIR(fileInfo.st_mode))
			++dirsInDir;
		else
			++filesInDir;
	}

	rewinddir(dirInp); /* Go to the beggining of the directory */

	/* If the foler is empty kill this process */
	if(filesInDir == 0 && dirsInDir == 0)
	{
		closedir(dirInp);
		exit(1);
	}

	/* Allocate pipes for every regular file in the current directory */
	if(filesInDir > 0)
		threadDataArray = (threadParams_t *)calloc(filesInDir, sizeof(threadParams_t));

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
					free(threadDataArray);
					dirFiles = NULL;
					strDirCounterHelper(fullFileName, chStrToSearch, collectFifoFd);
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

				/* Create pipe for the thread to write */
				if((pipe(threadDataArray[thread_index].pipeFD)) == -1)
				{
					fprintf(stderr, "Failed to create pipe!\n");
					fprintf(stderr, "ERROR: %s\n", strerror(errno));
					freeAndClose(threadDataArray, dirInp);
					return;
				}			

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

	/* Wait for the threads to finish */
	for(i = 0; i < thread_index; ++i)
	{
		//fprintf(stderr, "THREAD: %d, ID: %d\n", i, (int)threadIDs[i]);
		error = pthread_join(threadIDs[i], NULL);
		if(error)
		{
			if(error == EINVAL)
				fprintf(stderr, "EINVAL\n");
			if(error == ESRCH)
				fprintf(stderr, "ESRCH\n");

			fprintf(stderr, "Failed to join thread %d, ID: %d, ERROR: %s\n", i, (int)threadIDs[i], strerror(errno));
			fprintf(stderr, "MyPid[%d] --> ParentPid[%d]\n", getpid(), getppid());
			return;
		}
	}

	while((childpid = r_wait(NULL)) > 0);

	//fprintf(stderr, "I am waiting before semaphore!\n");
	/* Entry to the semaphore area */
	while(sem_wait(semlockp) == -1){
		if(errno != EINTR){
			perror("Failed to lock the semlock!\n");
			return;
		}
	}
	//fprintf(stderr, "I have entered semaphore!\n");

	/* Copy everything from thread pipes to the main fifo */
	/* Critical section */
	for(i=0; i<thread_index; ++i)
	{
		close(threadDataArray[i].pipeFD[1]); /* Close writting */
		copyfileFromFDtoFD(threadDataArray[i].pipeFD[0], collectFifoFd);
		close(threadDataArray[i].pipeFD[0]); /* Close reading after copying */
	}

	/* Exit from the semaphore area */
	while(sem_post(semlockp) == -1){
		perror("Failed to unlock the semlock!\n");
		return;
	}
	//fprintf(stderr, "I am Exiting semaphore!\n");

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

int strFileCounterOcc(char *fileName, char *chStrToSearch, int toFd){
	FILE *inputFilePtr = fopen(fileName, "r");
	int iSearchedStrSize = strlen(chStrToSearch); /* Size of the searched string */ 
	char chBufferPtr[BLKSIZE] = "\0"; /* buffer where is saved every line read from the file */
	char pipeBufferWrite[BLKSIZE] = "\0";
	/* Indexes */
	int iIndexOfBuffer, iIndexOfSearchedStr, iTempBufferIndex;
	/* char readed from the file */
	char chFromFile = '\0';
	/*  */
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
	sprintf(pipeBufferWrite, "%s\n", fileName);
	r_write(toFd, pipeBufferWrite, strlen(fileName)+1);

	for(i=0; i<iColumnsSize; ++i)
	{
		/* Reset the buffer index */
		iIndexOfBuffer = 0;
		/* Read a line from the file into the buffer */

		while(fscanf(inputFilePtr, "%c", &chFromFile) && chFromFile != '\n')
		{
			chBufferPtr[iIndexOfBuffer] = chFromFile;
			++iIndexOfBuffer;
		}

		for(j=0; j<iIndexOfBuffer; ++j)
		{
			/* If the char from the buffer is same as 
			the first char of the searched string */
			if(chBufferPtr[j] == chStrToSearch[0])
			{	
				/* Save the index of that char from the buffer */
				iTempBufferIndex = j;
				/* Reset the index of the searched string */
				iIndexOfSearchedStr = 0;
				/* while the second, third, fourth.... character 
				   is same as the searched string chars */
				while(chBufferPtr[iTempBufferIndex] == chStrToSearch[iIndexOfSearchedStr])
				{
					/* Increase both indexes */
					++iTempBufferIndex;
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

					sprintf(pipeBufferWrite, "%d %d\n", i+1, j+1);
					r_write(toFd, pipeBufferWrite, strlen(pipeBufferWrite));					
				}

				/* Reset the same char counter */
				iSameCharCounter = 0;
			}
		}		
	}

	if(iSearchedStrCounter != 0)
	{
		sprintf(pipeBufferWrite, "Total times: %d\n\n", iSearchedStrCounter);
		r_write(toFd, pipeBufferWrite, strlen(pipeBufferWrite));
	}

	/* Close oppened file */
	fclose(inputFilePtr);
	inputFilePtr = NULL;

	return iSearchedStrCounter;
}

int isDir(char *path){

	struct stat statbuf;
	if(stat(path, &statbuf) == -1)
		return 0;
	else
		return S_ISDIR(statbuf.st_mode);
}

int isRegFile(char *path){

	struct stat statbuf;
	if(stat(path, &statbuf) == -1)
		return 0;
	else
		return S_ISREG(statbuf.st_mode);
}

int copyfileFromFDtoFD(int fromfd, int tofd){
	char *bp = NULL;
	char buf[BLKSIZE];
	int bytesread, byteswritten;
	int totalbytes = 0;

	for ( ; ; )
	{
		/* handle interruption by signal */
		while(((bytesread = read(fromfd, buf, BLKSIZE)) == -1) && 
															(errno == EINTR));

		if (bytesread <= 0)
			break; /* real error or end-of-file on fromfd */

		bp = buf;
		while(bytesread > 0)
		{
			/* handle interruption by signal */
			while(((byteswritten = write(tofd, bp, bytesread)) == -1 ) &&
															(errno == EINTR));

			if (byteswritten <= 0)
				break;

			totalbytes += byteswritten;
			bytesread -= byteswritten;
			bp += byteswritten;
		}

		if(byteswritten == -1)
			break; /* real error on tofd */
	}

	return totalbytes;
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
	if((sem_unlink(semName) != -1) && !error)
		return 0;

	/* Set errno to first error that occurred */
	if(error)
		errno = error;
	return -1;
}

void signal_SIGINT_handler(int signo){
	int counter = 0;

	for(counter = 0; counter<thread_index; ++counter)
		kill(threadIDs[counter], SIGINT);

	for(counter = 0; counter<child_index; ++counter)
		kill(childPids[counter], SIGINT);

	free(threadDataArray); /* Free allocated data */
	destroyNamedSemaphore(SEM_NAME, semlockp); /* Destroy named semaphore */

	fprintf(stderr, "\nCTRL-C has been pressed! Properly closing the program...\n");
	exit(signo);
}
