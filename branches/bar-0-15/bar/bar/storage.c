/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/storage.c,v $
* $Revision: 1.27 $
* $Author: torsten $
* Contents: storage functions
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#ifdef HAVE_FTP
  #include <ftplib.h>
#endif /* HAVE_FTP */
#ifdef HAVE_SSH2
  #include <libssh2.h>
  #include <libssh2_sftp.h>
#endif /* HAVE_SSH2 */
#include <errno.h>
#include <assert.h>

#include "global.h"
#include "strings.h"
#include "stringlists.h"
#include "files.h"
#include "network.h"
#include "database.h"
#include "errors.h"

#include "bar.h"
#include "network.h"
#include "passwords.h"
#include "misc.h"

#include "crypt.h"
#include "archive.h"

#include "storage.h"

//#include <fcntl.h>

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
#define MAX_BUFFER_SIZE     (64*1024)
#define MAX_FILENAME_LENGTH (8*1024)

#define UNLOAD_VOLUME_DELAY_TIME (10LL*1000LL*1000LL) /* [us] */
#define LOAD_VOLUME_DELAY_TIME   (10LL*1000LL*1000LL) /* [us] */

#define MAX_CD_SIZE  (900LL*1024LL*1024LL)     // 900M
#define MAX_DVD_SIZE (2LL*4613734LL*1024LL)    // 9G (dual layer)
#define MAX_BD_SIZE  (2LL*25LL*1024LL*1024LL)  // 50G (dual layer)

#define CD_VOLUME_SIZE      MAX_CD_SIZE
#define CD_VOLUME_ECC_SIZE  (560LL*1024LL*1024LL)
#define DVD_VOLUME_SIZE     MAX_DVD_SIZE
#define DVD_VOLUME_ECC_SIZE (3600LL*1024LL*1024LL)
#define BD_VOLUME_SIZE      MAX_BD_SIZE
#define BD_VOLUME_ECC_SIZE  (20LL*1024LL*1024LL*1024LL)

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/
#ifdef HAVE_FTP
  LOCAL Password *defaultFTPPassword;
#endif /* HAVE_FTP */
#ifdef HAVE_SSH2
  LOCAL Password *defaultSSHPassword;
#endif /* HAVE_SSH2 */

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : updateStatusInfo
* Purpose: update status info
* Input  : storageStatusInfo - storage status info
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void updateStatusInfo(const StorageFileHandle *storageFileHandle)
{
  assert(storageFileHandle != NULL);

  if (storageFileHandle->storageStatusInfoFunction != NULL)
  {
    storageFileHandle->storageStatusInfoFunction(storageFileHandle->storageStatusInfoUserData,
                                                 &storageFileHandle->runningInfo
                                                );
  }
}

/***********************************************************************\
* Name   : initPassword
* Purpose: init FTP password
* Input  : -
* Output : -
* Return : TRUE if FTP password intialized, FALSE otherwise
* Notes  : -
\***********************************************************************/

#ifdef HAVE_FTP
LOCAL bool initFTPPassword(const JobOptions *jobOptions)
{
  assert(jobOptions != NULL);

  if (jobOptions->ftpServer.password == NULL)
  {
    if (globalOptions.runMode == RUN_MODE_INTERACTIVE)
    {
      return Password_input(defaultFTPPassword,"FTP login password",PASSWORD_INPUT_MODE_ANY);
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    return TRUE;
  }
}
#endif /* HAVE_FTP */

/***********************************************************************\
* Name   : initSSHPassword
* Purpose: init SSH password
* Input  : -
* Output : -
* Return : TRUE if SSH password intialized, FALSE otherwise
* Notes  : -
\***********************************************************************/

#ifdef HAVE_SSH2
LOCAL bool initSSHPassword(const JobOptions *jobOptions)
{
  assert(jobOptions != NULL);

  if (jobOptions->sshServer.password == NULL)
  {
    if (globalOptions.runMode == RUN_MODE_INTERACTIVE)
    {
      return Password_input(defaultSSHPassword,"SSH login password",PASSWORD_INPUT_MODE_ANY);
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    return TRUE;
  }
}
#endif /* HAVE_SSH2 */

#ifdef HAVE_FTP
/***********************************************************************\
* Name   : checkFTPLogin
* Purpose: check if FTP login is possible
* Input  : loginName - login name
*          password  - login password
*          hostName  - host name
* Output : -
* Return : ERROR_NONE if login is possible, error code otherwise
* Notes  : -
\***********************************************************************/

LOCAL Errors checkFTPLogin(const String loginName,
                           Password     *password,
                           const String hostName
                          )
{
  netbuf     *ftpControl;
  const char *plainPassword;

  /* check host name (Note: FTP library crash if host name is not valid!) */
  if (!Network_hostExists(hostName))
  {
    return ERROR_HOST_NOT_FOUND;
  }

  /* connect */
  if (FtpConnect(String_cString(hostName),&ftpControl) != 1)
  {
    return ERROR_FTP_SESSION_FAIL;
  }

  /* login */
  plainPassword = Password_deploy(password);
  if (FtpLogin(String_cString(loginName),
               plainPassword,
               ftpControl
              ) != 1
     )
  {
    Password_undeploy(password);
    FtpQuit(ftpControl);
    return ERROR_FTP_AUTHENTIFICATION;
  }
  Password_undeploy(password);
  FtpQuit(ftpControl);

  return ERROR_NONE;
}
#endif /* HAVE_FTP */

#ifdef HAVE_SSH2
/***********************************************************************\
* Name   : checkSSHLogin
* Purpose: check if SSH login is possible
* Input  : loginName          - login name
*          password           - login password
*          hostName           - host name
*          hostPort           - host SSH port
*          publicKeyFileName  - SSH public key file name
*          privateKeyFileName - SSH private key file name
* Output : -
* Return : ERROR_NONE if login is possible, error code otherwise
* Notes  : -
\***********************************************************************/

LOCAL Errors checkSSHLogin(const String loginName,
                           Password     *password,
                           const String hostName,
                           uint         hostPort,
                           const String publicKeyFileName,
                           const String privateKeyFileName
                          )
{
  SocketHandle socketHandle;
  Errors       error;

  error = Network_connect(&socketHandle,
                          SOCKET_TYPE_SSH,
                          hostName,
                          hostPort,
                          loginName,
                          password,
                          publicKeyFileName,
                          privateKeyFileName,
                          0
                         );
  if (error != ERROR_NONE)
  {
    return error;
  }
  Network_disconnect(&socketHandle);

  return ERROR_NONE;
}
#endif /* HAVE_SSH2 */

#ifdef HAVE_SSH2
/***********************************************************************\
* Name   : waitSessionSocket
* Purpose: wait a little until session socket can be read/write
* Input  : socketHandle - socket handle
* Output : -
* Return : TRUE if session socket can be read/write, FALSE on
+          error/timeout
* Notes  : -
\***********************************************************************/

LOCAL bool waitSessionSocket(SocketHandle *socketHandle)
{
  LIBSSH2_SESSION *session;
  struct timeval  tv;
  fd_set          fdSet;

  assert(socketHandle != NULL);

  // get session
  session = Network_getSSHSession(socketHandle);
  assert(session != NULL);

  // wait for max. 60s
  tv.tv_sec  = 60;
  tv.tv_usec = 0;
  FD_ZERO(&fdSet);
  FD_SET(socketHandle->handle,&fdSet);
  return (select(socketHandle->handle+1,
                 ((libssh2_session_block_directions(session) & LIBSSH2_SESSION_BLOCK_INBOUND ) != 0) ? &fdSet : NULL,
                 ((libssh2_session_block_directions(session) & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0) ? &fdSet : NULL,
                 NULL,
                 &tv
                ) > 0
         );
}
#endif /* HAVE_SSH2 */


/***********************************************************************\
* Name   : limitBandWidth
* Purpose: limit used band width
* Input  : storageBandWidth - storage band width
*          transmittedBytes - transmitted bytes
*          transmissionTime - time for transmission
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

#if defined(HAVE_FTP) || defined(HAVE_SSH2)
LOCAL void limitBandWidth(StorageBandWidth *storageBandWidth,
                          ulong            transmittedBytes,
                          uint64           transmissionTime
                         )
{
  uint   z;
  ulong  averageBandWidth;
  uint64 delayTime;

  assert(storageBandWidth != NULL);

  if (storageBandWidth->max > 0)
  {
    storageBandWidth->measurementBytes += transmittedBytes;
    storageBandWidth->measurementTime += transmissionTime;
/*
fprintf(stderr,"%s,%d: storageBandWidth->blockSize=%ld sum=%lu %llu\n",__FILE__,__LINE__,
storageBandWidth->blockSize,
storageBandWidth->measurementBytes,
storageBandWidth->measurementTime
);
*/

    if ((ulong)(storageBandWidth->measurementTime/1000LL) > 100L)   // to small time values are not reliable, thus accumlate time
    {
      /* calculate average band width */
      averageBandWidth = 0;
      for (z = 0; z < MAX_BAND_WIDTH_MEASUREMENTS; z++)
      {
        averageBandWidth += storageBandWidth->measurements[z];
      }
      averageBandWidth /= MAX_BAND_WIDTH_MEASUREMENTS;
//fprintf(stderr,"%s,%d: averageBandWidth=%lu storageBandWidth->max=%lu deleta=%llu\n",__FILE__,__LINE__,averageBandWidth,storageBandWidth->max,storageBandWidth->measurementTime);

      /* delay if needed, recalculate optimal band width block size */
      if      (averageBandWidth > (storageBandWidth->max+1000*8))
      {
//fprintf(stderr,"%s,%d: -- averageBandWidth=%lu storageBandWidth->max=%lu deleta=%llu\n",__FILE__,__LINE__,averageBandWidth,storageBandWidth->max,storageBandWidth->measurementTime);
        delayTime = ((uint64)(averageBandWidth-storageBandWidth->max)*1000000LL)/averageBandWidth;
//fprintf(stderr,"%s,%d: %llu %llu\n",__FILE__,__LINE__,(storageBandWidth->measurementBytes*8LL*1000000LL)/storageBandWidth->max,storageBandWidth->measurementTime);
        delayTime = (storageBandWidth->measurementBytes*8LL*1000000LL)/storageBandWidth->max-storageBandWidth->measurementTime;
//        if (storageBandWidth->blockSize > 1024) storageBandWidth->blockSize -= 1024;
      }
      else if (averageBandWidth < (storageBandWidth->max-1000*8))
      {
        delayTime = 0LL;
  fprintf(stderr,"%s,%d: ++ averageBandWidth=%lu storageBandWidth->max=%lu deleta=%llu\n",__FILE__,__LINE__,averageBandWidth,storageBandWidth->max,storageBandWidth->measurementTime);
//        storageBandWidth->blockSize += 1024;
      }
else {
        delayTime = 0LL;
//fprintf(stderr,"%s,%d: == averageBandWidth=%lu storageBandWidth->max=%lu deleta=%llu\n",__FILE__,__LINE__,averageBandWidth,storageBandWidth->max,storageBandWidth->measurementTime);
}
      if (delayTime > 0) Misc_udelay(delayTime);

      /* calculate bandwidth */
      storageBandWidth->measurements[storageBandWidth->measurementNextIndex] = (ulong)(((uint64)storageBandWidth->measurementBytes*8LL*1000000LL)/(storageBandWidth->measurementTime+delayTime));
//fprintf(stderr,"%s,%d: %lu\n",__FILE__,__LINE__,storageBandWidth->measurements[storageBandWidth->measurementNextIndex]);
      storageBandWidth->measurementNextIndex = (storageBandWidth->measurementNextIndex+1)%MAX_BAND_WIDTH_MEASUREMENTS;

      storageBandWidth->measurementBytes = 0;
      storageBandWidth->measurementTime  = 0;
    }
  }
}
#endif /* defined(HAVE_FTP) || defined(HAVE_SSH2) */

/***********************************************************************\
* Name   : requestNewMedium
* Purpose: request new CD/DVD/BD medium
* Input  : storageFileHandle - storage file handle
*          waitFlag          - TRUE to wait for new medium
* Output : -
* Return : TRUE if new medium loaded, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL Errors requestNewMedium(StorageFileHandle *storageFileHandle, bool waitFlag)
{
  Errors                error;
  TextMacro             textMacros[2];
  bool                  mediumRequestedFlag;
  StorageRequestResults storageRequestResult;

  error = ERROR_UNKNOWN;

  TEXT_MACRO_N_STRING (textMacros[0],"%device",storageFileHandle->opticalDisk.name     );
  TEXT_MACRO_N_INTEGER(textMacros[1],"%number",storageFileHandle->requestedVolumeNumber);

  if (   (storageFileHandle->volumeState == STORAGE_VOLUME_STATE_UNKNOWN)
      || (storageFileHandle->volumeState == STORAGE_VOLUME_STATE_LOADED)
     )
  {
    /* sleep a short time to give hardware time for finishing volume, then unload current volume */
    printInfo(0,"Unload medium #%d...",storageFileHandle->volumeNumber);
    Misc_udelay(UNLOAD_VOLUME_DELAY_TIME);
    Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.unloadVolumeCommand),
                        textMacros,SIZE_OF_ARRAY(textMacros),
                        NULL,
                        NULL,
                        NULL
                       );
    printInfo(0,"ok\n");

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_UNLOADED;
  }

  /* request new medium */
  mediumRequestedFlag  = FALSE;
  storageRequestResult = STORAGE_REQUEST_VOLUME_UNKNOWN;
  if      (storageFileHandle->requestVolumeFunction != NULL)
  {
    mediumRequestedFlag = TRUE;

    /* request new medium via call back, unload if requested */
    do
    {
      storageRequestResult = storageFileHandle->requestVolumeFunction(storageFileHandle->requestVolumeUserData,
                                                                      storageFileHandle->requestedVolumeNumber
                                                                     );
      if (storageRequestResult == STORAGE_REQUEST_VOLUME_UNLOAD)
      {
        /* sleep a short time to give hardware time for finishing volume, then unload current medium */
        printInfo(0,"Unload medium...");
        Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.unloadVolumeCommand),
                            textMacros,SIZE_OF_ARRAY(textMacros),
                            NULL,
                            NULL,
                            NULL
                           );
        printInfo(0,"ok\n");
      }
    }
    while (storageRequestResult == STORAGE_REQUEST_VOLUME_UNLOAD);

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_WAIT;
  }
  else if (storageFileHandle->opticalDisk.requestVolumeCommand != NULL)
  {
    mediumRequestedFlag = TRUE;

    /* request new volume via external command */
    printInfo(0,"Request new medium #%d...",storageFileHandle->requestedVolumeNumber);
    if (Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.requestVolumeCommand),
                            textMacros,SIZE_OF_ARRAY(textMacros),
                            NULL,
                            NULL,
                            NULL
                           ) == ERROR_NONE
       )
    {
      printInfo(0,"ok\n");
      storageRequestResult = STORAGE_REQUEST_VOLUME_OK;
    }
    else
    {
      printInfo(0,"FAIL\n");
      storageRequestResult = STORAGE_REQUEST_VOLUME_FAIL;
    }

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_WAIT;
  }
  else
  {
    if (storageFileHandle->volumeState == STORAGE_VOLUME_STATE_UNLOADED)
    {
      printInfo(0,"Please insert medium #%d into drive '%s'\n",storageFileHandle->requestedVolumeNumber,String_cString(storageFileHandle->opticalDisk.name));
    }
    if (waitFlag)
    {
      mediumRequestedFlag = TRUE;

      printInfo(0,"<<press ENTER to continue>>\n");
      Misc_waitEnter();

      storageRequestResult = STORAGE_REQUEST_VOLUME_OK;
    }

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_WAIT;
  }

  if (mediumRequestedFlag)
  {
    switch (storageRequestResult)
    {
      case STORAGE_REQUEST_VOLUME_OK:
        /* load volume, then sleep a short time to give hardware time for reading volume information */
        printInfo(0,"Load medium #%d...",storageFileHandle->requestedVolumeNumber);
        Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.loadVolumeCommand),
                            textMacros,SIZE_OF_ARRAY(textMacros),
                            NULL,
                            NULL,
                            NULL
                           );
        Misc_udelay(LOAD_VOLUME_DELAY_TIME);
        printInfo(0,"ok\n");

        /* store new volume number */
        storageFileHandle->volumeNumber = storageFileHandle->requestedVolumeNumber;

        /* update status info */
        storageFileHandle->runningInfo.volumeNumber = storageFileHandle->volumeNumber;
        updateStatusInfo(storageFileHandle);

        storageFileHandle->volumeState = STORAGE_VOLUME_STATE_LOADED;
        return ERROR_NONE;
        break;
      case STORAGE_REQUEST_VOLUME_ABORTED:
        return ERROR_NONE;
        break;
      default:
        return ERROR_LOAD_VOLUME_FAIL;
        break;
    }
  }
  else
  {
    return ERROR_NONE;
  }
}

/***********************************************************************\
* Name   : requestNewVolume
* Purpose: request new volume
* Input  : storageFileHandle - storage file handle
*          waitFlag          - TRUE to wait for new volume
* Output : -
* Return : TRUE if new volume loaded, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL Errors requestNewVolume(StorageFileHandle *storageFileHandle, bool waitFlag)
{
  Errors                error;
  TextMacro             textMacros[2];
  bool                  volumeRequestedFlag;
  StorageRequestResults storageRequestResult;

  error = ERROR_UNKNOWN;

  TEXT_MACRO_N_STRING (textMacros[0],"%device",storageFileHandle->device.name          );
  TEXT_MACRO_N_INTEGER(textMacros[1],"%number",storageFileHandle->requestedVolumeNumber);

  if (   (storageFileHandle->volumeState == STORAGE_VOLUME_STATE_UNKNOWN)
      || (storageFileHandle->volumeState == STORAGE_VOLUME_STATE_LOADED)
     )
  {
    /* sleep a short time to give hardware time for finishing volume; unload current volume */
    printInfo(0,"Unload volume #%d...",storageFileHandle->volumeNumber);
    Misc_udelay(UNLOAD_VOLUME_DELAY_TIME);
    Misc_executeCommand(String_cString(storageFileHandle->device.unloadVolumeCommand),
                        textMacros,SIZE_OF_ARRAY(textMacros),
                        NULL,
                        NULL,
                        NULL
                       );
    printInfo(0,"ok\n");

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_UNLOADED;
  }

  /* request new volume */
  volumeRequestedFlag  = FALSE;
  storageRequestResult = STORAGE_REQUEST_VOLUME_UNKNOWN;
  if      (storageFileHandle->requestVolumeFunction != NULL)
  {
    volumeRequestedFlag = TRUE;

    /* request new volume via call back, unload if requested */
    do
    {
      storageRequestResult = storageFileHandle->requestVolumeFunction(storageFileHandle->requestVolumeUserData,
                                                                      storageFileHandle->requestedVolumeNumber
                                                                     );
      if (storageRequestResult == STORAGE_REQUEST_VOLUME_UNLOAD)
      {
        /* sleep a short time to give hardware time for finishing volume, then unload current medium */
        printInfo(0,"Unload volume...");
        Misc_udelay(UNLOAD_VOLUME_DELAY_TIME);
        Misc_executeCommand(String_cString(storageFileHandle->device.unloadVolumeCommand),
                            textMacros,SIZE_OF_ARRAY(textMacros),
                            NULL,
                            NULL,
                            NULL
                           );
        printInfo(0,"ok\n");
      }
    }
    while (storageRequestResult == STORAGE_REQUEST_VOLUME_UNLOAD);

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_WAIT;
  }
  else if (storageFileHandle->device.requestVolumeCommand != NULL)
  {
    volumeRequestedFlag = TRUE;

    /* request new volume via external command */
    printInfo(0,"Request new volume #%d...",storageFileHandle->requestedVolumeNumber);
    if (Misc_executeCommand(String_cString(storageFileHandle->device.loadVolumeCommand),
                            textMacros,SIZE_OF_ARRAY(textMacros),
                            NULL,
                            NULL,
                            NULL
                           ) == ERROR_NONE
       )
    {
      printInfo(0,"ok\n");
      storageRequestResult = STORAGE_REQUEST_VOLUME_OK;
    }
    else
    {
      printInfo(0,"FAIL\n");
      storageRequestResult = STORAGE_REQUEST_VOLUME_FAIL;
    }

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_WAIT;
  }
  else
  {
    if (storageFileHandle->volumeState == STORAGE_VOLUME_STATE_UNLOADED)
    {
      printInfo(0,"Please insert volume #%d into drive '%s'\n",storageFileHandle->requestedVolumeNumber,storageFileHandle->device.name);
    }
    if (waitFlag)
    {
      volumeRequestedFlag = TRUE;

      printInfo(0,"<<press ENTER to continue>>\n");
      Misc_waitEnter();

      storageRequestResult = STORAGE_REQUEST_VOLUME_OK;
    }

    storageFileHandle->volumeState = STORAGE_VOLUME_STATE_WAIT;
  }

  if (volumeRequestedFlag)
  {
    switch (storageRequestResult)
    {
      case STORAGE_REQUEST_VOLUME_OK:
        /* load volume; sleep a short time to give hardware time for reading volume information */
        printInfo(0,"Load volume #%d...",storageFileHandle->requestedVolumeNumber);
        Misc_executeCommand(String_cString(storageFileHandle->device.loadVolumeCommand),
                            textMacros,SIZE_OF_ARRAY(textMacros),
                            NULL,
                            NULL,
                            NULL
                           );
        Misc_udelay(LOAD_VOLUME_DELAY_TIME);
        printInfo(0,"ok\n");

        /* store new volume number */
        storageFileHandle->volumeNumber = storageFileHandle->requestedVolumeNumber;

        /* update status info */
        storageFileHandle->runningInfo.volumeNumber = storageFileHandle->volumeNumber;
        updateStatusInfo(storageFileHandle);

        storageFileHandle->volumeState = STORAGE_VOLUME_STATE_LOADED;
        return ERROR_NONE;
        break;
      case STORAGE_REQUEST_VOLUME_ABORTED:
        return ERROR_NONE;
        break;
      default:
        return ERROR_LOAD_VOLUME_FAIL;
        break;
    }
  }
  else
  {
    return ERROR_NONE;
  }
}

/***********************************************************************\
* Name   : processIOmkisofs
* Purpose: process mkisofs output
* Input  : storageFileHandle - storage file handle variable
*          line              - line
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void processIOmkisofs(StorageFileHandle *storageFileHandle,
                            const String      line
                           )
{
  String s;
  double p;

//fprintf(stderr,"%s,%d: line=%s\n",__FILE__,__LINE__,String_cString(line));
  s = String_new();
  if (String_matchCString(line,STRING_BEGIN,".* ([0-9\\.]+)% done.*",NULL,NULL,s,NULL))
  {
//fprintf(stderr,"%s,%d: mkisofs: %s\n",__FILE__,__LINE__,String_cString(line));
    p = String_toDouble(s,0,NULL,NULL,0);
    storageFileHandle->runningInfo.volumeProgress = ((double)storageFileHandle->opticalDisk.step*100.0+p)/(double)(storageFileHandle->opticalDisk.steps*100);
    updateStatusInfo(storageFileHandle);
  }
  String_delete(s);
}

/***********************************************************************\
* Name   : processIOdvdisaster
* Purpose: process dvdisaster output
* Input  : storageFileHandle - storage file handle variable
*          line              - line
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void processIOdvdisaster(StorageFileHandle *storageFileHandle,
                               const String      line
                              )
{
  String s;
  double p;

//fprintf(stderr,"%s,%d: line=%s\n",__FILE__,__LINE__,String_cString(line));
  s = String_new();
  if (String_matchCString(line,STRING_BEGIN,".*adding space\\): +([0-9\\.]+)%",NULL,NULL,s,NULL))
  {
//fprintf(stderr,"%s,%d: dvdisaster1: %s\n",__FILE__,__LINE__,String_cString(line));
    p = String_toDouble(s,0,NULL,NULL,0);
    storageFileHandle->runningInfo.volumeProgress = ((double)(storageFileHandle->opticalDisk.step+0)*100.0+p)/(double)(storageFileHandle->opticalDisk.steps*100);
    updateStatusInfo(storageFileHandle);
  }
  if (String_matchCString(line,STRING_BEGIN,".*generation: +([0-9\\.]+)%",NULL,NULL,s,NULL))
  {
//fprintf(stderr,"%s,%d: dvdisaster2: %s\n",__FILE__,__LINE__,String_cString(line));
    p = String_toDouble(s,0,NULL,NULL,0);
    storageFileHandle->runningInfo.volumeProgress = ((double)(storageFileHandle->opticalDisk.step+1)*100.0+p)/(double)(storageFileHandle->opticalDisk.steps*100);
    updateStatusInfo(storageFileHandle);
  }

  String_delete(s);
}

/***********************************************************************\
* Name   : processIOgrowisofs
* Purpose: process growisofs output
* Input  : storageFileHandle - storage file handle variable
*          line              - line
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void processIOgrowisofs(StorageFileHandle *storageFileHandle,
                              const String      line
                             )
{
  String s;
  double p;

//fprintf(stderr,"%s,%d: line=%s\n",__FILE__,__LINE__,String_cString(line));
  s = String_new();
  if (String_matchCString(line,STRING_BEGIN,".* \\(([0-9\\.]+)%\\) .*",NULL,NULL,s,NULL))
  {
//fprintf(stderr,"%s,%d: growisofs2: %s\n",__FILE__,__LINE__,String_cString(line));
    p = String_toDouble(s,0,NULL,NULL,0);
    storageFileHandle->runningInfo.volumeProgress = ((double)storageFileHandle->opticalDisk.step*100.0+p)/(double)(storageFileHandle->opticalDisk.steps*100);
    updateStatusInfo(storageFileHandle);
  }
  String_delete(s);
}

/*---------------------------------------------------------------------*/

Errors Storage_initAll(void)
{
  #ifdef HAVE_FTP
    FtpInit();
    defaultFTPPassword = Password_new();
  #endif /* HAVE_FTP */
  #ifdef HAVE_SSH2
    defaultSSHPassword = Password_new();
  #endif /* HAVE_SSH2 */

  return ERROR_NONE;
}

void Storage_doneAll(void)
{
  #ifdef HAVE_SSH2
    Password_delete(defaultSSHPassword);
  #endif /* HAVE_SSH2 */
  #ifdef HAVE_FTP
    Password_delete(defaultFTPPassword);
  #endif /* HAVE_FTP */
}

StorageTypes Storage_getType(const String storageName)
{
  return Storage_parseName(storageName,NULL,NULL);
}

StorageTypes Storage_parseName(const String storageName,
                               String       storageSpecifier,
                               String       fileName
                              )
{
  StorageTypes storageType;
  String       string;
  long         nextIndex;

  string = String_new();

  if      (String_startsWithCString(storageName,"ftp://"))
  {
    String_sub(string,storageName,6,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^:]*:[^@]*@[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^@]*@[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_set(storageSpecifier,string);
      if (fileName != NULL) String_clear(fileName);
    }

    storageType = STORAGE_TYPE_FTP;
  }
  else if (String_startsWithCString(storageName,"ssh://"))
  {
    String_sub(string,storageName,6,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^@]*@[^:]*:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^:]*:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_set(storageSpecifier,string);
      if (fileName != NULL) String_clear(fileName);
    }

    storageType = STORAGE_TYPE_SSH;
  }
  else if (String_startsWithCString(storageName,"scp://"))
  {
    String_sub(string,storageName,6,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^@]*@[^:]*:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^:]*:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_set(storageSpecifier,string);
      if (fileName != NULL) String_clear(fileName);
    }

    storageType = STORAGE_TYPE_SCP;
  }
  else if (String_startsWithCString(storageName,"sftp://"))
  {
    String_sub(string,storageName,7,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^@]*@[^:]*:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^:]*:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_set(storageSpecifier,string);
      if (fileName != NULL) String_clear(fileName);
    }

    storageType = STORAGE_TYPE_SFTP;
  }
  else if (String_startsWithCString(storageName,"cd://"))
  {
    String_sub(string,storageName,5,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^:]+:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_clear(storageSpecifier);
      if (fileName != NULL) String_set(fileName,string);
    }

    storageType = STORAGE_TYPE_CD;
  }
  else if (String_startsWithCString(storageName,"dvd://"))
  {
    String_sub(string,storageName,6,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^:]+:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_clear(storageSpecifier);
      if (fileName != NULL) String_set(fileName,string);
    }

    storageType = STORAGE_TYPE_DVD;
  }
  else if (String_startsWithCString(storageName,"bd://"))
  {
    String_sub(string,storageName,5,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^:]+:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_clear(storageSpecifier);
      if (fileName != NULL) String_set(fileName,string);
    }

    storageType = STORAGE_TYPE_BD;
  }
  else if (String_startsWithCString(storageName,"device://"))
  {
    String_sub(string,storageName,9,STRING_END);
    if      (   String_matchCString(string,STRING_BEGIN,"[^:]+:[^/]*/",&nextIndex,NULL,NULL)
             || String_matchCString(string,STRING_BEGIN,"[^/]*/",&nextIndex,NULL,NULL)
            )
    {
      if (storageSpecifier != NULL) String_sub(storageSpecifier,string,0,nextIndex-1);
      if (fileName != NULL) String_sub(fileName,string,nextIndex,STRING_END);
    }
    else
    {
      if (storageSpecifier != NULL) String_clear(storageSpecifier);
      if (fileName != NULL) String_set(fileName,string);
    }

    storageType = STORAGE_TYPE_DEVICE;
  }
  else if (String_startsWithCString(storageName,"file://"))
  {
    if (storageSpecifier != NULL) String_clear(storageSpecifier);
    if (fileName != NULL) String_sub(fileName,storageName,7,STRING_END);

    storageType = STORAGE_TYPE_FILESYSTEM;
  }
  else
  {
    if (storageSpecifier != NULL) String_clear(storageSpecifier);
    if (fileName != NULL) String_set(fileName,storageName);

    storageType = STORAGE_TYPE_FILESYSTEM;
  }

  String_delete(string);

  return storageType;
}

String Storage_getName(String       name,
                       StorageTypes storageType,
                       const String storageSpecifier,
                       const String fileName
                      )
{
  assert(name != NULL);
  assert(storageSpecifier != NULL);

  String_clear(name);
  switch (storageType)
  {
    case STORAGE_TYPE_FILESYSTEM:
      if (!String_empty(fileName))
      {
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_SSH:
      if (!String_empty(fileName))
      {
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_FTP:
      String_appendCString(name,"ftp://");
      String_append(name,storageSpecifier);
      if (!String_empty(fileName))
      {
        String_appendChar(name,'/');
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_SCP:
      String_appendCString(name,"scp://");
      String_append(name,storageSpecifier);
      if (!String_empty(fileName))
      {
        String_appendChar(name,'/');
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_SFTP:
      String_appendCString(name,"sftp://");
      String_append(name,storageSpecifier);
      if (!String_empty(fileName))
      {
        String_appendChar(name,'/');
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_CD:
      String_appendCString(name,"cd://");
      String_append(name,storageSpecifier);
      if (!String_empty(fileName))
      {
        String_appendChar(name,'/');
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_DVD:
      String_appendCString(name,"dvd://");
      String_append(name,storageSpecifier);
      if (!String_empty(fileName))
      {
        String_appendChar(name,'/');
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_BD:
      String_appendCString(name,"bd://");
      String_append(name,storageSpecifier);
      if (!String_empty(fileName))
      {
        String_appendChar(name,'/');
        String_append(name,fileName);
      }
      break;
    case STORAGE_TYPE_DEVICE:
      String_appendCString(name,"device://");
      String_append(name,storageSpecifier);
      if (!String_empty(fileName))
      {
        String_appendChar(name,'/');
        String_append(name,fileName);
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return name;
}

bool Storage_parseFTPSpecifier(const String ftpSpecifier,
                               String       loginName,
                               Password     *password,
                               String       hostName
                              )
{
  bool   result;
  String s;

  assert(ftpSpecifier != NULL);

  String_clear(loginName);
  String_clear(hostName);

  if      (String_matchCString(ftpSpecifier,STRING_BEGIN,"[^:]*:[^@]*@.*",NULL,NULL,NULL))
  {
    s = String_new();
    String_parse(ftpSpecifier,STRING_BEGIN,"%S:%S@",NULL,loginName,s);
    if (password != NULL) Password_setString(password,s);
    String_delete(s);

    result = TRUE;
  }
  else if (String_matchCString(ftpSpecifier,STRING_BEGIN,"[^@]*@.*",NULL,NULL,NULL))
  {
    String_parse(ftpSpecifier,STRING_BEGIN,"%S@",NULL,loginName);

    result = TRUE;
  }
  else if (!String_empty(ftpSpecifier))
  {
    String_set(hostName,ftpSpecifier);

    result = TRUE;
  }
  else
  {
    result = FALSE;
  }

  return result;
}

bool Storage_parseSSHSpecifier(const String sshSpecifier,
                               String       loginName,
                               String       hostName,
                               uint         *hostPort
                              )
{
  bool result;

  assert(sshSpecifier != NULL);

  String_clear(loginName);
  String_clear(hostName);
  if (hostPort != NULL) (*hostPort) = 0;

  if      (String_matchCString(sshSpecifier,STRING_BEGIN,"[^@]*@[^:]*:[:digit:]+",NULL,NULL,NULL))
  {
    result = String_parse(sshSpecifier,STRING_BEGIN,"%S@%S:%d",NULL,loginName,hostName,hostPort);
  }
  else if (String_matchCString(sshSpecifier,STRING_BEGIN,"[^:]*:[:digit:]+",NULL,NULL,NULL))
  {
    result = String_parse(sshSpecifier,STRING_BEGIN,"%S:%d",NULL,hostName,hostPort);
  }
  else if (String_matchCString(sshSpecifier,STRING_BEGIN,"[^@]*@.*",NULL,NULL,NULL))
  {
    result = String_parse(sshSpecifier,STRING_BEGIN,"%S@%S",NULL,loginName,hostName);
  }
  else if (!String_empty(sshSpecifier))
  {
    String_set(hostName,sshSpecifier);

    result = TRUE;
  }
  else
  {
    result = FALSE;
  }

  return result;
}

bool Storage_parseDeviceSpecifier(const String deviceSpecifier,
                                  const String defaultDeviceName,
                                  String       deviceName
                                 )
{
  bool result;

  assert(deviceSpecifier != NULL);

  String_clear(deviceName);

  if (String_matchCString(deviceSpecifier,STRING_BEGIN,"[^:]*:.*",NULL,NULL,NULL))
  {
    result = String_parse(deviceSpecifier,STRING_BEGIN,"%S:%S",NULL,deviceName,NULL);
  }
  else
  {
    if (deviceName != NULL) String_set(deviceName,defaultDeviceName);

    result =TRUE;
  }

  return result;
}

Errors Storage_init(StorageFileHandle            *storageFileHandle,
                    const String                 storageName,
                    const JobOptions             *jobOptions,
                    StorageRequestVolumeFunction storageRequestVolumeFunction,
                    void                         *storageRequestVolumeUserData,
                    StorageStatusInfoFunction    storageStatusInfoFunction,
                    void                         *storageStatusInfoUserData,
                    String                       fileName
                   )
{
  String       storageSpecifier;
  StorageTypes storageType;

  assert(storageFileHandle != NULL);
  assert(storageName != NULL);
  assert(jobOptions != NULL);
  assert(fileName != NULL);

  storageFileHandle->jobOptions                = jobOptions;
  storageFileHandle->requestVolumeFunction     = storageRequestVolumeFunction;
  storageFileHandle->requestVolumeUserData     = storageRequestVolumeUserData;
  storageFileHandle->storageStatusInfoFunction = storageStatusInfoFunction;
  storageFileHandle->storageStatusInfoUserData = storageStatusInfoUserData;
  if (jobOptions->waitFirstVolumeFlag)
  {
    storageFileHandle->volumeNumber          = 0;
    storageFileHandle->requestedVolumeNumber = 0;
    storageFileHandle->volumeState           = STORAGE_VOLUME_STATE_UNKNOWN;
  }
  else
  {
    storageFileHandle->volumeNumber          = 1;
    storageFileHandle->requestedVolumeNumber = 1;
    storageFileHandle->volumeState           = STORAGE_VOLUME_STATE_LOADED;
  }

  storageSpecifier = String_new();
  storageType = Storage_parseName(storageName,storageSpecifier,fileName);
  switch (storageType)
  {
    case STORAGE_TYPE_FILESYSTEM:
      /* init variables */
      storageFileHandle->type = STORAGE_TYPE_FILESYSTEM;
//      String_set(fileName,storageSpecifier);

      /* check if file can be created */
      break;
    case STORAGE_TYPE_SSH:
      String_delete(storageSpecifier);
      return ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        {
          uint      z;
          Errors    error;
          FTPServer ftpServer;

          /* init variables */
          storageFileHandle->type                    = STORAGE_TYPE_FTP;
          storageFileHandle->ftp.hostName            = String_new();
          storageFileHandle->ftp.loginName           = String_new();
          storageFileHandle->ftp.password            = Password_new();
          storageFileHandle->ftp.bandWidth.max       = globalOptions.maxBandWidth;
          storageFileHandle->ftp.bandWidth.blockSize = 64*1024;
          for (z = 0; z < MAX_BAND_WIDTH_MEASUREMENTS; z++)
          {
            storageFileHandle->ftp.bandWidth.measurements[z] = globalOptions.maxBandWidth;
          }
          storageFileHandle->ftp.bandWidth.measurementNextIndex = 0;
          storageFileHandle->ftp.bandWidth.measurementBytes     = 0;
          storageFileHandle->ftp.bandWidth.measurementTime      = 0;

          /* parse storage specifier string */
          if (!Storage_parseFTPSpecifier(storageSpecifier,
                                         storageFileHandle->ftp.loginName,
                                         storageFileHandle->ftp.password,
                                         storageFileHandle->ftp.hostName
                                        )
             )
          {
            Password_delete(storageFileHandle->ftp.password);
            String_delete(storageFileHandle->ftp.loginName);
            String_delete(storageFileHandle->ftp.hostName);
            String_delete(storageSpecifier);
            return ERROR_INVALID_FTP_SPECIFIER;
          }

          /* get FTP server settings */
          getFTPServerSettings(storageFileHandle->ftp.hostName,jobOptions,&ftpServer);
          if (String_empty(storageFileHandle->ftp.loginName)) String_set(storageFileHandle->ftp.loginName,ftpServer.loginName);

          /* check FTP login, get correct password */
          error = ERROR_UNKNOWN;
          if ((error != ERROR_NONE) && !Password_empty(storageFileHandle->ftp.password))
          {
            error = checkFTPLogin(storageFileHandle->ftp.loginName,
                                  storageFileHandle->ftp.password,
                                  storageFileHandle->ftp.hostName
                                 );
          }
          if ((error != ERROR_NONE) && (ftpServer.password != NULL))
          {
            error = checkFTPLogin(storageFileHandle->ftp.loginName,
                                  ftpServer.password,
                                  storageFileHandle->ftp.hostName
                                 );
            if (error == ERROR_NONE)
            {
              Password_set(storageFileHandle->ftp.password,ftpServer.password);
            }
          }
          if ((error != ERROR_NONE) && !Password_empty(defaultFTPPassword))
          {
            error = checkFTPLogin(storageFileHandle->ftp.loginName,
                                  defaultFTPPassword,
                                  storageFileHandle->ftp.hostName
                                 );
            if (error == ERROR_NONE)
            {
              Password_set(storageFileHandle->ftp.password,defaultFTPPassword);
            }
          }
          if (error != ERROR_NONE)
          {
            /* initialize default password */
            if (initFTPPassword(jobOptions))
            {
              error = checkFTPLogin(storageFileHandle->ftp.loginName,
                                    defaultFTPPassword,
                                    storageFileHandle->ftp.hostName
                                   );
              if (error == ERROR_NONE)
              {
                Password_set(storageFileHandle->ftp.password,defaultFTPPassword);
              }
            }
            else
            {
              error = (ftpServer.password != NULL)?ERROR_INVALID_FTP_PASSWORD:ERROR_NO_FTP_PASSWORD;
            }
          }
          if (error != ERROR_NONE)
          {
            Password_delete(storageFileHandle->ftp.password);
            String_delete(storageFileHandle->ftp.loginName);
            String_delete(storageFileHandle->ftp.hostName);
            String_delete(storageSpecifier);
            return error;
          }

          /* free resources */
        }
      #else /* not HAVE_FTP */
        String_delete(storageSpecifier);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        {
          uint      z;
          Errors    error;
          SSHServer sshServer;

          /* init variables */
          storageFileHandle->type                      = STORAGE_TYPE_SCP;
          storageFileHandle->scp.hostName              = String_new();
          storageFileHandle->scp.hostPort              = 0;
          storageFileHandle->scp.loginName             = String_new();
          storageFileHandle->scp.password              = Password_new();
          storageFileHandle->scp.sshPublicKeyFileName  = NULL;
          storageFileHandle->scp.sshPrivateKeyFileName = NULL;
          storageFileHandle->scp.bandWidth.max         = globalOptions.maxBandWidth;
          storageFileHandle->scp.bandWidth.blockSize   = 64*1024;
          for (z = 0; z < MAX_BAND_WIDTH_MEASUREMENTS; z++)
          {
            storageFileHandle->scp.bandWidth.measurements[z] = globalOptions.maxBandWidth;
          }
          storageFileHandle->scp.bandWidth.measurementNextIndex = 0;
          storageFileHandle->scp.bandWidth.measurementBytes     = 0;
          storageFileHandle->scp.bandWidth.measurementTime      = 0;

          /* allocate read-ahead buffer */
          storageFileHandle->scp.readAheadBuffer.data = (byte*)malloc(MAX_BUFFER_SIZE);
          if (storageFileHandle->scp.readAheadBuffer.data == NULL)
          {
            HALT_INSUFFICIENT_MEMORY();
          }

          /* parse storage specifier string */
          if (!Storage_parseSSHSpecifier(storageSpecifier,
                                         storageFileHandle->scp.loginName,
                                         storageFileHandle->scp.hostName,
                                         &storageFileHandle->scp.hostPort
                                        )
             )
          {
            Password_delete(storageFileHandle->scp.password);
            String_delete(storageFileHandle->scp.loginName);
            String_delete(storageFileHandle->scp.hostName);
            String_delete(storageSpecifier);
            return ERROR_INVALID_SSH_SPECIFIER;
          }

          /* get SSH server settings */
          getSSHServerSettings(storageFileHandle->scp.hostName,jobOptions,&sshServer);
          if (String_empty(storageFileHandle->scp.loginName)) String_set(storageFileHandle->scp.loginName,sshServer.loginName);
          if (storageFileHandle->scp.hostPort == 0) storageFileHandle->scp.hostPort = sshServer.port;
          storageFileHandle->scp.sshPublicKeyFileName  = sshServer.publicKeyFileName;
          storageFileHandle->scp.sshPrivateKeyFileName = sshServer.privateKeyFileName;

          /* check if SSH login is possible */
          error = ERROR_UNKNOWN;
          if ((error == ERROR_UNKNOWN) && (sshServer.password != NULL))
          {
            error = checkSSHLogin(storageFileHandle->scp.loginName,
                                  sshServer.password,
                                  storageFileHandle->scp.hostName,
                                  storageFileHandle->scp.hostPort,
                                  storageFileHandle->scp.sshPublicKeyFileName,
                                  storageFileHandle->scp.sshPrivateKeyFileName
                                 );
            if (error == ERROR_NONE)
            {
              Password_set(storageFileHandle->scp.password,sshServer.password);
            }
          }
          if (error == ERROR_UNKNOWN)
          {
            /* initialize default password */
            if (initSSHPassword(jobOptions))
            {
              error = checkSSHLogin(storageFileHandle->scp.loginName,
                                    defaultSSHPassword,
                                    storageFileHandle->scp.hostName,
                                    storageFileHandle->scp.hostPort,
                                    storageFileHandle->scp.sshPublicKeyFileName,
                                    storageFileHandle->scp.sshPrivateKeyFileName
                                   );
              if (error == ERROR_NONE)
              {
                Password_set(storageFileHandle->scp.password,defaultSSHPassword);
              }
            }
            else
            {
              error = ERROR_NO_SSH_PASSWORD;
            }
          }
          if (error != ERROR_NONE)
          {
            Password_delete(storageFileHandle->scp.password);
            String_delete(storageFileHandle->scp.loginName);
            String_delete(storageFileHandle->scp.hostName);
            String_delete(storageSpecifier);
            return error;
          }

          /* free resources */
        }
      #else /* not HAVE_SSH2 */
        String_delete(storageSpecifier);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        {
          uint      z;
          Errors    error;
          SSHServer sshServer;

          /* init variables */
          storageFileHandle->type                       = STORAGE_TYPE_SFTP;
          storageFileHandle->sftp.hostName              = String_new();
          storageFileHandle->sftp.hostPort              = 0;
          storageFileHandle->sftp.loginName             = String_new();
          storageFileHandle->sftp.password              = Password_new();
          storageFileHandle->sftp.sshPublicKeyFileName  = NULL;
          storageFileHandle->sftp.sshPrivateKeyFileName = NULL;
          storageFileHandle->sftp.bandWidth.max         = globalOptions.maxBandWidth;
          storageFileHandle->sftp.bandWidth.blockSize   = 64*1024;
          for (z = 0; z < MAX_BAND_WIDTH_MEASUREMENTS; z++)
          {
            storageFileHandle->sftp.bandWidth.measurements[z] = globalOptions.maxBandWidth;
          }
          storageFileHandle->sftp.bandWidth.measurementNextIndex = 0;
          storageFileHandle->sftp.bandWidth.measurementBytes     = 0;
          storageFileHandle->sftp.bandWidth.measurementTime      = 0;

          /* allocate read-ahead buffer */
          storageFileHandle->sftp.readAheadBuffer.data = (byte*)malloc(MAX_BUFFER_SIZE);
          if (storageFileHandle->sftp.readAheadBuffer.data == NULL)
          {
            HALT_INSUFFICIENT_MEMORY();
          }

          /* parse storage specifier string */
          if (!Storage_parseSSHSpecifier(storageSpecifier,
                                         storageFileHandle->sftp.loginName,
                                         storageFileHandle->sftp.hostName,
                                         &storageFileHandle->sftp.hostPort
                                        )
             )
          {
            Password_delete(storageFileHandle->sftp.password);
            String_delete(storageFileHandle->sftp.loginName);
            String_delete(storageFileHandle->sftp.hostName);
            String_delete(storageSpecifier);
            return ERROR_INVALID_SSH_SPECIFIER;
          }

          /* get SSH server settings */
          getSSHServerSettings(storageFileHandle->sftp.hostName,jobOptions,&sshServer);
          if (String_empty(storageFileHandle->sftp.loginName)) String_set(storageFileHandle->sftp.loginName,sshServer.loginName);
          if (storageFileHandle->sftp.hostPort == 0) storageFileHandle->sftp.hostPort = sshServer.port;
          storageFileHandle->sftp.sshPublicKeyFileName  = sshServer.publicKeyFileName;
          storageFileHandle->sftp.sshPrivateKeyFileName = sshServer.privateKeyFileName;

          /* check if SSH login is possible */
          error = ERROR_UNKNOWN;
          if ((error != ERROR_NONE) && (sshServer.password != NULL))
          {
            error = checkSSHLogin(storageFileHandle->sftp.loginName,
                                  sshServer.password,
                                  storageFileHandle->sftp.hostName,
                                  storageFileHandle->sftp.hostPort,
                                  storageFileHandle->sftp.sshPublicKeyFileName,
                                  storageFileHandle->sftp.sshPrivateKeyFileName
                                 );
            if (error == ERROR_NONE)
            {
              Password_set(storageFileHandle->sftp.password,sshServer.password);
            }
          }
          if (error != ERROR_NONE)
          {
            /* initialize default password */
            if (initSSHPassword(jobOptions))
            {
              error = checkSSHLogin(storageFileHandle->sftp.loginName,
                                    defaultSSHPassword,
                                    storageFileHandle->sftp.hostName,
                                    storageFileHandle->sftp.hostPort,
                                    storageFileHandle->sftp.sshPublicKeyFileName,
                                    storageFileHandle->sftp.sshPrivateKeyFileName
                                   );
              if (error == ERROR_NONE)
              {
                Password_set(storageFileHandle->sftp.password,defaultSSHPassword);
              }
            }
          }
          if (error != ERROR_NONE)
          {
            Password_delete(storageFileHandle->sftp.password);
            String_delete(storageFileHandle->sftp.loginName);
            String_delete(storageFileHandle->sftp.hostName);
            String_delete(storageSpecifier);
            return error;
          }

          /* free resources */
        }
      #else /* not HAVE_SSH2 */
        String_delete(storageSpecifier);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      {
        String         deviceName;
        OpticalDisk    opticalDisk;
        uint64         volumeSize,maxMediumSize;
        FileSystemInfo fileSystemInfo;
        Errors         error;
        String         sourceFileName,fileBaseName,destinationFileName;

        /* parse storage specifier string */
        deviceName = String_new();
        if (!Storage_parseDeviceSpecifier(storageSpecifier,
                                          jobOptions->deviceName,
                                          deviceName
                                         )
           )
        {
          String_delete(deviceName);
          String_delete(storageSpecifier);
          return ERROR_INVALID_DEVICE_SPECIFIER;
        }
        if (String_empty(deviceName)) String_set(deviceName,globalOptions.defaultDeviceName);

        /* get DVD settings */
        switch (storageType)
        {
          case STORAGE_TYPE_CD:
            getCDSettings(jobOptions,&opticalDisk);
            break;
          case STORAGE_TYPE_DVD:
            getDVDSettings(jobOptions,&opticalDisk);
            break;
          case STORAGE_TYPE_BD:
            getBDSettings(jobOptions,&opticalDisk);
            break;
          default:
            #ifndef NDEBUG
              HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
            #endif /* NDEBUG */
            break;
        }

        /* check space in temporary directory: should be enough to hold
           raw CD/DVD/BD data (volumeSize) and CD/DVD image (4G, single layer)
           including error correction codes (2x)
        */
        error = File_getFileSystemInfo(tmpDirectory,&fileSystemInfo);
        if (error != ERROR_NONE)
        {
          String_delete(deviceName);
          String_delete(storageSpecifier);
          return error;
        }
        volumeSize    = 0LL;
        maxMediumSize = 0LL;
        switch (storageType)
        {
          case STORAGE_TYPE_CD:
            volumeSize = (jobOptions->volumeSize > 0LL)
                          ?jobOptions->volumeSize
                          :(jobOptions->errorCorrectionCodesFlag
                            ?CD_VOLUME_ECC_SIZE
                            :CD_VOLUME_SIZE
                           );
            maxMediumSize = MAX_CD_SIZE;
            break;
          case STORAGE_TYPE_DVD:
            volumeSize = (jobOptions->volumeSize > 0LL)
                          ?jobOptions->volumeSize
                          :(jobOptions->errorCorrectionCodesFlag
                            ?DVD_VOLUME_ECC_SIZE
                            :DVD_VOLUME_SIZE
                           );
            maxMediumSize = MAX_DVD_SIZE;
            break;
          case STORAGE_TYPE_BD:
            volumeSize = (jobOptions->volumeSize > 0LL)
                          ?jobOptions->volumeSize
                          :(jobOptions->errorCorrectionCodesFlag
                            ?BD_VOLUME_ECC_SIZE
                            :BD_VOLUME_SIZE
                           );
            maxMediumSize = MAX_BD_SIZE;
            break;
          default:
            #ifndef NDEBUG
              HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
            #endif /* NDEBUG */
            break;
        }
        if (fileSystemInfo.freeBytes < (volumeSize+maxMediumSize*(jobOptions->errorCorrectionCodesFlag?2:1)))
        {
          printWarning("Insufficient space in temporary directory '%s' for medium (%.1f%s free, %.1f%s recommended)!\n",
                       String_cString(tmpDirectory),
                       BYTES_SHORT(fileSystemInfo.freeBytes),BYTES_UNIT(fileSystemInfo.freeBytes),
                       BYTES_SHORT((volumeSize+maxMediumSize*(jobOptions->errorCorrectionCodesFlag?2:1))),BYTES_UNIT((volumeSize+maxMediumSize*(jobOptions->errorCorrectionCodesFlag?2:1)))
                      );
        }

        /* init variables */
        storageFileHandle->type                               = storageType;
        storageFileHandle->opticalDisk.name                   = deviceName;
        storageFileHandle->opticalDisk.requestVolumeCommand   = opticalDisk.requestVolumeCommand;
        storageFileHandle->opticalDisk.unloadVolumeCommand    = opticalDisk.unloadVolumeCommand;
        storageFileHandle->opticalDisk.loadVolumeCommand      = opticalDisk.loadVolumeCommand;
        storageFileHandle->opticalDisk.volumeSize             = volumeSize;
        storageFileHandle->opticalDisk.imagePreProcessCommand = opticalDisk.imagePreProcessCommand;
        storageFileHandle->opticalDisk.imagePostProcessCommand= opticalDisk.imagePostProcessCommand;
        storageFileHandle->opticalDisk.imageCommand           = opticalDisk.imageCommand;
        storageFileHandle->opticalDisk.eccPreProcessCommand   = opticalDisk.eccPreProcessCommand;
        storageFileHandle->opticalDisk.eccPostProcessCommand  = opticalDisk.eccPostProcessCommand;
        storageFileHandle->opticalDisk.eccCommand             = opticalDisk.eccCommand;
        storageFileHandle->opticalDisk.writePreProcessCommand = opticalDisk.writePreProcessCommand;
        storageFileHandle->opticalDisk.writePostProcessCommand= opticalDisk.writePostProcessCommand;
        storageFileHandle->opticalDisk.writeCommand           = opticalDisk.writeCommand;
        storageFileHandle->opticalDisk.writeImageCommand      = opticalDisk.writeImageCommand;
        storageFileHandle->opticalDisk.steps                  = jobOptions->errorCorrectionCodesFlag?4:1; 
        storageFileHandle->opticalDisk.directory              = String_new();                             
        storageFileHandle->opticalDisk.step                   = 0;                                        
        if (jobOptions->waitFirstVolumeFlag)
        {
          storageFileHandle->opticalDisk.number  = 0;
          storageFileHandle->opticalDisk.newFlag = TRUE;
        }
        else
        {
          storageFileHandle->opticalDisk.number  = 1;
          storageFileHandle->opticalDisk.newFlag = FALSE;
        }
        StringList_init(&storageFileHandle->opticalDisk.fileNameList);
        storageFileHandle->opticalDisk.fileName     = String_new();
        storageFileHandle->opticalDisk.totalSize    = 0LL;

        /* create temporary directory for medium files */
        error = File_getTmpDirectoryName(storageFileHandle->opticalDisk.directory,NULL,tmpDirectory);
        if (error != ERROR_NONE)
        {
          String_delete(storageFileHandle->opticalDisk.fileName);
          String_delete(storageFileHandle->opticalDisk.directory);
          String_delete(deviceName);
          String_delete(storageSpecifier);
          return error;
        }

        if (!jobOptions->noBAROnMediumFlag)
        {
          /* store a copy of BAR executable on medium (ignore errors) */
          sourceFileName = String_newCString(globalOptions.barExecutable);
          fileBaseName = File_getFileBaseName(String_new(),sourceFileName);
          destinationFileName = File_appendFileName(String_duplicate(storageFileHandle->opticalDisk.directory),fileBaseName);
          File_copy(sourceFileName,destinationFileName);
          StringList_append(&storageFileHandle->opticalDisk.fileNameList,destinationFileName);
          String_delete(destinationFileName);
          String_delete(fileBaseName);
          String_delete(sourceFileName);
        }

        /* request first medium */
        storageFileHandle->requestedVolumeNumber = 1;
      }
      break;
    case STORAGE_TYPE_DEVICE:
      {
        String         deviceName;
        Device         device;
        FileSystemInfo fileSystemInfo;
        Errors         error;

        /* parse storage specifier string */
        deviceName = String_new();
        if (!Storage_parseDeviceSpecifier(storageSpecifier,
                                          jobOptions->deviceName,
                                          deviceName
                                         )
           )
        {
          String_delete(deviceName);
          String_delete(storageSpecifier);
          return ERROR_INVALID_DEVICE_SPECIFIER;
        }

        /* get device settings */
        getDeviceSettings(deviceName,jobOptions,&device);

        /* check space in temporary directory: 2x volumeSize */
        error = File_getFileSystemInfo(tmpDirectory,&fileSystemInfo);
        if (error != ERROR_NONE)
        {
          String_delete(deviceName);
          String_delete(storageSpecifier);
          return error;
        }
        if (fileSystemInfo.freeBytes < (device.volumeSize*2))
        {
          printWarning("Insufficient space in temporary directory '%s' (%.1f%s free, %.1f%s recommended)!\n",
                       String_cString(tmpDirectory),
                       BYTES_SHORT(fileSystemInfo.freeBytes),BYTES_UNIT(fileSystemInfo.freeBytes),
                       BYTES_SHORT(device.volumeSize*2),BYTES_UNIT(device.volumeSize*2)
                      );
        }

        /* init variables */
        storageFileHandle->type                          = STORAGE_TYPE_DEVICE;
        storageFileHandle->device.name                   = deviceName;
        storageFileHandle->device.requestVolumeCommand   = device.requestVolumeCommand;
        storageFileHandle->device.unloadVolumeCommand    = device.unloadVolumeCommand;
        storageFileHandle->device.loadVolumeCommand      = device.loadVolumeCommand;
        storageFileHandle->device.volumeSize             = device.volumeSize;
        storageFileHandle->device.imagePreProcessCommand = device.imagePreProcessCommand;
        storageFileHandle->device.imagePostProcessCommand= device.imagePostProcessCommand;
        storageFileHandle->device.imageCommand           = device.imageCommand;
        storageFileHandle->device.eccPreProcessCommand   = device.eccPreProcessCommand;
        storageFileHandle->device.eccPostProcessCommand  = device.eccPostProcessCommand;
        storageFileHandle->device.eccCommand             = device.eccCommand;
        storageFileHandle->device.writePreProcessCommand = device.writePreProcessCommand;
        storageFileHandle->device.writePostProcessCommand= device.writePostProcessCommand;
        storageFileHandle->device.writeCommand           = device.writeCommand;
        storageFileHandle->device.directory              = String_new();
        if (jobOptions->waitFirstVolumeFlag)
        {
          storageFileHandle->device.number  = 0;
          storageFileHandle->device.newFlag = TRUE;
        }
        else
        {
          storageFileHandle->device.number  = 1;
          storageFileHandle->device.newFlag = FALSE;
        }
        StringList_init(&storageFileHandle->device.fileNameList);
        storageFileHandle->device.fileName  = String_new();
        storageFileHandle->device.totalSize = 0LL;

        /* create temporary directory for device files */
        error = File_getTmpDirectoryName(storageFileHandle->device.directory,NULL,tmpDirectory);
        if (error != ERROR_NONE)
        {
          String_delete(storageFileHandle->device.fileName);
          String_delete(storageFileHandle->device.directory);
          String_delete(deviceName);
          String_delete(storageSpecifier);
          return error;
        }

        /* request first volume for device */
        storageFileHandle->requestedVolumeNumber = 1;
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }
  String_delete(storageSpecifier);

  storageFileHandle->runningInfo.volumeNumber   = 0;
  storageFileHandle->runningInfo.volumeProgress = 0;

  return ERROR_NONE;
}

Errors Storage_done(StorageFileHandle *storageFileHandle)
{
  assert(storageFileHandle != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      break;
    case STORAGE_TYPE_SSH:
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        Password_delete(storageFileHandle->ftp.password);
        String_delete(storageFileHandle->ftp.loginName);
        String_delete(storageFileHandle->ftp.hostName);
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        free(storageFileHandle->scp.readAheadBuffer.data);
        Password_delete(storageFileHandle->scp.password);
        String_delete(storageFileHandle->scp.loginName);
        String_delete(storageFileHandle->scp.hostName);
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        free(storageFileHandle->sftp.readAheadBuffer.data);
        Password_delete(storageFileHandle->sftp.password);
        String_delete(storageFileHandle->scp.loginName);
        String_delete(storageFileHandle->sftp.hostName);
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      {
        Errors error;
        String fileName;

        /* delete files */
        fileName = String_new();
        while (!StringList_empty(&storageFileHandle->opticalDisk.fileNameList))
        {
          StringList_getFirst(&storageFileHandle->opticalDisk.fileNameList,fileName);
          error = File_delete(fileName,FALSE);
          if (error != ERROR_NONE)
          {
            String_delete(fileName);
            return error;
          }
        }
        String_delete(fileName);

        /* delete temporare directory */
        File_delete(storageFileHandle->opticalDisk.directory,FALSE);

        /* free resources */
        String_delete(storageFileHandle->opticalDisk.fileName);
        String_delete(storageFileHandle->opticalDisk.directory);
        String_delete(storageFileHandle->opticalDisk.name);
      }
      break;
    case STORAGE_TYPE_DEVICE:
      {
        Errors error;
        String fileName;

        /* delete files */
        fileName = String_new();
        while (!StringList_empty(&storageFileHandle->device.fileNameList))
        {
          StringList_getFirst(&storageFileHandle->device.fileNameList,fileName);
          error = File_delete(fileName,FALSE);
          if (error != ERROR_NONE)
          {
            String_delete(fileName);
            return error;
          }
        }
        String_delete(fileName);

        /* delete temporare directory */
        File_delete(storageFileHandle->device.directory,FALSE);

        /* free resources */
        String_delete(storageFileHandle->device.fileName);
        String_delete(storageFileHandle->device.directory);
        String_delete(storageFileHandle->device.name);
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

String Storage_getHandleName(String                  storageName,
                             const StorageFileHandle *storageFileHandle,
                             const String            fileName
                            )
{
  String storageSpecifier;

  assert(storageName != NULL);
  assert(storageFileHandle != NULL);

  /* initialize variables */
  storageSpecifier = String_new(); 

  /* format specifier */
  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      break;
    case STORAGE_TYPE_SSH:
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        if (!String_empty(storageFileHandle->ftp.hostName))
        {
          if (!String_empty(storageFileHandle->ftp.loginName))
          {
            String_format(storageSpecifier,"%S@",storageFileHandle->ftp.loginName);
          }
          String_format(storageSpecifier,"%S/",storageFileHandle->ftp.hostName);
        }
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        if (!String_empty(storageFileHandle->scp.hostName))
        {
          if (!String_empty(storageFileHandle->scp.loginName))
          {
            String_format(storageSpecifier,"%S@",storageFileHandle->scp.loginName);
          }
          String_append(storageSpecifier,storageFileHandle->scp.hostName);
          if (storageFileHandle->scp.hostPort != 0)
          {
            String_format(storageSpecifier,":%d",storageFileHandle->scp.hostPort);
          }
          String_appendChar(storageSpecifier,'/');
        }
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        if (!String_empty(storageFileHandle->sftp.hostName))
        {
          if (!String_empty(storageFileHandle->sftp.loginName))
          {
            String_format(storageSpecifier,"%S@",storageFileHandle->sftp.loginName);
          }
          String_append(storageSpecifier,storageFileHandle->sftp.hostName);
          if (storageFileHandle->sftp.hostPort != 0)
          {
            String_format(storageSpecifier,":%d",storageFileHandle->sftp.hostPort);
          }
          String_appendChar(storageSpecifier,'/');
        }
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
      if (!String_empty(storageFileHandle->opticalDisk.name))
      {
        String_append(storageSpecifier,storageFileHandle->opticalDisk.name);
        String_appendChar(storageSpecifier,':');
      }
      break;
    case STORAGE_TYPE_DVD:
      if (!String_empty(storageFileHandle->opticalDisk.name))
      {
        String_append(storageSpecifier,storageFileHandle->opticalDisk.name);
        String_appendChar(storageSpecifier,':');
      }
      break;
    case STORAGE_TYPE_BD:
      if (!String_empty(storageFileHandle->opticalDisk.name))
      {
        String_append(storageSpecifier,storageFileHandle->opticalDisk.name);
        String_appendChar(storageSpecifier,':');
      }
      break;
    case STORAGE_TYPE_DEVICE:
      if (!String_empty(storageFileHandle->device.name))
      {
        String_append(storageSpecifier,storageFileHandle->device.name);
        String_appendChar(storageSpecifier,':');
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  /* get name */
  Storage_getName(storageName,storageFileHandle->type,storageSpecifier,fileName);

  /* free resources */
  String_delete(storageSpecifier);

  return storageName;
}

Errors Storage_preProcess(StorageFileHandle *storageFileHandle)
{
  Errors error;

  assert(storageFileHandle != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      /* request next medium */
      if (storageFileHandle->opticalDisk.newFlag)
      {
        storageFileHandle->opticalDisk.number++;
        storageFileHandle->opticalDisk.newFlag = FALSE;

        storageFileHandle->requestedVolumeNumber = storageFileHandle->opticalDisk.number;
      }

      /* check if new medium is required */
      if (storageFileHandle->volumeNumber != storageFileHandle->requestedVolumeNumber)
      {
        /* request load new medium */
        error = requestNewMedium(storageFileHandle,FALSE);
        if (error != ERROR_NONE)
        {
          return error;
        }
      }
      break;
    case STORAGE_TYPE_DEVICE:
      /* request next volume */
      if (storageFileHandle->device.newFlag)
      {
        storageFileHandle->device.number++;
        storageFileHandle->device.newFlag = FALSE;

        storageFileHandle->requestedVolumeNumber = storageFileHandle->device.number;
      }

      /* check if new volume is required */
      if (storageFileHandle->volumeNumber != storageFileHandle->requestedVolumeNumber)
      {
        error = requestNewVolume(storageFileHandle,FALSE);
        if (error != ERROR_NONE)
        {
          return ERROR_LOAD_VOLUME_FAIL;
        }
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

Errors Storage_postProcess(StorageFileHandle *storageFileHandle,
                           bool              finalFlag
                          )
{
  assert(storageFileHandle != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      {
        String    imageFileName;
        TextMacro textMacros[5];
        String    fileName;
        Errors    error;
        FileInfo  fileInfo;

        if (finalFlag || (storageFileHandle->opticalDisk.totalSize > storageFileHandle->opticalDisk.volumeSize))
        {
          /* medium size limit reached -> create medium and request new volume */

          /* update info */
          storageFileHandle->runningInfo.volumeProgress = 0.0;
          updateStatusInfo(storageFileHandle);

          /* get temporary image file name */
          imageFileName = String_new();
          error = File_getTmpFileName(imageFileName,NULL,tmpDirectory);
          if (error != ERROR_NONE)
          {
            return error;
          }

          /* init macros */
          TEXT_MACRO_N_STRING (textMacros[0],"%device", storageFileHandle->opticalDisk.name     );
          TEXT_MACRO_N_STRING (textMacros[1],"%file",   storageFileHandle->opticalDisk.directory);
          TEXT_MACRO_N_STRING (textMacros[2],"%image",  imageFileName                           );
          TEXT_MACRO_N_INTEGER(textMacros[3],"%sectors",0                                       );
          TEXT_MACRO_N_INTEGER(textMacros[4],"%number", storageFileHandle->volumeNumber         );

          if (storageFileHandle->jobOptions->errorCorrectionCodesFlag)
          {
            /* create medium image */
            printInfo(0,"Make medium image #%d with %d file(s)...",storageFileHandle->opticalDisk.number,StringList_count(&storageFileHandle->opticalDisk.fileNameList));
            storageFileHandle->opticalDisk.step = 0;
            error = Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.imageCommand),
                                        textMacros,SIZE_OF_ARRAY(textMacros),
                                        (ExecuteIOFunction)processIOmkisofs,
                                        (ExecuteIOFunction)processIOmkisofs,
                                        storageFileHandle
                                       );
            if (error != ERROR_NONE)
            {
              printInfo(0,"FAIL\n");
              File_delete(imageFileName,FALSE);
              String_delete(imageFileName);
              return error;
            }
            File_getFileInfo(imageFileName,&fileInfo);
            printInfo(0,"ok (%llu bytes)\n",fileInfo.size);

            /* add error-correction codes to medium image */
            printInfo(0,"Add ECC to image #%d...",storageFileHandle->opticalDisk.number);
            storageFileHandle->opticalDisk.step = 1;
            error = Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.eccCommand),
                                        textMacros,SIZE_OF_ARRAY(textMacros),
                                        (ExecuteIOFunction)processIOdvdisaster,
                                        (ExecuteIOFunction)processIOdvdisaster,
                                        storageFileHandle
                                       );
            if (error != ERROR_NONE)
            {
              printInfo(0,"FAIL\n");
              File_delete(imageFileName,FALSE);
              String_delete(imageFileName);
              return error;
            }
            File_getFileInfo(imageFileName,&fileInfo);
            printInfo(0,"ok (%llu bytes)\n",fileInfo.size);

            /* get number of image sectors */
            if (File_getFileInfo(imageFileName,&fileInfo) == ERROR_NONE)
            {
              TEXT_MACRO_N_INTEGER(textMacros[3],"%sectors",(ulong)(fileInfo.size/2048LL));
            }

            /* check if new medium is required */
            if (storageFileHandle->volumeNumber != storageFileHandle->requestedVolumeNumber)
            {
              /* request load new medium */
              error = requestNewMedium(storageFileHandle,TRUE);
              if (error != ERROR_NONE)
              {
                File_delete(imageFileName,FALSE);
                String_delete(imageFileName);
                return error;
              }
              updateStatusInfo(storageFileHandle);
            }

            /* write image to medium */
            printInfo(0,"Write image to medium #%d...",storageFileHandle->opticalDisk.number);
            storageFileHandle->opticalDisk.step = 3;
            error = Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.writeImageCommand),
                                        textMacros,SIZE_OF_ARRAY(textMacros),
                                        (ExecuteIOFunction)processIOgrowisofs,
                                        (ExecuteIOFunction)processIOgrowisofs,
                                        storageFileHandle
                                       );
            if (error != ERROR_NONE)
            {
              printInfo(0,"FAIL\n");
              File_delete(imageFileName,FALSE);
              String_delete(imageFileName);
              return error;
            }
            printInfo(0,"ok\n");
          }
          else
          {
            /* check if new medium is required */
            if (storageFileHandle->volumeNumber != storageFileHandle->requestedVolumeNumber)
            {
              /* request load new medium */
              error = requestNewMedium(storageFileHandle,TRUE);
              if (error != ERROR_NONE)
              {
                File_delete(imageFileName,FALSE);
                String_delete(imageFileName);
                return error;
              }
              updateStatusInfo(storageFileHandle);
            }

            /* write to medium */
            printInfo(0,"Write medium #%d with %d file(s)...",storageFileHandle->opticalDisk.number,StringList_count(&storageFileHandle->opticalDisk.fileNameList));
            storageFileHandle->opticalDisk.step = 0;
            error = Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.writeCommand),
                                        textMacros,SIZE_OF_ARRAY(textMacros),
                                        (ExecuteIOFunction)processIOgrowisofs,
                                        NULL,
                                        storageFileHandle
                                       );
            if (error != ERROR_NONE)
            {
              printInfo(0,"FAIL\n");
              File_delete(imageFileName,FALSE);
              String_delete(imageFileName);
              return error;
            }
            printInfo(0,"ok\n");
          }

          /* delete image */
          File_delete(imageFileName,FALSE);
          String_delete(imageFileName);

          /* update info */
          storageFileHandle->runningInfo.volumeProgress = 1.0;
          updateStatusInfo(storageFileHandle);

          /* delete stored files */
          fileName = String_new();
          while (!StringList_empty(&storageFileHandle->opticalDisk.fileNameList))
          {
            StringList_getFirst(&storageFileHandle->opticalDisk.fileNameList,fileName);
            error = File_delete(fileName,FALSE);
            if (error != ERROR_NONE)
            {
              String_delete(fileName);
              return error;
            }
          }
          String_delete(fileName);

          /* reset */
          storageFileHandle->opticalDisk.newFlag   = TRUE;
          storageFileHandle->opticalDisk.totalSize = 0;
        }
      }
      break;
    case STORAGE_TYPE_DEVICE:
      {
        String    imageFileName;
        TextMacro textMacros[4];
        String    fileName;
        Errors    error;

        if (finalFlag || (storageFileHandle->device.totalSize > storageFileHandle->device.volumeSize))
        {
          /* device size limit reached -> write to device volume and request new volume */

          /* check if new volume is required */
          if (storageFileHandle->volumeNumber != storageFileHandle->requestedVolumeNumber)
          {
            error = requestNewVolume(storageFileHandle,TRUE);
            if (error != ERROR_NONE)
            {
              return ERROR_LOAD_VOLUME_FAIL;
            }
            updateStatusInfo(storageFileHandle);
          }

          /* get temporary image file name */
          imageFileName = String_new();
          error = File_getTmpFileName(imageFileName,NULL,tmpDirectory);
          if (error != ERROR_NONE)
          {
            return error;
          }

          /* init macros */
          TEXT_MACRO_N_STRING (textMacros[0],"%device",storageFileHandle->device.name     );
          TEXT_MACRO_N_STRING (textMacros[1],"%file",  storageFileHandle->device.directory);
          TEXT_MACRO_N_STRING (textMacros[2],"%image", imageFileName                      );
          TEXT_MACRO_N_INTEGER(textMacros[3],"%number",storageFileHandle->volumeNumber    );

          /* create image */
          if (error == ERROR_NONE)
          {
            printInfo(0,"Make image pre-processing of volume #%d...",storageFileHandle->volumeNumber);
            error = Misc_executeCommand(String_cString(storageFileHandle->device.imagePreProcessCommand ),textMacros,SIZE_OF_ARRAY(textMacros),NULL,NULL,NULL);
            printInfo(0,(error == ERROR_NONE)?"ok\n":"FAIL\n");
          }
          if (error == ERROR_NONE)
          {
            printInfo(0,"Make image volume #%d...",storageFileHandle->volumeNumber);
            error = Misc_executeCommand(String_cString(storageFileHandle->device.imageCommand           ),textMacros,SIZE_OF_ARRAY(textMacros),NULL,NULL,NULL);
            printInfo(0,(error == ERROR_NONE)?"ok\n":"FAIL\n");
          }
          if (error == ERROR_NONE)
          {
            printInfo(0,"Make image post-processing of volume #%d...",storageFileHandle->volumeNumber);
            error = Misc_executeCommand(String_cString(storageFileHandle->device.imagePostProcessCommand),textMacros,SIZE_OF_ARRAY(textMacros),NULL,NULL,NULL);
            printInfo(0,(error == ERROR_NONE)?"ok\n":"FAIL\n");
          }

          /* write to device */
          if (error == ERROR_NONE)
          {
            printInfo(0,"Write device pre-processing of volume #%d...",storageFileHandle->volumeNumber);
            error = Misc_executeCommand(String_cString(storageFileHandle->device.writePreProcessCommand ),textMacros,SIZE_OF_ARRAY(textMacros),NULL,NULL,NULL);
            printInfo(0,(error == ERROR_NONE)?"ok\n":"FAIL\n");
          }
          if (error == ERROR_NONE)
          {
            printInfo(0,"Write device volume #%d...",storageFileHandle->volumeNumber);
            error = Misc_executeCommand(String_cString(storageFileHandle->device.writeCommand           ),textMacros,SIZE_OF_ARRAY(textMacros),NULL,NULL,NULL);
            printInfo(0,(error == ERROR_NONE)?"ok\n":"FAIL\n");
          }
          if (error == ERROR_NONE)
          {
            printInfo(0,"Write device post-processing of volume #%d...",storageFileHandle->volumeNumber);
            error = Misc_executeCommand(String_cString(storageFileHandle->device.writePostProcessCommand),textMacros,SIZE_OF_ARRAY(textMacros),NULL,NULL,NULL);
            printInfo(0,(error == ERROR_NONE)?"ok\n":"FAIL\n");
          }

          if (error != ERROR_NONE)
          {
            File_delete(imageFileName,FALSE);
            String_delete(imageFileName);
            return error;
          }
          File_delete(imageFileName,FALSE);
          String_delete(imageFileName);

          /* delete stored files */
          fileName = String_new();
          while (!StringList_empty(&storageFileHandle->device.fileNameList))
          {
            StringList_getFirst(&storageFileHandle->device.fileNameList,fileName);
            error = File_delete(fileName,FALSE);
            if (error != ERROR_NONE)
            {
              String_delete(fileName);
              return error;
            }
          }
          String_delete(fileName);

          /* reset */
          storageFileHandle->device.newFlag   = TRUE;
          storageFileHandle->device.totalSize = 0;
        }
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

uint Storage_getVolumeNumber(const StorageFileHandle *storageFileHandle)
{
  assert(storageFileHandle != NULL);

  return storageFileHandle->volumeNumber;
}

void Storage_setVolumeNumber(StorageFileHandle *storageFileHandle,
                             uint              volumeNumber
                            )
{
  assert(storageFileHandle != NULL);

  storageFileHandle->volumeNumber = volumeNumber;
}

Errors Storage_unloadVolume(StorageFileHandle *storageFileHandle)
{
  Errors error;

  assert(storageFileHandle != NULL);

  error = ERROR_UNKNOWN;
  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
    case STORAGE_TYPE_FTP:
    case STORAGE_TYPE_SSH:
    case STORAGE_TYPE_SCP:
    case STORAGE_TYPE_SFTP:
      error = ERROR_NONE;
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      {
        TextMacro textMacros[1];

        TEXT_MACRO_N_STRING(textMacros[0],"%device",storageFileHandle->opticalDisk.name);
        error = Misc_executeCommand(String_cString(storageFileHandle->opticalDisk.unloadVolumeCommand),
                                    textMacros,SIZE_OF_ARRAY(textMacros),
                                    NULL,
                                    NULL,
                                    NULL
                                   );
      }
      break;
    case STORAGE_TYPE_DEVICE:
      {
        TextMacro textMacros[1];

        TEXT_MACRO_N_STRING(textMacros[0],"%device",storageFileHandle->device.name);
        error = Misc_executeCommand(String_cString(storageFileHandle->device.unloadVolumeCommand),
                                    textMacros,SIZE_OF_ARRAY(textMacros),
                                    NULL,
                                    NULL,
                                    NULL
                                   );
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return error;
}

Errors Storage_create(StorageFileHandle *storageFileHandle,
                      const String      fileName,
                      uint64            fileSize
                     )
{
  Errors error;
  String directoryName;

  assert(storageFileHandle != NULL);
  assert(fileName != NULL);

  /* init variables */
  storageFileHandle->mode = STORAGE_MODE_WRITE;

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      /* check if archive file exists */
      if (!storageFileHandle->jobOptions->overwriteArchiveFilesFlag && File_exists(fileName))
      {
        return ERROR_FILE_EXITS;
      }

      /* open file */
      error = File_open(&storageFileHandle->fileSystem.fileHandle,
                        fileName,
                        FILE_OPENMODE_CREATE
                       );
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
      {
        const char *plainPassword;

        /* initialise variables */

        /* connect */
        if (!Network_hostExists(storageFileHandle->ftp.hostName))
        {
          return ERROR_HOST_NOT_FOUND;
        }
        if (FtpConnect(String_cString(storageFileHandle->ftp.hostName),&storageFileHandle->ftp.control) != 1)
        {
          return ERROR_FTP_SESSION_FAIL;
        }

        /* login */
        plainPassword = Password_deploy(storageFileHandle->ftp.password);
        if (FtpLogin(String_cString(storageFileHandle->ftp.loginName),
                     plainPassword,
                     storageFileHandle->ftp.control
                    ) != 1
           )
        {
          Password_undeploy(storageFileHandle->ftp.password);
          FtpQuit(storageFileHandle->ftp.control);
          return ERROR_FTP_AUTHENTIFICATION;
        }
        Password_undeploy(storageFileHandle->ftp.password);

        /* create file */
        if (FtpAccess(String_cString(fileName),
                      FTPLIB_FILE_WRITE,
                      FTPLIB_IMAGE,
                      storageFileHandle->ftp.control,
                      &storageFileHandle->ftp.data
                     ) != 1
           )
        {
          FtpQuit(storageFileHandle->ftp.control);
          return ERROR_CREATE_FILE;
        }
      }
      #else /* not HAVE_FTP */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      return ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        {
          /* connect */
          error = Network_connect(&storageFileHandle->scp.socketHandle,
                                  SOCKET_TYPE_SSH,
                                  storageFileHandle->scp.hostName,
                                  storageFileHandle->scp.hostPort,
                                  storageFileHandle->scp.loginName,
                                  storageFileHandle->scp.password,
                                  storageFileHandle->scp.sshPublicKeyFileName,
                                  storageFileHandle->scp.sshPrivateKeyFileName,
                                  0
                                 );
          if (error != ERROR_NONE)
          {
            return error;
          }

          /* open channel and file for writing */
          storageFileHandle->scp.channel = libssh2_scp_send(Network_getSSHSession(&storageFileHandle->scp.socketHandle),
                                                            String_cString(fileName),
// ???
0600,
                                                            fileSize
                                                           );
          if (storageFileHandle->scp.channel == NULL)
          {
            char *sshErrorText;

            libssh2_session_last_error(Network_getSSHSession(&storageFileHandle->scp.socketHandle),&sshErrorText,NULL,0);
            error = ERRORX(SSH,
                           libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->scp.socketHandle)),
                           sshErrorText
                          );
            Network_disconnect(&storageFileHandle->scp.socketHandle);
            return error;
          }
        }
      #else /* not HAVE_SSH2 */
        UNUSED_VARIABLE(fileSize);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        {
          /* connect */
          error = Network_connect(&storageFileHandle->sftp.socketHandle,
                                  SOCKET_TYPE_SSH,
                                  storageFileHandle->sftp.hostName,
                                  storageFileHandle->sftp.hostPort,
                                  storageFileHandle->sftp.loginName,
                                  storageFileHandle->sftp.password,
                                  storageFileHandle->sftp.sshPublicKeyFileName,
                                  storageFileHandle->sftp.sshPrivateKeyFileName,
                                  0
                                 );
          if (error != ERROR_NONE)
          {
            return error;
          }

          /* init session */
          storageFileHandle->sftp.sftp = libssh2_sftp_init(Network_getSSHSession(&storageFileHandle->sftp.socketHandle));
          if (storageFileHandle->sftp.sftp == NULL)
          {
            char *sshErrorText;

            libssh2_session_last_error(Network_getSSHSession(&storageFileHandle->scp.socketHandle),&sshErrorText,NULL,0);
            error = ERRORX(SSH,
                           libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->scp.socketHandle)),
                           sshErrorText
                          );
            Network_disconnect(&storageFileHandle->sftp.socketHandle);
            return error;
          }

          /* create file */
          storageFileHandle->sftp.sftpHandle = libssh2_sftp_open(storageFileHandle->sftp.sftp,
                                                                 String_cString(fileName),
                                                                 LIBSSH2_FXF_CREAT|LIBSSH2_FXF_WRITE|LIBSSH2_FXF_TRUNC,
// ???
LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR
                                                                );
          if (storageFileHandle->sftp.sftpHandle == NULL)
          {
            char *sshErrorText;

            libssh2_session_last_error(Network_getSSHSession(&storageFileHandle->scp.socketHandle),&sshErrorText,NULL,0);
            error = ERRORX(SSH,
                           libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->scp.socketHandle)),
                           sshErrorText
                          );
            libssh2_sftp_shutdown(storageFileHandle->sftp.sftp);
            Network_disconnect(&storageFileHandle->sftp.socketHandle);
            return error;
          }
        }
      #else /* not HAVE_SSH2 */
        UNUSED_VARIABLE(fileSize);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      /* create file name */
      String_set(storageFileHandle->opticalDisk.fileName,storageFileHandle->opticalDisk.directory);
      File_appendFileName(storageFileHandle->opticalDisk.fileName,fileName);

      /* create directory if not existing */
      directoryName = File_getFilePathName(String_new(),storageFileHandle->opticalDisk.fileName);
      if (!File_exists(directoryName))
      {
        error = File_makeDirectory(directoryName,
                                   FILE_DEFAULT_USER_ID,
                                   FILE_DEFAULT_GROUP_ID,
                                   FILE_DEFAULT_PERMISSION
                                  );
        if (error != ERROR_NONE)
        {
          String_delete(directoryName);
          return error;
        }
      }
      String_delete(directoryName);

      /* create file */
      error = File_open(&storageFileHandle->opticalDisk.fileHandle,
                        storageFileHandle->opticalDisk.fileName,
                        FILE_OPENMODE_CREATE
                       );
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    case STORAGE_TYPE_DEVICE:
      /* create file name */
      String_set(storageFileHandle->device.fileName,storageFileHandle->device.directory);
      File_appendFileName(storageFileHandle->device.fileName,fileName);

      /* open file */
      error = File_open(&storageFileHandle->device.fileHandle,
                        storageFileHandle->device.fileName,
                        FILE_OPENMODE_CREATE
                       );
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

Errors Storage_open(StorageFileHandle *storageFileHandle,
                    const String      fileName
                   )
{
  Errors error;

  assert(storageFileHandle != NULL);
  assert(fileName != NULL);

  /* init variables */
  storageFileHandle->mode = STORAGE_MODE_READ;

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      /* open file */
      error = File_open(&storageFileHandle->fileSystem.fileHandle,
                        fileName,
                        FILE_OPENMODE_READ
                       );
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
      {
        const char *plainPassword;

        /* initialise variables */

        /* connect */
        if (!Network_hostExists(storageFileHandle->ftp.hostName))
        {
          return ERROR_HOST_NOT_FOUND;
        }
        if (FtpConnect(String_cString(storageFileHandle->ftp.hostName),&storageFileHandle->ftp.control) != 1)
        {
          return ERROR_FTP_SESSION_FAIL;

        }

        /* login */
        plainPassword = Password_deploy(storageFileHandle->ftp.password);
        if (FtpLogin(String_cString(storageFileHandle->ftp.loginName),
                     plainPassword,
                     storageFileHandle->ftp.control
                    ) != 1
           )
        {
          Password_undeploy(storageFileHandle->ftp.password);
          FtpQuit(storageFileHandle->ftp.control);
          return ERROR_FTP_AUTHENTIFICATION;
        }
        Password_undeploy(storageFileHandle->ftp.password);

        /* open file for reading */
        if (FtpAccess(String_cString(fileName),
                      FTPLIB_FILE_READ,
                      FTPLIB_IMAGE,
                      storageFileHandle->ftp.control,
                      &storageFileHandle->ftp.data
                     ) != 1
           )
        {
          FtpQuit(storageFileHandle->ftp.control);
          return ERROR_FTP_AUTHENTIFICATION;
        }
      }
      #else /* not HAVE_FTP */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      return ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        {
          struct stat fileInfo;

          /* init variables */
          storageFileHandle->scp.index                  = 0LL;
          storageFileHandle->scp.readAheadBuffer.offset = 0LL;
          storageFileHandle->scp.readAheadBuffer.length = 0L;
          storageFileHandle->scp.bandWidth.max          = globalOptions.maxBandWidth;

          /* connect */
          error = Network_connect(&storageFileHandle->scp.socketHandle,
                                  SOCKET_TYPE_SSH,
                                  storageFileHandle->scp.hostName,
                                  storageFileHandle->scp.hostPort,
                                  storageFileHandle->scp.loginName,
                                  storageFileHandle->scp.password,
                                  storageFileHandle->scp.sshPublicKeyFileName,
                                  storageFileHandle->scp.sshPrivateKeyFileName,
                                  0
                                 );
          if (error != ERROR_NONE)
          {
            return error;
          }

          /* open channel and file for reading */
          storageFileHandle->scp.channel = libssh2_scp_recv(Network_getSSHSession(&storageFileHandle->scp.socketHandle),
                                                            String_cString(fileName),
                                                            &fileInfo
                                                           );
          if (storageFileHandle->scp.channel == NULL)
          {
            char *sshErrorText;

            libssh2_session_last_error(Network_getSSHSession(&storageFileHandle->scp.socketHandle),&sshErrorText,NULL,0);
            error = ERRORX(SSH,
                           libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->scp.socketHandle)),
                           sshErrorText
                          );
            Network_disconnect(&storageFileHandle->scp.socketHandle);
            return error;
          }
          storageFileHandle->scp.size = (uint64)fileInfo.st_size;
        }
      #else /* not HAVE_SSH2 */
        UNUSED_VARIABLE(fileName);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        {
          LIBSSH2_SFTP_ATTRIBUTES sftpAttributes;

          /* init variables */
          storageFileHandle->sftp.index                  = 0LL;
          storageFileHandle->sftp.readAheadBuffer.offset = 0LL;
          storageFileHandle->sftp.readAheadBuffer.length = 0L;
          storageFileHandle->sftp.bandWidth.max          = globalOptions.maxBandWidth;

          /* connect */
          error = Network_connect(&storageFileHandle->sftp.socketHandle,
                                  SOCKET_TYPE_SSH,
                                  storageFileHandle->sftp.hostName,
                                  storageFileHandle->sftp.hostPort,
                                  storageFileHandle->sftp.loginName,
                                  storageFileHandle->sftp.password,
                                  storageFileHandle->sftp.sshPublicKeyFileName,
                                  storageFileHandle->sftp.sshPrivateKeyFileName,
                                  0
                                 );
          if (error != ERROR_NONE)
          {
            return error;
          }

          /* init session */
          storageFileHandle->sftp.sftp = libssh2_sftp_init(Network_getSSHSession(&storageFileHandle->sftp.socketHandle));
          if (storageFileHandle->sftp.sftp == NULL)
          {
            error = ERROR(SSH,libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->sftp.socketHandle)));
            Network_disconnect(&storageFileHandle->sftp.socketHandle);
            return error;
          }

          /* open file */
          storageFileHandle->sftp.sftpHandle = libssh2_sftp_open(storageFileHandle->sftp.sftp,
                                                                 String_cString(fileName),
                                                                 LIBSSH2_FXF_READ,
                                                                 0
                                                                );
          if (storageFileHandle->sftp.sftpHandle == NULL)
          {
            char *sshErrorText;

            libssh2_session_last_error(Network_getSSHSession(&storageFileHandle->scp.socketHandle),&sshErrorText,NULL,0);
            error = ERRORX(SSH,
                           libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->scp.socketHandle)),
                           sshErrorText
                          );
            libssh2_sftp_shutdown(storageFileHandle->sftp.sftp);
            Network_disconnect(&storageFileHandle->sftp.socketHandle);
            return error;
          }

          /* get file size */
          if (libssh2_sftp_fstat(storageFileHandle->sftp.sftpHandle,
                                 &sftpAttributes
                                ) != 0
             )
          {
            char *sshErrorText;

            libssh2_session_last_error(Network_getSSHSession(&storageFileHandle->scp.socketHandle),&sshErrorText,NULL,0);
            error = ERRORX(SSH,
                           libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->scp.socketHandle)),
                           sshErrorText
                          );
            libssh2_sftp_close(storageFileHandle->sftp.sftpHandle);
            libssh2_sftp_shutdown(storageFileHandle->sftp.sftp);
            Network_disconnect(&storageFileHandle->sftp.socketHandle);
            return error;
          }
          storageFileHandle->sftp.size = sftpAttributes.filesize;
        }
      #else /* not HAVE_SSH2 */
        UNUSED_VARIABLE(fileName);
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      /* init variables */
      /* open file */
#if 0
      error = File_open(&storageFileHandle->fileSystem.fileHandle,
                        storageFileHandle->fileSystem.fileName,
                        FILE_OPENMODE_READ
                       );
      if (error != ERROR_NONE)
      {
        String_delete(storageFileHandle->fileSystem.fileName);
        String_delete(storageSpecifier);
        return error;
      }
#endif /* 0 */
      break;
    case STORAGE_TYPE_DEVICE:
      /* init variables */
      /* open file */
#if 0
      error = File_open(&storageFileHandle->fileSystem.fileHandle,
                        storageFileHandle->fileSystem.fileName,
                        FILE_OPENMODE_READ
                       );
      if (error != ERROR_NONE)
      {
        String_delete(storageFileHandle->fileSystem.fileName);
        String_delete(storageSpecifier);
        return error;
      }
#endif /* 0 */
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

void Storage_close(StorageFileHandle *storageFileHandle)
{
  assert(storageFileHandle != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      File_close(&storageFileHandle->fileSystem.fileHandle);
      break;
    case STORAGE_TYPE_SSH:
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        assert(storageFileHandle->ftp.control != NULL);
        assert(storageFileHandle->ftp.data != NULL);

        FtpClose(storageFileHandle->ftp.data);
        FtpQuit(storageFileHandle->ftp.control);
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        switch (storageFileHandle->mode)
        {
          case STORAGE_MODE_WRITE:
            libssh2_channel_send_eof(storageFileHandle->scp.channel);
//???
//            libssh2_channel_wait_eof(storageFileHandle->scp.channel);
            libssh2_channel_wait_closed(storageFileHandle->scp.channel);
            break;
          case STORAGE_MODE_READ:
            libssh2_channel_close(storageFileHandle->scp.channel);
            libssh2_channel_wait_closed(storageFileHandle->scp.channel);
            break;
          #ifndef NDEBUG
            default:
              HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
              break; /* not reached */
          #endif /* NDEBUG */
        }
        libssh2_channel_free(storageFileHandle->scp.channel);
        Network_disconnect(&storageFileHandle->scp.socketHandle);
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        libssh2_sftp_close(storageFileHandle->sftp.sftpHandle);
        libssh2_sftp_shutdown(storageFileHandle->sftp.sftp);
        Network_disconnect(&storageFileHandle->sftp.socketHandle);
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      storageFileHandle->opticalDisk.totalSize += File_getSize(&storageFileHandle->opticalDisk.fileHandle);
      File_close(&storageFileHandle->opticalDisk.fileHandle);
      StringList_append(&storageFileHandle->opticalDisk.fileNameList,storageFileHandle->opticalDisk.fileName);
      break;
    case STORAGE_TYPE_DEVICE:
      storageFileHandle->device.totalSize += File_getSize(&storageFileHandle->device.fileHandle);
      File_close(&storageFileHandle->device.fileHandle);
      StringList_append(&storageFileHandle->device.fileNameList,storageFileHandle->device.fileName);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }
}

Errors Storage_delete(StorageFileHandle *storageFileHandle,
                      const String      fileName
                     )
{
  Errors error;

  assert(storageFileHandle != NULL);

  error = ERROR_UNKNOWN;
  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      error = File_delete(fileName,FALSE);
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        error = (FtpDelete(String_cString(fileName),storageFileHandle->ftp.data) == 1)?ERROR_NONE:ERROR_DELETE_FILE;
      #else /* not HAVE_FTP */
        error = ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
#if 0
whould this be a possible implementation?
        {
          String command;

          /* there is no unlink command for scp: execute either 'rm' or 'del' on remote server */
          command = String_new();
          if (error != ERROR_NONE)
          {
            String_format(String_clear(command),"rm %'S",fileName);
            error = (libssh2_channel_exec(storageFileHandle->scp.channel,
                                          String_cString(command)
                                         ) != 0
                    )?ERROR_NONE:ERROR_DELETE_FILE;
          }
          if (error != ERROR_NONE)
          {
            String_format(String_clear(command),"del %'S",fileName);
            error = (libssh2_channel_exec(storageFileHandle->scp.channel,
                                          String_cString(command)
                                         ) != 0
                    )?ERROR_NONE:ERROR_DELETE_FILE;
          }
          String_delete(command);
        }
      #else /* not HAVE_SSH2 */
        error = ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
#endif /* 0 */
      error = ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        error = Network_connect(&storageFileHandle->sftp.socketHandle,
                                SOCKET_TYPE_SSH,
                                storageFileHandle->sftp.hostName,
                                storageFileHandle->sftp.hostPort,
                                storageFileHandle->sftp.loginName,
                                storageFileHandle->sftp.password,
                                storageFileHandle->sftp.sshPublicKeyFileName,
                                storageFileHandle->sftp.sshPrivateKeyFileName,
                                0
                               );
        if (error == ERROR_NONE)
        {
          /* init session */
          storageFileHandle->sftp.sftp = libssh2_sftp_init(Network_getSSHSession(&storageFileHandle->sftp.socketHandle));
          if (storageFileHandle->sftp.sftp != NULL)
          {
            error = (libssh2_sftp_unlink(storageFileHandle->sftp.sftp,
                                         String_cString(fileName)
                                        ) != 0
                    )?ERROR_NONE:ERROR_DELETE_FILE;

            libssh2_sftp_shutdown(storageFileHandle->sftp.sftp);
          }
          else
          {
            error = ERROR(SSH,libssh2_session_last_errno(Network_getSSHSession(&storageFileHandle->sftp.socketHandle)));
            Network_disconnect(&storageFileHandle->sftp.socketHandle);
          }
          Network_disconnect(&storageFileHandle->sftp.socketHandle);
        }
      #else /* not HAVE_SSH2 */
        error = ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      #ifdef HAVE_SSH2
        error = ERROR_NONE;
      #else /* not HAVE_SSH2 */
        error = ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_DEVICE:
      error = ERROR_NONE;
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return error;
}

bool Storage_eof(StorageFileHandle *storageFileHandle)
{
  assert(storageFileHandle != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      return File_eof(&storageFileHandle->fileSystem.fileHandle);
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        return storageFileHandle->scp.index >= storageFileHandle->scp.size;
      #else /* not HAVE_SSH2 */
        return TRUE;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        return storageFileHandle->sftp.index >= storageFileHandle->sftp.size;
      #else /* not HAVE_SSH2 */
        return TRUE;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      #ifdef HAVE_SSH2
        return File_eof(&storageFileHandle->opticalDisk.fileHandle);
      #else /* not HAVE_SSH2 */
        return TRUE;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_DEVICE:
      return File_eof(&storageFileHandle->device.fileHandle);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return TRUE;
}

Errors Storage_read(StorageFileHandle *storageFileHandle,
                    void              *buffer,
                    ulong             size,
                    ulong             *bytesRead
                   )
{
  Errors error;

  assert(storageFileHandle != NULL);
  assert(storageFileHandle->mode == STORAGE_MODE_READ);
  assert(buffer != NULL);
  assert(bytesRead != NULL);

//fprintf(stderr,"%s,%d: size=%lu\n",__FILE__,__LINE__,size);
  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      error = File_read(&storageFileHandle->fileSystem.fileHandle,buffer,size,bytesRead);
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        assert(storageFileHandle->ftp.control != NULL);
        assert(storageFileHandle->ftp.data != NULL);

        (*bytesRead) = FtpRead(buffer,size,storageFileHandle->ftp.data);
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      return ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        {
          ulong   i;
          ssize_t n;

          assert(storageFileHandle->scp.channel != NULL);
          assert(storageFileHandle->scp.readAheadBuffer.data != NULL);

#if 0
          (*bytesRead) = 0;

        n = libssh2_channel_read(storageFileHandle->scp.channel,
                                            buffer,
                                            size
                                           );
        if (n < 0)
        {
                return ERROR(IO_ERROR,errno);
        }
        (*bytesRead) += n;
        storageFileHandle->scp.index += n;
#else /* 0 */
          (*bytesRead) = 0;

          /* copy as much as available from read-ahead buffer */
          if (   (storageFileHandle->scp.index >= storageFileHandle->scp.readAheadBuffer.offset)
              && (storageFileHandle->scp.index < (storageFileHandle->scp.readAheadBuffer.offset+storageFileHandle->scp.readAheadBuffer.length))
             )
          {
            i = storageFileHandle->scp.index-storageFileHandle->scp.readAheadBuffer.offset;
            n = MIN(size,storageFileHandle->scp.readAheadBuffer.length-i);
            memcpy(buffer,storageFileHandle->scp.readAheadBuffer.data+i,n);
            buffer = (byte*)buffer+n;
            size -= n;
            (*bytesRead) += n;
            storageFileHandle->scp.index += n;
          }

          /* read rest of data */
          if (size > 0)
          {
            if (size < MAX_BUFFER_SIZE)
            {
              /* read into read-ahead buffer */
              do
              {
                n = libssh2_channel_read(storageFileHandle->scp.channel,
                                         (char*)storageFileHandle->scp.readAheadBuffer.data,
                                         MIN((size_t)(storageFileHandle->scp.size-storageFileHandle->scp.index),MAX_BUFFER_SIZE)
                                        );
              }
              while (n == LIBSSH2_ERROR_EAGAIN);
              if (n < 0)
              {
                return ERROR(IO_ERROR,errno);
              }
              storageFileHandle->scp.readAheadBuffer.offset = storageFileHandle->scp.index;
              storageFileHandle->scp.readAheadBuffer.length = n;
//fprintf(stderr,"%s,%d: n=%ld storageFileHandle->scp.bufferOffset=%llu storageFileHandle->scp.bufferLength=%lu\n",__FILE__,__LINE__,n,
//storageFileHandle->scp.readAheadBuffer.offset,storageFileHandle->scp.readAheadBuffer.length);

              n = MIN(size,storageFileHandle->scp.readAheadBuffer.length);
              memcpy(buffer,storageFileHandle->scp.readAheadBuffer.data,n);
            }
            else
            {
              /* read direct */
              n = libssh2_channel_read(storageFileHandle->scp.channel,
                                       buffer,
                                       size
                                      );
              if (n < 0)
              {
                return ERROR(IO_ERROR,errno);
              }
            }
            (*bytesRead) += n;
            storageFileHandle->scp.index += n;
          }
#endif /* 0 */
        }
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        {
          ulong   i;
          ssize_t n;

          assert(storageFileHandle->sftp.sftpHandle != NULL);
          assert(storageFileHandle->sftp.readAheadBuffer.data != NULL);

          (*bytesRead) = 0;

          /* copy as much as available from read-ahead buffer */
          if (   (storageFileHandle->sftp.index >= storageFileHandle->sftp.readAheadBuffer.offset)
              && (storageFileHandle->sftp.index < (storageFileHandle->sftp.readAheadBuffer.offset+storageFileHandle->sftp.readAheadBuffer.length))
             )
          {
            i = storageFileHandle->sftp.index-storageFileHandle->sftp.readAheadBuffer.offset;
            n = MIN(size,storageFileHandle->sftp.readAheadBuffer.length-i);
            memcpy(buffer,storageFileHandle->sftp.readAheadBuffer.data+i,n);
            buffer = (byte*)buffer+n;
            size -= n;
            (*bytesRead) += n;
            storageFileHandle->sftp.index += n;
          }

          /* read rest of data */
          if (size > 0)
          {
            #ifdef HAVE_SSH2_SFTP_SEEK2
              libssh2_sftp_seek2(storageFileHandle->sftp.sftpHandle,storageFileHandle->sftp.index);
            #else
              libssh2_sftp_seek(storageFileHandle->sftp.sftpHandle,storageFileHandle->sftp.index);
            #endif
            if (size < MAX_BUFFER_SIZE)
            {
              /* read into read-ahead buffer */
              n = libssh2_sftp_read(storageFileHandle->sftp.sftpHandle,
                                    (char*)storageFileHandle->sftp.readAheadBuffer.data,
                                    MIN((size_t)(storageFileHandle->sftp.size-storageFileHandle->sftp.index),MAX_BUFFER_SIZE)
                                   );
              if (n < 0)
              {
                return ERROR(IO_ERROR,errno);
              }
              storageFileHandle->sftp.readAheadBuffer.offset = storageFileHandle->sftp.index;
              storageFileHandle->sftp.readAheadBuffer.length = n;
//fprintf(stderr,"%s,%d: n=%ld storageFileHandle->sftp.bufferOffset=%llu storageFileHandle->sftp.bufferLength=%lu\n",__FILE__,__LINE__,n,
//storageFileHandle->sftp.readAheadBuffer.offset,storageFileHandle->sftp.readAheadBuffer.length);

              n = MIN(size,storageFileHandle->sftp.readAheadBuffer.length);
              memcpy(buffer,storageFileHandle->sftp.readAheadBuffer.data,n);
            }
            else
            {
              /* read direct */
              n = libssh2_sftp_read(storageFileHandle->sftp.sftpHandle,
                                    buffer,
                                    size
                                   );
              if (n < 0)
              {
                return ERROR(IO_ERROR,errno);
              }
            }
            (*bytesRead) += n;
            storageFileHandle->sftp.index += n;
          }
        }
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      error = File_read(&storageFileHandle->opticalDisk.fileHandle,buffer,size,bytesRead);
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    case STORAGE_TYPE_DEVICE:
      error = File_read(&storageFileHandle->device.fileHandle,buffer,size,bytesRead);
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

Errors Storage_write(StorageFileHandle *storageFileHandle,
                     const void        *buffer,
                     ulong             size
                    )
{
  Errors error;

  assert(storageFileHandle != NULL);
  assert(storageFileHandle->mode == STORAGE_MODE_WRITE);
  assert(buffer != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      error = File_write(&storageFileHandle->fileSystem.fileHandle,buffer,size);
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        {
          ulong  writtenBytes;
          long   length;
          long   n;
          uint64 startTimestamp,endTimestamp;

          assert(storageFileHandle->ftp.control != NULL);
          assert(storageFileHandle->ftp.data != NULL);

          writtenBytes = 0;
          while (writtenBytes < size)
          {
            /* get start time */
            startTimestamp = Misc_getTimestamp();

            /* send data */
            if (storageFileHandle->ftp.bandWidth.max > 0)
            {
              length = MIN(storageFileHandle->ftp.bandWidth.blockSize,size-writtenBytes);
            }
            else
            {
              length = size-writtenBytes;
            }
            n = FtpWrite((void*)buffer,length,storageFileHandle->ftp.data);
            if (n < 0)
            {
              return ERROR_NETWORK_SEND;
            }
            buffer = (byte*)buffer+n;
            writtenBytes += n;

            /* get end time, transmission time */
            endTimestamp = Misc_getTimestamp();

            /* limit used band width if requested (note: when the system time is
               changing endTimestamp may become smaller than startTimestamp;
               thus do not check this with an assert())
            */
            if (endTimestamp >= startTimestamp)
            {
              limitBandWidth(&storageFileHandle->ftp.bandWidth,n,endTimestamp-startTimestamp);
            }
          };
        }
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      return ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        {
          ulong  writtenBytes;
          long   length;
          long   n;
          uint64 startTimestamp,endTimestamp;

          assert(storageFileHandle->scp.channel != NULL);

          writtenBytes = 0;
          while (writtenBytes < size)
          {
            /* send data */
            if (storageFileHandle->scp.bandWidth.max > 0)
            {
              length = MIN(storageFileHandle->scp.bandWidth.blockSize,size-writtenBytes);
            }
            else
            {
              length = size-writtenBytes;
            }

            /* get start time */
            startTimestamp = Misc_getTimestamp();

            // workaround for libssh2-problem: it seems sending of blocks >=8k cause problems, e. g. corrupt ssh MAC
            length = MIN(length,4*1024);
            do
            {
              n = libssh2_channel_write(storageFileHandle->scp.channel,
                                        buffer,
                                        length
                                       );
            }
            while (n == LIBSSH2_ERROR_EAGAIN);

            /* get end time, transmission time */
            endTimestamp = Misc_getTimestamp();

// ??? is it possible in blocking-mode that write() return 0 and this is not an error?
#if 1
            if      (n == 0)
            {
              // should not happen in blocking-mode: bug? libssh2 API changed somewhere between 0.18 and 1.2.4? => wait for data
              if (!waitSessionSocket(&storageFileHandle->scp.socketHandle))
              {
                return ERROR_NETWORK_SEND;
              }
            }
            else if (n < 0)
            {
              return ERROR_NETWORK_SEND;
            }
#else /* 0 */
            if (n <= 0)
            {
              return ERROR_NETWORK_SEND;
            }
#endif /* 0 */
            buffer = (byte*)buffer+n;
            writtenBytes += n;


            /* limit used band width if requested (note: when the system time is
               changing endTimestamp may become smaller than startTimestamp;
               thus do not check this with an assert())
            */
            if (endTimestamp >= startTimestamp)
            {
              limitBandWidth(&storageFileHandle->scp.bandWidth,n,endTimestamp-startTimestamp);
            }
          };
        }
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        {
          ulong  writtenBytes;
          long   length;
          long   n;
          uint64 startTimestamp,endTimestamp;

          assert(storageFileHandle->sftp.sftpHandle != NULL);

          writtenBytes = 0;
          while (writtenBytes < size)
          {
            /* send data */
            if (storageFileHandle->sftp.bandWidth.max > 0)
            {
              length = MIN(storageFileHandle->sftp.bandWidth.blockSize,size-writtenBytes);
            }
            else
            {
              length = size-writtenBytes;
            }

            /* get start time */
            startTimestamp = Misc_getTimestamp();

            do
            {
              n = libssh2_sftp_write(storageFileHandle->sftp.sftpHandle,
                                     buffer,
                                     length
                                    );
            }
            while (n == LIBSSH2_ERROR_EAGAIN);

            /* get end time, transmission time */
            endTimestamp = Misc_getTimestamp();

// ??? is it possible in blocking-mode that write() return 0 and this is not an error?
#if 1
            if      (n == 0)
            {
              // should not happen in blocking-mode: bug? libssh2 API changed somewhere between 0.18 and 1.2.4? => wait for data
              if (!waitSessionSocket(&storageFileHandle->sftp.socketHandle))
              {
                return ERROR_NETWORK_SEND;
              }
            }
            else if (n < 0)
            {
              return ERROR_NETWORK_SEND;
            }
#else /* 0 */
            if (n <= 0)
            {
              return ERROR_NETWORK_SEND;
            }
#endif /* 0 */
            buffer = (byte*)buffer+n;
            size -= n;

            /* limit used band width if requested (note: when the system time is
               changing endTimestamp may become smaller than startTimestamp;
               thus do not check this with an assert())
            */
            if (endTimestamp >= startTimestamp)
            {
              limitBandWidth(&storageFileHandle->sftp.bandWidth,n,endTimestamp-startTimestamp);
            }
          };
        }
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      error = File_write(&storageFileHandle->opticalDisk.fileHandle,buffer,size);
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    case STORAGE_TYPE_DEVICE:
      error = File_write(&storageFileHandle->device.fileHandle,buffer,size);
      if (error != ERROR_NONE)
      {
        return error;
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

uint64 Storage_getSize(StorageFileHandle *storageFileHandle)
{
  assert(storageFileHandle != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      return File_getSize(&storageFileHandle->fileSystem.fileHandle);
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        return storageFileHandle->scp.size;
      #else /* not HAVE_SSH2 */
        return 0LL;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        return storageFileHandle->sftp.size;
      #else /* not HAVE_SSH2 */
        return 0LL;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      #ifdef HAVE_SSH2
        return File_getSize(&storageFileHandle->opticalDisk.fileHandle);
      #else /* not HAVE_SSH2 */
        return 0LL;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_DEVICE:
      return File_getSize(&storageFileHandle->device.fileHandle);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return 0;
}

Errors Storage_tell(StorageFileHandle *storageFileHandle,
                    uint64            *offset
                   )
{
  assert(storageFileHandle != NULL);
  assert(offset != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      return File_tell(&storageFileHandle->fileSystem.fileHandle,offset);
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        (*offset) = storageFileHandle->scp.index;
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        (*offset) = storageFileHandle->sftp.index;
        return ERROR_NONE;
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      #ifdef HAVE_SSH2
        return File_tell(&storageFileHandle->opticalDisk.fileHandle,offset);
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_DEVICE:
      return File_tell(&storageFileHandle->device.fileHandle,offset);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

Errors Storage_seek(StorageFileHandle *storageFileHandle,
                    uint64            offset
                   )
{
  assert(storageFileHandle != NULL);

  switch (storageFileHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      return File_seek(&storageFileHandle->fileSystem.fileHandle,offset);
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      #ifdef HAVE_SSH2
        {        
          /* scp protocol does not support a seek-function. Thus try to
             read and discard data to position the read index to the
             requested offset.
             Note: this is slow!
          */

          assert(storageFileHandle->scp.channel != NULL);
          assert(storageFileHandle->scp.readAheadBuffer.data != NULL);

          if      (offset > storageFileHandle->scp.index)
          {
            uint64  size;
            ulong   i;
            ssize_t n;

            size = offset-storageFileHandle->scp.index;

            /* skip data in read-ahead buffer */
            if (   (storageFileHandle->scp.index >= storageFileHandle->scp.readAheadBuffer.offset)
                && (storageFileHandle->scp.index < (storageFileHandle->scp.readAheadBuffer.offset+storageFileHandle->scp.readAheadBuffer.length))
               )
            {
              i = storageFileHandle->scp.index-storageFileHandle->scp.readAheadBuffer.offset;
              n = MIN(size,storageFileHandle->scp.readAheadBuffer.length-i);
              size -= n;
              storageFileHandle->scp.index += n;
            }

            /* skip rest of data */
            if (size > 0)
            {
              while (size > 0LL)
              {
                // wait for data
                if (!waitSessionSocket(&storageFileHandle->scp.socketHandle))
                {
                  return ERROR(IO_ERROR,errno);
                }

                // read data
                n = libssh2_channel_read(storageFileHandle->scp.channel,
                                         (char*)storageFileHandle->scp.readAheadBuffer.data,
                                         MIN((size_t)size,MAX_BUFFER_SIZE)
                                        );
                if (n < 0)
                {
                  return ERROR(IO_ERROR,errno);
                }
                storageFileHandle->scp.readAheadBuffer.offset = storageFileHandle->scp.index;
                storageFileHandle->scp.readAheadBuffer.length = n;

                size -= n;
                storageFileHandle->scp.index += n;
              }
            }
          }
          else if (offset < storageFileHandle->scp.index)
          {
            return ERROR_FUNCTION_NOT_SUPPORTED;
          }
        }
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        #ifdef HAVE_SSH2_SFTP_SEEK2
          libssh2_sftp_seek2(storageFileHandle->sftp.sftpHandle,
                             offset
                            );
        #else
          libssh2_sftp_seek(storageFileHandle->sftp.sftpHandle,
                            (size_t)offset
                           );
        #endif
        storageFileHandle->sftp.index = offset;
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      #ifdef HAVE_SSH2
        return File_seek(&storageFileHandle->opticalDisk.fileHandle,offset);
      #else /* not HAVE_SSH2 */
        return ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_DEVICE:
      return File_seek(&storageFileHandle->device.fileHandle,offset);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

/*---------------------------------------------------------------------*/

Errors Storage_openDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle,
                                 const String               storageName,
                                 const JobOptions           *jobOptions
                                )
{
  String       storageSpecifier;
  String       pathName;
  StorageTypes storageType;
  Errors       error;

  assert(storageDirectoryListHandle != NULL);
  assert(storageName != NULL);
  assert(jobOptions != NULL);

  /* get storage specifier, file name, path name */
  storageSpecifier = String_new();
  pathName         = String_new();
  storageType = Storage_parseName(storageName,storageSpecifier,pathName);

  /* open directory listing */
  error = ERROR_UNKNOWN;
  switch (storageType)
  {
    case STORAGE_TYPE_FILESYSTEM:
      UNUSED_VARIABLE(jobOptions);

      /* init variables */
      storageDirectoryListHandle->type = STORAGE_TYPE_FILESYSTEM;

      /* open directory */
      error = File_openDirectoryList(&storageDirectoryListHandle->fileSystem.directoryListHandle,
                                     pathName
                                    );
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        {
          String     loginName;
          Password   *password;
          String     hostName;
          FTPServer  ftpServer;
          const char *plainPassword;
          netbuf     *control;

          /* init variables */
          storageDirectoryListHandle->type                 = STORAGE_TYPE_FTP;
          storageDirectoryListHandle->ftp.fileListFileName = String_new();
          storageDirectoryListHandle->ftp.line             = String_new();

          /* create temporary list file */
          error = File_getTmpFileName(storageDirectoryListHandle->ftp.fileListFileName,NULL,tmpDirectory);
          if (error != ERROR_NONE)
          {
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            return error;
          }

          /* parse storage specifier string */
          loginName = String_new();
          password  = Password_new();
          hostName  = String_new();
          if (!Storage_parseFTPSpecifier(storageSpecifier,loginName,password,hostName))
          {
            String_delete(hostName);
            Password_delete(password);
            String_delete(loginName);
            File_delete(storageDirectoryListHandle->ftp.fileListFileName,FALSE);
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            error = ERROR_FTP_SESSION_FAIL;
            break;
          }

          /* get FTP server settings */
          getFTPServerSettings(hostName,jobOptions,&ftpServer);
          if (String_empty(loginName)) String_set(loginName,ftpServer.loginName);

          /* check FTP login, get correct password */
          error = ERROR_UNKNOWN;
          if ((error != ERROR_NONE) && !Password_empty(password))
          {
            error = checkFTPLogin(loginName,
                                  password,
                                  hostName
                                 );
          }
          if ((error != ERROR_NONE) && (ftpServer.password != NULL))
          {
            error = checkFTPLogin(loginName,
                                  ftpServer.password,
                                  hostName
                                 );
            if (error == ERROR_NONE)
            {
              Password_set(password,ftpServer.password);
            }
          }
          if ((error != ERROR_NONE) && !Password_empty(defaultFTPPassword))
          {
            error = checkFTPLogin(loginName,
                                  defaultFTPPassword,
                                  hostName
                                 );
            if (error == ERROR_NONE)
            {
              Password_set(password,defaultFTPPassword);
            }
          }
          if (error != ERROR_NONE)
          {
            /* initialize default password */
            if (initFTPPassword(jobOptions))
            {
              error = checkFTPLogin(loginName,
                                    defaultFTPPassword,
                                    hostName
                                   );
              if (error == ERROR_NONE)
              {
                Password_set(password,defaultFTPPassword);
              }
            }
            else
            {
              error = (ftpServer.password != NULL)?ERROR_INVALID_FTP_PASSWORD:ERROR_NO_FTP_PASSWORD;
            }
          }
          if (error != ERROR_NONE)
          {
            String_delete(hostName);
            Password_delete(password);
            String_delete(loginName);
            File_delete(storageDirectoryListHandle->ftp.fileListFileName,FALSE);
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            error = ERROR_HOST_NOT_FOUND;
            break;
          }

          /* connect */
          if (!Network_hostExists(hostName))
          {
            String_delete(hostName);
            Password_delete(password);
            String_delete(loginName);
            File_delete(storageDirectoryListHandle->ftp.fileListFileName,FALSE);
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            error = ERROR_HOST_NOT_FOUND;
            break;
          }
          if (FtpConnect(String_cString(hostName),&control) != 1)
          {
            String_delete(hostName);
            Password_delete(password);
            String_delete(loginName);
            File_delete(storageDirectoryListHandle->ftp.fileListFileName,FALSE);
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            return ERROR_FTP_SESSION_FAIL;
          }

          /* login */
          plainPassword = Password_deploy(password);
          if (FtpLogin(String_cString(loginName),
                       plainPassword,
                       control
                      ) != 1
             )
          {
            Password_undeploy(password);
            FtpQuit(control);
            String_delete(hostName);
            Password_delete(password);
            String_delete(loginName);
            File_delete(storageDirectoryListHandle->ftp.fileListFileName,FALSE);
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            error = ERROR_FTP_AUTHENTIFICATION;
            break;
          }
          Password_undeploy(password);

          /* read directory */
          if (FtpDir(String_cString(storageDirectoryListHandle->ftp.fileListFileName),String_cString(pathName),control) != 1)
          {
            String_delete(hostName);
            Password_delete(password);
            String_delete(loginName);
            File_delete(storageDirectoryListHandle->ftp.fileListFileName,FALSE);
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            error = ERROR_OPEN_DIRECTORY;
            break;
          }

          /* disconnect */
          FtpQuit(control);

          /* open list file */
          error = File_open(&storageDirectoryListHandle->ftp.fileHandle,storageDirectoryListHandle->ftp.fileListFileName,FILE_OPENMODE_READ);
          if (error != ERROR_NONE)
          {
            String_delete(hostName);
            Password_delete(password);
            String_delete(loginName);
            File_delete(storageDirectoryListHandle->ftp.fileListFileName,FALSE);
            String_delete(storageDirectoryListHandle->ftp.line);
            String_delete(storageDirectoryListHandle->ftp.fileListFileName);
            break;
          }

          /* free resources */
          String_delete(hostName);
          Password_delete(password);
          String_delete(loginName);
        }
      #else /* not HAVE_FTP */
        UNUSED_VARIABLE(jobOptions);

        error = ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
error = ERROR_FUNCTION_NOT_SUPPORTED;
      #else /* not HAVE_SSH2 */
        UNUSED_VARIABLE(jobOptions);

        error = ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
//      error = ERROR_FUNCTION_NOT_SUPPORTED;
//      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        {
          String    loginName;
          String    hostName;
          uint      hostPort;
          SSHServer sshServer;

          /* init variables */
          storageDirectoryListHandle->type               = STORAGE_TYPE_SFTP;
          storageDirectoryListHandle->sftp.pathName      = String_new();
          storageDirectoryListHandle->sftp.buffer        = (char*)malloc(MAX_FILENAME_LENGTH);
          if (storageDirectoryListHandle->sftp.buffer == NULL)
          {
            error = ERROR_INSUFFICIENT_MEMORY;
            break;
          }
          storageDirectoryListHandle->sftp.bufferLength  = 0;
          storageDirectoryListHandle->sftp.entryReadFlag = FALSE;

          /* parse storage specifier string */
          loginName = String_new();
          hostName  = String_new();
          if (!Storage_parseSSHSpecifier(storageSpecifier,loginName,hostName,&hostPort))
          {
            String_delete(hostName);
            String_delete(loginName);
            free(storageDirectoryListHandle->sftp.buffer);
            String_delete(storageDirectoryListHandle->sftp.pathName);
            error = ERROR_SSH_SESSION_FAIL;
            break;
          }
          String_set(storageDirectoryListHandle->sftp.pathName,pathName);

          /* get SSH server settings */
          getSSHServerSettings(hostName,jobOptions,&sshServer);
          if (String_empty(loginName)) String_set(loginName,sshServer.loginName);

          /* open network connection */
          error = Network_connect(&storageDirectoryListHandle->sftp.socketHandle,
                                  SOCKET_TYPE_SSH,
                                  hostName,
                                  (hostPort != 0)?hostPort:sshServer.port,
                                  loginName,
                                  (sshServer.password != NULL)?sshServer.password:defaultSSHPassword,
                                  sshServer.publicKeyFileName,
                                  sshServer.privateKeyFileName,
                                  0
                                 );
          if (error != ERROR_NONE)
          {
            String_delete(hostName);
            String_delete(loginName);
            free(storageDirectoryListHandle->sftp.buffer);
            String_delete(storageDirectoryListHandle->sftp.pathName);
            break;
          }

          /* init FTP session */
          storageDirectoryListHandle->sftp.sftp = libssh2_sftp_init(Network_getSSHSession(&storageDirectoryListHandle->sftp.socketHandle));
          if (storageDirectoryListHandle->sftp.sftp == NULL)
          {
            error = ERROR(SSH,libssh2_session_last_errno(Network_getSSHSession(&storageDirectoryListHandle->sftp.socketHandle)));
            Network_disconnect(&storageDirectoryListHandle->sftp.socketHandle);
            String_delete(hostName);
            String_delete(loginName);
            free(storageDirectoryListHandle->sftp.buffer);
            String_delete(storageDirectoryListHandle->sftp.pathName);
            break;
          }

          /* open directory for reading */
          storageDirectoryListHandle->sftp.sftpHandle = libssh2_sftp_opendir(storageDirectoryListHandle->sftp.sftp,
                                                                             String_cString(storageDirectoryListHandle->sftp.pathName)
                                                                            );
          if (storageDirectoryListHandle->sftp.sftpHandle == NULL)
          {
            error = ERROR(SSH,libssh2_session_last_errno(Network_getSSHSession(&storageDirectoryListHandle->sftp.socketHandle)));
            libssh2_sftp_shutdown(storageDirectoryListHandle->sftp.sftp);
            Network_disconnect(&storageDirectoryListHandle->sftp.socketHandle);
            String_delete(hostName);
            String_delete(loginName);
            free(storageDirectoryListHandle->sftp.buffer);
            String_delete(storageDirectoryListHandle->sftp.pathName);
            break;
          }

          /* free resources */
          String_delete(hostName);
          String_delete(loginName);
        }
      #else /* not HAVE_SSH2 */
        UNUSED_VARIABLE(jobOptions);

        error = ERROR_FUNCTION_NOT_SUPPORTED;
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      UNUSED_VARIABLE(jobOptions);

      /* init variables */
      storageDirectoryListHandle->type = STORAGE_TYPE_DEVICE;

      /* open directory */
      error = File_openDirectoryList(&storageDirectoryListHandle->opticalDisk.directoryListHandle,
                                     storageName
                                    );
      break;
    case STORAGE_TYPE_DEVICE:
      UNUSED_VARIABLE(jobOptions);

      error = ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  /* free resources */
  String_delete(pathName);
  String_delete(storageSpecifier);

  return error;
}

void Storage_closeDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle)
{
  assert(storageDirectoryListHandle != NULL);

  switch (storageDirectoryListHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      File_closeDirectoryList(&storageDirectoryListHandle->fileSystem.directoryListHandle);
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        File_close(&storageDirectoryListHandle->ftp.fileHandle);
        String_delete(storageDirectoryListHandle->ftp.line);
        String_delete(storageDirectoryListHandle->ftp.fileListFileName);
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      HALT_INTERNAL_ERROR("scp does not support directory operations");
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
        libssh2_sftp_closedir(storageDirectoryListHandle->sftp.sftpHandle);
        libssh2_sftp_shutdown(storageDirectoryListHandle->sftp.sftp);
        Network_disconnect(&storageDirectoryListHandle->sftp.socketHandle);
        free(storageDirectoryListHandle->sftp.buffer);
        String_delete(storageDirectoryListHandle->sftp.pathName);
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      File_closeDirectoryList(&storageDirectoryListHandle->opticalDisk.directoryListHandle);
      break;
    case STORAGE_TYPE_DEVICE:
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }
}

bool Storage_endOfDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle)
{
  bool endOfDirectoryFlag;

  assert(storageDirectoryListHandle != NULL);

  endOfDirectoryFlag = TRUE;
  switch (storageDirectoryListHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      endOfDirectoryFlag = File_endOfDirectoryList(&storageDirectoryListHandle->fileSystem.directoryListHandle);
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        endOfDirectoryFlag = File_eof(&storageDirectoryListHandle->ftp.fileHandle);
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      HALT_INTERNAL_ERROR("scp does not support directory operations");
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
      {
        int n;

        /* read entry iff not already read */
        while (!storageDirectoryListHandle->sftp.entryReadFlag)
        {
          n = libssh2_sftp_readdir(storageDirectoryListHandle->sftp.sftpHandle,
                                   storageDirectoryListHandle->sftp.buffer,
                                   MAX_FILENAME_LENGTH,
                                   &storageDirectoryListHandle->sftp.attributes
                                  );
          if (n > 0)
          {
            if (   !S_ISDIR(storageDirectoryListHandle->sftp.attributes.flags)
                && ((n != 1) || (strncmp(storageDirectoryListHandle->sftp.buffer,".", 1) != 0))
                && ((n != 2) || (strncmp(storageDirectoryListHandle->sftp.buffer,"..",2) != 0))
               )
            {
              storageDirectoryListHandle->sftp.bufferLength = n;
              storageDirectoryListHandle->sftp.entryReadFlag = TRUE;
            }
          }
          else
          {
            break;
          }
        }

        endOfDirectoryFlag = !storageDirectoryListHandle->sftp.entryReadFlag;
      }
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      endOfDirectoryFlag = File_endOfDirectoryList(&storageDirectoryListHandle->opticalDisk.directoryListHandle);
      break;
    case STORAGE_TYPE_DEVICE:
      endOfDirectoryFlag = TRUE;
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return endOfDirectoryFlag;
}

Errors Storage_readDirectoryList(StorageDirectoryListHandle *storageDirectoryListHandle,
                                 String                     fileName,
                                 FileInfo                   *fileInfo
                                )
{
  Errors error;

  assert(storageDirectoryListHandle != NULL);

  error = ERROR_UNKNOWN;
  switch (storageDirectoryListHandle->type)
  {
    case STORAGE_TYPE_FILESYSTEM:
      error = File_readDirectoryList(&storageDirectoryListHandle->fileSystem.directoryListHandle,fileName);
      if (error == ERROR_NONE)
      {
        if (fileInfo != NULL)
        {
          error = File_getFileInfo(fileName,fileInfo);
        }
      }
      break;
    case STORAGE_TYPE_FTP:
      #ifdef HAVE_FTP
        {
          bool   readFlag;
          char   permission[32];
          uint64 size;
          uint   year,month,day;
          uint   hour,minute;
          uint   n;

          readFlag = FALSE;
          while (!readFlag && !File_eof(&storageDirectoryListHandle->ftp.fileHandle))
          {
            /* read line */
            error = File_readLine(&storageDirectoryListHandle->ftp.fileHandle,storageDirectoryListHandle->ftp.line);
            if (error != ERROR_NONE)
            {
              return error;
            }
//fprintf(stderr,"%s,%d: line=%s\n",__FILE__,__LINE__,String_cString(storageDirectoryListHandle->ftp.line));

            /* parse */
            if      (String_parse(storageDirectoryListHandle->ftp.line,
                                  STRING_BEGIN,
                                  "%32s %* %* %* %llu %d-%d-%d %d:%d %S",
                                  NULL,
                                  permission,
                                  &size,
                                  &year,&month,&day,
                                  &hour,&minute,
                                  fileName
                                 )
                    )
            {
              if (fileInfo != NULL)
              {
                n = strlen(permission);

                switch (permission[0])
                {
                  case 'd': fileInfo->type = FILE_TYPE_DIRECTORY; break;
                  default:  fileInfo->type = FILE_TYPE_FILE; break;
                }
                fileInfo->size            = size;
                fileInfo->timeLastAccess  = 0LL;
                fileInfo->timeModified    = Misc_makeDateTime(year,month,day,
                                                              hour,minute,0
                                                             );
                fileInfo->timeLastChanged = 0LL;
                fileInfo->userId          = 0;
                fileInfo->groupId         = 0;
                fileInfo->permission      = 0;
                if ((n > 1) && (permission[1] = 'r')) fileInfo->permission |= S_IRUSR;
                if ((n > 2) && (permission[2] = 'w')) fileInfo->permission |= S_IWUSR;
                if ((n > 3) && (permission[3] = 'x')) fileInfo->permission |= S_IXUSR;
                if ((n > 4) && (permission[4] = 'r')) fileInfo->permission |= S_IRGRP;
                if ((n > 5) && (permission[5] = 'w')) fileInfo->permission |= S_IWGRP;
                if ((n > 6) && (permission[6] = 'x')) fileInfo->permission |= S_IXGRP;
                if ((n > 7) && (permission[7] = 'r')) fileInfo->permission |= S_IROTH;
                if ((n > 8) && (permission[8] = 'w')) fileInfo->permission |= S_IWOTH;
                if ((n > 9) && (permission[9] = 'x')) fileInfo->permission |= S_IXOTH;
                fileInfo->major           = 0;
                fileInfo->minor           = 0;
              }

              readFlag = TRUE;
            }
            else if (String_parse(storageDirectoryListHandle->ftp.line,
                                  STRING_BEGIN,
                                  "%32s %* %* %* %llu %* %* %*:%* %S",
                                  NULL,
                                  permission,
                                  &size,
                                  fileName
                                 )
                    )
            {
              if (fileInfo != NULL)
              {
                n = strlen(permission);

                switch (permission[0])
                {
                  case 'd': fileInfo->type = FILE_TYPE_DIRECTORY; break;
                  default:  fileInfo->type = FILE_TYPE_FILE; break;
                }
                fileInfo->size            = size;
                fileInfo->timeLastAccess  = 0LL;
                fileInfo->timeModified    = 0LL;
                fileInfo->timeLastChanged = 0LL;
                fileInfo->userId          = 0;
                fileInfo->groupId         = 0;
                fileInfo->permission      = 0;
                if ((n > 1) && (permission[1] = 'r')) fileInfo->permission |= S_IRUSR;
                if ((n > 2) && (permission[2] = 'w')) fileInfo->permission |= S_IWUSR;
                if ((n > 3) && (permission[3] = 'x')) fileInfo->permission |= S_IXUSR;
                if ((n > 4) && (permission[4] = 'r')) fileInfo->permission |= S_IRGRP;
                if ((n > 5) && (permission[5] = 'w')) fileInfo->permission |= S_IWGRP;
                if ((n > 6) && (permission[6] = 'x')) fileInfo->permission |= S_IXGRP;
                if ((n > 7) && (permission[7] = 'r')) fileInfo->permission |= S_IROTH;
                if ((n > 8) && (permission[8] = 'w')) fileInfo->permission |= S_IWOTH;
                if ((n > 9) && (permission[9] = 'x')) fileInfo->permission |= S_IXOTH;
                fileInfo->major           = 0;
                fileInfo->minor           = 0;
              }

              readFlag = TRUE;
            }
            else if (String_parse(storageDirectoryListHandle->ftp.line,
                                  STRING_BEGIN,
                                  "%32s %* %* %* %llu %* %* %* %S",
                                  NULL,
                                  permission,
                                  &size,
                                  fileName
                                 )
                    )
            {
              if (fileInfo != NULL)
              {
                n = strlen(permission);

                switch (permission[0])
                {
                  case 'd': fileInfo->type = FILE_TYPE_DIRECTORY; break;
                  default:  fileInfo->type = FILE_TYPE_FILE; break;
                }
                fileInfo->size            = size;
                fileInfo->timeLastAccess  = 0LL;
                fileInfo->timeModified    = 0LL;
                fileInfo->timeLastChanged = 0LL;
                fileInfo->userId          = 0;
                fileInfo->groupId         = 0;
                fileInfo->permission      = 0;
                if ((n > 1) && (permission[1] = 'r')) fileInfo->permission |= S_IRUSR;
                if ((n > 2) && (permission[2] = 'w')) fileInfo->permission |= S_IWUSR;
                if ((n > 3) && (permission[3] = 'x')) fileInfo->permission |= S_IXUSR;
                if ((n > 4) && (permission[4] = 'r')) fileInfo->permission |= S_IRGRP;
                if ((n > 5) && (permission[5] = 'w')) fileInfo->permission |= S_IWGRP;
                if ((n > 6) && (permission[6] = 'x')) fileInfo->permission |= S_IXGRP;
                if ((n > 7) && (permission[7] = 'r')) fileInfo->permission |= S_IROTH;
                if ((n > 8) && (permission[8] = 'w')) fileInfo->permission |= S_IWOTH;
                if ((n > 9) && (permission[9] = 'x')) fileInfo->permission |= S_IXOTH;
                fileInfo->major           = 0;
                fileInfo->minor           = 0;
              }

              readFlag = TRUE;
            }
          }
        }
      #else /* not HAVE_FTP */
      #endif /* HAVE_FTP */
      break;
    case STORAGE_TYPE_SSH:
      #ifdef HAVE_SSH2
HALT_INTERNAL_ERROR_STILL_NOT_IMPLEMENTED();
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_SCP:
      HALT_INTERNAL_ERROR("scp does not support directory operations");
      break;
    case STORAGE_TYPE_SFTP:
      #ifdef HAVE_SSH2
      {
        int n;

        if (!storageDirectoryListHandle->sftp.entryReadFlag)
        {
          do
          {
            n = libssh2_sftp_readdir(storageDirectoryListHandle->sftp.sftpHandle,
                                     storageDirectoryListHandle->sftp.buffer,
                                     MAX_FILENAME_LENGTH,
                                     &storageDirectoryListHandle->sftp.attributes
                                    );
            if      (n > 0)
            {
              if (   !S_ISDIR(storageDirectoryListHandle->sftp.attributes.flags)
                  && ((n != 1) || (strncmp(storageDirectoryListHandle->sftp.buffer,".", 1) != 0))
                  && ((n != 2) || (strncmp(storageDirectoryListHandle->sftp.buffer,"..",2) != 0))
                 )
              {
                storageDirectoryListHandle->sftp.entryReadFlag = TRUE;
                error = ERROR_NONE;
              }
            }
            else
            {
              error = ERROR(IO_ERROR,errno);
            }
          }
          while (!storageDirectoryListHandle->sftp.entryReadFlag && (error == ERROR_NONE));
        }
        else
        {
          error = ERROR_NONE;
        }

        if (storageDirectoryListHandle->sftp.entryReadFlag)
        {
          String_set(fileName,storageDirectoryListHandle->sftp.pathName);
          File_appendFileNameBuffer(fileName,storageDirectoryListHandle->sftp.buffer,storageDirectoryListHandle->sftp.bufferLength);

          if (fileInfo != NULL)
          {
            if      (S_ISREG(storageDirectoryListHandle->sftp.attributes.permissions))  fileInfo->type = FILE_TYPE_FILE;
            else if (S_ISDIR(storageDirectoryListHandle->sftp.attributes.permissions))  fileInfo->type = FILE_TYPE_DIRECTORY;
            else if (S_ISLNK(storageDirectoryListHandle->sftp.attributes.permissions))  fileInfo->type = FILE_TYPE_LINK;
            else if (S_ISCHR(storageDirectoryListHandle->sftp.attributes.permissions))
            {
              fileInfo->type        = FILE_TYPE_SPECIAL;
              fileInfo->specialType = FILE_SPECIAL_TYPE_CHARACTER_DEVICE;
            }
            else if (S_ISBLK(storageDirectoryListHandle->sftp.attributes.permissions))
            {
              fileInfo->type        = FILE_TYPE_SPECIAL;
              fileInfo->specialType = FILE_SPECIAL_TYPE_BLOCK_DEVICE;
            }
            else if (S_ISFIFO(storageDirectoryListHandle->sftp.attributes.permissions))
            {
              fileInfo->type        = FILE_TYPE_SPECIAL;
              fileInfo->specialType = FILE_SPECIAL_TYPE_FIFO;
            }
            else if (S_ISSOCK(storageDirectoryListHandle->sftp.attributes.permissions))
            {
              fileInfo->type        = FILE_TYPE_SPECIAL;
              fileInfo->specialType = FILE_SPECIAL_TYPE_SOCKET;
            }
            else                                fileInfo->type = FILE_TYPE_UNKNOWN;
            fileInfo->size            = storageDirectoryListHandle->sftp.attributes.filesize;
            fileInfo->timeLastAccess  = storageDirectoryListHandle->sftp.attributes.atime;
            fileInfo->timeModified    = storageDirectoryListHandle->sftp.attributes.mtime;
            fileInfo->timeLastChanged = 0LL;
            fileInfo->userId          = storageDirectoryListHandle->sftp.attributes.uid;
            fileInfo->groupId         = storageDirectoryListHandle->sftp.attributes.gid;
            fileInfo->permission      = storageDirectoryListHandle->sftp.attributes.permissions;
            fileInfo->major           = 0;
            fileInfo->minor           = 0;
            memset(fileInfo->cast,0,sizeof(FileCast));
          }

          storageDirectoryListHandle->sftp.entryReadFlag = FALSE;
        }
      }
      #else /* not HAVE_SSH2 */
      #endif /* HAVE_SSH2 */
      break;
    case STORAGE_TYPE_CD:
    case STORAGE_TYPE_DVD:
    case STORAGE_TYPE_BD:
      error = File_readDirectoryList(&storageDirectoryListHandle->opticalDisk.directoryListHandle,fileName);
      break;
    case STORAGE_TYPE_DEVICE:
      error = ERROR_FUNCTION_NOT_SUPPORTED;
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return error;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
