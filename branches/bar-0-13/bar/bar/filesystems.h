/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/filesystems.h,v $
* $Revision: 1.2 $
* $Author: torsten $
* Contents: Backup ARchiver file system functions
* Systems: all
*
\***********************************************************************/

#ifndef __FILESYSTEMS__
#define __FILESYSTEMS__

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "global.h"
#include "strings.h"
#include "bitmaps.h"
#include "devices.h"
#include "errors.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/* supported file systems */
typedef enum
{
  FILE_SYSTEM_TYPE_NONE,

  FILE_SYSTEM_TYPE_EXT,
  FILE_SYSTEM_TYPE_FAT,
  FILE_SYSTEM_TYPE_REISERFS,

  FILE_SYSTEM_TYPE_UNKNOWN,
} FileSystemTypes;

/***************************** Datatypes *******************************/

/* file system functions */
typedef bool(*FileSystemInitFunction)(DeviceHandle *deviceHandle, void *handle);
typedef void(*FileSystemDoneFunction)(DeviceHandle *deviceHandle, void *handle);
typedef bool(*FileSystemBlockIsUsedFunction)(DeviceHandle *deviceHandle, void *handle, uint64 offset);

/* file system handle */
typedef struct
{
  DeviceHandle                  *deviceHandle;
  FileSystemTypes               type;
  void                          *handle;
  FileSystemDoneFunction        doneFunction;
  FileSystemBlockIsUsedFunction blockIsUsedFunction;
} FileSystemHandle;

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : FileSystem_init
* Purpose: init file system
* Input  : fileSystemHandle - file system handle
*          deviceHandle     - device handle
* Output : fileSystemHandle - file system handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors FileSystem_init(FileSystemHandle *fileSystemHandle,
                       DeviceHandle     *deviceHandle
                      );

/***********************************************************************\
* Name   : FileSystem_close
* Purpose: close file system
* Input  : fileSystemHandle - file system handle
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors FileSystem_done(FileSystemHandle *fileSystemHandle);

/***********************************************************************\
* Name   : FileSystem_blockIsUsed
* Purpose: check if block is used by file system
* Input  : fileSystemHandle - file system handle
*          offset           - offset (byte position) (0..n-1)
* Output : -
* Return : TRUE if block is used, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool FileSystem_blockIsUsed(FileSystemHandle *fileSystemHandle,
                            uint64           offset
                           );

#ifdef __cplusplus
  }
#endif

#endif /* __FILESYSTEMS__ */

/* end of file */
