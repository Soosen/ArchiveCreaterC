#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vorgabe.h"
#include <assert.h>
#include <sys/sendfile.h>
#include <unistd.h>
 
//global 
struct fileInfoList list;

//start queue
void init(fileInfoList *list) {
    list->head = NULL;
    list->tail = &list->head;
}

void enqueue(fileInfoList *list, fileInfo *item) {
    assert(list);
    assert(item);
    item->next = NULL;
    *list->tail = item;
    list->tail = &item->next;
}

fileInfo *dequeue(fileInfoList *list) {
    assert(list);
    fileInfo *item = list->head;
    if (item) {
        list->head = item->next;
        if (!list->head) {
            list->tail = &list->head;
        } else {
            item->next = NULL;
        }
    }
    return item;
}
//end queue

void traverse(char *path){
    
    //Directory descriptor
    DIR *dir = opendir(path);
    
    //Error by opendir
    if (!dir) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }
    

    struct dirent *ent;

    //for all files
    while ((ent = readdir(dir))) {
        
        //Dont print current dir and parent dir
        if(strcmp (ent->d_name, ".") != 0 && strcmp (ent->d_name, "..") != 0){
            
            //Init curPath
            char curPath[150];
                
            //copy directory path to curPath
            strcpy(curPath, path);
            
            //add "/" -> directory/
            strcat(curPath, "/");
                
            //add file name -> directory/fileNameOrDir - curPath contains now the full file or dir path
            strcat(curPath, ent->d_name);
                
            //is a regular file
            if(ent->d_type == DT_REG){
                
                //File info struct
                struct stat fileinfo;
                
                //File descriptor
                int fd;
                
                //open current file
                if ((fd = open(curPath ,O_RDONLY)) == -1) { fprintf(stderr, "File open error\n"); }
                if (fstat(fd, &fileinfo) == -1) {fprintf(stderr, "fstat error\n"); }
                
                //print for excercise 1.2
                printf("Path: %-30s  Size: %ld Bytes\n", curPath, fileinfo.st_size);
                
                //FileInfo Object pointer
                fileInfo *fi = (fileInfo*)malloc(sizeof(fileInfo));

                //set path, size, offset
                fi->path = strdup(curPath);
                fi->size = fileinfo.st_size;
                fi->offset = 0;


                //enqueue current fileInfo to the global Queue list
                enqueue(&list, fi);

            }
            
            //if is a directory call the traverse function recursivly
            if(ent->d_type == DT_DIR){
                 traverse(curPath);
            }
            
        }
    }

    //close the directory
   if (closedir(dir) == -1) { fprintf(stderr, "Close dir error\n");  }

}


void printQueue(void){

    printf("Pritning the queue...\n\n");
    //temp for current fileInfo in the Queue 
    struct fileInfo *current = list.head;

    //loop through all elemnts (not using dequeue to not delete the entries)
    while(current != NULL){

        //print for excercise 1.4
        printf("Path: %-30s  Size: %ld Bytes\n", current->path, current->size);

        //set current to the next element
        current = current->next;
    }

    printf("\nSuccess!\n\n");
    

}

void archive(char archName[]){

    //open the archive file
    FILE *file = fopen(archName, "w");

    if (!file) {
        fprintf(stderr, "Error while opening the archive file.\n");
        exit(EXIT_FAILURE);
    }

    //Version 1
    uint16_t version = 1;

    //Write "BSARCH" to the file
    fprintf(file, "BSARCH");

    //Write version as uint16 to the file
    fwrite(&version, sizeof(uint16_t), 1, file);

    //go to the end of the file
    fseek(file, 0L, SEEK_END);

    //get current length of the file
    uint64_t sz = ftell(file);

    //add 8
    sz = sz + 8;

    //write 16 to the file as uint_64
    fwrite(&sz, sizeof(uint64_t), 1, file);

    //prepare space for the ToCLength
    uint64_t ToCLength = 0;
    fwrite(&ToCLength, sizeof(uint64_t), 1, file);

    //init current offset
    uint64_t currentOffset = 0;
    
    //current file info from the queue
    struct fileInfo *currentFileInfo = list.head;

    //loop through all queue entries
    while(currentFileInfo != NULL){

        //wrtie current file path length + 1 because of nullbyte
        uint16_t currentPathlength = strlen(currentFileInfo->path) + 1;
        fwrite(&currentPathlength, sizeof(uint16_t), 1, file);

        //write current path
        char currentPath[currentPathlength];
        strcpy(currentPath, currentFileInfo->path);
        fprintf(file, "%s", currentPath);

        //wrtite a null-byte
        fprintf(file, "%c",'\0');

        //write current File size
        uint64_t currentFileSize = currentFileInfo->size;
        fwrite(&currentFileSize, sizeof(uint64_t), 1, file);

        //write current file offset
        fwrite(&currentOffset, sizeof(uint64_t), 1, file);

        //Increment the current offset by current File Size
        currentOffset = currentOffset + currentFileSize;

        currentFileInfo = currentFileInfo->next;
    }
    
    
    //get size of the ToC = file size - 24
    fseek(file, 0L, SEEK_END);
    ToCLength = ftell(file) - 24;
    
    //put the write/read position to the byte 16 (after the ToC start position)
    fseek(file, 16, SEEK_SET);

    //write the length of ToC
    fwrite(&ToCLength, sizeof(uint64_t), 1, file);
    

    //put the write/read position to the end of the file again
    fseek(file, 0L, SEEK_END);

    //close the file
    fclose(file);
    
    //open the file again but with open() function instead of fopen() to get int fd instead of FILE fd
    int fileToWriteFD = open(archName, O_CREAT | O_WRONLY);
    if (!fileToWriteFD) {
        fprintf(stderr, "Error while opening the archive file.\n");
        exit(EXIT_FAILURE);
    }

    //Go to the end of file (I don't know why but using O_APPEND flag in the open() function is causing an "Invalid Argument error" so i used lseek to the end of the file)
    lseek(fileToWriteFD, 0, SEEK_END);

    //set offset to 0
    off_t off = 0;

    //receive var for errors
    int rv = 0;

    //init file to read FD
    int fileToReadFD;

    //loop through all queue elements
    while(list.head != NULL){

        //open the read file
        fileToReadFD = open(list.head->path, O_RDONLY);

        if (!fileToReadFD) {
            fprintf(stderr, "Error while opening the file to read.\n");
            exit(EXIT_FAILURE);
        };

        //send read file to the write file
        if(rv = (sendfile(fileToWriteFD, fileToReadFD, &off, list.head->size)) < 0){
            fprintf(stderr, "Error while sending a file.\n");
            exit(EXIT_FAILURE);
        }

        //close current fileToRead
        close(fileToReadFD);

        //remove the head entry from the queue and go to the next one
        dequeue(&list);
    }
    //close the archive
    close(fileToWriteFD);
    


}
int main(int argc, char *argv[])
{
    //if arguments not equal 3
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <archivname> <dirname>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    //path to dir you want to archive
    char path[strlen(argv[2])];
    strcpy(path, argv[2]);

    //path of the archive
    char archvName[strlen(argv[1])];    
    strcpy(archvName, argv[1]);

    // "/./"
    char *singlePoint; 

    // "/../"
    char *doublePoint; 

    //contains "/./" or "/../"?
    singlePoint = strstr(argv[2], "/./");
    doublePoint = strstr(argv[2], "/../");
    
    //exit if contains "/./" or "/../"
    if (singlePoint || doublePoint){
       fprintf(stderr, "Forbidden path\n");
        exit(EXIT_FAILURE);
    }
    
    //initialize the queue
    init(&list);
   
    //call traverse function
    printf("\nStarting the traversing process...\n\n");
    traverse(path);
    printf("\nTraversing completed successfully...\n\n");

    //call printQueue function
    printQueue();

    //call archive function
    archive(archvName);
    
    return 0;
}


