/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/filesystems.c,v $
* $Revision: 1.4 $
* $Author: torsten $
* Contents: Backup ARchiver file system functions
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <features.h>
#include <errno.h>
#include <assert.h>

#include "global.h"
#include "strings.h"
//#include "stringlists.h"
#include "devices.h"
#include "errors.h"

#include "filesystems.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/
/* file system definition */
typedef struct
{
  FileSystemTypes               type;
  uint                          sizeOfHandle;
  FileSystemInitFunction        initFunction;
  FileSystemDoneFunction        doneFunction;
  FileSystemBlockIsUsedFunction blockIsUsedFunction;
} FileSystem;

/***************************** Variables *******************************/

/* define file system */
#define DEFINE_FILE_SYSTEM(name) \
  { \
    FILE_SYSTEM_TYPE_ ## name, \
    sizeof(name ## Handle), \
    (FileSystemInitFunction)name ## _init, \
    (FileSystemDoneFunction)name ## _done, \
    (FileSystemBlockIsUsedFunction)name ## _blockIsUsed, \
  }

/****************************** Macros *********************************/

/* convert from little endian to host system format */
#if __BYTE_ORDER == __LITTLE_ENDIAN
  #define LE16_TO_HOST(x) (x)
  #define LE32_TO_HOST(x) (x)
#else /* not __BYTE_ORDER == __LITTLE_ENDIAN */
  #define LE16_TO_HOST(x) swap16(x)
  #define LE32_TO_HOST(x) swap32(x)
#endif /* __BYTE_ORDER == __LITTLE_ENDIAN */

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#else /* not __BYTE_ORDER == __LITTLE_ENDIAN */
/***********************************************************************\
* Name   : swap16
* Purpose: swap 16bit value
* Input  : n - value
* Output : -
* Return : swapped value
* Notes  : -
\***********************************************************************/

LOCAL_INLINE uint16 swap16(uint16 n)
{
  return   ((n & 0xFF00U >> 8) << 0)
         | ((n & 0x00FFU >> 0) << 8)
         ;
}

/***********************************************************************\
* Name   : swap32
* Purpose: swap 32bit value
* Input  : n - value
* Output : -
* Return : swapped value
* Notes  : -
\***********************************************************************/

LOCAL_INLINE uint32 swap32(uint32 n)
{
  return   ((n & 0xFF000000U >> 24) <<  0)
         | ((n & 0x00FF0000U >> 16) <<  8)
         | ((n & 0x0000FF00U >>  8) << 16)
         | ((n & 0x000000FFU >>  0) << 24)
         ;
}
#endif /* __BYTE_ORDER == __LITTLE_ENDIAN */

#include "filesystems_ext.c"
#include "filesystems_fat.c"
#include "filesystems_reiserfs.c"

/* support file systems */
LOCAL FileSystem FILE_SYSTEMS[] =
{
  DEFINE_FILE_SYSTEM(EXT),
  DEFINE_FILE_SYSTEM(FAT),
  DEFINE_FILE_SYSTEM(REISERFS),
};

/*---------------------------------------------------------------------*/

Errors FileSystem_init(FileSystemHandle *fileSystemHandle,
                       DeviceHandle     *deviceHandle
                      )
{
  void *handle;
  int  z;

  assert(fileSystemHandle != NULL);
  assert(deviceHandle != NULL);

  /* initialize variables */
  fileSystemHandle->deviceHandle        = deviceHandle;
  fileSystemHandle->type                = FILE_SYSTEM_TYPE_UNKNOWN;
  fileSystemHandle->handle              = NULL;
  fileSystemHandle->doneFunction        = NULL;
  fileSystemHandle->blockIsUsedFunction = NULL;

  /* detect file system on device */
  z = 0;
  while ((z < SIZE_OF_ARRAY(FILE_SYSTEMS)) && (fileSystemHandle->type == FILE_SYSTEM_TYPE_UNKNOWN))
  {
    handle = malloc(FILE_SYSTEMS[z].sizeOfHandle);
    if (handle == NULL)
    {
      HALT_INSUFFICIENT_MEMORY();
    }

    if (FILE_SYSTEMS[z].initFunction(deviceHandle,handle))
    {
      fileSystemHandle->type                = FILE_SYSTEMS[z].type;
      fileSystemHandle->handle              = handle;
      fileSystemHandle->doneFunction        = FILE_SYSTEMS[z].doneFunction;
      fileSystemHandle->blockIsUsedFunction = FILE_SYSTEMS[z].blockIsUsedFunction;
    }
    else
    {
      free(handle);
    }
    z++;
  }

  return ERROR_NONE;
}

Errors FileSystem_done(FileSystemHandle *fileSystemHandle)
{
  assert(fileSystemHandle != NULL);

  if (fileSystemHandle->doneFunction != NULL)
  {
    fileSystemHandle->doneFunction(fileSystemHandle->deviceHandle,fileSystemHandle->handle);
  }
  if (fileSystemHandle->handle != NULL)
  {
    free(fileSystemHandle->handle);
  }

  return ERROR_NONE;
}

bool FileSystem_blockIsUsed(FileSystemHandle *fileSystemHandle, uint64 offset)
{
  assert(fileSystemHandle != NULL);

  if (fileSystemHandle->blockIsUsedFunction != NULL)
  {
    return fileSystemHandle->blockIsUsedFunction(fileSystemHandle->deviceHandle,fileSystemHandle->handle,offset);
  }
  else
  {
    return TRUE;
  }
}

#ifdef __cplusplus
  }
#endif

/* end of file */
