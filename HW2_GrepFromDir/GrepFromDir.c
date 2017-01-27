/*****************************************************************************
 *	HW2_131044084_Mehmed_Mustafa											 *
 *	System Programming - GrepFromDir									     *
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

#define MAX_PATH_LENGHT 4096 /* Maximum number in path lenght */

int strDirCounter(const char *path, const char *chStrToSearch);
void strDirCounterHelper(const char *path, const char *chStrToSearch, FILE *strCounterFile);
void getFileParameters(FILE *inputFilePtr, int *iColumnsNumPtr, int *iLongestLineSizePtr);
int strFileCounter(const char *fileName, const char *chStrToSearch, FILE *outputFilePtr);

int main(int argc, char *argv[])
{
	DIR *dirInput;
	int totalStrCounter;

	if(argc == 3)
	{
		/* Oper directory and check if the opening is successful */
		if ((dirInput = opendir(argv[1])) == NULL) 
		{
		    perror(argv[1]); /* Error */
		    return -1;
		}

		totalStrCounter = strDirCounter(argv[1], argv[2]);
		printf("%d\n", totalStrCounter);

		closedir(dirInput);
	}
	else
	{
		printf("Wrong number of arguments[%d]!\n", argc);
		printf("Usage: ./execName dirName stringToSearch\n");
		printf("Example: ./grepfromDir ../Desktop ece\n");
	}

	return 0;
}

int strDirCounter(const char *path, const char *chStrToSearch){
	FILE *tempIntegerFile; /* Temporary storage for integer numbers */ 
	int totalStrCounter = 0; /* Counter for string in the directory */
	int intFromFile; /* Integer readed from the file */

	/* Open Temporary Log file to write inside */
	tempIntegerFile = fopen("occurenceCounterTempLog.log", "w");

	/* Write into the temporary log file the number of the occurences of 
		the searched string in every file into separate lines */
	strDirCounterHelper(path, chStrToSearch, tempIntegerFile);

	fclose(tempIntegerFile); /* Close oppened file */

	/* Open Temporary Log file to read from */
	tempIntegerFile = fopen("occurenceCounterTempLog.log", "r");

	/* Read integers and add them to find total number of occurences in a dir */
	while(fscanf(tempIntegerFile, "%d", &intFromFile) != EOF){
		totalStrCounter += intFromFile;
	}

	fclose(tempIntegerFile); /* Close oppened file */

	remove("occurenceCounterTempLog.log"); /* Remove the temporary file */

	return totalStrCounter;
}

void strDirCounterHelper(const char *path, const char *chStrToSearch, FILE *tempIntegerFile)
{
	struct stat dirInfo; /* Directory Info */
	struct stat fileInfo; /* File Info */
	struct dirent *dirFiles; /* To reach names of the files, d_name */
    char fullFileName[MAX_PATH_LENGHT];	/* For file names */
    char dirName[MAX_PATH_LENGHT];	/* Directory name */
    DIR *dirInp; /* Opened dir */
	FILE *fileLog; /* String counter log file */
    pid_t pid; /* To check when the process is child */
	int strCounter = 0; /* Saves the number of str occurence in a file */

	strncpy(dirName, path, MAX_PATH_LENGHT-1);
	
	/* If the linking is unsuccessful */
    if (lstat(dirName, &dirInfo) == -1)
    {
        perror(dirName); /* Error */
        return;
    }

    /* Oper directory and check if the opening is successful */
    if ((dirInp = opendir(dirName)) == NULL) 
    {
        perror(dirName); /* Error */
        return;
    }

	/* Read the directory */
	while((dirFiles = readdir(dirInp)) != NULL)
	{
		sprintf(fullFileName, "%s/%s", dirName, dirFiles->d_name);

		/* If the last char of the file name is ~ jump to next iteration */
		/* Doesn't look into removed temporary files */
		/* For example: B.txt~ */
		if(fullFileName[strlen(fullFileName)-1] == '~')
			continue;

		/* If the linking is unsuccessful */
		if(lstat(fullFileName, &fileInfo) == -1)
			perror(fullFileName); /* Error */

        /* If the file is directory go inside and continue */
        if(S_ISDIR(fileInfo.st_mode))
        {
            if((strcmp(dirFiles->d_name, "..")) && (strcmp(dirFiles->d_name, ".")))
			{
				/* Fork the processes and make error check */
		        if((pid = fork()) == -1)
		        {
		            perror("Error: Unable to fork!\n");
		            exit(1); /* Exit on fork problem */
		        }

		        if (pid == 0) /* Child here */
		        {
					/* Send child to look in this directory recursively */
					strDirCounterHelper(fullFileName, chStrToSearch, tempIntegerFile);
		            exit(0); /* End child process */
		        }
				else /* Parent here */
					wait(NULL); /* Parent waits for childs */
			}
        }
		else /* When the file is not a directory */
		{
			/* Fork the processes and make error check */
	        if((pid = fork()) == -1)
	        {
	            perror("Error: Unable to fork!\n");
	            exit(1); /* Exit on fork problem */
	        }

	        if(pid == 0) /* Child here */
	        {
				fileLog = fopen("GrepFromDir.log", "a"); /* Open log file */

				/* Find total number of the strings in the file */
				strCounter = strFileCounter(fullFileName, chStrToSearch, fileLog);

				/* If the counter is positive */
				if(strCounter > 0)
				{
					/* Write the number to the temporary log file for integers */
					fprintf(tempIntegerFile, "%d\n", strCounter);
				}

            	fclose(fileLog); /* Close log file */

                exit(0); /* End child process */
	        }
			else /* Parent here */
				wait(NULL); /* Parent waits for childs */
		}
	} /* While loop ends here */
	
    /* Close directory */
    while((closedir(dirInp) == -1) && (errno == EINTR));

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


int strFileCounter(const char *fileName, const char *chStrToSearch, FILE *outputFilePtr){
	FILE *inputFilePtr = fopen(fileName, "r");

	int iSearchedStrSize = strlen(chStrToSearch); /* Size of the searched string */ 
	char *chBufferPtr; /* buffer where is saved every line read from the file */
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
						fprintf(outputFilePtr, "%s\n", fileName);
					}

		/* Print on the log file the line and column where the string is found */
					fprintf(outputFilePtr, "\tline# %d   column# %d\n", i+1, j+1);
				}
				/* Reset the same char counter */
				iSameCharCounter = 0;
			}
		}
		
	}

	if(iSearchedStrCounter != 0)
		/* Print on the log file how many times the string is found */
		fprintf(outputFilePtr, "\"%s\" string found: %d times!\n\n", chStrToSearch, iSearchedStrCounter);		

	/* Deallocate allocated memory for the buffer */
	free(chBufferPtr);
	/* Close oppened file */
	fclose(inputFilePtr);

	return iSearchedStrCounter;

}











