#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <printf.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "gfserver.h"
#include "cache-student.h"

#define BUFSIZE (6200)
bool g_workerThreadsEnabled;

//Replace with an implementation of handle_with_cache and any other
//functions you may need.
ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg)
{
	printf("Entering %s \n", __FUNCTION__);
	
	size_t bytes_transferred = 0;	
	ContxtWebProxy_t *webProxyCntxt = (ContxtWebProxy_t*) arg;
	void* shmDataAddr;// = contxtProxy + 1;
	
	//Lock the queue
	pthread_mutex_lock(&g_proxyLock->mutex);
	while(steque_isempty(g_proxyQueue))
	{
		pthread_cond_wait(&g_proxyLock->cond, &g_proxyLock->mutex);
	}

	//Read the queue
	ContextProxy_t * contxtProxy = (ContextProxy_t*) steque_pop(g_proxyQueue);
	//Unlock the queue
	pthread_mutex_unlock(&g_proxyLock->mutex);
	if (contxtProxy == NULL)
	{
		//report and go back & wait for boss's order
		printf("WARNING: Failed to read request queue in thread \n");
		goto Exit;
	}
	
	MQFileRequest_t requestFile;	
	sprintf(requestFile.filePath, "%s", path);
	requestFile.nSegments = webProxyCntxt->nSegments;
	requestFile.segmentSize = webProxyCntxt->segmentSize;
	strcpy(requestFile.shmName, contxtProxy->shmName);

	if (webProxyCntxt->mqRequest < 0)
	{
		perror("webProxyCntxt->mqResponse is invalid : ");
		return SERVER_FAILURE;
	}
	
	printf("requestFile.filePath %s \n", requestFile.filePath);
	if (mq_send(webProxyCntxt->mqRequest, (const char *) &requestFile, sizeof(MQFileRequest_t), 0) == -1)
	{
		printf("Error: mq_send Failed ErrCode : %d webProxyCntxt->mqRequest %d TID: %ld \n", errno, webProxyCntxt->mqRequest, syscall(SYS_gettid));
		bytes_transferred = 0;
		return SERVER_FAILURE;
	}

	sem_wait(&contxtProxy->shmContext->semaphoreRD); //TOP Lock

	if (contxtProxy->shmContext->status == GF_OK)
	{
		// POST GF_OK
		printf("Posting gf_sendHeader GF_OK file Len %zu \n", contxtProxy->shmContext->fileLen);
		size_t result = gfs_sendheader(ctx, GF_OK, contxtProxy->shmContext->fileLen);
		if (result == -1)
		{
			perror("gfs_sendheader err !");
		}

		size_t write_len = 0;
		bytes_transferred = 0;
		while(bytes_transferred < contxtProxy->shmContext->fileLen)
		{	
			if (contxtProxy->shmContext->dataLength <= 0)
			{
				fprintf(stderr, "handle_with_cache read error, %zd, %zu, %zu", 
									contxtProxy->shmContext->dataLength, bytes_transferred,contxtProxy->shmContext->fileLen );

				return SERVER_FAILURE;
			}
			
			shmDataAddr = contxtProxy->shmContext + 1;
			char localBuf[contxtProxy->shmContext->dataLength];
			
			// printf("Actual dataLength %zu \n", contxtProxy->shmContext->dataLength);
			// printf("Actual segSize %zu \n", webProxyCntxt->segmentSize);
			// printf("Size of ContextShm_t %zu \n",sizeof(ContextShm_t));

			memcpy(&localBuf, shmDataAddr, contxtProxy->shmContext->dataLength); // Copy only read length not segSize - sizeof(ContextShm_t) here, writer takes care of writing only that calculated amount.

			write_len = gfs_send(ctx, &localBuf, contxtProxy->shmContext->dataLength);	

			if (write_len != contxtProxy->shmContext->dataLength)
			{
				perror("handle_with_file write error");
				return SERVER_FAILURE;
			}
			
			bytes_transferred += write_len;

			if (sem_post(&contxtProxy->shmContext->semaphoreWR) != 0)
			{
				perror("Giving a turn to SEM_POST Write Failed ");
			}

			sem_wait(&contxtProxy->shmContext->semaphoreRD);

		} //while (file_length)
		
	}
	else
	{
		printf("Function %s Line: %d calling gfs_sendheader GF_FILE_NOT_FOUND ! \n", __FUNCTION__, __LINE__);			
		gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
		// //Post it to writer to go with another request now
		// if (sem_post(&contxtProxy->shmContext->semaphoreWR) != 0)
		// {
		// 	perror("sem_post it to writer to send data now Failed ");
		// }
		// sem_wait(&contxtProxy->shmContext->semaphoreRD);
	}

	printf("File posted %zu of file %s \n", bytes_transferred, contxtProxy->shmContext->filePath);

	//Post it to writer to go with another request now	
	if (sem_post(&contxtProxy->shmContext->semaphoreWR) != 0)
	{
		perror("sem_post it to writer to send data now Failed ");
	}

Exit:
	// Release the shared memory for another thread
	printf("Exit: %s bytes_transferred: %zu FileLen: %zu FilePath %s \n", __FUNCTION__, bytes_transferred, contxtProxy->shmContext->fileLen, contxtProxy->shmContext->filePath);	
	if (g_proxyQueue)
	{
		printf("Pushing to queue ++ \n");
		contxtProxy->shmContext->fileLen = 0;
		bzero(contxtProxy->shmContext->filePath, MAX_PATH);

		contxtProxy->shmContext->dataLength = 0;

		pthread_mutex_lock(&g_proxyLock->mutex);
		steque_enqueue(g_proxyQueue, contxtProxy);
		pthread_mutex_unlock(&g_proxyLock->mutex);
		pthread_cond_broadcast(&g_proxyLock->cond);
	}
	
	return bytes_transferred;
}

// ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg){
// 	int fildes;
// 	size_t file_len, bytes_transferred;
// 	ssize_t read_len, write_len;
// 	char buffer[BUFSIZE];
// 	char *data_dir = arg;
//     struct stat st;

// 	strcpy(buffer,data_dir);
// 	strcat(buffer,path);

// 	if( 0 > (fildes = open(buffer, O_RDONLY))){
// 		if (errno == ENOENT)
// 			/* If the file just wasn't found, then send FILE_NOT_FOUND code*/ 
// 			return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
// 		else
// 			/* Otherwise, it must have been a server error. gfserver library will handle*/ 
// 			return SERVER_FAILURE;
// 	}

// 	/* Calculating the file size */
// 	if (0 > fstat(fildes, &st)) {
// 			perror("handle_with_cache call to fstat");
// 			return -1;
// 	}
// 	file_len = st.st_size;

// 	gfs_sendheader(ctx, GF_OK, file_len);

// 	/* Sending the file contents chunk by chunk. */
// 	bytes_transferred = 0;
// 	while(bytes_transferred < file_len){
// 		read_len = read(fildes, buffer, BUFSIZE);
// 		if (read_len <= 0){
// 			fprintf(stderr, "handle_with_file read error, %zd, %zu, %zu", read_len, bytes_transferred, file_len );
// 			return SERVER_FAILURE;
// 		}
// 		write_len = gfs_send(ctx, buffer, read_len);
// 		if (write_len != read_len){
// 			fprintf(stderr, "handle_with_file write error");
// 			return SERVER_FAILURE;
// 		}
// 		bytes_transferred += write_len;
// 	}

// 	return bytes_transferred;
// }

