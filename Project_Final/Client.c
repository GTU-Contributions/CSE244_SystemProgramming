/****************************************************
 *													*
 *     BIL244 System Programming Final Project		*
 * 					client.c						*
 * 			Student Name: Mehmed Mustafa            *
 * 			Student ID  : 131044084                 *
 * 			Date        : 25/05/2016                *
 *                                                  *
 ****************************************************/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <dirent.h>
#include <signal.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include "restart.h"

typedef unsigned short portNumber_t;

#define ERROR -1
#define TRUE 1
#define FALSE 0
#define MAX_NAME 256
#define MAX_PATH_NAME 512
#define MAX_BUFFER_SIZE 256
#define BYTE_ARRAY_SIZE 16384
#define MAX_TOKENS 3

/* RECEIVING FILE STUFF */
#define WRITE_FLAGS (O_RDWR | O_CREAT | O_EXCL)
#define WRITE_PERMS (mode_t)(S_IRWXU | S_IRWXG | S_IRWXO)

/* Socketler uzerinde aktarilan paket struct */
typedef struct msgPacket{
	char msgHeader[MAX_NAME];
	char msgBuffer[MAX_BUFFER_SIZE];
	char byteArray[BYTE_ARRAY_SIZE];
	int writtenSize;
}msgPacket_t;

static int sigIntFlag = 0;
static int sigIntChildFlag = 0;
pid_t childPid;

/* CTRL + C handler */
void sigint_handler(int sig){
	close(0);
	kill(childPid, SIGINT);
	sigIntFlag = 1;
}

/* CTRL + C child process handler */
void child_sigint_handler(int sig){
	sigIntChildFlag = 1;
	close(0);
	exit(0);
}

/* String sayi mi diye bakar */
int isNumber(char *numString);
/* Gelen string IP mi diye bakar */
int isValidIp4Format(char *ipString);
/* Main argumani olarak gelen bilgileri ceker */
int getIPandPortValuesFromArguments(int argc, char *argv[], portNumber_t *portNumber, char *ipAddress);
/* Servera baglanma socketi ac */
int openSocketForCommunication(char *ipAddress, portNumber_t port);
/* Clientin ana fonksiyonu */
void startTheLauncher(char *ipAddress, portNumber_t port);
/* Clientin child processi surekli socketi dinliyor cliente dosya gonderildi mi diye */
void childReceiveFile(int *communicationSocket);
/* Client dosya gonderiyor */
void sendFile(int *communicationSocket, char *fileName, int clientID);
/* Klavyeden girilen buffer bilgisini parcalara bolerek client komutunu anlar */
int processCommand(char *buffer, char *fileName, int *clientIDtoSent);
/* int argc, char *argv[] tarzinda token olusturur */
int createTokens(char *buf, int *tokenCounter, char **tokens);
/* tokenleri free eder */
void freeTokens(char **tokens);
/* String olarak gelen dosya isminde bir dosya var mi diye bakar */
int isFileExist(char *fileName);
/* Client dizinde ki dosya isimlerini listeleyip ekrana basar */
void listLocal();
/* Server dizinde ki dosya isimlerini serverdan alip ekrana basar*/
void listServer(int *communicationSocket);
/* Su an serverda nekadar connected client varsa onlarin bilgilerini ekrana basar */
void lsClients(int *communicationSocket);
/* Program komutlari hakkinda bilgi verir, ekrana basar */
void help();

int main(int argc, char *argv[]){
 	portNumber_t port;
 	char ipAddress[16] = "\0";

 	if(getIPandPortValuesFromArguments(argc, argv, &port, ipAddress)){
 		fprintf(stderr, "Usage:	./client <Server IP Address> <Port Number>\n");
 		fprintf(stderr, "Example: ./client 127.0.0.1 15666\n");
 		return ERROR;
 	}
 	
 	system("clear");

 	fprintf(stderr, "Trying to connect to the server with Ip address: %s ...\n", ipAddress);
 	fprintf(stderr, "The port number you are trying to connect is: %d ...\n", (int)port);
 	
 	startTheLauncher(ipAddress, port);

 	return 0;
}

int isNumber(char *numString){
	int i, numLenght = 0;

	if(numString == NULL)
		return FALSE;

	numLenght = strlen(numString);

	for(i=0; i<numLenght; ++i){
		if(numString[i] < '0' || numString[i] > '9')
			return FALSE;
	}

	return TRUE;
}

int isValidIp4Format(char *ipString){
	int i, ipLenght = 0, scanStatus = 0;
	char junk[256] = "0";
	unsigned int pos[4];

	if(ipString == NULL)
		return FALSE;

	ipLenght = strlen(ipString);

	if(ipLenght < 7 || 15 < ipLenght)
		return FALSE;

	scanStatus = sscanf(ipString, "%3u.%3u.%3u.%3u%s", &pos[0], &pos[1], &pos[2], &pos[3], junk);

	if(scanStatus != 4 || (junk[0] != '0'))
		return FALSE;

	for(i = 0; i < 4; ++i)
		if(pos[i] > 255)
			return FALSE;

	return TRUE;
}

int getIPandPortValuesFromArguments(int argc, char *argv[], portNumber_t *portNumber, char *ipAddress){
	int port;

 	if(argc != 3){
 		fprintf(stderr, "Error: The arguments must be 3!\n");
 		return ERROR;
 	}
 	if(!isValidIp4Format(argv[1])){
 		fprintf(stderr, "Error: Wrong ip address format!\n");
 		return ERROR;
 	}
 	if(!isNumber(argv[2])){
 		fprintf(stderr, "Error: The port must be a non-negative number!\n");
 		return ERROR;
 	}

 	port = atoi(argv[2]);

 	if(port > 65535){
 		fprintf(stderr, "Error: The port number can't be bigger than 65535!\n");
 		return ERROR;
 	}

 	*portNumber = (portNumber_t)port;
 	strcpy(ipAddress, argv[1]);

 	return 0;
}

int connectToTheServer(char *ipAddress, portNumber_t port){
	struct sockaddr_in server_addr;
	struct hostent *hp;
	struct in_addr ipv4addr;
	int socketFD, error;

	/* Creating communication endpoint (socketFD) */
	if((socketFD = socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
		return ERROR;

	memset((char *)&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((short)port);

	// Converts character string into network address
	inet_pton(AF_INET, ipAddress, &ipv4addr);
	// Get host name 
	hp = gethostbyaddr(&ipv4addr, sizeof(ipv4addr), AF_INET);

	if(hp == NULL){
		error = errno;
		while((close(socketFD) == ERROR) && (errno == EINTR));
		errno = error;
		return ERROR;		
	}
	else{
		strncpy((char *)hp->h_addr, (char *)&server_addr.sin_addr.s_addr, hp->h_length);
	}

	/* Request connection from the server */
	if((connect(socketFD, (struct sockaddr *)&server_addr, sizeof(server_addr))) == ERROR){
		error = errno;
		while((close(socketFD) == ERROR) && (errno == EINTR));
		errno = error;
		return ERROR;
	}

	return socketFD;
}

// Clientin ana fonksiyonu
void startTheLauncher(char *ipAddress, portNumber_t port){
	int socketForCommunication;
	int clientChoice, clientIDtoSent;
	char fileName[MAX_NAME];
	char buffer[MAX_BUFFER_SIZE];
	int lengthBuffer = 0;
	int bytesWritten, bytesRead, bytesPeeked;
	msgPacket_t internetPacket;

	if((socketForCommunication = connectToTheServer(ipAddress, port)) == ERROR){
		perror("Failed to connect to the server!\n");
		return;
	}

	fprintf(stderr, "You have been connected to the server[%s] with port[%d]!\n", ipAddress, (int)port);

	/* Bir tane child yapar, cliente gelen dosya var ise child o dosyayi kabul etsin diye */
	if((childPid = fork()) == ERROR){
		perror("Failed to accept connection!\n");
		r_close(socketForCommunication);
		return;
	}

	if(childPid == 0) // Child code
	{ 
		//Child burada surekli bilgi bekler soketten
		childReceiveFile(&socketForCommunication);
		r_close(socketForCommunication);
		exit(0);
	}

	while(TRUE){
		signal(SIGINT, sigint_handler);

		clientChoice = -1;
		clientIDtoSent = -10;
		fprintf(stderr, "Server: Enter your choice$ ");
		// Get command from the standart input
		fgets(buffer, MAX_BUFFER_SIZE-1, stdin);

		if(sigIntFlag){
			break;
		}

		lengthBuffer = strlen(buffer);
		if(lengthBuffer > 0 && buffer[lengthBuffer - 1] == '\n')
			buffer[lengthBuffer-1] = '\0';

		/* Konzoldan girilen komutu process eder ve kullanicinin ne sectigini bulur */
		if((clientChoice = processCommand(buffer, fileName, &clientIDtoSent)) == ERROR){
			fprintf(stderr, "Server: Your choice is not valid! Try again...\n");
			continue;
		}

		memset(internetPacket.msgHeader, '\0', MAX_NAME);
		memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
		memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
		internetPacket.writtenSize = 0;

		/* Kullanicinin secimine gore switch case te ki bir fonksiyonu cagirir gerekli handshakingi yaptiktan sonra */
		switch(clientChoice)
		{
			/* listLocal */
			case 1:
				listLocal();
				break;
			/* listServer */
			case 2:
				sprintf(internetPacket.msgHeader, "listServer");
				sprintf(internetPacket.msgBuffer, "Send me information about the server files!");
				bytesWritten = send(socketForCommunication, &internetPacket, sizeof(msgPacket_t), 0);
				if(bytesWritten <= 0){
					perror("Server: Error occured in listServer command! Try again...\n");
					break;
				}
				bytesPeeked = recv(socketForCommunication, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);
				if(strcmp(internetPacket.msgHeader, "~~~_listServer_~~~") == 0){
					bytesRead = recv(socketForCommunication, &internetPacket, sizeof(msgPacket_t), 0);
					listServer(&socketForCommunication);
				}
				break;
			/* lsClients */
			case 3:
				sprintf(internetPacket.msgHeader, "lsClients");
				sprintf(internetPacket.msgBuffer, "Send me information about the connected clients!");
				bytesWritten = send(socketForCommunication, &internetPacket, sizeof(msgPacket_t), 0);
				if(bytesWritten <= 0){
					perror("Server: Error occured in lsClients command! Try again...\n");
					break;
				}
				bytesPeeked = recv(socketForCommunication, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);
				if(strcmp(internetPacket.msgHeader, "~~~_lsClients_~~~") == 0){
					bytesRead = recv(socketForCommunication, &internetPacket, sizeof(msgPacket_t), 0);
					lsClients(&socketForCommunication);
				}
				break;
			/* sendFile <fileName> <clientID> */
			case 4:
				if(!isFileExist(fileName)){
					fprintf(stderr, "File \"%s\" doesn't exist!\n", fileName);
					break;
				}
				sprintf(internetPacket.msgHeader, "sendFile");
				sprintf(internetPacket.msgBuffer, "%s %d", fileName, clientIDtoSent);
				internetPacket.writtenSize = 0;
				bytesWritten = send(socketForCommunication, &internetPacket, sizeof(msgPacket_t), 0);
				if(bytesWritten <= 0){
					perror("Server: Error occured in lsClients command! Try again...\n");
					break;
				}
				bytesPeeked = recv(socketForCommunication, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);
				if(strcmp(internetPacket.msgHeader, "~~~_sendFile_~~~") == 0){
					// If the destination is available start sending the file
					if(strcmp(internetPacket.msgBuffer, "Destination is available") == 0){
						bytesRead = recv(socketForCommunication, &internetPacket, sizeof(msgPacket_t), 0);
						sendFile(&socketForCommunication, fileName, clientIDtoSent);
					}
					else if(strcmp(internetPacket.msgBuffer, "Destination is busy") == 0)
					{
						recv(socketForCommunication, &internetPacket, sizeof(msgPacket_t), 0);
						fprintf(stderr, "Server: Client is busy now! Try later...\n");
					}
					else if(strncmp(internetPacket.msgBuffer, "Already existing", 16) == 0){
						fprintf(stderr, "Already existing \"%s\" in client: %d\n", fileName, clientIDtoSent);
					}
				}
				break;
			/* help */
			case 5:
				help();
				break;
			default: 
				fprintf(stderr, "Server: Your choice is not valid! Try again...\n");
		}
	}

	r_close(socketForCommunication);
	return;
}


void childReceiveFile(int *communicationSocket){
	int bytesWritten, bytesRead, bytesPeeked, i, flag = 0;
	msgPacket_t internetPacket;
	int fileToBeReceivedFD = -99;
	char fileName[MAX_NAME];
	int isAvailableToReceive = TRUE;
	
	memset(fileName, '\0', MAX_NAME);
	memset(internetPacket.msgHeader, '\0', MAX_NAME);
	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
	internetPacket.writtenSize = 0;

	while(TRUE){
		signal(SIGINT, child_sigint_handler);

		/* Surekli soketi dinler ve gelen mesajlara goz atar dosya gonderildi mi diye gormesi icin */
		bytesPeeked = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);
		if(bytesPeeked <= 0){
			return;
		}

		if(strcmp(internetPacket.msgHeader, "+++_receiveFile_+++") != 0 && 
			strcmp(internetPacket.msgHeader, "+++_NewReceiveFile_+++") != 0)
			continue;

		if(strcmp(internetPacket.msgHeader, "+++_NewReceiveFile_+++") == 0){
			bytesRead = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
			sscanf(internetPacket.msgBuffer, "Can you handle a file %s", fileName);

			sprintf(internetPacket.msgHeader, "+++_sendFile_+++");
			if(isAvailableToReceive){
				if(isFileExist(fileName))
					sprintf(internetPacket.msgBuffer, "This file exist!");
				else{
					sprintf(internetPacket.msgBuffer, "Yes, I can");
					fileToBeReceivedFD = open(fileName, WRITE_FLAGS, WRITE_PERMS);
					isAvailableToReceive = FALSE;
					continue;
				}
			}
			else
				sprintf(internetPacket.msgBuffer, "No, I can't");

			send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
			continue;
		}

		if((strcmp(internetPacket.msgHeader, "+++_receiveFile_+++") == 0) && (internetPacket.writtenSize != 0))
		{
			bytesRead = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);

			if(strcmp(internetPacket.msgBuffer, "~~~END_OCCURED_HERE!~~~") == 0)
				flag = 1;
			else
			{
				for(i=0; i<internetPacket.writtenSize; ++i){
					bytesWritten = r_write(fileToBeReceivedFD, &(internetPacket.byteArray[i]), 1);
				}

				memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
				internetPacket.writtenSize = 0;
			}
		}

		if(flag){
			r_close(fileToBeReceivedFD);
			isAvailableToReceive = TRUE;
			flag = 0;

			if(sigIntChildFlag){
				break;
			}
		}

	}
	kill(childPid, SIGINT);
	return;
}

void sendFile(int *communicationSocket, char *fileName, int clientID){
	int fileToBeSentFD;
	int bytesRead, i;
	char buffer[MAX_BUFFER_SIZE];
	msgPacket_t internetPacket;

	memset(buffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.msgHeader, '\0', MAX_NAME);
	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);

	fileToBeSentFD = open(fileName, O_RDONLY);
	sprintf(internetPacket.msgHeader, "~~~_sendFile_~~~");
	int counter = 1;
	while(TRUE){

		for(i=0; i<BYTE_ARRAY_SIZE; ++i){
			if((bytesRead = r_read(fileToBeSentFD, &(internetPacket.byteArray[i]), 1)) <= 0){
				sprintf(internetPacket.msgBuffer, "~~~END_OCCURED_HERE!~~~");
				break;
			}
		}

		internetPacket.writtenSize = i;

		send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);

		if(i != BYTE_ARRAY_SIZE)
			break;
	}

	close(fileToBeSentFD);
	return;
}

int processCommand(char *buffer, char *fileName, int *clientIDtoSent){
	char **tokens;
	int tokensNum = 0, choice, i;

	if(fileName == NULL || clientIDtoSent == NULL)
		return ERROR;

	memset(fileName, '\0', MAX_NAME); 
	*clientIDtoSent = -10;

	tokens = (char **)calloc(MAX_TOKENS, sizeof(char *));
	for(i = 0; i<MAX_TOKENS; ++i)
		tokens[i] = (char *)calloc(MAX_BUFFER_SIZE, sizeof(char *));

	if(createTokens(buffer, &tokensNum, tokens) == ERROR){
		freeTokens(tokens);
		return ERROR;
	}

	choice = ERROR;

	if(tokensNum == 1){
		if(strcmp(tokens[0], "listLocal") == 0)
			choice = 1;
		if(strcmp(tokens[0], "listServer") == 0)
			choice = 2;
		if(strcmp(tokens[0], "lsClients") == 0)
			choice = 3;
		if(strcmp(tokens[0], "help") == 0)
			choice = 5;
	}
	else if(tokensNum == 2){
		if(strcmp(tokens[0], "sendFile") == 0){
			choice = 4;
			strncpy(fileName, tokens[1], MAX_NAME);
		}
	}
	else if(tokensNum == 3){
		if((strcmp(tokens[0], "sendFile") == 0) && 
			isNumber(tokens[2]))
		{
			choice = 4;
			*clientIDtoSent = atoi(tokens[2]);
			strncpy(fileName, tokens[1], MAX_NAME);
		}
	}

	freeTokens(tokens);
	return choice;
}

int createTokens(char *buf, int *tokenCounter, char **tokens){
	char *subString;

	if(buf == NULL || tokenCounter == NULL || tokens == NULL)
		return ERROR;

	*tokenCounter = 0;

	subString = strtok(buf, " ");
	while(subString != NULL){
		if(*tokenCounter < MAX_TOKENS)
			strncpy(tokens[*tokenCounter], subString, MAX_BUFFER_SIZE);

		++(*tokenCounter);
		subString = strtok(NULL, " ");
	}

	if(*tokenCounter > MAX_TOKENS)
		return ERROR;

	return 0;
}

void freeTokens(char **tokens){
	if(tokens == NULL)
		return;

	int i;
	for(i = 0; i<MAX_TOKENS; ++i){
		free(tokens[i]);
		tokens[i] = NULL;
	}
	free(tokens);
	tokens = NULL;
}

int isFileExist(char *fileName){
	struct stat status;
	return (stat(fileName, &status) == 0);
}

void listLocal(){
	DIR *dirPtr = NULL;
	struct dirent *dirFilesPtr = NULL;
	char currentDirectory[MAX_PATH_NAME];

	if(getcwd(currentDirectory, MAX_PATH_NAME) == NULL){
		perror("Failed to get current working directory!\n");
		return;
	}

	if((dirPtr = opendir(currentDirectory)) == NULL){
		perror("Failed to open the directory!\n");
		return;
	}

	system("clear");

	int counter = 0;
	fprintf(stderr, "*************************LOCAL FILES*************************\n");
	while((dirFilesPtr = readdir(dirPtr)) != NULL)
	{
		if((strcmp(dirFilesPtr->d_name, "..")==0) || 
			(strcmp(dirFilesPtr->d_name, ".")==0))
			continue;
		fprintf(stderr, "%s\n", dirFilesPtr->d_name);
	}
	fprintf(stderr, "*************************************************************\n");

	while((closedir(dirPtr) == ERROR) && (errno == EINTR));
}

void listServer(int *communicationSocket){
	int bytesPeeked = -10, bytesRead = -10;
	msgPacket_t internetPacket;

	memset(internetPacket.msgHeader, '\0', MAX_NAME);
	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
	internetPacket.writtenSize = 0;
	system("clear");

	fprintf(stderr, "*************************SERVER FILES************************\n");
	while(TRUE){

		bytesPeeked = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);
		if(strcmp(internetPacket.msgHeader, "~~~_listServer_~~~") != 0)
			continue;

		bytesRead = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
		if(bytesRead <= 0){
			perror("ERROR OCCURED WHILE READING FROM COMMUNICATION SOCKET!\n");
			break;
		}

		if(strcmp(internetPacket.msgBuffer, "~~~END_OCCURED_HERE!~~~") == 0)
			break;

		fprintf(stderr, "%s\n", internetPacket.msgBuffer);
		memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	}
	fprintf(stderr, "*************************************************************\n");
}

void lsClients(int *communicationSocket){
	int bytesRead = -10, bytesPeeked = -10;
	msgPacket_t internetPacket;

	memset(internetPacket.msgHeader, '\0', MAX_NAME);
	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
	internetPacket.writtenSize = 0;
	system("clear");

	fprintf(stderr, "********************CONNECTED CLIENTS LIST*******************\n");
	while(TRUE){

		bytesPeeked = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);
		if(strcmp(internetPacket.msgHeader, "~~~_lsClients_~~~") != 0)
			continue;

		bytesRead = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
		if(bytesRead <= 0){
			perror("ERROR OCCURED WHILE READING FROM COMMUNICATION SOCKET!\n");
			break;
		}

		if(strcmp(internetPacket.msgBuffer, "~~~END_OCCURED_HERE!~~~") == 0)
			break;

		fprintf(stderr, "%s\n", internetPacket.msgBuffer);
		memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	}
	fprintf(stderr, "*************************************************************\n");
}

void help(){
	system("clear");
	fprintf(stderr, "*****************************HELP****************************\n");
	fprintf(stderr, "\"listLocal\" - to list the local files in the directory\n");
	fprintf(stderr, "client program started.\n");
	fprintf(stderr, "\"listServer\" - to list the files in the current scope\n");
	fprintf(stderr, " of the server-client.\n");
	fprintf(stderr, "\"lsClients\" - to list the clients currently connected \n");
	fprintf(stderr, "to the server with their respective clientids.\n");
	fprintf(stderr, "\"sendFile <fileName> <clientid>\" - to send file <fileName>\n");
	fprintf(stderr, "(if the file exists) from local directory to the client\n");
	fprintf(stderr, "with id <clientid>. If no client id is given\n");
	fprintf(stderr, "the file is send to the server's local directory.\n");
	fprintf(stderr, "\"help\" - to display the available comments and their usage.\n");
	fprintf(stderr, "\"exit\" - to exit from the program.\n");
	fprintf(stderr, "*************************************************************\n");
}
