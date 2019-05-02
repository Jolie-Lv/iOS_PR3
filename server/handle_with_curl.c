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
#include <unistd.h>
#include <curl/curl.h>

#include "gfserver.h"
#include "proxy-student.h"

#define BUFSIZE (6200)

#define DEBUG_LOG_ENABLE 1

typedef struct {
	char* buff;
	size_t length;
} RxData;

size_t WriteDataCallBack(void* ptr, size_t len, size_t items, void* data)
{
	if (!data)
	{
		return 0;
	}
	
	size_t retLength = len * items;
	RxData *recvData = (RxData*) data;
	recvData->buff = realloc(recvData->buff, recvData->length + retLength + 1);

	// Memcopy to length's position
	memcpy(&(recvData->buff[recvData->length]), ptr, retLength);
	recvData->length += retLength;
	recvData->buff[recvData->length] = 0; //null terminate
	return retLength;
}

/*
 * handle_with_curl is the suggested name for your curl call back.
 * We suggest you implement your code here.  Feel free to use any
 * other functions that you may need.
 */
ssize_t handle_with_curl(gfcontext_t *ctx, char *path, void* arg){
	size_t dataTransferredinBytes, wrLen;
	RxData recvData;
	recvData.buff = malloc(1);
	recvData.length = 0;

	//Initialize libcurl global here first and then easy init
	curl_global_init( CURL_GLOBAL_ALL );
	CURL* curlHandle = curl_easy_init();
	// CURLcode returnCode =400;

	// Request file URL
	dataTransferredinBytes = 0;
	char* reqURL = malloc(sizeof(char) * URL_Length);	
	sprintf(reqURL, "%s%s", (char*)arg, path);
	#if DEBUG_LOG_ENABLE
		printf("reqURL URL %s \n", reqURL);
	#endif //DEBUG_LOG_ENABLE
	
	curl_easy_setopt(curlHandle, CURLOPT_URL, reqURL);
	
	//Configure libcurl
	curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, WriteDataCallBack);
	curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, (void *)&recvData);
	curl_easy_setopt(curlHandle, CURLOPT_USERAGENT, "GFServer-Agent/1.0");	
	curl_easy_setopt(curlHandle, CURLOPT_FAILONERROR, 1L);

	long curlResponseCode;

	//Perform using libcurl
	curl_easy_perform(curlHandle);
	curl_easy_getinfo(curlHandle, CURLINFO_RESPONSE_CODE, &curlResponseCode);
    printf("Return code %zu \n", curlResponseCode);
	
	//S3 sends 403 even for 404
	if(curlResponseCode == 404 || (curlResponseCode == 403))
	{
		gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);		
	}
	else if (curlResponseCode == 200)	
	{
		gfs_sendheader(ctx, GF_OK, recvData.length);
		printf("%lu bytes received throug CURL Lib from aws s3 store \n", (long)recvData.length);
		//post the data now
		while(dataTransferredinBytes < recvData.length)
		{
			wrLen = gfs_send(ctx, recvData.buff, recvData.length);
			if (wrLen <= 0)
			{
				printf("Error: Wrote %zu of length %zu \n", wrLen, recvData.length);
				dataTransferredinBytes = 0;
				goto cleanUp;
			}

			dataTransferredinBytes += wrLen;
		}
	}
	else
	{
		printf("Sent GF_ERROR Header response \n");
		gfs_sendheader(ctx, GF_ERROR, 0);
	}

cleanUp:
	//Clean up libcurl
	if (reqURL)
	{
		free(reqURL);
	}

	curl_easy_cleanup(curlHandle);
	if (recvData.buff)
	{
		free(recvData.buff);
	}

	curl_global_cleanup();
	
	return dataTransferredinBytes;
}


/*
 * The original sample uses handle_with_file.  We add a wrapper here so that
 * you can use this in place of the existing invocation of handle_with_file.
 * 
 * Feel free to use handle_with_curl directly.
 */
ssize_t handle_with_file(gfcontext_t *ctx, char *path, void* arg){
	return handle_with_curl(ctx, path, arg);
}