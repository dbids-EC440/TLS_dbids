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
    //unsigned int pageFault = ((unsigned int) si->si_addr & ~(pageSize - 1));

    //Check whether this is a tls or real segfault
    //if (tls segfault)
        // brute force scan through all allocated TLS regions
            //for each page:
            /*if (page->address == pageFault) {pthread_exit(NULL);}*/
    //else
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

//Reads length bytes starting at memory location pointed to by buffer, and writes them to LSA
extern int tls_write(unsigned int offset, unsigned int length, char *buffer)
{
    
    return SUCCESS;
}

//Reads length bytes from LSA at offset and writes them to buffer
extern int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
    
    return SUCCESS;
}

extern int tls_clone(pthread_t tid)
{
    return SUCCESS;
}
