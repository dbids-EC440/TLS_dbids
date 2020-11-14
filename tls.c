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
#include <string.h>
#include <limits.h>

//Page struct for memory management
struct page 
{
    uintptr_t address;       /* start address of page */
    int ref_count;               /* counter for shared pages */
};

//Global struct to local storage area information
struct LSA
{
    unsigned int size;          /* size in bytes */
    unsigned int pageNum;       /* number of pages */
    struct page** pages;        /* array of pointers to pages */
};

struct hash_element
{
    pthread_t tid;
    struct LSA* lsa;
    struct hash_element* next;
};

//Instantiate the hash table for all the threads
struct hash_element* hashTable[NUM_THREADS];
int pageSize = 0;

//Returns the hash value to index into the hash table
unsigned int computeHash(pthread_t tid)
{
    return (unsigned int) tid % 127; //Note that 127 was hardcoded here as it is prime
}

//Finds a lsa for the correct pthread_t and returns a pointer to it
//If not found it returns a NULL pointer
struct hash_element* findHashElement(pthread_t tid)
{
    int hashValue = computeHash(tid);

    //First check to see if there is an element at the hash
    if (hashTable[hashValue] != NULL)
    {
        struct hash_element* curr = hashTable[hashValue];
        
        //Find the element
        while (hashTable[hashValue]->tid != tid && curr != NULL)
        {
            curr = curr->next;
        }
        
        return curr;
    }


    return NULL;
}

//Inserts a lsa into the hash table at the head of the LL
int insertHashElement(struct hash_element* threadHash)
{
    pthread_t threadID = pthread_self();
    int hashValue = computeHash(threadID);

    //Check to see if there is an existing hash element at that index in the table
    if (hashTable[hashValue] != NULL)
    {
        struct hash_element* nextElement = hashTable[hashValue];
        hashTable[hashValue] = threadHash;
        hashTable[hashValue]->next = nextElement;
        return SUCCESS;
    }
    else
    {
        hashTable[hashValue] = threadHash;
        hashTable[hashValue]->next = NULL;
        return SUCCESS;
    }
    
    return FAILURE;
}

//Removes a lsa from the hash table and returns that hash element or NULL on error
struct hash_element* removeHashElement(pthread_t tid)
{
    int hashValue = computeHash(tid);

    //First check to see if there is an element at the hash
    if (hashTable[hashValue] != NULL)
    {
        struct hash_element* prev = NULL;
        struct hash_element* curr = hashTable[hashValue];
        struct hash_element* next = hashTable[hashValue] -> next;
        
        //Find the element to remove
        while (hashTable[hashValue]->tid != tid && curr != NULL)
        {
            prev = curr;
            curr = next;
            next = curr->next;
        }

        //Then remove the element
        if(prev)
            prev->next = next;
        
        return curr;
    }

    return NULL;
}

//Function to handle SIGSEGV and SIGBUS when they are caused by TLS
void pageFaultHandler(int sig, siginfo_t *si, void *context)
{
    uintptr_t pageFault = ((uintptr_t) si->si_addr & ~(pageSize - 1));

    //Check whether this is a tls or real segfault
    int i, j;
    for (i=0; i < NUM_THREADS; i++)
    {
        if (hashTable[i])
        {
            struct hash_element* hash_iterator = hashTable[i];
            
            //Iterate through all the elements at that value in the hash table
            while (hash_iterator)
            {
                //Scan the pages of the lsa for the matching address
                for (j = 0; j < hash_iterator->lsa->pageNum; j++)
                {
                    struct page* p = hash_iterator->lsa->pages[j];
                    if (p->address == pageFault)
                    {
                        //If this is a tls related segfault, terminate the thread that caused it
                        pthread_exit(NULL);
                    }
                }

                hash_iterator = hash_iterator->next;
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

    //Define the elements of the hash table as all NULL pointers
    /*int i;
    for (i = 0; i < NUM_THREADS; i++)
    {
        hashTable[i]->lsa = NULL;
        hashTable[i]->next = NULL;
    }*/
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
    if (size <= 0 ||  findHashElement(threadID)!= NULL)
    {
        return FAILURE;
    }
    
    //Allocate the TLS for this thread using malloc and initialize
    struct LSA* threadLSA = (struct LSA*) malloc(sizeof(struct LSA));
    threadLSA->size = size;
    threadLSA->pageNum = size / pageSize + (size % pageSize != 0); //ceiling the pageNum as needed
    
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

    //Create a hash_element for this LSA
    struct hash_element* threadHash = (struct hash_element*) malloc(sizeof(struct hash_element));
    threadHash->lsa = threadLSA;
    threadHash->tid = threadID;

    //Add the hash element to the global hash table
    int error = insertHashElement(threadHash);

    //Check that insert was successful
    if (error == FAILURE)
    {
        return FAILURE;
    }

    return SUCCESS;
}

//Destroys the thread local storage
extern int tls_destroy()
{
    pthread_t threadID = pthread_self();
    struct hash_element* removedHash = removeHashElement(threadID);

    //Check that the tid exists in the hash
    if (removedHash != NULL)
    {
        //Deal with individual pages
        int i;
        for (i = 0; i < removedHash->lsa->pageNum; i++)
        {
            //Check if page is shared, if not free, else decrement ref_count
            if (removedHash->lsa->pages[i]->ref_count == 1)
                free(removedHash->lsa->pages[i]);
            else
                --(removedHash->lsa->pages[i]->ref_count);
        }

        //Free the hash element and LSA
        free(removedHash->lsa);
        free(removedHash);

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
//readWrite == 2 -> write (PROT_WRITE)
//readWrite == 1 -> read  (PROT_READ)
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

    //Get the LSA we are writing to
    struct hash_element* threadHash = findHashElement(threadID);

    //Check that there is a LSA for this thread
    if (threadHash == NULL)
        return FAILURE;

    struct LSA* threadLSA = threadHash->lsa;
    
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
    for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) 
    {
        struct page *p, *copy;
        unsigned int pn, poff;

        //Detemine page number in LSA, page offset, and the page itself
        pn = idx / pageSize;
        poff = idx % pageSize;
        p = threadLSA->pages[pn];
        
        /* If this page is shared, create a private copy (COW) */
        if (p->ref_count > 1) 
        {
            //copy existing page
            copy = (struct page *) malloc(sizeof(struct page));
            copy->address = (uintptr_t) mmap(0, pageSize, PROT_WRITE, (MAP_PRIVATE | MAP_ANONYMOUS), -1, 0);
            copy->ref_count= 1;
            //printf("copy->address: %zd\n", copy->address);
            //printf("threadLSA->pages[pn]->address before: %zd\n", threadLSA->pages[pn]->address);
            //printf("threadLSA contents before: %c\n", *((char *) threadLSA->pages[pn]->address));
            threadLSA->pages[pn] = copy;
            //printf("threadLSA->pages[pn]->address after: %zd\n", threadLSA->pages[pn]->address);
            //printf("threadLSA contents after: %c\n", *((char *) threadLSA->pages[pn]->address));
            //update original page
            p->ref_count--;
            //printf("p contents: %c\n", *((char *) p->address));
            tls_protect(p);
            //printf("p->address before: %zd\n", p->address);
            p = copy;
            //printf("p->address after: %zd\n", p->address);
        }
        
        //Then get the dst byte and set it equal to the corresponding char in buffer
        dst = ((char *) p->address) + poff;
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

    //Get the LSA we are writing to
    struct hash_element* threadHash = findHashElement(threadID);

    //Check that there is a LSA for this thread
    if (threadHash == NULL)
        return FAILURE;

    struct LSA* threadLSA = threadHash->lsa;
    
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
        
        pn = idx / pageSize;
        poff = idx % pageSize;
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
    pthread_t currentTID = pthread_self();

    //Get the current hash element
    struct hash_element* currentHash = findHashElement(currentTID);

    //Get the targetLSA
    struct hash_element* targetHash = findHashElement(tid);
    struct LSA* targetLSA = targetHash->lsa;

    //Check that there is not a LSA for this thread
    if (currentHash != NULL)
        return FAILURE;

    //Check that the target thread has an LSA
    if (targetLSA == NULL)
        return FAILURE;

    //Create the struct to hold the hash_element for the thread
    currentHash = (struct hash_element*) malloc(sizeof(struct hash_element));
    currentHash->tid = currentTID;

    //Do the cloning of the LSA
    currentHash->lsa = (struct LSA*) malloc(sizeof(struct LSA));
    currentHash->lsa->pageNum = targetLSA->pageNum;
    currentHash->lsa->size = targetLSA->size;
    currentHash->lsa->pages = (struct page**) calloc(currentHash->lsa->pageNum, sizeof(struct page*));
    int i;
    for (i = 0; i < currentHash->lsa->pageNum; i++)
    {
        currentHash->lsa->pages[i] = targetLSA->pages[i];
        (currentHash->lsa->pages[i]->ref_count)++;
    }

    //Add the hash element to the global hash table
    int error = insertHashElement(currentHash);

    //Check that insert was successful
    if (error == FAILURE)
    {
        return FAILURE;
    }

    return SUCCESS;
}
