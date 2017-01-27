/*****************************************************************************
 *	HW4_131044084_Mehmed_Mustafa											 *
 *	System Programming - GrepFromDir using PIPEs, FIFOs and Threads 		 *
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
#include "restart.h"

#define MAX_PATH_LENGHT 4096 /* Maximum number in path lenght */
#define BLKSIZE 4096

typedef struct fifoFD{
	char fifoName[256];
	int fifoR_FD;
}fifoFD_t;

typedef struct threadParams{
	char *strToSearch;
	char dirPath[MAX_PATH_LENGHT];
	int pipeFD[2];
}threadParams_t;

fifoFD_t *fifosArray = NULL;
threadParams_t *threadDataArray; /* Thread data Array */
pthread_t threadIDs[256]; /* Thread ID's array */

static int fifo_index = 0;
static int thread_index = 0;

void getFileParameters(FILE *inputFilePtr, int *iColumnsNumPtr, int *iLongestLineSizePtr);
int strFileCounterOcc(char *fileName, char *chStrToSearch, int toFd);
int strDirCounter(char *path, char *chStrToSearch);
void strDirCounterHelper(char *path, char *chStrToSearch, int collectFifoFd);
int isDir(char *path);
int isRegFile(char *path);
int copyfileFromFDtoFD(int fromfd, int tofd);
int convertLogFile(FILE *fileFrom, FILE *fileTo);
void freeAndClose(threadParams_t *threadsData, fifoFD_t *fifos, DIR *dir);

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


	sprintf(mainFifoName, "Wrapper: %lu\n", (long)getpid());
	if(mkfifo(mainFifoName, 0666) == -1)
	{
		perror("Failed to create pipe!!\n");
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
	else /* Parent process  continues */
	{
		tempLog = fopen("./tempLog.log", "w");
		fdFifoRd = open(mainFifoName, O_RDONLY);
		while((childpid = r_wait(NULL)) > 0); //Wait for the child process to finish
		copyfileFromFDtoFD(fdFifoRd, fileno(tempLog));
		r_close(fdFifoRd);
		fclose(tempLog);
		unlink(mainFifoName);
	}

	tempLog = fopen("./tempLog.log", "r");
	logOutputFile = fopen("./GrepFromDir.log", "a");

	totalStrCounter = convertLogFile(tempLog, logOutputFile);

	fclose(logOutputFile);
	fclose(tempLog);

	remove("./tempLog.log");

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
	
	/* Allocate fifos for every directory in the current directory */
	if(dirsInDir > 0)
		fifosArray = (fifoFD_t *)calloc(dirsInDir, sizeof(fifoFD_t));


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
				sprintf(fifosArray[fifo_index].fifoName, "%d-%d", fifo_index, (int)getpid());

				/* Make fifo */
				if(mkfifo(fifosArray[fifo_index].fifoName, 0666) == -1)
				{
					fprintf(stderr, "Failed to create fifo[%s]!\n", fifosArray[fifo_index].fifoName);
					freeAndClose(threadDataArray, fifosArray, dirInp);
					return;
				}

				/* Create a child */
				if((childpid = fork()) == -1)
				{
					fprintf(stderr, "Failed to fork!\n");
					freeAndClose(threadDataArray, fifosArray, dirInp);
					return;
				}

				if(childpid == 0) /* Child here to open fifo for writing and continue recursively */
				{
					int fdChild;
					fdChild = r_open2(fifosArray[fifo_index].fifoName, O_WRONLY);

					if(fdChild == -1)
					{
						fprintf(stderr, "Failed to open fifo[%s] for writing!\n", fifosArray[fifo_index].fifoName);
						unlink(fifosArray[fifo_index].fifoName);
						exit(1);
					}
					else
					{
						fifo_index = 0;
						thread_index = 0;
						free(threadDataArray);
						//closedir(dirInp);
						strDirCounterHelper(fullFileName, chStrToSearch, fdChild);
						close(fdChild);
						exit(0); /* Child process ends here */
					}
				}
				else /* Parent here to open fifo for reading to avoid blocking of the fifo */
				{
					fifosArray[fifo_index].fifoR_FD = r_open2(fifosArray[fifo_index].fifoName, O_RDONLY);
					if(fifosArray[fifo_index].fifoR_FD == -1)
					{
						fprintf(stderr, "Failed to open fifo[%s] for reading!\n", fifosArray[fifo_index].fifoName);
						unlink(fifosArray[fifo_index].fifoName);
						freeAndClose(threadDataArray, fifosArray, dirInp);
						return;
					}
				}
				++fifo_index;
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
					freeAndClose(threadDataArray, fifosArray, dirInp);
					return;
				}			

				/* Create thread */
				error = pthread_create(&(threadIDs[thread_index]), NULL, threadFunc, &(threadDataArray[thread_index]));
				if(error)
				{
					fprintf(stderr, "Failed to create thread!\n");
					freeAndClose(threadDataArray, fifosArray, dirInp);
					return;
				}
				++thread_index;
			}
		}
	} /* While loop ends here */


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

	/* Wait for the processes to finish */
	while((childpid = r_wait(NULL)) > 0);

	/* Copy everything from thread pipes to parent fifo(collectFifoFd) */
	for(i=0; i<thread_index; ++i)
	{
		close(threadDataArray[i].pipeFD[1]); /* Close writting */
		copyfileFromFDtoFD(threadDataArray[i].pipeFD[0], collectFifoFd);
		close(threadDataArray[i].pipeFD[0]); /* Close reading after copying */
	}

	/* Copy everything from child fifos to parent fifo(collectFifoFd) */
	for(i=0; i<fifo_index; ++i)
	{
		copyfileFromFDtoFD(fifosArray[i].fifoR_FD, collectFifoFd);
		unlink(fifosArray[i].fifoName);
	}

	freeAndClose(threadDataArray, fifosArray, dirInp);
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

void freeAndClose(threadParams_t *threadsData, fifoFD_t *fifos, DIR *dir){
	if(threadsData != NULL)
	{
		free(threadsData);
		threadsData = NULL;
	}
	if(fifos != NULL)
	{
		free(fifos);
		fifos = NULL;
	}
	closedir(dir);
	dir = NULL;
}
