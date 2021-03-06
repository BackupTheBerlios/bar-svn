/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: Backup ARchiver archive create function
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <config.h>  // use <...> to support separated build directory

#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "global.h"
#include "autofree.h"
#include "strings.h"
#include "lists.h"
#include "stringlists.h"
#include "threads.h"
#include "msgqueues.h"
#include "semaphores.h"
#include "dictionaries.h"

#include "errors.h"
#include "patterns.h"
#include "entrylists.h"
#include "patternlists.h"
#include "files.h"
#include "devices.h"
#include "filesystems.h"
#include "archive.h"
#include "sources.h"
#include "crypt.h"
#include "storage.h"
#include "misc.h"
#include "database.h"

#include "commands_create.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

#define MAX_FILE_MSG_QUEUE_ENTRIES    256
#define MAX_STORAGE_MSG_QUEUE_ENTRIES 256

// file data buffer size
#define BUFFER_SIZE                   (64*1024)

#define INCREMENTAL_LIST_FILE_ID      "BAR incremental list"
#define INCREMENTAL_LIST_FILE_VERSION 1

typedef enum
{
  FORMAT_MODE_ARCHIVE_FILE_NAME,
  FORMAT_MODE_PATTERN,
} FormatModes;

typedef enum
{
  INCREMENTAL_FILE_STATE_UNKNOWN,
  INCREMENTAL_FILE_STATE_OK,
  INCREMENTAL_FILE_STATE_ADDED,
} IncrementalFileStates;

/***************************** Datatypes *******************************/

// incremental data info prefix
typedef struct
{
  IncrementalFileStates state;
  FileCast              cast;
} IncrementalListInfo;

// create info
typedef struct
{
  StorageSpecifier            *storageSpecifier;                  // storage specifier structure
  String                      uuid;                               // unique id to store or NULL
  const EntryList             *includeEntryList;                  // list of included entries
  const PatternList           *excludePatternList;                // list of exclude patterns
  const PatternList           *compressExcludePatternList;        // exclude compression pattern list
  const JobOptions            *jobOptions;
  ArchiveTypes                archiveType;                        // archive type to create
  String                      scheduleTitle;                      // schedule title or NULL
  String                      scheduleCustomText;                 // schedule custom text or NULL
  bool                        *pauseCreateFlag;                   // TRUE for pause creation
  bool                        *pauseStorageFlag;                  // TRUE for pause storage
  bool                        *requestedAbortFlag;                // TRUE to abort create

  bool                        partialFlag;                        // TRUE for create incremental/differential archive
  Dictionary                  namesDictionary;                    // dictionary with files (used for incremental/differental backup)
  bool                        storeIncrementalFileInfoFlag;       // TRUE to store incremental file data
  StorageHandle               storageHandle;                      // storage handle
  time_t                      startTime;                          // start time [ms] (unix time)

  MsgQueue                    entryMsgQueue;                      // queue with entries to store

  ArchiveInfo                 archiveInfo;

  bool                        collectorSumThreadExitedFlag;       // TRUE iff collector sum thread exited

  MsgQueue                    storageMsgQueue;                    // queue with waiting storage files
  Semaphore                   storageInfoLock;                    // lock semaphore for storage info
  struct
  {
    uint                      count;                              // number of current storage files
    uint64                    bytes;                              // number of bytes in current storage files
  }                           storageInfo;
  bool                        storageThreadExitFlag;
  StringList                  storageFileList;                    // list with stored storage files

  Errors                      failError;                          // failure error

  CreateStatusInfoFunction    statusInfoFunction;                 // status info call back
  void                        *statusInfoUserData;                // user data for status info call back
  CreateStatusInfo            statusInfo;                         // status info
  Semaphore                   statusInfoLock;                     // status info lock
  Semaphore                   statusInfoNameLock;                 // status info name lock
} CreateInfo;

// hard link info
typedef struct
{
  uint       count;                                               // number of hard links
  StringList nameList;                                            // list of hard linked names
} HardLinkInfo;

// entry message, send from collector thread -> main
typedef struct
{
  EntryTypes entryType;
  FileTypes  fileType;
  String     name;                                                // file/image/directory/link/special name
  StringList nameList;                                            // list of hard link names
} EntryMsg;

// storage message, send from main -> storage thread
typedef struct
{
  DatabaseHandle *databaseHandle;
  int64          storageId;                                       // database storage id
  String         fileName;                                        // temporary archive name
  uint64         fileSize;                                        // archive size
  String         destinationFileName;                             // destination archive name
} StorageMsg;

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : freeEntryMsg
* Purpose: free file entry message call back
* Input  : entryMsg - entry message
*          userData - user data (not used)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeEntryMsg(EntryMsg *entryMsg, void *userData)
{
  assert(entryMsg != NULL);

  UNUSED_VARIABLE(userData);

  switch (entryMsg->fileType)
  {
    case FILE_TYPE_FILE:
    case FILE_TYPE_DIRECTORY:
    case FILE_TYPE_LINK:
    case FILE_TYPE_SPECIAL:
      StringList_done(&entryMsg->nameList);
      String_delete(entryMsg->name);
      break;
    case FILE_TYPE_HARDLINK:
      StringList_done(&entryMsg->nameList);
      break;
    default:
      #ifndef NDEBUG
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
      #endif /* NDEBUG */
      break; /* not reached */
  }
}

/***********************************************************************\
* Name   : initCreateInfo
* Purpose: initialize create info
* Input  : createInfo                 - create info variable
*          storageSpecifier           - storage specifier structure
*          uuid                       - unique id to store or NULL
*          includeEntryList           - include entry list
*          excludePatternList         - exclude pattern list
*          compressExcludePatternList - exclude compression pattern list
*          jobOptions                 - job options
*          archiveType                - archive type; see ArchiveTypes
*                                       (normal/full/incremental)
*          storageNameCustomText      - storage name custome text or NULL
*          createStatusInfoFunction   - status info call back function
*                                       (can be NULL)
*          createStatusInfoUserData   - user data for status info
*                                       function
*          pauseCreateFlag            - pause creation flag (can be
*                                       NULL)
*          pauseStorageFlag           - pause storage flag (can be NULL)
*          requestedAbortFlag         - request abort flag (can be NULL)
* Output : createInfo - initialized create info variable
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initCreateInfo(CreateInfo               *createInfo,
                          StorageSpecifier         *storageSpecifier,
                          const String             uuid,
                          const EntryList          *includeEntryList,
                          const PatternList        *excludePatternList,
                          const PatternList        *compressExcludePatternList,
                          JobOptions               *jobOptions,
                          ArchiveTypes             archiveType,
                          const String             scheduleTitle,
                          const String             scheduleCustomText,
                          CreateStatusInfoFunction createStatusInfoFunction,
                          void                     *createStatusInfoUserData,
                          bool                     *pauseCreateFlag,
                          bool                     *pauseStorageFlag,
                          bool                     *requestedAbortFlag
                         )
{
  assert(createInfo != NULL);

  // init variables
  createInfo->storageSpecifier               = storageSpecifier;
  createInfo->uuid                           = uuid;
  createInfo->includeEntryList               = includeEntryList;
  createInfo->excludePatternList             = excludePatternList;
  createInfo->compressExcludePatternList     = compressExcludePatternList;
  createInfo->jobOptions                     = jobOptions;
  createInfo->scheduleTitle                  = scheduleTitle;
  createInfo->scheduleCustomText             = scheduleCustomText;
  createInfo->pauseCreateFlag                = pauseCreateFlag;
  createInfo->pauseStorageFlag               = pauseStorageFlag;
  createInfo->requestedAbortFlag             = requestedAbortFlag;
  createInfo->storeIncrementalFileInfoFlag   = FALSE;
  createInfo->startTime                      = time(NULL);
  createInfo->collectorSumThreadExitedFlag   = FALSE;
  createInfo->storageInfo.count              = 0;
  createInfo->storageInfo.bytes              = 0LL;
  createInfo->storageThreadExitFlag          = FALSE;
  StringList_init(&createInfo->storageFileList);
  createInfo->failError                      = ERROR_NONE;
  createInfo->statusInfoFunction             = createStatusInfoFunction;
  createInfo->statusInfoUserData             = createStatusInfoUserData;
  createInfo->statusInfo.doneEntries         = 0L;
  createInfo->statusInfo.doneBytes           = 0LL;
  createInfo->statusInfo.totalEntries        = 0L;
  createInfo->statusInfo.totalBytes          = 0LL;
  createInfo->statusInfo.collectTotalSumDone = FALSE;
  createInfo->statusInfo.skippedEntries      = 0L;
  createInfo->statusInfo.skippedBytes        = 0LL;
  createInfo->statusInfo.errorEntries        = 0L;
  createInfo->statusInfo.errorBytes          = 0LL;
  createInfo->statusInfo.archiveBytes        = 0LL;
  createInfo->statusInfo.compressionRatio    = 0.0;
  createInfo->statusInfo.name                = String_new();
  createInfo->statusInfo.entryDoneBytes      = 0LL;
  createInfo->statusInfo.entryTotalBytes     = 0LL;
  createInfo->statusInfo.storageName         = String_new();
  createInfo->statusInfo.storageDoneBytes    = 0LL;
  createInfo->statusInfo.storageTotalBytes   = 0LL;
  createInfo->statusInfo.volumeNumber        = 0;
  createInfo->statusInfo.volumeProgress      = 0.0;

  if (   (archiveType == ARCHIVE_TYPE_FULL)
      || (archiveType == ARCHIVE_TYPE_INCREMENTAL)
      || (archiveType == ARCHIVE_TYPE_DIFFERENTIAL)
     )
  {
    createInfo->archiveType = archiveType;
    createInfo->partialFlag =    (archiveType == ARCHIVE_TYPE_INCREMENTAL)
                              || (archiveType == ARCHIVE_TYPE_DIFFERENTIAL);
  }
  else
  {
    createInfo->archiveType = jobOptions->archiveType;
    createInfo->partialFlag =    (jobOptions->archiveType == ARCHIVE_TYPE_INCREMENTAL)
                              || (jobOptions->archiveType == ARCHIVE_TYPE_DIFFERENTIAL);
  }

  // init entry name queue, storage queue
  if (!MsgQueue_init(&createInfo->entryMsgQueue,MAX_FILE_MSG_QUEUE_ENTRIES))
  {
    HALT_FATAL_ERROR("Cannot initialize file message queue!");
  }
  if (!MsgQueue_init(&createInfo->storageMsgQueue,0))
  {
    HALT_FATAL_ERROR("Cannot initialize storage message queue!");
  }

  // init locks
  if (!Semaphore_init(&createInfo->storageInfoLock))
  {
    HALT_FATAL_ERROR("Cannot initialize storage semaphore!");
  }
  if (!Semaphore_init(&createInfo->statusInfoLock))
  {
    HALT_FATAL_ERROR("Cannot initialize status info semaphore!");
  }
  if (!Semaphore_init(&createInfo->statusInfoNameLock))
  {
    HALT_FATAL_ERROR("Cannot initialize status info name semaphore!");
  }
}

/***********************************************************************\
* Name   : doneCreateInfo
* Purpose: deinitialize create info
* Input  : createInfo - create info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void doneCreateInfo(CreateInfo *createInfo)
{
  assert(createInfo != NULL);

  Semaphore_done(&createInfo->statusInfoNameLock);
  Semaphore_done(&createInfo->statusInfoLock);
  Semaphore_done(&createInfo->storageInfoLock);

  MsgQueue_done(&createInfo->storageMsgQueue,NULL,NULL);
  MsgQueue_done(&createInfo->entryMsgQueue,(MsgQueueMsgFreeFunction)freeEntryMsg,NULL);

  String_delete(createInfo->statusInfo.storageName);
  String_delete(createInfo->statusInfo.name);
  StringList_done(&createInfo->storageFileList);
}

/***********************************************************************\
* Name   : readIncrementalList
* Purpose: read data of incremental list from file
* Input  : fileName        - file name
*          namesDictionary - names dictionary variable
* Output : -
* Return : ERROR_NONE if incremental list read in files dictionary,
*          error code otherwise
* Notes  : -
\***********************************************************************/

LOCAL Errors readIncrementalList(const String fileName,
                                 Dictionary   *namesDictionary
                                )
{
  #define MAX_KEY_DATA (64*1024)

  void                *keyData;
  Errors              error;
  FileHandle          fileHandle;
  char                id[32];
  uint16              version;
  IncrementalListInfo incrementalListInfo;
  uint16              keyLength;

  assert(fileName != NULL);
  assert(namesDictionary != NULL);

  // initialize variables
  keyData = malloc(MAX_KEY_DATA);
  if (keyData == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  // init variables
  Dictionary_clear(namesDictionary,NULL,NULL);

  // open file
  error = File_open(&fileHandle,fileName,FILE_OPEN_READ);
  if (error != ERROR_NONE)
  {
    free(keyData);
    return error;
  }

  // read and check header
  error = File_read(&fileHandle,id,sizeof(id),NULL);
  if (error != ERROR_NONE)
  {
    File_close(&fileHandle);
    free(keyData);
    return error;
  }
  if (strcmp(id,INCREMENTAL_LIST_FILE_ID) != 0)
  {
    File_close(&fileHandle);
    free(keyData);
    return ERROR_NOT_AN_INCREMENTAL_FILE;
  }
  error = File_read(&fileHandle,&version,sizeof(version),NULL);
  if (error != ERROR_NONE)
  {
    File_close(&fileHandle);
    free(keyData);
    return error;
  }
  if (version != INCREMENTAL_LIST_FILE_VERSION)
  {
    File_close(&fileHandle);
    free(keyData);
    return ERROR_WRONG_INCREMENTAL_FILE_VERSION;
  }

  // read entries
  while (!File_eof(&fileHandle))
  {
    // read entry
    incrementalListInfo.state = INCREMENTAL_FILE_STATE_UNKNOWN;
    error = File_read(&fileHandle,&incrementalListInfo.cast,sizeof(incrementalListInfo.cast),NULL);
    if (error != ERROR_NONE) break;
    error = File_read(&fileHandle,&keyLength,sizeof(keyLength),NULL);
    if (error != ERROR_NONE) break;
    error = File_read(&fileHandle,keyData,keyLength,NULL);
    if (error != ERROR_NONE) break;

    // store in dictionary
    Dictionary_add(namesDictionary,
                   keyData,
                   keyLength,
                   &incrementalListInfo,
                   sizeof(incrementalListInfo)
                  );
  }

  // close file
  File_close(&fileHandle);

  // free resources
  free(keyData);

  return error;
}

/***********************************************************************\
* Name   : writeIncrementalList
* Purpose: write incremental list data to file
* Input  : fileName        - file name
*          namesDictionary - names dictionary
* Output : -
* Return : ERROR_NONE if incremental list file written, error code
*          otherwise
* Notes  : -
\***********************************************************************/

LOCAL Errors writeIncrementalList(const String fileName,
                                  Dictionary   *namesDictionary
                                 )
{
  String                    directory;
  String                    tmpFileName;
  Errors                    error;
  FileHandle                fileHandle;
  char                      id[32];
  uint16                    version;
  DictionaryIterator        dictionaryIterator;
  const void                *keyData;
  ulong                     keyLength;
  void                      *data;
  ulong                     length;
  uint16                    n;
  const IncrementalListInfo *incrementalListInfo;

  assert(fileName != NULL);
  assert(namesDictionary != NULL);

  // get directory of .bid file
  directory = File_getFilePathName(String_new(),fileName);

  // create directory if not existing
  if (!String_isEmpty(directory))
  {
    if      (!File_exists(directory))
    {
      error = File_makeDirectory(directory,FILE_DEFAULT_USER_ID,FILE_DEFAULT_GROUP_ID,FILE_DEFAULT_PERMISSION);
      if (error != ERROR_NONE)
      {
        String_delete(directory);
        return error;
      }
    }
    else if (!File_isDirectory(directory))
    {
      error = ERRORX_(NOT_A_DIRECTORY,0,String_cString(directory));
      String_delete(directory);
      return error;
    }
  }

  // get temporary name for new .bid file
  tmpFileName = String_new();
  error = File_getTmpFileName(tmpFileName,NULL,directory);
  if (error != ERROR_NONE)
  {
    String_delete(tmpFileName);
    String_delete(directory);
    return error;
  }

  // open file new .bid file
  error = File_open(&fileHandle,tmpFileName,FILE_OPEN_CREATE);
  if (error != ERROR_NONE)
  {
    File_delete(tmpFileName,FALSE);
    String_delete(tmpFileName);
    String_delete(directory);
    return error;
  }

  // write header
  memset(id,0,sizeof(id));
  strncpy(id,INCREMENTAL_LIST_FILE_ID,sizeof(id)-1);
  error = File_write(&fileHandle,id,sizeof(id));
  if (error != ERROR_NONE)
  {
    File_close(&fileHandle);
    File_delete(tmpFileName,FALSE);
    String_delete(tmpFileName);
    String_delete(directory);
    return error;
  }
  version = INCREMENTAL_LIST_FILE_VERSION;
  error = File_write(&fileHandle,&version,sizeof(version));
  if (error != ERROR_NONE)
  {
    File_close(&fileHandle);
    File_delete(tmpFileName,FALSE);
    String_delete(tmpFileName);
    String_delete(directory);
    return error;
  }

  // write entries
  Dictionary_initIterator(&dictionaryIterator,namesDictionary);
  while (Dictionary_getNext(&dictionaryIterator,
                            &keyData,
                            &keyLength,
                            &data,
                            &length
                           )
        )
  {
    assert(keyData != NULL);
    assert(keyLength <= 65535);
    assert(data != NULL);
    assert(length == sizeof(IncrementalListInfo));

    incrementalListInfo = (IncrementalListInfo*)data;
#if 0
{
char s[1024];

memcpy(s,keyData,keyLength);s[keyLength]=0;
fprintf(stderr,"%s,%d: %s %d\n",__FILE__,__LINE__,s,incrementalFileInfo->state);
}
#endif /* 0 */

    error = File_write(&fileHandle,incrementalListInfo->cast,sizeof(incrementalListInfo->cast));
    if (error != ERROR_NONE) break;
    n = (uint16)keyLength;
    error = File_write(&fileHandle,&n,sizeof(n));
    if (error != ERROR_NONE) break;
    error = File_write(&fileHandle,keyData,n);
    if (error != ERROR_NONE) break;
  }
  Dictionary_doneIterator(&dictionaryIterator);

  // close file .bid file
  File_close(&fileHandle);
  if (error != ERROR_NONE)
  {
    File_delete(tmpFileName,FALSE);
    File_delete(tmpFileName,FALSE);
    String_delete(tmpFileName);
    String_delete(directory);
    return error;
  }

  // rename files
  error = File_rename(tmpFileName,fileName);
  if (error != ERROR_NONE)
  {
    File_delete(tmpFileName,FALSE);
    String_delete(tmpFileName);
    String_delete(directory);
    return error;
  }

  // free resources
  String_delete(tmpFileName);
  String_delete(directory);

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : isAborted
* Purpose: check if job is aborted
* Input  : createInfo - create info
* Output : -
* Return : TRUE iff aborted
* Notes  : -
\***********************************************************************/

LOCAL_INLINE bool isAborted(const CreateInfo *createInfo)
{
  assert(createInfo != NULL);

  return (createInfo->requestedAbortFlag != NULL) && (*createInfo->requestedAbortFlag);
}

/***********************************************************************\
* Name   : pauseCreate
* Purpose: pause create
* Input  : createInfo - create info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void pauseCreate(const CreateInfo *createInfo)
{
  assert(createInfo != NULL);

  while ((createInfo->pauseCreateFlag != NULL) && (*createInfo->pauseCreateFlag))
  {
    Misc_udelay(500L*1000L);
  }
}

/***********************************************************************\
* Name   : pauseStorage
* Purpose: pause storage
* Input  : createInfo - create info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void pauseStorage(const CreateInfo *createInfo)
{
  assert(createInfo != NULL);

  while ((createInfo->pauseStorageFlag != NULL) && (*createInfo->pauseStorageFlag))
  {
    Misc_udelay(500L*1000L);
  }
}

/***********************************************************************\
* Name   : isFileChanged
* Purpose: check if file changed
* Input  : namesDictionary - names dictionary
*          fileName        - file name
*          fileInfo        - file info with file cast data
* Output : -
* Return : TRUE iff file changed, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool isFileChanged(Dictionary     *namesDictionary,
                         const String   fileName,
                         const FileInfo *fileInfo
                        )
{
  union
  {
    void                *value;
    IncrementalListInfo *incrementalListInfo;
  } data;
  ulong length;

  assert(namesDictionary != NULL);
  assert(fileName != NULL);
  assert(fileInfo != NULL);

  // check if exists
  if (!Dictionary_find(namesDictionary,
                       String_cString(fileName),
                       String_length(fileName),
                       &data.value,
                       &length
                      )
     )
  {
    return TRUE;
  }
  assert(length == sizeof(IncrementalListInfo));

  // check if modified
  if (memcmp(data.incrementalListInfo->cast,&fileInfo->cast,sizeof(FileCast)) != 0)
  {
    return TRUE;
  }

  return FALSE;
}

/***********************************************************************\
* Name   : addIncrementalList
* Purpose: add file to incremental list
* Input  : namesDictionary - names dictionary
*          fileName        - file name
*          fileInfo        - file info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void addIncrementalList(Dictionary     *namesDictionary,
                              const String   fileName,
                              const FileInfo *fileInfo
                             )
{
  IncrementalListInfo incrementalListInfo;

  assert(namesDictionary != NULL);
  assert(fileName != NULL);
  assert(fileInfo != NULL);

  incrementalListInfo.state = INCREMENTAL_FILE_STATE_ADDED;
  memcpy(incrementalListInfo.cast,fileInfo->cast,sizeof(FileCast));

  Dictionary_add(namesDictionary,
                 String_cString(fileName),
                 String_length(fileName),
                 &incrementalListInfo,
                 sizeof(incrementalListInfo)
                );
}

/***********************************************************************\
* Name   : updateStatusInfo
* Purpose: update status info
* Input  : createInfo - create info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateStatusInfo(CreateInfo *createInfo)
{
  assert(createInfo != NULL);

  if (createInfo->statusInfoFunction != NULL)
  {
    createInfo->statusInfoFunction(createInfo->statusInfoUserData,
                                   createInfo->failError,
                                   &createInfo->statusInfo
                                  );
  }
}

/***********************************************************************\
* Name   : updateStorageStatusInfo
* Purpose: update storage info data
* Input  : createInfo        - create info
*          storageStatusInfo - storage status info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateStorageStatusInfo(CreateInfo              *createInfo,
                                   const StorageStatusInfo *storageStatusInfo
                                  )
{
  SemaphoreLock semaphoreLock;

  assert(createInfo != NULL);
  assert(storageStatusInfo != NULL);

  SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
  {
    createInfo->statusInfo.volumeNumber   = storageStatusInfo->volumeNumber;
    createInfo->statusInfo.volumeProgress = storageStatusInfo->volumeProgress;
    updateStatusInfo(createInfo);
  }
}

/***********************************************************************\
* Name   : isIncluded
* Purpose: check if name is included
* Input  : includeEntryNode - include entry node
*          name             - name
* Output : -
* Return : TRUE if excluded, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool isIncluded(const EntryNode *includeEntryNode,
                      const String    name
                     )
{
  assert(includeEntryNode != NULL);
  assert(name != NULL);

  return Pattern_match(&includeEntryNode->pattern,name,PATTERN_MATCH_MODE_BEGIN);
}

/***********************************************************************\
* Name   : checkIsExcluded
* Purpose: check if name is excluded
* Input  : excludePatternList - exclude pattern list
*          name               - name
* Output : -
* Return : TRUE if excluded, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool isExcluded(const PatternList *excludePatternList,
                      const String      name
                     )
{
  assert(excludePatternList != NULL);
  assert(name != NULL);

  return PatternList_match(excludePatternList,name,PATTERN_MATCH_MODE_EXACT);
}

/***********************************************************************\
* Name   : isNoBackup
* Purpose: check if file .nobackup/.NOBACKUP exists in sub-directory
* Input  : pathName - path name
* Output : -
* Return : TRUE if .nobackup/.NOBACKUP exists and option
*          ignoreNoBackupFile is not set, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool isNoBackup(const String pathName)
{
  String fileName;
  bool   haveNoBackupFlag;

  assert(pathName != NULL);

  haveNoBackupFlag = FALSE;
  if (!globalOptions.ignoreNoBackupFileFlag)
  {
    fileName = String_new();
    haveNoBackupFlag |= File_exists(File_appendFileNameCString(File_setFileName(fileName,pathName),".nobackup"));
    haveNoBackupFlag |= File_exists(File_appendFileNameCString(File_setFileName(fileName,pathName),".NOBACKUP"));
    String_delete(fileName);
  }

  return haveNoBackupFlag;
}

/***********************************************************************\
* Name   : isNoDumpAttribute
* Purpose: check if file attribute 'no dump' is set
* Input  : fileInfo   - file info
*          jobOptions - job options
* Output : -
* Return : TRUE if 'no dump' attribute is set and option ignoreNoDump is
*          not set, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL_INLINE bool isNoDumpAttribute(const FileInfo *fileInfo, const JobOptions *jobOptions)
{
  assert(fileInfo != NULL);
  assert(jobOptions != NULL);

  return !jobOptions->ignoreNoDumpAttributeFlag && File_haveAttributeNoDump(fileInfo);
}

/***********************************************************************\
* Name   : appendFileToEntryList
* Purpose: append file to entry list
* Input  : entryMsgQueue - entry message queue
*          entryType     - entry type
*          name          - name (will be copied!)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void appendFileToEntryList(MsgQueue     *entryMsgQueue,
                                 EntryTypes   entryType,
                                 const String name
                                )
{
  EntryMsg entryMsg;

  assert(entryMsgQueue != NULL);
  assert(name != NULL);

  // init
  entryMsg.entryType = entryType;
  entryMsg.fileType  = FILE_TYPE_FILE;
  entryMsg.name      = String_duplicate(name);
  StringList_init(&entryMsg.nameList);

  // put into message queue
  if (!MsgQueue_put(entryMsgQueue,&entryMsg,sizeof(entryMsg)))
  {
    freeEntryMsg(&entryMsg,NULL);
  }
}

/***********************************************************************\
* Name   : appendDirectoryToEntryList
* Purpose: append directory to entry list
* Input  : entryMsgQueue - entry message queue
*          entryType     - entry type
*          name          - name (will be copied!)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void appendDirectoryToEntryList(MsgQueue     *entryMsgQueue,
                                      EntryTypes   entryType,
                                      const String name
                                     )
{
  EntryMsg entryMsg;

  assert(entryMsgQueue != NULL);
  assert(name != NULL);

  // init
  entryMsg.entryType = entryType;
  entryMsg.fileType  = FILE_TYPE_DIRECTORY;
  entryMsg.name      = String_duplicate(name);
  StringList_init(&entryMsg.nameList);

  // put into message queue
  if (!MsgQueue_put(entryMsgQueue,&entryMsg,sizeof(entryMsg)))
  {
    freeEntryMsg(&entryMsg,NULL);
  }
}

/***********************************************************************\
* Name   : appendLinkToEntryList
* Purpose: append link to entry list
* Input  : entryMsgQueue - entry message queue
*          entryType     - entry type
*          fileType      - file type
*          name          - name (will be copied!)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void appendLinkToEntryList(MsgQueue     *entryMsgQueue,
                                 EntryTypes   entryType,
                                 const String name
                                )
{
  EntryMsg entryMsg;

  assert(entryMsgQueue != NULL);
  assert(name != NULL);

  // init
  entryMsg.entryType = entryType;
  entryMsg.fileType  = FILE_TYPE_LINK;
  entryMsg.name      = String_duplicate(name);
  StringList_init(&entryMsg.nameList);

  // put into message queue
  if (!MsgQueue_put(entryMsgQueue,&entryMsg,sizeof(entryMsg)))
  {
    freeEntryMsg(&entryMsg,NULL);
  }
}

/***********************************************************************\
* Name   : appendHardLinkToEntryList
* Purpose: append hard link to entry list
* Input  : entryMsgQueue - entry message queue
*          entryType     - entry type
*          nameList      - name list
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void appendHardLinkToEntryList(MsgQueue   *entryMsgQueue,
                                     EntryTypes entryType,
                                     StringList *nameList
                                    )
{
  EntryMsg entryMsg;

  assert(entryMsgQueue != NULL);

  // init
  entryMsg.entryType = entryType;
  entryMsg.fileType  = FILE_TYPE_HARDLINK;
  entryMsg.name      = NULL;
  StringList_init(&entryMsg.nameList);
  StringList_move(nameList,&entryMsg.nameList);

  // put into message queue
  if (!MsgQueue_put(entryMsgQueue,&entryMsg,sizeof(entryMsg)))
  {
    freeEntryMsg(&entryMsg,NULL);
  }
}

/***********************************************************************\
* Name   : appendSpecialToEntryList
* Purpose: append special to entry list
* Input  : entryMsgQueue - entry message queue
*          entryType     - entry type
*          name          - name (will be copied!)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void appendSpecialToEntryList(MsgQueue     *entryMsgQueue,
                                    EntryTypes   entryType,
                                    const String name
                                   )
{
  EntryMsg entryMsg;

  assert(entryMsgQueue != NULL);
  assert(name != NULL);

  // init
  entryMsg.entryType = entryType;
  entryMsg.fileType  = FILE_TYPE_SPECIAL;
  entryMsg.name      = String_duplicate(name);
  StringList_init(&entryMsg.nameList);

  // put into message queue
  if (!MsgQueue_put(entryMsgQueue,&entryMsg,sizeof(entryMsg)))
  {
    freeEntryMsg(&entryMsg,NULL);
  }
}

/***********************************************************************\
* Name   : formatArchiveFileName
* Purpose: get archive file name
* Input  : fileName              - file name variable
*          formatMode            - format mode; see FORMAT_MODE_*
*          templateFileName      - template file name
*          archiveType           - archive type; see ARCHIVE_TYPE_*
*          scheduleTitle         - schedule title or NULL
*          scheduleCustomText    - schedule custom text or NULL
*          time                  - time
*          partNumber            - part number (>=0 for parts,
*                                  ARCHIVE_PART_NUMBER_NONE for single
*                                  part archive)
*          lastPartFlag          - TRUE iff last part
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors formatArchiveFileName(String       fileName,
                                   FormatModes  formatMode,
                                   const String templateFileName,
                                   ArchiveTypes archiveType,
                                   const String scheduleTitle,
                                   const String scheduleCustomText,
                                   time_t       time,
                                   int          partNumber,
                                   bool         lastPartFlag
                                  )
{
  TextMacro textMacros[9];

  String    uuid;
  bool      partNumberFlag;
  #ifdef HAVE_LOCALTIME_R
    struct tm tmBuffer;
  #endif /* HAVE_LOCALTIME_R */
  struct tm *tm;
  uint      weekNumberU,weekNumberW;
  ulong     i,j;
  char      format[4];
  char      buffer[256];
  size_t    length;
  ulong     divisor;
  ulong     n;
  uint      z;
  int       d;

  // init variables
  uuid = Misc_getUUID(String_new());
  #ifdef HAVE_LOCALTIME_R
    tm = localtime_r(&time,&tmBuffer);
  #else /* not HAVE_LOCALTIME_R */
    tm = localtime(&time);
  #endif /* HAVE_LOCALTIME_R */
  assert(tm != NULL);
  strftime(buffer,sizeof(buffer)-1,"%U",tm);
  weekNumberU = (uint)atoi(buffer);
  strftime(buffer,sizeof(buffer)-1,"%W",tm);
  weekNumberW = (uint)atoi(buffer);

  // expand named macros
  switch (archiveType)
  {
    case ARCHIVE_TYPE_NORMAL:
      TEXT_MACRO_N_CSTRING(textMacros[0],"%type","normal");
      TEXT_MACRO_N_CSTRING(textMacros[0],"%T","N");
      break;
    case ARCHIVE_TYPE_FULL:
      TEXT_MACRO_N_CSTRING(textMacros[0],"%type","full");
      TEXT_MACRO_N_CSTRING(textMacros[0],"%T","F");
      break;
    case ARCHIVE_TYPE_INCREMENTAL:
      TEXT_MACRO_N_CSTRING(textMacros[0],"%type","incremental");
      TEXT_MACRO_N_CSTRING(textMacros[0],"%T","I");
      break;
    case ARCHIVE_TYPE_DIFFERENTIAL:
      TEXT_MACRO_N_CSTRING(textMacros[0],"%type","differential");
      TEXT_MACRO_N_CSTRING(textMacros[0],"%T","D");
      break;
    case ARCHIVE_TYPE_UNKNOWN:
      TEXT_MACRO_N_CSTRING(textMacros[0],"%type","unknown");
      TEXT_MACRO_N_CSTRING(textMacros[0],"%T","U");
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }
  switch (formatMode)
  {
    case FORMAT_MODE_ARCHIVE_FILE_NAME:
      TEXT_MACRO_N_CSTRING(textMacros[1],"%last", lastPartFlag ? "-last" : "");
      TEXT_MACRO_N_CSTRING(textMacros[2],"%uuid", String_cString(uuid));
      TEXT_MACRO_N_CSTRING(textMacros[3],"%title",(scheduleTitle != NULL) ? String_cString(scheduleTitle) : "");
      TEXT_MACRO_N_CSTRING(textMacros[4],"%text", (scheduleCustomText != NULL) ? String_cString(scheduleCustomText) : "");
      TEXT_MACRO_N_INTEGER(textMacros[5],"%U2",(weekNumberU%2)+1);
      TEXT_MACRO_N_INTEGER(textMacros[6],"%U4",(weekNumberU%4)+1);
      TEXT_MACRO_N_INTEGER(textMacros[7],"%W2",(weekNumberW%2)+1);
      TEXT_MACRO_N_INTEGER(textMacros[8],"%W4",(weekNumberW%4)+1);
      Misc_expandMacros(fileName,String_cString(templateFileName),textMacros,SIZE_OF_ARRAY(textMacros));
      break;
    case FORMAT_MODE_PATTERN:
      TEXT_MACRO_N_CSTRING(textMacros[1],"%last", "(-last){0,1}");
      TEXT_MACRO_N_CSTRING(textMacros[2],"%uuid", "[-0-9a-fA-F]+");
      TEXT_MACRO_N_CSTRING(textMacros[3],"%title","\\S+");
      TEXT_MACRO_N_CSTRING(textMacros[4],"%text", "\\S+");
      TEXT_MACRO_N_CSTRING(textMacros[5],"%U2","[12]");
      TEXT_MACRO_N_CSTRING(textMacros[6],"%U4","[1234]");
      TEXT_MACRO_N_CSTRING(textMacros[7],"%W2","[12]");
      TEXT_MACRO_N_CSTRING(textMacros[8],"%W4","[1234]");
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
      #endif /* NDEBUG */
  }
  Misc_expandMacros(fileName,String_cString(templateFileName),textMacros,SIZE_OF_ARRAY(textMacros));

  // expand date/time macros, part number
  partNumberFlag = FALSE;
  i = 0L;
  while (i < String_length(fileName))
  {
    switch (String_index(fileName,i))
    {
      case '%':
        if ((i+1) < String_length(fileName))
        {
          switch (String_index(fileName,i+1))
          {
            case '%':
              // %% -> %
              String_remove(fileName,i,1);
              i += 1L;
              break;
            case '#':
              // %# -> #
              String_remove(fileName,i,1);
              i += 1L;
              break;
            default:
              // format date/time part
              switch (String_index(fileName,i+1))
              {
                case 'E':
                case 'O':
                  // %Ex, %Ox: extended date/time macros
                  format[0] = '%';
                  format[1] = String_index(fileName,i+1);
                  format[2] = String_index(fileName,i+2);
                  format[3] = '\0';

                  String_remove(fileName,i,3);
                  break;
                default:
                  // %x: date/time macros
                  format[0] = '%';
                  format[1] = String_index(fileName,i+1);
                  format[2] = '\0';

                  String_remove(fileName,i,2);
                  break;
              }
              length = strftime(buffer,sizeof(buffer)-1,format,tm);

              // insert into string
              switch (formatMode)
              {
                case FORMAT_MODE_ARCHIVE_FILE_NAME:
                  String_insertBuffer(fileName,i,buffer,length);
                  i += length;
                  break;
                case FORMAT_MODE_PATTERN:
                  for (z = 0 ; z < length; z++)
                  {
                    if (strchr("*+?{}():[].^$|",buffer[z]) != NULL)
                    {
                      String_insertChar(fileName,i,'\\');
                      i += 1L;
                    }
                    String_insertChar(fileName,i,buffer[z]);
                    i += 1L;
                  }
                  break;
                #ifndef NDEBUG
                  default:
                    HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
                    break; /* not reached */
                  #endif /* NDEBUG */
              }
              break;
          }
        }
        else
        {
          // % at end of string
          i += 1L;
        }
        break;
      case '#':
        // #...#
        switch (formatMode)
        {
          case FORMAT_MODE_ARCHIVE_FILE_NAME:
            if (partNumber != ARCHIVE_PART_NUMBER_NONE)
            {
              // find #...# and get max. divisor for part number
              divisor = 1L;
              j = i+1L;
              while ((j < String_length(fileName) && String_index(fileName,j) == '#'))
              {
                j++;
                if (divisor < 1000000000L) divisor*=10;
              }
              if ((ulong)partNumber >= (divisor*10L))
              {
                free(uuid);
                return ERROR_INSUFFICIENT_SPLIT_NUMBERS;
              }

              // replace #...# by part number
              n = partNumber;
              z = 0;
              while (divisor > 0L)
              {
                d = n/divisor; n = n%divisor; divisor = divisor/10;
                if (z < sizeof(buffer)-1)
                {
                  buffer[z] = '0'+d; z++;
                }
              }
              buffer[z] = '\0';
              String_replaceCString(fileName,i,j-i,buffer);
              i = j;

              partNumberFlag = TRUE;
            }
            else
            {
              i += 1L;
            }
            break;
          case FORMAT_MODE_PATTERN:
            // replace by "."
            String_replaceChar(fileName,i,1,'.');
            i += 1L;
            break;
          #ifndef NDEBUG
            default:
              HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
              break; /* not reached */
            #endif /* NDEBUG */
        }
        break;
      default:
        i += 1L;
        break;
    }
  }

  // append part number if multipart mode and there is no part number in format string
  if ((partNumber != ARCHIVE_PART_NUMBER_NONE) && !partNumberFlag)
  {
    switch (formatMode)
    {
      case FORMAT_MODE_ARCHIVE_FILE_NAME:
        String_format(fileName,".%06d",partNumber);
        break;
      case FORMAT_MODE_PATTERN:
        String_appendCString(fileName,"......");
        break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break; /* not reached */
        #endif /* NDEBUG */
    }
  }

  // free resources
  String_delete(uuid);

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : formatIncrementalFileName
* Purpose: format incremental file name
* Input  : fileName         - file name variable
*          storageSpecifier - storage specifier
* Output : -
* Return : file name
* Notes  : -
\***********************************************************************/

LOCAL String formatIncrementalFileName(String                 fileName,
                                       const StorageSpecifier *storageSpecifier
                                      )
{
  #define SEPARATOR_CHARS "-_"

  ulong i;
  char  ch;

  // remove all macros and leading and tailing separator characters
  String_clear(fileName);
  i = 0L;
  while (i < String_length(storageSpecifier->fileName))
  {
    ch = String_index(storageSpecifier->fileName,i);
    switch (ch)
    {
      case '%':
        i += 1L;
        if (i < String_length(storageSpecifier->fileName))
        {
          // removed previous separator characters
          String_trimRight(fileName,SEPARATOR_CHARS);

          ch = String_index(storageSpecifier->fileName,i);
          switch (ch)
          {
            case '%':
              // %%
              String_appendChar(fileName,'%');
              i += 1L;
              break;
            case '#':
              // %#
              String_appendChar(fileName,'#');
              i += 1L;
              break;
            default:
              // discard %xyz
              if (isalpha(ch))
              {
                while (   (i < String_length(storageSpecifier->fileName))
                       && isalpha(ch)
                      )
                {
                  i += 1L;
                  ch = String_index(storageSpecifier->fileName,i);
                }
              }

              // discard following separator characters
              if (strchr(SEPARATOR_CHARS,ch) != NULL)
              {
                while (   (i < String_length(storageSpecifier->fileName))
                       && (strchr(SEPARATOR_CHARS,ch) != NULL)
                      )
                {
                  i += 1L;
                  ch = String_index(storageSpecifier->fileName,i);
                }
              }
              break;
          }
        }
        break;
      case '#':
        i += 1L;
        break;
      default:
        String_appendChar(fileName,ch);
        i += 1L;
        break;
    }
  }

  // replace or add file name extension
  if (String_subEqualsCString(fileName,
                              FILE_NAME_EXTENSION_ARCHIVE_FILE,
                              String_length(fileName)-strlen(FILE_NAME_EXTENSION_ARCHIVE_FILE),
                              strlen(FILE_NAME_EXTENSION_ARCHIVE_FILE)
                             )
     )
  {
    String_replaceCString(fileName,
                          String_length(fileName)-strlen(FILE_NAME_EXTENSION_ARCHIVE_FILE),
                          strlen(FILE_NAME_EXTENSION_ARCHIVE_FILE),
                          FILE_NAME_EXTENSION_INCREMENTAL_FILE
                         );
  }
  else
  {
    String_appendCString(fileName,FILE_NAME_EXTENSION_INCREMENTAL_FILE);
  }

  return fileName;
}

/***********************************************************************\
* Name   : collectorSumThreadCode
* Purpose: file collector sum thread: only collect files and update
*          total files/bytes values
* Input  : createInfo - create info block
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void collectorSumThreadCode(CreateInfo *createInfo)
{
  Dictionary          duplicateNamesDictionary;
  StringList          nameList;
  String              basePath;
  String              name;
  EntryNode           *includeEntryNode;
  StringTokenizer     fileNameTokenizer;
  String              string;
  Errors              error;
  String              fileName;
  FileInfo            fileInfo;
  SemaphoreLock       semaphoreLock;
  DirectoryListHandle directoryListHandle;
  DeviceInfo          deviceInfo;

  assert(createInfo != NULL);
  assert(createInfo->includeEntryList != NULL);
  assert(createInfo->excludePatternList != NULL);
  assert(createInfo->jobOptions != NULL);

  // initialize variables
  if (!Dictionary_init(&duplicateNamesDictionary,NULL,NULL))
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  StringList_init(&nameList);
  basePath = String_new();
  name     = String_new();

  // process include entries
  includeEntryNode = createInfo->includeEntryList->head;
  while (   (includeEntryNode != NULL)
         && (createInfo->failError == ERROR_NONE)
         && !isAborted(createInfo)
        )
  {
    // pause
    pauseCreate(createInfo);

    // find base path
    File_initSplitFileName(&fileNameTokenizer,includeEntryNode->string);
    if (File_getNextSplitFileName(&fileNameTokenizer,&string) && !Pattern_checkIsPattern(string))
    {
      if (String_length(string) > 0L)
      {
        File_setFileName(basePath,string);
      }
      else
      {
        File_setFileNameChar(basePath,FILES_PATHNAME_SEPARATOR_CHAR);
      }
    }
    while (File_getNextSplitFileName(&fileNameTokenizer,&string) && !Pattern_checkIsPattern(string))
    {
      File_appendFileName(basePath,string);
    }
    File_doneSplitFileName(&fileNameTokenizer);

    // find files
    StringList_append(&nameList,basePath);
    while (   (createInfo->failError == ERROR_NONE)
           && !isAborted(createInfo)
           && !StringList_isEmpty(&nameList)
          )
    {
      // pause
      pauseCreate(createInfo);

      // get next file/directory to process
      name = StringList_getLast(&nameList,name);

      if (   isIncluded(includeEntryNode,name)
          && !isExcluded(createInfo->excludePatternList,name)
         )
      {
        // read file info
        error = File_getFileInfo(&fileInfo,name);
        if (error != ERROR_NONE)
        {
          continue;
        }

        if (!isNoDumpAttribute(&fileInfo,createInfo->jobOptions))
        {
          switch (fileInfo.type)
          {
            case FILE_TYPE_FILE:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);

                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (   !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                       )
                    {
                      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                      {
                        createInfo->statusInfo.totalEntries++;
                        createInfo->statusInfo.totalBytes += fileInfo.size;
                        updateStatusInfo(createInfo);
                      }
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    break;
                }
              }
              break;
            case FILE_TYPE_DIRECTORY:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);

                if (!isNoBackup(name))
                {
                  switch (includeEntryNode->type)
                  {
                    case ENTRY_TYPE_FILE:
                      if (   !createInfo->partialFlag
                          || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                         )
                      {
                        SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                        {
                          createInfo->statusInfo.totalEntries++;
                          updateStatusInfo(createInfo);
                        }
                      }
                      break;
                    case ENTRY_TYPE_IMAGE:
                      break;
                  }

                  // open directory contents
                  error = File_openDirectoryList(&directoryListHandle,name);
                  if (error == ERROR_NONE)
                  {
                    // read directory contents
                    fileName = String_new();
                    while (   (createInfo->failError == ERROR_NONE)
                           && !isAborted(createInfo)
                           && !File_endOfDirectoryList(&directoryListHandle)
                          )
                    {
                      // pause
                      pauseCreate(createInfo);

                      // read next directory entry
                      error = File_readDirectoryList(&directoryListHandle,fileName);
                      if (error != ERROR_NONE)
                      {
                        continue;
                      }

                      if (   isIncluded(includeEntryNode,fileName)
                          && !isExcluded(createInfo->excludePatternList,fileName)
                         )
                      {
                        // read file info
                        error = File_getFileInfo(&fileInfo,fileName);
                        if (error != ERROR_NONE)
                        {
                          continue;
                        }

                        if (!isNoDumpAttribute(&fileInfo,createInfo->jobOptions))
                        {
                          switch (fileInfo.type)
                          {
                            case FILE_TYPE_FILE:
                              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName)))
                              {
                                // add to known names history
                                Dictionary_add(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName),NULL,0);

                                switch (includeEntryNode->type)
                                {
                                  case ENTRY_TYPE_FILE:
                                    if (   !createInfo->partialFlag
                                        || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                       )
                                    {
                                      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                                      {
                                        createInfo->statusInfo.totalEntries++;
                                        createInfo->statusInfo.totalBytes += fileInfo.size;
                                        updateStatusInfo(createInfo);
                                      }
                                    }
                                    break;
                                  case ENTRY_TYPE_IMAGE:
                                    break;
                                }
                              }
                              break;
                            case FILE_TYPE_DIRECTORY:
                              // add to name list
                              StringList_append(&nameList,fileName);
                              break;
                            case FILE_TYPE_LINK:
                              switch (includeEntryNode->type)
                              {
                                case ENTRY_TYPE_FILE:
                                  if (   !createInfo->partialFlag
                                      || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                     )
                                  {
                                    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                                    {
                                      createInfo->statusInfo.totalEntries++;
                                      updateStatusInfo(createInfo);
                                    }
                                  }
                                  break;
                                case ENTRY_TYPE_IMAGE:
                                  break;
                              }
                              break;
                            case FILE_TYPE_HARDLINK:
                              switch (includeEntryNode->type)
                              {
                                case ENTRY_TYPE_FILE:
                                  if (  !createInfo->partialFlag
                                      || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                     )
                                  {
                                    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                                    {
                                      createInfo->statusInfo.totalEntries++;
                                      createInfo->statusInfo.totalBytes += fileInfo.size;
                                      updateStatusInfo(createInfo);
                                    }
                                  }
                                  break;
                                case ENTRY_TYPE_IMAGE:
                                  break;
                              }
                              break;
                            case FILE_TYPE_SPECIAL:
                              switch (includeEntryNode->type)
                              {
                                case ENTRY_TYPE_FILE:
                                  if (  !createInfo->partialFlag
                                      || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                     )
                                  {
                                    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                                    {
                                      createInfo->statusInfo.totalEntries++;
                                      if (   (includeEntryNode->type == ENTRY_TYPE_IMAGE)
                                          && (fileInfo.specialType == FILE_SPECIAL_TYPE_BLOCK_DEVICE)
                                          && (fileInfo.size >= 0LL)
                                         )
                                      {
                                        createInfo->statusInfo.totalBytes += fileInfo.size;
                                      }
                                      updateStatusInfo(createInfo);
                                    }
                                  }
                                  break;
                                case ENTRY_TYPE_IMAGE:
                                  if (fileInfo.specialType == FILE_SPECIAL_TYPE_BLOCK_DEVICE)
                                  {
                                    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                                    {
                                      createInfo->statusInfo.totalEntries++;
                                      if (fileInfo.size >= 0LL) createInfo->statusInfo.totalBytes += fileInfo.size;
                                      updateStatusInfo(createInfo);
                                    }
                                  }
                                  break;
                              }
                              break;
                            default:
                              break;
                          }
                        }
                      }
                    }
                    String_delete(fileName);

                    // close directory
                    File_closeDirectoryList(&directoryListHandle);
                  }
                }
              }
              break;
            case FILE_TYPE_LINK:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);
                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (   !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                       )
                    {
                      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                      {
                        createInfo->statusInfo.totalEntries++;
                        updateStatusInfo(createInfo);
                      }
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    break;
                }
              }
              break;
            case FILE_TYPE_HARDLINK:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);
                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (   !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                       )
                    {
                      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                      {
                        createInfo->statusInfo.totalEntries++;
                        createInfo->statusInfo.totalBytes += fileInfo.size;
                        updateStatusInfo(createInfo);
                      }
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    break;
                }
              }
              break;
            case FILE_TYPE_SPECIAL:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);
                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (   !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                       )
                    {
                      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                      {
                        createInfo->statusInfo.totalEntries++;
                        updateStatusInfo(createInfo);
                      }
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    if (fileInfo.specialType == FILE_SPECIAL_TYPE_BLOCK_DEVICE)
                    {
                      // get device info
                      error = Device_getDeviceInfo(&deviceInfo,name);
                      if (error != ERROR_NONE)
                      {
                        continue;
                      }
                      UNUSED_VARIABLE(deviceInfo);

                      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                      {
                        createInfo->statusInfo.totalEntries++;
                        if (fileInfo.size >= 0LL) createInfo->statusInfo.totalBytes += fileInfo.size;
                        updateStatusInfo(createInfo);
                      }
                    }
                    break;
                }
              }
              break;
            default:
              break;
          }
        }

        // free resources
      }
    }

    // next include entry
    includeEntryNode = includeEntryNode->next;
  }

  // done
  SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
  {
    createInfo->statusInfo.collectTotalSumDone = TRUE;
    updateStatusInfo(createInfo);
  }

  // free resoures
  String_delete(name);
  String_delete(basePath);
  StringList_done(&nameList);
  Dictionary_done(&duplicateNamesDictionary,NULL,NULL);

  // terminate
  createInfo->collectorSumThreadExitedFlag = TRUE;
}

/***********************************************************************\
* Name   : collectorThreadCode
* Purpose: file collector thread
* Input  : createInfo - create info block
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void collectorThreadCode(CreateInfo *createInfo)
{
  AutoFreeList        autoFreeList;
  Dictionary          duplicateNamesDictionary;
  StringList          nameList;
  String              basePath;
  String              name;
  SemaphoreLock       semaphoreLock;
  String              fileName;
  Dictionary          hardLinksDictionary;
  EntryNode           *includeEntryNode;
  StringTokenizer     fileNameTokenizer;
  String              string;
  Errors              error;
  FileInfo            fileInfo;
  DirectoryListHandle directoryListHandle;
  DeviceInfo          deviceInfo;
  DictionaryIterator  dictionaryIterator;
//???
union { const void *value; const uint64 *id; } keyData;
union { void *value; HardLinkInfo *hardLinkInfo; } data;

  assert(createInfo != NULL);
  assert(createInfo->includeEntryList != NULL);
  assert(createInfo->excludePatternList != NULL);
  assert(createInfo->jobOptions != NULL);

  // initialize variables
  AutoFree_init(&autoFreeList);
  if (!Dictionary_init(&duplicateNamesDictionary,NULL,NULL))
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  StringList_init(&nameList);
  basePath = String_new();
  name     = String_new();
  fileName = String_new();
  if (!Dictionary_init(&hardLinksDictionary,NULL,NULL))
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  AUTOFREE_ADD(&autoFreeList,&duplicateNamesDictionary,{ Dictionary_done(&duplicateNamesDictionary,NULL,NULL); });
  AUTOFREE_ADD(&autoFreeList,&nameList,{ StringList_done(&nameList); });
  AUTOFREE_ADD(&autoFreeList,basePath,{ String_delete(basePath); });
  AUTOFREE_ADD(&autoFreeList,name,{ String_delete(name); });
  AUTOFREE_ADD(&autoFreeList,fileName,{ String_delete(fileName); });
  AUTOFREE_ADD(&autoFreeList,&hardLinksDictionary,{ Dictionary_done(&hardLinksDictionary,NULL,NULL); });

  // process include entries
  includeEntryNode = createInfo->includeEntryList->head;
  while (   (includeEntryNode != NULL)
         && (createInfo->failError == ERROR_NONE)
         && !isAborted(createInfo)
        )
  {
    // pause
    pauseCreate(createInfo);

    // find base path
    File_initSplitFileName(&fileNameTokenizer,includeEntryNode->string);
    if (File_getNextSplitFileName(&fileNameTokenizer,&string) && !Pattern_checkIsPattern(string))
    {
      if (String_length(string) > 0L)
      {
        File_setFileName(basePath,string);
      }
      else
      {
        File_setFileNameChar(basePath,FILES_PATHNAME_SEPARATOR_CHAR);
      }
    }
    while (File_getNextSplitFileName(&fileNameTokenizer,&string) && !Pattern_checkIsPattern(string))
    {
      File_appendFileName(basePath,string);
    }
    File_doneSplitFileName(&fileNameTokenizer);

    // find files
    StringList_append(&nameList,basePath);
    while (   !StringList_isEmpty(&nameList)
           && (createInfo->failError == ERROR_NONE)
           && !isAborted(createInfo)
          )
    {
      // pause
      pauseCreate(createInfo);

      // get next entry to process
      name = StringList_getLast(&nameList,name);
//fprintf(stderr,"%s, %d: ----------------------\n",__FILE__,__LINE__);
//fprintf(stderr,"%s, %d: %s included=%d excluded=%d dictionary=%d\n",__FILE__,__LINE__,String_cString(name),isIncluded(includeEntryNode,name),isExcluded(createInfo->excludePatternList,name),Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)));

      if (   isIncluded(includeEntryNode,name)
          && !isExcluded(createInfo->excludePatternList,name)
         )
      {
        // read file info
        error = File_getFileInfo(&fileInfo,name);
        if (error != ERROR_NONE)
        {
          printInfo(2,"Cannot access '%s' (error: %s) - skipped\n",String_cString(name),Error_getText(error));
          logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(name),Error_getText(error));

          SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
          {
            createInfo->statusInfo.errorEntries++;
            updateStatusInfo(createInfo);
          }
          continue;
        }

        if (!isNoDumpAttribute(&fileInfo,createInfo->jobOptions))
        {
          switch (fileInfo.type)
          {
            case FILE_TYPE_FILE:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);

                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (   !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                       )
                    {
                      // add to entry list
                      appendFileToEntryList(&createInfo->entryMsgQueue,
                                            ENTRY_TYPE_FILE,
                                            name
                                           );
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    break;
                }
              }
              break;
            case FILE_TYPE_DIRECTORY:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);

                if (!isNoBackup(name))
                {
                  switch (includeEntryNode->type)
                  {
                    case ENTRY_TYPE_FILE:
                      if (   !createInfo->partialFlag
                          || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                         )
                      {
                        // add to entry list
                        appendDirectoryToEntryList(&createInfo->entryMsgQueue,
                                                   ENTRY_TYPE_FILE,
                                                   name
                                                  );
                      }
                      break;
                    case ENTRY_TYPE_IMAGE:
                      break;
                  }

                  // open directory contents
                  error = File_openDirectoryList(&directoryListHandle,name);
                  if (error == ERROR_NONE)
                  {
                    // read directory content
                    while (   (createInfo->failError == ERROR_NONE)
                           && !isAborted(createInfo)
                           && !File_endOfDirectoryList(&directoryListHandle)
                          )
                    {
                      // pause
                      pauseCreate(createInfo);

                      // read next directory entry
                      error = File_readDirectoryList(&directoryListHandle,fileName);
                      if (error != ERROR_NONE)
                      {
                        printInfo(2,"Cannot read directory '%s' (error: %s) - skipped\n",String_cString(name),Error_getText(error));
                        logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(name),Error_getText(error));

                        SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                        {
                          createInfo->statusInfo.errorEntries++;
                          createInfo->statusInfo.errorBytes += (uint64)fileInfo.size;
                          updateStatusInfo(createInfo);
                        }
                        continue;
                      }
//fprintf(stderr,"%s, %d: %s included=%d excluded=%d dictionary=%d\n",__FILE__,__LINE__,String_cString(fileName),isIncluded(includeEntryNode,fileName),isExcluded(createInfo->excludePatternList,fileName),Dictionary_contains(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName)));

                      if (   isIncluded(includeEntryNode,fileName)
                          && !isExcluded(createInfo->excludePatternList,fileName)
                         )
                      {
                        // read file info
                        error = File_getFileInfo(&fileInfo,fileName);
                        if (error != ERROR_NONE)
                        {
                          printInfo(2,"Cannot access '%s' (error: %s) - skipped\n",String_cString(fileName),Error_getText(error));
                          logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(fileName),Error_getText(error));

                          SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                          {
                            createInfo->statusInfo.errorEntries++;
                            updateStatusInfo(createInfo);
                          }
                          continue;
                        }

                        if (!isNoDumpAttribute(&fileInfo,createInfo->jobOptions))
                        {
                          switch (fileInfo.type)
                          {
                            case FILE_TYPE_FILE:
                              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName)))
                              {
                                // add to known names history
                                Dictionary_add(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName),NULL,0);
                                switch (includeEntryNode->type)
                                {
                                  case ENTRY_TYPE_FILE:
                                    if (   !createInfo->partialFlag
                                        || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                       )
                                    {
                                      // add to entry list
                                      appendFileToEntryList(&createInfo->entryMsgQueue,
                                                            ENTRY_TYPE_FILE,
                                                            fileName
                                                           );
                                    }
                                    break;
                                  case ENTRY_TYPE_IMAGE:
                                    break;
                                }
                              }
                              break;
                            case FILE_TYPE_DIRECTORY:
                              // add to directory search list
                              StringList_append(&nameList,fileName);
                              break;
                            case FILE_TYPE_LINK:
                              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName)))
                              {
                                // add to known names history
                                Dictionary_add(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName),NULL,0);
                                switch (includeEntryNode->type)
                                {
                                  case ENTRY_TYPE_FILE:
                                    if (   !createInfo->partialFlag
                                        || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                       )
                                    {
                                      // add to entry list
                                      appendLinkToEntryList(&createInfo->entryMsgQueue,
                                                            ENTRY_TYPE_FILE,
                                                            fileName
                                                           );
                                    }
                                    break;
                                  case ENTRY_TYPE_IMAGE:
                                    break;
                                }
                              }
                              break;
                            case FILE_TYPE_HARDLINK:
                              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName)))
                              {
                                // add to known names history
                                Dictionary_add(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName),NULL,0);
                                switch (includeEntryNode->type)
                                {
                                  case ENTRY_TYPE_FILE:
                                    {
                                      union { void *value; HardLinkInfo *hardLinkInfo; } data;
                                      HardLinkInfo hardLinkInfo;

                                      if (   !createInfo->partialFlag
                                          || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                         )
                                      {
                                        if (Dictionary_find(&hardLinksDictionary,
                                                            &fileInfo.id,
                                                            sizeof(fileInfo.id),
                                                            &data.value,
                                                            NULL
                                                           )
                                           )
                                        {
                                          // append name to hard link name list
                                          StringList_append(&data.hardLinkInfo->nameList,fileName);

                                          if (StringList_count(&data.hardLinkInfo->nameList) >= data.hardLinkInfo->count)
                                          {
                                            // found last hardlink -> add to entry list
                                            appendHardLinkToEntryList(&createInfo->entryMsgQueue,
                                                                      ENTRY_TYPE_FILE,
                                                                      &data.hardLinkInfo->nameList
                                                                     );

                                            // clear entry
                                            Dictionary_remove(&hardLinksDictionary,
                                                              &fileInfo.id,
                                                              sizeof(fileInfo.id),
                                                              NULL,
                                                              NULL
                                                             );
                                          }
                                        }
                                        else
                                        {
                                          // create hard link name list
                                          hardLinkInfo.count = fileInfo.linkCount;
                                          StringList_init(&hardLinkInfo.nameList);
                                          StringList_append(&hardLinkInfo.nameList,fileName);

                                          if (!Dictionary_add(&hardLinksDictionary,
                                                              &fileInfo.id,
                                                              sizeof(fileInfo.id),
                                                              &hardLinkInfo,
                                                              sizeof(hardLinkInfo)
                                                             )
                                             )
                                          {
                                            HALT_INSUFFICIENT_MEMORY();
                                          }
                                        }
                                      }
                                    }
                                    break;
                                  case ENTRY_TYPE_IMAGE:
                                    break;
                                }
                              }
                              break;
                            case FILE_TYPE_SPECIAL:
                              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName)))
                              {
                                // add to known names history
                                Dictionary_add(&duplicateNamesDictionary,String_cString(fileName),String_length(fileName),NULL,0);
                                switch (includeEntryNode->type)
                                {
                                  case ENTRY_TYPE_FILE:
                                    if (   !createInfo->partialFlag
                                        || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                                       )
                                    {
                                      // add to entry list
                                      appendSpecialToEntryList(&createInfo->entryMsgQueue,
                                                               ENTRY_TYPE_FILE,
                                                               fileName
                                                              );
                                    }
                                    break;
                                  case ENTRY_TYPE_IMAGE:
                                    if (fileInfo.specialType == FILE_SPECIAL_TYPE_BLOCK_DEVICE)
                                    {
                                      // add to entry list
                                      appendSpecialToEntryList(&createInfo->entryMsgQueue,
                                                               ENTRY_TYPE_IMAGE,
                                                               fileName
                                                              );
                                    }
                                    break;
                                }
                              }
                              break;
                            default:
                              printInfo(2,"Unknown type of file '%s' - skipped\n",String_cString(fileName));
                              logMessage(LOG_TYPE_ENTRY_TYPE_UNKNOWN,"unknown type '%s'\n",String_cString(fileName));

                              SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                              {
                                createInfo->statusInfo.errorEntries++;
                                createInfo->statusInfo.errorBytes += (uint64)fileInfo.size;
                                updateStatusInfo(createInfo);
                              }
                              break;
                          }
                        }
                        else
                        {
                          logMessage(LOG_TYPE_ENTRY_EXCLUDED,"excluded '%s' (no dump attribute)\n",String_cString(fileName));

                          SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                          {
                            createInfo->statusInfo.skippedEntries++;
                            createInfo->statusInfo.skippedBytes += fileInfo.size;
                            updateStatusInfo(createInfo);
                          }
                        }
                      }
                      else
                      {
                        logMessage(LOG_TYPE_ENTRY_EXCLUDED,"excluded '%s'\n",String_cString(fileName));

                        SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                        {
                          createInfo->statusInfo.skippedEntries++;
                          createInfo->statusInfo.skippedBytes += fileInfo.size;
                          updateStatusInfo(createInfo);
                        }
                      }
                    }

                    // close directory
                    File_closeDirectoryList(&directoryListHandle);
                  }
                  else
                  {
                    printInfo(2,"Cannot open directory '%s' (error: %s) - skipped\n",String_cString(name),Error_getText(error));
                    logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(name),Error_getText(error));

                    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                    {
                      createInfo->statusInfo.errorEntries++;
                      updateStatusInfo(createInfo);
                    }
                  }
                }
                else
                {
                  logMessage(LOG_TYPE_ENTRY_EXCLUDED,"excluded '%s' (.nobackup file)\n",String_cString(name));
                }
              }
              break;
            case FILE_TYPE_LINK:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);

                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (  !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                      )
                    {
                      // add to entry list
                      appendLinkToEntryList(&createInfo->entryMsgQueue,
                                            ENTRY_TYPE_FILE,
                                            name
                                           );
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    break;
                }
              }
              break;
            case FILE_TYPE_HARDLINK:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);

                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (   !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                       )
                    {
                      union { void *value; HardLinkInfo *hardLinkInfo; } data;
                      HardLinkInfo hardLinkInfo;

                      if (   !createInfo->partialFlag
                          || isFileChanged(&createInfo->namesDictionary,fileName,&fileInfo)
                         )
                      {
                        if (Dictionary_find(&hardLinksDictionary,
                                            &fileInfo.id,
                                            sizeof(fileInfo.id),
                                            &data.value,
                                            NULL
                                           )
                           )
                        {
                          // append name to hard link name list
                          StringList_append(&data.hardLinkInfo->nameList,name);

                          if (StringList_count(&data.hardLinkInfo->nameList) >= data.hardLinkInfo->count)
                          {
                            // found last hardlink -> add to entry list
                            appendHardLinkToEntryList(&createInfo->entryMsgQueue,
                                                      ENTRY_TYPE_FILE,
                                                      &data.hardLinkInfo->nameList
                                                     );

                            // clear entry
                            Dictionary_remove(&hardLinksDictionary,
                                              &fileInfo.id,
                                              sizeof(fileInfo.id),
                                              NULL,
                                              NULL
                                             );
                          }
                        }
                        else
                        {
                          // create hard link name list
                          hardLinkInfo.count = fileInfo.linkCount;
                          StringList_init(&hardLinkInfo.nameList);
                          StringList_append(&hardLinkInfo.nameList,name);

                          if (!Dictionary_add(&hardLinksDictionary,
                                              &fileInfo.id,
                                              sizeof(fileInfo.id),
                                              &hardLinkInfo,
                                              sizeof(hardLinkInfo)
                                             )
                             )
                          {
                            HALT_INSUFFICIENT_MEMORY();
                          }
                        }
                      }
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    break;
                }
              }
              break;
            case FILE_TYPE_SPECIAL:
              if (!Dictionary_contains(&duplicateNamesDictionary,String_cString(name),String_length(name)))
              {
                // add to known names history
                Dictionary_add(&duplicateNamesDictionary,String_cString(name),String_length(name),NULL,0);

                switch (includeEntryNode->type)
                {
                  case ENTRY_TYPE_FILE:
                    if (   !createInfo->partialFlag
                        || isFileChanged(&createInfo->namesDictionary,name,&fileInfo)
                       )
                    {
                      // add to entry list
                      appendSpecialToEntryList(&createInfo->entryMsgQueue,
                                               ENTRY_TYPE_FILE,
                                               name
                                              );
                    }
                    break;
                  case ENTRY_TYPE_IMAGE:
                    if (fileInfo.specialType == FILE_SPECIAL_TYPE_BLOCK_DEVICE)
                    {
                      // get device info
                      error = Device_getDeviceInfo(&deviceInfo,name);
                      if (error != ERROR_NONE)
                      {
                        printInfo(2,"Cannot access '%s' (error: %s) - skipped\n",String_cString(name),Error_getText(error));
                        logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(name),Error_getText(error));

                        SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
                        {
                          createInfo->statusInfo.errorEntries++;
                          updateStatusInfo(createInfo);
                        }
                        continue;
                      }
                      UNUSED_VARIABLE(deviceInfo);

                      // add to entry list
                      appendSpecialToEntryList(&createInfo->entryMsgQueue,
                                               ENTRY_TYPE_IMAGE,
                                               name
                                              );
                    }
                    break;
                }
              }
              break;
            default:
              printInfo(2,"Unknown type of file '%s' - skipped\n",String_cString(name));
              logMessage(LOG_TYPE_ENTRY_TYPE_UNKNOWN,"unknown type '%s'\n",String_cString(name));

              SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
              {
                createInfo->statusInfo.errorEntries++;
                createInfo->statusInfo.errorBytes += (uint64)fileInfo.size;
                updateStatusInfo(createInfo);
              }
              break;
          }
        }
        else
        {
          logMessage(LOG_TYPE_ENTRY_EXCLUDED,"excluded '%s' (no dump attribute)\n",String_cString(name));

          SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
          {
            createInfo->statusInfo.skippedEntries++;
            createInfo->statusInfo.skippedBytes += fileInfo.size;
            updateStatusInfo(createInfo);
          }
        }

        // free resources
      }
      else
      {
        logMessage(LOG_TYPE_ENTRY_EXCLUDED,"excluded '%s'\n",String_cString(name));

        SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
        {
          createInfo->statusInfo.skippedEntries++;
          createInfo->statusInfo.skippedBytes += fileInfo.size;
          updateStatusInfo(createInfo);
        }
      }
    }

    // next include entry
    includeEntryNode = includeEntryNode->next;
  }

  // add incomplete hard link entries (not all hard links found) to entry list
  Dictionary_initIterator(&dictionaryIterator,&hardLinksDictionary);
  while (Dictionary_getNext(&dictionaryIterator,
                            &keyData.value,
                            NULL,
                            &data.value,
                            NULL
                           )
        )
  {
    appendHardLinkToEntryList(&createInfo->entryMsgQueue,
                              ENTRY_TYPE_FILE,
                              &data.hardLinkInfo->nameList
                             );
  }
  Dictionary_doneIterator(&dictionaryIterator);

  // done
  MsgQueue_setEndOfMsg(&createInfo->entryMsgQueue);

  // free resoures
  Dictionary_done(&hardLinksDictionary,NULL,NULL);
  String_delete(fileName);
  String_delete(name);
  String_delete(basePath);
  StringList_done(&nameList);
  Dictionary_done(&duplicateNamesDictionary,NULL,NULL);
  AutoFree_done(&autoFreeList);
}

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : freeStorageMsg
* Purpose: free storage msg
* Input  : storageMsg - storage message
*          userData   - user data (ignored)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeStorageMsg(StorageMsg *storageMsg, void *userData)
{
  assert(storageMsg != NULL);

  UNUSED_VARIABLE(userData);

  String_delete(storageMsg->destinationFileName);
  String_delete(storageMsg->fileName);
}

/***********************************************************************\
* Name   : storageInfoIncrement
* Purpose: increment storage info
* Input  : createInfo - create info
*          size       - storage file size
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void storageInfoIncrement(CreateInfo *createInfo, uint64 size)
{
  SemaphoreLock semaphoreLock;

  assert(createInfo != NULL);

  SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->storageInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
  {
    createInfo->storageInfo.count += 1;
    createInfo->storageInfo.bytes += size;
  }
}

/***********************************************************************\
* Name   : storageInfoDecrement
* Purpose: decrement storage info
* Input  : createInfo - create info
*          size       - storage file size
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void storageInfoDecrement(CreateInfo *createInfo, uint64 size)
{
  SemaphoreLock semaphoreLock;

  assert(createInfo != NULL);

  SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->storageInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
  {
    assert(createInfo->storageInfo.count > 0);
    assert(createInfo->storageInfo.bytes >= size);

    createInfo->storageInfo.count -= 1;
    createInfo->storageInfo.bytes -= size;
  }
}

/***********************************************************************\
* Name   : storeArchiveFile
* Purpose: call back to store archive
* Input  : userData       - user data
*          databaseHandle - database handle or NULL if no database
*          storageId      - database id of storage
*          fileName       - archive file name
*          partNumber     - part number or ARCHIVE_PART_NUMBER_NONE for
*                           single part
*          lastPartFlag   - TRUE iff last archive part, FALSE otherwise
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors storeArchiveFile(void           *userData,
                              DatabaseHandle *databaseHandle,
                              int64          storageId,
                              String         tmpFileName,
                              int            partNumber,
                              bool           lastPartFlag
                             )
{
  CreateInfo    *createInfo = (CreateInfo*)userData;
  Errors        error;
  FileInfo      fileInfo;
  String        destinationFileName;
  String        storageName;
  String        printableStorageName;
  StorageMsg    storageMsg;
  SemaphoreLock semaphoreLock;

  assert(createInfo != NULL);
  assert(createInfo->storageSpecifier != NULL);
  assert(tmpFileName != NULL);
  assert(!String_isEmpty(tmpFileName));

  // get file info
// TODO replace by getFileSize()
  error = File_getFileInfo(&fileInfo,tmpFileName);
  if (error != ERROR_NONE)
  {
    return error;
  }

  // get destination file name, storage name, printable storage name
  destinationFileName  = String_new();
  storageName          = String_new();
  error = formatArchiveFileName(destinationFileName,
                                FORMAT_MODE_ARCHIVE_FILE_NAME,
                                createInfo->storageSpecifier->fileName,
                                createInfo->archiveType,
                                createInfo->scheduleTitle,
                                createInfo->scheduleCustomText,
                                createInfo->startTime,
                                partNumber,
                                lastPartFlag
                               );
  if (error != ERROR_NONE)
  {
    String_delete(storageName);
    String_delete(destinationFileName);
    return error;
  }
  String_set(storageName,Storage_getName(createInfo->storageSpecifier,destinationFileName));
  DEBUG_TESTCODE("storeArchiveFile1") { String_delete(storageName); String_delete(destinationFileName); return DEBUG_TESTCODE_ERROR(); }

  // set database storage name and uuid
  if (storageId != DATABASE_ID_NONE)
  {
    printableStorageName = Storage_getPrintableName(createInfo->storageSpecifier,destinationFileName);
    error = Index_update(indexDatabaseHandle,
                         storageId,
                         printableStorageName,
                         createInfo->uuid,
                         0LL   // size
                        );
    if (error != ERROR_NONE)
    {
      printError("Cannot update index for storage '%s' (error: %s)!\n",
                 String_cString(printableStorageName),
                 Error_getText(error)
                );
      return error;
    }
    DEBUG_TESTCODE("storeArchiveFile2") { String_delete(storageName); String_delete(destinationFileName); return DEBUG_TESTCODE_ERROR(); }
  }

  // send to storage controller
  storageMsg.databaseHandle      = databaseHandle;
  storageMsg.storageId           = storageId;
  storageMsg.fileName            = String_duplicate(tmpFileName);
  storageMsg.fileSize            = fileInfo.size;
  storageMsg.destinationFileName = destinationFileName;
  storageInfoIncrement(createInfo,fileInfo.size);
  if (!MsgQueue_put(&createInfo->storageMsgQueue,&storageMsg,sizeof(storageMsg)))
  {
    freeStorageMsg(&storageMsg,NULL);
    String_delete(storageName);
    String_delete(destinationFileName);
    return ERROR_NONE;
  }
  DEBUG_TESTCODE("storeArchiveFile3") { freeStorageMsg(&storageMsg,NULL); String_delete(destinationFileName); return DEBUG_TESTCODE_ERROR(); }

  // update status info
  SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
  {
    createInfo->statusInfo.storageTotalBytes += fileInfo.size;
    updateStatusInfo(createInfo);
  }

  // wait for space in temporary directory
  if (globalOptions.maxTmpSize > 0)
  {
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->storageInfoLock,SEMAPHORE_LOCK_TYPE_READ)
    {
      while (   (createInfo->storageInfo.count > 2)                           // more than 2 archives are waiting
             && (createInfo->storageInfo.bytes > globalOptions.maxTmpSize)    // temporary space above limit is used
             && !isAborted(createInfo)
            )
      {
        Semaphore_waitModified(&createInfo->storageInfoLock,30*1000);
      }
    }
  }

  // free resources
  String_delete(storageName);

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : storageThreadCode
* Purpose: archive storage thread
* Input  : createInfo - create info block
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void storageThreadCode(CreateInfo *createInfo)
{
  #define MAX_RETRIES 3

  AutoFreeList               autoFreeList;
  byte                       *buffer;
  String                     storageName;
  void                       *autoFreeSavePoint;
  StorageMsg                 storageMsg;
  Errors                     error;
  FileInfo                   fileInfo;
  FileHandle                 fileHandle;
  uint                       retryCount;
  ulong                      bufferLength;
  SemaphoreLock              semaphoreLock;
  String                     pattern;
  StorageSpecifier           storageDirectorySpecifier;
  IndexQueryHandle           indexQueryHandle;
  int64                      oldStorageId;
  String                     oldStorageName;
  StorageDirectoryListHandle storageDirectoryListHandle;
  String                     fileName;

  assert(createInfo != NULL);
  assert(createInfo->storageSpecifier != NULL);

  // init variables
  AutoFree_init(&autoFreeList);
  buffer = (byte*)malloc(BUFFER_SIZE);
  if (buffer == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  AUTOFREE_ADD(&autoFreeList,buffer,{ free(buffer); });

  // initial pre-processing
  if (!isAborted(createInfo))
  {
    if (createInfo->failError == ERROR_NONE)
    {
      // pause
      pauseStorage(createInfo);

      // initial pre-process
      error = Storage_preProcess(&createInfo->storageHandle,TRUE);
      if (error != ERROR_NONE)
      {
        printError("Cannot pre-process storage (error: %s)!\n",
                   Error_getText(error)
                  );
        createInfo->failError = error;
      }
    }
  }

  // store data
  storageName       = String_new();
  autoFreeSavePoint = AutoFree_save(&autoFreeList);
  while (   (createInfo->failError == ERROR_NONE)
         && !isAborted(createInfo)
         && MsgQueue_get(&createInfo->storageMsgQueue,&storageMsg,NULL,sizeof(storageMsg))
        )
  {
    AUTOFREE_ADD(&autoFreeList,&storageMsg,
                 {
                   storageInfoDecrement(createInfo,storageMsg.fileSize);
                   File_delete(storageMsg.fileName,FALSE);
                   if (storageMsg.storageId != DATABASE_ID_NONE) Index_delete(indexDatabaseHandle,storageMsg.storageId);
                   freeStorageMsg(&storageMsg,NULL);
                 }
                );

    // pause
    pauseStorage(createInfo);

    // pre-process
    error = Storage_preProcess(&createInfo->storageHandle,FALSE);
    if (error != ERROR_NONE)
    {
      printError("Cannot pre-process file '%s' (error: %s)!\n",
                 String_cString(storageMsg.fileName),
                 Error_getText(error)
                );
      createInfo->failError = error;

      AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
      continue;
    }
    DEBUG_TESTCODE("storageThreadCode1") { createInfo->failError = DEBUG_TESTCODE_ERROR(); break; }

    // get printable storage name
    String_set(storageName,Storage_getPrintableName(createInfo->storageSpecifier,storageMsg.destinationFileName));

    // get file info
    error = File_getFileInfo(&fileInfo,storageMsg.fileName);
    if (error != ERROR_NONE)
    {
      printError("Cannot get information for file '%s' (error: %s)!\n",
                 String_cString(storageMsg.fileName),
                 Error_getText(error)
                );
      createInfo->failError = error;

      AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
      continue;
    }
    DEBUG_TESTCODE("storageThreadCode2") { createInfo->failError = DEBUG_TESTCODE_ERROR(); break; }

    // open file to store
    #ifndef NDEBUG
      printInfo(1,"Store '%s' to '%s'...",String_cString(storageMsg.fileName),String_cString(storageName));
    #else /* not NDEBUG */
      printInfo(1,"Store archive '%s'...",String_cString(storageName));
    #endif /* NDEBUG */
    error = File_open(&fileHandle,storageMsg.fileName,FILE_OPEN_READ);
    if (error != ERROR_NONE)
    {
      printInfo(0,"FAIL!\n");
      printError("Cannot open file '%s' (error: %s)!\n",
                 String_cString(storageMsg.fileName),
                 Error_getText(error)
                );
      createInfo->failError = error;

      AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
      continue;
    }
    DEBUG_TESTCODE("storageThreadCode4") { createInfo->failError = DEBUG_TESTCODE_ERROR(); AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE); continue; }

    // write data to store file
    retryCount = 0;
    do
    {
      // next try
      if (retryCount > MAX_RETRIES)
      {
        break;
      }
      retryCount++;

      // pause
      pauseStorage(createInfo);

      // create storage file
      error = Storage_create(&createInfo->storageHandle,
                             storageMsg.destinationFileName,
                             fileInfo.size
                            );
      if (error != ERROR_NONE)
      {
        if (retryCount <= MAX_RETRIES)
        {
          // retry
          continue;
        }
        else
        {
          printInfo(0,"FAIL!\n");
          printError("Cannot store file '%s' (error: %s)\n",
                     String_cString(storageName),
                     Error_getText(error)
                    );
          break;
        }
      }
      DEBUG_TESTCODE("storageThreadCode5") { error = DEBUG_TESTCODE_ERROR(); break; }

      // update status info, check for abort
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        String_set(createInfo->statusInfo.storageName,storageName);
        updateStatusInfo(createInfo);
      }

      // store data
      File_seek(&fileHandle,0);
      do
      {
        // pause
        pauseStorage(createInfo);

        // read data from local file
        error = File_read(&fileHandle,buffer,BUFFER_SIZE,&bufferLength);
        if (error != ERROR_NONE)
        {
          printInfo(0,"FAIL!\n");
          printError("Cannot read file '%s' (error: %s)!\n",
                     String_cString(storageName),
                     Error_getText(error)
                    );
          break;
        }
        DEBUG_TESTCODE("storageThreadCode6") { error = DEBUG_TESTCODE_ERROR(); break; }

        // store data
        error = Storage_write(&createInfo->storageHandle,buffer,bufferLength);
        if (error != ERROR_NONE)
        {
          if (retryCount <= MAX_RETRIES)
          {
            // retry
            break;
          }
          else
          {
            printInfo(0,"FAIL!\n");
            printError("Cannot write file '%s' (error: %s)!\n",
                       String_cString(storageName),
                       Error_getText(error)
                      );
            break;
          }
        }
        DEBUG_TESTCODE("storageThreadCode7") { error = DEBUG_TESTCODE_ERROR(); break; }

        // update status info, check for abort
        SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
        {
          createInfo->statusInfo.storageDoneBytes += (uint64)bufferLength;
          updateStatusInfo(createInfo);
        }
      }
      while (   !File_eof(&fileHandle)
             && !isAborted(createInfo)
            );

      // close storage
      Storage_close(&createInfo->storageHandle);
    }
    while (   (error != ERROR_NONE)
           && (retryCount <= MAX_RETRIES)
           && !isAborted(createInfo)
          );
    if (error != ERROR_NONE)
    {
      createInfo->failError = error;

      File_close(&fileHandle);
      AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
      continue;
    }

    // close file to store
    File_close(&fileHandle);

    if (!isAborted(createInfo))
    {
      printInfo(1,"ok\n");
      logMessage(LOG_TYPE_STORAGE,"Stored '%s'\n",String_cString(storageName));
    }
    else
    {
      printInfo(1,"ABORTED\n");
    }

    // update index database and set state
    if (storageMsg.storageId != DATABASE_ID_NONE)
    {
      // delete old indizes for same storage file
      oldStorageName = String_new();
      error = Index_initListStorage(&indexQueryHandle,
                                    indexDatabaseHandle,
                                    STORAGE_TYPE_ANY,
                                    createInfo->storageSpecifier->hostName,
                                    createInfo->storageSpecifier->loginName,
                                    createInfo->storageSpecifier->deviceName,
                                    storageMsg.destinationFileName,
                                    INDEX_STATE_ALL
                                   );
      while (Index_getNextStorage(&indexQueryHandle,
                                  &oldStorageId,
                                  oldStorageName,
                                  NULL, // uuid
                                  NULL, // createdDateTime
                                  NULL, // size
                                  NULL, // indexState,
                                  NULL, // indexMode,
                                  NULL, // lastCheckedDateTime,
                                  NULL  // errorMessage
                                 )
            )
      {
        if (oldStorageId != storageMsg.storageId)
        {
          error = Index_delete(indexDatabaseHandle,oldStorageId);
          if (error != ERROR_NONE)
          {
            printError("Cannot delete old index for storage '%s' (error: %s)!\n",
                       String_cString(oldStorageName),
                       Error_getText(error)
                      );
            createInfo->failError = error;
            break;
          }
          DEBUG_TESTCODE("storageThreadCode8") { createInfo->failError = DEBUG_TESTCODE_ERROR(); break; }
        }
      }
      Index_doneList(&indexQueryHandle);
      String_delete(oldStorageName);
      if (createInfo->failError != ERROR_NONE)
      {
        AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
        continue;
      }

      // set database storage size
      error = Index_update(indexDatabaseHandle,
                           storageMsg.storageId,
                           NULL, // storageName
                           NULL, // uuid
                           fileInfo.size
                          );
      if (error != ERROR_NONE)
      {
        printError("Cannot update index for storage '%s' (error: %s)!\n",
                   String_cString(storageName),
                   Error_getText(error)
                  );
        createInfo->failError = error;

        AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
        continue;
      }
      DEBUG_TESTCODE("storageThreadCode9") { createInfo->failError = DEBUG_TESTCODE_ERROR(); }

      // set database state and time stamp
      error = Index_setState(indexDatabaseHandle,
                             storageMsg.storageId,
                             ((createInfo->failError == ERROR_NONE) && !isAborted(createInfo))
                               ? INDEX_STATE_OK
                               : INDEX_STATE_ERROR,
                             Misc_getCurrentDateTime(),
                             NULL // errorMessage
                            );
      if (error != ERROR_NONE)
      {
        printError("Cannot update index for storage '%s' (error: %s)!\n",
                   String_cString(storageName),
                   Error_getText(error)
                  );
        createInfo->failError = error;

        AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
        continue;
      }
      DEBUG_TESTCODE("storageThreadCode10") { createInfo->failError = DEBUG_TESTCODE_ERROR(); AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE); continue; }
    }

    // post-process
    error = Storage_postProcess(&createInfo->storageHandle,FALSE);
    if (error != ERROR_NONE)
    {
      printError("Cannot post-process storage file '%s' (error: %s)!\n",
                 String_cString(storageName),
                 Error_getText(error)
                );
      createInfo->failError = error;

      AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
      continue;
    }
    DEBUG_TESTCODE("storageThreadCode11") { createInfo->failError = DEBUG_TESTCODE_ERROR(); AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE); continue; }

    // check if aborted
    if (isAborted(createInfo))
    {
      AutoFree_restore(&autoFreeList,autoFreeSavePoint,TRUE);
      continue;
    }

    // add to list of stored storage files
    StringList_append(&createInfo->storageFileList,storageMsg.destinationFileName);

    // delete temporary storage file
    error = File_delete(storageMsg.fileName,FALSE);
    if (error != ERROR_NONE)
    {
      printWarning("Cannot delete file '%s' (error: %s)!\n",
                   String_cString(storageMsg.fileName),
                   Error_getText(error)
                  );
    }

    // update storage info
    storageInfoDecrement(createInfo,storageMsg.fileSize);

    // free resources
    freeStorageMsg(&storageMsg,NULL);
    AutoFree_restore(&autoFreeList,autoFreeSavePoint,FALSE);
  }
  String_delete(storageName);

  // final post-processing
  if (   !isAborted(createInfo)
      && (createInfo->failError == ERROR_NONE)
     )
  {
    // pause
    pauseStorage(createInfo);

    error = Storage_postProcess(&createInfo->storageHandle,TRUE);
    if (error != ERROR_NONE)
    {
      printError("Cannot post-process storage (error: %s)!\n",
                 Error_getText(error)
                );
      createInfo->failError = error;
    }
  }

  // delete old storage files
  if (   !isAborted(createInfo)
      && (createInfo->failError == ERROR_NONE)
     )
  {
    if (globalOptions.deleteOldArchiveFilesFlag)
    {
      // get archive name pattern
      pattern = String_new();
      error = formatArchiveFileName(pattern,
                                    FORMAT_MODE_PATTERN,
                                    createInfo->storageSpecifier->fileName,
                                    createInfo->archiveType,
                                    createInfo->scheduleTitle,
                                    createInfo->scheduleCustomText,
                                    createInfo->startTime,
                                    ARCHIVE_PART_NUMBER_NONE,
                                    FALSE
                                   );
      if (error == ERROR_NONE)
      {
        // open directory
        Storage_duplicateSpecifier(&storageDirectorySpecifier,createInfo->storageSpecifier);
        File_getFilePathName(storageDirectorySpecifier.fileName,createInfo->storageSpecifier->fileName);
        error = Storage_openDirectoryList(&storageDirectoryListHandle,
                                          &storageDirectorySpecifier,
                                          createInfo->jobOptions,
                                          SERVER_CONNECTION_PRIORITY_HIGH
                                         );
        if (error == ERROR_NONE)
        {
          // read directory
          fileName = String_new();
          while (   !Storage_endOfDirectoryList(&storageDirectoryListHandle)
                 && (Storage_readDirectoryList(&storageDirectoryListHandle,fileName,NULL) == ERROR_NONE)
                )
          {
            // find in storage list
            if (String_match(fileName,STRING_BEGIN,pattern,NULL,NULL))
            {
              if (StringList_find(&createInfo->storageFileList,fileName) == NULL)
              {
                Storage_delete(&createInfo->storageHandle,fileName);
              }
            }
          }
          String_delete(fileName);

          // close directory
          Storage_closeDirectoryList(&storageDirectoryListHandle);
        }
        Storage_doneSpecifier(&storageDirectorySpecifier);
      }
      String_delete(pattern);
    }
  }

  // free resoures
  free(buffer);
  AutoFree_done(&autoFreeList);

  createInfo->storageThreadExitFlag = TRUE;
}

/***********************************************************************\
* Name   : storeFileEntry
* Purpose: store a file entry into archive
* Input  : createInfo - create info structure
*          fileName   - file name to store
*          buffer     - buffer for temporary data
*          bufferSize - size of data buffer
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors storeFileEntry(CreateInfo   *createInfo,
                            const String fileName,
                            byte         *buffer,
                            uint         bufferSize
                           )
{
  Errors                    error;
  FileInfo                  fileInfo;
  FileExtendedAttributeList fileExtendedAttributeList;
  FileHandle                fileHandle;
  bool                      byteCompressFlag;
  bool                      deltaCompressFlag;
  ArchiveEntryInfo          archiveEntryInfo;
  SemaphoreLock             semaphoreLock;
  bool                      nameSemaphoreLocked;
  uint64                    entryDoneBytes;
  ulong                     bufferLength;
  uint64                    archiveSize;
  uint64                    doneBytes,archiveBytes;
  double                    compressionRatio;
  uint                      percentageDone;

  assert(createInfo != NULL);
  assert(createInfo->jobOptions != NULL);
  assert(fileName != NULL);
  assert(buffer != NULL);

  printInfo(1,"Add '%s'...",String_cString(fileName));

  // get file info, file extended attributes
  error = File_getFileInfo(&fileInfo,fileName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(fileName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get info for '%s' (error: %s)\n",
                 String_cString(fileName),
                 Error_getText(error)
                );
      return error;
    }
  }
  error = File_getExtendedAttributes(&fileExtendedAttributeList,fileName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(fileName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get extended attributes for '%s' (error: %s)\n",
                 String_cString(fileName),
                 Error_getText(error)
                );
      return error;
    }
  }

//fprintf(stderr,"%s, %d: ----------------\n",__FILE__,__LINE__);
//FileExtendedAttributeNode *fileExtendedAttributeNode; LIST_ITERATE(&fileExtendedAttributeList,fileExtendedAttributeNode) { fprintf(stderr,"%s, %d: fileExtendedAttributeNode=%s\n",__FILE__,__LINE__,String_cString(fileExtendedAttributeNode->name)); }

  // open file
  error = File_open(&fileHandle,fileName,FILE_OPEN_READ|FILE_OPEN_NO_CACHE);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"open file failed '%s'\n",String_cString(fileName));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
        createInfo->statusInfo.errorBytes += (uint64)fileInfo.size;
      }
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot open file '%s' (error: %s)\n",
                 String_cString(fileName),
                 Error_getText(error)
                );
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }
  }

  if (!createInfo->jobOptions->noStorageFlag)
  {
    // check if file data should be byte compressed
    byteCompressFlag =    (fileInfo.size > (int64)globalOptions.compressMinFileSize)
                       && !PatternList_match(createInfo->compressExcludePatternList,fileName,PATTERN_MATCH_MODE_EXACT);

    // check if file data should be delta compressed
    deltaCompressFlag = Compress_isCompressed(createInfo->jobOptions->compressAlgorithm.delta);

    // create new archive file entry
    error = Archive_newFileEntry(&archiveEntryInfo,
                                 &createInfo->archiveInfo,
                                 fileName,
                                 &fileInfo,
                                 &fileExtendedAttributeList,
                                 deltaCompressFlag,
                                 byteCompressFlag
                                );
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot create new archive file entry '%s' (error: %s)\n",
                 String_cString(fileName),
                 Error_getText(error)
                );
      (void)File_close(&fileHandle);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    // try to lock status name info
    nameSemaphoreLocked = Semaphore_lock(&createInfo->statusInfoNameLock,SEMAPHORE_LOCK_TYPE_READ_WRITE,SEMAPHORE_NO_WAIT);
    if (nameSemaphoreLocked)
    {
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        String_set(createInfo->statusInfo.name,fileName);
        createInfo->statusInfo.entryDoneBytes  = 0LL;
        createInfo->statusInfo.entryTotalBytes = fileInfo.size;
        updateStatusInfo(createInfo);
      }
    }

    // write file content to archive
    error          = ERROR_NONE;
    entryDoneBytes = 0LL;
    do
    {
      // pause
      pauseCreate(createInfo);

      // read file data
      error = File_read(&fileHandle,buffer,bufferSize,&bufferLength);
      if (error == ERROR_NONE)
      {
        // write data to archive
        if (bufferLength > 0L)
        {
          error = Archive_writeData(&archiveEntryInfo,buffer,bufferLength,1);
          if (error == ERROR_NONE)
          {
            entryDoneBytes += (uint64)bufferLength;
            archiveSize    = Archive_getSize(&createInfo->archiveInfo);

            // try to lock status name info
            if (!nameSemaphoreLocked)
            {
              nameSemaphoreLocked = Semaphore_lock(&createInfo->statusInfoNameLock,SEMAPHORE_LOCK_TYPE_READ_WRITE,SEMAPHORE_NO_WAIT);
            }

            // update status info
            SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
            {
              doneBytes        = createInfo->statusInfo.doneBytes+(uint64)bufferLength;
              archiveBytes     = createInfo->statusInfo.storageTotalBytes+archiveSize;
              compressionRatio = (!createInfo->jobOptions->dryRunFlag && (doneBytes > 0))
                                   ? 100.0-(archiveBytes*100.0)/doneBytes
                                   : 0.0;

              if (nameSemaphoreLocked)
              {
                String_set(createInfo->statusInfo.name,fileName);
                createInfo->statusInfo.entryDoneBytes  = entryDoneBytes;
                createInfo->statusInfo.entryTotalBytes = fileInfo.size;
              }
              createInfo->statusInfo.doneBytes        = doneBytes;
              createInfo->statusInfo.archiveBytes     = archiveBytes;
              createInfo->statusInfo.compressionRatio = compressionRatio;
              updateStatusInfo(createInfo);
            }
          }

          if (isPrintInfo(2))
          {
            percentageDone = 0;
            SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ)
            {
              percentageDone = (createInfo->statusInfo.entryTotalBytes > 0LL) ? (uint)((createInfo->statusInfo.entryDoneBytes*100LL)/createInfo->statusInfo.entryTotalBytes) : 100;
            }
            printInfo(2,"%3d%%\b\b\b\b",percentageDone);
          }
        }
      }
    }
    while (   !isAborted(createInfo)
           && (bufferLength > 0L)
           && (createInfo->failError == ERROR_NONE)
           && (error == ERROR_NONE)
          );
    if (isAborted(createInfo))
    {
      printInfo(1,"ABORTED\n");
      Archive_closeEntry(&archiveEntryInfo);
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      (void)File_close(&fileHandle);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return FALSE;
    }
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot store archive file (error: %s)!\n",
                 Error_getText(error)
                );
      Archive_closeEntry(&archiveEntryInfo);
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      (void)File_close(&fileHandle);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }
    printInfo(2,"    \b\b\b\b");

    // close archive entry
    error = Archive_closeEntry(&archiveEntryInfo);
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot close archive file entry (error: %s)!\n",
                 Error_getText(error)
                );
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      (void)File_close(&fileHandle);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    // unlock status name info
    if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);

    // close file
    (void)File_close(&fileHandle);

    // get final compression ratio
    if (archiveEntryInfo.file.chunkFileData.fragmentSize > 0LL)
    {
      compressionRatio = 100.0-archiveEntryInfo.file.chunkFileData.info.size*100.0/archiveEntryInfo.file.chunkFileData.fragmentSize;
    }
    else
    {
      compressionRatio = 0.0;
    }

    if (!createInfo->jobOptions->dryRunFlag)
    {
      printInfo(1,"ok (%llu bytes, ratio %.1f%%)\n",fileInfo.size,compressionRatio);
      logMessage(LOG_TYPE_ENTRY_OK,"added '%s'\n",String_cString(fileName));
    }
    else
    {
      printInfo(1,"ok (%llu bytes, dry-run)\n",fileInfo.size);
    }

    // update done entries
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
    {
      createInfo->statusInfo.doneEntries++;
      updateStatusInfo(createInfo);
    }
  }
  else
  {
    printInfo(1,"ok (%llu bytes, not stored)\n",fileInfo.size);
  }

  // free resources
  File_doneExtendedAttributes(&fileExtendedAttributeList);

  // add to incremental list
  if (createInfo->storeIncrementalFileInfoFlag)
  {
    addIncrementalList(&createInfo->namesDictionary,fileName,&fileInfo);
  }

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : storeImageEntry
* Purpose: store an image entry into archive
* Input  : createInfo - create info structure
*          deviceName - device name
*          buffer     - buffer for temporary data
*          bufferSize - size of data buffer
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors storeImageEntry(CreateInfo   *createInfo,
                             const String deviceName,
                             byte         *buffer,
                             uint         bufferSize
                            )
{
  Errors           error;
  DeviceInfo       deviceInfo;
  uint             maxBufferBlockCount;
  DeviceHandle     deviceHandle;
  bool             fileSystemFlag;
  FileSystemHandle fileSystemHandle;
  bool             byteCompressFlag;
  bool             deltaCompressFlag;
  SemaphoreLock    semaphoreLock;
  bool             nameSemaphoreLocked;
  uint64           entryDoneBytes;
  uint64           block;
  uint64           blockCount;
  uint             bufferBlockCount;
  uint64           archiveSize;
  uint64           doneBytes,archiveBytes;
  double           compressionRatio;
  uint             percentageDone;
  ArchiveEntryInfo archiveEntryInfo;

  assert(createInfo != NULL);
  assert(createInfo->jobOptions != NULL);
  assert(deviceName != NULL);
  assert(buffer != NULL);

  printInfo(1,"Add '%s'...",String_cString(deviceName));

  // get device info
  error = Device_getDeviceInfo(&deviceInfo,deviceName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(deviceName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot open device '%s' (error: %s)\n",
                 String_cString(deviceName),
                 Error_getText(error)
                );
      return error;
    }
  }

  // check device block size, get max. blocks in buffer
  if (deviceInfo.blockSize > bufferSize)
  {
    printInfo(1,"FAIL\n");
    printError("Device block size %llu on '%s' is too big (max: %llu)\n",
               deviceInfo.blockSize,
               String_cString(deviceName),
               bufferSize
              );
    return ERROR_INVALID_DEVICE_BLOCK_SIZE;
  }
  if (deviceInfo.blockSize <= 0)
  {
    printInfo(1,"FAIL\n");
    printError("Cannot get device block size for '%s'\n",
               String_cString(deviceName)
              );
    return ERROR_INVALID_DEVICE_BLOCK_SIZE;
  }
  assert(deviceInfo.blockSize > 0);
  maxBufferBlockCount = bufferSize/deviceInfo.blockSize;

  // open device
  error = Device_open(&deviceHandle,deviceName,DEVICE_OPEN_READ);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"open device failed '%s'\n",String_cString(deviceName));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
        createInfo->statusInfo.errorBytes += (uint64)deviceInfo.size;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot open device '%s' (error: %s)\n",
                 String_cString(deviceName),
                 Error_getText(error)
                );
      return error;
    }
  }

  // check if device contain a known file system or a raw image should be stored
  if (!createInfo->jobOptions->rawImagesFlag)
  {
    fileSystemFlag = (FileSystem_init(&fileSystemHandle,&deviceHandle) == ERROR_NONE);
  }
  else
  {
    fileSystemFlag = FALSE;
  }

  if (!createInfo->jobOptions->noStorageFlag)
  {
    // check if image data should be byte compressed
    byteCompressFlag =    (deviceInfo.size > (int64)globalOptions.compressMinFileSize)
                       && !PatternList_match(createInfo->compressExcludePatternList,deviceName,PATTERN_MATCH_MODE_EXACT);

    // check if file data should be delta compressed
    deltaCompressFlag = Compress_isCompressed(createInfo->jobOptions->compressAlgorithm.delta);

    // create new archive image entry
    error = Archive_newImageEntry(&archiveEntryInfo,
                                  &createInfo->archiveInfo,
                                  deviceName,
                                  &deviceInfo,
                                  deltaCompressFlag,
                                  byteCompressFlag
                                 );
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot create new archive image entry '%s' (error: %s)\n",
                 String_cString(deviceName),
                 Error_getText(error)
                );
      if (fileSystemFlag) FileSystem_done(&fileSystemHandle);
      Device_close(&deviceHandle);
      return error;
    }

    // try to lock status name info
    nameSemaphoreLocked = Semaphore_lock(&createInfo->statusInfoNameLock,SEMAPHORE_LOCK_TYPE_READ_WRITE,SEMAPHORE_NO_WAIT);
    if (nameSemaphoreLocked)
    {
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        String_set(createInfo->statusInfo.name,deviceName);
        createInfo->statusInfo.entryDoneBytes  = 0LL;
        createInfo->statusInfo.entryTotalBytes = deviceInfo.size;
        updateStatusInfo(createInfo);
      }
    }

    // write device content to archive
    block          = 0LL;
    blockCount     = deviceInfo.size/(uint64)deviceInfo.blockSize;
    error          = ERROR_NONE;
    entryDoneBytes = 0LL;
    while (   (block < blockCount)
           && !isAborted(createInfo)
           && (createInfo->failError == ERROR_NONE)
           && (error == ERROR_NONE)
          )
    {
      // pause
      pauseCreate(createInfo);

      // read blocks from device
      bufferBlockCount = 0;
      while (   (block < blockCount)
             && (bufferBlockCount < maxBufferBlockCount)
            )
      {
        if (   !fileSystemFlag
            || FileSystem_blockIsUsed(&fileSystemHandle,block*(uint64)deviceInfo.blockSize)
           )
        {
          // read single block
          error = Device_seek(&deviceHandle,block*(uint64)deviceInfo.blockSize);
          if (error != ERROR_NONE) break;
          error = Device_read(&deviceHandle,buffer+bufferBlockCount*deviceInfo.blockSize,deviceInfo.blockSize,NULL);
          if (error != ERROR_NONE) break;
        }
        else
        {
          // block not used -> store as "0"-block
          memset(buffer+bufferBlockCount*deviceInfo.blockSize,0,deviceInfo.blockSize);
        }
        bufferBlockCount++;
        block++;
      }
      if (error != ERROR_NONE) break;

      // write data to archive
      if (bufferBlockCount > 0)
      {
        error = Archive_writeData(&archiveEntryInfo,buffer,bufferBlockCount*deviceInfo.blockSize,deviceInfo.blockSize);
        if (error == ERROR_NONE)
        {
          entryDoneBytes += (uint64)bufferBlockCount*(uint64)deviceInfo.blockSize;
          archiveSize    = Archive_getSize(&createInfo->archiveInfo);

          // try to lock status name info
          if (!nameSemaphoreLocked)
          {
            nameSemaphoreLocked = Semaphore_lock(&createInfo->statusInfoNameLock,SEMAPHORE_LOCK_TYPE_READ_WRITE,SEMAPHORE_NO_WAIT);
          }

          // update status info
          SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
          {
            doneBytes        = createInfo->statusInfo.doneBytes+(uint64)bufferBlockCount*(uint64)deviceInfo.blockSize;
            archiveBytes     = createInfo->statusInfo.storageTotalBytes+archiveSize;
            compressionRatio = (!createInfo->jobOptions->dryRunFlag && (doneBytes > 0))
                                 ? 100.0-(archiveBytes*100.0)/doneBytes
                                 : 0.0;

            createInfo->statusInfo.doneBytes += (uint64)bufferBlockCount*(uint64)deviceInfo.blockSize;
            if (nameSemaphoreLocked)
            {
              String_set(createInfo->statusInfo.name,deviceName);
              createInfo->statusInfo.entryDoneBytes  = entryDoneBytes;
              createInfo->statusInfo.entryTotalBytes = deviceInfo.size;
            }
            createInfo->statusInfo.doneBytes        = doneBytes;
            createInfo->statusInfo.archiveBytes     = archiveBytes;
            createInfo->statusInfo.compressionRatio = compressionRatio;
            updateStatusInfo(createInfo);
          }
        }

        if (isPrintInfo(2))
        {
          percentageDone = 0;
          SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ)
          {
            percentageDone = (createInfo->statusInfo.entryTotalBytes > 0LL) ?  (uint)((createInfo->statusInfo.entryDoneBytes*100LL)/createInfo->statusInfo.entryTotalBytes) : 100;
          }
          printInfo(2,"%3d%%\b\b\b\b",percentageDone);
        }
      }
    }
    if (isAborted(createInfo))
    {
      printInfo(1,"ABORTED\n");
      Archive_closeEntry(&archiveEntryInfo);
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      if (fileSystemFlag) FileSystem_done(&fileSystemHandle);
      Device_close(&deviceHandle);
      return error;
    }
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot store archive file (error: %s)!\n",
                 Error_getText(error)
                );
      Archive_closeEntry(&archiveEntryInfo);
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      if (fileSystemFlag) FileSystem_done(&fileSystemHandle);
      Device_close(&deviceHandle);
      return error;
    }
    printInfo(2,"    \b\b\b\b");

    // close archive entry
    error = Archive_closeEntry(&archiveEntryInfo);
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot close archive image entry (error: %s)!\n",
                 Error_getText(error)
                );
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      if (fileSystemFlag) FileSystem_done(&fileSystemHandle);
      Device_close(&deviceHandle);
      return error;
    }

    // unlock status name info
    if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);

    // done file system
    if (fileSystemFlag)
    {
      FileSystem_done(&fileSystemHandle);
    }

    // get final compression ratio
    if (archiveEntryInfo.image.chunkImageData.blockCount > 0)
    {
      compressionRatio = 100.0-archiveEntryInfo.image.chunkImageData.info.size*100.0/(archiveEntryInfo.image.chunkImageData.blockCount*(uint64)deviceInfo.blockSize);
    }
    else
    {
      compressionRatio = 0.0;
    }

    if (!createInfo->jobOptions->dryRunFlag)
    {
      printInfo(1,"ok (%s, %llu bytes, ratio %.1f%%)\n",
                fileSystemFlag ? FileSystem_getName(fileSystemHandle.type) : "raw",
                deviceInfo.size,
                compressionRatio
               );
      logMessage(LOG_TYPE_ENTRY_OK,"added '%s'\n",String_cString(deviceName));
    }
    else
    {
      printInfo(1,"ok (%s, %llu bytes, dry-run)\n",
                fileSystemFlag ? FileSystem_getName(fileSystemHandle.type) : "raw",
                deviceInfo.size
               );
    }

    // update done entries
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
    {
      createInfo->statusInfo.doneEntries++;
      updateStatusInfo(createInfo);
    }
  }
  else
  {
    printInfo(1,"ok (%s, %llu bytes, not stored)\n",
              fileSystemFlag ? FileSystem_getName(fileSystemHandle.type) : "raw",
              deviceInfo.size
             );
  }

  // close device
  Device_close(&deviceHandle);

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : storeDirectoryEntry
* Purpose: store a directory entry into archive
* Input  : createInfo    - create info structure
*          directoryName - directory name to store
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors storeDirectoryEntry(CreateInfo   *createInfo,
                                 const String directoryName
                                )
{
  Errors                    error;
  FileInfo                  fileInfo;
  FileExtendedAttributeList fileExtendedAttributeList;
  ArchiveEntryInfo          archiveEntryInfo;
  SemaphoreLock             semaphoreLock;

  assert(createInfo != NULL);
  assert(createInfo->jobOptions != NULL);
  assert(directoryName != NULL);

  printInfo(1,"Add '%s'...",String_cString(directoryName));

  // get file info, file extended attributes
  error = File_getFileInfo(&fileInfo,directoryName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(directoryName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get info for '%s' (error: %s)\n",
                 String_cString(directoryName),
                 Error_getText(error)
                );
      return error;
    }
  }
  error = File_getExtendedAttributes(&fileExtendedAttributeList,directoryName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(directoryName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get extended attributes for '%s' (error: %s)\n",
                 String_cString(directoryName),
                 Error_getText(error)
                );
      return error;
    }
  }

  if (!createInfo->jobOptions->noStorageFlag)
  {
    // new directory
    error = Archive_newDirectoryEntry(&archiveEntryInfo,
                                      &createInfo->archiveInfo,
                                      directoryName,
                                      &fileInfo,
                                      &fileExtendedAttributeList
                                     );
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot create new archive directory entry '%s' (error: %s)\n",
                 String_cString(directoryName),
                 Error_getText(error)
                );
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"open failed '%s'\n",String_cString(directoryName));
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    // close archive entry
    error = Archive_closeEntry(&archiveEntryInfo);
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot close archive directory entry (error: %s)!\n",
                 Error_getText(error)
                );
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    if (!createInfo->jobOptions->dryRunFlag)
    {
      printInfo(1,"ok\n");
      logMessage(LOG_TYPE_ENTRY_OK,"added '%s'\n",String_cString(directoryName));
    }
    else
    {
      printInfo(1,"ok (dry-run)\n");
    }

    // update done entries
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
    {
      createInfo->statusInfo.doneEntries++;
      updateStatusInfo(createInfo);
    }
  }
  else
  {
    printInfo(1,"ok (not stored)\n");
  }

  // free resources
  File_doneExtendedAttributes(&fileExtendedAttributeList);

  // add to incremental list
  if (createInfo->storeIncrementalFileInfoFlag)
  {
    addIncrementalList(&createInfo->namesDictionary,directoryName,&fileInfo);
  }

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : storeLinkEntry
* Purpose: store a link entry into archive
* Input  : createInfo - create info structure
*          linkName   - link name to store
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors storeLinkEntry(CreateInfo   *createInfo,
                            const String linkName
                           )
{
  Errors                    error;
  FileInfo                  fileInfo;
  FileExtendedAttributeList fileExtendedAttributeList;
  String                    fileName;
  ArchiveEntryInfo          archiveEntryInfo;
  SemaphoreLock             semaphoreLock;

  assert(createInfo != NULL);
  assert(linkName != NULL);

  printInfo(1,"Add '%s'...",String_cString(linkName));

  // get file info, file extended attributes
  error = File_getFileInfo(&fileInfo,linkName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(linkName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get info for '%s' (error: %s)\n",
                 String_cString(linkName),
                 Error_getText(error)
                );
      return error;
    }
  }
  error = File_getExtendedAttributes(&fileExtendedAttributeList,linkName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(linkName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get extended attributes for '%s' (error: %s)\n",
                 String_cString(linkName),
                 Error_getText(error)
                );
      return error;
    }
  }

  if (!createInfo->jobOptions->noStorageFlag)
  {
    // read link
    fileName = String_new();
    error = File_readLink(fileName,linkName);
    if (error != ERROR_NONE)
    {
      if (createInfo->jobOptions->skipUnreadableFlag)
      {
        printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
        logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"open failed '%s'\n",String_cString(linkName));
        SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
        {
          createInfo->statusInfo.errorEntries++;
          createInfo->statusInfo.errorBytes += (uint64)fileInfo.size;
        }
        String_delete(fileName);
        File_doneExtendedAttributes(&fileExtendedAttributeList);
        return ERROR_NONE;
      }
      else
      {
        printInfo(1,"FAIL\n");
        printError("Cannot read link '%s' (error: %s)\n",
                   String_cString(linkName),
                   Error_getText(error)
                  );
        String_delete(fileName);
        File_doneExtendedAttributes(&fileExtendedAttributeList);
        return error;
      }
    }

    // new link
    error = Archive_newLinkEntry(&archiveEntryInfo,
                                 &createInfo->archiveInfo,
                                 linkName,
                                 fileName,
                                 &fileInfo,
                                 &fileExtendedAttributeList
                                );
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot create new archive link entry '%s' (error: %s)\n",
                 String_cString(linkName),
                 Error_getText(error)
                );
      String_delete(fileName);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    // close archive entry
    error = Archive_closeEntry(&archiveEntryInfo);
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot close archive link entry (error: %s)!\n",
                 Error_getText(error)
                );
      String_delete(fileName);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    if (!createInfo->jobOptions->dryRunFlag)
    {
      printInfo(1,"ok\n");
      logMessage(LOG_TYPE_ENTRY_OK,"added '%s'\n",String_cString(linkName));
    }
    else
    {
      printInfo(1,"ok (dry-run)\n");
    }

    // update done entries
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
    {
      createInfo->statusInfo.doneEntries++;
      updateStatusInfo(createInfo);
    }

    // free resources
    String_delete(fileName);
  }
  else
  {
    printInfo(1,"ok (not stored)\n");
  }

  // free resources
  File_doneExtendedAttributes(&fileExtendedAttributeList);

  // add to incremental list
  if (createInfo->storeIncrementalFileInfoFlag)
  {
    addIncrementalList(&createInfo->namesDictionary,linkName,&fileInfo);
  }

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : storeHardLinkEntry
* Purpose: store a hard link entry into archive
* Input  : createInfo  - create info structure
*          nameList    - hard link name list to store
*          buffer      - buffer for temporary data
*          bufferSize  - size of data buffer
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors storeHardLinkEntry(CreateInfo       *createInfo,
                                const StringList *nameList,
                                byte             *buffer,
                                uint             bufferSize
                               )
{
  Errors                    error;
  FileInfo                  fileInfo;
  FileExtendedAttributeList fileExtendedAttributeList;
  FileHandle                fileHandle;
  bool                      byteCompressFlag;
  bool                      deltaCompressFlag;
  ArchiveEntryInfo          archiveEntryInfo;
  SemaphoreLock             semaphoreLock;
  bool                      nameSemaphoreLocked;
  uint64                    entryDoneBytes;
  ulong                     bufferLength;
  uint64                    archiveSize;
  uint64                    doneBytes,archiveBytes;
  double                    compressionRatio;
  uint                      percentageDone;
  const StringNode          *stringNode;
  String                    name;

  assert(createInfo != NULL);
  assert(nameList != NULL);
  assert(!StringList_isEmpty(nameList));
  assert(buffer != NULL);

  printInfo(1,"Add '%s'...",String_cString(StringList_first(nameList,NULL)));

  // get file info, file extended attributes
  error = File_getFileInfo(&fileInfo,StringList_first(nameList,NULL));
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(StringList_first(nameList,NULL)),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries += StringList_count(nameList);
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get info for '%s' (error: %s)\n",
                 String_cString(StringList_first(nameList,NULL)),
                 Error_getText(error)
                );
      return error;
    }
  }
  error = File_getExtendedAttributes(&fileExtendedAttributeList,StringList_first(nameList,NULL));
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(StringList_first(nameList,NULL)),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get extended attributes for '%s' (error: %s)\n",
                 String_cString(StringList_first(nameList,NULL)),
                 Error_getText(error)
                );
      return error;
    }
  }

  // open file
  error = File_open(&fileHandle,StringList_first(nameList,NULL),FILE_OPEN_READ|FILE_OPEN_NO_CACHE);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"open file failed '%s'\n",String_cString(StringList_first(nameList,NULL)));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries += StringList_count(nameList);
        createInfo->statusInfo.errorBytes += (uint64)StringList_count(nameList)*(uint64)fileInfo.size;
      }
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot open file '%s' (error: %s)\n",
                 String_cString(StringList_first(nameList,NULL)),
                 Error_getText(error)
                );
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }
  }

  if (!createInfo->jobOptions->noStorageFlag)
  {
    // check if file data should be byte compressed
    byteCompressFlag =    (fileInfo.size > (int64)globalOptions.compressMinFileSize)
                       && !PatternList_matchStringList(createInfo->compressExcludePatternList,nameList,PATTERN_MATCH_MODE_EXACT);

    // check if file data should be delta compressed
    deltaCompressFlag = Compress_isCompressed(createInfo->jobOptions->compressAlgorithm.delta);

    // create new archive hard link entry
    error = Archive_newHardLinkEntry(&archiveEntryInfo,
                                     &createInfo->archiveInfo,
                                     nameList,
                                     &fileInfo,
                                     &fileExtendedAttributeList,
                                     deltaCompressFlag,
                                     byteCompressFlag
                                    );
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot create new archive file entry '%s' (error: %s)\n",
                 String_cString(StringList_first(nameList,NULL)),
                 Error_getText(error)
                );
      (void)File_close(&fileHandle);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    // try to lock status name info
    nameSemaphoreLocked = Semaphore_lock(&createInfo->statusInfoNameLock,SEMAPHORE_LOCK_TYPE_READ_WRITE,SEMAPHORE_NO_WAIT);
    if (nameSemaphoreLocked)
    {
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        String_set(createInfo->statusInfo.name,StringList_first(nameList,NULL));
        createInfo->statusInfo.entryDoneBytes  = 0LL;
        createInfo->statusInfo.entryTotalBytes = fileInfo.size;
        updateStatusInfo(createInfo);
      }
    }

    // write hard link content to archive
    error          = ERROR_NONE;
    entryDoneBytes = 0LL;
    do
    {
      // pause
      pauseCreate(createInfo);

      // read file data
      error = File_read(&fileHandle,buffer,bufferSize,&bufferLength);
      if (error == ERROR_NONE)
      {
        // write data to archive
        if (bufferLength > 0L)
        {
          error = Archive_writeData(&archiveEntryInfo,buffer,bufferLength,1);
          if (error == ERROR_NONE)
          {
            entryDoneBytes += (uint64)StringList_count(nameList)*(uint64)bufferLength;
            archiveSize = Archive_getSize(&createInfo->archiveInfo);

            SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
            {
              doneBytes        = createInfo->statusInfo.doneBytes+(uint64)bufferLength;
              archiveBytes     = createInfo->statusInfo.storageTotalBytes+archiveSize;
              compressionRatio = (!createInfo->jobOptions->dryRunFlag && (doneBytes > 0))
                                   ? 100.0-(archiveBytes*100.0)/doneBytes
                                   : 0.0;

              createInfo->statusInfo.doneBytes += (uint64)StringList_count(nameList)*(uint64)bufferLength;
              if (nameSemaphoreLocked)
              {
                String_set(createInfo->statusInfo.name,StringList_first(nameList,NULL));
                createInfo->statusInfo.entryDoneBytes  = entryDoneBytes;
                createInfo->statusInfo.entryTotalBytes = fileInfo.size;
              }
              createInfo->statusInfo.doneBytes        = doneBytes;
              createInfo->statusInfo.archiveBytes     = archiveBytes;
              createInfo->statusInfo.compressionRatio = compressionRatio;
              updateStatusInfo(createInfo);
            }
          }

          if (isPrintInfo(2))
          {
            percentageDone = 0;
            SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ)
            {
              percentageDone = (createInfo->statusInfo.entryTotalBytes > 0LL) ? (uint)((createInfo->statusInfo.entryDoneBytes*100LL)/createInfo->statusInfo.entryTotalBytes) : 100;
            }
            printInfo(2,"%3d%%\b\b\b\b",percentageDone);
          }
        }
      }
    }
    while (   !isAborted(createInfo)
           && (bufferLength > 0L)
           && (createInfo->failError == ERROR_NONE)
           && (error == ERROR_NONE)
          );
    if (isAborted(createInfo))
    {
      printInfo(1,"ABORTED\n");
      Archive_closeEntry(&archiveEntryInfo);
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      (void)File_close(&fileHandle);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot store archive file (error: %s)!\n",
                 Error_getText(error)
                );
      Archive_closeEntry(&archiveEntryInfo);
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      (void)File_close(&fileHandle);
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }
    printInfo(2,"    \b\b\b\b");

    // close archive entry
    error = Archive_closeEntry(&archiveEntryInfo);
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot close archive file entry (error: %s)!\n",
                 Error_getText(error)
                );
      if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);
      (void)File_close(&fileHandle);
      return error;
    }

    // unlock status name info
    if (nameSemaphoreLocked) Semaphore_unlock(&createInfo->statusInfoNameLock);

    // close file
    (void)File_close(&fileHandle);

    // get final compression ratio
    if (archiveEntryInfo.hardLink.chunkHardLinkData.fragmentSize > 0LL)
    {
      compressionRatio = 100.0-archiveEntryInfo.hardLink.chunkHardLinkData.info.size*100.0/archiveEntryInfo.hardLink.chunkHardLinkData.fragmentSize;
    }
    else
    {
      compressionRatio = 0.0;
    }

    if (!createInfo->jobOptions->dryRunFlag)
    {
      printInfo(1,"ok (%llu bytes, ratio %.1f%%)\n",
                fileInfo.size,
                compressionRatio
               );
      logMessage(LOG_TYPE_ENTRY_OK,"added '%s'\n",String_cString(StringList_first(nameList,NULL)));
    }
    else
    {
      printInfo(1,"ok (%llu bytes, dry-run)\n",fileInfo.size);
    }

    // update done entries
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
    {
      createInfo->statusInfo.doneEntries += StringList_count(nameList);
      updateStatusInfo(createInfo);
    }
  }
  else
  {
    printInfo(1,"ok (%llu bytes, not stored)\n",fileInfo.size);
  }

  // free resources
  File_doneExtendedAttributes(&fileExtendedAttributeList);

  // add to incremental list
  if (createInfo->storeIncrementalFileInfoFlag)
  {
    STRINGLIST_ITERATE(nameList,stringNode,name)
    {
      addIncrementalList(&createInfo->namesDictionary,name,&fileInfo);
    }
  }

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : storeSpecialEntry
* Purpose: store a special entry into archive
* Input  : createInfo - create info structure
*          fileName   - file name to store
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors storeSpecialEntry(CreateInfo   *createInfo,
                               const String fileName
                              )
{
  Errors                    error;
  FileInfo                  fileInfo;
  FileExtendedAttributeList fileExtendedAttributeList;
  ArchiveEntryInfo          archiveEntryInfo;
  SemaphoreLock             semaphoreLock;

  assert(createInfo != NULL);
  assert(createInfo->jobOptions != NULL);
  assert(fileName != NULL);

  printInfo(1,"Add '%s'...",String_cString(fileName));

  // get file info, file extended attributes
  error = File_getFileInfo(&fileInfo,fileName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(fileName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get info for '%s' (error: %s)\n",
                 String_cString(fileName),
                 Error_getText(error)
                );
      return error;
    }
  }
  error = File_getExtendedAttributes(&fileExtendedAttributeList,fileName);
  if (error != ERROR_NONE)
  {
    if (createInfo->jobOptions->skipUnreadableFlag)
    {
      printInfo(1,"skipped (reason: %s)\n",Error_getText(error));
      logMessage(LOG_TYPE_ENTRY_ACCESS_DENIED,"access denied '%s' (error: %s)\n",String_cString(fileName),Error_getText(error));
      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.errorEntries++;
      }
      return ERROR_NONE;
    }
    else
    {
      printInfo(1,"FAIL\n");
      printError("Cannot get extended attributes for '%s' (error: %s)\n",
                 String_cString(fileName),
                 Error_getText(error)
                );
      return error;
    }
  }

  if (!createInfo->jobOptions->noStorageFlag)
  {
    // new special
    error = Archive_newSpecialEntry(&archiveEntryInfo,
                                    &createInfo->archiveInfo,
                                    fileName,
                                    &fileInfo,
                                    &fileExtendedAttributeList
                                   );
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot create new archive special entry '%s' (error: %s)\n",
                 String_cString(fileName),
                 Error_getText(error)
                );
      File_doneExtendedAttributes(&fileExtendedAttributeList);
      return error;
    }

    // close archive entry
    error = Archive_closeEntry(&archiveEntryInfo);
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot close archive special entry (error: %s)!\n",
                 Error_getText(error)
                );
      return error;
    }

    if (!createInfo->jobOptions->dryRunFlag)
    {
      printInfo(1,"ok\n");
      logMessage(LOG_TYPE_ENTRY_OK,"added '%s'\n",String_cString(fileName));
    }
    else
    {
      printInfo(1,"ok (dry-run)\n");
    }

    // update done entries
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
    {
      createInfo->statusInfo.doneEntries++;
      updateStatusInfo(createInfo);
    }
  }
  else
  {
    printInfo(1,"ok (not stored)\n");
  }

  // free resources
  File_doneExtendedAttributes(&fileExtendedAttributeList);

  // add to incremental list
  if (createInfo->storeIncrementalFileInfoFlag)
  {
    addIncrementalList(&createInfo->namesDictionary,fileName,&fileInfo);
  }

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : createThreadCode
* Purpose: create worker thread
* Input  : createInfo - create info structure
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void createThreadCode(CreateInfo *createInfo)
{
  byte             *buffer;
  EntryMsg         entryMsg;
  bool             ownFileFlag;
  const StringNode *stringNode;
  String           name;
  Errors           error;
  SemaphoreLock    semaphoreLock;

  assert(createInfo != NULL);

  // allocate buffer
  buffer = (byte*)malloc(BUFFER_SIZE);
  if (buffer == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  // store files
  while (   (createInfo->failError == ERROR_NONE)
         && !isAborted(createInfo)
         && MsgQueue_get(&createInfo->entryMsgQueue,&entryMsg,NULL,sizeof(entryMsg))
        )
  {
    // pause
    pauseCreate(createInfo);

    // check if own file (in temporary directory or storage file)
    ownFileFlag =    String_startsWith(entryMsg.name,tmpDirectory)
                  || StringList_contain(&createInfo->storageFileList,entryMsg.name);
    if (!ownFileFlag)
    {
      STRINGLIST_ITERATE(&entryMsg.nameList,stringNode,name)
      {
        ownFileFlag =    String_startsWith(name,tmpDirectory)
                      || StringList_contain(&createInfo->storageFileList,name);
        if (ownFileFlag) break;
      }
    }

    if (!ownFileFlag)
    {
      switch (entryMsg.fileType)
      {
        case FILE_TYPE_FILE:
          switch (entryMsg.entryType)
          {
            case ENTRY_TYPE_FILE:
              error = storeFileEntry(createInfo,
                                     entryMsg.name,
                                     buffer,
                                     BUFFER_SIZE
                                    );
              if (error != ERROR_NONE) createInfo->failError = error;
              break;
            case ENTRY_TYPE_IMAGE:
              break;
          }
          break;
        case FILE_TYPE_DIRECTORY:
          switch (entryMsg.entryType)
          {
            case ENTRY_TYPE_FILE:
              error = storeDirectoryEntry(createInfo,
                                          entryMsg.name
                                         );
              if (error != ERROR_NONE) createInfo->failError = error;
              break;
            case ENTRY_TYPE_IMAGE:
              break;
          }
          break;
        case FILE_TYPE_LINK:
          switch (entryMsg.entryType)
          {
            case ENTRY_TYPE_FILE:
              error = storeLinkEntry(createInfo,
                                     entryMsg.name
                                    );
              if (error != ERROR_NONE) createInfo->failError = error;
              break;
            case ENTRY_TYPE_IMAGE:
              break;
          }
          break;
        case FILE_TYPE_HARDLINK:
          switch (entryMsg.entryType)
          {
            case ENTRY_TYPE_FILE:
              error = storeHardLinkEntry(createInfo,
                                         &entryMsg.nameList,
                                         buffer,
                                         BUFFER_SIZE
                                        );
              if (error != ERROR_NONE) createInfo->failError = error;
              break;
            case ENTRY_TYPE_IMAGE:
              break;
          }
          break;
        case FILE_TYPE_SPECIAL:
          switch (entryMsg.entryType)
          {
            case ENTRY_TYPE_FILE:
              error = storeSpecialEntry(createInfo,
                                        entryMsg.name
                                       );
              if (error != ERROR_NONE) createInfo->failError = error;
              break;
            case ENTRY_TYPE_IMAGE:
              error = storeImageEntry(createInfo,
                                      entryMsg.name,
                                      buffer,
                                      BUFFER_SIZE
                                     );
              if (error != ERROR_NONE) createInfo->failError = error;
              break;
          }
          break;
        default:
          #ifndef NDEBUG
            HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          #endif /* NDEBUG */
          break; /* not reached */
      }
    }
    else
    {
      printInfo(1,"Add '%s'...skipped (reason: own created file)\n",String_cString(entryMsg.name));

      SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
      {
        createInfo->statusInfo.skippedEntries++;
      }
    }

    // update status info and check if aborted
    SEMAPHORE_LOCKED_DO(semaphoreLock,&createInfo->statusInfoLock,SEMAPHORE_LOCK_TYPE_READ_WRITE)
    {
      updateStatusInfo(createInfo);
    }

    // free entry message
    freeEntryMsg(&entryMsg,NULL);

// NYI: is this really useful? (avoid that sum-collector-thread is slower than file-collector-thread)
    // slow down if too fast
    while (   !createInfo->collectorSumThreadExitedFlag
           && (createInfo->statusInfo.doneEntries >= createInfo->statusInfo.totalEntries)
          )
    {
      Misc_udelay(1000*1000);
    }
  }

  // free resources
  free(buffer);
}

/*---------------------------------------------------------------------*/

Errors Command_create(const String                    storageName,
                      const String                    uuid,
                      const EntryList                 *includeEntryList,
                      const PatternList               *excludePatternList,
                      const PatternList               *compressExcludePatternList,
                      JobOptions                      *jobOptions,
                      ArchiveTypes                    archiveType,
                      const String                    scheduleTitle,
                      const String                    scheduleCustomText,
                      ArchiveGetCryptPasswordFunction archiveGetCryptPasswordFunction,
                      void                            *archiveGetCryptPasswordUserData,
                      CreateStatusInfoFunction        createStatusInfoFunction,
                      void                            *createStatusInfoUserData,
                      StorageRequestVolumeFunction    storageRequestVolumeFunction,
                      void                            *storageRequestVolumeUserData,
                      bool                            *pauseCreateFlag,
                      bool                            *pauseStorageFlag,
                      bool                            *requestedAbortFlag
                     )
{
  AutoFreeList     autoFreeList;
  StorageSpecifier storageSpecifier;
  CreateInfo       createInfo;
  Thread           collectorSumThread;                 // files collector sum thread
  Thread           collectorThread;                    // files collector thread
  Thread           storageThread;                      // storage thread
  Thread           *createThreads;
  uint             createThreadCount;
  uint             z;
  Errors           error;
  String           incrementalListFileName;
  bool             useIncrementalFileInfoFlag;
  bool             incrementalFileInfoExistFlag;

  assert(storageName != NULL);
  assert(includeEntryList != NULL);
  assert(excludePatternList != NULL);

  // init variables
  AutoFree_init(&autoFreeList);
  incrementalListFileName      = NULL;
  useIncrementalFileInfoFlag   = FALSE;
  incrementalFileInfoExistFlag = FALSE;

  // check if storage name given
  if (String_isEmpty(storageName))
  {
    printError("No storage name given\n");
    AutoFree_cleanup(&autoFreeList);
    return ERROR_NO_STORAGE_NAME;
  }

  // parse storage name
  Storage_initSpecifier(&storageSpecifier);
  error = Storage_parseName(&storageSpecifier,storageName);
  if (error != ERROR_NONE)
  {
    printError("Cannot initialize storage '%s' (error: %s)\n",
               String_cString(storageName),
               Error_getText(error)
              );
    Storage_doneSpecifier(&storageSpecifier);
    AutoFree_cleanup(&autoFreeList);
    return error;
  }
  DEBUG_TESTCODE("Command_create1") { Storage_doneSpecifier(&storageSpecifier); AutoFree_cleanup(&autoFreeList); return DEBUG_TESTCODE_ERROR(); }
  AUTOFREE_ADD(&autoFreeList,&storageSpecifier,{ Storage_doneSpecifier(&storageSpecifier); });

  // init threads
  initCreateInfo(&createInfo,
                 &storageSpecifier,
                 uuid,
                 includeEntryList,
                 excludePatternList,
                 compressExcludePatternList,
                 jobOptions,
                 archiveType,
                 scheduleTitle,
                 scheduleCustomText,
                 CALLBACK(createStatusInfoFunction,createStatusInfoUserData),
                 pauseCreateFlag,
                 pauseStorageFlag,
                 requestedAbortFlag
                );
  createThreadCount = (globalOptions.maxThreads != 0) ? globalOptions.maxThreads : Thread_getNumberOfCores();
  createThreads = (Thread*)malloc(createThreadCount*sizeof(Thread));
  if (createThreads == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  AUTOFREE_ADD(&autoFreeList,&createInfo,{ doneCreateInfo(&createInfo); });
  AUTOFREE_ADD(&autoFreeList,createThreads,{ free(createThreads); });

  // init storage
  error = Storage_init(&createInfo.storageHandle,
                       createInfo.storageSpecifier,
                       createInfo.jobOptions,
                       &globalOptions.maxBandWidthList,
                       SERVER_CONNECTION_PRIORITY_HIGH,
                       CALLBACK(storageRequestVolumeFunction,storageRequestVolumeUserData),
                       CALLBACK((StorageStatusInfoFunction)updateStorageStatusInfo,&createInfo)
                      );
  if (error != ERROR_NONE)
  {
    printError("Cannot initialize storage '%s' (error: %s)\n",
               Storage_getPrintableNameCString(createInfo.storageSpecifier,NULL),
               Error_getText(error)
              );
    AutoFree_cleanup(&autoFreeList);
    return error;
  }
  DEBUG_TESTCODE("Command_create2") { Storage_done(&createInfo.storageHandle); AutoFree_cleanup(&autoFreeList); return DEBUG_TESTCODE_ERROR(); }
  AUTOFREE_ADD(&autoFreeList,&createInfo.storageHandle,{ Storage_done(&createInfo.storageHandle); });

  if (   (createInfo.archiveType == ARCHIVE_TYPE_FULL)
      || (createInfo.archiveType == ARCHIVE_TYPE_INCREMENTAL)
      || (createInfo.archiveType == ARCHIVE_TYPE_DIFFERENTIAL)
      || !String_isEmpty(jobOptions->incrementalListFileName)
     )
  {
    // get increment list file name
    incrementalListFileName = String_new();
    if (!String_isEmpty(jobOptions->incrementalListFileName))
    {
      String_set(incrementalListFileName,jobOptions->incrementalListFileName);
    }
    else
    {
      formatIncrementalFileName(incrementalListFileName,
                                createInfo.storageSpecifier
                               );
    }
    Dictionary_init(&createInfo.namesDictionary,NULL,NULL);
    AUTOFREE_ADD(&autoFreeList,incrementalListFileName,{ String_delete(incrementalListFileName); });
    AUTOFREE_ADD(&autoFreeList,&createInfo.namesDictionary,{ Dictionary_done(&createInfo.namesDictionary,NULL,NULL); });

    // read incremental list
    incrementalFileInfoExistFlag = File_exists(incrementalListFileName);
    if (   (   (createInfo.archiveType == ARCHIVE_TYPE_INCREMENTAL )
            || (createInfo.archiveType == ARCHIVE_TYPE_DIFFERENTIAL)
           )
        && incrementalFileInfoExistFlag
       )
    {
      printInfo(1,"Read incremental list '%s'...",String_cString(incrementalListFileName));
      error = readIncrementalList(incrementalListFileName,
                                  &createInfo.namesDictionary
                                 );
      if (error != ERROR_NONE)
      {
        printInfo(1,"FAIL!\n");
        printError("Cannot read incremental list file '%s' (error: %s)\n",
                   String_cString(incrementalListFileName),
                   Error_getText(error)
                  );
#if 0
// NYI: must index be deleted on error?
        if (   (indexDatabaseHandle != NULL)
            && !archiveInfo->jobOptions->noIndexDatabaseFlag
            && !archiveInfo->jobOptions->dryRunFlag
            && !archiveInfo->jobOptions->noStorageFlag
           )
        {
          Storage_indexDiscard(&createInfo.storageIndexHandle);
        }
#endif /* 0 */
        AutoFree_cleanup(&autoFreeList);
        return error;
      }
      DEBUG_TESTCODE("Command_create3") { AutoFree_cleanup(&autoFreeList); return DEBUG_TESTCODE_ERROR(); }
      printInfo(1,
                "ok (%lu entries)\n",
                Dictionary_count(&createInfo.namesDictionary)
               );
    }

    useIncrementalFileInfoFlag              = TRUE;
    createInfo.storeIncrementalFileInfoFlag =    (createInfo.archiveType == ARCHIVE_TYPE_FULL)
                                              || (createInfo.archiveType == ARCHIVE_TYPE_INCREMENTAL);
  }

  // start collector and storage threads
  if (!Thread_init(&collectorSumThread,"BAR collector sum",globalOptions.niceLevel,collectorSumThreadCode,&createInfo))
  {
    HALT_FATAL_ERROR("Cannot initialize collector sum thread!");
  }
  if (!Thread_init(&collectorThread,"BAR collector",globalOptions.niceLevel,collectorThreadCode,&createInfo))
  {
    HALT_FATAL_ERROR("Cannot initialize collector thread!");
  }
  if (!Thread_init(&storageThread,"BAR storage",globalOptions.niceLevel,storageThreadCode,&createInfo))
  {
    HALT_FATAL_ERROR("Cannot initialize storage thread!");
  }
  AUTOFREE_ADD(&autoFreeList,&collectorSumThread,{ MsgQueue_setEndOfMsg(&createInfo.entryMsgQueue); Thread_join(&collectorSumThread); Thread_done(&collectorSumThread); });
  AUTOFREE_ADD(&autoFreeList,&collectorThread,{ MsgQueue_setEndOfMsg(&createInfo.entryMsgQueue); Thread_join(&collectorThread); Thread_done(&collectorThread); });
  AUTOFREE_ADD(&autoFreeList,&storageThread,{ MsgQueue_setEndOfMsg(&createInfo.storageMsgQueue); Thread_join(&storageThread); Thread_done(&storageThread); });

  // create new archive
  error = Archive_create(&createInfo.archiveInfo,
                         jobOptions,
                         CALLBACK(storeArchiveFile,&createInfo),
                         CALLBACK(archiveGetCryptPasswordFunction,archiveGetCryptPasswordUserData),
                         indexDatabaseHandle
                        );
  if (error != ERROR_NONE)
  {
    printError("Cannot create archive file '%s' (error: %s)\n",
               Storage_getPrintableNameCString(createInfo.storageSpecifier,NULL),
               Error_getText(error)
              );
#if 0
// NYI: must index be deleted on error?
    if (   (indexDatabaseHandle != NULL)
        && !createInfo.archiveInfo->jobOptions->noIndexDatabaseFlag
        && !createInfo.archiveInfo->jobOptions->dryRunFlag
        && !createInfo.archiveInfo->jobOptions->noStorageFlag
       )
    {
      Storage_closeIndex(&createInfo.storageIndexHandle);
    }
#endif /* 0 */
    AutoFree_cleanup(&autoFreeList);
    return error;
  }
  DEBUG_TESTCODE("command_create4") { Archive_close(&createInfo.archiveInfo); AutoFree_cleanup(&autoFreeList); return DEBUG_TESTCODE_ERROR(); }

  // start create threads
#if 1
  for (z = 0; z < createThreadCount; z++)
  {
    if (!Thread_init(&createThreads[z],"BAR create",globalOptions.niceLevel,createThreadCode,&createInfo))
    {
      HALT_FATAL_ERROR("Cannot initialize create thread!");
    }
  }

  // wait for create threads
  for (z = 0; z < createThreadCount; z++)
  {
    if (!Thread_join(&createThreads[z]))
    {
      HALT_FATAL_ERROR("Cannot stop create thread!");
    }
    Thread_done(&createThreads[z]);
  }
#else
fprintf(stderr,"%s, %d: \n",__FILE__,__LINE__);
createThreadCode(&createInfo);
#endif

  // close archive
  error = Archive_close(&createInfo.archiveInfo);
  if (error != ERROR_NONE)
  {
    printError("Cannot close archive file '%s' (error: %s)\n",
               Storage_getPrintableNameCString(createInfo.storageSpecifier,NULL),
               Error_getText(error)
              );
#if 0
// NYI: must index be deleted on error?
    if (   (indexDatabaseHandle != NULL)
        && !createInfo.archiveInfo->jobOptions->noIndexDatabaseFlag
        && !createInfo.archiveInfo->jobOptions->dryRunFlag
        && !createInfo.archiveInfo->jobOptions->noStorageFlag
       )
    {
      Storage_closeIndex(&createInfo.storageIndexHandle);
    }
#endif /* 0 */
    AutoFree_cleanup(&autoFreeList);
    return error;
  }
  DEBUG_TESTCODE("command_create5") { AutoFree_cleanup(&autoFreeList); return DEBUG_TESTCODE_ERROR(); }

  // signal end of data
  MsgQueue_setEndOfMsg(&createInfo.entryMsgQueue);
  MsgQueue_setEndOfMsg(&createInfo.storageMsgQueue);

  // wait for threads
  if (!Thread_join(&storageThread))
  {
    HALT_INTERNAL_ERROR("Cannot stop storage thread!");
  }
  if (!Thread_join(&collectorThread))
  {
    HALT_INTERNAL_ERROR("Cannot stop collector thread!");
  }
  if (!Thread_join(&collectorSumThread))
  {
    HALT_INTERNAL_ERROR("Cannot stop collector sum thread!");
  }

  // final update of status info
  (void)updateStatusInfo(&createInfo);

  // write incremental list
  if (   createInfo.storeIncrementalFileInfoFlag
      && (createInfo.failError == ERROR_NONE)
      && !isAborted(&createInfo)
      && !jobOptions->dryRunFlag
     )
  {
    printInfo(1,"Write incremental list '%s'...",String_cString(incrementalListFileName));
    error = writeIncrementalList(incrementalListFileName,
                                 &createInfo.namesDictionary
                                );
    if (error != ERROR_NONE)
    {
      printInfo(1,"FAIL\n");
      printError("Cannot write incremental list file '%s' (error: %s)\n",
                 String_cString(incrementalListFileName),
                 Error_getText(error)
                );
      AutoFree_cleanup(&autoFreeList);
      return error;
    }
    DEBUG_TESTCODE("command_create3") { AutoFree_cleanup(&autoFreeList); return DEBUG_TESTCODE_ERROR(); }

    printInfo(1,"ok\n");
    logMessage(LOG_TYPE_ALWAYS,"create incremental file '%s'\n",String_cString(incrementalListFileName));
  }

  // output statics
  if (createInfo.failError == ERROR_NONE)
  {
    printInfo(1,"%lu file/image(s)/%llu bytes(s) included\n",createInfo.statusInfo.doneEntries,createInfo.statusInfo.doneBytes);
    printInfo(2,"%lu file/image(s) skipped\n",createInfo.statusInfo.skippedEntries);
    printInfo(2,"%lu file/image(s) with errors\n",createInfo.statusInfo.errorEntries);
  }

  // get error code
  if (!isAborted(&createInfo))
  {
    error = createInfo.failError;
  }
  else
  {
    error = ERROR_ABORTED;
  }

  // free resources
  Thread_done(&collectorSumThread);
  Thread_done(&collectorThread);
  Thread_done(&storageThread);
  if (useIncrementalFileInfoFlag)
  {
    String_delete(incrementalListFileName);
    Dictionary_done(&createInfo.namesDictionary,NULL,NULL);
  }
  Storage_done(&createInfo.storageHandle);
  doneCreateInfo(&createInfo);
  free(createThreads);
  Storage_doneSpecifier(&storageSpecifier);
  AutoFree_done(&autoFreeList);

  return error;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
