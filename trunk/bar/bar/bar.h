/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: Backup ARchiver main program
* Systems: all
*
\***********************************************************************/

#ifndef __BAR__
#define __BAR__

/****************************** Includes *******************************/
#include <config.h>  // use <...> to support separated build directory

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include "forward.h"         // required for JobOptions

#include "global.h"
#include "lists.h"
#include "strings.h"
#include "configvalues.h"

#include "patterns.h"
#include "compress.h"
#include "passwords.h"
#include "crypt.h"
#include "misc.h"
#include "database.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

// file name extensions
#define FILE_NAME_EXTENSION_ARCHIVE_FILE     ".bar"
#define FILE_NAME_EXTENSION_INCREMENTAL_FILE ".bid"

// program exit codes
typedef enum
{
  EXITCODE_OK=0,
  EXITCODE_FAIL=1,

  EXITCODE_INVALID_ARGUMENT=5,
  EXITCODE_CONFIG_ERROR,

  EXITCODE_TESTCODE=124,
  EXITCODE_INIT_FAIL=125,
  EXITCODE_FATAL_ERROR=126,
  EXITCODE_FUNCTION_NOT_SUPPORTED=127,

  EXITCODE_UNKNOWN=128
} ExitCodes;

// run modes
typedef enum
{
  RUN_MODE_INTERACTIVE,
  RUN_MODE_BATCH,
  RUN_MODE_SERVER,
} RunModes;

// log types
typedef enum
{
  LOG_TYPE_ALWAYS              = 0,
  LOG_TYPE_ERROR               = (1 <<  0),
  LOG_TYPE_WARNING             = (1 <<  1),
  LOG_TYPE_ENTRY_OK            = (1 <<  2),
  LOG_TYPE_ENTRY_TYPE_UNKNOWN  = (1 <<  3),
  LOG_TYPE_ENTRY_ACCESS_DENIED = (1 <<  4),
  LOG_TYPE_ENTRY_MISSING       = (1 <<  5),
  LOG_TYPE_ENTRY_INCOMPLETE    = (1 <<  6),
  LOG_TYPE_ENTRY_EXCLUDED      = (1 <<  7),
  LOG_TYPE_STORAGE             = (1 <<  8),
  LOG_TYPE_INDEX               = (1 <<  9),
} LogTypes;

#define LOG_TYPE_NONE 0x00000000
#define LOG_TYPE_ALL  0xFFFFffff

// archive types
typedef enum
{
  ARCHIVE_TYPE_NORMAL,                  // normal archives; no incremental list file
  ARCHIVE_TYPE_FULL,                    // full archives, create incremental list file
  ARCHIVE_TYPE_INCREMENTAL,             // incremental achives
  ARCHIVE_TYPE_DIFFERENTIAL,            // differential achives
  ARCHIVE_TYPE_UNKNOWN,
} ArchiveTypes;

#define SCHEDULE_ANY -1
/*
#define SCHEDULE_ANY_MONTH \
  (  SET_VALUE(MONTH_JAN) \
   | SET_VALUE(MONTH_FEB) \
   | SET_VALUE(MONTH_MAR) \
   | SET_VALUE(MONTH_APR) \
   | SET_VALUE(MONTH_MAY) \
   | SET_VALUE(MONTH_JUN) \
   | SET_VALUE(MONTH_JUL) \
   | SET_VALUE(MONTH_AUG) \
   | SET_VALUE(MONTH_SEP) \
   | SET_VALUE(MONTH_OCT) \
   | SET_VALUE(MONTH_NOV) \
   | SET_VALUE(MONTH_DEC) \
  )
*/
#define SCHEDULE_ANY_DAY \
  (  SET_VALUE(WEEKDAY_MON) \
   | SET_VALUE(WEEKDAY_TUE) \
   | SET_VALUE(WEEKDAY_WED) \
   | SET_VALUE(WEEKDAY_THU) \
   | SET_VALUE(WEEKDAY_FRI) \
   | SET_VALUE(WEEKDAY_SAT) \
   | SET_VALUE(WEEKDAY_SUN) \
  )

#define MAX_CONNECTION_COUNT_UNLIMITED MAX_INT
#define MAX_STORAGE_SIZE_UNLIMITED     MAX_INT64

/***************************** Datatypes *******************************/

// band width usage
typedef struct BandWidthNode
{
  LIST_NODE_HEADER(struct BandWidthNode);

  int    year;                                           // valid year or SCHEDULE_ANY
  int    month;                                          // valid month or SCHEDULE_ANY
  int    day;                                            // valid day or SCHEDULE_ANY
  int    hour;                                           // valid hour or SCHEDULE_ANY
  int    minute;                                         // valid minute or SCHEDULE_ANY
  long   weekDays;                                       // valid weekdays or SCHEDULE_ANY_DAY
  ulong  n;                                              // band with limit [bits/s]
  String fileName;                                       // file to read band width from
} BandWidthNode;

typedef struct
{
  LIST_HEADER(BandWidthNode);
  ulong  n;
  uint64 lastReadTimestamp;
} BandWidthList;

// password mode
typedef enum
{
  PASSWORD_MODE_DEFAULT,                                 // use global password
  PASSWORD_MODE_ASK,                                     // ask for password
  PASSWORD_MODE_CONFIG                                   // use password from config
} PasswordModes;

// FTP server settings
typedef struct
{
  String           loginName;                            // login name
  Password         *password;                            // login password
} FTPServer;

// SSH server settings
typedef struct
{
  uint             port;                                 // server port (ssh,scp,sftp)
  String           loginName;                            // login name
  Password         *password;                            // login password
  String           publicKeyFileName;                    // public key file name (ssh,scp,sftp)
  String           privateKeyFileName;                   // private key file name (ssh,scp,sftp)
} SSHServer;

// WebDAV server settings
typedef struct
{
  String           loginName;                            // login name
  Password         *password;                            // login password
  String           publicKeyFileName;                    // public key file name
  String           privateKeyFileName;                   // private key file name
} WebDAVServer;

// server types
typedef enum
{
  SERVER_TYPE_FTP,
  SERVER_TYPE_SSH,
  SERVER_TYPE_WEBDAV
} ServerTypes;

// server connection priority
typedef enum
{
  SERVER_CONNECTION_PRIORITY_LOW,
  SERVER_CONNECTION_PRIORITY_HIGH,
} ServerConnectionPriorities;

// server
typedef struct
{
  Semaphore   lock;
  String      name;                                      // server name
  ServerTypes type;                                      // server type
  union
  {
    FTPServer    ftpServer;
    SSHServer    sshServer;
    WebDAVServer webDAVServer;
  };
  uint        maxConnectionCount;                        // max. number of concurrent connections or MAX_CONNECTION_COUNT_UNLIMITED
  uint64      maxStorageSize;                            // max. number of bytes to store on server
  struct
  {
    uint      lowPriorityRequestCount;                   // number of waiting low priority connection requests
    uint      highPriorityRequestCount;                  // number of waiting high priority connection requests
    uint      count;                                     // number of current connections
  }           connection;
} Server;

// server node
typedef struct ServerNode
{
  LIST_NODE_HEADER(struct ServerNode);

  Server server;
} ServerNode;

// server list
typedef struct
{
  LIST_HEADER(ServerNode);
} ServerList;

// file/FTP/SCP/SFTP/WebDAV settings
typedef struct
{
  String writePreProcessCommand;                         // command to execute before writing
  String writePostProcessCommand;                        // command to execute after writing
} File;

// optical disk settings
typedef struct
{
  String defaultDeviceName;                              // default device name
  String requestVolumeCommand;                           // command to request new medium
  String unloadVolumeCommand;                            // command to unload medium
  String loadVolumeCommand;                              // command to load medium
  uint64 volumeSize;                                     // size of medium [bytes] (0 for default)

  String imagePreProcessCommand;                         // command to execute before creating image
  String imagePostProcessCommand;                        // command to execute after created image
  String imageCommand;                                   // command to create medium image
  String eccPreProcessCommand;                           // command to execute before ECC calculation
  String eccPostProcessCommand;                          // command to execute after ECC calculation
  String eccCommand;                                     // command for ECC calculation
  String writePreProcessCommand;                         // command to execute before writing medium
  String writePostProcessCommand;                        // command to execute after writing medium
  String writeCommand;                                   // command to write medium
  String writeImageCommand;                              // command to write image on medium
} OpticalDisk;

// device settings
typedef struct
{
  String defaultDeviceName;                              // default device name
  String requestVolumeCommand;                           // command to request new volume
  String unloadVolumeCommand;                            // command to unload volume
  String loadVolumeCommand;                              // command to load volume
  uint64 volumeSize;                                     // size of volume [bytes]

  String imagePreProcessCommand;                         // command to execute before creating image
  String imagePostProcessCommand;                        // command to execute after created image
  String imageCommand;                                   // command to create volume image
  String eccPreProcessCommand;                           // command to execute before ECC calculation
  String eccPostProcessCommand;                          // command to execute after ECC calculation
  String eccCommand;                                     // command for ECC calculation
  String writePreProcessCommand;                         // command to execute before writing volume
  String writePostProcessCommand;                        // command to execute after writing volume
  String writeCommand;                                   // command to write volume
} Device;

typedef struct DeviceNode
{
  LIST_NODE_HEADER(struct DeviceNode);

  String name;                                           // device name
  Device device;
} DeviceNode;

typedef struct
{
  LIST_HEADER(DeviceNode);
} DeviceList;

// global options
typedef struct
{
  RunModes               runMode;

  const char             *barExecutable;                 // name of BAR executable

  uint                   niceLevel;                      // nice level 0..19
  uint                   maxThreads;                     // max. number of concurrent compress/encryption threads or 0

  String                 tmpDirectory;                   // directory for temporary files
  uint64                 maxTmpSize;                     // max. size of temporary files

  BandWidthList          maxBandWidthList;               // list of max. send/receive bandwidth to use [bits/s]

  ulong                  compressMinFileSize;            // min. size of file for using compression

  Password               *cryptPassword;                 // default password for encryption/decryption

  Server                 *ftpServer;                     // current selected FTP server
  Server                 *defaultFTPServer;              // default FTP server

  Server                 *sshServer;                     // current selected SSH server
  Server                 *defaultSSHServer;              // default SSH server

  Server                 *webDAVServer;                  // current selected WebDAV server
  Server                 *defaultWebDAVServer;           // default WebDAV server

  const ServerList       *serverList;                    // list with FTP/SSH/WebDAV servers

  String                 remoteBARExecutable;

  File                   file;                           // file settings
  File                   ftp;                            // ftp settings
  File                   scp;                            // scp settings
  File                   sftp;                           // sftp settings
  File                   webdav;                         // WebDAV settings
  OpticalDisk            cd;                             // CD settings
  OpticalDisk            dvd;                            // DVD settings
  OpticalDisk            bd;                             // BD settings

  Device                 *device;                        // current selected device
  const DeviceList       *deviceList;                    // list with devices
  Device                 *defaultDevice;                 // default device

  bool                   indexDatabaseAutoUpdateFlag;    // TRUE for automatic update of index datbase
  BandWidthList          indexDatabaseMaxBandWidthList;  // list of max. band width to use for index updates [bits/s]
  uint                   indexDatabaseKeepTime;          // number of seconds to keep index data of not existing storage

  bool                   groupFlag;                      // TRUE iff entries in list should be grouped
  bool                   allFlag;                        // TRUE iff all entries should be listed/restored
  bool                   longFormatFlag;                 // TRUE iff long format list
  bool                   humanFormatFlag;                // TRUE iff human format list
  bool                   noHeaderFooterFlag;             // TRUE iff no header/footer should be printed in list
  bool                   deleteOldArchiveFilesFlag;      // TRUE iff old archive files should be deleted after creating new files
  bool                   ignoreNoBackupFileFlag;         // TRUE iff .nobackup/.NOBACKUP file should be ignored

  bool                   noDefaultConfigFlag;            // TRUE iff default config should not be read
  bool                   quietFlag;                      // TRUE iff suppress any output
  long                   verboseLevel;                   /* verbosity level
                                                              0 - none
                                                              1 - fatal errors
                                                              2 - processing information
                                                              3 - external programs
                                                              4 - stdout+stderr of external programs
                                                              5 - some SSH debug debug
                                                              6 - all SSH/FTP/WebDAV debug
                                                         */

  bool                   serverDebugFlag;                // TRUE iff server debug enabled (for debug only)
} GlobalOptions;

// schedule
typedef struct
{
  int year;                                              // year or SCHEDULE_ANY
  int month;                                             // month or SCHEDULE_ANY
  int day;                                               // day or SCHEDULE_ANY
} ScheduleDate;
typedef long ScheduleWeekDays;                           // week days set or SCHEDULE_ANY
typedef struct
{
  int hour;                                              // hour or SCHEDULE_ANY
  int minute;                                            // minute or SCHEDULE_ANY
} ScheduleTime;
typedef struct ScheduleNode
{
  LIST_NODE_HEADER(struct ScheduleNode);

  ScheduleDate     date;
  ScheduleWeekDays weekDays;
  ScheduleTime     time;
  ArchiveTypes     archiveType;                          // archive type to create
  String           customText;                           // custom text
  bool             enabledFlag;                          // TRUE iff enabled
} ScheduleNode;

typedef struct
{
  LIST_HEADER(ScheduleNode);
} ScheduleList;

// job options
typedef struct
{
  uint32 userId;                                         // restore user id
  uint32 groupId;                                        // restore group id
} JobOptionsOwner;

typedef struct
{
  CompressAlgorithms delta;                              // delta compress algorithm to use
  CompressAlgorithms byte;                               // byte compress algorithm to use
} JobOptionsCompressAlgorithm;

// see forward declaration in forward.h
struct JobOptions
{
  ArchiveTypes                archiveType;               // archive type (normal, full, incremental, differential)

  uint64                      archivePartSize;           // archive part size [bytes]

  String                      incrementalListFileName;   // name of incremental list file

  uint                        directoryStripCount;       // number of directories to strip in restore
  String                      destination;               // destination for restore
  JobOptionsOwner             owner;                     // restore owner

  PatternTypes                patternType;               // pattern type

  JobOptionsCompressAlgorithm compressAlgorithm;         // compress algorithms

  CryptTypes                  cryptType;                 // crypt type (symmetric, asymmetric)
  CryptAlgorithms             cryptAlgorithm;            // crypt algorithm to use
  PasswordModes               cryptPasswordMode;         // crypt password mode
  Password                    *cryptPassword;            // crypt password
  String                      cryptPublicKeyFileName;
  String                      cryptPrivateKeyFileName;

  FTPServer                   ftpServer;                 // job specific FTP server settings
  SSHServer                   sshServer;                 // job specific SSH server settings
  WebDAVServer                webDAVServer;              // job specific WebDAV server settings

  OpticalDisk                 opticalDisk;               // job specific optical disk settings

  String                      deviceName;                // device name to use
  Device                      device;                    // job specific device settings

  uint64                      volumeSize;                // volume size or 0LL for default [bytes]

  bool                        skipUnreadableFlag;        // TRUE for skipping unreadable files
  bool                        forceDeltaCompressionFlag; // TRUE to force delta compression of files
  bool                        ignoreNoDumpAttributeFlag; // TRUE for ignoring no-dump attribute
  bool                        overwriteArchiveFilesFlag; // TRUE for overwrite existing archive files
  bool                        overwriteFilesFlag;        // TURE for overwrite existing files on restore
  bool                        errorCorrectionCodesFlag;  // TRUE iff error correction codes should be added
  bool                        alwaysCreateImageFlag;     // TRUE iff always create image for CD/DVD/BD/device
  bool                        waitFirstVolumeFlag;       // TRUE for wait for first volume
  bool                        rawImagesFlag;             // TRUE for storing raw images
  bool                        noFragmentsCheckFlag;      // TRUE to skip checking file fragments for completeness
  bool                        noIndexDatabaseFlag;       // TRUE for do not store index database for archives
  bool                        dryRunFlag;                // TRUE to do a dry-run (do not store, do not create incremental data, do not store in database)
  bool                        noStorageFlag;             // TRUE to skip storage, only create incremental data file
  bool                        noBAROnMediumFlag;         // TRUE for not storing BAR on medium
  bool                        stopOnErrorFlag;
};

/***************************** Variables *******************************/
extern GlobalOptions  globalOptions;          // global options
extern String         tmpDirectory;           // temporary directory
extern DatabaseHandle *indexDatabaseHandle;   // index database handle
extern Semaphore      consoleLock;            // lock console

/****************************** Macros *********************************/

// return short number of bytes
#define BYTES_SHORT(n) (((n)>(1024LL*1024LL*1024LL))?(double)(n)/(double)(1024LL*1024LL*1024LL): \
                        ((n)>       (1024LL*1024LL))?(double)(n)/(double)(1024LL*1024LL*1024LL): \
                        ((n)>                1024LL)?(double)(n)/(double)(1024LL*1024LL*1024LL): \
                        (double)(n) \
                       )
// return unit for short number of bytes
#define BYTES_UNIT(n) (((n)>(1024LL*1024LL*1024LL))?"GB": \
                       ((n)>       (1024LL*1024LL))?"MB": \
                       ((n)>                1024LL)?"KB": \
                       "bytes" \
                      )

#define CONSOLE_LOCKED_DO(semaphoreLock,semaphore,semaphoreLockType) \
  for (semaphoreLock = lockConsole(); \
       semaphoreLock; \
       unlockConsole(semaphore), semaphoreLock = FALSE \
      )

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : getErrorText
* Purpose: get errror text of error code
* Input  : error - error
* Output : -
* Return : error text (read only!)
* Notes  : -
\***********************************************************************/

const char *getErrorText(Errors error);

/***********************************************************************\
* Name   : isPrintInfo
* Purpose: check if info should be printed
* Input  : verboseLevel - verbosity level
* Output : -
* Return : true iff info should be printed
* Notes  : -
\***********************************************************************/

bool isPrintInfo(uint verboseLevel);

/***********************************************************************\
* Name   : vprintInfo, pprintInfo, printInfo
* Purpose: output info to console
* Input  : verboseLevel - verbosity level
*          prefix       - prefix text
*          format       - format string (like printf)
*          arguments    - arguments
*          ...          - optional arguments (like printf)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void vprintInfo(uint verboseLevel, const char *prefix, const char *format, va_list arguments);
void pprintInfo(uint verboseLevel, const char *prefix, const char *format, ...);
void printInfo(uint verboseLevel, const char *format, ...);

/***********************************************************************\
* Name   : vlogMessage, plogMessage, logMessage
* Purpose: log message
*          logType   - log type; see LOG_TYPES_*
*          prefix    - prefix text
*          text      - format string (like printf)
*          arguments - arguments
*          ...       - optional arguments (like printf)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void vlogMessage(ulong logType, const char *prefix, const char *text, va_list arguments);
void plogMessage(ulong logType, const char *prefix, const char *text, ...);
void logMessage(ulong logType, const char *text, ...);

/***********************************************************************\
* Name   : lockConsole
* Purpose: lock console
* Input  : -
* Output : -
* Return : TRUE iff locked
* Notes  : -
\***********************************************************************/

bool lockConsole(void);

/***********************************************************************\
* Name   : unlockConsole
* Purpose: unlock console
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void unlockConsole(void);

/***********************************************************************\
* Name   : printConsole
* Purpose: output to console
* Input  : file         - stdout or stderr
*          format       - format string (like printf)
*          ...          - optional arguments (like printf)
*          arguments    - arguments
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void printConsole(FILE *file, const char *format, ...);

/***********************************************************************\
* Name   : printWarning
* Purpose: output warning on console
* Input  : text - format string (like printf)
*          ...  - optional arguments (like printf)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void printWarning(const char *text, ...);

/***********************************************************************\
* Name   : printError
* Purpose: print error message on stderr
*          text - format string (like printf)
*          ...  - optional arguments (like printf)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void printError(const char *text, ...);

/***********************************************************************\
* Name   : logPostProcess
* Purpose: log post processing
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void logPostProcess(void);

/***********************************************************************\
* Name   : initJobOptions
* Purpose: init job options structure
* Input  : jobOptions - job options variable
* Output : jobOptions - initialized job options variable
* Return : -
* Notes  : -
\***********************************************************************/

void initJobOptions(JobOptions *jobOptions);

/***********************************************************************\
* Name   : initDuplicateJobOptions
* Purpose: init duplicated job options structure
* Input  : jobOptions     - job options variable
*          fromJobOptions - source job options
* Output : jobOptions - initialized job options variable
* Return : -
* Notes  : -
\***********************************************************************/

void initDuplicateJobOptions(JobOptions *jobOptions, const JobOptions *fromJobOptions);

/***********************************************************************\
* Name   : copyJobOptions
* Purpose: copy job options structure
* Input  : fromJobOptions - source job options
*          toJobOptions   - destination job options variable
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void copyJobOptions(const JobOptions *fromJobOptions, JobOptions *toJobOptions);

/***********************************************************************\
* Name   : doneJobOptions
* Purpose: done job options structure
* Input  : jobOptions - job options
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void doneJobOptions(JobOptions *jobOptions);

/***********************************************************************\
* Name   : getBandWidth
* Purpose: get band width from value or external file
* Input  : bandWidthList - band width list settings or NULL
* Output : -
* Return : return band width [bits/s] or 0
* Notes  : -
\***********************************************************************/

ulong getBandWidth(BandWidthList *bandWidthList);

/***********************************************************************\
* Name   : getFTPServerSettings
* Purpose: get FTP server settings
* Input  : hostName   - FTP server host name
*          jobOptions - job options
* Output : ftperver   - FTP server settings from job options, server
*                       list or default FTP server values
* Return : server
* Notes  : -
\***********************************************************************/

Server *getFTPServerSettings(const String     hostName,
                             const JobOptions *jobOptions,
                             FTPServer        *ftpServer
                            );

/***********************************************************************\
* Name   : getSSHServerSettings
* Purpose: get SSH server settings
* Input  : hostName   - SSH server host name
*          jobOptions - job options
* Output : sshServer  - SSH server settings from job options, server
*                       list or default SSH server values
* Return : server
* Notes  : -
\***********************************************************************/

Server *getSSHServerSettings(const String     hostName,
                             const JobOptions *jobOptions,
                             SSHServer        *sshServer
                            );

/***********************************************************************\
* Name   : getWebDAVServerSettings
* Purpose: get WebDAV server settings
* Input  : hostName   - WebDAV server host name
*          jobOptions - job options
* Output : webDAVServer - WebDAV server settings from job options,
*                         server list or default WebDAV server values
* Return : server
* Notes  : -
\***********************************************************************/

Server *getWebDAVServerSettings(const String     hostName,
                                          const JobOptions *jobOptions,
                                          WebDAVServer     *webDAVServer
                                         );

/***********************************************************************\
* Name   : getCDSettings
* Purpose: get CD settings
* Input  : jobOptions - job options
* Output : cd - cd settings from job options or default CD values
* Return : -
* Notes  : -
\***********************************************************************/

void getCDSettings(const JobOptions *jobOptions,
                   OpticalDisk      *cd
                  );

/***********************************************************************\
* Name   : getDVDSettings
* Purpose: get DVD settings
* Input  : jobOptions - job options
* Output : dvd - dvd settings from job options or default DVD values
* Return : -
* Notes  : -
\***********************************************************************/

void getDVDSettings(const JobOptions *jobOptions,
                    OpticalDisk      *dvd
                   );

/***********************************************************************\
* Name   : getDVDSettings
* Purpose: get DVD settings
* Input  : jobOptions - job options
* Output : bd - bd settings from job options or default BD values
* Return : -
* Notes  : -
\***********************************************************************/

void getBDSettings(const JobOptions *jobOptions,
                   OpticalDisk      *bd
                  );

/***********************************************************************\
* Name   : getDeviceSettings
* Purpose: get device settings
* Input  : name       - device name
*          jobOptions - job options
* Output : device - device settings from job options, device list or
*                   default device values
* Return : -
* Notes  : -
\***********************************************************************/

void getDeviceSettings(const String     name,
                       const JobOptions *jobOptions,
                       Device           *device
                      );

/***********************************************************************\
* Name   : allocateServer
* Purpose: allocate server
* Input  : server   - server
*          priority - server connection priority; see
*                     SERVER_CONNECTION_PRIORITY_...
*          timeout  - timeout or -1 [ms]
* Output : -
* Return : TRUE iff server allocated, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool allocateServer(Server *server, ServerConnectionPriorities priority, long timeout);

/***********************************************************************\
* Name   : freeServer
* Purpose: free allocated server
* Input  : server - server
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void freeServer(Server *server);

/***********************************************************************\
* Name   : isServerAllocationPending
* Purpose: check if a server allocation with high priority is pending
* Input  : server - server
* Output : -
* Return : TRUE if server allocation with high priority is pending,
*          FALSE otherwise
* Notes  : -
\***********************************************************************/

bool isServerAllocationPending(Server *server);

/***********************************************************************\
* Name   : inputCryptPassword
* Purpose: input crypt password
* Input  : userData      - (not used)
*          password      - crypt password variable
*          fileName      - file name
*          validateFlag  - TRUE to validate input, FALSE otherwise
*          weakCheckFlag - TRUE for weak password checking, FALSE
*                          otherwise (print warning if password seems to
*                          be a weak password)
* Output : password - crypt password
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors inputCryptPassword(void         *userData,
                          Password     *password,
                          const String fileName,
                          bool         validateFlag,
                          bool         weakCheckFlag
                         );

/***********************************************************************\
* Name   : configValueParseBandWidth
* Purpose: config value call back for parsing band width setting
*          patterns
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseBandWidth(void *userData, void *variable, const char *name, const char *value, char *errorMessage, uint maxErrorMessageLength);

/***********************************************************************\
* Name   : configValueFormatInitOwner
* Purpose: init format of config band width settings
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitBandWidth(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneBandWidth
* Purpose: done format of config band width setting
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneBandWidth(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatBandWidth
* Purpose: format next config band width setting
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Notes  : -
\***********************************************************************/

bool configValueFormatBandWidth(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseOwner
* Purpose: config value call back for parsing owner
*          patterns
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseOwner(void *userData, void *variable, const char *name, const char *value, char *errorMessage, uint maxErrorMessageLength);

/***********************************************************************\
* Name   : configValueFormatInitOwner
* Purpose: init format of config owner statements
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitOwner(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneOwner
* Purpose: done format of config owner statements
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneOwner(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatOwner
* Purpose: format next config owner statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Notes  : -
\***********************************************************************/

bool configValueFormatOwner(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseFileEntry, configValueParseImageEntry
* Purpose: config value option call back for parsing include/exclude
*          patterns
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseFileEntry(void *userData, void *variable, const char *name, const char *value, char *errorMessage, uint maxErrorMessageLength);
bool configValueParseImageEntry(void *userData, void *variable, const char *name, const char *value, char *errorMessage, uint maxErrorMessageLength);

/***********************************************************************\
* Name   : configValueFormatInitEntry
* Purpose: init format of config include statements
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitEntry(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneEntry
* Purpose: done format of config include statements
* Input  : formatUserData - format user data
*          userData       - user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneEntry(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatFileEntry, configValueFormatImageEntry
* Purpose: format next config include statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Notes  : -
\***********************************************************************/

bool configValueFormatFileEntry(void **formatUserData, void *userData, String line);
bool configValueFormatImageEntry(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParsePattern
* Purpose: config value option call back for parsing pattern
*          patterns
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParsePattern(void *userData, void *variable, const char *name, const char *value, char *errorMessage, uint maxErrorMessageLength);

/***********************************************************************\
* Name   : configValueFormatInitPattern
* Purpose: init format of config pattern statements
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitPattern(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDonePattern
* Purpose: done format of config pattern statements
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDonePattern(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatPattern
* Purpose: format next config pattern statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Notes  : -
\***********************************************************************/

bool configValueFormatPattern(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseString
* Purpose: config value option call back for parsing string
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Output : -
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseString(void *userData, void *variable, const char *name, const char *value, char *errorMessage, uint maxErrorMessageLength);

/***********************************************************************\
* Name   : configValueParsePassword
* Purpose: config value option call back for parsing password
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParsePassword(void *userData, void *variable, const char *name, const char *value, char *errorMessage, uint maxErrorMessageLength);

/***********************************************************************\
* Name   : configValueFormatInitPassord
* Purpose: init format config password
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitPassord(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDonePassword
* Purpose: done format of config password setting
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDonePassword(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatPassword
* Purpose: format password config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatPassword(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseDeltaCompressAlgorithm
* Purpose: config value option call back for parsing delta compress
*          algorithm
* Input  : userData - user data
*          variable - config variable
*          name     - config name
*          value    - config value
* Output : -
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseDeltaCompressAlgorithm(void *userData, void *variable, const char *name, const char *value);

/***********************************************************************\
* Name   : configValueFormatInitDeltaCompressAlgorithm
* Purpose: init format config compress algorithm
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitDeltaCompressAlgorithm(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneDeltaCompressAlgorithm
* Purpose: done format of config compress algorithm
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneDeltaCompressAlgorithm(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatDeltaCompressAlgorithm
* Purpose: format compress algorithm config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatDeltaCompressAlgorithm(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseByteCompressAlgorithm
* Purpose: config value option call back for parsing byte compress
*          algorithm
* Input  : userData - user data
*          variable - config variable
*          name     - config name
*          value    - config value
* Output : -
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseByteCompressAlgorithm(void *userData, void *variable, const char *name, const char *value);

/***********************************************************************\
* Name   : configValueFormatInitByteCompressAlgorithm
* Purpose: init format config compress algorithm
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitByteCompressAlgorithm(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneByteCompressAlgorithm
* Purpose: done format of config compress algorithm
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneByteCompressAlgorithm(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatByteCompressAlgorithm
* Purpose: format compress algorithm config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatByteCompressAlgorithm(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseCompressAlgorithm
* Purpose: config value option call back for parsing compress algorithm
* Input  : userData - user data
*          variable - config variable
*          name     - config name
*          value    - config value
* Output : -
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseCompressAlgorithm(void *userData, void *variable, const char *name, const char *value, char errorMessage[], uint errorMessageSize);

/***********************************************************************\
* Name   : configValueFormatInitCompressAlgorithm
* Purpose: init format config compress algorithm
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitCompressAlgorithm(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneCompressAlgorithm
* Purpose: done format of config compress algorithm
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneCompressAlgorithm(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatCompressAlgorithm
* Purpose: format compress algorithm config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatCompressAlgorithm(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : parseScheduleDate
* Purpose: parse schedule date
* Input  : date - date string (<year|*>-<month|*>-<day|*>)
* Output :
* Return : scheduleNode or NULL on error
* Notes  : month names: jan, feb, mar, apr, may, jun, jul, aug, sep, oct
*          nov, dec
*          week day names: mon, tue, wed, thu, fri, sat, sun
\***********************************************************************/

ScheduleNode *parseScheduleDate(const String date);

/***********************************************************************\
* Name   : parseScheduleParts
* Purpose: parse schedule parts
* Input  : date        - date string (<year|*>-<month|*>-<day|*>)
*          weekDay     - week days string (<day>,...)
*          time        - time string <hour|*>:<minute|*>
* Output :
* Return : scheduleNode or NULL on error
* Notes  : month names: jan, feb, mar, apr, may, jun, jul, aug, sep, oct
*          nov, dec
*          week day names: mon, tue, wed, thu, fri, sat, sun
\***********************************************************************/

ScheduleNode *parseScheduleWeekDays(const String weekDay);

/***********************************************************************\
* Name   : parseScheduleParts
* Purpose: parse schedule parts
* Input  : date        - date string (<year|*>-<month|*>-<day|*>)
*          weekDay     - week day string
*          time        - time string <hour|*>:<minute|*>
* Output :
* Return : scheduleNode or NULL on error
* Notes  : month names: jan, feb, mar, apr, may, jun, jul, aug, sep, oct
*          nov, dec
*          week day names: mon, tue, wed, thu, fri, sat, sun
\***********************************************************************/

ScheduleNode *parseScheduleTime(const String time);

/***********************************************************************\
* Name   : parseScheduleParts
* Purpose: parse schedule parts
* Input  : date        - date string (<year|*>-<month|*>-<day|*>)
*          weekDays    - week days string (<day>,...)
*          time        - time string <hour|*>:<minute|*>
* Output :
* Return : scheduleNode or NULL on error
* Notes  : month names: jan, feb, mar, apr, may, jun, jul, aug, sep, oct
*          nov, dec
*          week day names: mon, tue, wed, thu, fri, sat, sun
\***********************************************************************/

ScheduleNode *parseScheduleDateTime(const String date,
                                    const String weekDays,
                                    const String time
                                   );

/***********************************************************************\
* Name   : parseSchedule
* Purpose: parse schedule
* Input  : s - schedule string
* Output :
* Return : scheduleNode or NULL on error
* Notes  : string format
*            <year|*>-<month|*>-<day|*> [<week day|*>] <hour|*>:<minute|*> <0|1> <archive type>
*          month names: jan, feb, mar, apr, may, jun, jul, aug, sep, oct
*          nov, dec
*          week day names: mon, tue, wed, thu, fri, sat, sun
*          archive type names: normal, full, incremental, differential
\***********************************************************************/

ScheduleNode *parseSchedule(const String s);

/***********************************************************************\
* Name   : configValueParseScheduleDate
* Purpose: config value option call back for parsing schedule date
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseScheduleDate(void *userData, void *variable, const char *name, const char *value, char errorMessage[], uint errorMessageSize);

/***********************************************************************\
* Name   : configValueFormatInitScheduleDate
* Purpose: init format config schedule
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitScheduleDate(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneScheduleDate
* Purpose: done format of config schedule statements
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneScheduleDate(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatScheduleDate
* Purpose: format schedule config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatScheduleDate(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseScheduleWeekDays
* Purpose: config value option call back for parsing schedule week days
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseScheduleWeekDays(void *userData, void *variable, const char *name, const char *value, char errorMessage[], uint errorMessageSize);

/***********************************************************************\
* Name   : configValueFormatInitSchedule
* Purpose: init format config schedule
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitScheduleWeekDays(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneScheduleWeekDays
* Purpose: done format of config schedule statements
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneScheduleWeekDays(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatScheduleWeekDays
* Purpose: format schedule config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatScheduleWeekDays(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseScheduleTime
* Purpose: config value option call back for parsing schedule time
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseScheduleTime(void *userData, void *variable, const char *name, const char *value, char errorMessage[], uint errorMessageSize);

/***********************************************************************\
* Name   : configValueFormatInitScheduleTime
* Purpose: init format config schedule
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitScheduleTime(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneScheduleTime
* Purpose: done format of config schedule statements
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneScheduleTime(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatScheduleTime
* Purpose: format schedule config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatScheduleTime(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : configValueParseSchedule
* Purpose: config value option call back for parsing schedule
* Input  : userData              - user data
*          variable              - config variable
*          name                  - config name
*          value                 - config value
*          maxErrorMessageLength - max. length of error message text
* Output : errorMessage - error message text
* Return : TRUE if config value parsed and stored in variable, FALSE
*          otherwise
* Notes  : -
\***********************************************************************/

bool configValueParseSchedule(void *userData, void *variable, const char *name, const char *value, char errorMessage[], uint errorMessageSize);

/***********************************************************************\
* Name   : configValueFormatInitSchedule
* Purpose: init format config schedule
* Input  : userData - user data
*          variable - config variable
* Output : formatUserData - format user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatInitSchedule(void **formatUserData, void *userData, void *variable);

/***********************************************************************\
* Name   : configValueFormatDoneSchedule
* Purpose: done format of config schedule statements
* Input  : formatUserData - format user data
*          userData       - user data
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void configValueFormatDoneSchedule(void **formatUserData, void *userData);

/***********************************************************************\
* Name   : configValueFormatSchedule
* Purpose: format schedule config statement
* Input  : formatUserData - format user data
*          userData       - user data
*          line           - line variable
*          name           - config name
* Output : line - formated line
* Return : TRUE if config statement formated, FALSE if end of data
* Return : -
* Notes  : -
\***********************************************************************/

bool configValueFormatSchedule(void **formatUserData, void *userData, String line);

/***********************************************************************\
* Name   : archiveTypeToString
* Purpose: get name for archive type
* Input  : archiveType  - archive type
*          defaultValue - default value
* Output : -
* Return : name
* Notes  : -
\***********************************************************************/

const char *archiveTypeToString(ArchiveTypes archiveType, const char *defaultValue);

/***********************************************************************\
* Name   : parseArchiveType
* Purpose: parse archive type
* Input  : name - normal|full|incremental|differential
* Output : archiveType - archive type
* Return : TRUE iff parsed
* Notes  : -
\***********************************************************************/

bool parseArchiveType(const char *name, ArchiveTypes *archiveType);

#ifdef __cplusplus
  }
#endif

#endif /* __BAR__ */

/* end of file */
