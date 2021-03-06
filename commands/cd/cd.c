/*
 * File:   cd.c
 * Author: Matthew Fortier
 * Assignment: Final Project Shell
 * Description: Changes the directory based on arguments and current directory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <limits.h>
#include "cd.h"
#include "../../package/fatSupport.h"
#include "../../shell.h"

struct PATH *global_path;

#define BYTES_IN_SECTOR 512

int main(int argc, char** argv)
{
// SHARED MEMORY
   int shmid;
   key_t key = KEY;
   size_t shmsize = sizeof(struct PATH);

   shmid = shmget(key, shmsize, 0666);
   if(shmid < 0)
   {
      perror("shmget");
      exit(1);
   }
   global_path = shmat(shmid, (void *) 0, 0);
   if(global_path == (void *) -1)
   {
      perror("shmat");
      exit(1);
   }

   char** directories;
   char** tempDirectories;
   int directoryCount;
   int i;
   int cluster;
   char *temp = global_path->cwd;

   if (argv[1] == NULL)       // Test for no arguments
   {
      memcpy(global_path->cwd, "/", sizeof("/"));
      global_path->cluster = 0;
   }
   else if (strcmp(argv[1], "/") == 0)          // Test for root directory
   {
      memcpy(global_path->cwd, "/", sizeof("/"));
      global_path->cluster = 0;
   }
   else if (argv[1][0] == '/')          // Test for absolute path
   {
      cluster = 0;          // start at root cluster
      directories = parseInput(argv[1], "/");           // gets an array of all directories
      directoryCount = countDirectories(directories);
      for ( i = 0; i < directoryCount; i++)             // tests each directory sequentially
      {
         cluster = checkDirectory(addSpaces(directories[i]), cluster);
      }
      char *new_path = generatePath(directories, directoryCount);
      printf("%s\n", new_path);
      if (cluster != -1)            // Check to see if commands succeeded
      {
         global_path->cluster = cluster;                // Reset clusetr to new cluster
         memcpy(global_path->cwd, new_path, 4096);                // Set global path to new path
      }
      else {
         printf("%s: directory or file does not exist\n", new_path);
      }
   }
   else {
      cluster = global_path->cluster;
      directories = parseInput(argv[1], "/");
      directoryCount = countDirectories(directories);
      for ( i = 0; i < directoryCount; i++)
      {
         cluster = checkDirectory(addSpaces(directories[i]), cluster);
      }
      char *new_path = generatePath(directories, directoryCount);
      char *current_path = global_path->cwd;
      if (cluster != -1)
      {
         global_path->cluster = cluster;

         if (strcmp(argv[1], "..") == 0)
         {
            const char ch = '/';
            char* ret;

            ret = strrchr(current_path, ch);

            if(global_path->cluster != 0)
              *ret = '\0';
            else
              memcpy(global_path->cwd, "/", 4096);

            memcpy(global_path->cwd, current_path, 4096);
         }
         else if (strcmp(argv[1], ".") == 0)
         {
            // Do nothing
         }
         else
         {
            if (strcmp(global_path->cwd, "/") != 0)
              strcat(current_path, "/");
            strcat(current_path, argv[1]);
            memcpy(global_path->cwd, current_path, sizeof(current_path));
         }
      }
      else {
         printf("Directory does not exist or is a file\n");
      }
   }

   return 0;
}

int countDirectories(char** directories)
{
   int i = 0;
   int count = 0;

   while(directories[i] != NULL)
   {
      count++;
      i++;
   }
   return count;
}

char * addSpaces(char* directory)
{
   static char filename[8];
   memset(filename, 32, 7);
   int i;
   for (i = 0; i < strlen(directory); i++)
   {
      filename[i] = directory[i];
   }
   filename[8] = '\0';
   return filename;
}

char * generatePath(char** directs, int directoryCount)
{
   static char path[] = "";
   int i;
   if (directoryCount == 1)
   {
     strcat(path, "/");
     strcat(path, directs[0]);
     return path;
   }
   for (i = 0; i < directoryCount; i++)
   {
      if(global_path->cluster != 0)
        strcat(path, "/");
      strcat(path, directs[i]);
   }
   return path;
}

int checkDirectory(char *directory, int cluster)
{
   char path[] = FLOPPY;
   unsigned char* sect;
   int bytes;
   int FLC;
   int i;
   FILE_SYSTEM_ID = fopen(path, "r+");

   if (FILE_SYSTEM_ID == NULL)
   {
      printf("Could not open the floppy drive or image.\n");
      exit(1);
   }
   BYTES_PER_SECTOR = BYTES_IN_SECTOR;

   sect = (unsigned char*) malloc((BYTES_PER_SECTOR) *sizeof(unsigned char));
   if (cluster == 0)
   {
      bytes = read_sector(cluster + 19, sect);          // Reads in correct sector depending on if cluster is pointing to root
   }
   else {
      bytes = read_sector(cluster + 31, sect);
   }

   for (i = 0; i < BYTES_PER_SECTOR; i += 32)       // Loops through 32 byte entires and reads the cluster
   {
      FLC = read_cluster(i, sect, directory);           // Returns the First Logical Cluster for the directory given
      if (FLC >= 0)            // Returns -1 if directory does not exists and returns it
      {
         return FLC;
      }
      else if (FLC == -2)
      {
         break;
      }
   }

   fclose(FILE_SYSTEM_ID);
   free(sect);

   return -1;
}

int read_cluster(int marker, unsigned char* sect, char* directory)
{
   int i;
   char filename[9];
   char extension[4];
   char *fullName;
   char *tempString;
   int attributes;
   int FLC;
   int size;
   char type[5];
   int mostSignificantBits;
   int leastSignificantBits;
   int temp_bits1;
   int temp_bits2;
   int temp;

// Get the filename for this entry
   for (i = marker; i < marker + 7; i++)
   {
      filename[i - marker] = sect[i];
   }

// If the first byte is 0x00, then there are no more entries
   if (filename[0] == 0x00)
   {
      return -1;
   }

   filename[7] = '\0';      // Make sure to add null terminating character

// Get the file extension for this entry
   for (i = marker + 8; i < marker + 11; i++)
   {
      extension[i - (marker + 8)] = sect[i];
   }

   extension[3] = '\0';     // Make sure to add null terminating character

   tempString = trimwhitespace(filename);
   strcpy(fullName, tempString);
   tempString = trimwhitespace(extension);
   strcat(fullName, tempString);
   directory = trimwhitespace(directory);

   attributes = sect[marker + 11];      // Set the attributes for this entry

// Get the FLC
   mostSignificantBits  = ( ( (int) sect[marker + 27] ) << 8 ) & 0x0000ff00;
   leastSignificantBits =   ( (int) sect[marker + 26] )        & 0x000000ff;
   temp = mostSignificantBits | leastSignificantBits;

   FLC = temp;
   if (strcmp(fullName, directory) == 0)
   {
      if(attributes != 0x10)
      {
         return -2;
      }
      else
      {
         return FLC;
      }
   }
/* Unnessecary for this function but may be useful

   // Get File Size (in bytes)
   mostSignificantBits  = ( ( (int) sect[marker + 31] ) << 24 ) & 0xff000000;
   leastSignificantBits = ( ( (int) sect[marker + 30] ) << 16 ) & 0x00ff0000;
   temp_bits2 = ( ( (int) sect[marker + 29] ) << 8 ) & 0x0000ff00;
   temp_bits1 = ( (int) sect[marker + 28] ) & 0x000000ff;
   temp = mostSignificantBits | leastSignificantBits | temp_bits2 | temp_bits1;

   size = temp;

   Print out ls information ignoring anything that is not a file or directory
   if (attributes != 0x0F && attributes != 0x08){
   if (attributes == 0x10){
    strcpy(type, "Dir");
    printf("%s %s %10s %13d %9d\n", filename, extension, type, size, FLC);
   } else {
    strcpy(type, "File");
    printf("%s.%s %10s %13d %9d\n", filename, extension, type, size, FLC);
   }
   }
 */
   return -1;
}

char ** parseInput(char line[], const char *delimiter)
{
      char* path = strdup(line);
      char ** res  = NULL;
      char *  p    = strtok (path, delimiter);
      int n_spaces = 0, i;


      /* split string and append tokens to 'res' */

      while (p) {
        res = realloc (res, sizeof (char*) * ++n_spaces);

        if (res == NULL)
          exit (-1); /* memory allocation failed */

        p[strlen(p)] = '\0';
        res[n_spaces-1] = p;

        p = strtok (NULL, delimiter);
      }

      /* realloc one extra element for the last NULL */

      res = realloc (res, sizeof (char*) * (n_spaces+1));
      res[n_spaces] = 0;

      /* print the result */

      // for (i = 0; i < (n_spaces+1); ++i)
      //   printf ("res[%d] = %s\n", i, res[i]);

      return res;
}

//http://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
char *trimwhitespace(char *str)
{
   char *end;

      // Trim leading space
   while(isspace((unsigned char)*str))
      str++;

   if(*str == 0) // All spaces?
      return str;

      // Trim trailing space
   end = str + strlen(str) - 1;
   while(end > str && isspace((unsigned char)*end))
      end--;

      // Write new null terminator
   *(end+1) = 0;

   return str;
}

char ** removeLastElement(char ** directories)
{
  int i = 0;
  char** temp = malloc(sizeof(char*) * (countDirectories(directories) - 1));
  for (i = 0; i < (countDirectories(directories) - 1); i++)
  {
    temp[i] = directories[i];
  }
  return temp;
}
