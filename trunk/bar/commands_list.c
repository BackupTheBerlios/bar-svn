/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/commands_list.c,v $
* $Revision: 1.34 $
* $Author: torsten $
* Contents: Backup ARchiver archive list function
* Systems: all
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
#include "arrays.h"

#include "bar.h"
#include "errors.h"
#include "misc.h"
#include "patterns.h"
#include "files.h"
#include "archive.h"
#include "storage.h"
#include "network.h"
#include "server.h"

#include "commands_list.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

typedef struct SSHSocketNode
{
  LIST_NODE_HEADER(struct SSHSocketNode);

  String       name;
  SocketHandle socketHandle;
} SSHSocketNode;

typedef struct
{
  LIST_HEADER(SSHSocketNode);
} SSHSocketList;

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : printHeader
* Purpose: print list header
* Input  : archiveFileName - archive file name
*          
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void printHeader(const String archiveFileName)
{
  printInfo(0,"List archive '%s':\n",String_cString(archiveFileName));
  printInfo(0,"\n");
  if (globalOptions.longFormatFlag)
  {
    printInfo(0,
              "%4s %-10s %-25s %-22s %-10s %-7s %-10s %s\n",
              "Type",
              "Size",
              "Date/Time",
              "Part",
              "Compress",
              "Ratio %",
              "Crypt",
              "Name"
             );
  }
  else
  {
    printInfo(0,
              "%4s %-10s %-25s %s\n",
              "Type",
              "Size",
              "Date/Time",
              "Name"
             );
  }
  printInfo(0,"--------------------------------------------------------------------------------------------------------------\n");
}

/***********************************************************************\
* Name   : printFooter
* Purpose: print list footer
* Input  : fileCount - number of files listed
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void printFooter(ulong fileCount)
{
  printInfo(0,"--------------------------------------------------------------------------------------------------------------\n");
  printInfo(0,"%lu file(s)\n",fileCount);
  printInfo(0,"\n");
}

/***********************************************************************\
* Name   : printFileInfo
* Purpose: print file information
* Input  : fileName          - file name
*          fileSize          - file size [bytes]
*          archiveFileSize   - archive size [bytes]
*          compressAlgorithm - used compress algorithm
*          cryptAlgorithm    - used crypt algorithm
*          cryptType         - crypt type; see CRYPT_TYPES
*          fragmentOffset    - fragment offset (0..n-1)
*          fragmentSize      - fragment length
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void printFileInfo(const String       fileName,
                         uint64             fileSize,
                         uint64             timeModified,
                         uint64             archiveFileSize,
                         CompressAlgorithms compressAlgorithm,
                         CryptAlgorithms    cryptAlgorithm,
                         CryptTypes         cryptType,
                         uint64             fragmentOffset,
                         uint64             fragmentSize
                        )
{
  String dateTime;
  double ratio;
  String cryptString;

  assert(fileName != NULL);

  dateTime = Misc_formatDateTime(String_new(),timeModified,NULL);

  if ((compressAlgorithm != COMPRESS_ALGORITHM_NONE) && (fragmentSize > 0))
  {
    ratio = 100.0-archiveFileSize*100.0/fragmentSize;
  }
  else
  {
    ratio = 0;
  }

  if (globalOptions.longFormatFlag)
  {
    cryptString = String_format(String_new(),"%s%c",Crypt_getAlgorithmName(cryptAlgorithm),(cryptType==CRYPT_TYPE_ASYMMETRIC)?'*':' ');
    printf("FILE %10llu %-25s %10llu..%10llu %-10s %6.1f%% %-10s %s\n",
           fileSize,
           String_cString(dateTime),
           fragmentOffset,
           (fragmentSize > 0)?fragmentOffset+fragmentSize-1:fragmentOffset,
           Compress_getAlgorithmName(compressAlgorithm),
           ratio,
           String_cString(cryptString),
           String_cString(fileName)
          );
    String_delete(cryptString);
  }
  else
  {
    printf("FILE %10llu %-25s %s\n",
           fileSize,
           String_cString(dateTime),
           String_cString(fileName)
          );
  }

  String_delete(dateTime);
}

/***********************************************************************\
* Name   : printDirectoryInfo
* Purpose: print link information
* Input  : directoryName  - directory name
*          cryptAlgorithm - used crypt algorithm
*          cryptType      - crypt type; see CRYPT_TYPES
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void printDirectoryInfo(const String    directoryName,
                              CryptAlgorithms cryptAlgorithm,
                              CryptTypes      cryptType
                             )
{
  String cryptString;

  assert(directoryName != NULL);

  if (globalOptions.longFormatFlag)
  {
    cryptString = String_format(String_new(),"%s%c",Crypt_getAlgorithmName(cryptAlgorithm),(cryptType==CRYPT_TYPE_ASYMMETRIC)?'*':' ');
    printf("DIR                                                                                 %-10s %s\n",
           String_cString(cryptString),
           String_cString(directoryName)
          );
    String_delete(cryptString);
  }
  else
  {
    printf("DIR                                       %s\n",
           String_cString(directoryName)
          );
  }
}

/***********************************************************************\
* Name   : printLinkInfo
* Purpose: print link information
* Input  : linkName        - link name
*          destinationName - name of referenced file
*          cryptAlgorithm  - used crypt algorithm
*          cryptType       - crypt type; see CRYPT_TYPES
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void printLinkInfo(const String    linkName,
                         const String    destinationName,
                         CryptAlgorithms cryptAlgorithm,
                         CryptTypes      cryptType
                        )
{
  String cryptString;

  assert(linkName != NULL);
  assert(destinationName != NULL);

  if (globalOptions.longFormatFlag)
  {
    cryptString = String_format(String_new(),"%s%c",Crypt_getAlgorithmName(cryptAlgorithm),(cryptType==CRYPT_TYPE_ASYMMETRIC)?'*':' ');
    printf("LINK                                                                                %-10s %s -> %s\n",
           String_cString(cryptString),
           String_cString(linkName),
           String_cString(destinationName)
          );
    String_delete(cryptString);
  }
  else
  {
    printf("LINK                                      %s -> %s\n",
           String_cString(linkName),
           String_cString(destinationName)
          );
  }
}

/***********************************************************************\
* Name   : printSpecialInfo
* Purpose: print special information
* Input  : fileName        - file name
*          cryptAlgorithm  - used crypt algorithm
*          cryptType       - crypt type; see CRYPT_TYPES
*          fileSpecialType - special file type
*          major           - device major number
*          minor           - device minor number
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void printSpecialInfo(const String     fileName,
                            CryptAlgorithms  cryptAlgorithm,
                            CryptTypes       cryptType,
                            FileSpecialTypes fileSpecialType,
                            ulong            major,
                            ulong            minor
                           )
{
  String cryptString;

  assert(fileName != NULL);

  switch (fileSpecialType)
  {
    case FILE_SPECIAL_TYPE_CHARACTER_DEVICE:
      if (globalOptions.longFormatFlag)
      {
        cryptString = String_format(String_new(),"%s%c",Crypt_getAlgorithmName(cryptAlgorithm),(cryptType==CRYPT_TYPE_ASYMMETRIC)?'*':' ');
        printf("CHAR                                                                                %-10s %s, %lu %lu\n",
               String_cString(cryptString),
               String_cString(fileName),
               major,
               minor
              );
        String_delete(cryptString);
      }
      else
      {
        printf("CHAR                                      %s\n",
               String_cString(fileName)
              );
      }
      break;
    case FILE_SPECIAL_TYPE_BLOCK_DEVICE:
      if (globalOptions.longFormatFlag)
      {
        cryptString = String_format(String_new(),"%s%c",Crypt_getAlgorithmName(cryptAlgorithm),(cryptType==CRYPT_TYPE_ASYMMETRIC)?'*':' ');
        printf("BLOCK                                                                               %-10s %s, %lu %lu\n",
               String_cString(cryptString),
               String_cString(fileName),
               major,
               minor
              );
        String_delete(cryptString);
      }
      else
      {
        printf("BLOCK                                     %s\n",
               String_cString(fileName)
              );
      }
      break;
    case FILE_SPECIAL_TYPE_FIFO:
      if (globalOptions.longFormatFlag)
      {
        cryptString = String_format(String_new(),"%s%c",Crypt_getAlgorithmName(cryptAlgorithm),(cryptType==CRYPT_TYPE_ASYMMETRIC)?'*':' ');
        printf("FIFO                                                                                %-10s %s\n",
               String_cString(cryptString),
               String_cString(fileName)
              );
        String_delete(cryptString);
      }
      else
      {
        printf("FIFO                                      %s\n",
               String_cString(fileName)
              );
      }
      break;
    case FILE_SPECIAL_TYPE_SOCKET:
      if (globalOptions.longFormatFlag)
      {
        cryptString = String_format(String_new(),"%s%c",Crypt_getAlgorithmName(cryptAlgorithm),(cryptType==CRYPT_TYPE_ASYMMETRIC)?'*':' ');
        printf("SOCKET                                                                              %-10s %s\n",
               String_cString(cryptString),
               String_cString(fileName)
              );
        String_delete(cryptString);
      }
      else
      {
        printf("SOCKET                                    %s\n",
               String_cString(fileName)
              );
      }
      break;
    default:
      #ifndef NDEBUG
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
      #endif /* NDEBUG */
      break; /* not reached */
  }
}

/*---------------------------------------------------------------------*/

Errors Command_list(StringList  *archiveFileNameList,
                    PatternList *includePatternList,
                    PatternList *excludePatternList,
                    JobOptions  *jobOptions
                   )
{
  String       archiveFileName;
  bool         printedInfoFlag;
  ulong        fileCount;
  String       storageSpecifier;
  Errors       failError;
  bool         retryFlag;
bool         remoteBarFlag;
//  SSHSocketList sshSocketList;
//  SSHSocketNode *sshSocketNode;
  SocketHandle  socketHandle;

  assert(archiveFileNameList != NULL);
  assert(includePatternList != NULL);
  assert(excludePatternList != NULL);
  assert(jobOptions != NULL);

remoteBarFlag=FALSE;

  /* init variables */
  archiveFileName  = String_new();
  storageSpecifier = String_new();

  /* list archive content */
  failError = ERROR_NONE;
  while (!StringList_empty(archiveFileNameList))
  {
    StringList_getFirst(archiveFileNameList,archiveFileName);
    printedInfoFlag = FALSE;
    fileCount       = 0;

    switch (Storage_getType(archiveFileName,storageSpecifier))
    {
      case STORAGE_TYPE_FILESYSTEM:
      case STORAGE_TYPE_FTP:
      case STORAGE_TYPE_SCP:
      case STORAGE_TYPE_SFTP:
        {
          Errors          error;
          ArchiveInfo     archiveInfo;
          ArchiveFileInfo archiveFileInfo;
          FileTypes       fileType;

          /* open archive */
          error = Archive_open(&archiveInfo,
                               archiveFileName,
                               jobOptions,
                               PASSWORD_MODE_DEFAULT
                              );
          if (error != ERROR_NONE)
          {
            printError("Cannot open file '%s' (error: %s)!\n",
                       String_cString(archiveFileName),
                       getErrorText(error)
                      );
            if (failError == ERROR_NONE) failError = error;
            continue;
          }

          /* list contents */
          while (   !Archive_eof(&archiveInfo)
                 && (failError == ERROR_NONE)
                )
          {
            /* get next file type */
            error = Archive_getNextFileType(&archiveInfo,
                                            &archiveFileInfo,
                                            &fileType
                                           );
            if (error != ERROR_NONE)
            {
              printError("Cannot not read next entry in archive '%s' (error: %s)!\n",
                         String_cString(archiveFileName),
                         getErrorText(error)
                        );
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            switch (fileType)
            {
              case FILE_TYPE_FILE:
                {
                  ArchiveFileInfo    archiveFileInfo;
                  CompressAlgorithms compressAlgorithm;
                  CryptAlgorithms    cryptAlgorithm;
                  CryptTypes         cryptType;
                  String             fileName;
                  FileInfo           fileInfo;
                  uint64             fragmentOffset,fragmentSize;

                  /* open archive file */
                  fileName = String_new();
                  do
                  {
                    error = Archive_readFileEntry(&archiveInfo,
                                                  &archiveFileInfo,
                                                  &compressAlgorithm,
                                                  &cryptAlgorithm,
                                                  &cryptType,
                                                  fileName,
                                                  &fileInfo,
                                                  &fragmentOffset,
                                                  &fragmentSize
                                                 );
                    retryFlag = FALSE;
#if 0
                    if ((error == ERROR_CORRUPT_DATA) && !inputPasswordFlag)
                    {
                      inputCryptPassword(&jobOptions->cryptPassword);
                      retryFlag         = TRUE;
                      inputPasswordFlag = TRUE;
                    }
#endif /* 0 */
                  }
                  while ((error != ERROR_NONE) && retryFlag);
                  if (error != ERROR_NONE)
                  {
                    printError("Cannot not read 'file' content of archive '%s' (error: %s)!\n",
                               String_cString(archiveFileName),
                               getErrorText(error)
                              );
                    String_delete(fileName);
                    if (failError == ERROR_NONE) failError = error;
                    break;
                  }

                  if (   (List_empty(includePatternList) || Pattern_matchList(includePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
                      && !Pattern_matchList(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
                     )
                  {
                    if (!printedInfoFlag)
                    {
                      printHeader(archiveFileName);
                      printedInfoFlag = TRUE;
                    }

                    /* output file info */
                    printFileInfo(fileName,
                                  fileInfo.size,
                                  fileInfo.timeModified,
                                  archiveFileInfo.file.chunkInfoFileData.size,
                                  compressAlgorithm,
                                  cryptAlgorithm,
                                  cryptType,
                                  fragmentOffset,
                                  fragmentSize
                                 );
                    fileCount++;
                  }

                  /* close archive file, free resources */
                  Archive_closeEntry(&archiveFileInfo);
                  String_delete(fileName);
                }
                break;
              case FILE_TYPE_DIRECTORY:
                {
                  String          directoryName;
                  CryptAlgorithms cryptAlgorithm;
                  CryptTypes      cryptType;
                  FileInfo        fileInfo;

                  /* open archive lin */
                  directoryName = String_new();
                  do
                  {
                    error = Archive_readDirectoryEntry(&archiveInfo,
                                                       &archiveFileInfo,
                                                       &cryptAlgorithm,
                                                       &cryptType,
                                                       directoryName,
                                                       &fileInfo
                                                      );
                    retryFlag = FALSE;
#if 0
                    if ((error == ERROR_CORRUPT_DATA) && !inputPasswordFlag)
                    {
                      inputCryptPassword(&jobOptions->cryptPassword);
                      retryFlag         = TRUE;
                      inputPasswordFlag = TRUE;
                    }
#endif /* 0 */
                  }
                  while ((error != ERROR_NONE) && retryFlag);
                  if (error != ERROR_NONE)
                  {
                    printError("Cannot not read 'directory' content of archive '%s' (error: %s)!\n",
                               String_cString(archiveFileName),
                               getErrorText(error)
                              );
                    String_delete(directoryName);
                    if (failError == ERROR_NONE) failError = error;
                    break;
                  }

                  if (   (List_empty(includePatternList) || Pattern_matchList(includePatternList,directoryName,PATTERN_MATCH_MODE_EXACT))
                      && !Pattern_matchList(excludePatternList,directoryName,PATTERN_MATCH_MODE_EXACT)
                     )
                  {
                    if (!printedInfoFlag)
                    {
                      printHeader(archiveFileName);
                      printedInfoFlag = TRUE;
                    }

                    /* output file info */
                    printDirectoryInfo(directoryName,
                                       cryptAlgorithm,
                                       cryptType
                                      );
                    fileCount++;
                  }

                  /* close archive file, free resources */
                  Archive_closeEntry(&archiveFileInfo);
                  String_delete(directoryName);
                }
                break;
              case FILE_TYPE_LINK:
                {
                  CryptAlgorithms cryptAlgorithm;
                  CryptTypes      cryptType;
                  String          linkName;
                  String          fileName;
                  FileInfo        fileInfo;

                  /* open archive lin */
                  linkName = String_new();
                  fileName = String_new();
                  do
                  {
                    error = Archive_readLinkEntry(&archiveInfo,
                                                  &archiveFileInfo,
                                                  &cryptAlgorithm,
                                                  &cryptType,
                                                  linkName,
                                                  fileName,
                                                  &fileInfo
                                                 );
                    retryFlag = FALSE;
#if 0
                    if ((error == ERROR_CORRUPT_DATA) && !inputPasswordFlag)
                    {
                      inputCryptPassword(&jobOptions->cryptPassword);
                      retryFlag         = TRUE;
                      inputPasswordFlag = TRUE;
                    }
#endif /* 0 */
                  }
                  while ((error != ERROR_NONE) && retryFlag);
                  if (error != ERROR_NONE)
                  {
                    printError("Cannot not read 'link' content of archive '%s' (error: %s)!\n",
                               String_cString(archiveFileName),
                               getErrorText(error)
                              );
                    String_delete(fileName);
                    String_delete(linkName);
                    if (failError == ERROR_NONE) failError = error;
                    break;
                  }

                  if (   (List_empty(includePatternList) || Pattern_matchList(includePatternList,linkName,PATTERN_MATCH_MODE_EXACT))
                      && !Pattern_matchList(excludePatternList,linkName,PATTERN_MATCH_MODE_EXACT)
                     )
                  {
                    if (!printedInfoFlag)
                    {
                      printHeader(archiveFileName);
                      printedInfoFlag = TRUE;
                    }

                    /* output file info */
                    printLinkInfo(linkName,
                                  fileName,
                                  cryptAlgorithm,
                                  cryptType
                                 );
                    fileCount++;
                  }

                  /* close archive file, free resources */
                  Archive_closeEntry(&archiveFileInfo);
                  String_delete(fileName);
                  String_delete(linkName);
                }
                break;
              case FILE_TYPE_SPECIAL:
                {
                  CryptAlgorithms cryptAlgorithm;
                  CryptTypes      cryptType;
                  String          fileName;
                  FileInfo        fileInfo;

                  /* open archive lin */
                  fileName = String_new();
                  do
                  {
                    error = Archive_readSpecialEntry(&archiveInfo,
                                                     &archiveFileInfo,
                                                     &cryptAlgorithm,
                                                     &cryptType,
                                                     fileName,
                                                     &fileInfo
                                                    );
                    retryFlag = FALSE;
#if 0
                    if ((error == ERROR_CORRUPT_DATA) && !inputPasswordFlag)
                    {
                      inputCryptPassword(&jobOptions->cryptPassword);
                      retryFlag         = TRUE;
                      inputPasswordFlag = TRUE;
                    }
#endif /* 0 */
                  }
                  while ((error != ERROR_NONE) && retryFlag);
                  if (error != ERROR_NONE)
                  {
                    printError("Cannot not read 'special' content of archive '%s' (error: %s)!\n",
                               String_cString(archiveFileName),
                               getErrorText(error)
                              );
                    String_delete(fileName);
                    if (failError == ERROR_NONE) failError = error;
                    break;
                  }

                  if (   (List_empty(includePatternList) || Pattern_matchList(includePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
                      && !Pattern_matchList(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
                     )
                  {
                    if (!printedInfoFlag)
                    {
                      printHeader(archiveFileName);
                      printedInfoFlag = TRUE;
                    }

                    /* output file info */
                    printSpecialInfo(fileName,
                                     cryptAlgorithm,
                                     cryptType,
                                     fileInfo.specialType,
                                     fileInfo.major,
                                     fileInfo.minor
                                    );
                    fileCount++;
                  }

                  /* close archive file, free resources */
                  Archive_closeEntry(&archiveFileInfo);
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
        break;
      case STORAGE_TYPE_SSH:
        {
          String               userName,hostName,hostFileName;
          SSHServer            sshServer;
          NetworkExecuteHandle networkExecuteHandle;
          String               line;
          Errors               error;
          int                  id,errorCode;
          bool                 completedFlag;
          String               fileName,directoryName,linkName;
          uint64               fileSize;
          uint64               timeModified;
          uint64               archiveFileSize;
          uint64               fragmentOffset,fragmentLength;
          CompressAlgorithms   compressAlgorithm;
          CryptAlgorithms      cryptAlgorithm;
          CryptTypes           cryptType;
          int                  exitCode;

          /* parse storage string */
          userName     = String_new();
          hostName     = String_new();
          hostFileName = String_new();
          if (!Storage_parseSSHSpecifier(storageSpecifier,userName,hostName,hostFileName))
          {
            String_delete(hostFileName);
            String_delete(hostName);
            String_delete(userName);
            printError("Cannot not parse storage name '%s'!\n",
                       String_cString(storageSpecifier)
                      );
            if (failError == ERROR_NONE) failError = ERROR_INIT_TLS;
            break;
          }

          /* start remote BAR via SSH (if not already started) */
          if (!remoteBarFlag)
          {
            getSSHServer(hostName,jobOptions,&sshServer);
            error = Network_connect(&socketHandle,
                                    SOCKET_TYPE_SSH,
                                    hostName,
                                    sshServer.port,
                                    userName,
                                    sshServer.password,
                                    sshServer.publicKeyFileName,
                                    sshServer.privateKeyFileName,
                                    0
                                   );
            if (error != ERROR_NONE)
            {
              printError("Cannot not connecto to '%s:%d' (error: %s)!\n",
                         String_cString(hostName),
                         sshServer.port,
                         getErrorText(error)
                        );
              String_delete(hostFileName);
              String_delete(hostName);
              String_delete(userName);
              if (failError == ERROR_NONE) failError = error;
              break;
            }

            remoteBarFlag = TRUE;
          }

          line = String_new();
          fileName      = String_new();
          directoryName = String_new();
          linkName      = String_new();


          /* start remote BAR in batch mode */
          String_format(String_clear(line),"%s --batch",globalOptions.remoteBARExecutable);
          error = Network_execute(&networkExecuteHandle,
                                  &socketHandle,
                                  NETWORK_EXECUTE_IO_MASK_STDOUT|NETWORK_EXECUTE_IO_MASK_STDERR,
                                  String_cString(line)
                                 );
          if (error != ERROR_NONE)
          {
            printError("Cannot not execute remote BAR program '%s' (error: %s)!\n",
                       String_cString(line),
                       getErrorText(error)
                      );
            if (failError == ERROR_NONE) failError = error;
            break;
          }

          do
          {
            /* send list archive command */
            String_format(String_clear(line),"1 SET crypt-password %'s",jobOptions->cryptPassword);
            Network_executeWriteLine(&networkExecuteHandle,line);
            String_format(String_clear(line),"2 ARCHIVE_LIST %S",hostFileName);
            Network_executeWriteLine(&networkExecuteHandle,line);
            Network_executeSendEOF(&networkExecuteHandle);

            /* list contents */
            while (!Network_executeEOF(&networkExecuteHandle,NETWORK_EXECUTE_IO_TYPE_STDOUT,60*1000))
            {
              /* read line */
              Network_executeReadLine(&networkExecuteHandle,NETWORK_EXECUTE_IO_TYPE_STDOUT,line,60*1000);
  //fprintf(stderr,"%s,%d: %s\n",__FILE__,__LINE__,String_cString(line));

              /* parse and output list */
              if      (String_parse(line,
                                    STRING_BEGIN,
                                    "%d %d %y FILE %llu %llu %llu %llu %d %d %d %S",
                                    NULL,
                                    &id,
                                    &errorCode,
                                    &completedFlag,
                                    &fileSize,
                                    &timeModified,
                                    &archiveFileSize,
                                    &fragmentOffset,
                                    &fragmentLength,
                                    &compressAlgorithm,
                                    &cryptAlgorithm,
                                    &cryptType,
                                    fileName
                                   )
                      )
              {
                if (   (List_empty(includePatternList) || Pattern_matchList(includePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
                    && !Pattern_matchList(excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
                   )
                {
                  if (!printedInfoFlag)
                  {
                    printHeader(archiveFileName);
                    printedInfoFlag = TRUE;
                  }

                  /* output file info */
                  printFileInfo(fileName,
                                fileSize,
                                timeModified,
                                archiveFileSize,
                                compressAlgorithm,
                                cryptAlgorithm,
                                cryptType,
                                fragmentOffset,
                                fragmentLength
                               );
                  fileCount++;
                }
              }
              else if (String_parse(line,
                                    STRING_BEGIN,
                                    "%d %d %d DIRECTORY %d %S",
                                    NULL,
                                    &id,
                                    &errorCode,
                                    &completedFlag,
                                    &cryptAlgorithm,
                                    &cryptType,
                                    directoryName
                                   )
                      )
              {
                if (   (List_empty(includePatternList) || Pattern_matchList(includePatternList,directoryName,PATTERN_MATCH_MODE_EXACT))
                    && !Pattern_matchList(excludePatternList,directoryName,PATTERN_MATCH_MODE_EXACT)
                   )
                {
                  if (!printedInfoFlag)
                  {
                    printHeader(archiveFileName);
                    printedInfoFlag = TRUE;
                  }

                  /* output file info */
                  printDirectoryInfo(directoryName,
                                     cryptAlgorithm,
                                     cryptType
                                    );
                  fileCount++;
                }
              }
              else if (String_parse(line,
                                    STRING_BEGIN,
                                    "%d %d %d LINK %d %S %S",
                                    NULL,
                                    &id,
                                    &errorCode,
                                    &completedFlag,
                                    &cryptAlgorithm,
                                    cryptType,
                                    linkName,
                                    fileName
                                   )
                      )
              {
                if (   (List_empty(includePatternList) || Pattern_matchList(includePatternList,linkName,PATTERN_MATCH_MODE_EXACT))
                    && !Pattern_matchList(excludePatternList,linkName,PATTERN_MATCH_MODE_EXACT)
                   )
                {
                  if (!printedInfoFlag)
                  {
                    printHeader(archiveFileName);
                    printedInfoFlag = TRUE;
                  }

                  /* output file info */
                  printLinkInfo(linkName,
                                fileName,
                                cryptAlgorithm,
                                cryptType
                               );
                  fileCount++;
                }
              }
              else
              {
fprintf(stderr,"%s,%d: ERROR %s\n",__FILE__,__LINE__,String_cString(line));
              }
            }
            while (!Network_executeEOF(&networkExecuteHandle,NETWORK_EXECUTE_IO_TYPE_STDERR,60*1000))
            {
Network_executeReadLine(&networkExecuteHandle,NETWORK_EXECUTE_IO_TYPE_STDERR,line,0);
if (String_length(line)>0) fprintf(stderr,"%s,%d: error=%s\n",__FILE__,__LINE__,String_cString(line));
            }

            retryFlag = FALSE;
#if 0
            if ((error == ERROR_CORRUPT_DATA) && !inputPasswordFlag)
            {
              inputCryptPassword(&jobOptions->cryptPassword);
              retryFlag         = TRUE;
              inputPasswordFlag = TRUE;
            }
#endif /* 0 */
          }
          while ((error != ERROR_NONE) && retryFlag);

          exitCode = Network_terminate(&networkExecuteHandle);
          if (exitCode != 0)
          {
            printError("Remote BAR program return exitcode %d!\n",exitCode);
            if (failError == ERROR_NONE) failError = ERROR_NETWORK_EXECUTE_FAIL;
          }

          /* free resources */
          String_delete(linkName);
          String_delete(directoryName);
          String_delete(fileName);
          String_delete(line);
          String_delete(hostFileName);
          String_delete(hostName);
          String_delete(userName);
        }
        break;
      default:
        #ifndef NDEBUG
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        #endif /* NDEBUG */
        break; /* not reached */
    }
    if (printedInfoFlag)
    {
      printFooter(fileCount);
    }
  }

  /* free resources */
  String_delete(storageSpecifier);
  String_delete(archiveFileName);

  return failError;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
