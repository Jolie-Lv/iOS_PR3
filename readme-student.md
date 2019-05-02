
# Project README file

This is **YOUR** Readme file.

## Project Description
We will manually review your file looking for:

- A summary description of your project design.  If you wish to use grapics, please simply use a URL to point to a JPG or PNG file that we can review

- Any additional observations that you have about what you've done. Examples:
	- __What created problems for you?__
	Locally tested with gfcllient_download passed all file downloading (in a human readable format), but Bonnie it failing for file_not found case, so i'm adding to CURLOPT_ERRORBUFFER to read the errors, as CURL doesn't return 404 for file not found case, its returning CURLE_OK(0). But inits reponse buffer i see access denied, so adding CURLOPT_ERRORBUFFER to handle this now. This approach didn't help. So later, from here https://ec.haxx.se/libcurl-http-responses.html i learnt how to use curl_easy_getinfo to get http response. I completely didn't use the webproxy.c command line argument for the URL ROOT path, learnt hard way for multiple failures.
	
	Initial design is to use 2 message_queue for control channel but end up adding more syncronization logic and code. So removed 1 message queue from the control channel and using SHM Memory to post the file status (GF_OK, GF_FILE_NOT_FOUND) cases.
	
	I opened Message Queue inside each thread and causes Bad-Address Exceptions, so i moved them to boss thread and handled using the pointers.
	
	Another problem, my SHM Context Struct size is greater than SEgSize provided by Bonnie. Printing the sizes write before exception line helped to understand this issue first.
	
	- __What tests would you have added to the test suite?__
		Mostly added lots of error checks so its caught earlier inside methods while validating with gfclient_download.
		Test Scenarios are derived based on experiences and assumptions.
		
		Increased number of threads and nsegments with out vscode debugger and ran with different files sizes.
		
	- __If you were going to do this project again, how would you improve it?__

	- __If you didn't think something was clear in the documentation, what would you write instead?__
	
## Known Bugs/Issues/Limitations
Assumptions:
1. Not applicable

__Please tell us what you know doesn't work in this submission__

## References

__Please include references to any external materials that you used in your project__
Part#1
1. https://www.hackthissite.org/articles/read/1078  -- for Curl Lib Usage
2. Many C library calls used stackoverflow posts
3. https://ec.haxx.se/libcurl-http-responses.html -- Specifically for the http response
Part#2

Why I chose Semaphore instead of mutex across process?
Reason : 1 https://stackoverflow.com/questions/35996972/c-mutex-lock-on-shared-memory-accessed-by-multiple-processes
Yes, you can create a mutex in shared memory and than use this mutex from the other process, but there is no way you can ensure the mutex is fully initialized by the first process when you are checking it in the second. For trully independent programms I strongly recommend using semaphores.

Reason : 2 
https://www.geeksforgeeks.org/mutex-vs-semaphore/ -- Semaphore is a signalling vs Mutex is a locking
Using Semaphore: A semaphore is a generalized mutex. In lieu of single buffer, we can split the 4 KB buffer into four 1 KB buffers (identical resources). A semaphore can be associated with these four buffers. The consumer and producer can work on different buffers at the same time.

References: 
https://github.com/Jeshwanth/Linux_code_examples/blob/master/POSIX_shm_sem/servershm.c
https://stackoverflow.com/questions/8359322/how-to-share-semaphores-between-processes-using-shared-memory
https://www.softprayog.in/programming/interprocess-communication-using-posix-message-queues-in-linux