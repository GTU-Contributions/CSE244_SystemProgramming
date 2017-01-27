/*****************************************************************************
 *	HW3_131044084_Mehmed_Mustafa											 *
 *	System Programming - GrepFromDir using PIPEs and FIFOs					 *
 *	Date: 19.03.2016														 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include "restart.h"

#define MAX_PATH_LENGHT 4096 /* Maximum number in path lenght */
#define BLKSIZE 4096
#define FIFO_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

static int pipeFD_status = -1;
static int fifoFD_status = -1;

int strDirCounter(const char *path, const char *chStrToSearch);
void strDirCounterHelper(const char *path, const char *chStrToSearch, int collectFifoFd);
int convertLogFile(FILE *fileFrom, FILE *fileTo);
void getFileParameters(FILE *inputFilePtr, int *iColumnsNumPtr, int *iLongestLineSizePtr);
int strFileCounter(const char *fileName, const char *chStrToSearch, FILE *outputFilePtr);
int numFilesDir(DIR *dirInp, int *dirsCounter, int *filesCounter);
int isDir(char *path);
int isRegFile(char *path);
int copyfileFromFDtoFD(int fromfd, int tofd);
void freeMemoryInt(int **ptr, int size);

int main(int argc, char *argv[])
{
	DIR *dirInput = NULL;
	int totalStrCounter = 0;

	if(argc != 3)
	{
		printf("Wrong number of arguments[%d]!\n", argc);
		printf("Usage: ./execName dirName stringToSearch\n");
		printf("Example: ./grepfromDir ../Desktop ece\n");
	}

	/* Oper directory and check if the opening is successful */
	if ((dirInput = opendir(argv[1])) == NULL) 
	{
	    perror(argv[1]);
	    return -1;
	}

	totalStrCounter = strDirCounter(argv[1], argv[2]);
	fprintf(stdout, "%d\n", totalStrCounter);

	closedir(dirInput);

	return 0;
}

int isDirectoryEmpty(char *dirName){
	int n = 0;
	struct dirent *d;
	DIR *dir = opendir(dirName);

	if(dir == NULL) // Not a directory or doesn't exist
		return 1;

	while ((d = readdir(dir)) != NULL)
		if(++n > 2)
			break;

  	closedir(dir);

  	if(n <= 2) // The directory is empty
    	return 1;
  	else
    	return 0;
}

/* Wrapper for the recursive function */
int strDirCounter(const char *path, const char *chStrToSearch){
	FILE *tempLog = NULL, *logOutputFile = NULL;
	int totalStrCounter = 0; /* Counter for string in the directory */
	char fifoName[20];
	char buf[BLKSIZE];
	int fdFifoWr, fdFifoRd;
	pid_t childpid = 0;
	int mTest = 0;

	tempLog = fopen("tempLog.log", "a");

	sprintf(fifoName, "Wrapper: %lu\n", (long)getpid());
	if(mkfifo(fifoName, FIFO_PERMS) == -1)
		perror("Failed to create pipe!!\n");

	/* Create a child */
	if((childpid = fork()) == -1){
		perror("Failed to fork!\n");
		exit(1);
	}

	if(childpid == 0) /* Child process */
	{
		fdFifoWr = open(fifoName, O_WRONLY);
		strDirCounterHelper(path, chStrToSearch, fdFifoWr);
		exit(0);
	}
	else /* Parent process */
	{
		fdFifoRd = open(fifoName, O_RDONLY); 
		mTest = copyfileFromFDtoFD(fdFifoRd, fileno(tempLog));
		r_close(fdFifoRd);
	}

	unlink(fifoName);
	fclose(tempLog);

	tempLog = fopen("tempLog.log", "r");
	logOutputFile = fopen("GrepFromDir.log", "a");

	totalStrCounter = convertLogFile(tempLog, logOutputFile);

	fclose(logOutputFile);
	fclose(tempLog);

	remove("tempLog.log");

	return totalStrCounter;
}

/* Recursive function */
void strDirCounterHelper(const char *path, const char *chStrToSearch, int collectFifoFd)
{
	struct stat dirInfo; /* Directory Info */
	struct stat fileInfo; /* File Info */
	struct dirent *dirFiles; /* To reach names of the files, d_name */
    char fullFileName[MAX_PATH_LENGHT];	/* For file names */
    char currDirName[MAX_PATH_LENGHT];	/* Directory name */
    DIR *dirInp = NULL; /* Opened dir */
	int **pipeDesc = NULL; /* Pipe descriptors array */
	int *fifoDesc = NULL; /* Fifo descriptors array */
    pid_t childpid = 0; /* To check when the process is child */
	int childpidFifo = 0;
	char fifoName[20];
	int dirsInDir = 0; /* Saves the number of the files in a dir */
	int filesInDir = 0;
	int fdChild = 0;
	int i=0; /* For counters */

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

	while((dirFiles = readdir(dirInp)) != NULL){
		sprintf(fullFileName, "%s/%s", currDirName, dirFiles->d_name);

		if(fullFileName[strlen(fullFileName)-1] == '~')
			continue;

		/* If the linking is unsuccessful */
		if(lstat(fullFileName, &fileInfo) == -1)
			perror(fullFileName); /* Error */

		if((strcmp(dirFiles->d_name, "..") == 0) || (strcmp(dirFiles->d_name, ".")==0))
			continue;

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
		unlink(fifoName);
		exit(0);
	}

	/* Allocate pipes for every regular file in the current directory */
	if(filesInDir > 0)
	{
		pipeDesc = (int **)malloc(filesInDir*sizeof(int *));
		for(i=0; i<filesInDir; ++i)
			pipeDesc[i] = (int *)malloc(2*sizeof(int *));

		for(i=0; i<filesInDir; ++i)
			if(pipe(pipeDesc[i]) == -1)
			{
				perror("Failed to create pipe!\n");
				exit(1);
			}
	}
	
	/* Allocate fifos for every directory in the current directory */
	if(dirsInDir > 0)
	{
		fifoDesc = (int *)malloc(dirsInDir*sizeof(int));

		for(i=0; i<dirsInDir; ++i)
		{
			sprintf(fifoName, "%d-%d", i, (int)getpid());
			if(mkfifo(fifoName, FIFO_PERMS) == -1)
			{
				perror("Failed to create myfifo!\n");
				exit(1);	
			}
			fifoDesc[i] = open(fifoName, O_RDONLY | O_NONBLOCK);
		}

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
				++fifoFD_status;

			else if(isRegFile(fullFileName) != 0) /* The file is a regular */
				++pipeFD_status;

			/* Create a child */
			if((childpid = fork()) == -1)
			{
				perror("Failed to fork!\n");
				exit(1);
			}

			if(childpid == 0){
				break; /* End loop for the child process */
			}

		}
	}

	if(childpid == 0) /* Child process here */
	{
		if(isDir(fullFileName) != 0) /* The file is directory */
		{
			
			sprintf(fifoName, "%d-%d", fifoFD_status, (int)getppid());
			
			fdChild = 0;
			fdChild = open(fifoName, O_WRONLY | O_NONBLOCK);
			if(fdChild == -1){
				perror("Failed to open fifo!!!!\n");
			}

			fifoFD_status = -1;  /* Reset fifo fd counter - static */
			pipeFD_status = -1; /* Reset pipe fd counter - static */

			/* Free everyting allocated */
			free(fifoDesc);
			fifoDesc = NULL;
			freeMemoryInt(pipeDesc, filesInDir);
			pipeDesc = NULL;
			closedir(dirInp);
			dirInp = NULL;

			/* Go inside a directory if not empty */
			if(!isDirectoryEmpty(fullFileName))
				strDirCounterHelper(fullFileName, chStrToSearch, fdChild);

			/* Unlink fifo file */
			unlink(fifoName);
			exit(0); /* child process ends */

		}
		else if(isRegFile(fullFileName) != 0) /* The file is a regular */
		{
			close(pipeDesc[pipeFD_status][0]); /* Close reading for */
			strFileCounterOcc(fullFileName, chStrToSearch, pipeDesc[pipeFD_status][1]);
			close(pipeDesc[pipeFD_status][1]); /* Close writting */

			/* Free everyting allocated */
			free(fifoDesc);
			fifoDesc = NULL;
			freeMemoryInt(pipeDesc, filesInDir);
			pipeDesc = NULL;
			closedir(dirInp);
			dirInp = NULL;

			exit(0); /* child process ends */
		}

	}
	else /* Parent process here */
	{

		while((childpid = r_wait(NULL)) > 0);

		/* Copy everything from child fifos to parent fifo(collectFifoFd) */
		if(dirsInDir > 0)
			for(i=0; i<dirsInDir; ++i)
				copyfileFromFDtoFD(fifoDesc[i], collectFifoFd);

		/* Copy everything from child pipes to parent fifo(collectFifoFd) */
		if(filesInDir > 0)
			for(i=0; i<filesInDir; ++i)
			{
				close(pipeDesc[i][1]); /* Close writting  for parent */
				copyfileFromFDtoFD(pipeDesc[i][0], collectFifoFd);
				close(pipeDesc[i][0]); /* Close reading after copying */
			}
	}
	

	/* Free everyting allocated */
	free(fifoDesc);
	fifoDesc = NULL;
	freeMemoryInt(pipeDesc, filesInDir);
	pipeDesc = NULL;
    closedir(dirInp);
	dirInp = NULL;

}

/* Reference from book with little changes */
int copyfileFromFDtoFD(int fromfd, int tofd){
	char *bp = NULL;
	char buf[BLKSIZE] = "abcd\n";
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
	int tempInt = 0;
	int numOfoccurence = 1;
	int totalStrCounter = 0;

	while(fgets(buffer, BLKSIZE, fileFrom) != NULL)
	{

		if(buffer[0] == '\n')
			continue;

		if(buffer[0] == '.'){
			fprintf(fileTo, "%s", buffer);
			numOfoccurence = 1;
		}

		else if(buffer[0] == 'T'){
			sscanf(buffer, "Total times: %d", &tempInt);
			totalStrCounter += tempInt;
			fprintf(fileTo, "%s\n", buffer);
		}
		else{
			sscanf(buffer, "%d", &tempInt);
			fprintf(fileTo, "\t%d. line# %d", numOfoccurence, tempInt);

			sscanf(buffer, "%d", &tempInt);
			fprintf(fileTo, "    column# %d\n", tempInt);

			++numOfoccurence;
		}

	}
	
	return totalStrCounter;
}

/* Finds and return the number of columns in the file */
void getFileParameters(FILE *inputFilePtr, int *iColumnsNumPtr, int *iLongestLineSizePtr)
{
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

/* Reference from book */
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

/* Frees pipes array */
void freeMemoryInt(int **ptr, int size)
{
	int i; /* Counter */
	for(i=0; i<size; ++i)
	{
		free(ptr[i]);
		ptr[i] = NULL;
	}

	free(ptr);
}

int strFileCounterOcc(const char *fileName, const char *chStrToSearch, int toFd)
{
	FILE *inputFilePtr = fopen(fileName, "r");
	int iSearchedStrSize = strlen(chStrToSearch); /* Size of the searched string */ 
	char *chBufferPtr; /* buffer where is saved every line read from the file */
	char pipeBufferWrite[BLKSIZE];
	char temp[80];
	/* Indexes */
	int iIndexOfBuffer, iIndexOfSearchedStr, iTempBufferIndex;
	/* char readed from the file */
	char chFromFile;
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

	/* Allocate dynamic memory for the buffer with the size of the longest line */
	chBufferPtr = (char *)malloc(iLongestLineSize*sizeof(char));
	
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
				   is same as the serached string chars */
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

					/* If we have found the word for the first time */
					if(iSearchedStrCounter == 1)
					{
						/* Print the file name on the log file */
						sprintf(pipeBufferWrite, "%s\n", fileName);
					}
					sprintf(temp, "%d %d\n", i+1, j+1);
		/* Print on the log file the line and column where the string is found */
					strcat(pipeBufferWrite, temp);
				}
				/* Reset the same char counter */
				iSameCharCounter = 0;
			}
		}
		
	}
	
	if(iSearchedStrCounter != 0)
	{
		sprintf(temp, "Total times: %d\n\n", iSearchedStrCounter);
		strcat(pipeBufferWrite, temp);
	}

	r_write(toFd, pipeBufferWrite, strlen(pipeBufferWrite));

	/* Deallocate allocated memory for the buffer */
	free(chBufferPtr);
	/* Close oppened file */
	fclose(inputFilePtr);

	return iSearchedStrCounter;

}
