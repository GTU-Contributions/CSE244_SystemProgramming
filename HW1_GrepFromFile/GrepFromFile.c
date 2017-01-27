/*****************************************************************************
 *       HW1_131044084_Mehmed_Mustafa                                        *
 *       System Programming - GrepFromFile                                   *
 *       Date: 09.03.2016                                                    *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


int strCounter(FILE *inputFilePtr, char *fileName, char *chStrToSearch, FILE *outputFilePtr){
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
	/* Print the file name on the log file */
	fprintf(outputFilePtr, "%s\n", fileName);
	
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
		/* Print on the log file the line and column where the string is found */
					fprintf(outputFilePtr, "\tline# %d   column# %d\n", i+1, j+1);
				}
				/* Reset the same char counter */
				iSameCharCounter = 0;
			}
		}
		
	}
	/* Print on the log file how many times the string is found */
	fprintf(outputFilePtr, "\"%s\" string found: %d times!\n\n", chStrToSearch, iSearchedStrCounter);

	/* Deallocate allocated memory for the buffer */
	free(chBufferPtr);

	return iSearchedStrCounter;

}

int main(int argc, char *argv[]){
	
	FILE *inputFilePtr; /* File pointer where to search */
	FILE *outputFilePtr; /* Log file pointer */
	char *chStrToSearch; /* Holds the string wich is searched */
	int iStrFoundTimes; /* Holds how many times the serached string is found */

	if(argc != 3)
	{
		printf("Error in executing format!\n");
		printf("Right executing format: ./executeName fileNameAndType stringToSearch\n");
		printf("Example: ./grepfromFile mehmed.txt ece\n");
	}
	else
	{
		inputFilePtr = fopen(argv[1], "r");
		outputFilePtr = fopen("GrepFromFile.log", "a");

		if(inputFilePtr == NULL){
			printf("File doesn't exist!!\n");
			exit(1);
		}		

		chStrToSearch = argv[2];
		/* Get the number of occurence */
		iStrFoundTimes = strCounter(inputFilePtr, argv[1], chStrToSearch, outputFilePtr);
		/* Print the number of occurence in the console */
		printf("%d\n", iStrFoundTimes);

		fclose(inputFilePtr);
		fclose(outputFilePtr);

	}
	
	return 0;
}






























