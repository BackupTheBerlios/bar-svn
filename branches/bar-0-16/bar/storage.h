/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/storage.h,v $
* $Revision: 1.14 $
* $Author: torsten $
* Contents: storage functions
* Systems: all
*
\***********************************************************************/

#ifndef __STORAGE__
#define __STORAGE__

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_FTP
  #include <ftplib.h>
#endif /* HAVE_FTP */
#ifdef HAVE_SSH2
  #include <libssh2.h>
  #include <libssh2_sftp.h>
#endif /* HAVE_SSH2 */
#include <assert.h>

#include "global.h"
#include "strings.h"
#include "stringlists.h"
#include "files.h"
#include "network.h"
#include "database.h"
#include "errors.h"

#include "bar.h"
#include "crypt.h"
#include "passwords.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
#define MAX_BAND_WIDTH_MEASUREMENTS 256

/***************************** Datatypes *******************************/

/* storage modes */
typedef enum
{
  STORAGE_REQUEST_VOLUME_NONE,

  STORAGE_REQUEST_VOLUME_OK,
  STORAGE_REQUEST_VOLUME_FAIL,
  STORAGE_REQUEST_VOLUME_UNLOAD,
  STORAGE_REQUEST_VOLUME_ABORTED,

  STORAGE_REQUEST_VOLUME_UNKNOWN,
} StorageRequestResults;

/***********************************************************************\
* Name   : StorageRequestVolumeFunction
* Purpose: request new volume call-back
* Input  : userData - user data
*          volumeNumber - requested volume number
* Output : -
* Return : storage request result; see StorageRequestResults
* Notes  : -
\***********************************************************************/

typedef StorageRequestResults(*StorageRequestVolumeFunction)(void *userData,
                                                             uint volumeNumber
                                                            );

/* status info data */
typedef struct
{
  uint   volumeNumber;                     // current volume number
  double volumeProgress;                   // current volume progress [0..100]
} StorageStatusInfo;

/***********************************************************************\
* Name   : StorageStatusInfoFunction
* Purpose: storage status call-back
* Input  : userData          - user data
*          storageStatusInfo - storage status info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

typedef void(*StorageStatusInfoFunction)(void                    *userData,
                                         const StorageStatusInfo *storageStatusInfo
                                        );

/* storage modes */
typedef enum
{
  STORAGE_MODE_READ,
  STORAGE_MODE_WRITE,
} StorageModes;

/* storage types */
typedef enum
{
  STORAGE_TYPE_FILESYSTEM,
  STORAGE_TYPE_FTP,
  STORAGE_TYPE_SSH,
  STORAGE_TYPE_SCP,
  STORAGE_TYPE_SFTP,
  STORAGE_TYPE_CD,
  STORAGE_TYPE_DVD,
  STORAGE_TYPE_BD,
  STORAGE_TYPE_DEVICE
} StorageTypes;

/* volume states */
typedef enum
{
  STORAGE_VOLUME_STATE_UNKNOWN,
  STORAGE_VOLUME_STATE_UNLOADED,
  STORAGE_VOLUME_STATE_WAIT,
  STORAGE_VOLUME_STATE_LOADED,
} StorageVolumeStates;

/* bandwidth data */
typedef struct
{
  ulong  max;
  ulong  blockSize;
  ulong  measurements[MAX_BAND_WIDTH_MEASUREMENTS];
  uint   measurementNextIndex;
  ulong  measurementBytes;    // sum of transmitted bytes
  uint64 measurementTime;     // time for transmission [us]
} StorageBandWidth;

typedef struct
{
  StorageModes                 mode;
  StorageTypes                 type;
  const JobOptions             *jobOptions;

  StorageRequestVolumeFunction requestVolumeFunction;
  void                         *requestVolumeUserData;
  uint                         volumeNumber;           // current loaded volume number
  uint                         requestedVolumeNumber;  // requested volume number
  StorageVolumeStates          volumeState;            // volume state

  StorageStatusInfoFunction    storageStatusInfoFunction;
  void                         *storageStatusInfoUserData;

  union
  {
    // file storage
    struct
    {
      FileHandle fileHandle;
    } fileSystem;

    #ifdef HAVE_FTP
      // FTP storage
      struct
      {
        String           hostName;                     // FTP server host name
        uint             hostPort;                     // FTP server port number
        String           loginName;                    // FTP login name
        Password         *password;                    // FTP login password

        netbuf           *control;
        netbuf           *data;
        uint64           index;                        // current read/write index in file [0..n-1]
        uint64           size;                         // size of file [bytes]
        struct                                         // read-ahead buffer
        {
          byte   *data;
          uint64 offset;
          ulong  length;
        } readAheadBuffer;
        StorageBandWidth bandWidth;                    // band width data
      } ftp;
    #endif /* HAVE_FTP */

    #ifdef HAVE_SSH2
      // ssh storage (remote BAR)
      struct
      {
        String           hostName;                     // ssh server host name
        uint             hostPort;                     // ssh server port number
        String           loginName;                    // ssh login name
        Password         *password;                    // ssh login password
        String           sshPublicKeyFileName;         // ssh public key file name
        String           sshPrivateKeyFileName;        // ssh private key file name

        SocketHandle     socketHandle;
        LIBSSH2_CHANNEL  *channel;                     // ssh channel
        StorageBandWidth bandWidth;                    // band width data
      } ssh;

      // scp storage
      struct
      {
        String           hostName;                     // ssh server host name
        uint             hostPort;                     // ssh server port number
        String           loginName;                    // ssh login name
        Password         *password;                    // ssh login password
        String           sshPublicKeyFileName;         // ssh public key file name
        String           sshPrivateKeyFileName;        // ssh private key file name

        SocketHandle     socketHandle;
        LIBSSH2_CHANNEL  *channel;                     // scp channel
        uint64           index;                        // current read/write index in file [0..n-1]
        uint64           size;                         // size of file [bytes]
        struct                                         // read-ahead buffer
        {
          byte   *data;
          uint64 offset;
          ulong  length;
        } readAheadBuffer;
        StorageBandWidth bandWidth;                    // band width data
      } scp;

      // sftp storage
      struct
      {
        String              hostName;                  // ssh server host name
        uint                hostPort;                  // ssh server port number
        String              loginName;                 // ssh login name
        Password            *password;                 // ssh login password
        String              sshPublicKeyFileName;      // ssh public key file name
        String              sshPrivateKeyFileName;     // ssh private key file name

        SocketHandle        socketHandle;
        LIBSSH2_SFTP        *sftp;                     // sftp session
        LIBSSH2_SFTP_HANDLE *sftpHandle;               // sftp handle
        uint64              index;                     // current read/write index in file [0..n-1]
        uint64              size;                      // size of file [bytes]
        struct                                         // read-ahead buffer
        {
          byte   *data;
          uint64 offset;
          ulong  length;
        } readAheadBuffer;
        StorageBandWidth bandWidth;                    // band width data
      } sftp;
    #endif /* HAVE_SSH2 */

    // cd/dvd/bd storage
    struct
    {
      String     name;                                 // CD/DVD/BD device name

      String     requestVolumeCommand;                 // command to request new CD/DVD/BD
      String     unloadVolumeCommand;                  // command to unload CD/DVD/BD
      String     loadVolumeCommand;                    // command to load CD/DVD/BD
      uint64     volumeSize;                           // size of CD/DVD/BD [bytes]
      String     imagePreProcessCommand;               // command to execute before creating image
      String     imagePostProcessCommand;              // command to execute after created image
      String     imageCommand;                         // command to create CD/DVD/BD image
      String     eccPreProcessCommand;                 // command to execute before ECC calculation
      String     eccPostProcessCommand;                // command to execute after ECC calculation
      String     eccCommand;                           // command for ECC calculation
      String     writePreProcessCommand;               // command to execute before writing CD/DVD/BD
      String     writePostProcessCommand;              // command to execute after writing CD/DVD/BD
      String     writeCommand;                         // command to write CD/DVD/BD
      String     writeImageCommand;                    // command to write image on CD/DVD/BD
      bool       alwaysCreateImage;                    // TRUE iff always creating image

      uint       steps;                                // total number of steps to create CD/DVD/BD
      String     directory;                            // temporary directory for CD/DVD/BD files

      uint       step;                                 // current step number
      double     progress;                             // progress of current step

      uint       number;                               // current CD/DVD/BD number
      bool       newFlag;                              // TRUE iff new CD/DVD/BD needed
      StringList fileNameList;                         // list with file names
      String     fileName;                             // current file name
      FileHandle fileHandle;
      uint64     totalSize;                            // current size of CD/DVD/BD [bytes]
    } opticalDisk;

    // device storage
    struct
    {
      String     name;                                 // device name

      String     requestVolumeCommand;                 // command to request new volume
      String     unloadVolumeCommand;                  // command to unload volume
      String     loadVolumeCommand;                    // command to load volume
      uint64     volumeSize;                           // size of volume [bytes]
      String     imagePreProcessCommand;               // command to execute before creating image
      String     imagePostProcessCommand;              // command to execute after created image
      String     imageCommand;                         // command to create volume image
      String     eccPreProcessCommand;                 // command to execute before ECC calculation
      String     eccPostProcessCommand;                // command to execute after ECC calculation
      String     eccCommand;                           // command for ECC calculation
      String     writePreProcessCommand;               // command to execute before writing volume
      String     writePostProcessCommand;              // command to execute after writing volume
      String     writeCommand;                         // command to write volume

      String     directory;                            // temporary directory for files

      uint       number;                               // volume number
      bool       newFlag;                              // TRUE iff new volume needed
      StringList fileNameList;                         // list with file names
      String     fileName;                             // current file name
      FileHandle fileHandle;               
      uint64     totalSize;                            // current size [bytes]
    } device;
  };

  StorageStatusInfo runningInfo;
} StorageFileHandle;

typedef struct
{
  StorageTypes type;
  union
  {
    struct
    {
      DirectoryListHandle directoryListHandle;
    } fileSystem;
    #ifdef HAVE_FTP
      struct
      {
        String                  pathName;              // directory name

        String                  fileListFileName;
        FileHandle              fileHandle;
        String                  line;
      } ftp;
    #endif /* HAVE_FTP */
    #ifdef HAVE_SSH2
      struct
      {
        String                  pathName;              // directory name

        SocketHandle            socketHandle;
        LIBSSH2_SESSION         *session;
        LIBSSH2_CHANNEL         *channel;
        LIBSSH2_SFTP            *sftp;
        LIBSSH2_SFTP_HANDLE     *sftpHandle;
        char                    *buffer;               // buffer for reading file names
        ulong                   bufferLength;
        LIBSSH2_SFTP_ATTRIBUTES attributes;
        bool                    entryReadFlag;         // TRUE if entry read
      } sftp;
    #endif /* HAVE_SSH2 */
    struct
    {
      DirectoryListHandle directoryListHandle;
    } opticalDisk;
  };
} StorageDirectoryListHandle;

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : Storage_initAll
* Purpose: initialize storage functions
* Input  : -
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_initAll(void);

/***********************************************************************\
* Name   : Storage_doneAll
* Purpose: deinitialize storage functions
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Storage_doneAll(void);

/***********************************************************************\
* Name   : Storage_getType
* Purpose: get storage type from storage name
* Input  : storageName - storage name
* Output : -
* Return : storage type
* Notes  : storage types supported:
*            ftp://[<user name>[:<password>]@]<host name>[:<port>]/<file name>
*            ssh://[<user name>@]<host name>[:<port>]/<file name>
*            scp://[<user name>@]<host name>[:<port>]/<file name>
*            sftp://[<user name>@]<host name>[:<port>]/<file name>
*            cd://[<device name>:]<file name>
*            dvd://[<device name>:]<file name>
*            bd://[<device name>:]<file name>
*            device://[<device name>:]<file name>
*            file://<file name>
*            plain file name
*
*          name structure:
*            <type>://<storage specifier>/<filename>
\***********************************************************************/

StorageTypes Storage_getType(const String storageName);

/***********************************************************************\
* Name   : Storage_parseName
* Purpose: parse storage name and get storage type
* Input  : storageName - storage name
* Output : storageSpecifier - storage specific data (can be NULL)                             
*          fileName         - storage file name (can be NULL)
* Return : storage type
* Notes  : storage types supported:
*            ftp://[<user name>[:<password>]@]<host name>[:<port>]/<file name>
*            ssh://[<user name>@]<host name>[:<port>]/<file name>
*            scp://[<user name>@]<host name>[:<port>]/<file name>
*            sftp://[<user name>@]<host name>[:<port>]/<file name>
*            cd://[<device name>:]<file name>
*            dvd://[<device name>:]<file name>
*            bd://[<device name>:]<file name>
*            device://[<device name>:]<file name>
*            file://<file name>
*            plain file name
*
*          name structure:
*            <type>://<storage specifier>/<filename>
\***********************************************************************/

StorageTypes Storage_parseName(const String storageName,
                               String       storageSpecifier,
                               String       fileName
                              );

/***********************************************************************\
* Name   : Storage_getName
* Purpose: get storage name
* Input  : storageName      - storage name variable
*          storageType      - storage type; see StorageTypes
*          storageSpecifier - storage specifier
*          fileName         - file name (can be NULL)
* Output : storageName - storage name
* Return : storage name variable
* Notes  : -
\***********************************************************************/

String Storage_getName(String       storageName,
                       StorageTypes storageType,
                       const String storageSpecifier,
                       const String fileName
                      );

/***********************************************************************\
* Name   : Storage_getPrintableName
* Purpose: get printable storage name (without password)
* Input  : storageName - storage name variable
* Output : string - string
* Return : string
* Notes  : -
\***********************************************************************/

String Storage_getPrintableName(String       string,
                                const String storageName
                               );

/***********************************************************************\
* Name   : Storage_parseFTPSpecifier
* Purpose: parse FTP specifier:
*            [<user name>[:<password>]@]<host name>
* Input  : ftpSpecifier  - FTP specifier string
*          loginName     - login user name variable (can be NULL)
*          password      - password variable (can be NULL)
*          hostName      - host name variable (can be NULL)
*          hostPort      - host port variable (can be NULL)
* Output : loginName - login user name
*          password  - password
*          hostName  - host name
*          hostPort  - host port
* Return : TRUE if FTP specifier parsed, FALSE if specifier invalid
* Notes  : -
\***********************************************************************/

bool Storage_parseFTPSpecifier(const String ftpSpecifier,
                               String       loginName,
                               Password     *password,
                               String       hostName,
                               uint         *hostPort
                              );

/***********************************************************************\
* Name   : Storage_parseSSHSpecifier
* Purpose: parse ssh specifier:
*            [<user name>@]<host name>[:<port>]
* Input  : sshSpecifier - ssh specifier string
*          loginName    - login user name variable (can be NULL)
*          hostName     - host name variable (can be NULL)
*          hostPort     - host port number variable (can be NULL)
*          fileName     - file name variable (can be NULL)
* Output : loginName    - login user name (can be NULL)
*          hostName     - host name (can be NULL)
*          hostPort     - host port number (can be NULL)
*          fileName     - file name (can be NULL)
* Return : TRUE if ssh specifier parsed, FALSE if specifier invalid
* Notes  : -
\***********************************************************************/

bool Storage_parseSSHSpecifier(const String sshSpecifier,
                               String       loginName,
                               String       hostName,
                               uint         *hostPort
                              );

/***********************************************************************\
* Name   : Storage_parseDeviceSpecifier
* Purpose: parse device specifier:
*            <device name>:
* Input  : deviceSpecifier   - device specifier string
*          defaultDeviceName - default device name
*          deviceName        - device name variable (can be NULL)
*          fileName          - file name variable (can be NULL)
* Output : deviceName - device name (can be NULL)
*          fileName   - file name (can be NULL)
* Return : TRUE if device specifier parsed, FALSE if specifier invalid
* Notes  : -
\***********************************************************************/

bool Storage_parseDeviceSpecifier(const String deviceSpecifier,
                                  const String defaultDeviceName,
                                  String       deviceName
                                 );

/***********************************************************************\
* Name   : Storage_prepare
* Purpose: prepare storage: read password, init files
* Input  : storageName - storage name:
*          options     - options
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_prepare(const String     storageName,
                       const JobOptions *jobOptions
                      );

/***********************************************************************\
* Name   : Storage_init
* Purpose: init new storage
* Input  : storageFileHandle            - storage file handle variable
*          storageName                  - storage name
*          jobOptions                   - job options
*          storageRequestVolumeFunction - volume request call back
*          storageRequestVolumeUserData - user data for volume request
*                                         call back
* Output : storageFileHandle - initialized storage file handle
*          fileName          - file name (without storage specifier
*                              prefix)
* Return : ERROR_NONE or errorcode
* Notes  : supported storage names:
*            ftp://
*            ssh://
*            scp://
*            sftp://
*            cd://
*            dvd://
*            bd://
*            device://
*            file://
*            plain file name
\***********************************************************************/

Errors Storage_init(StorageFileHandle            *storageFileHandle,
                    const String                 storageName,
                    const JobOptions             *jobOptions,
                    StorageRequestVolumeFunction storageRequestVolumeFunction,
                    void                         *storageRequestVolumeUserData,
                    StorageStatusInfoFunction    storageStatusInfoFunction,
                    void                         *storageStatusInfoUserData,
                    String                       fileName
                   );

/***********************************************************************\
* Name   : Storage_done
* Purpose: deinit storage
* Input  : storageFileHandle - storage file handle variable
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_done(StorageFileHandle *storageFileHandle);

/***********************************************************************\
* Name   : Storage_getHandleName
* Purpose: get storage name from storage handle
* Input  : storageName       - storage name variable
*          storageFileHandle - storage file handle
*          fileName          - file name (can be NULL)
* Output : storageName - storage name
* Return : storage name variable
* Notes  : -
\***********************************************************************/

String Storage_getHandleName(String                  storageName,
                             const StorageFileHandle *storageFileHandle,
                             const String            fileName
                            );

/***********************************************************************\
* Name   : Storage_preProcess
* Purpose: pre-process storage
* Input  : storageFileHandle - storage file handle
*          initialFlag       - TRUE iff initial call, FALSE otherwise
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_preProcess(StorageFileHandle *storageFileHandle,
                          bool              initialFlag
                         );

/***********************************************************************\
* Name   : Storage_postProcess
* Purpose: post-process storage
* Input  : storageFileHandle - storage file handle
*          finalFlag         - TRUE iff final call, FALSE otherwise
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_postProcess(StorageFileHandle *storageFileHandle,
                           bool              finalFlag
                          );

/***********************************************************************\
* Name   : Storage_getVolumeNumber
* Purpose: get current volume number
* Input  : storageFileHandle - storage file handle
* Output : -
* Return : volume number
* Notes  : -
\***********************************************************************/

uint Storage_getVolumeNumber(const StorageFileHandle *storageFileHandle);

/***********************************************************************\
* Name   : Storage_setVolumeNumber
* Purpose: set volume number
* Input  : storageFileHandle - storage file handle
*          volumeNumber      - volume number
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Storage_setVolumeNumber(StorageFileHandle *storageFileHandle,
                             uint              volumeNumber
                            );

/***********************************************************************\
* Name   : Storage_unloadVolume
* Purpose: unload volume
* Input  : storageFileHandle - storage file handle
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_unloadVolume(StorageFileHandle *storageFileHandle);

/***********************************************************************\
* Name   : Storage_create
* Purpose: create new storage file
* Input  : storageFileHandle - storage file handle
*          fileName          - archive file name
*          fileSize          - storage file size
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_create(StorageFileHandle *storageFileHandle,
                      const String      fileName,
                      uint64            fileSize
                     );

/***********************************************************************\
* Name   : Storage_open
* Purpose: open storage file
* Input  : storageFileHandle - storage file handle
*          fileName          - archive file name
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_open(StorageFileHandle *storageFileHandle,
                    const String      fileName
                   );

/***********************************************************************\
* Name   : Storage_close
* Purpose: close storage file
* Input  : storageFileHandle - storage file handle
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Storage_close(StorageFileHandle *storageFileHandle);

/***********************************************************************\
* Name   : Storage_delete
* Purpose: delete storage file
* Input  : storageFileHandle - storage file handle
*          fileName          - archive file name
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_delete(StorageFileHandle *storageFileHandle,
                      const String      fileName
                     );

/***********************************************************************\
* Name   : Storage_eof
* Purpose: check if end-of-file in storage file
* Input  : storageFileHandle - storage file handle
* Output : -
* Return : TRUE if end-of-file, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Storage_eof(StorageFileHandle *storageFileHandle);

/***********************************************************************\
* Name   : Storage_read
* Purpose: read from storage file
* Input  : storageFileHandle - storage file handle
*          buffer            - buffer with data to write
*          size              - data size
* Output : bytesRead - number of bytes read
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_read(StorageFileHandle *storageFileHandle,
                    void              *buffer,
                    ulong             size,
                    ulong             *bytesRead
                   );

/***********************************************************************\
* Name   : Storage_write
* Purpose: write into storage file
* Input  : storageFileHandle - storage file handle
*          buffer            - buffer with data to write
*          size              - data size
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_write(StorageFileHandle *storageFileHandle,
                     const void        *buffer,
                     ulong             size
                    );

/***********************************************************************\
* Name   : Storage_getSize
* Purpose: get storage file size
* Input  : storageFileHandle - storage file handle
* Output : -
* Return : size of storage
* Notes  : -
\***********************************************************************/

uint64 Storage_getSize(StorageFileHandle *storageFileHandle);

/***********************************************************************\
* Name   : Storage_tell
* Purpose: get current position in storage file
* Input  : storageFileHandle - storage file handle
* Output : offset - offset (0..n-1)
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Storage_tell(StorageFileHandle *storageFileHandle,
                    uint64            *offset
                   );

/***********************************************************************\
* Name   : Storage_seek
* Purpose: seek in storage file
* Input  : storageFileHandle - storage file handle
*          offset            - offset (0..n-1)
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Storage_seek(StorageFileHandle *storageFileHandle,
                    uint64            offset
                   );

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : Storage_openDirectoryList
* Purpose: open storage directory list for reading directory entries
* Input  : storageDirectoryListHandle - storage directory list handle
*                                       variable
*          storageName                - storage name
*                                       (prefix+specifier+path only)
*          jobOptions                 - job options
* Output : storageDirectoryListHandle - initialized storage directory
*                                       list handle
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Storage_openDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle,
                                 const String               storageName,
                                 const JobOptions           *jobOptions
                                );

/***********************************************************************\
* Name   : Storage_closeDirectoryList
* Purpose: close storage directory list
* Input  : storageDirectoryListHandle - storage directory list handle
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Storage_closeDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle);

/***********************************************************************\
* Name   : Storage_endOfDirectoryList
* Purpose: check if end of storage directory list reached
* Input  : storageDirectoryListHandle - storage directory list handle
* Output : -
* Return : TRUE if not more diretory entries to read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Storage_endOfDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle);

/***********************************************************************\
* Name   : Storage_readDirectoryList
* Purpose: read next storage directory list entry in storage
* Input  : storageDirectoryListHandle - storage directory list handle
*          fileName                   - file name variable
*          fileInfo                   - file info (can be NULL)
* Output : fileName - next file name (including path)
*          fileInfo - next file info
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Storage_readDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle,
                                 String                     fileName,
                                 FileInfo                   *fileInfo
                                );

#ifdef __cplusplus
  }
#endif

#endif /* __STORAGE__ */

/* end of file */
