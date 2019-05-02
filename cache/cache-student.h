/*
 *  This file is for use by students to define anything they wish.  It is used by the proxy cache implementation
 */
 #ifndef __CACHE_STUDENT_H__
 #define __CACHE_STUDENT_H__
#define DEBUG_LOG 1
////////////////////////////////////////////////////////////////////////////
/// COMMON_FOR_ALL_H

#include<semaphore.h>
#include <fcntl.h> // For O_ constants
#include <sys/stat.h> // MODE constants
#include <mqueue.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>
#include "steque.h"
#include "gfserver.h"

// #define MQ_RESPONSE_NAME        "/ResponseMQ"
#define MQ_REQUEST_NAME         "/RequestMQ"
// #define MQ_REQUEST_FILE_NAME    "/home/Filerequest_t"
#define MQ_MAX_MESSAGES         10
#define MQ_MAX_MSG_SIZE         1024

#define SHM_NAME                "SHM_"
#define MAX_SHM_NAME             16

#define SHM_BUFFSIZE            6200
#define MAX_PATH                255


#define CACHE_RESULT_FILE_FOUND     10001
#define CACHE_RESULT_FILE_NOT_FOUND 10002
#define CACHE_RESULT_FILE_ERROR     10003

// typedef struct {
//     char   filePath[MAX_PATH];
//     size_t  fileLen;
//     int     iResult;    // mapped to CACHE_RESULT_FILE_FOUND    
//     // MOVE THIS TO STUDENT_NOTE:
//     // SimpleCache may use any random nsegment of share memory for the payload so track using this member.
//     // Why char, easy to track using is NUll than a integer, since nSegment may be 0 too?
//     // char     nSegmentIndexUsed[6200]; // giving a high number it should be an dynamic number of int's    
// }MQFileResponse_t;

typedef struct {
    char    filePath[MAX_PATH];
    char    shmName[MAX_SHM_NAME];
    size_t  nSegments;
    size_t  segmentSize;    
}MQFileRequest_t;

typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} lock_t;
//COMMON_FOR_ALL_H
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//WEBPROXY__H
// bool g_workerThreadsEnabled;
lock_t *g_proxyLock;

steque_t *g_proxyQueue;

typedef struct {
    size_t nSegments;
    size_t segmentSize;
    mqd_t mqRequest;
} ContxtWebProxy_t;

ContxtWebProxy_t g_WebPrxy;

typedef struct {   
    size_t      dataLength;
    char        filePath[MAX_PATH];
    size_t      fileLen;
    gfstatus_t  status;
    sem_t       semaphoreRD;
    sem_t       semaphoreWR;
} ContextShm_t;

typedef struct {    
    char                shmName[MAX_SHM_NAME]; 
    ContextShm_t*       shmContext;
    char*               argSource;    
} ContextProxy_t;


//WEBPROXY__H
////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////
//SIMPLE_CACHED_H
#include "steque.h"
#include <stdbool.h>

steque_t *cacheQueue;

lock_t *g_cacheLock;


typedef struct  {
  pthread_t       hThread;
  pid_t           pid;
  int             threadState; //READY/WAIT --TODO: Convert to ENUM?
  bool            IsEnabled; // Used to kill threads and exit saefe.
  mqd_t             mqdResponse;
}threadInfo_t;

void *cacheWorker(void* );
//SIMPLE_CACHED_H
////////////////////////////////////////////////////////////////////////////
 #endif // __CACHE_STUDENT_H__
 