//Devin Bidstrup EC440 Project 4

//#define _POSIX_C_SOURCE 200809L //needed for the sa_sigaction part of sigaction

#define SUCCESS 0
#define FAILURE -1
#define NUM_THREADS 128

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>

//Page struct for memory management
struct page 
{
    uintptr_t address;       /* start address of page */
    int ref_count;               /* counter for shared pages */
};

//Global struct to local storage area information
struct LSA
{
    pthread_t tid;
    unsigned int size;          /* size in bytes */
    unsigned int pageNum;       /* number of pages */
    struct page** pages;        /* array of pointers to pages */
};

//Instantiate the struct for this thread
struct LSA* LSA_array[NUM_THREADS];
int pageSize = 0;

//Function to handle SIGSEGV and SIGBUS when they are caused by TLS
void pageFaultHandler(int sig, siginfo_t *si, void *context)
{
    uintptr_t pageFault = ((uintptr_t) si->si_addr & ~(pageSize - 1));

    //Check whether this is a tls or real segfault
    int i, j;
    for (i=0; i < NUM_THREADS; i++)
    {
        if (LSA_array[i])
        {
            for (j = 0; j < LSA_array[i]->pageNum; j++)
            {
                struct page* p = LSA_array[i]->pages[j];
                if (p->address == pageFault)
                {
                    //If this is a tls related segfault, terminate the thread that caused it
                    pthread_exit(NULL);
                }
            }
        }
    }
    
    //else install default handlers and reraise the signal
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);       
}

//Function which initializes the needed parameters on the first run
void tls_init()
{
    struct sigaction sigact;

    /* get the size of a page */
    pageSize = getpagesize();

    /* install the signal handler for page faults (SIGSEGV, SIGBUS) */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_sigaction = pageFaultHandler;

    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);
}

//Create a LSA that can hold at least size bytes
extern int tls_create(unsigned int size)
{    
     /*  Create the TLS with the following parameters per page
    PROT_NONE       : Pages may not be accessed
    MAP_PRIVATE     : Create a private CoW mapping
    MAP_ANONYMOUS   : The mapping is not backed by any file, contents intialized to zero.
                      Ignores the fd and offset arguments.*/
    
    // Initialize the tls library if needed
    static int initialize = 1;
    if (initialize)
    {
        tls_init();
        initialize = 0;
    }

    pthread_t threadID = pthread_self();

    //Error Handling, check if size > 0 and if thread has an LSA already
    if (size <= 0 ||  LSA_array[threadID]!= NULL)
    {
        return FAILURE;
    }
    
    //Allocate the TLS for this thread using malloc and initialize
    struct LSA* threadLSA = (struct LSA*) malloc(sizeof(struct LSA));
    threadLSA->size = size;
    threadLSA->pageNum = size / pageSize + (size % pageSize != 0); //ceiling the pageNum as needed
    threadLSA->tid = threadID;
    
    //Allocate pages array and then individual pages
    threadLSA->pages = (struct page**) calloc(threadLSA->pageNum, sizeof(struct page*));
    int i;
    for (i = 0; i < threadLSA->pageNum; i++)
    {
        struct page* p = (struct page*) malloc(sizeof(struct page));
        //Use mmap to map the page to a given place in memory.
        p->address = (uintptr_t) mmap(0, pageSize, 0, (MAP_PRIVATE | MAP_ANONYMOUS), -1, 0);
        if (p->address == (uintptr_t) MAP_FAILED)
            return FAILURE;
        p->ref_count = 1;
        threadLSA->pages[i] = p;
    }

    //Add this thread mapping to the global LSA array
    LSA_array[threadID] = threadLSA;

    return SUCCESS;
}

//Destroys the thread local storage
extern int tls_destroy()
{
    pthread_t threadID = pthread_self();
    if (LSA_array[threadID])
    {
        int i;
        for (i = 0; i < LSA_array[threadID]->pageNum; i++)
        {
            //Check if page is shared, if not free, else decrement ref_count
            if (LSA_array[threadID]->pages[i]->ref_count == 1)
                free(LSA_array[threadID]->pages[i]);
            else
                --(LSA_array[threadID]->pages[i]->ref_count);
        }
        free(LSA_array[threadID]->pages);
        LSA_array[threadID]->size = 0;
        LSA_array[threadID]->pageNum = 0;
        LSA_array[threadID]->tid = 0;
        free(LSA_array[threadID]);
        return SUCCESS;
    }

    return FAILURE;   
}

//protect function allows no reading, writing, or executing from the pages
void tls_protect(struct page *p)
{
    if (mprotect((void *) p->address, pageSize, PROT_NONE))
    {
        fprintf(stderr, "tls_protect: could not protect page\n");
        exit(1);
    }
}

//Unprotect function which allows reading or writing to individual pages.
//I added the readWrite int to allow for only writing to or reading from the pages depending
void tls_unprotect(struct page *p, const int readWrite)
{
    if (mprotect((void *) p->address, pageSize, readWrite)) 
    {
        fprintf(stderr, "tls_unprotect: could not unprotect page\n");
        exit(1);
    }
}

//Reads length bytes starting at memory location pointed to by buffer, and writes them to LSA
extern int tls_write(unsigned int offset, unsigned int length, char *buffer)
{
    int i;
    pthread_t threadID = pthread_self();

    //Check that there is a LSA for this thread
    if (!LSA_array[threadID])
        return FAILURE;

    struct LSA* threadLSA = LSA_array[threadID];

    //Check that the function didn't ask to write more data than LSA can hold
    if ((offset + length) > threadLSA->size)
        return FAILURE;
    
    //Change the protection on the whole LSA to be able to write to it
    for (i = 0; i < threadLSA->pageNum; i++)
    {
        tls_unprotect(threadLSA->pages[i], PROT_WRITE);
    }
    
    //Perform the write operation
    int cnt, idx;
    char* dst = NULL;
    for (cnt= 0, idx= offset; idx< (offset + length); ++cnt, ++idx) 
    {
        struct page *p, *copy;
        unsigned int pn, poff;

        //Detemine page number in LSA, page offset, and the page itself
        pn = idx / pageSize;
        poff = idx % pageSize;
        p = threadLSA->pages[pn];
        
        /* If this page is shared, create a private copy (COW) */
        if (p->ref_count> 1) 
        {
           //copy existing page
           copy = (struct page *) calloc(1, sizeof(struct page));
           copy->address = (uintptr_t) mmap(0, pageSize, PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
           copy->ref_count= 1;
           threadLSA->pages[pn] = copy; 
            
            //update original page
            p->ref_count--;
            tls_protect(p);
            p = copy;
        }
        
        //Then get the dst byte and set it equal to the corresponding char in buffer
        dst= ((char *) p->address) + poff;
        *dst = buffer[cnt];    
    }

    //Reprotect all pages in the threads TLS
    for (i = 0; i < threadLSA->pageNum; i++)
    {
        tls_protect(threadLSA->pages[i]);
    }

    return SUCCESS;
}

//Reads length bytes from LSA at offset and writes them to buffer
extern int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
    int i;
    pthread_t threadID = pthread_self();

    //Check that there is a LSA for this thread
    if (!LSA_array[threadID])
        return FAILURE;

    struct LSA* threadLSA = LSA_array[threadID];

    //Check that the function didn't ask to read more data than LSA can hold
    if ((offset + length) > threadLSA->size)
        return FAILURE;
    
    //Change the protection on the whole LSA to be able to read from it
    for (i = 0; i < threadLSA->pageNum; i++)
    {
        tls_unprotect(threadLSA->pages[i], PROT_READ);
    }

    //Perform the read operation (from class)
    int cnt, idx;
    char* src = NULL;
    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) 
    {
        struct page *p;
        unsigned int pn, poff;
        
        pn= idx / pageSize;
        poff= idx % pageSize;
        
        p = threadLSA->pages[pn];
        src = ((char *) p->address) + poff;
        
        buffer[cnt] = *src;
    }

    //Reprotect all pages in the threads TLS
    for (i = 0; i < threadLSA->pageNum; i++)
    {
        tls_protect(threadLSA->pages[i]);
    }

    return SUCCESS;
}

//Clones the LSA of target thread tid
extern int tls_clone(pthread_t tid)
{
    int i;
    pthread_t currentTID = pthread_self();

    //Check that there is not a LSA for this thread
    if (LSA_array[currentTID])
        return FAILURE;

    //Check that the target thread has an LSA
    if (!LSA_array[tid])
        return FAILURE;

    //Create the struct to hold the LSA for the thread
    struct LSA* currentLSA = LSA_array[currentTID];
    currentLSA->tid = currentTID;

    //Do the cloning of the LSA
    struct LSA* targetLSA = LSA_array[tid];
    currentLSA->pageNum = targetLSA->pageNum;
    currentLSA->size = targetLSA->size;
    currentLSA->pages = targetLSA->pages;
    for (i = 0; i < targetLSA->pageNum; i++)
    {
        (currentLSA->pages[i]->ref_count)++;
    }

    //Add the LSA to the global array
    LSA_array[currentTID] = currentLSA;

    return SUCCESS;
}