/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/commands_restore.c,v $
* $Revision: 1.10 $
* $Author: torsten $
* Contents: Backup ARchiver archive restore function
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
#include "patternlists.h"
#include "files.h"
#include "archive.h"
#include "fragmentlists.h"
#include "misc.h"

#include "commands_restore.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/* file data buffer size */
#define BUFFER_SIZE (64*1024)

/***************************** Datatypes *******************************/

/* restore information */
typedef struct
{
  EntryList                 *includeEntryList;       // included entries (can be empty)
  PatternList               *excludePatternList;     // excluded entries (can be empty or NULL)
  const JobOptions          *jobOptions;
  bool                      *pauseFlag;              // pause flag (can be NULL)
  bool                      *requestedAbortFlag;     // request abort flag (can be NULL)

  Errors                    failError;               // restore error

  RestoreStatusInfoFunction statusInfoFunction;      // status info call back
  void                      *statusInfoUserData;     // user data for status info call back
  RestoreStatusInfo         statusInfo;              // status info
} RestoreInfo;

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : getDestinationFileName
* Purpose: get destination file name by stripping directory levels and
*          add destination directory
* Input  : destinationFileName - destination file name variable
*          fileName            - original file name
*          destination         - destination directory or NULL
*          directoryStripCount - number of directories to strip from
*                                original file name
* Output : -
* Return : file name
* Notes  : -
\***********************************************************************/

LOCAL String getDestinationFileName(String       destinationFileName,
                                    String       fileName,
                                    const String destination,
                                    uint         directoryStripCount
                                   )
{
  String          pathName,baseName,name;
  StringTokenizer fileNameTokenizer;
  int             z;

  assert(destinationFileName != NULL);
  assert(fileName != NULL);

  /* get destination base directory */
  if (destination != NULL)
  {
    File_setFileName(destinationFileName,destination);
  }
  else
  {
    File_clearFileName(destinationFileName);
  }

  /* split file name */
  File_splitFileName(fileName,&pathName,&baseName);

  /* strip directory, create destination directory */
  File_initSplitFileName(&fileNameTokenizer,pathName);
  z = 0;
  while ((z< directoryStripCount) && File_getNextSplitFileName(&fileNameTokenizer,&name))
  {
    z++;
  }
  while (File_getNextSplitFileName(&fileNameTokenizer,&name))
  {
    File_appendFileName(destinationFileName,name);
  }
  File_doneSplitFileName(&fileNameTokenizer);

  /* create destination file name */
  File_appendFileName(destinationFileName,baseName);

  /* free resources  */
  String_delete(pathName);
  String_delete(baseName);

  return destinationFileName;
}

/***********************************************************************\
* Name   : getDestinationDeviceName
* Purpose: get destination device name
* Input  : destinationDeviceName - destination device name variable
*          imageName             - original file name
*          destination           - destination device or NULL
* Output : -
* Return : device name
* Notes  : -
\***********************************************************************/

LOCAL String getDestinationDeviceName(String       destinationDeviceName,
                                      String       imageName,
                                      const String destination
                                     )
{
  assert(destinationDeviceName != NULL);
  assert(imageName != NULL);

  if (destination != NULL)
  {
    File_setFileName(destinationDeviceName,destination);
  }
  else
  {
    File_setFileName(destinationDeviceName,imageName);
  }

  return destinationDeviceName;
}

/***********************************************************************\
* Name   : updateStatusInfo
* Purpose: update status info
* Input  : createInfo - create info
* Output : -
* Return : bool TRUE to continue, FALSE to abort
* Notes  : -
\***********************************************************************/

LOCAL bool updateStatusInfo(const RestoreInfo *restoreInfo)
{
  assert(restoreInfo != NULL);

  if (restoreInfo->statusInfoFunction != NULL)
  {
    return restoreInfo->statusInfoFunction(restoreInfo->statusInfoUserData,restoreInfo->failError,&restoreInfo->statusInfo);
  }
  else
  {
    return TRUE;
  }
}

/*---------------------------------------------------------------------*/

Errors Command_restore(StringList                      *archiveNameList,
                       EntryList                       *includeEntryList,
                       PatternList                     *excludePatternList,
                       JobOptions                      *jobOptions,
                       ArchiveGetCryptPasswordFunction archiveGetCryptPasswordFunction,
                       void                            *archiveGetCryptPasswordUserData,
                       RestoreStatusInfoFunction       restoreStatusInfoFunction,
                       void                            *restoreStatusInfoUserData,
                       bool                            *pauseFlag,
                       bool                            *requestedAbortFlag
                      )
{
  RestoreInfo       restoreInfo;
  byte              *buffer;
  FragmentList      fragmentList;
  String            archiveName;
  String            printableArchiveName;
  bool              abortFlag;
  Errors            error;
  ArchiveInfo       archiveInfo;
  ArchiveEntryInfo  archiveEntryInfo;
  ArchiveEntryTypes archiveEntryType;
  FragmentNode      *fragmentNode;

  assert(archiveNameList != NULL);
  assert(includeEntryList != NULL);
  assert(jobOptions != NULL);

  /* initialize variables */
  restoreInfo.includeEntryList             = includeEntryList;
  restoreInfo.excludePatternList           = excludePatternList;
  restoreInfo.jobOptions                   = jobOptions;
  restoreInfo.pauseFlag                    = pauseFlag;
  restoreInfo.requestedAbortFlag           = requestedAbortFlag;
  restoreInfo.failError                    = ERROR_NONE;
  restoreInfo.statusInfoFunction           = restoreStatusInfoFunction;
  restoreInfo.statusInfoUserData           = restoreStatusInfoUserData;
  restoreInfo.statusInfo.doneEntries       = 0L;
  restoreInfo.statusInfo.doneBytes         = 0LL;
  restoreInfo.statusInfo.skippedEntries    = 0L;
  restoreInfo.statusInfo.skippedBytes      = 0LL;
  restoreInfo.statusInfo.errorEntries      = 0L;
  restoreInfo.statusInfo.errorBytes        = 0LL;
  restoreInfo.statusInfo.name              = String_new();
  restoreInfo.statusInfo.entryDoneBytes    = 0LL;
  restoreInfo.statusInfo.entryTotalBytes   = 0LL;
  restoreInfo.statusInfo.storageName       = String_new();
  restoreInfo.statusInfo.archiveDoneBytes  = 0LL;
  restoreInfo.statusInfo.archiveTotalBytes = 0LL;

  /* allocate resources */
  buffer = malloc(BUFFER_SIZE);
  if (buffer == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  FragmentList_init(&fragmentList);
  archiveName          = String_new();
  printableArchiveName = String_new();

  abortFlag = FALSE;
  while (   !abortFlag
         && ((restoreInfo.requestedAbortFlag == NULL) || !(*restoreInfo.requestedAbortFlag))
         && !StringList_empty(archiveNameList)
         && (restoreInfo.failError == ERROR_NONE)
        )
  {
    /* pause */
    while ((restoreInfo.pauseFlag != NULL) && (*restoreInfo.pauseFlag))
    {
      Misc_udelay(500*1000);
    }

    StringList_getFirst(archiveNameList,archiveName);
    Storage_getPrintableName(printableArchiveName,archiveName);
    printInfo(0,"Restore from archive '%s':\n",String_cString(printableArchiveName));

    /* open archive */
    error = Archive_open(&archiveInfo,
                         archiveName,
                         jobOptions,
                         archiveGetCryptPasswordFunction,
                         archiveGetCryptPasswordUserData
                        );
    if (error != ERROR_NONE)
    {
      printError("Cannot open archive file '%s' (error: %s)!\n",
                 String_cString(printableArchiveName),
                 Errors_getText(error)
                );
      if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
      continue;
    }
    String_set(restoreInfo.statusInfo.storageName,printableArchiveName);
    abortFlag = !updateStatusInfo(&restoreInfo);

    /* read files */
    while (   ((restoreInfo.requestedAbortFlag == NULL) || !(*restoreInfo.requestedAbortFlag))
           && !Archive_eof(&archiveInfo,TRUE)
           && (restoreInfo.failError == ERROR_NONE)
          )
    {
      /* pause */
      while ((restoreInfo.pauseFlag != NULL) && (*restoreInfo.pauseFlag))
      {
        Misc_udelay(500*1000);
      }

      /* get next archive entry type */
      error = Archive_getNextArchiveEntryType(&archiveInfo,
                                              &archiveEntryType,
                                              TRUE
                                             );
      if (error != ERROR_NONE)
      {
        printError("Cannot read next entry in archive '%s' (error: %s)!\n",
                   String_cString(printableArchiveName),
                   Errors_getText(error)
                  );
        if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
        break;
      }

      switch (archiveEntryType)
      {
        case ARCHIVE_ENTRY_TYPE_FILE:
          {
            String       fileName;
            FileInfo     fileInfo;
            uint64       fragmentOffset,fragmentSize;
            String       destinationFileName;
            FragmentNode *fragmentNode;
            String       parentDirectoryName;
//            FileInfo         localFileInfo;
            FileHandle   fileHandle;
            uint64       length;
            ulong        n;

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
                         String_cString(printableArchiveName),
                         Errors_getText(error)
                        );
              String_delete(fileName);
              if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
              continue;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,fileName,PATTERN_MATCH_MODE_EXACT))
                && ((excludePatternList == NULL) || !PatternList_match(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
               )
            {
              String_set(restoreInfo.statusInfo.name,fileName);
              restoreInfo.statusInfo.entryDoneBytes  = 0LL;
              restoreInfo.statusInfo.entryTotalBytes = fragmentSize;
              abortFlag = !updateStatusInfo(&restoreInfo);

              /* get destination filename */
              destinationFileName = getDestinationFileName(String_new(),
                                                           fileName,
                                                           jobOptions->destination,
                                                           jobOptions->directoryStripCount
                                                          );

              /* check if file fragment already exists, file already exists */
              fragmentNode = FragmentList_find(&fragmentList,destinationFileName);
              if (fragmentNode != NULL)
              {
                if (!jobOptions->overwriteFilesFlag && FragmentList_checkEntryExists(fragmentNode,fragmentOffset,fragmentSize))
                {
                  printInfo(1,
                            "  Restore file '%s'...skipped (file part %llu..%llu exists)\n",
                            String_cString(destinationFileName),
                            fragmentOffset,
                            (fragmentSize > 0LL)?fragmentOffset+fragmentSize-1:fragmentOffset
                           );
                  String_delete(destinationFileName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  continue;
                }
              }
              else
              {
                if (!jobOptions->overwriteFilesFlag && File_exists(destinationFileName))
                {
                  printInfo(1,"  Restore file '%s'...skipped (file exists)\n",String_cString(destinationFileName));
                  String_delete(destinationFileName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  continue;
                }
                fragmentNode = FragmentList_add(&fragmentList,destinationFileName,fileInfo.size,&fileInfo,sizeof(FileInfo));
              }

              printInfo(2,"  Restore file '%s'...",String_cString(destinationFileName));

              /* create parent directories if not existing */
              if (!jobOptions->dryRunFlag)
              {
                parentDirectoryName = File_getFilePathName(String_new(),destinationFileName);
                if (!String_empty(parentDirectoryName) && !File_exists(parentDirectoryName))
                {
                  /* create directory */
                  error = File_makeDirectory(parentDirectoryName,
                                             FILE_DEFAULT_USER_ID,
                                             FILE_DEFAULT_GROUP_ID,
                                             FILE_DEFAULT_PERMISSION
                                            );
                  if (error != ERROR_NONE)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot create directory '%s' (error: %s)\n",
                               String_cString(parentDirectoryName),
                               Errors_getText(error)
                              );
                    String_delete(parentDirectoryName);
                    String_delete(destinationFileName);
                    Archive_closeEntry(&archiveEntryInfo);
                    String_delete(fileName);
                    if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                    continue;
                  }

                  /* set directory owner ship */
                  error = File_setOwner(parentDirectoryName,
                                        (jobOptions->owner.userId != FILE_DEFAULT_USER_ID)?jobOptions->owner.userId:fileInfo.userId,
                                        (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID)?jobOptions->owner.groupId:fileInfo.groupId
                                       );
                  if (error != ERROR_NONE)
                  {
                    if (jobOptions->stopOnErrorFlag)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot set owner ship of directory '%s' (error: %s)\n",
                                 String_cString(parentDirectoryName),
                                 Errors_getText(error)
                                );
                      String_delete(parentDirectoryName);
                      String_delete(destinationFileName);
                      Archive_closeEntry(&archiveEntryInfo);
                      String_delete(fileName);
                      if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                      continue;
                    }
                    else
                    {
                      printWarning("Cannot set owner ship of directory '%s' (error: %s)\n",
                                   String_cString(parentDirectoryName),
                                   Errors_getText(error)
                                  );
                    }
                  }
                }
                String_delete(parentDirectoryName);
              }

              /* create file */
              if (!jobOptions->dryRunFlag)
              {
                /* open file */
                error = File_open(&fileHandle,destinationFileName,FILE_OPENMODE_WRITE);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot create/write to file '%s' (error: %s)\n",
                             String_cString(destinationFileName),
                             Errors_getText(error)
                            );
                  String_delete(destinationFileName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    restoreInfo.failError = error;
                  }
                  continue;
                }

                /* seek to fragment position */
                error = File_seek(&fileHandle,fragmentOffset);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot write file '%s' (error: %s)\n",
                             String_cString(destinationFileName),
                             Errors_getText(error)
                            );
                  File_close(&fileHandle);
                  String_delete(destinationFileName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    restoreInfo.failError = error;
                  }
                  continue;
                }
              }

              /* write file data */
              length = 0;
              while (   ((restoreInfo.requestedAbortFlag == NULL) || !(*restoreInfo.requestedAbortFlag))
                     && (length < fragmentSize)
                    )
              {
                /* pause */
                while ((restoreInfo.pauseFlag != NULL) && (*restoreInfo.pauseFlag))
                {
                  Misc_udelay(500*1000);
                }

                n = MIN(fragmentSize-length,BUFFER_SIZE);

                error = Archive_readData(&archiveEntryInfo,buffer,n);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot read content of archive '%s' (error: %s)!\n",
                             String_cString(printableArchiveName),
                             Errors_getText(error)
                            );
                  if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                  break;
                }
                if (!jobOptions->dryRunFlag)
                {
                  error = File_write(&fileHandle,buffer,n);
                  if (error != ERROR_NONE)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot write file '%s' (error: %s)\n",
                               String_cString(destinationFileName),
                               Errors_getText(error)
                              );
                    if (jobOptions->stopOnErrorFlag)
                    {
                      restoreInfo.failError = error;
                    }
                    break;
                  }
                }
                restoreInfo.statusInfo.entryDoneBytes += n;
                abortFlag = !updateStatusInfo(&restoreInfo);

                length += n;
              }
              if      (restoreInfo.failError != ERROR_NONE)
              {
                if (!jobOptions->dryRunFlag)
                {
                  File_close(&fileHandle);
                }
                String_delete(destinationFileName);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                continue;
              }
              else if ((restoreInfo.requestedAbortFlag != NULL) && (*restoreInfo.requestedAbortFlag))
              {
                printInfo(2,"ABORTED\n");
                if (!jobOptions->dryRunFlag)
                {
                  File_close(&fileHandle);
                }
                String_delete(destinationFileName);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                continue;
              }

              /* add fragment to file fragment list */
              FragmentList_addEntry(fragmentNode,fragmentOffset,fragmentSize);
//FragmentList_debugPrintInfo(fragmentNode,String_cString(fileName));

              /* set file size */
              if (!jobOptions->dryRunFlag)
              {
                if (File_getSize(&fileHandle) > fileInfo.size)
                {
                  File_truncate(&fileHandle,fileInfo.size);
                }
              }

              /* close file */
              if (!jobOptions->dryRunFlag)
              {
                File_close(&fileHandle);
              }

              /* check if file is complete */
              if (FragmentList_checkEntryComplete(fragmentNode))
              {
                /* set file time, file owner/group, file permission */
                if (!jobOptions->dryRunFlag)
                {
                  assert(fragmentNode->userData != NULL);

                  if (jobOptions->owner.userId  != FILE_DEFAULT_USER_ID ) ((FileInfo*)fragmentNode->userData)->userId  = jobOptions->owner.userId;
                  if (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID) ((FileInfo*)fragmentNode->userData)->groupId = jobOptions->owner.groupId;
                  error = File_setFileInfo(fragmentNode->name,(FileInfo*)fragmentNode->userData);
                  if (error != ERROR_NONE)
                  {
                    if (jobOptions->stopOnErrorFlag)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot set file info of '%s' (error: %s)\n",
                                 String_cString(fragmentNode->name),
                                 Errors_getText(error)
                                );
                      String_delete(destinationFileName);
                      Archive_closeEntry(&archiveEntryInfo);
                      String_delete(fileName);
                      restoreInfo.failError = error;
                      continue;
                    }
                    else
                    {
                      printWarning("Cannot set file info of '%s' (error: %s)\n",
                                   String_cString(fragmentNode->name),
                                   Errors_getText(error)
                                  );
                    }
                  }
                }

                /* discard fragment list */
                FragmentList_discard(&fragmentList,fragmentNode);
              }

              if (!jobOptions->dryRunFlag)
              {
                printInfo(2,"ok\n");
              }
              else
              {
                printInfo(2,"ok (dry-run)\n");
              }

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
              String_delete(destinationFileName);
            }
            else
            {
              /* skip */
              printInfo(3,"  Restore '%s'...skipped\n",String_cString(fileName));
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
            String       destinationDeviceName;
            FragmentNode *fragmentNode;
            DeviceHandle deviceHandle;
            uint64       block;
            ulong        bufferBlockCount;

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
                         String_cString(printableArchiveName),
                         Errors_getText(error)
                        );
              String_delete(imageName);
              if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,imageName,PATTERN_MATCH_MODE_EXACT))
                && ((excludePatternList == NULL) || !PatternList_match(excludePatternList,imageName,PATTERN_MATCH_MODE_EXACT))
               )
            {
              String_set(restoreInfo.statusInfo.name,imageName);
              restoreInfo.statusInfo.entryDoneBytes  = 0LL;
              restoreInfo.statusInfo.entryTotalBytes = blockCount;
              abortFlag = !updateStatusInfo(&restoreInfo);

              /* get destination filename */
              destinationDeviceName = getDestinationDeviceName(String_new(),
                                                               imageName,
                                                               jobOptions->destination
                                                              );


              /* check if image fragment exists */
              fragmentNode = FragmentList_find(&fragmentList,imageName);
              if (fragmentNode != NULL)
              {
                if (!jobOptions->overwriteFilesFlag && FragmentList_checkEntryExists(fragmentNode,blockOffset*(uint64)deviceInfo.blockSize,blockCount*(uint64)deviceInfo.blockSize))
                {
                  printInfo(1,
                            "  Restore image '%s'...skipped (image part %llu..%llu exists)\n",
                            String_cString(destinationDeviceName),
                            blockOffset*(uint64)deviceInfo.blockSize,
                            ((blockCount > 0)?blockOffset+blockCount-1:blockOffset)*(uint64)deviceInfo.blockSize
                           );
                  String_delete(destinationDeviceName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(imageName);
                  continue;
                }
              }
              else
              {
                fragmentNode = FragmentList_add(&fragmentList,imageName,deviceInfo.size,NULL,0);
              }

              printInfo(2,"  Restore image '%s'...",String_cString(destinationDeviceName));

              /* open device */
              if (!jobOptions->dryRunFlag)
              {
                error = Device_open(&deviceHandle,destinationDeviceName,DEVICE_OPENMODE_WRITE);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot write to device '%s' (error: %s)\n",
                             String_cString(destinationDeviceName),
                             Errors_getText(error)
                            );
                  String_delete(destinationDeviceName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(imageName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    restoreInfo.failError = error;
                  }
                  continue;
                }
                error = Device_seek(&deviceHandle,blockOffset*(uint64)deviceInfo.blockSize);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot write to device '%s' (error: %s)\n",
                             String_cString(destinationDeviceName),
                             Errors_getText(error)
                            );
                  Device_close(&deviceHandle);
                  String_delete(destinationDeviceName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(imageName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    restoreInfo.failError = error;
                  }
                  continue;
                }
              }

              /* write image data */
              block = 0;
              while (   ((restoreInfo.requestedAbortFlag == NULL) || !(*restoreInfo.requestedAbortFlag))
                     && (block < blockCount)
                    )
              {
                /* pause */
                while ((restoreInfo.pauseFlag != NULL) && (*restoreInfo.pauseFlag))
                {
                  Misc_udelay(500*1000);
                }

                assert(deviceInfo.blockSize > 0);
                bufferBlockCount = MIN(blockCount-block,BUFFER_SIZE/deviceInfo.blockSize);

                error = Archive_readData(&archiveEntryInfo,buffer,bufferBlockCount*deviceInfo.blockSize);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot read content of archive '%s' (error: %s)!\n",
                             String_cString(printableArchiveName),
                             Errors_getText(error)
                            );
                  if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                  break;
                }
                if (!jobOptions->dryRunFlag)
                {
                  error = Device_write(&deviceHandle,buffer,bufferBlockCount*deviceInfo.blockSize);
                  if (error != ERROR_NONE)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot write to device '%s' (error: %s)\n",
                               String_cString(destinationDeviceName),
                               Errors_getText(error)
                              );
                    if (jobOptions->stopOnErrorFlag)
                    {
                      restoreInfo.failError = error;
                    }
                    break;
                  }
                }
                restoreInfo.statusInfo.entryDoneBytes += bufferBlockCount*deviceInfo.blockSize;
                abortFlag = !updateStatusInfo(&restoreInfo);

                block += (uint64)bufferBlockCount;
              }
              if (!jobOptions->dryRunFlag)
              {
                Device_close(&deviceHandle);
                if ((restoreInfo.requestedAbortFlag != NULL) && (*restoreInfo.requestedAbortFlag))
                {
                  printInfo(2,"ABORTED\n");
                  String_delete(destinationDeviceName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(imageName);
                  continue;
                }
              }

              /* add fragment to file fragment list */
              FragmentList_addEntry(fragmentNode,blockOffset*(uint64)deviceInfo.blockSize,blockCount*(uint64)deviceInfo.blockSize);
//FragmentList_debugPrintInfo(fragmentNode,String_cString(fileName));

              /* discard fragment list if file is complete */
              if (FragmentList_checkEntryComplete(fragmentNode))
              {
                FragmentList_discard(&fragmentList,fragmentNode);
              }

              if (!jobOptions->dryRunFlag)
              {
                printInfo(2,"ok\n");
              }
              else
              {
                printInfo(2,"ok (dry-run)\n");
              }

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
              String_delete(destinationDeviceName);
            }
            else
            {
              /* skip */
              printInfo(3,"  Restore '%s'...skipped\n",String_cString(imageName));
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
            String   destinationFileName;
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
                         String_cString(printableArchiveName),
                         Errors_getText(error)
                        );
              String_delete(directoryName);
              if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,directoryName,PATTERN_MATCH_MODE_EXACT))
                && ((excludePatternList == NULL) || !PatternList_match(excludePatternList,directoryName,PATTERN_MATCH_MODE_EXACT))
               )
            {
              String_set(restoreInfo.statusInfo.name,directoryName);
              restoreInfo.statusInfo.entryDoneBytes  = 0LL;
              restoreInfo.statusInfo.entryTotalBytes = 00L;
              abortFlag = !updateStatusInfo(&restoreInfo);

              /* get destination filename */
              destinationFileName = getDestinationFileName(String_new(),
                                                           directoryName,
                                                           jobOptions->destination,
                                                           jobOptions->directoryStripCount
                                                          );


              /* check if directory already exists */
              if (!jobOptions->overwriteFilesFlag && File_exists(destinationFileName))
              {
                printInfo(1,
                          "  Restore directory '%s'...skipped (file exists)\n",
                          String_cString(destinationFileName)
                         );
                String_delete(destinationFileName);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(directoryName);
                continue;
              }

              printInfo(2,"  Restore directory '%s'...",String_cString(destinationFileName));

              /* create directory */
              if (!jobOptions->dryRunFlag)
              {
                error = File_makeDirectory(destinationFileName,
                                           FILE_DEFAULT_USER_ID,
                                           FILE_DEFAULT_GROUP_ID,
                                           fileInfo.permission
                                          );
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot create directory '%s' (error: %s)\n",
                             String_cString(destinationFileName),
                             Errors_getText(error)
                            );
                  String_delete(destinationFileName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(directoryName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    restoreInfo.failError = error;
                  }
                  continue;
                }
              }

              /* set file time, file owner/group */
              if (!jobOptions->dryRunFlag)
              {
                if (jobOptions->owner.userId  != FILE_DEFAULT_USER_ID ) fileInfo.userId  = jobOptions->owner.userId;
                if (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID) fileInfo.groupId = jobOptions->owner.groupId;
                error = File_setFileInfo(destinationFileName,&fileInfo);
                if (error != ERROR_NONE)
                {
                  if (jobOptions->stopOnErrorFlag)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot set directory info of '%s' (error: %s)\n",
                               String_cString(destinationFileName),
                               Errors_getText(error)
                              );
                    String_delete(destinationFileName);
                    Archive_closeEntry(&archiveEntryInfo);
                    String_delete(directoryName);
                    if (jobOptions->stopOnErrorFlag)
                    {
                      restoreInfo.failError = error;
                    }
                    continue;
                  }
                  else
                  {
                    printWarning("Cannot set directory info of '%s' (error: %s)\n",
                                 String_cString(destinationFileName),
                                 Errors_getText(error)
                                );
                  }
                }
              }

              if (!jobOptions->dryRunFlag)
              {
                printInfo(2,"ok\n");
              }
              else
              {
                printInfo(2,"ok (dry-run)\n");
              }

              /* check if all data read */
              if (!Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of directory entry '%S'.\n",directoryName);
              }

              /* free resources */
              String_delete(destinationFileName);
            }
            else
            {
              /* skip */
              printInfo(3,"  Restore '%s'...skipped\n",String_cString(directoryName));
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
            String   destinationFileName;
            String   parentDirectoryName;
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
                         String_cString(printableArchiveName),
                         Errors_getText(error)
                        );
              String_delete(fileName);
              String_delete(linkName);
              if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,linkName,PATTERN_MATCH_MODE_EXACT))
                && ((excludePatternList == NULL) || !PatternList_match(excludePatternList,linkName,PATTERN_MATCH_MODE_EXACT))
               )
            {
              String_set(restoreInfo.statusInfo.name,linkName);
              restoreInfo.statusInfo.entryDoneBytes  = 0LL;
              restoreInfo.statusInfo.entryTotalBytes = 00L;
              abortFlag = !updateStatusInfo(&restoreInfo);

              /* get destination filename */
              destinationFileName = getDestinationFileName(String_new(),
                                                           linkName,
                                                           jobOptions->destination,
                                                           jobOptions->directoryStripCount
                                                          );

              /* create parent directories if not existing */
              if (!jobOptions->dryRunFlag)
              {
                parentDirectoryName = File_getFilePathName(String_new(),destinationFileName);
                if (!String_empty(parentDirectoryName) && !File_exists(parentDirectoryName))
                {
                  /* create directory */
                  error = File_makeDirectory(parentDirectoryName,
                                             FILE_DEFAULT_USER_ID,
                                             FILE_DEFAULT_GROUP_ID,
                                             FILE_DEFAULT_PERMISSION
                                            );
                  if (error != ERROR_NONE)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot create directory '%s' (error: %s)\n",
                               String_cString(parentDirectoryName),
                               Errors_getText(error)
                              );
                    String_delete(parentDirectoryName);
                    String_delete(destinationFileName);
                    Archive_closeEntry(&archiveEntryInfo);
                    String_delete(fileName);
                    String_delete(linkName);
                    if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                    continue;
                  }

                  /* set directory owner ship */
                  error = File_setOwner(parentDirectoryName,
                                        (jobOptions->owner.userId != FILE_DEFAULT_USER_ID)?jobOptions->owner.userId:fileInfo.userId,
                                        (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID)?jobOptions->owner.groupId:fileInfo.groupId
                                       );
                  if (error != ERROR_NONE)
                  {
                    if (jobOptions->stopOnErrorFlag)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot set owner ship of directory '%s' (error: %s)\n",
                                 String_cString(parentDirectoryName),
                                 Errors_getText(error)
                                );
                      String_delete(parentDirectoryName);
                      String_delete(destinationFileName);
                      Archive_closeEntry(&archiveEntryInfo);
                      String_delete(fileName);
                      if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                      continue;
                    }
                    else
                    {
                      printWarning("Cannot set owner ship of directory '%s' (error: %s)\n",
                                   String_cString(parentDirectoryName),
                                   Errors_getText(error)
                                  );
                    }
                  }
                }
                String_delete(parentDirectoryName);
              }

              /* check if link areadly exists */
              if (!jobOptions->overwriteFilesFlag && File_exists(destinationFileName))
              {
                printInfo(1,
                          "  Restore link '%s'...skipped (file exists)\n",
                          String_cString(destinationFileName)
                         );
                String_delete(destinationFileName);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                String_delete(linkName);
                if (jobOptions->stopOnErrorFlag)
                {
                  restoreInfo.failError = ERRORX(FILE_EXISTS,0,String_cString(destinationFileName));
                }
                continue;
              }

              printInfo(2,"  Restore link '%s'...",String_cString(destinationFileName));

              /* create link */
              if (!jobOptions->dryRunFlag)
              {
                error = File_makeLink(destinationFileName,fileName);
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot create link '%s' -> '%s' (error: %s)\n",
                             String_cString(destinationFileName),
                             String_cString(fileName),
                             Errors_getText(error)
                            );
                  String_delete(destinationFileName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  String_delete(linkName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    restoreInfo.failError = error;
                  }
                  continue;
                }
              }

              /* set file time, file owner/group */
              if (!jobOptions->dryRunFlag)
              {
                if (jobOptions->owner.userId  != FILE_DEFAULT_USER_ID ) fileInfo.userId  = jobOptions->owner.userId;
                if (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID) fileInfo.groupId = jobOptions->owner.groupId;
                error = File_setFileInfo(destinationFileName,&fileInfo);
                if (error != ERROR_NONE)
                {
                  if (jobOptions->stopOnErrorFlag)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot set file info of '%s' (error: %s)\n",
                               String_cString(destinationFileName),
                               Errors_getText(error)
                              );
                    String_delete(destinationFileName);
                    Archive_closeEntry(&archiveEntryInfo);
                    String_delete(fileName);
                    String_delete(linkName);
                    if (jobOptions->stopOnErrorFlag)
                    {
                      restoreInfo.failError = error;
                    }
                    continue;
                  }
                  else
                  {
                    printWarning("Cannot set file info of '%s' (error: %s)\n",
                                 String_cString(destinationFileName),
                                 Errors_getText(error)
                                );
                  }
                }
              }

              if (!jobOptions->dryRunFlag)
              {
                printInfo(2,"ok\n");
              }
              else
              {
                printInfo(2,"ok (dry-run)\n");
              }

              /* check if all data read */
              if (!Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of link entry '%S'.\n",linkName);
              }

              /* free resources */
              String_delete(destinationFileName);
            }
            else
            {
              /* skip */
              printInfo(3,"  Restore '%s'...skipped\n",String_cString(linkName));
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
            String           hardLinkFileName;
            String           destinationFileName;
            bool             restoredDataFlag;
            const StringNode *stringNode;
            String           fileName;
            FragmentNode     *fragmentNode;
            String           parentDirectoryName;
//            FileInfo         localFileInfo;
            FileHandle       fileHandle;
            uint64           length;
            ulong            n;

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
                         String_cString(printableArchiveName),
                         Errors_getText(error)
                        );
              StringList_done(&fileNameList);
              if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
              continue;
            }

            hardLinkFileName    = String_new();
            destinationFileName = String_new();
            restoredDataFlag    = FALSE;
            STRINGLIST_ITERATE(&fileNameList,stringNode,fileName)
            {
              if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,fileName,PATTERN_MATCH_MODE_EXACT))
                  && ((excludePatternList == NULL) || !PatternList_match(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
                 )
              {
                String_set(restoreInfo.statusInfo.name,fileName);
                restoreInfo.statusInfo.entryDoneBytes  = 0LL;
                restoreInfo.statusInfo.entryTotalBytes = fragmentSize;
                abortFlag = !updateStatusInfo(&restoreInfo);

                /* get destination filename */
                getDestinationFileName(destinationFileName,
                                       fileName,
                                       jobOptions->destination,
                                       jobOptions->directoryStripCount
                                      );

                printInfo(2,"  Restore hard link '%s'...",String_cString(destinationFileName));

                /* create parent directories if not existing */
                if (!jobOptions->dryRunFlag)
                {
                  parentDirectoryName = File_getFilePathName(String_new(),destinationFileName);
                  if (!String_empty(parentDirectoryName) && !File_exists(parentDirectoryName))
                  {
                    /* create directory */
                    error = File_makeDirectory(parentDirectoryName,
                                               FILE_DEFAULT_USER_ID,
                                               FILE_DEFAULT_GROUP_ID,
                                               FILE_DEFAULT_PERMISSION
                                              );
                    if (error != ERROR_NONE)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot create directory '%s' (error: %s)\n",
                                 String_cString(parentDirectoryName),
                                 Errors_getText(error)
                                );
                      String_delete(parentDirectoryName);
                      if (jobOptions->stopOnErrorFlag)
                      {
                        restoreInfo.failError = error;
                        break;
                      }
                      else
                      {
                        continue;
                      }
                    }

                    /* set directory owner ship */
                    error = File_setOwner(parentDirectoryName,
                                          (jobOptions->owner.userId != FILE_DEFAULT_USER_ID)?jobOptions->owner.userId:fileInfo.userId,
                                          (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID)?jobOptions->owner.groupId:fileInfo.groupId
                                         );
                    if (error != ERROR_NONE)
                    {
                      if (jobOptions->stopOnErrorFlag)
                      {
                        printInfo(2,"FAIL!\n");
                        printError("Cannot set owner ship of directory '%s' (error: %s)\n",
                                   String_cString(parentDirectoryName),
                                   Errors_getText(error)
                                  );
                        String_delete(parentDirectoryName);
                        restoreInfo.failError = error;
                        break;
                      }
                      else
                      {
                        printWarning("Cannot set owner ship of directory '%s' (error: %s)\n",
                                     String_cString(parentDirectoryName),
                                     Errors_getText(error)
                                    );
                      }
                    }
                  }
                  String_delete(parentDirectoryName);
                }

                if (!restoredDataFlag)
                {
                  /* check if file fragment already eixsts, file already exists */
                  fragmentNode = FragmentList_find(&fragmentList,fileName);
                  if (fragmentNode != NULL)
                  {
                    if (!jobOptions->overwriteFilesFlag && FragmentList_checkEntryExists(fragmentNode,fragmentOffset,fragmentSize))
                    {
                      printInfo(2,"skipped (file part %llu..%llu exists)\n",
                                String_cString(destinationFileName),
                                fragmentOffset,
                                (fragmentSize > 0LL)?fragmentOffset+fragmentSize-1:fragmentOffset
                               );
                      continue;
                    }
                  }
                  else
                  {
                    if (!jobOptions->overwriteFilesFlag && File_exists(destinationFileName))
                    {
                      printInfo(2,"skipped (file exists)\n",String_cString(destinationFileName));
                      continue;
                    }
                    fragmentNode = FragmentList_add(&fragmentList,fileName,fileInfo.size,&fileInfo,sizeof(FileInfo));
                  }

                  /* create file */
                  if (!jobOptions->dryRunFlag)
                  {
                    /* open file */
                    error = File_open(&fileHandle,destinationFileName,FILE_OPENMODE_WRITE);
                    if (error != ERROR_NONE)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot create/write to file '%s' (error: %s)\n",
                                 String_cString(destinationFileName),
                                 Errors_getText(error)
                                );
                      if (jobOptions->stopOnErrorFlag)
                      {
                        restoreInfo.failError = error;
                        break;
                      }
                      else
                      {
                        continue;
                      }
                    }

                    /* seek to fragment position */
                    error = File_seek(&fileHandle,fragmentOffset);
                    if (error != ERROR_NONE)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot write file '%s' (error: %s)\n",
                                 String_cString(destinationFileName),
                                 Errors_getText(error)
                                );
                      File_close(&fileHandle);
                      if (jobOptions->stopOnErrorFlag)
                      {
                        restoreInfo.failError = error;
                        break;
                      }
                      else
                      {
                        continue;
                      }
                    }
                    String_set(hardLinkFileName,destinationFileName);
                  }

                  /* write file data */
                  length = 0;
                  while (   ((restoreInfo.requestedAbortFlag == NULL) || !(*restoreInfo.requestedAbortFlag))
                         && (length < fragmentSize)
                        )
                  {
                    /* pause */
                    while ((restoreInfo.pauseFlag != NULL) && (*restoreInfo.pauseFlag))
                    {
                      Misc_udelay(500*1000);
                    }

                    n = MIN(fragmentSize-length,BUFFER_SIZE);

                    error = Archive_readData(&archiveEntryInfo,buffer,n);
                    if (error != ERROR_NONE)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot read content of archive '%s' (error: %s)!\n",
                                 String_cString(printableArchiveName),
                                 Errors_getText(error)
                                );
                      restoreInfo.failError = error;
                      break;
                    }
                    if (!jobOptions->dryRunFlag)
                    {
                      error = File_write(&fileHandle,buffer,n);
                      if (error != ERROR_NONE)
                      {
                        printInfo(2,"FAIL!\n");
                        printError("Cannot write file '%s' (error: %s)\n",
                                   String_cString(destinationFileName),
                                   Errors_getText(error)
                                  );
                        if (jobOptions->stopOnErrorFlag)
                        {
                          restoreInfo.failError = error;
                        }
                        break;
                      }
                    }
                    restoreInfo.statusInfo.entryDoneBytes += n;
                    abortFlag = !updateStatusInfo(&restoreInfo);

                    length += n;
                  }
                  if      (restoreInfo.failError != ERROR_NONE)
                  {
                    if (!jobOptions->dryRunFlag)
                    {
                      File_close(&fileHandle);
                      break;
                    }
                  }
                  else if ((restoreInfo.requestedAbortFlag != NULL) && (*restoreInfo.requestedAbortFlag))
                  {
                    printInfo(2,"ABORTED\n");
                    File_close(&fileHandle);
                    break;
                  }

                  /* add fragment to file fragment list */
                  FragmentList_addEntry(fragmentNode,fragmentOffset,fragmentSize);
//FragmentList_debugPrintInfo(fragmentNode,String_cString(fileName));

                  /* set file size */
                  if (!jobOptions->dryRunFlag)
                  {
                    if (File_getSize(&fileHandle) > fileInfo.size)
                    {
                      File_truncate(&fileHandle,fileInfo.size);
                    }
                  }

                  /* close file */
                  if (!jobOptions->dryRunFlag)
                  {
                    File_close(&fileHandle);
                  }

                  /* set file time, file owner/group */
                  if (!jobOptions->dryRunFlag)
                  {
                    if (jobOptions->owner.userId  != FILE_DEFAULT_USER_ID ) fileInfo.userId  = jobOptions->owner.userId;
                    if (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID) fileInfo.groupId = jobOptions->owner.groupId;
                    error = File_setFileInfo(destinationFileName,&fileInfo);
                    if (error != ERROR_NONE)
                    {
                      if (jobOptions->stopOnErrorFlag)
                      {
                        printInfo(2,"FAIL!\n");
                        printError("Cannot set file info of '%s' (error: %s)\n",
                                   String_cString(destinationFileName),
                                   Errors_getText(error)
                                  );
                        restoreInfo.failError = error;
                        break;
                      }
                      else
                      {
                        printWarning("Cannot set file info of '%s' (error: %s)\n",
                                     String_cString(destinationFileName),
                                     Errors_getText(error)
                                    );
                      }
                    }
                  }

                  if (!jobOptions->dryRunFlag)
                  {
                    printInfo(2,"ok\n");
                  }
                  else
                  {
                    printInfo(2,"ok (dry-run)\n");
                  }

                  /* discard fragment list if file is complete */
                  if (FragmentList_checkEntryComplete(fragmentNode))
                  {
                    FragmentList_discard(&fragmentList,fragmentNode);
                  }

                  restoredDataFlag = TRUE;
                }
                else
                {
                  /* check file if exists */
                  if (!jobOptions->overwriteFilesFlag && File_exists(destinationFileName))
                  {
                    printInfo(2,"skipped (file exists)\n",String_cString(destinationFileName));
                    continue;
                  }

                  /* create hard link */
                  if (!jobOptions->dryRunFlag)
                  {
                    error = File_makeHardLink(destinationFileName,hardLinkFileName);
                    if (error != ERROR_NONE)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot create/write to file '%s' (error: %s)\n",
                                 String_cString(destinationFileName),
                                 Errors_getText(error)
                                );
                      if (jobOptions->stopOnErrorFlag)
                      {
                        restoreInfo.failError = error;
                        break;
                      }
                      else
                      {
                        continue;
                      }
                    }
                  }

                  if (!jobOptions->dryRunFlag)
                  {
                    printInfo(2,"ok\n");
                  }
                  else
                  {
                    printInfo(2,"ok (dry-run)\n");
                  }

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
                }
              }
              else
              {
                /* skip */
                printInfo(3,"  Restore '%s'...skipped\n",String_cString(fileName));
              }
            }
            String_delete(destinationFileName);
            String_delete(hardLinkFileName);
            if (restoreInfo.failError != ERROR_NONE)
            {
              Archive_closeEntry(&archiveEntryInfo);
              StringList_done(&fileNameList);
              continue;
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
            String   destinationFileName;
            String   parentDirectoryName;
//            FileInfo localFileInfo;

            /* read special device */
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
                         String_cString(printableArchiveName),
                         Errors_getText(error)
                        );
              String_delete(fileName);
              if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
              break;
            }

            if (   (List_empty(includeEntryList) || EntryList_match(includeEntryList,fileName,PATTERN_MATCH_MODE_EXACT))
                && ((excludePatternList == NULL) || !PatternList_match(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
               )
            {
              String_set(restoreInfo.statusInfo.name,fileName);
              restoreInfo.statusInfo.entryDoneBytes  = 0LL;
              restoreInfo.statusInfo.entryTotalBytes = 00L;
              abortFlag = !updateStatusInfo(&restoreInfo);

              /* get destination filename */
              destinationFileName = getDestinationFileName(String_new(),
                                                           fileName,
                                                           jobOptions->destination,
                                                           jobOptions->directoryStripCount
                                                          );

              /* create parent directories if not existing */
              if (!jobOptions->dryRunFlag)
              {
                parentDirectoryName = File_getFilePathName(String_new(),destinationFileName);
                if (!String_empty(parentDirectoryName) && !File_exists(parentDirectoryName))
                {
                  /* create directory */
                  error = File_makeDirectory(parentDirectoryName,
                                             FILE_DEFAULT_USER_ID,
                                             FILE_DEFAULT_GROUP_ID,
                                             FILE_DEFAULT_PERMISSION
                                            );
                  if (error != ERROR_NONE)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot create directory '%s' (error: %s)\n",
                               String_cString(parentDirectoryName),
                               Errors_getText(error)
                              );
                    String_delete(parentDirectoryName);
                    String_delete(destinationFileName);
                    Archive_closeEntry(&archiveEntryInfo);
                    String_delete(fileName);
                    if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                    continue;
                  }

                  /* set directory owner ship */
                  error = File_setOwner(parentDirectoryName,
                                        (jobOptions->owner.userId != FILE_DEFAULT_USER_ID)?jobOptions->owner.userId:fileInfo.userId,
                                        (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID)?jobOptions->owner.groupId:fileInfo.groupId
                                       );
                  if (error != ERROR_NONE)
                  {
                    if (jobOptions->stopOnErrorFlag)
                    {
                      printInfo(2,"FAIL!\n");
                      printError("Cannot set owner ship of directory '%s' (error: %s)\n",
                                 String_cString(parentDirectoryName),
                                 Errors_getText(error)
                                );
                      String_delete(parentDirectoryName);
                      String_delete(destinationFileName);
                      Archive_closeEntry(&archiveEntryInfo);
                      String_delete(fileName);
                      if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = error;
                      continue;
                    }
                    else
                    {
                      printWarning("Cannot set owner ship of directory '%s' (error: %s)\n",
                                   String_cString(parentDirectoryName),
                                   Errors_getText(error)
                                  );
                    }
                  }
                }
                String_delete(parentDirectoryName);
              }

              /* check if special file already exists */
              if (!jobOptions->overwriteFilesFlag && File_exists(destinationFileName))
              {
                printInfo(1,
                          "  Restore special device '%s'...skipped (file exists)\n",
                          String_cString(destinationFileName)
                         );
                String_delete(destinationFileName);
                Archive_closeEntry(&archiveEntryInfo);
                String_delete(fileName);
                if (jobOptions->stopOnErrorFlag)
                {
                  restoreInfo.failError = ERRORX(FILE_EXISTS,0,String_cString(destinationFileName));
                }
                continue;
              }

              printInfo(2,"  Restore special device '%s'...",String_cString(destinationFileName));

              /* create special device */
              if (!jobOptions->dryRunFlag)
              {
                error = File_makeSpecial(destinationFileName,
                                         fileInfo.specialType,
                                         fileInfo.major,
                                         fileInfo.minor
                                        );
                if (error != ERROR_NONE)
                {
                  printInfo(2,"FAIL!\n");
                  printError("Cannot create special device '%s' (error: %s)\n",
                             String_cString(fileName),
                             Errors_getText(error)
                            );
                  String_delete(destinationFileName);
                  Archive_closeEntry(&archiveEntryInfo);
                  String_delete(fileName);
                  if (jobOptions->stopOnErrorFlag)
                  {
                    restoreInfo.failError = error;
                  }
                  continue;
                }
              }

              /* set file time, file owner/group */
              if (!jobOptions->dryRunFlag)
              {
                if (jobOptions->owner.userId  != FILE_DEFAULT_USER_ID ) fileInfo.userId  = jobOptions->owner.userId;
                if (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID) fileInfo.groupId = jobOptions->owner.groupId;
                error = File_setFileInfo(destinationFileName,&fileInfo);
                if (error != ERROR_NONE)
                {
                  if (jobOptions->stopOnErrorFlag)
                  {
                    printInfo(2,"FAIL!\n");
                    printError("Cannot set file info of '%s' (error: %s)\n",
                               String_cString(destinationFileName),
                               Errors_getText(error)
                              );
                    String_delete(destinationFileName);
                    Archive_closeEntry(&archiveEntryInfo);
                    String_delete(fileName);
                    if (jobOptions->stopOnErrorFlag)
                    {
                      restoreInfo.failError = error;
                    }
                    continue;
                  }
                  else
                  {
                    printWarning("Cannot set file info of '%s' (error: %s)\n",
                                 String_cString(destinationFileName),
                                 Errors_getText(error)
                                );
                  }
                }
              }

              if (!jobOptions->dryRunFlag)
              {
                printInfo(2,"ok\n");
              }
              else
              {
                printInfo(2,"ok (dry-run)\n");
              }

              /* check if all data read */
              if (!Archive_eofData(&archiveEntryInfo))
              {
                printWarning("unexpected data at end of special entry '%S'.\n",fileName);
              }

              /* free resources */
              String_delete(destinationFileName);
            }
            else
            {
              /* skip */
              printInfo(3,"  Restore '%s'...skipped\n",String_cString(fileName));
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

  /* check fragment lists, set file info for incomplete entries */
  if ((restoreInfo.requestedAbortFlag == NULL) || !(*restoreInfo.requestedAbortFlag))
  {
    FRAGMENTLIST_ITERATE(&fragmentList,fragmentNode)
    {
      if (!FragmentList_checkEntryComplete(fragmentNode))
      {
        printInfo(0,"Warning: incomplete entry '%s'\n",String_cString(fragmentNode->name));
        if (restoreInfo.failError == ERROR_NONE) restoreInfo.failError = ERROR_FILE_INCOMPLETE;
      }

      if (fragmentNode->userData != NULL)
      {
        /* set file time, file owner/group, file permission */
        if (!jobOptions->dryRunFlag)
        {
          if (jobOptions->owner.userId  != FILE_DEFAULT_USER_ID ) ((FileInfo*)fragmentNode->userData)->userId  = jobOptions->owner.userId;
          if (jobOptions->owner.groupId != FILE_DEFAULT_GROUP_ID) ((FileInfo*)fragmentNode->userData)->groupId = jobOptions->owner.groupId;
          error = File_setFileInfo(fragmentNode->name,(FileInfo*)fragmentNode->userData);
          if (error != ERROR_NONE)
          {
            if (jobOptions->stopOnErrorFlag)
            {
              printError("Cannot set file info of '%s' (error: %s)\n",
                         String_cString(fragmentNode->name),
                         Errors_getText(error)
                        );
              restoreInfo.failError = error;
            }
            else
            {
              printWarning("Cannot set file info of '%s' (error: %s)\n",
                           String_cString(fragmentNode->name),
                           Errors_getText(error)
                          );
            }
          }
        }
      }
    }
  }

  /* free resources */
  String_delete(printableArchiveName);
  String_delete(archiveName);
  FragmentList_done(&fragmentList);
  free(buffer);
  String_delete(restoreInfo.statusInfo.name);
  String_delete(restoreInfo.statusInfo.storageName);

  if ((restoreInfo.requestedAbortFlag == NULL) || !(*restoreInfo.requestedAbortFlag))
  {
    return restoreInfo.failError;
  }
  else
  {
    return ERROR_ABORTED;
  }
}

#ifdef __cplusplus
  }
#endif

/* end of file */
