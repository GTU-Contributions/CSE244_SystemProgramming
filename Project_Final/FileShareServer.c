/****************************************************
 *													*
 *     BIL244 System Programming Final Project    	*
 * 				fileShareServer.c					*
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
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <time.h>
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
#define ABORT -2
#define MAX_NAME 256
#define MAX_PATH_NAME 512
#define MAX_BUFFER_SIZE 256
#define BYTE_ARRAY_SIZE 16384
#define MAX_PENDING_CLIENTS 20
#define MAX_CLIENTS 255

/* RECEIVING FILE STUFF */
#define WRITE_FLAGS (O_RDWR | O_CREAT | O_EXCL)
#define WRITE_PERMS (mode_t)(S_IRWXU | S_IRWXG | S_IRWXO)

/* SEMAPHORE STUFF */
#define SEM_LOG "CLIENT_LOG_SEM"
#define FLAGS (O_CREAT | O_EXCL)
#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

/* Socketler uzerinde aktarilan paket struct */
typedef struct msgPacket{
	char msgHeader[MAX_NAME];
	char msgBuffer[MAX_BUFFER_SIZE];
	char byteArray[BYTE_ARRAY_SIZE];
	int writtenSize;
}msgPacket_t;

/* Client bilgilerini tutmak icin struct */
typedef struct Client{
	int clientID;
	char hostName[MAX_NAME];
	int clientSocket;
	pid_t childPid;
}Client_t;

Client_t *clientsArray;
static int totalActiveClients = 0;
static int currentClientID = 0;

static int sigIntFlag = 0;
static int sigPipeFlag = 0;

sem_t *log_semPtr = NULL;

/* Client disconnect olduktan sonra child processi yakala ve arrayden sil */
void sigchld_handler(int sig){
	pid_t child;
	int status;

	child = waitpid(-1, &status, 0);
	if(removeClient(child) == ERROR)
		fprintf(stderr, "Failed to catch child[%d] after termination!\n", (int)child);
}

/* CTRL + C handling */
void sigint_handler(int sig){
	unlink("clientsLog.bin");
	sigIntFlag = 1;
}

/* CTRL + C for child processes */
void child_sigint_handler(int sig){
	sem_close(log_semPtr);
	exit(0);
}

/* Client arrayini initialize ediyor */
void initializeClientsArray();
/* Arraye client ekliyor */
int addClient(pid_t childPid, char *hostName, int socket);
/* Arrayden client cikariyor */
int removeClient(pid_t childPid);
/* Tum cocuklara SIGINT gonder */
void killChildren();
/* Gelen string sayi mi diye kontrol eder */
int isNumber(char *numString);
/* Maine arguman olarak gelen bilgileri ceker */
int getPortValueFromArguments(int argc, char *argv[], portNumber_t *portNumber);


/*----------------BU FONKSIYONLAR KITAPTAN ALINMISTIR-------------------*/
/* Addressi hosta donusturur */
void addr2name(struct in_addr addr, char *name, int namelen);
/* Hostu addresse donusturur */
int name2addr(char *name, in_addr_t *addrp);
/* Named semaphore uretir */
int getNamedSemaphore(char *semName, sem_t **sem, int val);
/* Named semaphore temizler */
int destroyNamedSemaphore(char *semName, sem_t *sem);
/*----------------------------------------------------------------------*/

/* Clientlerin gelmelerini beklemek icin port acar */
int openPortForListening(portNumber_t port);
/* Gelen client var ise onu kabul eder */
int acceptClientRequest(int socket, char *hostName, int hostNameSize);
/* Serveri baslatir, serverin ana fonksiyonudur */
void startTheServer(portNumber_t port);

/* Server ise Clientlerle ilgili bilgileri loga basar
   Child ise Clientlerle ilgili bilgileri logtan okur */
void processClientLog(int isServer, int *communicationSocket);
/* Gelen clientID den clientin servera baglandigi soketi bulur */
int getClientSocket(int clientID, int *clientSocket);

/* Gelen dosya isminde bir dosya var mi diye bakar */
int isFileExist(char *fileName);
/* Socket uzerinden gelen mesaja gore clientin gonderdigi commandi alir */
int getCommandNumber(char *buf);
/* Child processler icin ana fonksiyon */
void processClientCommands(int *communicationSocket);
/* Server clientten dosya alir */
void getFile(int *communicationSocket, char *fileName);
/* Servera gelen clientID ye gore socketini bulur ve o sockete fileName dosyasini yonlendirir */
void sendFileToClientID(int *communicationSocket, char *fileName, int clientIDtoSent);
/* Iki soket arasinda mesaj aktarimi yapar, sendFileToClientID fonksiyonunda kullanilmistir */
int copyFileFromSocketToSocket(int *fromSocket, int *toSocket);
/* Serverin bulundugu directory iceriginde ki dosyalari istek yapmis cliente gonderir */
void listServer(int *communicationSocket);
/* Servera su anda bagli olan tum clientleri istek yapmis cliente gonderir */
void lsClients(int *communicationSocket);


int main(int argc, char *argv[]){
 	portNumber_t port;

 	if(getPortValueFromArguments(argc, argv, &port)){
 		fprintf(stderr, "Usage: ./fileShareServer <Port Number>\n");
 		fprintf(stderr, "Example: ./fileShareServer 15666\n");
 		return ERROR;
 	}

 	fprintf(stderr, "The server port number is: %d\n", (int)port);
 	fprintf(stderr, "Server is starting...\n");

 	startTheServer(port);

 	return 0;
}

void initializeClientsArray(){
	int i;

	clientsArray = (Client_t *)malloc(MAX_CLIENTS*sizeof(Client_t));

	for(i=0; i<MAX_CLIENTS; ++i){
		clientsArray[i].clientID = -10;
		clientsArray[i].childPid = -10;
		clientsArray[i].clientSocket = -10;
		memset(clientsArray[i].hostName, '\0', MAX_NAME);
	}
}

int addClient(pid_t childPid, char *hostName, int socket){
	if(clientsArray == NULL)
		return ERROR;

	int i;

	for(i=0; i<MAX_CLIENTS; ++i)
		if(clientsArray[i].childPid == -10)
		{
			clientsArray[i].childPid = childPid;
			clientsArray[i].clientID = currentClientID;
			clientsArray[i].clientSocket = socket;
			strncpy(clientsArray[i].hostName, hostName, MAX_NAME);
			++totalActiveClients;
			++currentClientID;
			processClientLog(TRUE, NULL);
			return 0;
		}

	return ERROR;
}

int removeClient(pid_t childPid){
	if(clientsArray == NULL)
		return ERROR;

	int i;

	for(i=0; i<MAX_CLIENTS; ++i)
		if(clientsArray[i].childPid == childPid)
		{
			clientsArray[i].childPid = -10;
			clientsArray[i].clientID = -10;
			clientsArray[i].clientSocket = -10;
			memset(clientsArray[i].hostName, '\0', MAX_NAME);
			--totalActiveClients;
			processClientLog(TRUE, NULL);
			return 0;
		}

	return ERROR;
}

void killChildren(){
	int i;

	if(clientsArray == NULL)
		return;

	for(i=0; i<MAX_CLIENTS; ++i)
		if(clientsArray[i].childPid != -10)
			kill(clientsArray[i].childPid, SIGINT);
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

int getPortValueFromArguments(int argc, char *argv[], portNumber_t *portNumber){
	int port;

 	if(argc != 2){
 		fprintf(stderr, "Error: The arguments must be 2!\n");
 		return ERROR;
 	}
 	if(!isNumber(argv[1])){
 		fprintf(stderr, "Error: The port must be a non-negative number!\n");
 		return ERROR;
 	}

 	port = atoi(argv[1]);

 	if(port > 65535){
 		fprintf(stderr, "Error: The port number can't be bigger than 65535!\n");
 		return ERROR;
 	}

 	*portNumber = (portNumber_t)port;

 	return 0;
}

void addr2name(struct in_addr addr, char *name, int namelen){
	struct hostent *hostPtr;
	hostPtr = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);

	if(hostPtr == NULL){
		strncpy(name, inet_ntoa(addr), namelen-1);
	}
	else{
		strncpy(name, hostPtr->h_name, namelen-1);
	}

	name[namelen-1] = 0;
}

int name2addr(char *name, in_addr_t *addrp) {
	struct hostent *hp;
	if (isdigit((int)(*name)))
		*addrp = inet_addr(name);
	else {
		hp = gethostbyname(name);
		if(hp == NULL)
			return -1;
		memcpy((char *)addrp, hp->h_addr_list[0], hp->h_length);
	}

	return 0;
}

int getNamedSemaphore(char *semName, sem_t **sem, int val){

	while (((*sem = sem_open(semName, FLAGS, PERMS, val)) == SEM_FAILED) &&
									(errno == EINTR)) ;
	if(*sem != SEM_FAILED)
		return 0;
	if(errno != EEXIST)
		return -1;
	while(((*sem = sem_open(semName, 0)) == SEM_FAILED) && (errno == EINTR));

	if(*sem != SEM_FAILED)
		return 0;

	return -1;
}

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

int openSocketForListening(portNumber_t port){
	struct sockaddr_in server_info;
	int socketFD;
	int error;
	int trueValue = 1;
	int flags;

	/* Creating communication endpoint (socketFD) */
	if((socketFD = socket(AF_INET, SOCK_STREAM, 0)) == ERROR)
		return ERROR;

	flags = fcntl(socketFD, F_GETFL, 0);
	fcntl(socketFD, F_SETFL, flags | O_NONBLOCK);

	if(setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, (char *)&trueValue, sizeof(trueValue)) == ERROR){
		error = errno;
		while((close(socketFD) == ERROR) && (errno == EINTR));
		errno = error;
		return ERROR;
	}

	memset((char *)&server_info, '\0', sizeof(server_info));
	server_info.sin_family = AF_INET;
	server_info.sin_addr.s_addr = htonl(INADDR_ANY);
	server_info.sin_port = htons((short)port);

	/* Associate (socketFD) endpoint with the port number passed as a parameter */
	if(bind(socketFD, (struct sockaddr *)&server_info, sizeof(server_info)) == ERROR){
		error = errno;
		while((close(socketFD) == ERROR) && (errno == EINTR));
		errno = error;
		return ERROR;
	}

	/* Make (socketFD) endpoint passive listener */
	if(listen(socketFD, MAX_PENDING_CLIENTS) == ERROR){
		error = errno;
		while((close(socketFD) == ERROR) && (errno == EINTR));
		errno = error;
		return ERROR;
	}

	return socketFD;
}

int acceptClientRequest(int socket, char *hostName, int hostNameSize){
	struct sockaddr_in client_addr;
	int size = sizeof(struct sockaddr);
	int status;

	while(TRUE){
		status = accept(socket, (struct sockaddr *)(&client_addr), &size);
		if(status == ERROR && errno == EAGAIN){
			if(sigIntFlag)
				break;
		}
		else
			break;
	}
	
	if(sigIntFlag)
		return ABORT;

	if((status == -1) || (hostName == NULL) || (hostNameSize <= 0))
		return status;

	addr2name(client_addr.sin_addr, hostName, hostNameSize);

	return status;
}

void startTheServer(portNumber_t port){
	time_t serverStartingTime = time(NULL);
	struct tm *serverStartingTimePtr = localtime(&serverStartingTime);
	time_t clientConnectTime;
	struct tm *clientConnectTimePtr;
	pid_t childPid, serverPid = getpid();
	int socketForCommunication, socketForListening;
	char client[MAX_NAME];

	/* Open listening socket to wait for clients */
	if((socketForListening = openSocketForListening(port)) == ERROR){
		perror("Failed to open socket for listening!\n");
		return;
	}

	/* Create the named semaphore for the clients log */
	if(getNamedSemaphore(SEM_LOG, &log_semPtr, 1) == ERROR){
		perror("Failed to create named semaphore!\n");
		r_close(socketForListening);
		return;
	}

	initializeClientsArray();

	fprintf(stderr, "Server[%ld]: I am waiting for a client to connect!\n", (long)serverPid);
	while(TRUE){
		signal(SIGCHLD, sigchld_handler);
		signal(SIGINT, sigint_handler);

		memset(client, '\0', MAX_NAME);
		
		/* Accept coming client requests */
		if(socketForCommunication = acceptClientRequest(socketForListening, client, MAX_NAME)){
			if(socketForCommunication == ERROR){
				perror("Failed to accept connection!\n");
				continue;
			}
			else if(socketForCommunication == ABORT){
				break;
			}
		}

		if(sigIntFlag) /* Break if CTRL + C is pressed */
			break;

		/* Find the connection time of a client */
		clientConnectTime = time(NULL);
		clientConnectTimePtr = localtime(&clientConnectTime);
		fprintf(stderr, "Server[%ld]: Client has been connected at: ", (long)serverPid);
		fprintf(stderr, "%s", asctime(clientConnectTimePtr));

		/* Fork a child after connection of the client */
		if((childPid = fork()) == ERROR){
			perror("Failed to accept connection!\n");
			continue;
		}

		if(childPid == 0){ /* Child code */
			signal(SIGINT, child_sigint_handler);
			free(clientsArray);
			clientsArray = NULL;
			if(r_close(socketForListening) == ERROR){
				fprintf(stderr, "Child[%ld]: Failed to close listening socket: %s!\n",
						(long)getpid(), strerror(errno));
				return;
			}
			/* Cild processin ana fonksiyonunu cagir */
			processClientCommands(&socketForCommunication);
			sem_close(log_semPtr);
			r_close(socketForCommunication);
			exit(0);
		}

		/* Parent code */
		addClient(childPid, client, socketForCommunication);
		fprintf(stderr, "Total number of the connected clients: %d\n", totalActiveClients);

		if(r_close(socketForCommunication) == ERROR)
			fprintf(stderr, "Server[%ld]: failed to close socket for communication!\n", (long)serverPid);

		if(sigIntFlag) /* Break if CTRL + C is pressed */
			break;
	}

	/* CTRL + C geldikten sonra yapilanlar */

	// Tum cocuklari oldur
	killChildren(); 
	// Mallocla aldiklarini free et
	free(clientsArray);
	clientsArray = NULL;
	unlink("clientsLog.bin");

	if(destroyNamedSemaphore(SEM_LOG, log_semPtr) == ERROR){
		perror("Failed to destroy named semaphore!");
	}

	if(r_close(socketForListening) == ERROR)
		fprintf(stderr, "Server[%ld]: failed to close socket for listening!\n", (long)serverPid);

	while(r_waitpid(-1, NULL, WNOHANG) > 0); /* Clean up zombies */
	return;
}

/* Birinci parametre olarak TRUE gelirse server kismi, FALSE gelirse child kismi calisir*/
void processClientLog(int isServer, int *communicationSocket){
	FILE *clientLog;
	int i, activeClients, clientID;
	char buffer[MAX_BUFFER_SIZE];
	char hostName[MAX_NAME];
	msgPacket_t internetPacket;
	Client_t temp;

	/* Entry to the semaphore area */
	sem_wait(log_semPtr);

	if(isServer) // Server write's to the log file
	{
		clientLog = fopen("clientsLog.bin", "wb");

		fwrite(&totalActiveClients, sizeof(int), 1, clientLog);
		for(i=0; i<MAX_CLIENTS; ++i)
			if(clientsArray[i].clientID != -10)
				fwrite(&clientsArray[i], sizeof(Client_t), 1, clientLog);

		fclose(clientLog);
	}
    else // Child process read's from the log file
    {
    	clientLog = fopen("clientsLog.bin", "rb");

    	memset(internetPacket.msgHeader, '\0', MAX_NAME);
		memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
		memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
		internetPacket.writtenSize = 0;

		/* Cliente hazir olmasi icin mesaj gonder */
    	sprintf(internetPacket.msgHeader, "~~~_lsClients_~~~");
    	sprintf(internetPacket.msgBuffer, "Get ready to receive information about the connected clients!");
		send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);

		memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);

		/* Log dosyasindan okunan bilgileri cliente gonder */
		fread(&activeClients, sizeof(int), 1, clientLog);
		for(i=0; i<activeClients; ++i){
			fread(&temp, sizeof(Client_t), 1, clientLog);
			sprintf(internetPacket.msgBuffer, "ClientID: %d, ClientHost: %s", temp.clientID, temp.hostName);
			send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
			memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
		}

		/* Cliente mesajin bittigini bildir */
		sprintf(internetPacket.msgBuffer, "%s", "~~~END_OCCURED_HERE!~~~");
		send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
    	fclose(clientLog);
    }

	/* Exit from the semaphore area */
	sem_post(log_semPtr);
}

int getClientSocket(int clientID, int *clientSocket){
	FILE *clientLog;
	int currentScannedID, activeClients, i;
	char hostName[MAX_NAME];
	char buffer[MAX_BUFFER_SIZE];
	Client_t temp;

	/* Entry to the semaphore area */
	sem_wait(log_semPtr);

	clientLog = fopen("clientsLog.bin", "rb");
	if(clientLog == NULL){
		sem_post(log_semPtr);
		return ERROR;
	}

	fread(&activeClients, sizeof(int), 1, clientLog);
	for(i=0; i<activeClients; ++i){
		fread(&temp, sizeof(Client_t), 1, clientLog);
		if(temp.clientID == clientID)
			*clientSocket = temp.clientSocket;
	}

	fclose(clientLog);

	/* Exit from the semaphore area */
	sem_post(log_semPtr);

	return 0;
}

int isFileExist(char *fileName){
	struct stat status;
	return (stat(fileName, &status) == 0);
}

int getCommandNumber(char *buf){
	if(buf == NULL)
		return ERROR;

	if(strcmp(buf, "listServer") == 0)
		return 2;
	if(strcmp(buf, "lsClients") == 0)
		return 3;
	if(strncmp(buf, "sendFile", 8) == 0)
		return 4;

	return ERROR;
}

void processClientCommands(int *communicationSocket){
	int bytesPeeked = -10, bytesRead = -10;
	char buffer[MAX_BUFFER_SIZE];
	int clientCommand, clientIDtoSent;
	char fileName[MAX_NAME];
	msgPacket_t internetPacket;

	if(communicationSocket == NULL)
		return;
	
	/* Surekli socketi dinle client commandi icin */
	while(TRUE){

		memset(fileName, '\0', MAX_NAME);
		memset(internetPacket.msgHeader, '\0', MAX_NAME);
		memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);

		clientCommand = -1;
		clientIDtoSent = -10;
		/* Mesaji peek yap, sana gelip gelmedigini anlamak icin*/
		bytesPeeked = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);

		if(bytesPeeked <= 0){
			break;
		}

		/* Gelen mesaji incele ve sana gelmisse mesaji queuedan cek */
		clientCommand = getCommandNumber(internetPacket.msgHeader);
		if(clientCommand != ERROR){
			bytesRead = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
		}

		/* sendFile gelmisse dosya ismi ve client id oku mesajdan */
		if(clientCommand == 4){
			sscanf(internetPacket.msgBuffer, "%s %d", fileName, &clientIDtoSent);
		}

		/* Gelen komuta gore bir fonksiyon cagir */
		switch(clientCommand){
			/* listServer */
			case 2:
				listServer(communicationSocket);
				break;
			/* lsClients */	
			case 3:
				lsClients(communicationSocket);
				break;
			/* sendFile <fileName> <clientID> */
			case 4:
				/* -10 Servera gonder demek */
				if(clientIDtoSent == -10){
					getFile(communicationSocket, fileName);
				}
				else{
					sendFileToClientID(communicationSocket, fileName, clientIDtoSent);
				}
				break;
			default:
				continue;
		}
	}
}

void getFile(int *communicationSocket, char *fileName){
	msgPacket_t internetPacket;
	int fileToBeReceivedFD = -99;
	int bytesWritten, bytesPeeked, bytesRead, i, flag = 0;
	char newFileName[MAX_NAME];

	memset(internetPacket.msgHeader, '\0', MAX_NAME);
	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
	internetPacket.writtenSize = 0;

	i = 0;
	while(isFileExist(fileName)){
		sprintf(newFileName, "%d_%s", i, fileName);
		++i;
		sprintf(fileName, "%s", newFileName);
	}

	fileToBeReceivedFD = open(fileName, WRITE_FLAGS, WRITE_PERMS);

	sprintf(internetPacket.msgHeader, "~~~_sendFile_~~~");
	sprintf(internetPacket.msgBuffer, "Destination is available");
	send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);	

	int counter = 1;
	while(TRUE){

		internetPacket.writtenSize = 0;

		bytesPeeked = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);

		if(bytesPeeked <= 0){
			fprintf(stderr, "CANT PEEK\n");
			break;
		}

		if(strcmp(internetPacket.msgHeader, "~~~_sendFile_~~~") != 0)
			continue;

		bytesRead = recv(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);

		if(strcmp(internetPacket.msgBuffer, "~~~END_OCCURED_HERE!~~~") == 0){
			flag = 1;
		}

		for(i=0; i<internetPacket.writtenSize; ++i){
			bytesWritten = r_write(fileToBeReceivedFD, &(internetPacket.byteArray[i]), 1);
		}
		if(bytesWritten == -1){
			fprintf(stderr, "BYTES WRITTEN == -1\n");
			break;
		}
		if(flag){
			fprintf(stderr, "Server: Received new file \"%s\" !\n", fileName);
			break;
		}
	}

	close(fileToBeReceivedFD);
	return;
}

void sendFileToClientID(int *communicationSocket, char *fileName, int clientIDtoSent){
	msgPacket_t internetPacket;
	int bytesPeeked, bytesRead;
	int clientSocketToSent;

	memset(internetPacket.msgHeader, '\0', MAX_NAME);
	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
	internetPacket.writtenSize = 0;

	if(fileName == NULL)
		fprintf(stderr, "NULL!! sendFileToClientID");

	if(getClientSocket(clientIDtoSent, &clientSocketToSent)){
		sprintf(internetPacket.msgHeader, "~~~_sendFile_~~~");
		sprintf(internetPacket.msgBuffer, "Destination is not available");
		send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
		return;
	}

	sprintf(internetPacket.msgHeader, "+++_NewReceiveFile_+++");
	sprintf(internetPacket.msgBuffer, "Can you handle a file %s", fileName);
	send(clientSocketToSent, &internetPacket, sizeof(msgPacket_t), 0);

	while(TRUE){

		bytesPeeked = recv(clientSocketToSent, &internetPacket, sizeof(msgPacket_t), MSG_PEEK);
		if(bytesPeeked <= 0){
			sprintf(internetPacket.msgHeader, "~~~_sendFile_~~~");
			sprintf(internetPacket.msgBuffer, "Destination is not available");
			send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
			return;
		}

		// Get info if destination is available
		if(strcmp(internetPacket.msgHeader, "+++_sendFile_+++") == 0){

			bytesRead = recv(clientSocketToSent, &internetPacket, sizeof(msgPacket_t), 0);
			if(strcmp(internetPacket.msgBuffer, "Yes, I can") == 0){
				sprintf(internetPacket.msgHeader, "~~~_sendFile_~~~");
				sprintf(internetPacket.msgBuffer, "Destination is available");
				send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);				
				break;
			}
			else if(strcmp(internetPacket.msgBuffer, "No, I can't") == 0) // "No I can't" case
			{
				sprintf(internetPacket.msgHeader, "~~~_sendFile_~~~");
				sprintf(internetPacket.msgBuffer, "Destination is busy");
				send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
				return;
			}
			else if(strcmp(internetPacket.msgBuffer, "This file exist!") == 0) // File already exist in others directory  
			{
				sprintf(internetPacket.msgHeader, "~~~_sendFile_~~~");
				sprintf(internetPacket.msgBuffer, "Already existing \"%s\" in client: %d", fileName, clientIDtoSent);
				send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
				return;
			}
		}
	}
	
	copyFileFromSocketToSocket(communicationSocket, &clientSocketToSent);
	
	return;
}

int copyFileFromSocketToSocket(int *fromSocket, int *toSocket){
	char *bp = NULL;
	int bytesWritten, bytesRead, bytesPeeked;
	int totalBytes = 0;
	msgPacket_t internetPacketFrom;
	msgPacket_t internetPacketTo;

	memset(internetPacketFrom.msgHeader, '\0', MAX_NAME);
	memset(internetPacketFrom.msgBuffer, '\0', MAX_BUFFER_SIZE);

	memset(internetPacketTo.msgHeader, '\0', MAX_NAME);
	memset(internetPacketTo.msgBuffer, '\0', MAX_BUFFER_SIZE);

	while(TRUE){

		bytesPeeked = recv(*fromSocket, &internetPacketFrom, sizeof(msgPacket_t), MSG_PEEK);

		if(bytesPeeked <= 0)
			break;

		if(strcmp(internetPacketFrom.msgHeader, "~~~_sendFile_~~~") != 0)
			continue;

		bytesRead = recv(*fromSocket, &internetPacketFrom, sizeof(msgPacket_t), 0);

		if(bytesRead <= 0)
			break;

		internetPacketTo = internetPacketFrom;

		sprintf(internetPacketTo.msgHeader, "+++_receiveFile_+++");

		bytesWritten = send(*toSocket, &internetPacketTo, sizeof(msgPacket_t), 0);

		if(bytesWritten == -1)
			break;

		if(strcmp(internetPacketFrom.msgBuffer, "~~~END_OCCURED_HERE!~~~") == 0)
			break;
	}

	return totalBytes;
}

void listServer(int *communicationSocket){
	DIR *dirPtr = NULL;
	struct dirent *dirFilesPtr = NULL;
	char currentDirectory[MAX_PATH_NAME];
	msgPacket_t internetPacket;

	if(communicationSocket == NULL)
		return;

	if(getcwd(currentDirectory, MAX_PATH_NAME) == NULL){
		perror("Failed to get current working directory!\n");
		return;
	}

	if((dirPtr = opendir(currentDirectory)) == NULL){
		perror("Failed to open the directory!\n");
		return;
	}

	memset(internetPacket.msgHeader, '\0', MAX_NAME);
	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	memset(internetPacket.byteArray, '\0', BYTE_ARRAY_SIZE);
	internetPacket.writtenSize = 0;

	sprintf(internetPacket.msgHeader, "~~~_listServer_~~~");
	sprintf(internetPacket.msgBuffer, "Get ready to receive information about the server files!");
	send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);

	memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	while((dirFilesPtr = readdir(dirPtr)) != NULL){
		if((strcmp(dirFilesPtr->d_name, "..")==0) || 
			(strcmp(dirFilesPtr->d_name, ".")==0))
			continue;

		sprintf(internetPacket.msgBuffer, "%s", dirFilesPtr->d_name);
		send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);
		memset(internetPacket.msgBuffer, '\0', MAX_BUFFER_SIZE);
	}

	sprintf(internetPacket.msgBuffer, "%s", "~~~END_OCCURED_HERE!~~~");
	send(*communicationSocket, &internetPacket, sizeof(msgPacket_t), 0);

	while((closedir(dirPtr) == ERROR) && (errno == EINTR));
	dirPtr = NULL;
}

/* Klient bilgilerini gonder */
void lsClients(int *communicationSocket){

	processClientLog(FALSE, communicationSocket);
}