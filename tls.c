//Devin Bidstrup EC440 Project 4

#define SUCCESS 0
#define FAILURE -1

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

//Global struct to local storage area information
struct LSA
{
    void* baseAddress;
    unsigned int size;
};

//Instantiate the struct for this thread
struct LSA threadLSA;

//Create a LSA that can hold at least size bytes
extern int tls_create(unsigned int size)
{    
    /*  Create the TLS with the following parameters
    PROT_NONE       : Pages may not be accessed
    MAP_PRIVATE     : Create a private CoW mapping
    MAP_ANONYMOUS   : The mapping is not backed by any file, contents intialized to zero.
                      Ignores the fd and offset arguments.
    */
    threadLSA.baseAddress = mmap(NULL, (size_t) size, PROT_NONE, (MAP_PRIVATE | MAP_ANONYMOUS), -1, 0);

    //Check return value of mmap
    if (threadLSA.baseAddress != MAP_FAILED)
    {
        threadLSA.size = size;
        return SUCCESS;
    }

    return FAILURE;
}

//Reads length bytes starting at memory location pointed to by buffer, and writes them to LSA
extern int tls_write(unsigned int offset, unsigned int length, char *buffer)
{
    //Check that the function didn't ask to write more data than LSA can hold
    if ((offset + length) > threadLSA.size)
        return FAILURE;

    //Change the protection on the whole LSA to be able to write to it
    mprotect(threadLSA.baseAddress, threadLSA.size, PROT_WRITE);

    //Open a file descriptor to write to LSA at baseAddress
    int writeFD = open((char*) threadLSA.baseAddress, O_WRONLY);
    
    //Check that the open command succeeded
    if (writeFD == -1)
        return FAILURE;

    //Move the writeFD to baseAddress + offset
    lseek(writeFD, offset, SEEK_CUR);

    //Write those contents to the TLA at baseAddress + offset
    size_t writeReturnVal = write(writeFD, (void*) buffer, (size_t) length);
    
    //Check that writing succeeded
    if(writeReturnVal == -1)
        return FAILURE;

    close(writeFD);

    //Change the protection on the whole LSA so that no thread can change it
    mprotect(threadLSA.baseAddress, threadLSA.size, PROT_NONE);
    
    return SUCCESS;
}

//Reads length bytes from LSA at offset and writes them to buffer
extern int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
    //Check that the function didn't ask to write more data than LSA can hold
    if ((offset + length) > threadLSA.size)
        return FAILURE;

    //Change the protection on the whole LSA to be able to read from it
    mprotect(threadLSA.baseAddress, threadLSA.size, PROT_READ);
    
    //Open a file descriptor to read from LSA
    //We need to read from the LSA explicitly here to get the contents in the right format (char*)
    int readFD = open((char*) threadLSA.baseAddress, O_RDONLY);

    //Open a file descriptor to write to the buffer
    int writeFD = open(buffer, O_WRONLY);

    //Check that the open command succeeded
    if ((readFD == -1) | (writeFD == -1))
        return FAILURE;

    //Move the readFD to baseAddress + offset
    lseek(readFD, offset, SEEK_CUR);

    //Read the contents of the LSA and store in contentsBuffer
    char* contentsBuffer = (char*) malloc(length);
    size_t readReturnVal = read(readFD, contentsBuffer, (size_t) length);

    //Check that reading succeeded
    if (readReturnVal == -1)
        return FAILURE;
    
    size_t writeReturnVal = write(writeFD, contentsBuffer,(size_t) length);

    //Check that reading succeeded
    if (writeReturnVal == -1)
        return FAILURE;

    close(readFD);
    close(writeFD);
    free(contentsBuffer);
    //Change the protection on the whole LSA so that no thread can change it
    mprotect(threadLSA.baseAddress, threadLSA.size, PROT_NONE);
    
    return SUCCESS;
}

//Destroys the thread local storage
extern int tls_destroy()
{
    if (threadLSA.baseAddress)
    {
        threadLSA.size = 0;
        free(threadLSA.baseAddress);
        return SUCCESS;
    }

    return FAILURE;
    
}

extern int tls_clone(pthread_t tid)
{
    return SUCCESS;
}
