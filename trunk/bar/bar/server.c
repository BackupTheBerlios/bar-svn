/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/server.c,v $
* $Revision: 1.10 $
* $Author: torsten $
* Contents: Backup ARchiver server
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include "config.h"

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
#include "configvalues.h"
#include "threads.h"
#include "semaphores.h"
#include "msgqueues.h"
#include "stringlists.h"

#include "bar.h"
#include "passwords.h"
#include "network.h"
#include "files.h"
#include "patterns.h"
#include "patternlists.h"
#include "archive.h"
#include "storage.h"
#include "misc.h"

#include "commands_create.h"
#include "commands_restore.h"

#include "server.h"

/****************** Conditional compilation switches *******************/

#define _SERVER_DEBUG
#define _NO_SESSION_ID
#define _SIMULATOR

/***************************** Constants *******************************/

#define PROTOCOL_VERSION_MAJOR 1
#define PROTOCOL_VERSION_MINOR 0

#define SESSION_ID_LENGTH 64                   // max. length of session id

#define MAX_NETWORK_CLIENT_THREADS 3           // number of threads for a client

/***************************** Datatypes *******************************/

/* job states */
typedef enum
{
  JOB_STATE_NONE,
  JOB_STATE_WAITING,
  JOB_STATE_RUNNING,
  JOB_STATE_REQUEST_VOLUME,
  JOB_STATE_DONE,
  JOB_STATE_ERROR,
  JOB_STATE_ABORTED
} JobStates;

/* job node */
typedef struct JobNode
{
  LIST_NODE_HEADER(struct JobNode);

  String       fileName;                       // file name
  uint64       timeModified;                   // file modified date/time (timestamp)

  /* job config */
  JobTypes     type;                           // job type: backup, restore
  String       name;                           // name of job
  String       archiveName;                    // archive name
  PatternList  includePatternList;             // included files
  PatternList  excludePatternList;             // excluded files
  ScheduleList scheduleList;                   // schedule list
  JobOptions   jobOptions;                     // options for job
  bool         modifiedFlag;                   // TRUE iff job config modified

  /* schedule info */
  uint64       lastExecutedDateTime;           // last execution date/time (timestamp)
  uint64       lastCheckDateTime;              // last check date/time (timestamp)

  /* job info */
  uint         id;                             // unique job id
  JobStates    state;                          // current state of job
  ArchiveTypes archiveType;                    // archive type to create
  bool         requestedAbortFlag;             // request abort
  uint         requestedVolumeNumber;          // requested volume number
  uint         volumeNumber;                   // loaded volume number

  /* running info */
  struct
  {
    Errors            error;                          // error code
    ulong             estimatedRestTime;              // estimated rest running time [s]
    ulong             doneFiles;                      // number of processed files
    uint64            doneBytes;                      // sum of processed bytes
    ulong             totalFiles;                     // number of total files
    uint64            totalBytes;                     // sum of total bytes
    ulong             skippedFiles;                   // number of skipped files
    uint64            skippedBytes;                   // sum of skippped bytes
    ulong             errorFiles;                     // number of files with errors
    uint64            errorBytes;                     // sum of bytes of files with errors
    PerformanceFilter filesPerSecond;                 // average processed files per second
    PerformanceFilter bytesPerSecond;                 // average processed bytes per second
    PerformanceFilter storageBytesPerSecond;          // average processed storage bytes per second
    uint64            archiveBytes;                   // number of bytes stored in archive
    double            compressionRatio;               // compression ratio: saved "space" [%]
    String            fileName;                       // current file
    uint64            fileDoneBytes;                  // current file bytes done
    uint64            fileTotalBytes;                 // current file bytes total
    String            storageName;                    // current storage file
    uint64            storageDoneBytes;               // current storage file bytes done
    uint64            storageTotalBytes;              // current storage file bytes total
    uint              volumeNumber;                   // current volume number
    double            volumeProgress;                 // current volume progress
  } runningInfo;
} JobNode;

/* list with enqueued jobs */
typedef struct
{
  LIST_HEADER(JobNode);

  Semaphore lock;
  uint      lastJobId;
} JobList;

/* directory info node */
typedef struct DirectoryInfoNode
{
  LIST_NODE_HEADER(struct DirectoryInfoNode);

  String          pathName;
  uint64          fileCount;
  uint64          totalFileSize;
  StringList      pathNameList;
  bool            directoryOpenFlag;
  DirectoryHandle directoryHandle;
} DirectoryInfoNode;

/* directory info list */
typedef struct
{
  LIST_HEADER(DirectoryInfoNode);
} DirectoryInfoList;

/* session id */
typedef byte SessionId[SESSION_ID_LENGTH];

/* client types */
typedef enum
{
  CLIENT_TYPE_NONE,
  CLIENT_TYPE_BATCH,
  CLIENT_TYPE_NETWORK,
} ClientTypes;

/* authorization states */
typedef enum
{
  AUTHORIZATION_STATE_WAITING,
  AUTHORIZATION_STATE_OK,
  AUTHORIZATION_STATE_FAIL,
} AuthorizationStates;

/* client info */
typedef struct
{
  ClientTypes         type;

  SessionId           sessionId;
  AuthorizationStates authorizationState;

  uint                abortId;                             // command id to abort

  union
  {
    /* i/o via file */
    struct
    {
      FileHandle   fileHandle;
    } file;

    /* i/o via network and separated processing thread */
    struct
    {
      /* connection */
      String       name;
      uint         port;
      SocketHandle socketHandle;

      /* thread */
      Thread       threads[MAX_NETWORK_CLIENT_THREADS];
      MsgQueue     commandMsgQueue;
      bool         exitFlag;
    } network;
  };

  PatternList         includePatternList;
  PatternList         excludePatternList;
  JobOptions          jobOptions;
  DirectoryInfoList   directoryInfoList;
} ClientInfo;

/* client node */
typedef struct ClientNode
{
  LIST_NODE_HEADER(struct ClientNode);

  ClientInfo clientInfo;
  String     commandString;
} ClientNode;

/* client list */
typedef struct
{
  LIST_HEADER(ClientNode);
} ClientList;

/* server command function */
typedef void(*ServerCommandFunction)(ClientInfo    *clientInfo,
                                     uint          id,
                                     const String  arguments[],
                                     uint          argumentCount
                                    );

/* server command message */
typedef struct
{
  ServerCommandFunction serverCommandFunction;
  AuthorizationStates   authorizationState;
  uint                  id;
  Array                 arguments;
} CommandMsg;

LOCAL const ConfigValueUnit CONFIG_VALUE_BYTES_UNITS[] =
{
  {"G",1024*1024*1024},
  {"M",1024*1024},
  {"K",1024},
};

LOCAL const ConfigValueUnit CONFIG_VALUE_BITS_UNITS[] =
{
  {"K",1024},
};

LOCAL const ConfigValueSelect CONFIG_VALUE_ARCHIVE_TYPES[] =
{
  {"normal",     ARCHIVE_TYPE_NORMAL,    },
  {"full",       ARCHIVE_TYPE_FULL,      },
  {"incremental",ARCHIVE_TYPE_INCREMENTAL},
};

LOCAL const ConfigValueSelect CONFIG_VALUE_PATTERN_TYPES[] =
{
  {"glob",    PATTERN_TYPE_GLOB,         },
  {"regex",   PATTERN_TYPE_REGEX,        },
  {"extended",PATTERN_TYPE_EXTENDED_REGEX},
};

LOCAL const ConfigValueSelect CONFIG_VALUE_COMPRESS_ALGORITHMS[] =
{
  {"none", COMPRESS_ALGORITHM_NONE,  },

  {"zip0", COMPRESS_ALGORITHM_ZIP_0, },
  {"zip1", COMPRESS_ALGORITHM_ZIP_1, },
  {"zip2", COMPRESS_ALGORITHM_ZIP_2, },
  {"zip3", COMPRESS_ALGORITHM_ZIP_3, },
  {"zip4", COMPRESS_ALGORITHM_ZIP_4, },
  {"zip5", COMPRESS_ALGORITHM_ZIP_5, },
  {"zip6", COMPRESS_ALGORITHM_ZIP_6, },
  {"zip7", COMPRESS_ALGORITHM_ZIP_7, },
  {"zip8", COMPRESS_ALGORITHM_ZIP_8, },
  {"zip9", COMPRESS_ALGORITHM_ZIP_9, },

  #ifdef HAVE_BZ2
    {"bzip1",COMPRESS_ALGORITHM_BZIP2_1},
    {"bzip2",COMPRESS_ALGORITHM_BZIP2_2},
    {"bzip3",COMPRESS_ALGORITHM_BZIP2_3},
    {"bzip4",COMPRESS_ALGORITHM_BZIP2_4},
    {"bzip5",COMPRESS_ALGORITHM_BZIP2_5},
    {"bzip6",COMPRESS_ALGORITHM_BZIP2_6},
    {"bzip7",COMPRESS_ALGORITHM_BZIP2_7},
    {"bzip8",COMPRESS_ALGORITHM_BZIP2_8},
    {"bzip9",COMPRESS_ALGORITHM_BZIP2_9},
  #endif /* HAVE_BZ2 */
};

LOCAL const ConfigValueSelect CONFIG_VALUE_CRYPT_ALGORITHMS[] =
{
  {"none",      CRYPT_ALGORITHM_NONE,     },

  #ifdef HAVE_GCRYPT
    {"3DES",      CRYPT_ALGORITHM_3DES      },
    {"CAST5",     CRYPT_ALGORITHM_CAST5     },
    {"BLOWFISH",  CRYPT_ALGORITHM_BLOWFISH  },
    {"AES128",    CRYPT_ALGORITHM_AES128    },
    {"AES192",    CRYPT_ALGORITHM_AES192    },
    {"AES256",    CRYPT_ALGORITHM_AES256    },
    {"TWOFISH128",CRYPT_ALGORITHM_TWOFISH128},
    {"TWOFISH256",CRYPT_ALGORITHM_TWOFISH256},
  #endif /* HAVE_GCRYPT */
};

LOCAL const ConfigValueSelect CONFIG_VALUE_CRYPT_TYPES[] =
{
  #ifdef HAVE_GCRYPT
    {"symmetric", CRYPT_TYPE_SYMMETRIC },
    {"asymmetric",CRYPT_TYPE_ASYMMETRIC},
  #endif /* HAVE_GCRYPT */
};

LOCAL const ConfigValueSelect CONFIG_VALUE_PASSWORD_MODES[] =
{
  {"default",   PASSWORD_MODE_DEFAULT,    },
  {"ask",       PASSWORD_MODE_ASK,        },
  {"config",    PASSWORD_MODE_CONFIG,     },
};

LOCAL const ConfigValue CONFIG_VALUES[] =
{
  CONFIG_STRUCT_VALUE_STRING   ("archive-name",           JobNode,archiveName                            ),
  CONFIG_STRUCT_VALUE_SELECT   ("archive-type",           JobNode,jobOptions.archiveType,                CONFIG_VALUE_ARCHIVE_TYPES),
  CONFIG_STRUCT_VALUE_INTEGER64("archive-part-size",      JobNode,jobOptions.archivePartSize,            0LL,LONG_LONG_MAX,CONFIG_VALUE_BYTES_UNITS),

  CONFIG_STRUCT_VALUE_STRING   ("incremental-list-file",  JobNode,jobOptions.incrementalListFileName     ),

  CONFIG_STRUCT_VALUE_INTEGER  ("directory-strip",        JobNode,jobOptions.directoryStripCount,        0,INT_MAX,NULL),
  CONFIG_STRUCT_VALUE_STRING   ("directory",              JobNode,jobOptions.directory                   ),

  CONFIG_STRUCT_VALUE_SELECT   ("pattern-type",           JobNode,jobOptions.patternType,                CONFIG_VALUE_PATTERN_TYPES),

  CONFIG_STRUCT_VALUE_SELECT   ("compress-algorithm",     JobNode,jobOptions.compressAlgorithm,          CONFIG_VALUE_COMPRESS_ALGORITHMS),

  CONFIG_STRUCT_VALUE_SELECT   ("crypt-algorithm",        JobNode,jobOptions.cryptAlgorithm,             CONFIG_VALUE_CRYPT_ALGORITHMS),
  CONFIG_STRUCT_VALUE_SELECT   ("crypt-type",             JobNode,jobOptions.cryptType,                  CONFIG_VALUE_CRYPT_TYPES),
  CONFIG_STRUCT_VALUE_SELECT   ("crypt-password-mode",    JobNode,jobOptions.cryptPasswordMode,          CONFIG_VALUE_PASSWORD_MODES),
  CONFIG_STRUCT_VALUE_SPECIAL  ("crypt-password",         JobNode,jobOptions.cryptPassword,              configValueParsePassword,NULL,NULL,configValueFormatPassword,NULL),
  CONFIG_STRUCT_VALUE_STRING   ("crypt-public-key",       JobNode,jobOptions.cryptPublicKeyFileName      ),

  CONFIG_STRUCT_VALUE_STRING   ("ftp-login-name",         JobNode,jobOptions.ftpServer.loginName         ),
  CONFIG_STRUCT_VALUE_SPECIAL  ("ftp-password",           JobNode,jobOptions.ftpServer.password,         configValueParsePassword,NULL,NULL,configValueFormatPassword,NULL),

  CONFIG_STRUCT_VALUE_INTEGER  ("ssh-port",               JobNode,jobOptions.sshServer.port,             0,65535,NULL),
  CONFIG_STRUCT_VALUE_STRING   ("ssh-login-name",         JobNode,jobOptions.sshServer.loginName         ),
  CONFIG_STRUCT_VALUE_SPECIAL  ("ssh-password",           JobNode,jobOptions.sshServer.password ,        configValueParsePassword,NULL,NULL,configValueFormatPassword,NULL),
  CONFIG_STRUCT_VALUE_STRING   ("ssh-public-key",         JobNode,jobOptions.sshServer.publicKeyFileName ),
  CONFIG_STRUCT_VALUE_STRING   ("ssh-private-key",        JobNode,jobOptions.sshServer.privateKeyFileName),

  CONFIG_STRUCT_VALUE_INTEGER64("volume-size",            JobNode,jobOptions.device.volumeSize,          0LL,LONG_LONG_MAX,CONFIG_VALUE_BYTES_UNITS),

  CONFIG_STRUCT_VALUE_BOOLEAN  ("skip-unreadable",        JobNode,jobOptions.skipUnreadableFlag          ),
  CONFIG_STRUCT_VALUE_BOOLEAN  ("overwrite-archive-files",JobNode,jobOptions.overwriteArchiveFilesFlag   ),
  CONFIG_STRUCT_VALUE_BOOLEAN  ("overwrite-files",        JobNode,jobOptions.overwriteFilesFlag          ),
  CONFIG_STRUCT_VALUE_BOOLEAN  ("ecc",                    JobNode,jobOptions.errorCorrectionCodesFlag    ),

  CONFIG_STRUCT_VALUE_SPECIAL  ("include",                JobNode,includePatternList,                    configValueParseIncludeExclude,configValueFormatInitIncludeExclude,configValueFormatDoneIncludeExclude,configValueFormatIncludeExclude,NULL),
  CONFIG_STRUCT_VALUE_SPECIAL  ("exclude",                JobNode,excludePatternList,                    configValueParseIncludeExclude,configValueFormatInitIncludeExclude,configValueFormatDoneIncludeExclude,configValueFormatIncludeExclude,NULL),

  CONFIG_STRUCT_VALUE_SPECIAL  ("schedule",               JobNode,scheduleList,                          configValueParseSchedule,configValueFormatInitSchedule,configValueFormatDoneSchedule,configValueFormatSchedule,NULL),
};

/***************************** Variables *******************************/
LOCAL const Password   *serverPassword;
LOCAL const char       *serverJobsDirectory;
LOCAL const JobOptions *serverDefaultJobOptions;
LOCAL JobList          jobList;
LOCAL Thread           jobThread;
LOCAL Thread           schedulerThread;
LOCAL ClientList       clientList;
LOCAL bool             pauseFlag;
LOCAL bool             quitFlag;

/****************************** Macros *********************************/

#define CHECK_JOB_IS_ACTIVE(jobNode) (   (jobNode->state == JOB_STATE_WAITING) \
                                      || (jobNode->state == JOB_STATE_RUNNING) \
                                      || (jobNode->state == JOB_STATE_REQUEST_VOLUME) \
                                     )

#define CHECK_JOB_IS_RUNNING(jobNode) (   (jobNode->state == JOB_STATE_RUNNING) \
                                       || (jobNode->state == JOB_STATE_REQUEST_VOLUME) \
                                      )

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : getArchiveTypeName
* Purpose: get archive type name
* Input  : archiveType - archive type
* Output : -
* Return : name
* Notes  : -
\***********************************************************************/

LOCAL const char *getArchiveTypeName(ArchiveTypes archiveType)
{
  int z;

  z = 0;
  while (   (z < SIZE_OF_ARRAY(CONFIG_VALUE_ARCHIVE_TYPES))
         && (archiveType != CONFIG_VALUE_ARCHIVE_TYPES[z].value)
        )
  {
    z++;
  }

  return (z < SIZE_OF_ARRAY(CONFIG_VALUE_ARCHIVE_TYPES))?CONFIG_VALUE_ARCHIVE_TYPES[z].name:"";
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
* Name   : resetJobRunningInfo
* Purpose: reset job running info
* Input  : jobNode - job node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void resetJobRunningInfo(JobNode *jobNode)
{
  assert(jobNode != NULL);

  jobNode->runningInfo.error              = ERROR_NONE;
  jobNode->runningInfo.estimatedRestTime  = 0;
  jobNode->runningInfo.doneFiles          = 0L;
  jobNode->runningInfo.doneBytes          = 0LL;
  jobNode->runningInfo.totalFiles         = 0L;
  jobNode->runningInfo.totalBytes         = 0LL;
  jobNode->runningInfo.skippedFiles       = 0L;
  jobNode->runningInfo.skippedBytes       = 0LL;
  jobNode->runningInfo.errorFiles         = 0L;
  jobNode->runningInfo.errorBytes         = 0LL;
  jobNode->runningInfo.archiveBytes       = 0LL;
  jobNode->runningInfo.compressionRatio   = 0.0;
  jobNode->runningInfo.fileDoneBytes      = 0LL;
  jobNode->runningInfo.fileTotalBytes     = 0LL;
  jobNode->runningInfo.storageDoneBytes   = 0LL;
  jobNode->runningInfo.storageTotalBytes  = 0LL;
  jobNode->runningInfo.volumeNumber       = 0;
  jobNode->runningInfo.volumeProgress     = 0.0;

  String_clear(jobNode->runningInfo.fileName   );
  String_clear(jobNode->runningInfo.storageName);

  Misc_performanceFilterClear(&jobNode->runningInfo.filesPerSecond       );
  Misc_performanceFilterClear(&jobNode->runningInfo.bytesPerSecond       );
  Misc_performanceFilterClear(&jobNode->runningInfo.storageBytesPerSecond);
}

/***********************************************************************\
* Name   : newJob
* Purpose: create new job
* Input  : jobType  - job type
*          fileName - file name or NULL
*          name     - name of job
* Output : -
* Return : job node
* Notes  : -
\***********************************************************************/

LOCAL JobNode *newJob(JobTypes     jobType,
                      const String fileName
                     )
{
  JobNode *jobNode;

  /* allocate job node */
  jobNode = LIST_NEW_NODE(JobNode);
  if (jobNode == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  /* init job node */
  jobNode->fileName                       = String_duplicate(fileName);
  jobNode->timeModified                   = 0LL;

  jobNode->type                           = jobType;
  jobNode->name                           = File_getFileBaseName(File_newFileName(),fileName);
  jobNode->archiveName                    = String_new();
  PatternList_init(&jobNode->includePatternList);
  PatternList_init(&jobNode->excludePatternList);
  List_init(&jobNode->scheduleList);
  initJobOptions(&jobNode->jobOptions);
  jobNode->modifiedFlag                   = FALSE;

  jobNode->lastExecutedDateTime           = 0LL;
  jobNode->lastCheckDateTime              = 0LL;

  jobNode->id                             = getNewJobId();
  jobNode->state                          = JOB_STATE_NONE;
  jobNode->archiveType                    = ARCHIVE_TYPE_NORMAL;
  jobNode->requestedAbortFlag             = FALSE;
  jobNode->requestedVolumeNumber          = 0;
  jobNode->volumeNumber                   = 0;

  jobNode->runningInfo.fileName           = String_new();
  jobNode->runningInfo.storageName        = String_new();

  Misc_performanceFilterInit(&jobNode->runningInfo.filesPerSecond,       10*60);
  Misc_performanceFilterInit(&jobNode->runningInfo.bytesPerSecond,       10*60);
  Misc_performanceFilterInit(&jobNode->runningInfo.storageBytesPerSecond,10*60);

  resetJobRunningInfo(jobNode);

  return jobNode;
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

fprintf(stderr,"%s,%d: \n",__FILE__,__LINE__);
  String_delete(jobNode->runningInfo.fileName);
  String_delete(jobNode->runningInfo.storageName);
  Misc_performanceFilterDone(&jobNode->runningInfo.storageBytesPerSecond);
  Misc_performanceFilterDone(&jobNode->runningInfo.bytesPerSecond);
  Misc_performanceFilterDone(&jobNode->runningInfo.filesPerSecond);

  List_done(&jobNode->scheduleList,NULL,NULL);
  PatternList_done(&jobNode->excludePatternList);
  PatternList_done(&jobNode->includePatternList);
  freeJobOptions(&jobNode->jobOptions);
  String_delete(jobNode->archiveName);
  String_delete(jobNode->name);
  String_delete(jobNode->fileName);
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

/***********************************************************************\
* Name   : findJobById
* Purpose: find job by id
* Input  : jobId - job id
* Output : -
* Return : job node or NULL if not found
* Notes  : -
\***********************************************************************/

LOCAL JobNode *findJobById(int jobId)
{
  JobNode *jobNode;

  jobNode = jobList.head;
  while ((jobNode != NULL) && (jobNode->id != jobId))
  {
    jobNode = jobNode->next;
  }

  return jobNode;
}

/***********************************************************************\
* Name   : readJobFileScheduleInfo
* Purpose: read job file schedule info
* Input  : jobNode - job node
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors readJobFileScheduleInfo(JobNode *jobNode)
{
  String     fileName,pathName,baseName;
  FileHandle fileHandle;
  Errors     error;
  String     line;
  uint64     n;

  assert(jobNode != NULL);

  jobNode->lastExecutedDateTime = 0LL;

  /* get filename*/
  fileName = File_newFileName();
  File_splitFileName(jobNode->fileName,&pathName,&baseName);
  File_setFileName(fileName,pathName);
  File_appendFileName(fileName,String_insertChar(baseName,0,'.'));
  File_deleteFileName(baseName);
  File_deleteFileName(pathName);

  if (File_exists(fileName))
  {
    /* open file .name */
    error = File_open(&fileHandle,fileName,FILE_OPENMODE_READ);
    if (error != ERROR_NONE)
    {
      File_deleteFileName(fileName);
      return error;
    }

    /* read file */
    line = String_new();
    while (!File_eof(&fileHandle))
    {
      /* read line */
      error = File_readLine(&fileHandle,line);
      if (error != ERROR_NONE) break;

      /* skip comments, empty lines */
      if ((String_length(line) == 0) || (String_index(line,0) == '#'))
      {
        continue;
      }

      /* parse */
      if (String_parse(line,STRING_BEGIN,"%lld",NULL,&n))
      {
        jobNode->lastExecutedDateTime = n;
        jobNode->lastCheckDateTime    = n;
      }
    }
    String_delete(line);

    /* close file */
    File_close(&fileHandle);

    if (error != ERROR_NONE)
    {
      File_deleteFileName(fileName);
      return error;
    }
  }

  /* free resources */
  File_deleteFileName(fileName);

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : writeJobFileScheduleInfo
* Purpose: write job file schedule info
* Input  : jobNode - job node
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors writeJobFileScheduleInfo(JobNode *jobNode)
{
  String     fileName,pathName,baseName;
  FileHandle fileHandle;
  Errors     error;

  assert(jobNode != NULL);

  /* get filename*/
  fileName = File_newFileName();
  File_splitFileName(jobNode->fileName,&pathName,&baseName);
  File_setFileName(fileName,pathName);
  File_appendFileName(fileName,String_insertChar(baseName,0,'.'));
  File_deleteFileName(baseName);
  File_deleteFileName(pathName);

  /* create file .name */
  error = File_open(&fileHandle,fileName,FILE_OPENMODE_CREATE);
  if (error != ERROR_NONE)
  {
    File_deleteFileName(fileName);
    return error;
  }

  /* write file */
  error = File_printLine(&fileHandle,"%lld",jobNode->lastExecutedDateTime);

  /* close file */
  File_close(&fileHandle);

  if (error != ERROR_NONE)
  {
    File_deleteFileName(fileName);
    return error;
  }

  /* free resources */
  File_deleteFileName(fileName);

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : readJobFile
* Purpose: read job from file
* Input  : fileName - file name
* Output : -
* Return : TRUE iff job read, FALSE otherwise (error)
* Notes  : -
\***********************************************************************/

LOCAL bool readJobFile(JobNode *jobNode)
{
  Errors     error;
  FileHandle fileHandle;
  bool       failFlag;
  uint       lineNb;
  String     line;
  String     name,value;
  ulong      nextIndex;

  assert(jobNode != NULL);
  assert(jobNode->fileName != NULL);

  /* initialise variables */
  line = String_new();

  /* open file */
  error = File_open(&fileHandle,jobNode->fileName,FILE_OPENMODE_READ);
  if (error != ERROR_NONE)
  {
    printError("Cannot open file '%s' (error: %s)!\n",
               String_cString(jobNode->fileName),
               Errors_getText(error)
              );
    String_delete(line);
    return FALSE;
  }

  /* reset values */
  PatternList_clear(&jobNode->includePatternList);
  PatternList_clear(&jobNode->excludePatternList);
  List_clear(&jobNode->scheduleList,NULL,NULL);

  /* parse file */
  failFlag = FALSE;
  lineNb   = 0;
  name     = String_new();
  value    = String_new();
  while (!File_eof(&fileHandle) && !failFlag)
  {
    /* read line */
    error = File_readLine(&fileHandle,line);
    if (error != ERROR_NONE)
    {
      printError("Cannot read file '%s' (error: %s)!\n",
                 String_cString(jobNode->fileName),
                 Errors_getText(error)
                );
      failFlag = TRUE;
      break;
    }
    String_trim(line,STRING_WHITE_SPACES);
    lineNb++;

    /* skip comments, empty lines */
    if ((String_length(line) == 0) || (String_index(line,0) == '#'))
    {
      continue;
    }

    /* parse line */
    if (String_parse(line,STRING_BEGIN,"%S=% S",&nextIndex,name,value))
    {
//      String_unquote(String_trim(String_sub(value,line,nextIndex,STRING_END),STRING_WHITE_SPACES),STRING_QUOTES);
      if (!ConfigValue_parse(String_cString(name),
                             String_cString(value),
                             CONFIG_VALUES,SIZE_OF_ARRAY(CONFIG_VALUES),
                             NULL,
                             NULL,
                             jobNode
                            )
         )
      {
        printError("Unknown or invalid config value '%s' in %s, line %ld - skipped\n",
                   String_cString(name),
                   String_cString(jobNode->fileName),
                   lineNb
                  );
//        failFlag = TRUE;
//        break;
      }
    }
    else
    {
      printError("Error in %s, line %ld: '%s' - skipped\n",
                 String_cString(jobNode->fileName),
                 lineNb,
                 String_cString(line)
                );
//      failFlag = TRUE;
//      break;
    }
  }
  String_delete(value);
  String_delete(name);

  /* close file */
  File_close(&fileHandle);

  /* save time modified */
  jobNode->timeModified = File_getFileTimeModified(jobNode->fileName);

  /* read schedule info */
  readJobFileScheduleInfo(jobNode);

  /* free resources */
  String_delete(line);

  return !failFlag;
}

/***********************************************************************\
* Name   : rereadJobFiles
* Purpose: re-read job files
* Input  : jobsDirectory - jobs directory
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors rereadJobFiles(const char *jobsDirectory)
{
  Errors          error;
  DirectoryHandle directoryHandle;
  String          fileName;
  String          baseName;
  JobNode         *jobNode,*deleteJobNode;

  assert(jobsDirectory != NULL);

  /* init variables */
  fileName = File_newFileName();

  /* add new/update jobs */
  File_setFileNameCString(fileName,jobsDirectory);
  error = File_openDirectory(&directoryHandle,fileName);
  if (error != ERROR_NONE)
  {
    File_deleteFileName(fileName);
    return error;
  }
  baseName = File_newFileName();
  while (!File_endOfDirectory(&directoryHandle))
  {
    /* read directory entry */
    File_readDirectory(&directoryHandle,fileName);

    /* get base name */
    File_getFileBaseName(baseName,fileName);

    /* check if readable file and not ".*" */
    if (File_isFile(fileName) && File_isReadable(fileName) && (String_index(baseName,0) != '.'))
    {
      /* lock */
      Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

      /* find/create job */
      jobNode = jobList.head;
      while ((jobNode != NULL) && !String_equals(jobNode->name,baseName))
      {
        jobNode = jobNode->next;
      }
      if (jobNode == NULL)
      {
        jobNode = newJob(JOB_TYPE_BACKUP,fileName);
        List_append(&jobList,jobNode);
      }

      if (   !CHECK_JOB_IS_ACTIVE(jobNode)
          && (jobNode->timeModified < File_getFileTimeModified(fileName))
         )
      {
        /* read job */
        readJobFile(jobNode);
      }

      /* unlock */
      Semaphore_unlock(&jobList.lock);
    }
  }
  File_deleteFileName(baseName);
  File_closeDirectory(&directoryHandle);

  /* remove not existing jobs */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);
  jobNode = jobList.head;
  while (jobNode != NULL)
  {
    if (jobNode->state == JOB_STATE_NONE)
    {
      File_setFileNameCString(fileName,jobsDirectory);
      File_appendFileName(fileName,jobNode->name);
      if (File_isFile(fileName) && File_isReadable(fileName))
      {
        /* exists => ok */
        jobNode = jobNode->next;
      }
      else
      {
        /* not exists => delete job node */
        deleteJobNode = jobNode;
        jobNode = jobNode->next;
        List_remove(&jobList,deleteJobNode);
        deleteJob(deleteJobNode);
      }
    }
    else
    {
      jobNode = jobNode->next;
    }
  }
  Semaphore_unlock(&jobList.lock);

  /* free resources */
  File_deleteFileName(fileName);

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : deleteJobFileEntries
* Purpose: delete entries from job file
* Input  : stringList - job file string list to modify
*          name       - name of value
* Output : -
* Return : next entry in string list or NULL
* Notes  : -
\***********************************************************************/

LOCAL StringNode *deleteJobFileEntries(StringList *stringList,
                                       const char *name
                                      )
{
  StringNode *nextNode;

  StringNode *stringNode;
  String     s;

  nextNode = NULL;

  s = String_new();
  stringNode = stringList->head;
  while (stringNode != NULL)
  {
    /* skip comments, empty lines */
    if ((String_length(stringNode->string) == 0) || (String_index(stringNode->string,0) == '#'))
    {
      stringNode = stringNode->next;
      continue;
    }

    /* parse and delete */
    if (String_parse(stringNode->string,STRING_BEGIN,"%S=% S",NULL,s,NULL) && String_equalsCString(s,name))
    {
      if ((nextNode == NULL) || (nextNode = stringNode)) nextNode = stringNode->next;
      stringNode = StringList_remove(stringList,stringNode);
    }
    else
    {
      stringNode = stringNode->next;
    }
  }
  String_delete(s);

  return nextNode;
}

/***********************************************************************\
* Name   : updateJobFile
* Purpose: update job file
* Input  : jobNode - job node
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors updateJobFile(JobNode *jobNode)
{
  StringList jobFileList;
  String     line;
  Errors     error;
  FileHandle fileHandle;
  uint       z;
  StringNode *nextNode;
  ConfigValueFormat configValueFormat;

  assert(jobNode != NULL);

  StringList_init(&jobFileList);
  line  = String_new();

  /* read file */
  error = File_open(&fileHandle,jobNode->fileName,FILE_OPENMODE_READ);
  if (error != ERROR_NONE)
  {
    StringList_done(&jobFileList);
    String_delete(line);
    return error;
  }
  line  = String_new();
  while (!File_eof(&fileHandle))
  {
    /* read line */
    error = File_readLine(&fileHandle,line);
    if (error != ERROR_NONE) break;

    StringList_append(&jobFileList,line);
  }
  File_close(&fileHandle);
  if (error != ERROR_NONE)
  {
    StringList_done(&jobFileList);
    String_delete(line);
    return error;
  }

  /* update in line list */
  for (z = 0; z < SIZE_OF_ARRAY(CONFIG_VALUES); z++)
  {
    /* delete old entries, get position for insert new entries */
    nextNode = deleteJobFileEntries(&jobFileList,CONFIG_VALUES[z].name);

    /* insert new entries */
    ConfigValue_formatInit(&configValueFormat,
                           &CONFIG_VALUES[z],
                           CONFIG_VALUE_FORMAT_MODE_LINE,
                           jobNode
                          );
    while (ConfigValue_format(&configValueFormat,line))
    {
      StringList_insert(&jobFileList,line,nextNode);
    }
    ConfigValue_formatDone(&configValueFormat);
  }
//StringList_print(&jobFileList);
//  return ERROR_NONE;

  /* write file */
  error = File_open(&fileHandle,jobNode->fileName,FILE_OPENMODE_CREATE);
  if (error != ERROR_NONE)
  {
    String_delete(line);
    StringList_done(&jobFileList);
    return error;
  }
  while (!StringList_empty(&jobFileList))
  {
    StringList_getFirst(&jobFileList,line);
    error = File_writeLine(&fileHandle,line);
    if (error != ERROR_NONE) break;
  }
  File_close(&fileHandle);
  if (error != ERROR_NONE)
  {
    String_delete(line);
    StringList_done(&jobFileList);
    return error;
  }

  /* save time modified */
  jobNode->timeModified = File_getFileTimeModified(jobNode->fileName);

  /* free resources */
  String_delete(line);
  StringList_done(&jobFileList);

  /* reset modified flag */
  jobNode->modifiedFlag = FALSE;

  return ERROR_NONE;
}

/***********************************************************************\
* Name   : updateAllJobFiles
* Purpose: update all job files
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateAllJobFiles(void)
{
  JobNode *jobNode;

  jobNode = jobList.head;
  while (jobNode != NULL)
  {
    if (jobNode->modifiedFlag)
    {
      updateJobFile(jobNode);
    }

    jobNode = jobNode->next;
  }
}

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : updateCreateStatus
* Purpose: update create status
* Input  : jobNode          - job node
*          error            - error code
*          createStatusInfo - create status info data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateCreateStatus(JobNode                *jobNode,
                              Errors                 error,
                              const CreateStatusInfo *createStatusInfo
                             )
{
  double filesPerSecond,bytesPerSecond,storageBytesPerSecond;
  ulong  restFiles;
  uint64 restBytes;
  uint64 restStorageBytes;
  ulong  estimatedRestTime;

  assert(jobNode != NULL);
  assert(createStatusInfo != NULL);
  assert(createStatusInfo->fileName != NULL);
  assert(createStatusInfo->storageName != NULL);

  /* calculate estimated rest time */
  Misc_performanceFilterAdd(&jobNode->runningInfo.filesPerSecond,       createStatusInfo->doneFiles);
  Misc_performanceFilterAdd(&jobNode->runningInfo.bytesPerSecond,       createStatusInfo->doneBytes);
  Misc_performanceFilterAdd(&jobNode->runningInfo.storageBytesPerSecond,createStatusInfo->storageDoneBytes);
  filesPerSecond        = Misc_performanceFilterGetAverageValue(&jobNode->runningInfo.filesPerSecond       );
  bytesPerSecond        = Misc_performanceFilterGetAverageValue(&jobNode->runningInfo.bytesPerSecond       );
  storageBytesPerSecond = Misc_performanceFilterGetAverageValue(&jobNode->runningInfo.storageBytesPerSecond);

  restFiles        = (createStatusInfo->totalFiles        > createStatusInfo->doneFiles       )?createStatusInfo->totalFiles       -createStatusInfo->doneFiles       :0L;
  restBytes        = (createStatusInfo->totalBytes        > createStatusInfo->doneBytes       )?createStatusInfo->totalBytes       -createStatusInfo->doneBytes       :0LL;
  restStorageBytes = (createStatusInfo->storageTotalBytes > createStatusInfo->storageDoneBytes)?createStatusInfo->storageTotalBytes-createStatusInfo->storageDoneBytes:0LL;
  estimatedRestTime = 0;
  if (filesPerSecond        > 0) { estimatedRestTime = MAX(estimatedRestTime,(ulong)round((double)restFiles/filesPerSecond              )); }
  if (bytesPerSecond        > 0) { estimatedRestTime = MAX(estimatedRestTime,(ulong)round((double)restBytes/bytesPerSecond              )); }
  if (storageBytesPerSecond > 0) { estimatedRestTime = MAX(estimatedRestTime,(ulong)round((double)restStorageBytes/storageBytesPerSecond)); }

/*
fprintf(stderr,"%s,%d: createStatusInfo->doneFiles=%lu createStatusInfo->doneBytes=%llu jobNode->runningInfo.totalFiles=%lu jobNode->runningInfo.totalBytes %llu -- filesPerSecond=%f bytesPerSecond=%f estimatedRestTime=%lus\n",__FILE__,__LINE__,
createStatusInfo->doneFiles,
createStatusInfo->doneBytes,
jobNode->runningInfo.totalFiles,
jobNode->runningInfo.totalBytes,
filesPerSecond,bytesPerSecond,estimatedRestTime);
*/

  jobNode->runningInfo.error                 = error;
  jobNode->runningInfo.doneFiles             = createStatusInfo->doneFiles;
  jobNode->runningInfo.doneBytes             = createStatusInfo->doneBytes;
  jobNode->runningInfo.totalFiles            = createStatusInfo->totalFiles;
  jobNode->runningInfo.totalBytes            = createStatusInfo->totalBytes;
  jobNode->runningInfo.skippedFiles          = createStatusInfo->skippedFiles;
  jobNode->runningInfo.skippedBytes          = createStatusInfo->skippedBytes;
  jobNode->runningInfo.errorFiles            = createStatusInfo->errorFiles;
  jobNode->runningInfo.errorBytes            = createStatusInfo->errorBytes;
  jobNode->runningInfo.archiveBytes          = createStatusInfo->archiveBytes;
  jobNode->runningInfo.compressionRatio      = createStatusInfo->compressionRatio;
  jobNode->runningInfo.estimatedRestTime     = estimatedRestTime;
  String_set(jobNode->runningInfo.fileName,createStatusInfo->fileName);
  jobNode->runningInfo.fileDoneBytes         = createStatusInfo->fileDoneBytes;
  jobNode->runningInfo.fileTotalBytes        = createStatusInfo->fileTotalBytes;
  String_set(jobNode->runningInfo.storageName,createStatusInfo->storageName);
  jobNode->runningInfo.storageDoneBytes      = createStatusInfo->storageDoneBytes;
  jobNode->runningInfo.storageTotalBytes     = createStatusInfo->storageTotalBytes;
  jobNode->runningInfo.volumeNumber          = createStatusInfo->volumeNumber;
  jobNode->runningInfo.volumeProgress        = createStatusInfo->volumeProgress;
//fprintf(stderr,"%s,%d: createStatusInfo->fileName=%s\n",__FILE__,__LINE__,String_cString(jobNode->runningInfo.fileName));
}

/***********************************************************************\
* Name   : updateRestoreStatus
* Purpose: update restore status
* Input  : jobNode           - job node
*          error             - error code
*          restoreStatusInfo - create status info data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateRestoreStatus(JobNode                 *jobNode,
                               Errors                  error,
                               const RestoreStatusInfo *restoreStatusInfo
                              )
{
  double filesPerSecond,bytesPerSecond,storageBytesPerSecond;

  assert(jobNode != NULL);
  assert(restoreStatusInfo != NULL);
  assert(restoreStatusInfo->fileName != NULL);
  assert(restoreStatusInfo->storageName != NULL);

  /* calculate estimated rest time */
  Misc_performanceFilterAdd(&jobNode->runningInfo.filesPerSecond,       restoreStatusInfo->doneFiles);
  Misc_performanceFilterAdd(&jobNode->runningInfo.bytesPerSecond,       restoreStatusInfo->doneBytes);
  Misc_performanceFilterAdd(&jobNode->runningInfo.storageBytesPerSecond,restoreStatusInfo->storageDoneBytes);
  filesPerSecond        = Misc_performanceFilterGetAverageValue(&jobNode->runningInfo.filesPerSecond       );
  bytesPerSecond        = Misc_performanceFilterGetAverageValue(&jobNode->runningInfo.bytesPerSecond       );
  storageBytesPerSecond = Misc_performanceFilterGetAverageValue(&jobNode->runningInfo.storageBytesPerSecond);

/*
fprintf(stderr,"%s,%d: restoreStatusInfo->doneFiles=%lu restoreStatusInfo->doneBytes=%llu jobNode->runningInfo.totalFiles=%lu jobNode->runningInfo.totalBytes %llu -- filesPerSecond=%f bytesPerSecond=%f estimatedRestTime=%lus\n",__FILE__,__LINE__,
restoreStatusInfo->doneFiles,
restoreStatusInfo->doneBytes,
jobNode->runningInfo.totalFiles,
jobNode->runningInfo.totalBytes,
filesPerSecond,bytesPerSecond,estimatedRestTime);
*/

  jobNode->runningInfo.error                 = error;
  jobNode->runningInfo.doneFiles             = restoreStatusInfo->doneFiles;
  jobNode->runningInfo.doneBytes             = restoreStatusInfo->doneBytes;
  jobNode->runningInfo.skippedFiles          = restoreStatusInfo->skippedFiles;
  jobNode->runningInfo.skippedBytes          = restoreStatusInfo->skippedBytes;
  jobNode->runningInfo.errorFiles            = restoreStatusInfo->errorFiles;
  jobNode->runningInfo.errorBytes            = restoreStatusInfo->errorBytes;
  jobNode->runningInfo.archiveBytes          = 0LL;
  jobNode->runningInfo.compressionRatio      = 0.0;
  jobNode->runningInfo.estimatedRestTime     = 0;
  String_set(jobNode->runningInfo.fileName,restoreStatusInfo->fileName);
  jobNode->runningInfo.fileDoneBytes         = restoreStatusInfo->fileDoneBytes;
  jobNode->runningInfo.fileTotalBytes        = restoreStatusInfo->fileTotalBytes;
  String_set(jobNode->runningInfo.storageName,restoreStatusInfo->storageName);
  jobNode->runningInfo.storageDoneBytes      = restoreStatusInfo->storageDoneBytes;
  jobNode->runningInfo.storageTotalBytes     = restoreStatusInfo->storageTotalBytes;
  jobNode->runningInfo.volumeNumber          = 0; // ???
  jobNode->runningInfo.volumeProgress        = 0.0; // ???
//fprintf(stderr,"%s,%d: restoreStatusInfo->fileName=%s\n",__FILE__,__LINE__,String_cString(jobNode->runningInfo.fileName));
}

/***********************************************************************\
* Name   : storageRequestVolume
* Purpose: request volume call-back
* Input  : jobNode      - job node
*          volumeNumber - volume number
* Output : -
* Return : TRUE if volume loaded, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool storageRequestVolume(JobNode *jobNode,
                                uint    volumeNumber
                               )
{
  assert(jobNode != NULL);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

//??? lock nicht readwrite

  /* request volume */
  jobNode->requestedVolumeNumber = volumeNumber;

  /* wait until volume is available or job is aborted */
  assert(jobNode->state == JOB_STATE_RUNNING);
  jobNode->state = JOB_STATE_REQUEST_VOLUME;
  do
  {
    Semaphore_waitModified(&jobList.lock);
  }
  while (   !jobNode->requestedAbortFlag
         && (jobNode->volumeNumber != jobNode->requestedVolumeNumber)
        );
  jobNode->state = JOB_STATE_RUNNING;

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  return (jobNode->volumeNumber == jobNode->requestedVolumeNumber);
}

/***********************************************************************\
* Name   : jobThreadEntry
* Purpose: job thread entry
* Input  : jobList - job list
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void jobThreadEntry(void)
{
  JobNode      *jobNode;
  String       archiveName;
  PatternList  includePatternList,excludePatternList;
  JobOptions   jobOptions;
  ArchiveTypes archiveType;

  /* initialize variables */
  archiveName = String_new();
  PatternList_init(&includePatternList);
  PatternList_init(&excludePatternList);

  while (!quitFlag)
  {
    /* lock */
    Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);

    /* get next job */
    do
    {
      jobNode = jobList.head;
      while ((jobNode != NULL) && (jobNode->state != JOB_STATE_WAITING))
      {
        jobNode = jobNode->next;
      }
      if (jobNode == NULL) Semaphore_waitModified(&jobList.lock);
    }
    while (!quitFlag && (jobNode == NULL));
    if (quitFlag)
    {
      Semaphore_unlock(&jobList.lock);
      break;
    }
    assert(jobNode != NULL);

    /* get copy of mandatory job data */
    String_set(archiveName,jobNode->archiveName);
    PatternList_clear(&includePatternList); PatternList_copy(&jobNode->includePatternList,&includePatternList);
    PatternList_clear(&excludePatternList); PatternList_copy(&jobNode->excludePatternList,&excludePatternList);
    initJobOptions(&jobOptions); copyJobOptions(&jobNode->jobOptions,&jobOptions);
    archiveType = jobNode->archiveType,

    /* unlock is ok; job is now protected by running state */
    jobNode->state = JOB_STATE_RUNNING;
    Semaphore_unlock(&jobList.lock);

    /* run job */
#ifdef SIMULATOR
{
  int z;

  jobNode->runningInfo.estimatedRestTime=120;

  jobNode->runningInfo.totalFiles+=60;
  jobNode->runningInfo.totalBytes+=6000;

  for (z=0;z<120;z++)
  {
    extern void sleep(int);
    if (jobNode->requestedAbortFlag) break;

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
    /* run job */
    logMessage(LOG_TYPE_ALWAYS,"------------------------------------------------------------");
    switch (jobNode->type)
    {
      case JOB_TYPE_BACKUP:
        /* create archive */
        logMessage(LOG_TYPE_ALWAYS,"start create '%s'",String_cString(archiveName));
        jobNode->runningInfo.error = Command_create(String_cString(archiveName),
                                                    &includePatternList,
                                                    &excludePatternList,
                                                    &jobOptions,
                                                    archiveType,
                                                    (CreateStatusInfoFunction)updateCreateStatus,
                                                    jobNode,
                                                    (StorageRequestVolumeFunction)storageRequestVolume,
                                                    jobNode,
                                                    &pauseFlag,
                                                    &jobNode->requestedAbortFlag
                                                   );
        logMessage(LOG_TYPE_ALWAYS,"done create '%s' (error: %s)",String_cString(archiveName),Errors_getText(jobNode->runningInfo.error));
        break;
      case JOB_TYPE_RESTORE:
        {
          StringList archiveFileNameList;

          logMessage(LOG_TYPE_ALWAYS,"start restore '%s'",String_cString(archiveName));
          StringList_init(&archiveFileNameList);
          StringList_append(&archiveFileNameList,archiveName);
          jobNode->runningInfo.error = Command_restore(&archiveFileNameList,
                                                       &includePatternList,
                                                       &excludePatternList,
                                                       &jobOptions,
                                                       (RestoreStatusInfoFunction)updateRestoreStatus,
                                                       jobNode,
                                                       &pauseFlag,
                                                       &jobNode->requestedAbortFlag
                                                      );
          StringList_done(&archiveFileNameList);
          logMessage(LOG_TYPE_ALWAYS,"done restore '%s' (error: %s)",String_cString(archiveName),Errors_getText(jobNode->runningInfo.error));
        }
        break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break;
      #endif /* NDEBUG */
    }
    logPostProcess();

#endif /* SIMULATOR */

    /* free resources */
    freeJobOptions(&jobOptions);
    PatternList_clear(&excludePatternList);
    PatternList_clear(&includePatternList);

    /* lock */
    Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

    /* done job */
    jobNode->lastExecutedDateTime = Misc_getCurrentDateTime();
    if      (jobNode->requestedAbortFlag)
    {
      jobNode->state = JOB_STATE_ABORTED;
    }
    else if (jobNode->runningInfo.error != ERROR_NONE)
    {
      jobNode->state = JOB_STATE_ERROR;
    }
    else
    {
      jobNode->state = JOB_STATE_DONE;
    }

    /* store schedule info */
    writeJobFileScheduleInfo(jobNode);

    /* unlock */
    Semaphore_unlock(&jobList.lock);
  }

  /* free resources */
  PatternList_done(&excludePatternList);
  PatternList_done(&includePatternList);
  String_delete(archiveName);  
}

/*---------------------------------------------------------------------*/

LOCAL void schedulerThreadEntry(void)
{
  JobNode      *jobNode;
  uint64       currentDateTime;
  uint64       dateTime;
  uint         year,month,day,hour,minute;
  WeekDays     weekDay;
  ScheduleNode *executeScheduleNode;
  ScheduleNode *scheduleNode;
  bool         pendingFlag;
  int          z;

  while (!quitFlag)
  {
    /* update job files */
    Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);
    updateAllJobFiles();
    Semaphore_unlock(&jobList.lock);

    /* re-read config files */
    rereadJobFiles(serverJobsDirectory);

    /* trigger jobs */
    Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);
    currentDateTime = Misc_getCurrentDateTime();
    jobNode         = jobList.head;
    pendingFlag     = FALSE;
    while ((jobNode != NULL) && !pendingFlag)
    {
      /* check if job have to be executed */
      executeScheduleNode = NULL;
      if (!CHECK_JOB_IS_ACTIVE(jobNode))
      {
        dateTime = currentDateTime;
        while (   ((dateTime/60LL) > (jobNode->lastCheckDateTime/60LL))
               && (executeScheduleNode == NULL)
               && !pendingFlag
              )
        {
          // get date/time values
          Misc_splitDateTime(dateTime,
                             &year,
                             &month,
                             &day,
                             &hour,
                             &minute,
                             NULL,
                             &weekDay
                            );

          // check if matching with schedule list node
          scheduleNode = jobNode->scheduleList.head;
          while ((scheduleNode != NULL) && (executeScheduleNode == NULL))
          {
            if (   ((scheduleNode->year     == SCHEDULE_ANY    ) || (scheduleNode->year   == year  )      )
                && ((scheduleNode->month    == SCHEDULE_ANY    ) || (scheduleNode->month  == month )      )
                && ((scheduleNode->day      == SCHEDULE_ANY    ) || (scheduleNode->day    == day   )      )
                && ((scheduleNode->hour     == SCHEDULE_ANY    ) || (scheduleNode->hour   == hour  )      )
                && ((scheduleNode->minute   == SCHEDULE_ANY    ) || (scheduleNode->minute == minute)      )
                && ((scheduleNode->weekDays == SCHEDULE_ANY_DAY) || IN_SET(scheduleNode->weekDays,weekDay))
                && scheduleNode->enabled
               )
            {
              executeScheduleNode = scheduleNode;
            }
            scheduleNode = scheduleNode->next;
          }

          // check if other thread pending for job list
          pendingFlag = Semaphore_checkPending(&jobList.lock);

          // next time
          dateTime -= 60LL;
        }
        if (!pendingFlag)
        {
          jobNode->lastCheckDateTime = currentDateTime;
        }
      }

      /* trigger job */
      if (executeScheduleNode != NULL)
      {
        /* set state */
        jobNode->state              = JOB_STATE_WAITING;
        jobNode->archiveType        = executeScheduleNode->archiveType;
        jobNode->requestedAbortFlag = FALSE;
        resetJobRunningInfo(jobNode);
      }

      /* check next job */
      jobNode = jobNode->next;
    }
    Semaphore_unlock(&jobList.lock);

    /* sleep 1min, check update and quit flag */
    z = 0;
    while ((z < 60) && !quitFlag)
    {
      Misc_udelay(10LL*1000LL*1000LL);
      z+=10;
    }
  }
}

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : sendClient
* Purpose: send data to client
* Input  : clientInfo - client info
*          data       - data string
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void sendClient(ClientInfo *clientInfo, String data)
{
  Errors error;

  assert(clientInfo != NULL);
  assert(data != NULL);

  #ifdef SERVER_DEBUG
    fprintf(stderr,"%s,%d: result=%s",__FILE__,__LINE__,String_cString(data));
  #endif /* SERVER_DEBUG */

  switch (clientInfo->type)
  {
    case CLIENT_TYPE_BATCH:
      error = File_write(&clientInfo->file.fileHandle,String_cString(data),String_length(data));
//if (error != ERROR_NONE) fprintf(stderr,"%s,%d: WRITE ERROR X1\n",__FILE__,__LINE__);
fflush(stdout);
      break;
    case CLIENT_TYPE_NETWORK:
      error = Network_send(&clientInfo->network.socketHandle,String_cString(data),String_length(data));
//if (error != ERROR_NONE) fprintf(stderr,"%s,%d: WRITE ERROR X2\n",__FILE__,__LINE__);
      break;
    default:
      #ifndef NDEBUG
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
      #endif /* NDEBUG */
      break;
  }
//fprintf(stderr,"%s,%d: sent data: '%s'",__FILE__,__LINE__,String_cString(result));
}

/***********************************************************************\
* Name   : sendResult
* Purpose: send result to client
* Input  : clientInfo   - client info
*          id           - command id
*          completeFlag - TRUE if command is completed, FALSE otherwise
*          errorCode    - error code
*          format       - format string
*          ...          - optional arguments
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void sendResult(ClientInfo *clientInfo, uint id, bool completeFlag, uint errorCode, const char *format, ...)
{
  String  result;
  va_list arguments;

  result = String_new();

  String_format(result,"%d %d %d ",id,completeFlag?1:0,Errors_getCode(errorCode));
  va_start(arguments,format);
  String_vformat(result,format,arguments);
  va_end(arguments);
//fprintf(stderr,"%s,%d: result=%s\n",__FILE__,__LINE__,String_cString(result));
  String_appendChar(result,'\n');

  sendClient(clientInfo,result);

  String_delete(result);
}

/***********************************************************************\
* Name   : commandAborted
* Purpose: check if command was aborted
* Input  : clientInfo - client info
* Output : -
* Return : TRUE if command execution aborted or client disconnected,
*          FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool commandAborted(ClientInfo *clientInfo, uint commandId)
{
  bool abortedFlag;

  assert(clientInfo != NULL);

  abortedFlag = (clientInfo->abortId == commandId);
  switch (clientInfo->type)
  {
    case CLIENT_TYPE_BATCH:
      break;
    case CLIENT_TYPE_NETWORK:
      if (clientInfo->network.exitFlag) abortedFlag = TRUE;
      break;
    default:
      #ifndef NDEBUG
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
      #endif /* NDEBUG */
      break;
  }

  return abortedFlag;
}

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : getJobStateText
* Purpose: get text for job state
* Input  : jobState - job state
* Output : -
* Return : text
* Notes  : -
\***********************************************************************/

LOCAL const char *getJobStateText(JobStates jobState)
{
  const char *stateText;

  stateText = "unknown";
  switch (jobState)
  {
    case JOB_STATE_NONE:           stateText = "-";              break;
    case JOB_STATE_WAITING:        stateText = "waiting";        break;
    case JOB_STATE_RUNNING:        stateText = "running";        break;
    case JOB_STATE_REQUEST_VOLUME: stateText = "request volume"; break;
    case JOB_STATE_DONE:           stateText = "done";           break;
    case JOB_STATE_ERROR:          stateText = "ERROR";          break;
    case JOB_STATE_ABORTED:        stateText = "aborted";        break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return stateText;
}

/***********************************************************************\
* Name   : newDirectoryInfo
* Purpose: create new directory info
* Input  : pathName  - path name
* Output : -
* Return : directory info node
* Notes  : -
\***********************************************************************/

LOCAL DirectoryInfoNode *newDirectoryInfo(const String pathName)
{
  DirectoryInfoNode *directoryInfoNode;

  assert(pathName != NULL);

  /* allocate job node */
  directoryInfoNode = LIST_NEW_NODE(DirectoryInfoNode);
  if (directoryInfoNode == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  /* init directory info node */
  directoryInfoNode->pathName          = String_duplicate(pathName);
  directoryInfoNode->fileCount         = 0LL;
  directoryInfoNode->totalFileSize     = 0LL;
  directoryInfoNode->directoryOpenFlag = FALSE;

  /* init path name list */
  StringList_init(&directoryInfoNode->pathNameList);
  StringList_append(&directoryInfoNode->pathNameList,pathName);

  return directoryInfoNode;
}

/***********************************************************************\
* Name   : freeDirectoryInfoNode
* Purpose: free directory info node
* Input  : directoryInfoNode - directory info node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeDirectoryInfoNode(DirectoryInfoNode *directoryInfoNode)
{
  assert(directoryInfoNode != NULL);

  if (directoryInfoNode->directoryOpenFlag)
  {
    File_closeDirectory(&directoryInfoNode->directoryHandle);
  }
  StringList_done(&directoryInfoNode->pathNameList);
  String_delete(directoryInfoNode->pathName);
}

/***********************************************************************\
* Name   : findJobById
* Purpose: find job by id
* Input  : jobId - job id
* Output : -
* Return : job node or NULL if not found
* Notes  : -
\***********************************************************************/

LOCAL DirectoryInfoNode *findDirectoryInfo(DirectoryInfoList *directoryInfoList, const String pathName)
{
  DirectoryInfoNode *directoryInfoNode;

  assert(directoryInfoList != NULL);
  assert(pathName != NULL);

  directoryInfoNode = directoryInfoList->head;
  while ((directoryInfoNode != NULL) && !String_equals(directoryInfoNode->pathName,pathName))
  {
    directoryInfoNode = directoryInfoNode->next;
  }

  return directoryInfoNode;
}

/***********************************************************************\
* Name   : getDirectoryInfo
* Purpose: get directory info: number of files, total file size
* Input  : directory - path name
*          timeout   - timeout [ms] or -1 for no timeout
* Output : fileCount     - number of files
*          totalFileSize - total file size (bytes)
*          timeoutFlag   - TRUE iff timeout, FALSE otherwise
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void getDirectoryInfo(DirectoryInfoNode *directoryInfoNode,
                            long              timeout,
                            uint64            *fileCount,
                            uint64            *totalFileSize,
                            bool              *timeoutFlag
                           )
{
  uint64          startTimestamp;
  String          pathName;
  DirectoryHandle directoryHandle;
  String          fileName;
  FileInfo        fileInfo;
  Errors          error;
uint n;

  assert(directoryInfoNode != NULL);
  assert(fileCount != NULL);
  assert(totalFileSize != NULL);

  /* get start timestamp */
  startTimestamp = Misc_getTimestamp();

fprintf(stderr,"%s,%d: ---------------- start %s timeout=%d %llu %llu\n",__FILE__,__LINE__,
String_cString(directoryInfoNode->pathName),
timeout,
directoryInfoNode->fileCount,
directoryInfoNode->totalFileSize
);
n=0;
  pathName = String_new();
  fileName = String_new();
  if (timeoutFlag != NULL) (*timeoutFlag) = FALSE;
  while (   (   !StringList_empty(&directoryInfoNode->pathNameList)
             || directoryInfoNode->directoryOpenFlag
            )
         && ((timeoutFlag == NULL) || !(*timeoutFlag))
         && !quitFlag
        )
  { 
    if (!directoryInfoNode->directoryOpenFlag)
    {
      /* process FIFO for deep-first search; this keep the directory list shorter */
      StringList_getLast(&directoryInfoNode->pathNameList,pathName);

      /* open diretory for reading */
      error = File_openDirectory(&directoryInfoNode->directoryHandle,pathName);
      if (error != ERROR_NONE)
      {
        continue;
      }
      directoryInfoNode->directoryOpenFlag = TRUE;
    }

    /* read directory content */
    while (   !File_endOfDirectory(&directoryInfoNode->directoryHandle)
           && ((timeoutFlag == NULL) || !(*timeoutFlag))
           && !quitFlag
          )
    {
      /* read next directory entry */
      error = File_readDirectory(&directoryInfoNode->directoryHandle,fileName);
      if (error != ERROR_NONE)
      {
        continue;
      }
      directoryInfoNode->fileCount++;
if ((directoryInfoNode->fileCount%10000)==0)
{
 fprintf(stderr,"%s,%d: %s count=%llu listcount=%d\n",__FILE__,__LINE__,String_cString(pathName),directoryInfoNode->fileCount,directoryInfoNode->pathNameList.count);
}
      error = File_getFileInfo(fileName,&fileInfo);
      if (error != ERROR_NONE)
      {
        continue;
      }

      switch (File_getType(fileName))
      {
        case FILE_TYPE_FILE:
          directoryInfoNode->totalFileSize += fileInfo.size;
n++;
          break;
        case FILE_TYPE_DIRECTORY:
          StringList_append(&directoryInfoNode->pathNameList,fileName);
          break;
        case FILE_TYPE_LINK:
          break;
        default:
          break;
      }

      /* check for timeout */
      if ((timeout >= 0) && (timeoutFlag != NULL))
      {
        (*timeoutFlag) = (Misc_getTimestamp() >= (startTimestamp+timeout*1000));
      }
    }

    if ((timeoutFlag == NULL) || !(*timeoutFlag))
    {
      /* close diretory */
      directoryInfoNode->directoryOpenFlag = FALSE;
      File_closeDirectory(&directoryInfoNode->directoryHandle);
    }

    /* check for timeout */
    if ((timeout >= 0) && (timeoutFlag != NULL))
    {
      (*timeoutFlag) = (Misc_getTimestamp() >= (startTimestamp+timeout*1000));
    }
  }
  String_delete(pathName);
  String_delete(fileName);

  /* get values */
  (*fileCount)     = directoryInfoNode->fileCount;
  (*totalFileSize) = directoryInfoNode->totalFileSize;
  
fprintf(stderr,"%s,%d: done %s timeout=%d %llu %llu n=%u\n",__FILE__,__LINE__,
String_cString(directoryInfoNode->pathName),
timeout,
directoryInfoNode->fileCount,
directoryInfoNode->totalFileSize,
n
);
}

/*---------------------------------------------------------------------*/

LOCAL void serverCommand_authorize(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  bool okFlag;

  uint z;
  char s[3];
  char n0,n1;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get encoded password (to avoid plain text passwords in memory) */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected password");
    return;
  }

  /* check password */
  okFlag = TRUE;
  if (serverPassword != NULL)
  {
    z = 0;
    while ((z < SESSION_ID_LENGTH) && okFlag)
    {
      n0 = (char)(strtoul(String_subCString(s,arguments[0],z*2,2),NULL,16) & 0xFF);
      n1 = clientInfo->sessionId[z];
      if (z < Password_length(serverPassword))
      {
        okFlag = (Password_getChar(serverPassword,z) == (n0^n1));
      }
      else
      {
        okFlag = (n0 == n1);
      }
      z++;
    }
  }

  if (okFlag)
  {
    clientInfo->authorizationState = AUTHORIZATION_STATE_OK;
  }
  else
  {
    clientInfo->authorizationState = AUTHORIZATION_STATE_FAIL;
  }

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_abort(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint commandId;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected id");
    return;
  }
  commandId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* abort command */
  clientInfo->abortId = commandId;

  /* format result */
  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_status(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  /* format result */
  sendResult(clientInfo,id,TRUE,0,
             "%s",
             pauseFlag?"pause":"ok"
            );
}

LOCAL void serverCommand_pause(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  pauseFlag = TRUE;

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_continue(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  pauseFlag = FALSE;

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_errorInfo(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  Errors error;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get error code */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected error code");
    return;
  }
  error = (Errors)String_toInteger(arguments[0],0,NULL,NULL,0);

  /* format result */
  sendResult(clientInfo,id,TRUE,0,
             "%s",
             Errors_getText(error)
            );
}

LOCAL void serverCommand_deviceList(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  Errors       error;
  DeviceHandle deviceHandle;
  String       deviceName;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  /* open device */
  error = File_openDevices(&deviceHandle);
  if (error != ERROR_NONE)
  {
    sendResult(clientInfo,id,TRUE,error,"cannot open device list (error: %s)",Errors_getText(error));
    return;
  }

  /* read device entries */
  deviceName = String_new();
  while (!File_endOfDevices(&deviceHandle))
  {
    error = File_readDevice(&deviceHandle,deviceName);
    if (error != ERROR_NONE)
    {
      sendResult(clientInfo,id,TRUE,error,"cannot read device list (error: %s)",Errors_getText(error));
      File_closeDevices(&deviceHandle);
      String_delete(deviceName);
      return;
    }

    sendResult(clientInfo,id,FALSE,0,"%'S",deviceName);
  }
  String_delete(deviceName);

  /* close device */
  File_closeDevices(&deviceHandle);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_fileList(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  Errors                 error;
  StorageDirectoryHandle storageDirectoryHandle;
  String                 fileName;
  FileInfo               fileInfo;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get path name */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected path name");
    return;
  }

  /* open directory */
  error = Storage_openDirectory(&storageDirectoryHandle,
                                arguments[0],
                                &clientInfo->jobOptions
                               );
  if (error != ERROR_NONE)
  {
    sendResult(clientInfo,id,TRUE,error,"%s",Errors_getText(error));
    return;
  }

  /* read directory entries */
  fileName = String_new();
  while (!Storage_endOfDirectory(&storageDirectoryHandle))
  {
    error = Storage_readDirectory(&storageDirectoryHandle,fileName,&fileInfo);
    if (error == ERROR_NONE)
    {
      switch (fileInfo.type)
      {
        case FILE_TYPE_FILE:
          sendResult(clientInfo,id,FALSE,0,
                     "FILE %llu %llu %'S",
                     fileInfo.size,
                     fileInfo.timeModified,
                     fileName
                    );
          break;
        case FILE_TYPE_DIRECTORY:
          sendResult(clientInfo,id,FALSE,0,
                     "DIRECTORY %llu %'S",
                     fileInfo.timeModified,
                     fileName
                    );
          break;
        case FILE_TYPE_LINK:
          sendResult(clientInfo,id,FALSE,0,
                     "LINK %llu %'S",
                     fileInfo.timeModified,
                     fileName
                    );
          break;
        case FILE_TYPE_SPECIAL:
          sendResult(clientInfo,id,FALSE,0,
                     "SPECIAL %llu %'S",
                     fileInfo.timeModified,
                     fileName
                    );
          break;
        default:
          break;
      }
    }
    else
    {
      sendResult(clientInfo,id,FALSE,0,
                 "UNKNOWN %'S",
                 fileName
                );
    }
  }
  String_delete(fileName);

  /* close directory */
  Storage_closeDirectory(&storageDirectoryHandle);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_directoryInfo(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  long              timeout;
  DirectoryInfoNode *directoryInfoNode;
  uint64            fileCount;
  uint64            totalFileSize;
  bool              timeoutFlag;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get path name */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected file name");
    return;
  }
  timeout = (argumentCount >= 2)?String_toInteger(arguments[1],0,NULL,NULL,0):0L;

  /* find/create directory info */
  directoryInfoNode = findDirectoryInfo(&clientInfo->directoryInfoList,arguments[0]);
  if (directoryInfoNode == NULL)
  {
    directoryInfoNode = newDirectoryInfo(arguments[0]);
    List_append(&clientInfo->directoryInfoList,directoryInfoNode);
  }

  /* get total size of directoy/file */
  getDirectoryInfo(directoryInfoNode,
                   timeout,
                   &fileCount,
                   &totalFileSize,
                   &timeoutFlag
                  );

  sendResult(clientInfo,id,TRUE,0,"%llu %llu %d",fileCount,totalFileSize,timeoutFlag?1:0);
}

LOCAL void serverCommand_optionGet(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint              jobId;
  String            name;
  JobNode           *jobNode;
  uint              z;
  String            s;
  ConfigValueFormat configValueFormat;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, name, value */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected config value name");
    return;
  }
  name = arguments[1];

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* find config value */
  z = 0;
  while (   (z < SIZE_OF_ARRAY(CONFIG_VALUES))
         && !String_equalsCString(name,CONFIG_VALUES[z].name)
        )
  {
    z++;
  }
  if (z >= SIZE_OF_ARRAY(CONFIG_VALUES))
  {
    sendResult(clientInfo,id,TRUE,ERROR_UNKNOWN_VALUE,"unknown config value '%S'",name);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* send value */
  s = String_new();
  ConfigValue_formatInit(&configValueFormat,
                         &CONFIG_VALUES[z],
                         CONFIG_VALUE_FORMAT_MODE_VALUE,
                         jobNode
                        );
  ConfigValue_format(&configValueFormat,s);
  ConfigValue_formatDone(&configValueFormat);
  sendResult(clientInfo,id,TRUE,0,"%S",s);
  String_delete(s);

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_optionSet(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  String  name,value;
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, name, value */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected config name");
    return;
  }
  name = arguments[1];
  if (argumentCount < 3)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected config value for '%S'",arguments[1]);
    return;
  }
  value = arguments[2];

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* parse */
  if (ConfigValue_parse(String_cString(name),
                        String_cString(value),
                        CONFIG_VALUES,SIZE_OF_ARRAY(CONFIG_VALUES),
                        NULL,
                        NULL,
                        jobNode
                       )
     )
  {
    jobNode->modifiedFlag = TRUE;
    sendResult(clientInfo,id,TRUE,0,"");
  }
  else
  {
    sendResult(clientInfo,id,TRUE,ERROR_UNKNOWN_VALUE,"unknown config value '%S'",name);
  }

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_optionDelete(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  String  name;
  JobNode *jobNode;
  uint    z;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, name, value */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected config value name");
    return;
  }
  name = arguments[1];

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* find config value */
  z = 0;
  while (   (z < SIZE_OF_ARRAY(CONFIG_VALUES))
         && !String_equalsCString(name,CONFIG_VALUES[z].name)
        )
  {
    z++;
  }
  if (z >= SIZE_OF_ARRAY(CONFIG_VALUES))
  {
    sendResult(clientInfo,id,TRUE,ERROR_UNKNOWN_VALUE,"unknown config value '%S'",name);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* delete value */
//  ConfigValue_reset(&CONFIG_VALUES[z],jobNode);

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_jobList(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);
  jobNode = jobList.head;
  while (jobNode != NULL)
  {
    sendResult(clientInfo,id,FALSE,0,
               "%u %'S %'s %s %llu %'s %'s %'s %llu %lu",
               jobNode->id,
               jobNode->name,
               getJobStateText(jobNode->state),
               getArchiveTypeName(((jobNode->archiveType == ARCHIVE_TYPE_FULL) || (jobNode->archiveType == ARCHIVE_TYPE_INCREMENTAL))?jobNode->archiveType:jobNode->jobOptions.archiveType),
               jobNode->jobOptions.archivePartSize,
               Compress_getAlgorithmName(jobNode->jobOptions.compressAlgorithm),
               Crypt_getAlgorithmName(jobNode->jobOptions.cryptAlgorithm),
               Crypt_getTypeName(jobNode->jobOptions.cryptType),
               jobNode->lastExecutedDateTime,
               jobNode->runningInfo.estimatedRestTime
              );

    jobNode = jobNode->next;
  }
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_jobInfo(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);

  /* find job */
  jobNode = jobList.head;
  while ((jobNode != NULL) && (jobNode->id != jobId))
  {
    jobNode = jobNode->next;
  }

  /* format result */
  if (jobNode != NULL)
  {
    sendResult(clientInfo,id,TRUE,0,
               "%'s %'s %lu %llu %lu %llu %lu %llu %lu %llu %f %f %f %llu %f %'S %llu %llu %'S %llu %llu %d %f %d",
               getJobStateText(jobNode->state),
               (jobNode->runningInfo.error != ERROR_NONE)?Errors_getText(jobNode->runningInfo.error):"",
               jobNode->runningInfo.doneFiles,
               jobNode->runningInfo.doneBytes,
               jobNode->runningInfo.totalFiles,
               jobNode->runningInfo.totalBytes,
               jobNode->runningInfo.skippedFiles,
               jobNode->runningInfo.skippedBytes,
               jobNode->runningInfo.errorFiles,
               jobNode->runningInfo.errorBytes,
               Misc_performanceFilterGetValue(&jobNode->runningInfo.filesPerSecond,       10),
               Misc_performanceFilterGetValue(&jobNode->runningInfo.bytesPerSecond,       10),
               Misc_performanceFilterGetValue(&jobNode->runningInfo.storageBytesPerSecond,60),
               jobNode->runningInfo.archiveBytes,
               jobNode->runningInfo.compressionRatio,
               jobNode->runningInfo.fileName,
               jobNode->runningInfo.fileDoneBytes,
               jobNode->runningInfo.fileTotalBytes,
               jobNode->runningInfo.storageName,
               jobNode->runningInfo.storageDoneBytes,
               jobNode->runningInfo.storageTotalBytes,
               jobNode->runningInfo.volumeNumber,
               jobNode->runningInfo.volumeProgress,
               jobNode->requestedVolumeNumber
              );
  }
  else
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
  }

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_jobNew(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  String     fileName;
  JobNode    *jobNode;
  FileHandle fileHandle;
  Errors     error;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get filename */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected name");
    return;
  }
  fileName = File_appendFileName(File_setFileNameCString(File_newFileName(),serverJobsDirectory),arguments[0]);
  if (File_exists(fileName))
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB,"job '%s' already exists",String_cString(arguments[0]));
    return;
  }

  /* create job file */
  error = File_open(&fileHandle,fileName,FILE_OPENMODE_CREATE);
  if (error != ERROR_NONE)
  {
    File_deleteFileName(fileName);
    sendResult(clientInfo,id,TRUE,ERROR_JOB,"create job '%s' fail: %s",String_cString(arguments[0]),Errors_getText(error));
    return;
  }
  File_close(&fileHandle);

  /* create new job */
  jobNode = newJob(JOB_TYPE_BACKUP,fileName);
  if (jobNode == NULL)
  {
    File_delete(fileName,FALSE);
    File_deleteFileName(fileName);
    sendResult(clientInfo,id,TRUE,ERROR_INSUFFICIENT_MEMORY,"insufficient memory");
    return;
  }
  copyJobOptions(serverDefaultJobOptions,&jobNode->jobOptions);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* add new job to list */
  List_append(&jobList,jobNode);

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"%d",jobNode->id);

  /* free resources */
  File_deleteFileName(fileName);
}

LOCAL void serverCommand_jobRename(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  String  name;
  JobNode *jobNode;
  String  fileName;
  Errors  error;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected name");
    return;
  }
  name = arguments[1];
  if (File_exists(name))
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB,"job '%s' already exists",String_cString(arguments[1]));
    return;
  }

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* rename job */
  fileName = File_appendFileName(File_setFileNameCString(File_newFileName(),serverJobsDirectory),name);
  error = File_rename(jobNode->fileName,fileName);
  if (error != ERROR_NONE)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB,"error renaming job #%d: %s",jobId,Errors_getText(error));
    Semaphore_unlock(&jobList.lock);
    File_deleteFileName(fileName);
    return;
  }
  File_deleteFileName(fileName);

  /* store new file name */
  File_appendFileName(File_setFileNameCString(jobNode->fileName,serverJobsDirectory),name);
  String_set(jobNode->name,name);

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_jobDelete(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  JobNode *jobNode;
  Errors  error;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* remove job in list if not running or requested volume */
  if (CHECK_JOB_IS_RUNNING(jobNode))
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB,"job #%d running",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* delete job file */
  error = File_delete(jobNode->fileName,FALSE);
  if (error != ERROR_NONE)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB,"error deleting job #%d: %s",jobId,Errors_getText(error));
    Semaphore_unlock(&jobList.lock);
    return;
  }
  List_remove(&jobList,jobNode);
  deleteJob(jobNode);

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_jobStart(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint         jobId;
  ArchiveTypes archiveType;
  JobNode      *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* get archive type */
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected archive type");
    return;
  }
  if      (String_equalsCString(arguments[1],"normal"     )) archiveType = ARCHIVE_TYPE_NORMAL;
  else if (String_equalsCString(arguments[1],"full"       )) archiveType = ARCHIVE_TYPE_FULL;
  else if (String_equalsCString(arguments[1],"incremental")) archiveType = ARCHIVE_TYPE_INCREMENTAL;
  else
  {
    sendResult(clientInfo,id,TRUE,ERROR_UNKNOWN_VALUE,"unknown archive type '%S'",arguments[1]);
    return;
  }

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* run job */
  if  (!CHECK_JOB_IS_ACTIVE(jobNode))
  {
    /* set state */
    jobNode->state              = JOB_STATE_WAITING;
    jobNode->archiveType        = archiveType;
    jobNode->requestedAbortFlag = FALSE;
    resetJobRunningInfo(jobNode);
  }

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_jobAbort(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* abort job */
  if      (CHECK_JOB_IS_RUNNING(jobNode))
  {
    jobNode->requestedAbortFlag = TRUE;
    while (CHECK_JOB_IS_RUNNING(jobNode))
    {
      Semaphore_waitModified(&jobList.lock);
    }
  }
  else if (CHECK_JOB_IS_ACTIVE(jobNode))
  {
    jobNode->state = JOB_STATE_NONE;
  }

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_jobFlush(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* update all job files */
  updateAllJobFiles();

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_includePatternsList(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint        jobId;
  JobNode     *jobNode;
  PatternNode *patternNode;
  const char  *type;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* send include list */
  patternNode = jobNode->includePatternList.head;
  while (patternNode != NULL)
  {
    type = NULL;
    switch (patternNode->pattern.type)
    {
      case PATTERN_TYPE_GLOB          : type = "GLOB";           break;
      case PATTERN_TYPE_REGEX         : type = "REGEX";          break;
      case PATTERN_TYPE_EXTENDED_REGEX: type = "EXTENDED_REGEX"; break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break;
      #endif /* NDEBUG */
    }
    sendResult(clientInfo,id,FALSE,0,
               "%s %'S",
               type,
               patternNode->string
              );
    patternNode = patternNode->next;
  }
  sendResult(clientInfo,id,TRUE,0,"");

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_includePatternsClear(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* clear include list */
  PatternList_clear(&jobNode->includePatternList);
  jobNode->modifiedFlag = TRUE;
  sendResult(clientInfo,id,TRUE,0,"");

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_includePatternsAdd(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint         jobId;
  PatternTypes patternType;
  String       pattern;
  JobNode      *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected pattern type");
    return;
  }
  if      (String_equalsCString(arguments[1],"GLOB"))
  {
    patternType = PATTERN_TYPE_GLOB;
  }
  else if (String_equalsCString(arguments[1],"REGEX"))
  {
    patternType = PATTERN_TYPE_REGEX;
  }
  else if (String_equalsCString(arguments[1],"EXTENDED_REGEX"))
  {
    patternType = PATTERN_TYPE_EXTENDED_REGEX;
  }
  else
  {
    sendResult(clientInfo,id,TRUE,ERROR_UNKNOWN_VALUE,"unknown pattern type '%S'",arguments[1]);
    return;
  }
  if (argumentCount < 3)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected pattern");
    return;
  }
  pattern = arguments[2];

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* add to include list */
  PatternList_append(&jobNode->includePatternList,pattern,patternType);
  jobNode->modifiedFlag = TRUE;

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_excludePatternsList(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint        jobId;
  JobNode     *jobNode;
  PatternNode *patternNode;
  const char  *type;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* send exclude list */
  patternNode = jobNode->excludePatternList.head;
  while (patternNode != NULL)
  {
    type = NULL;
    switch (patternNode->pattern.type)
    {
      case PATTERN_TYPE_GLOB          : type = "GLOB";           break;
      case PATTERN_TYPE_REGEX         : type = "REGEX";          break;
      case PATTERN_TYPE_EXTENDED_REGEX: type = "EXTENDED_REGEX"; break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break;
      #endif /* NDEBUG */
    }
    sendResult(clientInfo,id,FALSE,0,
               "%s %'S",
               type,
               patternNode->string
              );
    patternNode = patternNode->next;
  }
  sendResult(clientInfo,id,TRUE,0,"");

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_excludePatternsClear(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* clear exclude list */
  PatternList_clear(&jobNode->excludePatternList);
  jobNode->modifiedFlag = TRUE;
  sendResult(clientInfo,id,TRUE,0,"");

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_excludePatternsAdd(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint         jobId;
  PatternTypes patternType;
  String       pattern;
  JobNode      *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected pattern type");
    return;
  }
  if      (String_equalsCString(arguments[1],"GLOB"))
  {
    patternType = PATTERN_TYPE_GLOB;
  }
  else if (String_equalsCString(arguments[1],"REGEX"))
  {
    patternType = PATTERN_TYPE_REGEX;
  }
  else if (String_equalsCString(arguments[1],"EXTENDED_REGEX"))
  {
    patternType = PATTERN_TYPE_EXTENDED_REGEX;
  }
  else
  {
    sendResult(clientInfo,id,TRUE,ERROR_UNKNOWN_VALUE,"unknown pattern type '%S'",arguments[1]);
    return;
  }
  if (argumentCount < 3)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected pattern");
    return;
  }
  pattern = arguments[2];

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* add to exclude list */
  PatternList_append(&jobNode->excludePatternList,pattern,patternType);
  jobNode->modifiedFlag = TRUE;

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_scheduleList(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint         jobId;
  JobNode      *jobNode;
  String       line;
  ScheduleNode *scheduleNode;
  String       names;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* send schedule list */
  line = String_new();
  scheduleNode = jobNode->scheduleList.head;
  while (scheduleNode != NULL)
  {
    String_clear(line);

    if (scheduleNode->year != SCHEDULE_ANY)
    {
      String_format(line,"%d",scheduleNode->year);
    }
    else
    {
      String_appendCString(line,"*");
    }
    String_appendChar(line,'-');
    if (scheduleNode->month != SCHEDULE_ANY)
    {
      String_format(line,"%02d",scheduleNode->month);
    }
    else
    {
      String_appendCString(line,"*");
    }
    String_appendChar(line,'-');
    if (scheduleNode->day != SCHEDULE_ANY)
    {
      String_format(line,"%02d",scheduleNode->day);
    }
    else
    {
      String_appendCString(line,"*");
    }
    String_appendChar(line,' ');
    if (scheduleNode->weekDays != SCHEDULE_ANY_DAY)
    {
      names = String_new();

      if (IN_SET(scheduleNode->weekDays,WEEKDAY_MON)) { String_joinCString(names,"Mon",','); }
      if (IN_SET(scheduleNode->weekDays,WEEKDAY_TUE)) { String_joinCString(names,"Tue",','); }
      if (IN_SET(scheduleNode->weekDays,WEEKDAY_WED)) { String_joinCString(names,"Wed",','); }
      if (IN_SET(scheduleNode->weekDays,WEEKDAY_THU)) { String_joinCString(names,"Thu",','); }
      if (IN_SET(scheduleNode->weekDays,WEEKDAY_FRI)) { String_joinCString(names,"Fri",','); }
      if (IN_SET(scheduleNode->weekDays,WEEKDAY_SAT)) { String_joinCString(names,"Sat",','); }
      if (IN_SET(scheduleNode->weekDays,WEEKDAY_SUN)) { String_joinCString(names,"Sun",','); }

      String_append(line,names);
      String_appendChar(line,' ');

      String_delete(names);
    }
    else
    {
      String_appendCString(line,"*");
    }
    String_appendChar(line,' ');
    if (scheduleNode->hour != SCHEDULE_ANY)
    {
      String_format(line,"%02d",scheduleNode->hour);
    }
    else
    {
      String_appendCString(line,"*");
    }
    String_appendChar(line,':');
    if (scheduleNode->minute != SCHEDULE_ANY)
    {
      String_format(line,"%02d",scheduleNode->minute);
    }
    else
    {
      String_appendCString(line,"*");
    }
    String_appendChar(line,' ');
    String_format(line,"%y",scheduleNode->enabled);
    String_appendChar(line,' ');
    switch (scheduleNode->archiveType)
    {
      case ARCHIVE_TYPE_NORMAL     : String_appendCString(line,"normal"     ); break;
      case ARCHIVE_TYPE_FULL       : String_appendCString(line,"full"       ); break;
      case ARCHIVE_TYPE_INCREMENTAL: String_appendCString(line,"incremental"); break;
      default                      : String_appendCString(line,"*"          ); break;
    }

    sendResult(clientInfo,id,FALSE,0,
               "%S",
               line
              );
    scheduleNode = scheduleNode->next;
  }
  sendResult(clientInfo,id,TRUE,0,"");
  String_delete(line);

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_scheduleClear(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get job id, type, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    return;
  }

  /* clear schedule list */
  List_clear(&jobNode->scheduleList,NULL,NULL);
  jobNode->modifiedFlag = TRUE;
  sendResult(clientInfo,id,TRUE,0,"");

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_scheduleAdd(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  String       s;
  uint         jobId;
  JobNode      *jobNode;
  ScheduleNode *scheduleNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* initialise variables */
  s = String_new();

  /* get job id, date, weekday, time */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    String_delete(s);
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected date");
    String_delete(s);
    return;
  }
  if (argumentCount < 3)
  {
    String_delete(s);
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected week day");
    return;
  }
  if (argumentCount < 4)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected time");
    String_delete(s);
    return;
  }
  if (argumentCount < 5)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected type");
    String_delete(s);
    return;
  }
  String_format(s,"%S %S %S %S",arguments[1],arguments[2],arguments[3],arguments[4]);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = findJobById(jobId);
  if (jobNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
    Semaphore_unlock(&jobList.lock);
    String_delete(s);
    return;
  }

  /* parse schedule */
  scheduleNode = parseSchedule(s);
  if (scheduleNode == NULL)
  {
    sendResult(clientInfo,id,TRUE,ERROR_PARSING,"cannot parse schedule '%S'",s);
    Semaphore_unlock(&jobList.lock);
    String_delete(s);
    return;
  }
  /* add to schedule list */
  List_append(&jobNode->scheduleList,scheduleNode);
  jobNode->modifiedFlag = TRUE;
  sendResult(clientInfo,id,TRUE,0,"");

  /* unlock */
  Semaphore_unlock(&jobList.lock);

  /* free resources */
  String_delete(s);
}

LOCAL void serverCommand_passwordClear(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  /* clear schedule list */
  Archive_clearCryptPasswords();
  sendResult(clientInfo,id,TRUE,0,"");

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_passwordAdd(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  Password password;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  /* initialise variables */
  Password_init(&password);

  /* get password */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected password");
    Password_done(&password);
    return;
  }

  /* add to password list */
  Password_setString(&password,arguments[0]);
  Archive_appendCryptPassword(&password);
  sendResult(clientInfo,id,TRUE,0,"");

  /* free resources */
  Password_done(&password);
}

LOCAL void serverCommand_volume(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  uint    jobId;
  uint    volumeNumber;
  JobNode *jobNode;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get id */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected job id");
    return;
  }
  jobId = String_toInteger(arguments[0],0,NULL,NULL,0);

  /* get volume number */
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected volume number");
    return;
  }
  volumeNumber = String_toInteger(arguments[1],0,NULL,NULL,0);

  /* lock */
  Semaphore_lock(&jobList.lock,SEMAPHORE_LOCK_TYPE_READ_WRITE);

  /* find job */
  jobNode = jobList.head;
  while ((jobNode != NULL) && (jobNode->id != jobId))
  {
    jobNode = jobNode->next;
  }

  if (jobNode != NULL)
  {
    jobNode->volumeNumber = volumeNumber;
    sendResult(clientInfo,id,TRUE,0,"");
  }
  else
  {
    sendResult(clientInfo,id,TRUE,ERROR_JOB_NOT_FOUND,"job #%d not found",jobId);
  }

  /* unlock */
  Semaphore_unlock(&jobList.lock);
}

LOCAL void serverCommand_archiveList(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  String          archiveName;
  Errors          error;
  ArchiveInfo     archiveInfo;
  ArchiveFileInfo archiveFileInfo;
  FileTypes       fileType;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get archive name, pattern */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected archive name");
    return;
  }
  archiveName = arguments[0];

  /* open archive */
  error = Archive_open(&archiveInfo,
                       archiveName,
                       &clientInfo->jobOptions,
                       PASSWORD_MODE_CONFIG
                      );
  if (error != ERROR_NONE)
  {
    sendResult(clientInfo,id,TRUE,error,"%s",Errors_getText(error));
    return;
  }

  /* list contents */
  error = ERROR_NONE;
  while (!Archive_eof(&archiveInfo) && (error == ERROR_NONE) && !commandAborted(clientInfo,id))
  {
//Misc_udelay(1000*100);

    /* get next file type */
    error = Archive_getNextFileType(&archiveInfo,
                                    &archiveFileInfo,
                                    &fileType
                                   );
    if (error == ERROR_NONE)
    {
      /* read entry */
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
            if (error != ERROR_NONE)
            {
              sendResult(clientInfo,id,TRUE,error,"Cannot not read content of archive '%S' (error: %s)",archiveName,Errors_getText(error));
              String_delete(fileName);
              break;
            }

            /* match pattern */
            if (   (List_empty(&clientInfo->includePatternList) || PatternList_match(&clientInfo->includePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(&clientInfo->excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              sendResult(clientInfo,id,FALSE,0,
                         "FILE %llu %llu %llu %llu %llu %'S",
                         fileInfo.size,
                         fileInfo.timeModified,
                         archiveFileInfo.file.chunkInfoFileData.size,
                         fragmentOffset,
                         fragmentSize,
                         fileName
                        );
            }

            /* close archive file, free resources */
            Archive_closeEntry(&archiveFileInfo);
            String_delete(fileName);
          }
          break;
        case FILE_TYPE_DIRECTORY:
          {
            CryptAlgorithms cryptAlgorithm;
            CryptTypes      cryptType;
            String          directoryName;
            FileInfo        fileInfo;

            /* open archive directory */
            directoryName = String_new();
            error = Archive_readDirectoryEntry(&archiveInfo,
                                               &archiveFileInfo,
                                               &cryptAlgorithm,
                                               &cryptType,
                                               directoryName,
                                               &fileInfo
                                              );
            if (error != ERROR_NONE)
            {
              sendResult(clientInfo,id,TRUE,error,"Cannot not read content of archive '%S' (error: %s)",archiveName,Errors_getText(error));
              String_delete(directoryName);
              break;
            }

            /* match pattern */
            if (   (List_empty(&clientInfo->includePatternList) || PatternList_match(&clientInfo->includePatternList,directoryName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(&clientInfo->excludePatternList,directoryName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              sendResult(clientInfo,id,FALSE,0,
                         "DIRECTORY %llu %'S",
                         fileInfo.timeModified,
                         directoryName
                        );
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

            /* open archive link */
            linkName = String_new();
            fileName = String_new();
            error = Archive_readLinkEntry(&archiveInfo,
                                          &archiveFileInfo,
                                          &cryptAlgorithm,
                                          &cryptType,
                                          linkName,
                                          fileName,
                                          &fileInfo
                                         );
            if (error != ERROR_NONE)
            {
              sendResult(clientInfo,id,TRUE,error,"Cannot not read content of archive '%S' (error: %s)",archiveName,Errors_getText(error));
              String_delete(fileName);
              String_delete(linkName);
              break;
            }

            /* match pattern */
            if (   (List_empty(&clientInfo->includePatternList) || PatternList_match(&clientInfo->includePatternList,linkName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(&clientInfo->excludePatternList,linkName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              sendResult(clientInfo,id,FALSE,0,
                         "LINK %llu %'S %'S",
                         fileInfo.timeModified,
                         linkName,
                         fileName
                        );
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

            /* open archive link */
            fileName = String_new();
            error = Archive_readSpecialEntry(&archiveInfo,
                                             &archiveFileInfo,
                                             &cryptAlgorithm,
                                             &cryptType,
                                             fileName,
                                             &fileInfo
                                            );
            if (error != ERROR_NONE)
            {
              sendResult(clientInfo,id,TRUE,error,"Cannot not read content of archive '%S' (error: %s)",archiveName,Errors_getText(error));
              String_delete(fileName);
              break;
            }

            /* match pattern */
            if (   (List_empty(&clientInfo->includePatternList) || PatternList_match(&clientInfo->includePatternList,fileName,PATTERN_MATCH_MODE_EXACT))
                && !PatternList_match(&clientInfo->excludePatternList,fileName,PATTERN_MATCH_MODE_EXACT)
               )
            {
              sendResult(clientInfo,id,FALSE,0,
                         "SPECIAL %'S",
                         fileName
                        );
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
    else
    {
      sendResult(clientInfo,id,TRUE,error,"Cannot not read next entry of archive '%S' (error: %s)",archiveName,Errors_getText(error));
      break;
    }
  }

  /* close archive */
  Archive_close(&archiveInfo);

  sendResult(clientInfo,id,TRUE,0,"");
}

LOCAL void serverCommand_restore(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  StringList  archiveNameList;
  PatternList includePatternList;
  PatternList excludePatternList;
  JobOptions  jobOptions;
  uint        z;
  Errors      error;

  assert(clientInfo != NULL);
  assert(arguments != NULL);

  /* get archive name, files */
  if (argumentCount < 1)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected archive name");
    return;
  }
  StringList_init(&archiveNameList);
  StringList_append(&archiveNameList,arguments[0]);
  if (argumentCount < 2)
  {
    sendResult(clientInfo,id,TRUE,ERROR_EXPECTED_PARAMETER,"expected destination directory");
    return;
  }
  initJobOptions(&jobOptions);
  jobOptions.directory = String_duplicate(arguments[1]);
  PatternList_init(&includePatternList);
  PatternList_init(&excludePatternList);
  for (z = 2; z < argumentCount; z++)
  {
    PatternList_append(&includePatternList,
                       arguments[z],
                       PATTERN_TYPE_GLOB
                      );
  }

  /* restore */
  error = Command_restore(&archiveNameList,
                          &includePatternList,
                          &excludePatternList,
                          &jobOptions,
                          NULL,
                          NULL,
                          NULL,
                          NULL
                         );
  sendResult(clientInfo,id,TRUE,error,Errors_getText(error));

  /* free resources */
  freeJobOptions(&jobOptions);
  PatternList_done(&excludePatternList);
  PatternList_done(&includePatternList);
  StringList_done(&archiveNameList);
}

#ifndef NDEBUG
LOCAL void serverCommand_debugMemoryInfo(ClientInfo *clientInfo, uint id, const String arguments[], uint argumentCount)
{
  assert(clientInfo != NULL);

  UNUSED_VARIABLE(arguments);
  UNUSED_VARIABLE(argumentCount);

  Array_debug();
  String_debug();

  sendResult(clientInfo,id,TRUE,0,"");
}
#endif /* NDEBUG */

// server commands
const struct
{
  const char            *name;
  const char            *parameters;
  ServerCommandFunction serverCommandFunction;
  AuthorizationStates   authorizationState;
}
SERVER_COMMANDS[] =
{
  { "AUTHORIZE",             "S",   serverCommand_authorize,           AUTHORIZATION_STATE_WAITING },
  { "ABORT",                 "i",   serverCommand_abort,               AUTHORIZATION_STATE_OK      },
  { "STATUS",                "",    serverCommand_status,              AUTHORIZATION_STATE_OK      },
  { "PAUSE",                 "",    serverCommand_pause ,              AUTHORIZATION_STATE_OK      },
  { "CONTINUE",              "",    serverCommand_continue,            AUTHORIZATION_STATE_OK      },
  { "ERROR_INFO",            "i",   serverCommand_errorInfo,           AUTHORIZATION_STATE_OK      },
  { "DEVICE_LIST",           "",    serverCommand_deviceList,          AUTHORIZATION_STATE_OK      },
  { "FILE_LIST",             "S",   serverCommand_fileList,            AUTHORIZATION_STATE_OK      },
  { "DIRECTORY_INFO",        "S",   serverCommand_directoryInfo,       AUTHORIZATION_STATE_OK      },
  { "JOB_LIST",              "",    serverCommand_jobList,             AUTHORIZATION_STATE_OK      },
  { "JOB_INFO",              "i",   serverCommand_jobInfo,             AUTHORIZATION_STATE_OK      },
  { "JOB_NEW",               "S",   serverCommand_jobNew,              AUTHORIZATION_STATE_OK      },
  { "JOB_RENAME",            "i S", serverCommand_jobRename,           AUTHORIZATION_STATE_OK      },
  { "JOB_DELETE",            "i",   serverCommand_jobDelete,           AUTHORIZATION_STATE_OK      },
  { "JOB_START",             "i",   serverCommand_jobStart,            AUTHORIZATION_STATE_OK      },
  { "JOB_ABORT",             "i",   serverCommand_jobAbort,            AUTHORIZATION_STATE_OK      },
  { "JOB_FLUSH",             "",    serverCommand_jobFlush,            AUTHORIZATION_STATE_OK      },
  { "INCLUDE_PATTERNS_LIST", "i",   serverCommand_includePatternsList, AUTHORIZATION_STATE_OK      },
  { "INCLUDE_PATTERNS_CLEAR","i",   serverCommand_includePatternsClear,AUTHORIZATION_STATE_OK      },
  { "INCLUDE_PATTERNS_ADD",  "i S", serverCommand_includePatternsAdd,  AUTHORIZATION_STATE_OK      },
  { "EXCLUDE_PATTERNS_LIST", "i",   serverCommand_excludePatternsList, AUTHORIZATION_STATE_OK      },
  { "EXCLUDE_PATTERNS_CLEAR","i",   serverCommand_excludePatternsClear,AUTHORIZATION_STATE_OK      },
  { "EXCLUDE_PATTERNS_ADD",  "i S", serverCommand_excludePatternsAdd,  AUTHORIZATION_STATE_OK      },
  { "SCHEDULE_LIST",         "i",   serverCommand_scheduleList,        AUTHORIZATION_STATE_OK      },
  { "SCHEDULE_CLEAR",        "i",   serverCommand_scheduleClear,       AUTHORIZATION_STATE_OK      },
  { "SCHEDULE_ADD",          "i S", serverCommand_scheduleAdd,         AUTHORIZATION_STATE_OK      },
  { "PASSWORD_CLEAR",        "",    serverCommand_passwordClear,       AUTHORIZATION_STATE_OK      },
  { "PASSWORD_ADD",          "S",   serverCommand_passwordAdd,         AUTHORIZATION_STATE_OK      },
  { "OPTION_GET",            "s",   serverCommand_optionGet,           AUTHORIZATION_STATE_OK      },
  { "OPTION_SET",            "s S", serverCommand_optionSet,           AUTHORIZATION_STATE_OK      },
  { "OPTION_DELETE",         "s S", serverCommand_optionDelete,        AUTHORIZATION_STATE_OK      },
  { "VOLUME",                "S i", serverCommand_volume,              AUTHORIZATION_STATE_OK      },
  { "ARCHIVE_LIST",          "S S", serverCommand_archiveList,         AUTHORIZATION_STATE_OK      },
  { "RESTORE",               "S S", serverCommand_restore,             AUTHORIZATION_STATE_OK      },
  #ifndef NDEBUG
  { "DEBUG_MEMORY_INFO",     "",    serverCommand_debugMemoryInfo,     AUTHORIZATION_STATE_OK      },
  #endif /* NDEBUG */
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

  String_erase(*string);
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

  /* initialize variables */
  commandMsg->serverCommandFunction = 0;
  commandMsg->authorizationState    = 0;
  commandMsg->id                    = 0;
  commandMsg->arguments             = NULL;

  /* initialize tokenizer */
  String_initTokenizer(&stringTokenizer,string,STRING_BEGIN,STRING_WHITE_SPACES,STRING_QUOTES,TRUE);

  /* get id */
  if (!String_getNextToken(&stringTokenizer,&token,NULL))
  {
    String_doneTokenizer(&stringTokenizer);
    return FALSE;
  }
  commandMsg->id = String_toInteger(token,0,&index,NULL,0);
  if (index != STRING_END)
  {
    String_doneTokenizer(&stringTokenizer);
    return FALSE;
  }

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
  commandMsg->authorizationState    = SERVER_COMMANDS[z].authorizationState;

  /* get arguments */
  commandMsg->arguments = Array_new(sizeof(String),0);
  while (String_getNextToken(&stringTokenizer,&token,NULL))
  {
    argument = String_duplicate(token);
    Array_append(commandMsg->arguments,&argument);
  }

  /* free resources */
  String_doneTokenizer(&stringTokenizer);

  return TRUE;
}

/***********************************************************************\
* Name   : networkClientThread
* Purpose: processing thread for network clients
* Input  : clientInfo - client info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void networkClientThread(ClientInfo *clientInfo)
{
  CommandMsg commandMsg;
  String     result;

  assert(clientInfo != NULL);

  result = String_new();
  while (   !clientInfo->network.exitFlag
         && MsgQueue_get(&clientInfo->network.commandMsgQueue,&commandMsg,NULL,sizeof(commandMsg))
        )
  {
    /* check authorization */
    if (clientInfo->authorizationState == commandMsg.authorizationState)
    {
      /* execute command */
      commandMsg.serverCommandFunction(clientInfo,
                                       commandMsg.id,
                                       Array_cArray(commandMsg.arguments),
                                       Array_length(commandMsg.arguments)
                                      );
    }
    else
    {
      /* authorization failure -> mark for disconnect */
      sendResult(clientInfo,commandMsg.id,TRUE,ERROR_AUTHORIZATION,"authorization failure");
      clientInfo->authorizationState = AUTHORIZATION_STATE_FAIL;
    }

    /* free resources */
    freeCommand(&commandMsg);
  }
  String_delete(result);

  clientInfo->network.exitFlag = TRUE;
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
* Name   : getNewSessionId
* Purpose: get new session id
* Input  : -
* Output : sessionId - new session id
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void getNewSessionId(SessionId sessionId)
{
  #ifndef NO_SESSION_ID
    Crypt_randomize(sessionId,sizeof(SessionId));
  #else /* not NO_SESSION_ID */
    memset(sessionId,0,sizeof(SessionId));
  #endif /* NO_SESSION_ID */
}

/***********************************************************************\
* Name   : sendSessionId
* Purpose: send session id to client
* Input  : clientInfo - client info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void sendSessionId(ClientInfo *clientInfo)
{
  String s;
  uint   z;

  s = String_new();
  String_appendCString(s,"SESSION ");
  for (z = 0; z < sizeof(SessionId); z++)
  {
    String_format(s,"%02x",clientInfo->sessionId[z]);
  }
  String_appendChar(s,'\n');
  sendClient(clientInfo,s);
  String_delete(s);
}

/***********************************************************************\
* Name   : initBatchClient
* Purpose: create batch client with file i/o
* Input  : clientInfo - client info to initialize
*          fileHandle - client file handle
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initBatchClient(ClientInfo *clientInfo,
                           FileHandle fileHandle
                          )
{
  assert(clientInfo != NULL);

  /* initialize */
  clientInfo->type               = CLIENT_TYPE_BATCH;
//  clientInfo->authorizationState   = AUTHORIZATION_STATE_WAITING;
clientInfo->authorizationState   = AUTHORIZATION_STATE_OK;
  clientInfo->abortId            = 0;
  clientInfo->file.fileHandle    = fileHandle;

  PatternList_init(&clientInfo->includePatternList);
  PatternList_init(&clientInfo->excludePatternList);
  initJobOptions(&clientInfo->jobOptions);

  /* create and send session id */
// authorization?
//  getNewSessionId(clientInfo->sessionId);
//  sendSessionId(clientInfo);
}

/***********************************************************************\
* Name   : newNetworkClient
* Purpose: create new client with network i/o
* Input  : clientInfo   - client info to initialize
*          name         - client name
*          port         - client port
*          socketHandle - client socket handle
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void initNetworkClient(ClientInfo   *clientInfo,
                             const String name,
                             uint         port,
                             SocketHandle socketHandle
                            )
{
  uint z;

  assert(clientInfo != NULL);

  /* initialize */
  clientInfo->type                 = CLIENT_TYPE_NETWORK;
  clientInfo->authorizationState   = AUTHORIZATION_STATE_WAITING;
  clientInfo->abortId              = 0;
  clientInfo->network.name         = String_duplicate(name);
  clientInfo->network.port         = port;
  clientInfo->network.socketHandle = socketHandle;
  clientInfo->network.exitFlag     = FALSE;

  PatternList_init(&clientInfo->includePatternList);
  PatternList_init(&clientInfo->excludePatternList);
  initJobOptions(&clientInfo->jobOptions);
  List_init(&clientInfo->directoryInfoList);

  if (!MsgQueue_init(&clientInfo->network.commandMsgQueue,0))
  {
    HALT_FATAL_ERROR("Cannot initialise client command message queue!");
  }
  for (z = 0; z < MAX_NETWORK_CLIENT_THREADS; z++)
  {
    if (!Thread_init(&clientInfo->network.threads[z],"Client",0,networkClientThread,clientInfo))
    {
      HALT_FATAL_ERROR("Cannot initialise client thread!");
    }
  }

  /* create and send session id */
  getNewSessionId(clientInfo->sessionId);
  sendSessionId(clientInfo);
}

/***********************************************************************\
* Name   : doneClient
* Purpose: deinitialize client
* Input  : clientInfo - client info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void doneClient(ClientInfo *clientInfo)
{
  int z;

  assert(clientInfo != NULL);

  switch (clientInfo->type)
  {
    case CLIENT_TYPE_BATCH:
      break;
    case CLIENT_TYPE_NETWORK:
      /* stop client thread */
      clientInfo->network.exitFlag = TRUE;
      MsgQueue_setEndOfMsg(&clientInfo->network.commandMsgQueue);
      for (z = MAX_NETWORK_CLIENT_THREADS-1; z >= 0; z--)
      {
        Thread_join(&clientInfo->network.threads[z]);
      }

      /* free resources */
      MsgQueue_done(&clientInfo->network.commandMsgQueue,(MsgQueueMsgFreeFunction)freeCommandMsg,NULL);
      String_delete(clientInfo->network.name);
      for (z = MAX_NETWORK_CLIENT_THREADS-1; z >= 0; z--)
      {
        Thread_done(&clientInfo->network.threads[z]);
      }
      List_done(&clientInfo->directoryInfoList,(ListNodeFreeFunction)freeDirectoryInfoNode,NULL);
      break;
    default:
      #ifndef NDEBUG
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
      #endif /* NDEBUG */
      break;
  }

  freeJobOptions(&clientInfo->jobOptions);
  PatternList_done(&clientInfo->excludePatternList);
  PatternList_done(&clientInfo->includePatternList);
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

  doneClient(&clientNode->clientInfo);
  String_delete(clientNode->commandString);
}

/***********************************************************************\
* Name   : newClient
* Purpose: create new client
* Input  : type - client type
* Output : -
* Return : client node
* Notes  : -
\***********************************************************************/

LOCAL ClientNode *newClient(void)
{
  ClientNode *clientNode;

  /* create new client node */
  clientNode = LIST_NEW_NODE(ClientNode);
  if (clientNode == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  /* initialize node */
  clientNode->clientInfo.type               = CLIENT_TYPE_NONE;
  clientNode->clientInfo.authorizationState = AUTHORIZATION_STATE_WAITING;
  clientNode->commandString                 = String_new();

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
  assert(clientNode != NULL);

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

LOCAL void processCommand(ClientInfo *clientInfo, const String command)
{
  CommandMsg commandMsg;

  assert(clientInfo != NULL);

  #ifdef SERVER_DEBUG
    fprintf(stderr,"%s,%d: command=%s\n",__FILE__,__LINE__,String_cString(command));
  #endif /* SERVER_DEBUG */
  if (String_equalsCString(command,"VERSION"))
  {
    /* check authorization */
    if (clientInfo->authorizationState == AUTHORIZATION_STATE_OK)
    {
      /* version info */
      sendResult(clientInfo,0,TRUE,0,"%d %d",PROTOCOL_VERSION_MAJOR,PROTOCOL_VERSION_MINOR);
    }
    else
    {
      /* authorization failure -> mark for disconnect */
      sendResult(clientInfo,0,TRUE,ERROR_AUTHORIZATION,"authorization failure");
      clientInfo->authorizationState = AUTHORIZATION_STATE_FAIL;
    }
  }
  #ifndef NDEBUG
    else if (String_equalsCString(command,"QUIT"))
    {
      quitFlag = TRUE;
fprintf(stderr,"%s,%d: QQQQQQQQQQQQit\n",__FILE__,__LINE__);
      sendResult(clientInfo,0,TRUE,0,"ok");
    }
  #endif /* not NDEBUG */
  else
  {
    /* parse command */
    if (!parseCommand(&commandMsg,command))
    {
      sendResult(clientInfo,commandMsg.id,TRUE,ERROR_PARSING,"parse error");
      return;
    }

    switch (clientInfo->type)
    {
      case CLIENT_TYPE_BATCH:
        /* check authorization */
        if (clientInfo->authorizationState == commandMsg.authorizationState)
        {
          /* execute */
          commandMsg.serverCommandFunction(clientInfo,
                                           commandMsg.id,
                                           Array_cArray(commandMsg.arguments),
                                           Array_length(commandMsg.arguments)
                                          );
        }
        else
        {
          /* authorization failure -> mark for disconnect */
          sendResult(clientInfo,commandMsg.id,TRUE,ERROR_AUTHORIZATION,"authorization failure");
          clientInfo->authorizationState = AUTHORIZATION_STATE_FAIL;
        }

        /* free resources */
        freeCommand(&commandMsg);
        break;
      case CLIENT_TYPE_NETWORK:
        /* send command to client thread */
        MsgQueue_put(&clientInfo->network.commandMsgQueue,&commandMsg,sizeof(commandMsg));
        break;
      default:
        #ifndef NDEBUG
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        #endif /* NDEBUG */
        break;
    }
  }
}

/*---------------------------------------------------------------------*/

Errors Server_initAll(void)
{
  return ERROR_NONE;
}

void Server_doneAll(void)
{
}

Errors Server_run(uint             port,
                  uint             tlsPort,
                  const char       *caFileName,
                  const char       *certFileName,
                  const char       *keyFileName,
                  const Password   *password,
                  const char       *jobsDirectory,
                  const JobOptions *defaultJobOptions
                 )
{
  Errors             error;
  bool               serverFlag,serverTLSFlag;
  ServerSocketHandle serverSocketHandle,serverTLSSocketHandle;
  fd_set             selectSet;
  ClientNode         *clientNode;
  SocketHandle       socketHandle;
  String             clientName;
  uint               clientPort;
  char               buffer[256];
  ulong              receivedBytes;
  ulong              z;
  ClientNode         *deleteClientNode;

  if (Password_empty(password))
  {
    printWarning("No server password set - start without any password!\n",caFileName);
  }

  /* initialise variables */
  serverPassword          = password;
  serverJobsDirectory     = jobsDirectory;
  serverDefaultJobOptions = defaultJobOptions;
  List_init(&jobList);
  List_init(&clientList);
  pauseFlag               = FALSE;
  quitFlag                = FALSE;

  /* init server sockets */
  serverFlag    = FALSE;
  serverTLSFlag = FALSE;
  if (port != 0)
  {
    error = Network_initServer(&serverSocketHandle,
                               port,
                               SERVER_TYPE_PLAIN,
                               NULL,
                               NULL,
                               NULL
                              );
    if (error != ERROR_NONE)
    {
      printError("Cannot initialize server at port %u (error: %s)!\n",
                 port,
                 Errors_getText(error)
                );
      return error;
    }
    printInfo(1,"Started server on port %d\n",port);
    serverFlag = TRUE;
  }
  if (tlsPort != 0)
  {
    if (   File_existsCString(caFileName)
        && File_existsCString(certFileName)
        && File_existsCString(keyFileName)
       )
    {
      #ifdef HAVE_GNU_TLS
        error = Network_initServer(&serverTLSSocketHandle,
                                   tlsPort,
                                   SERVER_TYPE_TLS,
                                   caFileName,
                                   certFileName,
                                   keyFileName
                                  );
        if (error != ERROR_NONE)
        {
          printError("Cannot initialize TLS/SSL server at port %u (error: %s)!\n",
                     tlsPort,
                     Errors_getText(error)
                    );
          if (port != 0) Network_doneServer(&serverSocketHandle);
          return FALSE;
        }
        printInfo(1,"Started TLS/SSL server on port %u\n",tlsPort);
        serverTLSFlag = TRUE;
      #else /* not HAVE_GNU_TLS */
        UNUSED_VARIABLE(caFileName);
        UNUSED_VARIABLE(certFileName);
        UNUSED_VARIABLE(keyFileName);

        printError("TLS/SSL server is not supported!\n");
        Network_doneServer(&serverSocketHandle);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_GNU_TLS */
    }
    else
    {
      if (!File_existsCString(caFileName)) printWarning("No certificate authority file '%s' (bar-ca.pem file) - TLS server not started.\n",caFileName);
      if (!File_existsCString(certFileName)) printWarning("No certificate file '%s' (bar-server-cert.pem file) - TLS server not started.\n",certFileName);
      if (!File_existsCString(keyFileName)) printWarning("No key file '%s' (bar-server-key.pem file) - TLS server not started.\n",keyFileName);
    }
  }
  if (!serverFlag && !serverTLSFlag)
  {
    if ((port == 0) && (tlsPort == 0))
    {
      printError("Cannot start any server (error: no port numbers specified)!\n");
    }
    else
    {
      printError("Cannot start any server!\n");
    }
    return ERROR_INVALID_ARGUMENT;
  }

  /* start threads */
  if (!Thread_init(&jobThread,"BAR job",globalOptions.niceLevel,jobThreadEntry,NULL))
  {
    HALT_FATAL_ERROR("Cannot initialise job thread!");
  }
  if (!Thread_init(&schedulerThread,"BAR scheduler",globalOptions.niceLevel,schedulerThreadEntry,NULL))
  {
    HALT_FATAL_ERROR("Cannot initialise scheduler thread!");
  }

  /* run server */
  clientName = String_new();
  while (!quitFlag)
  {
    /* wait for command */
//fprintf(stderr,"%s,%d: \n",__FILE__,__LINE__);
    FD_ZERO(&selectSet);
    if (serverFlag   ) FD_SET(Network_getServerSocket(&serverSocketHandle),   &selectSet);
    if (serverTLSFlag) FD_SET(Network_getServerSocket(&serverTLSSocketHandle),&selectSet);
    clientNode = clientList.head;
    while (clientNode != NULL)
    {
      FD_SET(Network_getSocket(&clientNode->clientInfo.network.socketHandle),&selectSet);
      clientNode = clientNode->next;
    }
    select(FD_SETSIZE,&selectSet,NULL,NULL,NULL);
//fprintf(stderr,"%s,%d: \n",__FILE__,__LINE__);

    /* connect new clients */
    if (serverFlag && FD_ISSET(Network_getServerSocket(&serverSocketHandle),&selectSet))
    {
      error = Network_accept(&socketHandle,
                             &serverSocketHandle,
                             SOCKET_FLAG_NON_BLOCKING
                            );
      if (error == ERROR_NONE)
      {
        Network_getRemoteInfo(&socketHandle,clientName,&clientPort);
        clientNode = newClient();
        assert(clientNode != NULL);
        initNetworkClient(&clientNode->clientInfo,clientName,clientPort,socketHandle);
        List_append(&clientList,clientNode);

        printInfo(1,"Connected client '%s:%u'\n",String_cString(clientNode->clientInfo.network.name),clientNode->clientInfo.network.port);
      }
      else
      {
        printError("Cannot establish client connection (error: %s)!\n",
                   Errors_getText(error)
                  );
      }
    }
    if (serverTLSFlag && FD_ISSET(Network_getServerSocket(&serverTLSSocketHandle),&selectSet))
    {
      error = Network_accept(&socketHandle,
                             &serverTLSSocketHandle,
                             SOCKET_FLAG_NON_BLOCKING
                            );
      if (error == ERROR_NONE)
      {
        Network_getRemoteInfo(&socketHandle,clientName,&clientPort);
        clientNode = newClient();
        assert(clientNode != NULL);
        initNetworkClient(&clientNode->clientInfo,clientName,clientPort,socketHandle);
        List_append(&clientList,clientNode);

        printInfo(1,"Connected client '%s:%u' (TLS/SSL)\n",String_cString(clientNode->clientInfo.network.name),clientNode->clientInfo.network.port);
      }
      else
      {
        printError("Cannot establish client TLS connection (error: %s)!\n",
                   Errors_getText(error)
                  );
      }
    }

    /* process client commands/disconnect clients */
    clientNode = clientList.head;
    while (clientNode != NULL)
    {
      if (FD_ISSET(Network_getSocket(&clientNode->clientInfo.network.socketHandle),&selectSet))
      {
        Network_receive(&clientNode->clientInfo.network.socketHandle,buffer,sizeof(buffer),WAIT_FOREVER,&receivedBytes);
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
                processCommand(&clientNode->clientInfo,clientNode->commandString);
                String_clear(clientNode->commandString);
              }
            }
            error = Network_receive(&clientNode->clientInfo.network.socketHandle,buffer,sizeof(buffer),WAIT_FOREVER,&receivedBytes);
          }
          while ((error == ERROR_NONE) && (receivedBytes > 0));
//fprintf(stderr,"%s,%d: \n",__FILE__,__LINE__);

          clientNode = clientNode->next;
        }
        else
        {
          /* disconnect */
          printInfo(1,"Disconnected client '%s:%u'\n",String_cString(clientNode->clientInfo.network.name),clientNode->clientInfo.network.port);

          deleteClientNode = clientNode;
          clientNode = clientNode->next;
          List_remove(&clientList,deleteClientNode);
          Network_disconnect(&deleteClientNode->clientInfo.network.socketHandle);
          deleteClient(deleteClientNode);
        }
      }
      else
      {
        clientNode = clientNode->next;
      }
    }

    /* disconnect clients because of authorization failure */
    clientNode = clientList.head;
    while (clientNode != NULL)
    {
      if (clientNode->clientInfo.authorizationState == AUTHORIZATION_STATE_FAIL)
      {
        /* authorizaton error -> disconnect  client */
        printInfo(1,"Disconnected client '%s:%u': authorization failure\n",String_cString(clientNode->clientInfo.network.name),clientNode->clientInfo.network.port);

        deleteClientNode = clientNode;
        clientNode = clientNode->next;
        List_remove(&clientList,deleteClientNode);

        Network_disconnect(&deleteClientNode->clientInfo.network.socketHandle);
        deleteClient(deleteClientNode);
      }
      else
      {
        /* next client */
        clientNode = clientNode->next;
      }
    }
  }
  String_delete(clientName);

  /* wait for thread exit */
  Thread_join(&schedulerThread);
  Thread_join(&jobThread);

  /* done server */
  if (serverFlag   ) Network_doneServer(&serverSocketHandle);
  if (serverTLSFlag) Network_doneServer(&serverTLSSocketHandle);

  /* free resources */
  Thread_done(&schedulerThread);
  Thread_done(&jobThread);
  List_done(&clientList,(ListNodeFreeFunction)freeClientNode,NULL);
  List_done(&jobList,(ListNodeFreeFunction)freeJobNode,NULL);

  return ERROR_NONE;
}

Errors Server_batch(int inputDescriptor,
                    int outputDescriptor
                   )
{
  Errors     error;
  FileHandle inputFileHandle,outputFileHandle;
  ClientInfo clientInfo;
  String     commandString;

  /* initialize variables */
  List_init(&jobList);
  quitFlag = FALSE;

  /* initialize input/output */
  error = File_openDescriptor(&inputFileHandle,inputDescriptor,FILE_OPENMODE_READ);
  if (error != ERROR_NONE)
  {
    printError("Cannot initialize input (error: %s)!\n",
               Errors_getText(error)
              );
    return error;
  }
  error = File_openDescriptor(&outputFileHandle,outputDescriptor,FILE_OPENMODE_WRITE);
  if (error != ERROR_NONE)
  {
    printError("Cannot initialize input (error: %s)!\n",
               Errors_getText(error)
              );
    File_close(&inputFileHandle);
    return error;
  }

  /* init client */
  initBatchClient(&clientInfo,outputFileHandle);

  /* send info */
  File_printLine(&outputFileHandle,
                 "BAR VERSION %d %d",
                 PROTOCOL_VERSION_MAJOR,PROTOCOL_VERSION_MINOR
                );

  /* run server */
  commandString = String_new();
#if 1
  while (!quitFlag && !File_eof(&inputFileHandle))
  {
    /* read command line */
    File_readLine(&inputFileHandle,commandString);

    /* process */
    processCommand(&clientInfo,commandString);
  }
#else /* 0 */
fprintf(stderr,"%s,%d: \n",__FILE__,__LINE__);
String_setCString(commandString,"1 SET crypt-password 'muster'");processCommand(&clientInfo,commandString);
String_setCString(commandString,"2 ADD_INCLUDE_PATTERN REGEX test/[^/]*");processCommand(&clientInfo,commandString);
String_setCString(commandString,"3 ARCHIVE_LIST test.bar");processCommand(&clientInfo,commandString);
//String_setCString(commandString,"3 ARCHIVE_LIST backup/backup-torsten-bar-000.bar");processCommand(&clientInfo,commandString);
//String_setCString(commandString,"4 ARCHIVE_LIST scp:torsten@it-zo.de:backup/backup-torsten-bar-000.bar");processCommand(&clientInfo,commandString);
processCommand(&clientInfo,commandString);
#endif /* 0 */
  String_delete(commandString);

  /* free resources */
  doneClient(&clientInfo);
  File_close(&outputFileHandle);
  File_close(&inputFileHandle);
  List_done(&jobList,(ListNodeFreeFunction)freeJobNode,NULL);

  return ERROR_NONE;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
