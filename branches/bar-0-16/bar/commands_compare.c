/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/commands_compare.c,v $
* $Revision: 1.12 $
* $Author: torsten $
* Contents: Backup ARchiver archive compare function
* Systems : all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "global.h"
#include "strings.h"
#include "stringlists.h"

#include "errors.h"
#include "patterns.h"
#include "entrylists.h"
#include "patternlists.h"
#include "files.h"
#include "archive.h"
#include "fragmentlists.h"

#include "commands_compare.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/* file data buffer size */
#define BUFFER_SIZE (64*1024)

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : compare
* Purpose: compare memory
* Input  : p0,p1  - memory to compare
*          length - size of memory blocks (in bytes)
* Output : -
* Return : number of equal bytes or length of memory blocks are equal
* Notes  : -
\***********************************************************************/

LOCAL ulong compare(const void *p0, const void *p1, ulong length)
{
  const char *b0,*b1;
  ulong      i;

  b0 = (char*)p0;
  b1 = (char*)p1;
  i = 0;
  while (   (i < length)
         && ((*b0) == (*b1))
        )
  {
    i++;
    b0++;
    b1++;
  }

  return i;
}

/*---------------------------------------------------------------------*/

Errors Command_compare(StringList                      *archiveFileNameList,
                       EntryList                       *includeEntryList,
                       PatternList                     *excludePatternList,
                       JobOptions                      *jobOptions,
                       ArchiveGetCryptPasswordFunction archiveGetCryptPasswordFunction,
                       void                            *archiveGetCryptPasswordUserData
                      )
{
  byte              *archiveBuffer,*buffer;
  FragmentList      fragmentList;
  String            archiveFileName;
  Errors            failError;
  Errors            error;
  ArchiveInfo       archiveInfo;
  ArchiveEntryInfo  archiveEntryInfo;
  ArchiveEntryTypes archiveEntryType;
  FragmentNode      *fragmentNode;

  assert(archiveFileNameList != NULL);
  assert(includeEntryList != NULL);
  assert(excludePatternList != NULL);
  assert(jobOptions != NULL);

  /* allocate resources */
  archiveBuffer = (byte*)malloc(BUFFER_SIZE);
  if (archiveBuffer == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  buffer = malloc(BUFFER_SIZE);
  if (buffer == NULL)
  {
    free(archiveBuffer);
    HALT_INSUFFICIENT_MEMORY();
  }
  FragmentList_init(&fragmentList);
  archiveFileName = String_new();

  failError = ERROR_NONE;
  while (   !StringList_empty(archiveFileNameList)
         && (failError == ERROR_NONE)
        )
  {
    StringList_getFirst(archiveFileNameList,archiveFileName);
    printInfo(1,"Comparing archive '%s':\n",String_cString(archiveFileName));

    /* open archive */
    error = Archive_open(&archiveInfo,
                         archiveFileName,
                         jobOptions,
                         archiveGetCryptPasswordFunction,
                         archiveGetCryptPasswordUserData
                        );
    if (error != ERROR_NONE)
    {
      printError("Cannot open archive file '%s' (error: %s)!\n",
                 String_cString(archiveFileName),
                 Errors_getText(error)
                );
      if (failError == ERROR_NONE) failError = error;
      continue;
    }

    /* read files */
    while (   !Archive_eof(&archiveInfo,TRUE)
           && (failError == ERROR_NONE)
          )
    {
      /* get next archive entry type */
      error = Archive_getNextArchiveEntryType(&archiveInfo,
                                              &archiveEntryType,
                                              TRUE
                                             );
      if (error != ERROR_NONE)
      {
        printError("Cannot read next entry in archive '%s' (error: %s)!\n",
                   String_cString(archiveFileName),
                   Errors_getText(error)
                  );
        if (failError == ERROR_NONE) failError = error;
        break;
      }

      switch (archiveEntryType)
      {
        case ARCHIVE_ENTRY_TYPE_FILE:
          {
            String       fileName;
            FileInfo     fileInfo;
            uint64       fragmentOffset,fragmentSize;
            FragmentNode *fragmentNode;
//            FileInfo   localFileInfo;
            FileHandle   fileHandle;
            bool         equalFlag;
            uint64       length;
            ulong        n;
            ulong        diffIndex;

            /* read file */
            fileName = String_new();
            error = Archive_readFileEntry(&archiveInfo,
                                          &archiveEntryInfo,
                                          NULL,
                                          NULL,
                                          NULL,
                                          fileName,
                                          &fileInfo,
                                          &fragmentOffset,
                                          &fragmentSize
                                         );
            if (error != ERROR_NONE)
            {
              printError("Cannot read 'file' content of archive '%s' (error: %s)!\n",
                         String_cString(archiveFileName),
                         Errors_getText(error)
                        );
              String_delete(fileName);
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,fileName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              printInfo(2,"  Compare file '%s'...",String_cString(fileName));

              /* check if file exists and file type */
              if (!File_exists(fileName))
              {
                printInfo(2,"FAIL!\n");
                printError("File '%s' not found!\n",String_cString(fileName));
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILE_NOT_FOUND;
                }
                break;
              }
              if (File_getType(fileName) != FILE_TYPE_FILE)
              {
                printInfo(2,"FAIL!\n");
                printError("'%s' is not a file!\n",String_cString(fileName));
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_WRONG_FILE_TYPE;
                }
                break;
              }

              /* get file fragment list */
              fragmentNode = FragmentList_find(&fragmentList,fileName);
              if (fragmentNode == NULL)
              {
                fragmentNode = FragmentList_add(&fragmentList,fileName,fileInfo.size,NULL,0);
              }
//FragmentList_print(fragmentNode,String_cString(fileName));

              /* open file */
              error = File_open(&fileHandle,fileName,FILE_OPENMODE_READ);
              if (error != ERROR_NONE)
              {
                printInfo(2,"FAIL!\n");
                printError("Cannot open file '%s' (error: %s)\n",
                           String_cString(fileName),
                           Errors_getText(error)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = error;
                }
                continue;
              }

              /* check file size */
              if (fileInfo.size != File_getSize(&fileHandle))
              {
                printInfo(2,"FAIL!\n");
                printError("'%s' differ in size: expected %lld bytes, found %lld bytes\n",
                           String_cString(fileName),
                           fileInfo.size,
                           File_getSize(&fileHandle)
                          );
                File_close(&fileHandle);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILES_DIFFER;
                }
                continue;
              }

              /* compare archive and file content */
              error = File_seek(&fileHandle,fragmentOffset);
              if (error != ERROR_NONE)
              {
                printInfo(2,"FAIL!\n");
                printError("Cannot read file '%s' (error: %s)\n",
                           String_cString(fileName),
                           Errors_getText(error)
                          );
                File_close(&fileHandle);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = error;
                }
                continue;
              }
              length    = 0;
              equalFlag = TRUE;
              diffIndex = 0;
              while ((length < fragmentSize) && equalFlag)
              {
                n = MIN(fragmentSize-length,BUFFER_SIZE);

                /* read archive, file */
                error = Archive_readData(&archiveEntryInfo,archiveBuffer,n);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot read content of archive '%s' (error: %s)!\n",
                             String_cString(archiveFileName),
                             Errors_getText(error)
                            );
                  if (failError == ERROR_NONE) failError = error;
                  break;
                }
                error = File_read(&fileHandle,buffer,n,NULL);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot read file '%s' (error: %s)\n",
                             String_cString(fileName),
                             Errors_getText(error)
                            );
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = error;
                  }
                  break;
                }

                /* compare */
                diffIndex = compare(archiveBuffer,buffer,n);
                equalFlag = (diffIndex >= n);
                if (!equalFlag)
                {
                  printInfo(2,"FAIL!\n");
                  printError("'%s' differ at offset %llu\n",
                             String_cString(fileName),
                             fragmentOffset+length+(uint64)diffIndex
                            );
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = ERROR_FILES_DIFFER;
                  }
                  break;
                }

                length += n;
              }
              File_close(&fileHandle);
              if (failError != ERROR_NONE)
              {
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                continue;
              }

              /* add fragment to file fragment list */
              FragmentList_addEntry(fragmentNode,fragmentOffset,fragmentSize);

              /* discard fragment list if file is complete */
              if (FragmentList_checkEntryComplete(fragmentNode))
              {
                FragmentList_discard(&fragmentList,fragmentNode);
              }

#if 0
              /* get local file info */
              /* check file time, permissions, file owner/group */
#endif /* 0 */
              printInfo(2,"ok\n");

              /* check if all data read.
                 Note: it is not possible to check if all data is read when
                 compression is used. The decompressor may not all data even
                 data is _not_ corrupt.
              */
              if (   (archiveEntryInfo.file.compressAlgorithm == COMPRESS_ALGORITHM_NONE)
                  && !Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of file entry '%S'.\n",fileName);
              }

              /* free resources */
            }
            else
            {
              /* skip */
              printInfo(3,"  Compare '%s'...skipped\n",String_cString(fileName));
            }

            /* close archive file, free resources */
            error = Archive_closeEntry(&archiveEntryInfo);
            if (error != ERROR_NONE)
            {
              printWarning("close 'file' entry fail (error: %s)\n",Errors_getText(error));
            }

            /* free resources */
            String_delete(fileName);
          }
          break;
        case ARCHIVE_ENTRY_TYPE_IMAGE:
          {
            String       imageName;
            DeviceInfo   deviceInfo;
            uint64       blockOffset,blockCount;
            FragmentNode *fragmentNode;
            DeviceHandle deviceHandle;
            bool         equalFlag;
            uint64       block;
            ulong        bufferBlockCount;
            ulong        diffIndex;

            /* read image */
            imageName = String_new();
            error = Archive_readImageEntry(&archiveInfo,
                                           &archiveEntryInfo,
                                           NULL,
                                           NULL,
                                           NULL,
                                           imageName,
                                           &deviceInfo,
                                           &blockOffset,
                                           &blockCount
                                          );
            if (error != ERROR_NONE)
            {
              printError("Cannot read 'image' content of archive '%s' (error: %s)!\n",
                         String_cString(archiveFileName),
                         Errors_getText(error)
                        );
              String_delete(imageName);
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,imageName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(excludePatternList,imageName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              printInfo(2,"  Compare image '%s'...",String_cString(imageName));

              /* check if device exists */
              if (!File_exists(imageName))
              {
                printInfo(2,"FAIL!\n");
                printError("Device '%s' not found!\n",String_cString(imageName));
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(imageName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILE_NOT_FOUND;
                }
                break;
              }

              /* get image fragment list */
              fragmentNode = FragmentList_find(&fragmentList,imageName);
              if (fragmentNode == NULL)
              {
                fragmentNode = FragmentList_add(&fragmentList,imageName,deviceInfo.size,NULL,0);
              }

              /* open device */
              error = Device_open(&deviceHandle,imageName,DEVICE_OPENMODE_READ);
              if (error != ERROR_NONE)
              {
                printInfo(2,"FAIL!\n");
                printError("Cannot open file '%s' (error: %s)\n",
                           String_cString(imageName),
                           Errors_getText(error)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(imageName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = error;
                }
                continue;
              }

              /* check image size */
              if (deviceInfo.size != Device_getSize(&deviceHandle))
              {
                printInfo(2,"FAIL!\n");
                printError("'%s' differ in size: expected %lld bytes, found %lld bytes\n",
                           String_cString(imageName),
                           deviceInfo.size,
                           Device_getSize(&deviceHandle)
                          );
                Device_close(&deviceHandle);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(imageName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILES_DIFFER;
                }
                continue;
              }

              /* compare archive and device content */
              error = Device_seek(&deviceHandle,blockOffset*(uint64)deviceInfo.blockSize);
              if (error != ERROR_NONE)
              {
                printInfo(2,"FAIL!\n");
                printError("Cannot read file '%s' (error: %s)\n",
                           String_cString(imageName),
                           Errors_getText(error)
                          );
                Device_close(&deviceHandle);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(imageName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = error;
                }
                continue;
              }
              block     = 0LL;
              equalFlag = TRUE;
              diffIndex = 0;
              while ((block < blockCount) && equalFlag)
              {
                assert(deviceInfo.blockSize > 0);
                bufferBlockCount = MIN(blockCount-block,BUFFER_SIZE/deviceInfo.blockSize);

                /* read archive, file */
                error = Archive_readData(&archiveEntryInfo,archiveBuffer,bufferBlockCount*deviceInfo.blockSize);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot read content of archive '%s' (error: %s)!\n",
                             String_cString(archiveFileName),
                             Errors_getText(error)
                            );
                  if (failError == ERROR_NONE) failError = error;
                  break;
                }
                error = Device_read(&deviceHandle,buffer,bufferBlockCount*deviceInfo.blockSize,NULL);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot read file '%s' (error: %s)\n",
                             String_cString(imageName),
                             Errors_getText(error)
                            );
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = error;
                  }
                  break;
                }

                /* compare */
                diffIndex = compare(archiveBuffer,buffer,bufferBlockCount*deviceInfo.blockSize);
                equalFlag = (diffIndex >= bufferBlockCount*deviceInfo.blockSize);
                if (!equalFlag)
                {
                  printInfo(2,"FAIL!\n");
                  printError("'%s' differ at offset %llu\n",
                             String_cString(imageName),
                             blockOffset*(uint64)deviceInfo.blockSize+block*(uint64)deviceInfo.blockSize+(uint64)diffIndex
                            );
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = ERROR_FILES_DIFFER;
                  }
                  break;
                }

                block += (uint64)bufferBlockCount;
              }
              Device_close(&deviceHandle);
              if (failError != ERROR_NONE)
              {
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(imageName);
                continue;
              }

              /* add fragment to file fragment list */
              FragmentList_addEntry(fragmentNode,blockOffset*(uint64)deviceInfo.blockSize,blockCount*(uint64)deviceInfo.blockSize);

              /* discard fragment list if file is complete */
              if (FragmentList_checkEntryComplete(fragmentNode))
              {
                FragmentList_discard(&fragmentList,fragmentNode);
              }

              printInfo(2,"ok\n");

              /* check if all data read.
                 Note: it is not possible to check if all data is read when
                 compression is used. The decompressor may not all data even
                 data is _not_ corrupt.
              */
              if (   (archiveEntryInfo.image.compressAlgorithm == COMPRESS_ALGORITHM_NONE)
                  && !Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of image entry '%S'.\n",imageName);
              }

              /* free resources */
            }
            else
            {
              /* skip */
              printInfo(3,"  Compare '%s'...skipped\n",String_cString(imageName));
            }

            /* close archive file, free resources */
            error = Archive_closeEntry(&archiveEntryInfo);
            if (error != ERROR_NONE)
            {
              printWarning("close 'image' entry fail (error: %s)\n",Errors_getText(error));
            }

            /* free resources */
            String_delete(imageName);
          }
          break;
        case ARCHIVE_ENTRY_TYPE_DIRECTORY:
          {
            String   directoryName;
            FileInfo fileInfo;
//            String   localFileName;
//            FileInfo localFileInfo;

            /* read directory */
            directoryName = String_new();
            error = Archive_readDirectoryEntry(&archiveInfo,
                                               &archiveEntryInfo,
                                               NULL,
                                               NULL,
                                               directoryName,
                                               &fileInfo
                                              );
            if (error != ERROR_NONE)
            {
              printError("Cannot read 'directory' content of archive '%s' (error: %s)!\n",
                         String_cString(archiveFileName),
                         Errors_getText(error)
                        );
              String_delete(directoryName);
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,directoryName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(excludePatternList,directoryName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              printInfo(2,"  Compare directory '%s'...",String_cString(directoryName));

              /* check if file exists and file type */
              if (!File_exists(directoryName))
              {
                printInfo(2,"FAIL!\n");
                printError("Directory '%s' does not exists!\n",String_cString(directoryName));
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(directoryName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILE_NOT_FOUND;
                }
                break;
              }
              if (File_getType(directoryName) != FILE_TYPE_DIRECTORY)
              {
                printInfo(2,"FAIL!\n");
                printError("'%s' is not a directory!\n",
                           String_cString(directoryName)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(directoryName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_WRONG_FILE_TYPE;
                }
                break;
              }

#if 0
              /* get local file info */
              error = File_getFileInfo(&localFileInfo,directoryName);
              if (error != ERROR_NONE)
              {
                printError("Cannot read local directory '%s' (error: %s)!\n",
                           String_cString(directoryName),
                           Errors_getText(error)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(directoryName);
                if (failError == ERROR_NONE) failError = error;
                break;
              }

              /* check file time, permissions, file owner/group */
#endif /* 0 */
              printInfo(2,"ok\n");

              /* check if all data read */
              if (!Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of directory entry '%S'.\n",directoryName);
              }

              /* free resources */
            }
            else
            {
              /* skip */
              printInfo(3,"  Compare '%s'...skipped\n",String_cString(directoryName));
            }

            /* close archive file */
            error = Archive_closeEntry(&archiveEntryInfo);
            if (error != ERROR_NONE)
            {
              printWarning("close 'directory' entry fail (error: %s)\n",Errors_getText(error));
            }

            /* free resources */
            String_delete(directoryName);
          }
          break;
        case ARCHIVE_ENTRY_TYPE_LINK:
          {
            String   linkName;
            String   fileName;
            FileInfo fileInfo;
            String   localFileName;
//            FileInfo localFileInfo;

            /* read link */
            linkName = String_new();
            fileName = String_new();
            error = Archive_readLinkEntry(&archiveInfo,
                                          &archiveEntryInfo,
                                          NULL,
                                          NULL,
                                          linkName,
                                          fileName,
                                          &fileInfo
                                         );
            if (error != ERROR_NONE)
            {
              printError("Cannot read 'link' content of archive '%s' (error: %s)!\n",
                         String_cString(archiveFileName),
                         Errors_getText(error)
                        );
              String_delete(fileName);
              String_delete(linkName);
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,linkName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(excludePatternList,linkName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              printInfo(2,"  Compare link '%s'...",String_cString(linkName));

              /* check if file exists and file type */
              if (!File_exists(linkName))
              {
                printInfo(2,"FAIL!\n");
                printError("Link '%s' -> '%s' does not exists!\n",
                           String_cString(linkName),
                           String_cString(fileName)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                String_delete(linkName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILE_NOT_FOUND;
                }
                break;
              }
              if (File_getType(linkName) != FILE_TYPE_LINK)
              {
                printInfo(2,"FAIL!\n");
                printError("'%s' is not a link!\n",
                           String_cString(linkName)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                String_delete(linkName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_WRONG_FILE_TYPE;
                }
                break;
              }

              /* check link name */
              localFileName = String_new();
              error = File_readLink(localFileName,linkName);
              if (error != ERROR_NONE)
              {
                printError("Cannot read local file '%s' (error: %s)!\n",
                           String_cString(linkName),
                           Errors_getText(error)
                          );
                String_delete(localFileName);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                String_delete(linkName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = error;
                }
                break;
              }
              if (!String_equals(fileName,localFileName))
              {
                printInfo(2,"FAIL!\n");
                printError("Link '%s' does not contain file '%s'!\n",
                           String_cString(linkName),
                           String_cString(fileName)
                          );
                String_delete(localFileName);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                String_delete(linkName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILES_DIFFER;
                }
                break;
              }
              String_delete(localFileName);

#if 0
              /* get local file info */
              error = File_getFileInfo(&localFileInfo,linkName);
              if (error != ERROR_NONE)
              {
                printError("Cannot read local file '%s' (error: %s)!\n",
                           String_cString(linkName),
                           Errors_getText(error)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                String_delete(linkName);
                if (failError == ERROR_NONE) failError = error;
                break;
              }

              /* check file time, permissions, file owner/group */
#endif /* 0 */
              printInfo(2,"ok\n");

              /* check if all data read */
              if (!Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of link entry '%S'.\n",linkName);
              }

              /* free resources */
            }
            else
            {
              /* skip */
              printInfo(3,"  Compare '%s'...skipped\n",String_cString(linkName));
            }

            /* close archive file */
            error = Archive_closeEntry(&archiveEntryInfo);
            if (error != ERROR_NONE)
            {
              printWarning("close 'link' entry fail (error: %s)\n",Errors_getText(error));
            }

            /* free resources */
            String_delete(fileName);
            String_delete(linkName);
          }
          break;
        case ARCHIVE_ENTRY_TYPE_HARDLINK:
          {
            StringList       fileNameList;
            FileInfo         fileInfo;
            uint64           fragmentOffset,fragmentSize;
            bool             comparedDataFlag;
            const StringNode *stringNode;
            String           fileName;
            FragmentNode     *fragmentNode;
//            FileInfo       localFileInfo;
            FileHandle       fileHandle;
            bool             equalFlag;
            uint64           length;
            ulong            n;
            ulong            diffIndex;

            /* read hard link */
            StringList_init(&fileNameList);
            error = Archive_readHardLinkEntry(&archiveInfo,
                                              &archiveEntryInfo,
                                              NULL,
                                              NULL,
                                              NULL,
                                              &fileNameList,
                                              &fileInfo,
                                              &fragmentOffset,
                                              &fragmentSize
                                             );
            if (error != ERROR_NONE)
            {
              printError("Cannot read 'hard link' content of archive '%s' (error: %s)!\n",
                         String_cString(archiveFileName),
                         Errors_getText(error)
                        );
              StringList_done(&fileNameList);
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            comparedDataFlag = FALSE;
            STRINGLIST_ITERATE(&fileNameList,stringNode,fileName)
            {
              if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,fileName,PATTERN_MATCH_MODE_EXACT))
                  && !PatternList_match(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
                 )
              {
                printInfo(2,"  Compare hard link '%s'...",String_cString(fileName));

                /* check file if exists and file type */
                if (!File_exists(fileName))
                {
                  printInfo(2,"FAIL!\n");
                  printError("File '%s' not found!\n",String_cString(fileName));
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = ERROR_FILE_NOT_FOUND;
                    break;
                  }
                  else
                  {
                    continue;
                  }
                }
                if (File_getType(fileName) != FILE_TYPE_HARDLINK)
                {
                  printInfo(2,"FAIL!\n");
                  printError("'%s' is not a hard link!\n",String_cString(fileName));
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = ERROR_WRONG_FILE_TYPE;
                    break;
                  }
                  else
                  {
                    continue;
                  }
                }

                if (!comparedDataFlag && (failError == ERROR_NONE))
                {
                  /* compare hard link data */

                  /* get file fragment list */
                  fragmentNode = FragmentList_find(&fragmentList,fileName);
                  if (fragmentNode == NULL)
                  {
                    fragmentNode = FragmentList_add(&fragmentList,fileName,fileInfo.size,NULL,0);
                  }
//FragmentList_print(fragmentNode,String_cString(fileName));

                  /* open file */
                  error = File_open(&fileHandle,fileName,FILE_OPENMODE_READ);
                  if (error != ERROR_NONE)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot open file '%s' (error: %s)\n",
                               String_cString(fileName),
                               Errors_getText(error)
                              );
                    if (jobOptions->stopOnErrorFlag)
                    {
                      failError = error;
                      break;
                    }
                    else
                    {
                      continue;
                    }
                  }

                  /* check file size */
                  if (fileInfo.size != File_getSize(&fileHandle))
                  {
                    printInfo(2,"FAIL!\n");
                    printError("'%s' differ in size: expected %lld bytes, found %lld bytes\n",
                               String_cString(fileName),
                               fileInfo.size,
                               File_getSize(&fileHandle)
                              );
                    File_close(&fileHandle);
                    if (jobOptions->stopOnErrorFlag)
                    {
                      failError = ERROR_FILES_DIFFER;
                      break;
                    }
                    else
                    {
                      continue;
                    }
                  }

                  /* compare archive and hard link content */
                  error = File_seek(&fileHandle,fragmentOffset);
                  if (error != ERROR_NONE)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot read file '%s' (error: %s)\n",
                               String_cString(fileName),
                               Errors_getText(error)
                              );
                    File_close(&fileHandle);
                    if (jobOptions->stopOnErrorFlag)
                    {
                      failError = error;
                      break;
                    }
                    else
                    {
                      continue;
                    }
                  }
                  length    = 0;
                  equalFlag = TRUE;
                  diffIndex = 0;
                  while ((length < fragmentSize) && equalFlag)
                  {
                    n = MIN(fragmentSize-length,BUFFER_SIZE);

                    /* read archive, file */
                    error = Archive_readData(&archiveEntryInfo,archiveBuffer,n);
                    if (error != ERROR_NONE)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot read content of archive '%s' (error: %s)!\n",
                                 String_cString(archiveFileName),
                                 Errors_getText(error)
                                );
                      if (failError == ERROR_NONE) failError = error;
                      break;
                    }
                    error = File_read(&fileHandle,buffer,n,NULL);
                    if (error != ERROR_NONE)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot read file '%s' (error: %s)\n",
                                 String_cString(fileName),
                                 Errors_getText(error)
                                );
                      if (jobOptions->stopOnErrorFlag)
                      {
                        failError = error;
                      }
                      break;
                    }

                    /* compare */
                    diffIndex = compare(archiveBuffer,buffer,n);
                    equalFlag = (diffIndex >= n);
                    if (!equalFlag)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("'%s' differ at offset %llu\n",
                                 String_cString(fileName),
                                 fragmentOffset+length+(uint64)diffIndex
                                );
                      if (jobOptions->stopOnErrorFlag)
                      {
                        failError = ERROR_FILES_DIFFER;
                      }
                      break;
                    }

                    length += n;
                  }
                  if (failError != ERROR_NONE)
                  {
                    File_close(&fileHandle);
                    break;
                  }

                  /* close file */
                  File_close(&fileHandle);

                  /* add fragment to file fragment list */
                  FragmentList_addEntry(fragmentNode,fragmentOffset,fragmentSize);

                  /* discard fragment list if file is complete */
                  if (FragmentList_checkEntryComplete(fragmentNode))
                  {
                    FragmentList_discard(&fragmentList,fragmentNode);
                  }
#if 0
                  /* get local file info */
                  /* check file time, permissions, file owner/group */
#endif /* 0 */
                  printInfo(2,"ok\n");

                  /* check if all data read.
                     Note: it is not possible to check if all data is read when
                     compression is used. The decompressor may not all data even
                     data is _not_ corrupt.
                  */
                  if (   (archiveEntryInfo.hardLink.compressAlgorithm == COMPRESS_ALGORITHM_NONE)
                      && !Archive_eofData(&archiveEntryInfo))
                  {
                    printWarning("unexpected data at end of hard link entry '%S'.\n",fileName);
                  }

                  comparedDataFlag = TRUE;
                }
                else
                {
                  /* compare hard link data already done */
                  if (failError == ERROR_NONE)
                  {
                    printInfo(2,"ok\n");
                  }
                  else
                  {
                    printInfo(2,"FAIL!\n");
                  }
                }
              }
              else
              {
                /* skip */
                printInfo(3,"  Compare '%s'...skipped\n",String_cString(fileName));
              }
            }

            /* close archive file, free resources */
            error = Archive_closeEntry(&archiveEntryInfo);
            if (error != ERROR_NONE)
            {
              printWarning("close 'hard link' entry fail (error: %s)\n",Errors_getText(error));
            }

            /* free resources */
            StringList_done(&fileNameList);
          }
          break;
        case ARCHIVE_ENTRY_TYPE_SPECIAL:
          {
            String   fileName;
            FileInfo fileInfo;
            FileInfo localFileInfo;

            /* read special */
            fileName = String_new();
            error = Archive_readSpecialEntry(&archiveInfo,
                                             &archiveEntryInfo,
                                             NULL,
                                             NULL,
                                             fileName,
                                             &fileInfo
                                            );
            if (error != ERROR_NONE)
            {
              printError("Cannot read 'special' content of archive '%s' (error: %s)!\n",
                         String_cString(archiveFileName),
                         Errors_getText(error)
                        );
              String_delete(fileName);
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,fileName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              printInfo(2,"  Compare special device '%s'...",String_cString(fileName));

              /* check if file exists and file type */
              if (!File_exists(fileName))
              {
                printInfo(2,"FAIL!\n");
                printError("Special device '%s' does not exists!\n",
                           String_cString(fileName)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_FILE_NOT_FOUND;
                }
                break;
              }
              if (File_getType(fileName) != FILE_TYPE_SPECIAL)
              {
                printInfo(2,"FAIL!\n");
                printError("'%s' is not a special device!\n",
                           String_cString(fileName)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = ERROR_WRONG_FILE_TYPE;
                }
                break;
              }

              /* check special settings */
              error = File_getFileInfo(&localFileInfo,fileName);
              if (error != ERROR_NONE)
              {
                printError("Cannot read local file '%s' (error: %s)!\n",
                           String_cString(fileName),
                           Errors_getText(error)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = error;
                }
                break;
              }
              if (fileInfo.specialType != localFileInfo.specialType)
              {
                printError("Different types of special device '%s'!\n",
                           String_cString(fileName)
                          );
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  failError = error;
                }
                break;
              }
              if (   (fileInfo.specialType == FILE_SPECIAL_TYPE_CHARACTER_DEVICE)
                  || (fileInfo.specialType == FILE_SPECIAL_TYPE_BLOCK_DEVICE)
                 )
              {
                if (fileInfo.major != localFileInfo.major)
                {
                  printError("Different major numbers of special device '%s'!\n",
                             String_cString(fileName)
                            );
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = error;
                  }
                  break;
                }
                if (fileInfo.minor != localFileInfo.minor)
                {
                  printError("Different minor numbers of special device '%s'!\n",
                             String_cString(fileName)
                            );
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    failError = error;
                  }
                  break;
                }
              }

#if 0

              /* check file time, permissions, file owner/group */
#endif /* 0 */

              printInfo(2,"ok\n");

              /* check if all data read */
              if (!Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of special entry '%S'.\n",fileName);
              }

              /* free resources */
            }
            else
            {
              /* skip */
              printInfo(3,"  Compare '%s'...skipped\n",String_cString(fileName));
            }

            /* close archive file */
            error = Archive_closeEntry(&archiveEntryInfo);
            if (error != ERROR_NONE)
            {
              printWarning("close 'special' entry fail (error: %s)\n",Errors_getText(error));
            }

            /* free resources */
            String_delete(fileName);
          }
          break;
        default:
          #ifndef NDEBUG
            HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          #endif /* NDEBUG */
          break; /* not reached */
      }
    }

    /* close archive */
    Archive_close(&archiveInfo);
  }

  /* check fragment lists */
  for (fragmentNode = fragmentList.head; fragmentNode != NULL; fragmentNode = fragmentNode->next)
  {
    if (!FragmentList_checkEntryComplete(fragmentNode))
    {
      printInfo(0,"Warning: incomplete entry '%s'\n",String_cString(fragmentNode->name));
      if (failError == ERROR_NONE) failError = ERROR_FILE_INCOMPLETE;
    }
  }

  /* free resources */
  String_delete(archiveFileName);
  FragmentList_done(&fragmentList);
  free(buffer);
  free(archiveBuffer);

  return failError;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
