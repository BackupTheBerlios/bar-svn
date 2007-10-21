/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/files.c,v $
* $Revision: 1.19 $
* $Author: torsten $
* Contents: Backup ARchiver file functions
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
//#include <fcntl.h>
//#include <sys/ioctl.h>
#include <unistd.h>
#include <dirent.h>
#include <utime.h>
#include <errno.h>
#include <assert.h>

#include "global.h"
#include "strings.h"

#include "files.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
#define MAX_BUFFER_SIZE (64*1024)

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

String File_setFileName(String fileName, const String name)
{
  assert(fileName != NULL);

  return String_set(fileName,name);
}

String File_setFileNameCString(String fileName, const char *name)
{
  assert(fileName != NULL);

  return String_setCString(fileName,name);
}

String File_setFileNameChar(String fileName, char ch)
{
  assert(fileName != NULL);

  return String_setChar(fileName,ch);
}

String File_appendFileName(String fileName, const String name)
{
  assert(fileName != NULL);
  assert(name != NULL);

  if (String_length(fileName) > 0)
  {
    if (String_index(fileName,String_length(fileName)-1) != FILES_PATHNAME_SEPARATOR_CHAR)
    {
      String_appendChar(fileName,FILES_PATHNAME_SEPARATOR_CHAR);
    }
  }
  String_append(fileName,name);
  
  return fileName;
}

String File_appendFileNameCString(String fileName, const char *name)
{
  assert(fileName != NULL);
  assert(name != NULL);

  if (String_length(fileName) > 0)
  {
    if (String_index(fileName,String_length(fileName)-1) != FILES_PATHNAME_SEPARATOR_CHAR)
    {
      String_appendChar(fileName,FILES_PATHNAME_SEPARATOR_CHAR);
    }
  }
  String_appendCString(fileName,name);
  
  return fileName;
}

String File_appendFileNameChar(String fileName, char ch)
{
  assert(fileName != NULL);

  if (String_length(fileName) > 0)
  {
    if (String_index(fileName,String_length(fileName)-1) != FILES_PATHNAME_SEPARATOR_CHAR)
    {
      String_appendChar(fileName,FILES_PATHNAME_SEPARATOR_CHAR);
    }
  }
  String_appendChar(fileName,ch);

  return fileName;
}

String File_appendFileNameBuffer(String fileName, const char *buffer, ulong bufferLength)
{
  assert(fileName != NULL);

  if (String_length(fileName) > 0)
  {
    if (String_index(fileName,String_length(fileName)-1) != FILES_PATHNAME_SEPARATOR_CHAR)
    {
      String_appendChar(fileName,FILES_PATHNAME_SEPARATOR_CHAR);
    }
  }
  String_appendBuffer(fileName,buffer,bufferLength);

  return fileName;
}

String File_getFilePathName(const String fileName, String pathName)
{
  long lastPathSeparatorIndex;

  assert(fileName != NULL);
  assert(pathName != NULL);

  /* find last path separator */
  lastPathSeparatorIndex = String_findLastChar(fileName,STRING_END,FILES_PATHNAME_SEPARATOR_CHAR);

  /* get path */
  if (lastPathSeparatorIndex >= 0)
  {
    String_sub(pathName,fileName,0,lastPathSeparatorIndex);
  }
  else
  {
    String_clear(pathName);
  }

  return pathName;
}

String File_getFileBaseName(const String fileName, String baseName)
{
  long lastPathSeparatorIndex;

  assert(fileName != NULL);
  assert(baseName != NULL);

  /* find last path separator */
  lastPathSeparatorIndex = String_findLastChar(fileName,STRING_END,FILES_PATHNAME_SEPARATOR_CHAR);

  /* get path */
  if (lastPathSeparatorIndex >= 0)
  {
    String_sub(baseName,fileName,lastPathSeparatorIndex+1,STRING_END);
  }
  else
  {
    String_clear(baseName);
  }

  return baseName;
}

void File_splitFileName(const String fileName, String *pathName, String *baseName)
{
  assert(fileName != NULL);
  assert(pathName != NULL);
  assert(baseName != NULL);

  (*pathName) = File_getFilePathName(fileName,String_new());
  (*baseName) = File_getFileBaseName(fileName,String_new());
}

void File_initSplitFileName(StringTokenizer *stringTokenizer, String fileName)
{
  assert(stringTokenizer != NULL);
  assert(fileName != NULL);

  String_initTokenizer(stringTokenizer,fileName,FILES_PATHNAME_SEPARATOR_CHARS,NULL,FALSE);
}

void File_doneSplitFileName(StringTokenizer *stringTokenizer)
{
  assert(stringTokenizer != NULL);

  String_doneTokenizer(stringTokenizer);
}

bool File_getNextSplitFileName(StringTokenizer *stringTokenizer, String *const name)
{
  assert(stringTokenizer != NULL);
  assert(name != NULL);

  return String_getNextToken(stringTokenizer,name,NULL);
}

/*---------------------------------------------------------------------*/

bool File_getTmpFileName(const char *directory, String fileName)
{
  char *s;
  int  handle;

  assert(fileName != NULL);

  if (directory != NULL)
  {
    String_format(fileName,"%s%cbar-XXXXXXX",directory,FILES_PATHNAME_SEPARATOR_CHAR);
  }
  else
  {
    String_format(fileName,"bar-XXXXXXX");
  }
  s = String_toCString(fileName);
  if (s == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  handle = mkstemp(s);
  if (handle == -1)
  {
    free(s);
    return FALSE;
  }
  close(handle);

  String_setBuffer(fileName,s,strlen(s));

  free(s);

  return TRUE;
}

bool File_getTmpDirectoryName(const char *directory, String fileName)
{
  char *s;

  assert(fileName != NULL);

  if (directory != NULL)
  {
    String_format(fileName,"%s%cbar-XXXXXXX",directory,FILES_PATHNAME_SEPARATOR_CHAR);
  }
  else
  {
    String_format(fileName,"bar-XXXXXXX");
  }
  s = String_toCString(fileName);
  if (s == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }

  if (mkdtemp(s) == NULL)
  {
    free(s);
    return FALSE;
  }

  String_setBuffer(fileName,s,strlen(s));

  free(s);

  return TRUE;
}

/*---------------------------------------------------------------------*/

Errors File_open(FileHandle    *fileHandle,
                 const String  fileName,
                 FileOpenModes fileOpenMode
                )
{
  off_t  n;
  Errors error;
  String pathName;

  assert(fileHandle != NULL);
  assert(fileName != NULL);

  fileHandle->index = 0;

  switch (fileOpenMode)
  {
    case FILE_OPENMODE_CREATE:
      /* create file */
      fileHandle->file = fopen(String_cString(fileName),"wb");
      if (fileHandle->file == NULL)
      {
        return ERROR_CREATE_FILE;
      }

      fileHandle->size  = 0;
      break;
    case FILE_OPENMODE_READ:
      /* open file for reading */
      fileHandle->file = fopen(String_cString(fileName),"rb");
      if (fileHandle->file == NULL)
      {
        return ERROR_OPEN_FILE;
      }

      /* get file size */
      if (fseeko(fileHandle->file,(off_t)0,SEEK_END) == -1)
      {
        fclose(fileHandle->file);
        return ERROR_IO_ERROR;
      }
      n = ftello(fileHandle->file);
      if (n == (off_t)-1)
      {
        fclose(fileHandle->file);
        return ERROR_IO_ERROR;
      }
      fileHandle->size = (uint64)n;
      if (fseeko(fileHandle->file,(off_t)0,SEEK_SET) == -1)
      {
        fclose(fileHandle->file);
        return ERROR_IO_ERROR;
      }
      break;
    case FILE_OPENMODE_WRITE:
      /* create directory if needed */
      pathName = File_getFilePathName(fileName,String_new());
      if (!File_exists(pathName))
      {
        error = File_makeDirectory(pathName);
        if (error != ERROR_NONE)
        {
          return error;
        }
      }
      String_delete(pathName);

      /* open existing file for writing */
      fileHandle->file = fopen(String_cString(fileName),"r+b");
      if (fileHandle->file == NULL)
      {
        if (errno == ENOENT)
        {
          fileHandle->file = fopen(String_cString(fileName),"wb");
          if (fileHandle->file == NULL)
          {
            return ERROR_OPEN_FILE;
          }
        }
        else
        {
          return ERROR_OPEN_FILE;
        }
      }

      fileHandle->size  = 0;
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

Errors File_openDescriptor(FileHandle    *fileHandle,
                           int           fileDescriptor,
                           FileOpenModes fileOpenMode
                          )
{
  assert(fileHandle != NULL);
  assert(fileDescriptor >= 0);

  fileHandle->index = 0;
  fileHandle->size  = 0;

  switch (fileOpenMode)
  {
    case FILE_OPENMODE_CREATE:
      /* create file */
      fileHandle->file = fdopen(fileDescriptor,"wb");
      if (fileHandle->file == NULL)
      {
        return ERROR_CREATE_FILE;
      }

      fileHandle->size  = 0;
      break;
    case FILE_OPENMODE_READ:
      /* open file for reading */
      fileHandle->file = fdopen(fileDescriptor,"rb");
      if (fileHandle->file == NULL)
      {
        return ERROR_OPEN_FILE;
      }
      break;
    case FILE_OPENMODE_WRITE:
      /* open file for writing */
      fileHandle->file = fdopen(fileDescriptor,"r+b");
      if (fileHandle->file == NULL)
      {
        return ERROR_OPEN_FILE;
      }

      fileHandle->size  = 0;
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return ERROR_NONE;
}

Errors File_close(FileHandle *fileHandle)
{
  assert(fileHandle != NULL);
  assert(fileHandle->file != NULL);

  fclose(fileHandle->file);
  fileHandle->file = NULL;

  return ERROR_NONE;
}

bool File_eof(FileHandle *fileHandle)
{
  int  ch;
  bool eofFlag;

  assert(fileHandle != NULL);

  ch = getc(fileHandle->file);
  if (ch != EOF)
  {
    ungetc(ch,fileHandle->file);
    eofFlag = FALSE;
  }
  else
  {
    eofFlag = TRUE;
  }

  return eofFlag;
}

Errors File_read(FileHandle *fileHandle,
                 void       *buffer,
                 ulong      bufferLength,
                 ulong      *bytesRead
                )
{
  ssize_t n;

  assert(fileHandle != NULL);
  assert(bytesRead != NULL);

  n = fread(buffer,1,bufferLength,fileHandle->file);
  if ((n <= 0) && ferror(fileHandle->file))
  {
    return ERROR_IO_ERROR;
  }
  fileHandle->index += n;

  (*bytesRead) = n;

  return ERROR_NONE;
}

Errors File_write(FileHandle *fileHandle,
                  const void *buffer,
                  ulong      bufferLength
                 )
{
  ssize_t n;

  assert(fileHandle != NULL);

  n = fwrite(buffer,1,bufferLength,fileHandle->file);
  if (n > 0) fileHandle->index += n;
  if (fileHandle->index > fileHandle->size) fileHandle->size = fileHandle->index;
  if (n != bufferLength)
  {
    return ERROR_IO_ERROR;
  }

  return ERROR_NONE;
}

Errors File_readLine(FileHandle *fileHandle,
                     String     line
                    )
{
  ssize_t n;
  int     ch;

  assert(fileHandle != NULL);

  String_clear(line);
  do
  {
    ch = getc(fileHandle->file);
    if (n > 0) fileHandle->index += n;
    if (n < 0)
    {
      return ERROR_IO_ERROR;
    }
    if (((char)ch != '\n') && ((char)ch != '\r'))
    {
      String_appendChar(line,ch);
    }
  }
  while ((char)ch != '\n');

  return ERROR_NONE;
}

Errors File_writeLine(FileHandle   *fileHandle,
                      const String line
                     )
{
  assert(fileHandle != NULL);

  return File_write(fileHandle,String_cString(line),String_length(line));
}

uint64 File_getSize(FileHandle *fileHandle)
{
  assert(fileHandle != NULL);

  return fileHandle->size;
}

Errors File_tell(FileHandle *fileHandle, uint64 *offset)
{
  off_t n;

  assert(fileHandle != NULL);
  assert(offset != NULL);

  n = ftello(fileHandle->file);
  if (n == (off_t)-1)
  {
    return ERROR_IO_ERROR;
  }
// NYI
assert(sizeof(off_t)==8);
assert(n == (off_t)fileHandle->index);

  (*offset) = fileHandle->index;

  return ERROR_NONE;
}

Errors File_seek(FileHandle *fileHandle,
                 uint64     offset
                )
{
  assert(fileHandle != NULL);

  if (fseeko(fileHandle->file,(off_t)offset,SEEK_SET) == -1)
  {
    return ERROR_IO_ERROR;
  }
  fileHandle->index = offset;

  return ERROR_NONE;
}

/*---------------------------------------------------------------------*/

Errors File_makeDirectory(const String pathName)
{
  StringTokenizer pathNameTokenizer;
  String          directoryName;
  String          name;

  assert(pathName != NULL);

  directoryName = String_new();
  File_initSplitFileName(&pathNameTokenizer,pathName);
  if (File_getNextSplitFileName(&pathNameTokenizer,&name))
  {
    if (String_length(name) > 0)
    {
      File_setFileName(directoryName,name);
    }
    else
    {
      File_setFileNameChar(directoryName,FILES_PATHNAME_SEPARATOR_CHAR);
    }
  }
  while (File_getNextSplitFileName(&pathNameTokenizer,&name))
  {
    if (String_length(name) > 0)
    {     
      File_appendFileName(directoryName,name);

      if (!File_exists(directoryName))
      {
        if (mkdir(String_cString(directoryName),0700) != 0)
        {
          File_doneSplitFileName(&pathNameTokenizer);
          String_delete(directoryName);
          return ERROR_IO_ERROR;
        }
      }
    }
  }
  File_doneSplitFileName(&pathNameTokenizer);
  String_delete(directoryName);

  return ERROR_NONE;
}

Errors File_openDirectory(DirectoryHandle *directoryHandle,
                          const String    pathName
                         )
{
  assert(directoryHandle != NULL);
  assert(pathName != NULL);

  directoryHandle->dir = opendir(String_cString(pathName));
  if (directoryHandle->dir == NULL)
  {
    return ERROR_OPEN_DIRECTORY;
  }

  directoryHandle->name  = String_copy(pathName);
  directoryHandle->entry = NULL;

  return ERROR_NONE;
}

void File_closeDirectory(DirectoryHandle *directoryHandle)
{
  assert(directoryHandle != NULL);

  closedir(directoryHandle->dir);
  String_delete(directoryHandle->name);
}

bool File_endOfDirectory(DirectoryHandle *directoryHandle)
{
  assert(directoryHandle != NULL);

  /* read entry iff not read */
  if (directoryHandle->entry == NULL)
  {
    directoryHandle->entry = readdir(directoryHandle->dir);
  }

  /* skip "." and ".." entries */
  while (   (directoryHandle->entry != NULL)
         && (   (strcmp(directoryHandle->entry->d_name,"." ) == 0)
             || (strcmp(directoryHandle->entry->d_name,"..") == 0)
            )
        )
  {
    directoryHandle->entry = readdir(directoryHandle->dir);
  }

  return directoryHandle->entry == NULL;
}

Errors File_readDirectory(DirectoryHandle *directoryHandle,
                          String          fileName
                         )
{
  assert(directoryHandle != NULL);
  assert(fileName != NULL);

  /* read entry iff not read */
  if (directoryHandle->entry == NULL)
  {
    directoryHandle->entry = readdir(directoryHandle->dir);
  }

  /* skip "." and ".." entries */
  while (   (directoryHandle->entry != NULL)
         && (   (strcmp(directoryHandle->entry->d_name,"." ) == 0)
             || (strcmp(directoryHandle->entry->d_name,"..") == 0)
            )
        )
  {
    directoryHandle->entry = readdir(directoryHandle->dir);
  }
  if (directoryHandle->entry == NULL)
  {
    return ERROR_IO_ERROR;
  }

  String_set(fileName,directoryHandle->name);
  File_appendFileNameCString(fileName,directoryHandle->entry->d_name);

  directoryHandle->entry = NULL;

  return ERROR_NONE;
}

Errors File_openDevices(DeviceHandle *deviceHandle)
{
  assert(deviceHandle != NULL);

  deviceHandle->file = fopen("/etc/mtab","r");
  if (deviceHandle->file == NULL)
  {
    return ERROR_OPEN_FILE;
  }
  deviceHandle->bufferFilledFlag = FALSE;

  return ERROR_NONE;
}

void File_closeDevices(DeviceHandle *deviceHandle)
{
  assert(deviceHandle != NULL);

  fclose(deviceHandle->file);
}

bool File_endOfDevices(DeviceHandle *deviceHandle)
{
  assert(deviceHandle != NULL);

  if (!deviceHandle->bufferFilledFlag)
  {
    if (fgets(deviceHandle->buffer,sizeof(deviceHandle->buffer),deviceHandle->file) == NULL)
    {
      return TRUE;
    }
    deviceHandle->bufferFilledFlag = TRUE;
  }

  return (feof(deviceHandle->file) != 0);
}

Errors File_readDevice(DeviceHandle *deviceHandle,
                       String       deviceName
                      )
{
  char *s0,*s1;

  assert(deviceHandle != NULL);
  assert(deviceName != NULL);

  if (!deviceHandle->bufferFilledFlag)
  {
    /* read line */
    if (fgets(deviceHandle->buffer,sizeof(deviceHandle->buffer),deviceHandle->file) == NULL)
    {
      return ERROR_IO_ERROR;
    }
    deviceHandle->bufferFilledFlag = TRUE;
  }

  /* parse */
  s0 = strchr(&deviceHandle->buffer[0],' ');
  if (s0 == NULL)
  {
    return ERROR_IO_ERROR;
  }
  s1 = strchr(s0+1,' ');
  if (s1 == NULL)
  {
    return ERROR_IO_ERROR;
  }
  assert(s1 > s0);
  String_setBuffer(deviceName,s0+1,s1-s0-1);

  deviceHandle->bufferFilledFlag = FALSE;

  return ERROR_NONE;
}

FileTypes File_getType(String fileName)
{
  struct stat64 fileStat;

  assert(fileName != NULL);

  if (lstat64(String_cString(fileName),&fileStat) == 0)
  {
    if      (S_ISREG(fileStat.st_mode)) return FILE_TYPE_FILE;
    else if (S_ISDIR(fileStat.st_mode)) return FILE_TYPE_DIRECTORY;
    else if (S_ISLNK(fileStat.st_mode)) return FILE_TYPE_LINK;
    else                                return FILE_TYPE_UNKNOWN;
  }
  else
  {
    return FILE_TYPE_UNKNOWN;
  }
}

bool File_delete(String fileName)
{
  struct stat64 fileStat;

  assert(fileName != NULL);

  if (lstat64(String_cString(fileName),&fileStat) == 0)
  {
    if      (   S_ISREG(fileStat.st_mode)
             || S_ISLNK(fileStat.st_mode)
            )
    {
      return (unlink(String_cString(fileName)) == 0);
    }
    else if (S_ISDIR(fileStat.st_mode))
    {
      return (rmdir(String_cString(fileName)) == 0);
    }
    else
    {
      return FALSE;
    }
  }
  else
  {
    return FALSE;
  }
}

bool File_rename(String oldFileName,
                 String newFileName
                )
{
  assert(oldFileName != NULL);
  assert(newFileName != NULL);

  return (rename(String_cString(oldFileName),String_cString(newFileName)) == 0);
}

bool File_copy(String sourceFileName,
               String destinationFileName
              )
{
  #define BUFFER_SIZE (1024*1024)

  byte   *buffer;
  FILE   *sourceFile,*destinationFile;
  size_t n;

  assert(sourceFileName != NULL);
  assert(destinationFileName != NULL);

  /* allocate buffer */
  buffer = (byte*)malloc(BUFFER_SIZE);
  if (buffer == NULL)
  {
    return FALSE;
  }

  /* open files */
  sourceFile = fopen(String_cString(sourceFileName),"r");
  if (sourceFile == NULL)
  {
    free(buffer);
    return FALSE;
  }
  destinationFile = fopen(String_cString(destinationFileName),"w");
  if (destinationFile == NULL)
  {
    fclose(sourceFile);
    free(buffer);
    return FALSE;
  }

  /* copy data */
  do
  {
    n = fread(buffer,1,BUFFER_SIZE,sourceFile);
    if (n > 0)
    {
      if (fwrite(buffer,1,n,destinationFile) != n)
      {
        fclose(destinationFile);
        fclose(sourceFile);
        free(buffer);
        return FALSE;
      }
    }
    else
    {
      if (ferror(sourceFile))
      {
        fclose(destinationFile);
        fclose(sourceFile);
        free(buffer);
        return FALSE;
      }
    }
  }
  while (n > 0);

  /* close files */
  fclose(destinationFile);
  fclose(sourceFile);

  /* free resources */
  free(buffer);

  return TRUE;

  #undef BUFFER_SIZE
}

bool File_exists(String fileName)
{
  struct stat fileStat;

  assert(fileName != NULL);

  return (stat(String_cString(fileName),&fileStat) == 0);
}

Errors File_getFileInfo(String   fileName,
                        FileInfo *fileInfo
                       )
{
  struct stat64 fileStat;

  assert(fileName != NULL);
  assert(fileInfo != NULL);

  if (lstat64(String_cString(fileName),&fileStat) != 0)
  {
    return ERROR_IO_ERROR;
  }

  fileInfo->size            = fileStat.st_size;
  fileInfo->timeLastAccess  = fileStat.st_atime;
  fileInfo->timeModified    = fileStat.st_mtime;
  fileInfo->timeLastChanged = fileStat.st_ctime;
  fileInfo->userId          = fileStat.st_uid;
  fileInfo->groupId         = fileStat.st_gid;
  fileInfo->permission      = fileStat.st_mode;

  return ERROR_NONE;
}

Errors File_setFileInfo(String   fileName,
                        FileInfo *fileInfo
                       )
{
  struct utimbuf utimeBuffer;

  assert(fileName != NULL);
  assert(fileInfo != NULL);

  utimeBuffer.actime  = fileInfo->timeLastAccess;
  utimeBuffer.modtime = fileInfo->timeModified;
  if (utime(String_cString(fileName),&utimeBuffer) != 0)
  {
    return ERROR_IO_ERROR;
  }
  if (chown(String_cString(fileName),fileInfo->userId,fileInfo->groupId) != 0)
  {
    return ERROR_IO_ERROR;
  }
  if (chmod(String_cString(fileName),fileInfo->permission) != 0)
  {
    return ERROR_IO_ERROR;
  }

  return ERROR_NONE;
}

Errors File_readLink(String linkName,
                     String fileName
                    )
{
  #define BUFFER_SIZE  256
  #define BUFFER_DELTA 128

  char *buffer;
  uint bufferSize;
  int  result;

  assert(linkName != NULL);
  assert(fileName != NULL);

  /* allocate initial buffer for name */
  buffer = (char*)malloc(BUFFER_SIZE);
  if (buffer == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }  
  bufferSize = BUFFER_SIZE;

  /* try to read link, increase buffer if needed */
  while ((result = readlink(String_cString(linkName),buffer,bufferSize)) == bufferSize)
  {
    bufferSize += BUFFER_DELTA;
    buffer = realloc(buffer,bufferSize);
    if (buffer == NULL)
    {
      HALT_INSUFFICIENT_MEMORY();
    }  
  }

  if (result != -1)
  {
    String_setBuffer(fileName,buffer,result);
    free(buffer);
    return ERROR_NONE;
  }
  else
  {
    free(buffer);
    return ERROR_IO_ERROR;
  }
}

Errors File_link(String linkName,
                 String fileName
                )
{
  assert(linkName != NULL);
  assert(fileName != NULL);

  unlink(String_cString(linkName));
  if (symlink(String_cString(fileName),String_cString(linkName)) != 0)
  {
    return ERROR_IO_ERROR;
  }

  return ERROR_NONE;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
