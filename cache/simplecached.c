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
#include <unistd.h>
#include <curl/curl.h>

#include "cache-student.h"
#include "gfserver.h"
#include "shm_channel.h"
#include "simplecache.h"

#include <fcntl.h> // For O_ constants
#include <sys/stat.h> // MODE constants
#include <mqueue.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>

#define DEBUG_LOG 1

#if !defined(CACHE_FAILURE)
#define CACHE_FAILURE (-1)
#endif // CACHE_FAILURE

#define MAX_CACHE_REQUEST_LEN 6200
int g_nthreads = 0 ;
bool quitProcess = false;
MQFileRequest_t *g_request;

static void _sig_handler(int signo){
	if (signo == SIGINT || signo == SIGTERM){
		/* Unlink IPC mechanisms here*/
		quitProcess = true;
		if (g_request)
		{
			free(g_request);
		}

		exit(signo);
	}
}

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work queue (Default: 3, Range: 1-1024)\n"      \
"  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"cachedir",           required_argument,      NULL,           'c'},
  {"nthreads",           required_argument,      NULL,           't'},
  {"help",               no_argument,            NULL,           'h'},
  {"hidden",			 no_argument,			 NULL,			 'i'}, /* server side */
  {NULL,                 0,                      NULL,             0}
};

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

int IsFileDescValid(int fd)
{
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

int main(int argc, char **argv) {
	int nthreads = 1;
	char *cachedir = "locals.txt";
	char option_char;
	int status = 0;

	/* disable buffering to stdout */
	setbuf(stdout, NULL);

	while ((option_char = getopt_long(argc, argv, "ic:ht:x", gLongOptions, NULL)) != -1) {
		switch (option_char) {
			default:
				status = 1;
			case 'h': // help
				Usage();
				exit(status);
			case 'c': //cache directory
				cachedir = optarg;
				break;
			case 't': // thread-count
				nthreads = atoi(optarg);
				break;   
			case 'i': // server side usage
				break;
			case 'x': // experimental
				break;
		}
	}

	if ((nthreads>6200) || (nthreads < 1)) {
		fprintf(stderr, "Invalid number of threads\n");
		exit(__LINE__);
	}

	if (SIG_ERR == signal(SIGINT, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
		exit(CACHE_FAILURE);
	}

	if (SIG_ERR == signal(SIGTERM, _sig_handler)){
		fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
		exit(CACHE_FAILURE);
	}
	g_nthreads = nthreads;
	threadInfo_t threadsInfo[g_nthreads];

  printf("OPTIONS :: status%d \t\n ", status);
  printf("OPTIONS :: cachedir %s \t\n ", cachedir);
  printf("OPTIONS :: nthreads %u \t\n ", nthreads);

	//
	// Create the queue 
	//
	cacheQueue = (steque_t*) malloc(sizeof(steque_t));
	steque_init(cacheQueue);

	//Create global mutex
	g_cacheLock  = (lock_t*) malloc(sizeof(lock_t));
	
	pthread_cond_init(&g_cacheLock->cond, NULL);
	pthread_mutex_init(&g_cacheLock->mutex, NULL);

	// OPEN Message Request Queue (passing locks) again and post the request
	struct mq_attr attr;
	attr.mq_flags = 0;
	attr.mq_maxmsg = MQ_MAX_MESSAGES;
	attr.mq_msgsize = MQ_MAX_MSG_SIZE;
	attr.mq_curmsgs = 0;

	for(int i=0; i<= g_nthreads; i++)
	{
		threadsInfo[i].threadState = 0;
		threadsInfo[i].IsEnabled = true;
		// threadsInfo[i].mqdResponse = mqResponse;
		if (pthread_create(&threadsInfo[i].hThread, NULL, cacheWorker, &threadsInfo[i]))
		{
			printf("Error creating thread");
		}
	}

	/* Cache initialization */
	simplecache_init(cachedir);

	/* Add your cache code here */	
	// OPEN Message Request Queue (passing locks) again and read the request
	mqd_t mqRequest = -1;
	while(mqRequest < 0 )
	{
		mqRequest = mq_open(MQ_REQUEST_NAME, O_RDWR, 0666, &attr);
		if (mqRequest < 0)
		{
			printf("Warning: Message Queue %s not ready retrying. ErrCode %d \n", MQ_REQUEST_NAME, errno);
			sleep(1);
		}
	}
	
	while(!quitProcess)
	{
		//READ MQ_REQUEST and DISPATCH
		g_request = (MQFileRequest_t*) malloc(MQ_MAX_MSG_SIZE+1);
		size_t result = mq_receive(mqRequest, (char *)g_request, MQ_MAX_MSG_SIZE+1, 0);
		if (result == -1)
		{
			perror("Warning: Waiting on mq_receive. \n");
			continue;
		}

		if (cacheQueue)
		{
			pthread_mutex_lock(&g_cacheLock->mutex);
			steque_enqueue(cacheQueue, g_request);
			pthread_mutex_unlock(&g_cacheLock->mutex);
		}

		if (pthread_cond_signal(&g_cacheLock->cond) != 0)
		{
			printf("Function %s Line: %d Broadcast Failed with Error %d ! \n", __FUNCTION__, __LINE__, errno);
		}
	}

	/* this code probably won't execute */
	pthread_mutex_destroy(&g_cacheLock->mutex);
	pthread_cond_destroy(&g_cacheLock->cond);

	//Destroy the queue
	if (cacheQueue)
	{
		steque_destroy(cacheQueue);
	}

	if (g_cacheLock)
	{
		free(g_cacheLock);
	}

	//cleanup part  
	for (int i=0; i<=g_nthreads; i++)
	{
		threadsInfo[i].IsEnabled = true; //Signall all thread to close
		if (pthread_cond_broadcast(&g_cacheLock->cond) != 0)
		{
			printf("Function %s Line: %d Broadcast Failed with Error %d ! \n", __FUNCTION__, __LINE__, errno);
		}

		pthread_join(threadsInfo[i].hThread, NULL);
	}

	return 0;
}

void *cacheWorker(void* arg)
{
	struct stat fileStat;
	bool isFileExist = false;
	// MQFileResponse_t responseFile;
	MQFileRequest_t *fRequest = NULL;
	ContextShm_t* shmpMapped = NULL;
	size_t readLen = 0; //start with zero
	size_t fileRead = 0;
	void* shmDataAddr;
	int fileDesc = -1;

	threadInfo_t *threadInfo = (threadInfo_t*) arg;  
  	threadInfo->pid = syscall(SYS_gettid);

	//This needs to be called from handler
	while(threadInfo->IsEnabled)
	{
		threadInfo->threadState = 1;
		//Lock the queue
		pthread_mutex_lock(&g_cacheLock->mutex);

		printf("Entering to wait for MQRequest on my cacheQueue TID %ld \n", syscall(SYS_gettid));	
		while(steque_isempty(cacheQueue))
		{
			pthread_cond_wait(&g_cacheLock->cond, &g_cacheLock->mutex);
		}

		//Read the queue
		fRequest = (MQFileRequest_t*) steque_pop(cacheQueue);
		
		//Unlock the queue
		pthread_mutex_unlock(&g_cacheLock->mutex);
		
		if (fRequest == NULL)
		{			
			fprintf(stderr, "Failed to read request queue in thread TID : %d", threadInfo->pid);
			continue;
		}

		// Check if cache exist and then 
		// If yes open SHM if yes and respond
		// If no, just post using SHM again without DATA
		printf("Requested file path %s \n", fRequest->filePath);
		if (-1 != (fileDesc = simplecache_get(fRequest->filePath)))
		{
			if (IsFileDescValid(fileDesc))
			{
				//Get file size
				if (fstat(fileDesc, &fileStat) == -1)
				{
					perror("Failed to open fstat : ");
					isFileExist = false;
				}
				else
				{
					isFileExist = true;
				}
			}
			else
			{
				printf("File Descriptor INVALID filepath %s \n ", fRequest->filePath);
				isFileExist = false;
			}
		}
		else
		{
			printf("File not found %s \n ", fRequest->filePath);
			isFileExist = false;
		}

		// Now share the file contents since proxy is ready to recieve
		int shmFD = shm_open(fRequest->shmName, O_RDWR, 0600);
		shmpMapped = (ContextShm_t*) mmap(NULL, fRequest->segmentSize,
							 PROT_READ | PROT_WRITE, MAP_SHARED, shmFD, 0);
		if(shmpMapped == MAP_FAILED)
		{
			perror("mmap failed ");
		}

		sem_wait(&shmpMapped->semaphoreWR); // TOP Lock
		//Once reader signals post actual file data
		if (isFileExist)
		{
			shmDataAddr = shmpMapped +1 ;

			strcpy(shmpMapped->filePath, fRequest->filePath);
			shmpMapped->status = GF_OK;
			
			shmpMapped->fileLen = fileStat.st_size;
			fileRead = 0; // Start with zero
			while(fileRead < shmpMapped->fileLen)
			{
 				size_t localBuffSize = fRequest->segmentSize - sizeof(ContextShm_t);
				readLen = pread(fileDesc, (char*)shmDataAddr, localBuffSize, fileRead);
				//call send
				shmpMapped->dataLength = readLen;
				
				fileRead = fileRead + readLen;
													
				if (sem_post(&shmpMapped->semaphoreRD) != 0)
				{
					perror("SEM_POST Write Failed ");
				}							
				
				// Wait till reader finishes reading			
				sem_wait(&shmpMapped->semaphoreWR);
				
			}// While(file_length)

			printf("File Read %zu of file %s \n", fileRead, shmpMapped->filePath);

		} //FILE_EXISTS
		else //FILE_NOT_EXIST
		{
			strcpy(shmpMapped->filePath, fRequest->filePath);
			shmpMapped->fileLen = 0;
			shmpMapped->status = GF_FILE_NOT_FOUND;
			printf("GF_FILE_NOT_FOUND %s \n ", fRequest->filePath);
									
			// if (sem_post(&shmpMapped->semaphoreRD) != 0)
			// {
			// 	perror("SEM_POST Write Failed ");
			// }							
			
			// sem_wait(&shmpMapped->semaphoreWR); // Wait for reader to finish posting header
		}
				
		if (sem_post(&shmpMapped->semaphoreRD) != 0)
		{
			perror("SEM_POST Write Failed ");
		}							

		// Release MQ Request Memmory
		if (fRequest)		
		{
			free(fRequest);
		}
		
		// munmap(shmpMapped, fRequest->segmentSize);
		// Dont SHM_UNLINK as webproxy owns him/her
	} //while(1) for thread

	return (void*) NULL;
}