/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/server.c,v $
* $Revision: 1.7 $
* $Author: torsten $
* Contents: Backup ARchiver server
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>

#include "global.h"
#include "lists.h"
#include "strings.h"
#include "arrays.h"
#include "semaphores.h"
#include "msgqueues.h"
#include "stringlists.h"

#include "bar.h"
#include "network.h"
#include "files.h"
#include "patterns.h"

#include "commands_create.h"

#include "server.h"

/****************** Conditional compilation switches *******************/

#define _SIMULATOR

/***************************** Constants *******************************/

#define PROTOCOL_VERSION_MAJOR 1
#define PROTOCOL_VERSION_MINOR 0

#define MAX_JOBS_IN_LIST 32

/***************************** Datatypes *******************************/

/* backup/restore job */
typedef struct JobNode
{
  NODE_HEADER(struct JobNode);

  uint id;

  enum
  {
    MODE_BACKUP,
    MODE_RESTORE,
  } mode;
  String             archiveFileName;
  PatternList        includePatternList;
  PatternList        excludePatternList;
  uint64             archivePartSize;
  CompressAlgorithms compressAlgorithm;
  CryptAlgorithms    cryptAlgorithm;

  /* running info */
  struct
  {
    enum
    {
      JOB_STATE_WAITING,
      JOB_STATE_RUNNING,
      JOB_STATE_COMPLETED
    } state;
    bool   terminteFlag;                    // signal to terminate job
    time_t startTime;                       // start time [s]
    ulong  estimatedRestTime;               // estimated rest running time [s]
    ulong  doneFiles;                       // number of processed files
    uint64 doneBytes;                       // sum of processed bytes
    double compressRatio;
    ulong  totalFiles;                      // number of total files
    uint64 totalBytes;                      // sum of total bytes
    String fileName;                        // current file
    String storageName;                     // current storage file
    Errors error;
  } runningInfo;
} JobNode;

typedef struct
{
  LIST_HEADER(JobNode);

  Semaphore lock;
  uint      lastJobId;
} JobList;

/* client info */
typedef struct ClientNode
{
  NODE_HEADER(struct ClientNode);

  /* thread */
  pthread_t    threadId;
  MsgQueue     commandMsgQueue;
  bool         exitFlag;

  /* connection */
  String       name;
  uint         port;
  SocketHandle socketHandle;
  bool         authentificationFlag;
  String       commandString;

  /* new job */
  JobNode      *jobNode;
} ClientNode;

typedef struct
{
  LIST_HEADER(ClientNode);
} ClientList;

typedef void(*ServerCommandFunction)(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount);

typedef struct
{
  ServerCommandFunction serverCommandFunction;
  uint                  id;
  Array                 arguments;
} CommandMsg;

/***************************** Variables *******************************/
LOCAL JobList    jobList;
LOCAL pthread_t  jobThreadId;
LOCAL ClientList clientList;
LOCAL bool       quitFlag;

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : updateCreateStatus
* Purpose: update create status
* Input  : createStatusInfo - create status info data
*          jobNode          - job node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateCreateStatus(const CreateStatusInfo *createStatusInfo,
                              JobNode                *jobNode
                             )
{
  ulong  elapsedTime;
  double filesPerSecond,bytesPerSecond;
  ulong  restFiles;
  uint64 restBytes;
  ulong  estimtedRestTime;
  double sum;
  uint   n;

  assert(createStatusInfo != NULL);
  assert(createStatusInfo->fileName != NULL);
  assert(createStatusInfo->storageName != NULL);
  assert(jobNode != NULL);

  /* calculate estimated rest time */
  elapsedTime = (ulong)(time(NULL)-jobNode->runningInfo.startTime);
  filesPerSecond = (elapsedTime > 0)?(double)createStatusInfo->doneFiles/(double)elapsedTime:0;
  bytesPerSecond = (elapsedTime > 0)?(double)createStatusInfo->doneBytes/(double)elapsedTime:0;
  restFiles = (jobNode->runningInfo.totalFiles > createStatusInfo->doneFiles)?jobNode->runningInfo.totalFiles-createStatusInfo->doneFiles:0L;
  restBytes = (jobNode->runningInfo.totalBytes > createStatusInfo->doneBytes)?jobNode->runningInfo.totalBytes-createStatusInfo->doneBytes:0LL;
  sum = 0; n = 0;
  if (filesPerSecond > 0) { sum += (double)restFiles/filesPerSecond; n++; }
  if (bytesPerSecond > 0) { sum += (double)restBytes/bytesPerSecond; n++; }
  if (n > 0)
  estimtedRestTime = (n > 0)?(ulong)round(sum/n):0;
/*
fprintf(stderr,"%s,%d: createStatusInfo->doneFiles=%lu createStatusInfo->doneBytes=%llu jobNode->runningInfo.totalFiles=%lu jobNode->runningInfo.totalBytes %llu -- elapsedTime=%lus filesPerSecond=%f bytesPerSecond=%f estimtedRestTime=%lus\n",__FILE__,__LINE__,
createStatusInfo->doneFiles,
createStatusInfo->doneBytes,
jobNode->runningInfo.totalFiles,
jobNode->runningInfo.totalBytes,
elapsedTime,filesPerSecond,bytesPerSecond,estimtedRestTime);
*/

  jobNode->runningInfo.doneFiles  = createStatusInfo->doneFiles;
  jobNode->runningInfo.doneBytes  = createStatusInfo->doneBytes;
  jobNode->runningInfo.totalFiles = createStatusInfo->totalFiles;
  jobNode->runningInfo.totalBytes = createStatusInfo->totalBytes;
  jobNode->runningInfo.estimatedRestTime = estimtedRestTime;
  String_set(jobNode->runningInfo.fileName,   createStatusInfo->fileName   );
  String_set(jobNode->runningInfo.storageName,createStatusInfo->storageName);
//fprintf(stderr,"%s,%d: createStatusInfo->fileName=%s\n",__FILE__,__LINE__,String_cString(jobNode->runningInfo.fileName));
}

/***********************************************************************\
* Name   : jobThread
* Purpose: job thread
* Input  : jobList - job list
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void jobThread(JobList *jobList)
{
  JobNode *jobNode;

  while (!quitFlag)
  {
    /* get next job */
    Semaphore_lock(&jobList->lock);
    do
    {
      jobNode = jobList->head;
      while ((jobNode != NULL) && (jobNode->runningInfo.state != JOB_STATE_WAITING))
      {
        jobNode = jobNode->next;
      }
      if (jobNode == NULL) Semaphore_wait(&jobList->lock);
    }
    while (!quitFlag && (jobNode == NULL));
    if (jobNode != NULL)
    {
      jobNode->runningInfo.state = JOB_STATE_RUNNING;
    }
    Semaphore_unlock(&jobList->lock);
    if (quitFlag) break;

    /* run job */
    jobNode->runningInfo.startTime = time(NULL);
#ifdef SIMULATOR
{
  int z;

  jobNode->runningInfo.estimatedRestTime=120;

  jobNode->runningInfo.totalFiles+=60;
  jobNode->runningInfo.totalBytes+=6000;

  for (z=0;z<120;z++)
  {
    extern void sleep(int);
    if (jobNode->runningInfo.terminteFlag) break;

    fprintf(stderr,"%s,%d: z=%d\n",__FILE__,__LINE__,z);
    sleep(1);

    if (z==40) { 
      jobNode->runningInfo.totalFiles+=80;
      jobNode->runningInfo.totalBytes+=8000;
    }

    jobNode->runningInfo.doneFiles++;
    jobNode->runningInfo.doneBytes+=100;
  //  jobNode->runningInfo.totalFiles+=3;
  //  jobNode->runningInfo.totalBytes+=181;
    jobNode->runningInfo.estimatedRestTime=120-z;
    String_clear(jobNode->runningInfo.fileName);String_format(jobNode->runningInfo.fileName,"file %d",z);
    String_clear(jobNode->runningInfo.storageName);String_format(jobNode->runningInfo.storageName,"storage %d%d",z,z);
  }
}
#else
/*
bool Command_create(const char         *archiveFileName,
                    PatternList        *includePatternList,
                    PatternList        *excludePatternList,
                    ulong              partSize,
                    CompressAlgorithms compressAlgorithm,
                    ulong              compressMinFileSize,
                    CryptAlgorithms    cryptAlgorithm,
                    const char         *password
                   );    
*/
fprintf(stderr,"%s,%d: %s\n",__FILE__,__LINE__,
String_cString(jobNode->archiveFileName)

);
    Command_create(String_cString(jobNode->archiveFileName),
                   &jobNode->includePatternList,
                   &jobNode->excludePatternList,
                   jobNode->archivePartSize,
                   jobNode->compressAlgorithm,
                   0,
                   jobNode->cryptAlgorithm,
                   "xxxx",
                   (CreateStatusInfoFunction)updateCreateStatus,
                   jobNode
                  );

#endif /* SIMULATOR */

    /* done job */
    jobNode->runningInfo.state = JOB_STATE_COMPLETED;
  }
}

/***********************************************************************\
* Name   : getNewJobId
* Purpose: get new job id
* Input  : -
* Output : -
* Return : job id
* Notes  : -
\***********************************************************************/

LOCAL uint getNewJobId(void)
{

  jobList.lastJobId++;

  return jobList.lastJobId;
}

/***********************************************************************\
* Name   : freeJobNode
* Purpose: free job node
* Input  : jobNode - job node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeJobNode(JobNode *jobNode)
{
  assert(jobNode != NULL);

  Pattern_doneList(&jobNode->excludePatternList);
  Pattern_doneList(&jobNode->includePatternList);
  String_delete(jobNode->archiveFileName);

  String_delete(jobNode->runningInfo.fileName);
  String_delete(jobNode->runningInfo.storageName);
}

/***********************************************************************\
* Name   : newJob
* Purpose: create new job
* Input  : -
* Output : -
* Return : job node
* Notes  : -
\***********************************************************************/

LOCAL JobNode *newJob(void)
{
  JobNode *jobNode;

  jobNode = LIST_NEW_NODE(JobNode);
  if (jobNode == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  jobNode->archiveFileName   = String_new();
  Pattern_initList(&jobNode->includePatternList);
  Pattern_initList(&jobNode->excludePatternList);
  jobNode->archivePartSize   = 0LL;
  jobNode->compressAlgorithm = COMPRESS_ALGORITHM_NONE;
  jobNode->cryptAlgorithm    = CRYPT_ALGORITHM_NONE;

  jobNode->runningInfo.state             = JOB_STATE_WAITING;
  jobNode->runningInfo.terminteFlag      = FALSE;
  jobNode->runningInfo.startTime         = 0;
  jobNode->runningInfo.estimatedRestTime = 0;
  jobNode->runningInfo.doneFiles         = 0L;
  jobNode->runningInfo.doneBytes         = 0LL;
  jobNode->runningInfo.compressRatio     = 0.0;
  jobNode->runningInfo.totalFiles        = 0L;
  jobNode->runningInfo.totalBytes        = 0LL;
  jobNode->runningInfo.fileName          = String_new();
  jobNode->runningInfo.storageName       = String_new();
  jobNode->runningInfo.error             = ERROR_NONE;

  return jobNode;  
}

/***********************************************************************\
* Name   : deleteJob
* Purpose: delete job
* Input  : jobNode - job node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void deleteJob(JobNode *jobNode)
{
  assert(jobNode != NULL);

  freeJobNode(jobNode);
  LIST_DELETE_NODE(jobNode);
}

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : getBooleanValue
* Purpose: get boolean value from string
* Input  : string - string
* Output : -
* Return : TRUE if string is either ", yes or true, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool getBooleanValue(const String s)
{
  return    String_equalsCString(s,"1")
         || String_equalsCString(s,"yes")
         || String_equalsCString(s,"true");
}

/***********************************************************************\
* Name   : sendResult
* Purpose: send result to client
* Input  : clientNode   - client node
*          id           - command id
*          completeFlag - TRUE if command is completed, FALSE otherwise
*          errorCode    - error code
*          format       - format string
*          ...          - optional arguments
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void sendResult(ClientNode *clientNode, uint id, bool completeFlag, uint errorCode, const char *format, ...)
{
  String  result;
  va_list arguments;

  result = String_new();

  String_format(result,"%d %d %d ",id,completeFlag?1:0,errorCode);
  va_start(arguments,format);
  String_vformat(result,format,arguments);
  va_end(arguments);
  String_appendChar(result,'\n');

//??? blockieren?
  Network_send(&clientNode->socketHandle,String_cString(result),String_length(result));
//fprintf(stderr,"%s,%d: sent data: '%s'",__FILE__,__LINE__,String_cString(result));

  String_delete(result);
}

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : getTotalSubDirectorySize
* Purpose: get total size of files in sub-directory
* Input  : subDirectory - path name
* Output : -
* Return : total size of files in sub-directory (in bytes)
* Notes  : -
\***********************************************************************/

LOCAL uint64 getTotalSubDirectorySize(const String subDirectory)
{
  uint64          totalSize;
  StringList      pathNameList;
  String          pathName;
  DirectoryHandle directoryHandle;
  String          fileName;
  FileInfo        fileInfo;
  Errors          error;

  totalSize = 0;

  StringList_init(&pathNameList);
  StringList_append(&pathNameList,String_copy(subDirectory));
  pathName = String_new();
  fileName = String_new();
  while (!StringList_empty(&pathNameList))
  {
    pathName = StringList_getFirst(&pathNameList,pathName);

    error = File_openDirectory(&directoryHandle,pathName);
    if (error != ERROR_NONE)
    {
      continue;
    }

    while (!File_endOfDirectory(&directoryHandle))
    {
      error = File_readDirectory(&directoryHandle,fileName);
      if (error != ERROR_NONE)
      {
        continue;
      }

      error = File_getFileInfo(fileName,&fileInfo);
      if (error != ERROR_NONE)
      {
        continue;
      }

      switch (File_getType(fileName))
      {
        case FILETYPE_FILE:
          totalSize += fileInfo.size;
          break;
        case FILETYPE_DIRECTORY:
          StringList_append(&pathNameList,String_copy(fileName));
          break;
        case FILETYPE_LINK:
          break;
        default:
          break;
      }
    }

    File_closeDirectory(&directoryHandle);
  }
  String_delete(pathName);
  String_delete(fileName);
  StringList_done(&pathNameList);

  return totalSize;
}

/*---------------------------------------------------------------------*/

LOCAL void serverCommand_auth(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  assert(clientNode != NULL);
  assert(arguments != NULL);

  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected password");
    return;
  }

  sendResult(clientNode,id,TRUE,0,"");
}

LOCAL void serverCommand_deviceList(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  Errors       error;
  DeviceHandle deviceHandle;
  String       deviceName;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  error = File_openDevices(&deviceHandle);
  if (error != ERROR_NONE)
  {
    sendResult(clientNode,id,TRUE,1,"cannot open device list (error: %s)",getErrorText(error));
    return;
  }

  deviceName = String_new();
  while (!File_endOfDevices(&deviceHandle))
  {
    error = File_readDevice(&deviceHandle,deviceName);
    if (error != ERROR_NONE)
    {
      sendResult(clientNode,id,TRUE,1,"cannot read device list (error: %s)",getErrorText(error));
      File_closeDevices(&deviceHandle);
      String_delete(deviceName);
      return;
    }

    sendResult(clientNode,id,FALSE,0,"%'S",deviceName);
  }
  String_delete(deviceName);

  File_closeDevices(&deviceHandle);

  sendResult(clientNode,id,TRUE,0,"");
}

LOCAL void serverCommand_fileList(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  bool            totalSizeFlag;
  Errors          error;
  DirectoryHandle directoryHandle;
  String          fileName;
  FileInfo        fileInfo;
  uint64          totalSize;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected path name");
    return;
  }
  totalSizeFlag = ((argumentCount >= 2) && getBooleanValue(arguments[1]));

  error = File_openDirectory(&directoryHandle,arguments[0]);
  if (error != ERROR_NONE)
  {
    sendResult(clientNode,id,TRUE,1,"cannot open '%S' (error: %s)",arguments[0],getErrorText(error));
    return;
  }

  fileName = String_new();
  while (!File_endOfDirectory(&directoryHandle))
  {
    error = File_readDirectory(&directoryHandle,fileName);
    if (error == ERROR_NONE)
    {
      error = File_getFileInfo(fileName,&fileInfo);
      if (error == ERROR_NONE)
      {
        switch (File_getType(fileName))
        {
          case FILETYPE_FILE:
            sendResult(clientNode,id,FALSE,0,
                       "FILE %llu %'S",
                       fileInfo.size,
                       fileName
                      );
            break;
          case FILETYPE_DIRECTORY:
            if (totalSizeFlag)
            {         
              totalSize = getTotalSubDirectorySize(fileName);
            }
            else
            {
              totalSize = 0;
            }
            sendResult(clientNode,id,FALSE,0,
                       "DIRECTORY %llu %'S",
                       totalSize,
                       fileName
                      );
            break;
          case FILETYPE_LINK:
            sendResult(clientNode,id,FALSE,0,
                       "LINK %'S",
                       fileName
                      );
            break;
          default:
            sendResult(clientNode,id,FALSE,0,
                       "unknown %'S",
                       fileName
                      );
            break;
        }
      }
      else
      {
        sendResult(clientNode,id,FALSE,0,
                   "UNKNOWN %'S",
                   fileName
                  );
      }
    }
    else
    {
      sendResult(clientNode,id,TRUE,1,"cannot read directory '%S' (error: %s)",arguments[0],getErrorText(error));
      File_closeDirectory(&directoryHandle);
      String_delete(fileName);
      return;
    }
  }
  String_delete(fileName);

  File_closeDirectory(&directoryHandle);

  sendResult(clientNode,id,TRUE,0,"");
}

LOCAL void serverCommand_jobList(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  JobNode    *jobNode;
  const char *stateText;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  Semaphore_lock(&jobList.lock);
  jobNode = jobList.head;
  while (jobNode != NULL)
  {
    switch (jobNode->runningInfo.state)
    {
      case JOB_STATE_WAITING:   stateText = "WAITING";   break;
      case JOB_STATE_RUNNING:   stateText = "RUNNING";   break;
      case JOB_STATE_COMPLETED: stateText = "COMPLETED"; break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break; /* not reached */
      #endif /* NDEBUG */
    }
    sendResult(clientNode,id,FALSE,0,
               "%u %s %llu %'s %'s %lu %lu",
               jobNode->id,
               stateText,
               jobNode->archivePartSize,
               Compress_getAlgorithmName(jobNode->compressAlgorithm),
               Crypt_getAlgorithmName(jobNode->cryptAlgorithm),
               jobNode->runningInfo.startTime,
               jobNode->runningInfo.estimatedRestTime
              );

    jobNode = jobNode->next;
  }
  Semaphore_unlock(&jobList.lock);

  sendResult(clientNode,id,TRUE,0,"");
}

LOCAL void serverCommand_jobInfo(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  uint       jobId;
  JobNode    *jobNode;
//  int        errorCode;
  const char *stateText;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock);

  /* find job */
  jobNode = jobList.head;
  while ((jobNode != NULL) && (jobNode->id != jobId))
  {
    jobNode = jobNode->next;
  }

  /* format result */
  if (jobNode != NULL)
  {
    switch (jobNode->runningInfo.state)
    {
      case JOB_STATE_WAITING:   stateText = "WAITING";   break;
      case JOB_STATE_RUNNING:   stateText = "RUNNING";   break;
      case JOB_STATE_COMPLETED: stateText = "COMPLETED"; break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break; /* not reached */
      #endif /* NDEBUG */
    }
    sendResult(clientNode,id,TRUE,0,
               "%s %lu %llu %f %lu %llu %'S %'S",
               stateText,
               jobNode->runningInfo.doneFiles,
               jobNode->runningInfo.doneBytes,
               jobNode->runningInfo.compressRatio,
               jobNode->runningInfo.totalFiles,
               jobNode->runningInfo.totalBytes,
               jobNode->runningInfo.fileName,
               jobNode->runningInfo.storageName
              );
  }
  else
  {
    sendResult(clientNode,id,TRUE,1,"job %d not found",jobId);
  }

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_addIncludePattern(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  String includePattern;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  /* get include pattern */
  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected pattern");
    return;
  }
  includePattern = arguments[0];

  if (clientNode->jobNode != NULL)
  {
    Pattern_appendList(&clientNode->jobNode->includePatternList,String_cString(includePattern),PATTERN_TYPE_GLOB);
    sendResult(clientNode,id,TRUE,0,"");
  }
  else
  {
    sendResult(clientNode,id,TRUE,1,"no job");
  }
}

LOCAL void serverCommand_addExcludePattern(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  String excludePattern;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  /* get exclude pattern */
  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected pattern");
    return;
  }
  excludePattern = arguments[0];

  if (clientNode->jobNode != NULL)
  {
    Pattern_appendList(&clientNode->jobNode->excludePatternList,String_cString(excludePattern),PATTERN_TYPE_GLOB);
    sendResult(clientNode,id,TRUE,0,"");
  }
  else
  {
    sendResult(clientNode,id,TRUE,1,"no job");
  }
}

LOCAL void serverCommand_getConfigValue(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  assert(clientNode != NULL);
  assert(arguments != NULL);

  /* get config value name */
  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected config value name");
    return;
  }

  if (clientNode->jobNode != NULL)
  {
    if      (String_equalsCString(arguments[0],"archive-file"))
    {
      sendResult(clientNode,id,TRUE,0,"%'S",clientNode->jobNode->archiveFileName);
    }
    else if (String_equalsCString(arguments[0],"archive-part-size"))
    {
      sendResult(clientNode,id,TRUE,0,"%llu",clientNode->jobNode->archivePartSize);
    }
    else if (String_equalsCString(arguments[0],"compress-algorithm"))
    {
      sendResult(clientNode,id,TRUE,0,"%'s",Compress_getAlgorithmName(clientNode->jobNode->compressAlgorithm));
    }
    else if (String_equalsCString(arguments[0],"crypt-algorithm"))
    {
      sendResult(clientNode,id,TRUE,0,"%'s",Crypt_getAlgorithmName(clientNode->jobNode->cryptAlgorithm));
    }
    else
    {
      sendResult(clientNode,id,TRUE,1,"unknown config value");
    }
  }
  else
  {
    sendResult(clientNode,id,TRUE,1,"no job");
  }
}

LOCAL void serverCommand_setConfigValue(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  assert(clientNode != NULL);
  assert(arguments != NULL);

  /* get config value name */
  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected config name");
    return;
  }
  if (argumentCount < 2)
  {
    sendResult(clientNode,id,TRUE,1,"expected config value");
    return;
  }

  if (clientNode->jobNode != NULL)
  {
    if      (String_equalsCString(arguments[0],"archive-file"))
    {
      String_set(clientNode->jobNode->archiveFileName,arguments[1]);
      sendResult(clientNode,id,TRUE,0,"");
    }
    else if (String_equalsCString(arguments[0],"archive-part-size"))
    {
      const StringUnit UNITS[] = {{"K",1024},{"M",1024*1024},{"G",1024*1024*1024}};

      clientNode->jobNode->archivePartSize = String_toInteger64(arguments[1],NULL,UNITS,SIZE_OF_ARRAY(UNITS));
      sendResult(clientNode,id,TRUE,0,"");
    }
    else if (String_equalsCString(arguments[0],"compress-algorithm"))
    {
      CompressAlgorithms compressAlgorithm;

      compressAlgorithm = Compress_getAlgorithm(String_cString(arguments[1]));
      if (compressAlgorithm == COMPRESS_ALGORITHM_UNKNOWN)
      {
        sendResult(clientNode,id,TRUE,1,"unknown compress algorithm");
        return;
      }
      clientNode->jobNode->compressAlgorithm = compressAlgorithm;
      sendResult(clientNode,id,TRUE,0,"");
    }
    else if (String_equalsCString(arguments[0],"crypt-algorithm"))
    {
      CryptAlgorithms cryptAlgorithm;

      cryptAlgorithm = Crypt_getAlgorithm(String_cString(arguments[1]));
      if (cryptAlgorithm == CRYPT_ALGORITHM_UNKNOWN)
      {
        sendResult(clientNode,id,TRUE,1,"unknown crypt algorithm");
        return;
      }
      clientNode->jobNode->cryptAlgorithm = cryptAlgorithm;
      sendResult(clientNode,id,TRUE,0,"");
    }
    else
    {
      sendResult(clientNode,id,TRUE,1,"unknown config value");
    }
  }
  else
  {
    sendResult(clientNode,id,TRUE,1,"no job");
  }
}

LOCAL void serverCommand_newJob(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  assert(clientNode != NULL);
  assert(arguments != NULL);

  if (clientNode->jobNode != NULL)
  {
    deleteJob(clientNode->jobNode);
  }

  clientNode->jobNode = newJob();

  sendResult(clientNode,id,TRUE,0,"");
}

LOCAL void serverCommand_addJob(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  JobNode *jobNode;
  uint    jobId;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  if (clientNode->jobNode != NULL)
  {
    /* lock */
    Semaphore_lock(&jobList.lock);

    /* clean-up job list */
    while (List_count(&jobList) >= MAX_JOBS_IN_LIST)
    {
      jobNode = jobList.head;
      while ((jobNode != NULL) && (jobNode->runningInfo.state != JOB_STATE_COMPLETED))
      {
        jobNode = jobNode->next;
      }
      if (jobNode != NULL)
      {
        List_remove(&jobList,jobNode);
      }
    }

    /* add new job to list */
    jobId = getNewJobId();
    clientNode->jobNode->id = jobId;
    List_append(&jobList,clientNode->jobNode);
    clientNode->jobNode = NULL;

    /* unlock */
    Semaphore_unlock(&jobList.lock);

    sendResult(clientNode,id,TRUE,0,"%d",jobId);
  }
  else
  {
    sendResult(clientNode,id,TRUE,1,"no job");
  }
}

LOCAL void serverCommand_remJob(ClientNode *clientNode, uint id, const String arguments[], uint argumentCount)
{
  JobNode *jobNode;
  uint    jobId;

  assert(clientNode != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientNode,id,TRUE,1,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock);

  /* find job */
  jobNode = jobList.head;
  while ((jobNode != NULL) && (jobNode->id != jobId))
  {
    jobNode = jobNode->next;
  }

  /* check if job running, remove job to list */
  jobNode->runningInfo.terminteFlag = TRUE;
  while (jobNode->runningInfo.state == JOB_STATE_RUNNING)
  {
    Semaphore_wait(&jobList.lock);
  }
  List_remove(&jobList,jobNode);

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientNode,id,TRUE,0,"");
}

const struct { const char *name; ServerCommandFunction serverCommandFunction; } SERVER_COMMANDS[] =
{
  { "AUTH",                serverCommand_auth              }, 
  { "DEVICE_LIST",         serverCommand_deviceList        }, 
  { "FILE_LIST",           serverCommand_fileList          }, 
  { "JOB_LIST",            serverCommand_jobList           }, 
  { "JOB_INFO",            serverCommand_jobInfo           }, 
  { "ADD_INCLUDE_PATTERN", serverCommand_addIncludePattern },
  { "ADD_EXCLUDE_PATTERN", serverCommand_addExcludePattern },
  { "GET_CONFIG_VALUE",    serverCommand_getConfigValue    },
  { "SET_CONFIG_VALUE",    serverCommand_setConfigValue    },
  { "NEW_JOB",             serverCommand_newJob            },
  { "ADD_JOB",             serverCommand_addJob            },
  { "REM_JOB",             serverCommand_remJob            },
};

/***********************************************************************\
* Name   : freeArgumentsArrayElement
* Purpose: free argument array element
* Input  : string   - array element to free
*          userData - user data (ignored)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeArgumentsArrayElement(String *string, void *userData)
{
  assert(string != NULL);

  UNUSED_VARIABLE(userData);

  String_delete(*string);
}

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : freeCommand
* Purpose: free allocated command message
* Input  : commandMsg - command message
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeCommand(CommandMsg *commandMsg)
{
  assert(commandMsg != NULL);

  Array_delete(commandMsg->arguments,(ArrayElementFreeFunction)freeArgumentsArrayElement,NULL);
}

/***********************************************************************\
* Name   : parseCommand
* Purpose: parse command
* Input  : string - command
* Output : commandMsg - command message
* Return : TRUE if no error, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool parseCommand(CommandMsg *commandMsg,
                        String      string
                       )
{
  StringTokenizer stringTokenizer;
  String          token;
  uint            z;
  long            index;
  String          argument;

  assert(commandMsg != NULL);

  /* initialize tokenizer */
  String_initTokenizer(&stringTokenizer,string,STRING_WHITE_SPACES,"\"'",TRUE);

  /* get command */
  if (!String_getNextToken(&stringTokenizer,&token,NULL))
  {
    String_doneTokenizer(&stringTokenizer);
    return FALSE;
  }
  z = 0;
  while ((z < SIZE_OF_ARRAY(SERVER_COMMANDS)) && !String_equalsCString(token,SERVER_COMMANDS[z].name))
  {
    z++;
  }
  if (z >= SIZE_OF_ARRAY(SERVER_COMMANDS))
  {
    String_doneTokenizer(&stringTokenizer);
    return FALSE;
  }
  commandMsg->serverCommandFunction = SERVER_COMMANDS[z].serverCommandFunction;

  /* get id */
  if (!String_getNextToken(&stringTokenizer,&token,NULL))
  {
    String_doneTokenizer(&stringTokenizer);
    return FALSE;
  }
  commandMsg->id = String_toInteger(token,&index,NULL,0);
  if (index != STRING_END)
  {
    String_doneTokenizer(&stringTokenizer);
    return FALSE;
  }

  /* get arguments */
  commandMsg->arguments = Array_new(sizeof(String),0);
  while (String_getNextToken(&stringTokenizer,&token,NULL))
  {
    argument = String_copy(token);
    Array_append(commandMsg->arguments,&argument);
  }

  /* free resources */
  String_doneTokenizer(&stringTokenizer);

  return TRUE;
}

/***********************************************************************\
* Name   : clientThread
* Purpose: client processing thread
* Input  : clientNode - client node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void clientThread(ClientNode *clientNode)
{
  CommandMsg commandMsg;
  String     result;

  assert(clientNode != NULL);

  result = String_new();
  while (   !clientNode->exitFlag
         && MsgQueue_get(&clientNode->commandMsgQueue,&commandMsg,NULL,sizeof(commandMsg))
        )
  {
    /* execute command */
    commandMsg.serverCommandFunction(clientNode,
                                     commandMsg.id,
                                     Array_cArray(commandMsg.arguments),
                                     Array_length(commandMsg.arguments)
                                    );

    /* free resources */
    freeCommand(&commandMsg);
  }
  String_delete(result);

  clientNode->exitFlag = TRUE;
}

/***********************************************************************\
* Name   : freeCommandMsg
* Purpose: free command msg
* Input  : commandMsg - command message
*          userData   - user data (ignored)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeCommandMsg(CommandMsg *commandMsg, void *userData)
{
  assert(commandMsg != NULL);

  UNUSED_VARIABLE(userData);

  freeCommand(commandMsg);
}

/***********************************************************************\
* Name   : freeClientNode
* Purpose: free client node
* Input  : clientNode - client node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeClientNode(ClientNode *clientNode)
{
  assert(clientNode != NULL);

  /* stop client thread */
  clientNode->exitFlag = TRUE;
  MsgQueue_setEndOfMsg(&clientNode->commandMsgQueue);
  pthread_join(clientNode->threadId,NULL);

  /* delete */
  MsgQueue_done(&clientNode->commandMsgQueue,(MsgQueueMsgFreeFunction)freeCommandMsg,NULL);
  String_delete(clientNode->commandString);
  String_delete(clientNode->name);
  if (clientNode->jobNode != NULL) deleteJob(clientNode->jobNode);
}

/***********************************************************************\
* Name   : newClient
* Purpose: create new client
* Input  : name         - client name
*          port         - client port
*          socketHandle - client socket handle
* Output : -
* Return : client node
* Notes  : -
\***********************************************************************/

LOCAL ClientNode *newClient(const String name, uint port, SocketHandle socketHandle)
{
  ClientNode *clientNode;

  clientNode = LIST_NEW_NODE(ClientNode);
  if (clientNode == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  clientNode->exitFlag = FALSE;

  clientNode->name                 = String_copy(name);
  clientNode->port                 = port;
  clientNode->socketHandle         = socketHandle;
  clientNode->authentificationFlag = FALSE;
  clientNode->commandString        = String_new();
  clientNode->jobNode              = NULL;

  if (!MsgQueue_init(&clientNode->commandMsgQueue,0))
  {
    HALT_FATAL_ERROR("Cannot initialise client command message queue!");
  }
  if (pthread_create(&clientNode->threadId,NULL,(void*(*)(void*))clientThread,clientNode) != 0)
  {
    HALT_FATAL_ERROR("Cannot initialise client thread!");
  }

  return clientNode;
}

/***********************************************************************\
* Name   : deleteClient
* Purpose: delete client
* Input  : clientNode - client node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void deleteClient(ClientNode *clientNode)
{
  freeClientNode(clientNode);

  LIST_DELETE_NODE(clientNode);
}

/***********************************************************************\
* Name   : processCommand
* Purpose: process client command
* Input  : clientNode - client node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void processCommand(ClientNode *clientNode, const String command)
{
  CommandMsg commandMsg;

  assert(clientNode != NULL);

fprintf(stderr,"%s,%d: command=%s\n",__FILE__,__LINE__,String_cString(command));
  if (String_equalsCString(command,"VERSION"))
  {
    /* version info */
    sendResult(clientNode,0,TRUE,0,"%d %d\n",PROTOCOL_VERSION_MAJOR,PROTOCOL_VERSION_MINOR);
  }
  #ifndef NDEBUG
    else if (String_equalsCString(command,"QUIT"))
    {
      quitFlag = TRUE;
      sendResult(clientNode,0,TRUE,0,"ok\n");
    }
  #endif /* not NDEBUG */
  else
  {
    /* parse command */
    if (!parseCommand(&commandMsg,command))
    {
      sendResult(clientNode,0,TRUE,1,"parse error");
      return;
    }

    /* send command to client thread */
    MsgQueue_put(&clientNode->commandMsgQueue,&commandMsg,sizeof(commandMsg));
  }
}

/*---------------------------------------------------------------------*/

Errors Server_init(void)
{
  return ERROR_NONE;
}

void Server_done(void)
{
}

bool Server_run(uint       serverPort,
                const char *serverPassword
               )
{
  Errors       error;
  SocketHandle serverSocketHandle;
  fd_set       selectSet;
  ClientNode   *clientNode;
  SocketHandle socketHandle;
  String       name;
  uint         port;
  char         buffer[256];
  ulong        receivedBytes;
  ulong        z;
  ClientNode   *deleteClientNode;

  /* initialise variables */
  List_init(&jobList);
  List_init(&clientList);
  quitFlag = FALSE;

  /* start server */
  error = Network_initServer(&serverSocketHandle,serverPort);
  if (error != ERROR_NONE)
  {
    printError("Cannot initialize server (error: %s)!\n",
               getErrorText(error)
              );
    return FALSE;
  }

  /* start job-thread */
  if (pthread_create(&jobThreadId,NULL,(void*(*)(void*))jobThread,&jobList) != 0)
  {
    HALT_FATAL_ERROR("Cannot initialise job thread!");
  }

  /* run server */
  name = String_new();
  while (!quitFlag)
  {
    /* wait for command */
    FD_ZERO(&selectSet);
    FD_SET(Network_getSocket(&serverSocketHandle),&selectSet);
    clientNode = clientList.head;
    while (clientNode != NULL)
    {
      FD_SET(Network_getSocket(&clientNode->socketHandle),&selectSet);
      clientNode = clientNode->next;
    }
    select(FD_SETSIZE,&selectSet,NULL,NULL,NULL);

    /* connect new clients */
    if (FD_ISSET(Network_getSocket(&serverSocketHandle),&selectSet))
    {
      error = Network_accept(&socketHandle,
                             &serverSocketHandle
                            );
      if (error == ERROR_NONE)
      {
        Network_getRemoteInfo(&socketHandle,name,&port);
        clientNode = newClient(name,port,socketHandle);
        List_append(&clientList,clientNode);

        info(1,"Connected client '%s:%u'\n",String_cString(clientNode->name),clientNode->port);
      }
      else
      {
        printError("Cannot estable client connection (error: %s)!\n",
                   getErrorText(error)
                  );
      }
    }

    /* process client commands/disconnect clients */
    clientNode = clientList.head;
    while (clientNode != NULL)
    {
      if (FD_ISSET(Network_getSocket(&clientNode->socketHandle),&selectSet))
      {
        error = Network_receive(&clientNode->socketHandle,buffer,sizeof(buffer),&receivedBytes);
        if (error == ERROR_NONE)
        {
          if (receivedBytes > 0)
          {
            /* received data -> process */
            do
            {
              for (z = 0; z < receivedBytes; z++)
              {
                if (buffer[z] != '\n')
                {
                  String_appendChar(clientNode->commandString,buffer[z]);
                }
                else
                {
                  processCommand(clientNode,clientNode->commandString);
                  String_clear(clientNode->commandString);
                }
              }
              error = Network_receive(&clientNode->socketHandle,buffer,sizeof(buffer),&receivedBytes);
            }
            while ((error == ERROR_NONE) && (receivedBytes > 0));

            clientNode = clientNode->next;
          }
          else
          {
            /* disconnect */
            info(1,"Disconnected client '%s:%u'\n",String_cString(clientNode->name),clientNode->port);

            /* remove from list */
            deleteClientNode = clientNode;
            clientNode = clientNode->next;
            List_remove(&clientList,deleteClientNode);

            /* delete */
            deleteClient(deleteClientNode);
          }
        }
        else
        {
          clientNode = clientNode->next;
        }
      }
      else
      {
        clientNode = clientNode->next;
      }
    }
  }
  String_delete(name);

  /* done server */
  Network_doneServer(&serverSocketHandle);

  /* free resources */
  List_done(&clientList,(ListNodeFreeFunction)freeClientNode,NULL);
  List_done(&jobList,(ListNodeFreeFunction)freeJobNode,NULL);

  return TRUE;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
