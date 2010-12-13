/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/index.c,v $
* $Revision: 1.1.2.1 $
* $Author: torsten $
* Contents: database index functions
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "global.h"
#include "strings.h"
#include "database.h"
#include "errors.h"

#include "bar.h"
#include "database_definition.h"

#include "index.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
const char* INDEX_STATE_STRINGS[7] =
{
  "NONE",
  "OK",
  "CREATE",
  "INDEX_UPDATE_REQUESTED",
  "INDEX_UPDATE",
  "ERROR",
  "UNKNOWN"
};

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : getREGEXPString
* Purpose: get REGEXP filter string
* Input  : string      - string variable
*          columnName  - column name
*          patternText - pattern text
* Output : -
* Return : string for WHERE statement
* Notes  : -
\***********************************************************************/

LOCAL String getREGEXPString(String string, const char *columnName, const String patternText)
{
  StringTokenizer stringTokenizer;
  String          token;
  ulong           z;
  char            ch;

  String_setCString(string,"1");
  if (patternText != NULL)
  {
    String_initTokenizer(&stringTokenizer,
                         patternText,
                         STRING_BEGIN,
                         STRING_WHITE_SPACES,
                         STRING_QUOTES,
                         TRUE
                        );
    while (String_getNextToken(&stringTokenizer,&token,NULL))
    {
      String_appendCString(string," AND REGEXP('");
      z = 0;
      while (z < String_length(token))
      {
        ch = String_index(token,z);
        switch (ch)
        {
          case '.':
          case '[':
          case ']':
          case '(':
          case ')':
          case '{':
          case '}':
          case '+':
          case '|':
          case '^':
          case '$':
          case '\\':
            String_appendChar(string,'\\');
            String_appendChar(string,ch);
            z++;
            break;
          case '*':
            String_appendCString(string,".*");
            z++;
            break;
          case '?':
            String_appendChar(string,'.');
            z++;
            break;
          case '\'':
            String_appendCString(string,"''");
            z++;
            break;
          default:
            String_appendChar(string,ch);
            z++;
            break;
        }
      }
      String_format(string,"',0,%s)",columnName);
    }
    String_doneTokenizer(&stringTokenizer);
  }

  return string;
}

/*---------------------------------------------------------------------*/

Errors Index_initAll(void)
{
  return ERROR_NONE;
}

void Index_doneAll(void)
{
}

Errors Index_init(DatabaseHandle *indexDatabaseHandle,
                  const char     *indexDatabaseFileName
                 )
{
  Errors error;

  /* open/create datbase */
  if (File_existsCString(indexDatabaseFileName))
  {
    /* open index database */
    error = Database_open(indexDatabaseHandle,indexDatabaseFileName,DATABASE_OPENMODE_READWRITE);
    if (error != ERROR_NONE)
    {
      return error;
    }
  }
  else
  {
    /* create index database */
    error = Database_open(indexDatabaseHandle,indexDatabaseFileName,DATABASE_OPENMODE_CREATE);
    if (error != ERROR_NONE)
    {
      return error;
    }
    error = Database_execute(indexDatabaseHandle,
                             NULL,
                             NULL,
                             DATABASE_TABLE_DEFINITION
                            );
    if (error != ERROR_NONE)
    {
      Database_close(indexDatabaseHandle);
      return error;
    }
  }

  /* disable synchronous mode and journal to increase transaction speed */
  Database_execute(indexDatabaseHandle,
                   NULL,
                   NULL,
                   "PRAGMA synchronous=OFF;"
                  );
  Database_execute(indexDatabaseHandle,
                   NULL,
                   NULL,
                   "PRAGMA journal_mode=OFF;"
                  );

  return ERROR_NONE;
}

void Index_done(DatabaseHandle *indexDatabaseHandle)
{
  assert(indexDatabaseHandle != NULL);

  Database_close(indexDatabaseHandle);
}

bool Index_findByName(DatabaseHandle *databaseHandle,
                      const String   name,
                      int64          *storageId,
                      IndexStates    *indexState,
                      uint64         *lastChecked
                     )
{
  DatabaseQueryHandle databaseQueryHandle;
  Errors              error;
  bool                result;

  assert(storageId != NULL);
  assert(databaseHandle != NULL);

  (*storageId) = DATABASE_ID_NONE;

  error = Database_prepare(&databaseQueryHandle,
                           databaseHandle,
                           "SELECT id, \
                                   state, \
                                   lastChecked \
                            FROM storage \
                            WHERE name=%'S \
                           ",
                           name
                          );
  if (error != ERROR_NONE)
  {
    return FALSE;
  }
  if (Database_getNextRow(&databaseQueryHandle,
                          "%ld %d %ld",
                          storageId,
                          indexState,
                          lastChecked
                         )
     )
  {
    if (name != NULL) String_clear(name);

    result = TRUE;
  }
  else
  {
    result = FALSE;
  }
  Database_finalize(&databaseQueryHandle);

  return result;
}

bool Index_findByState(DatabaseHandle *databaseHandle,
                       IndexStates    indexState,
                       int64          *storageId,
                       String         name,
                       uint64         *lastChecked
                      )
{
  DatabaseQueryHandle databaseQueryHandle;
  Errors              error;
  bool                result;

  assert(storageId != NULL);
  assert(databaseHandle != NULL);

  (*storageId) = DATABASE_ID_NONE;

  error = Database_prepare(&databaseQueryHandle,
                           databaseHandle,
                           "SELECT id, \
                                   name \
                                   lastChecked \
                            FROM storage \
                            WHERE state=%d \
                           ",
                           indexState
                          );
  if (error != ERROR_NONE)
  {
    return FALSE;
  }
  if (Database_getNextRow(&databaseQueryHandle,
                          "%ld %S %ld",
                          storageId,
                          &name,
                          lastChecked
                         )
     )
  {
    if (name != NULL) String_clear(name);

    result = TRUE;
  }
  else
  {
    result = FALSE;
  }
  Database_finalize(&databaseQueryHandle);

  return result;
}

Errors Index_create(DatabaseHandle *databaseHandle,
                    const String   name,
                    IndexStates    indexState,
                    IndexModes     indexMode,
                    int64          *storageId
                   )
{
  Errors error;

  assert(storageId != NULL);
  assert(databaseHandle != NULL);

  error = Database_execute(databaseHandle,
                           NULL,
                           NULL,
                           "INSERT INTO storage \
                              (\
                               name,\
                               size,\
                               created,\
                               state,\
                               mode,\
                               lastChecked\
                              ) \
                            VALUES \
                             (\
                              %'S,\
                              0,\
                              DATETIME('now'),\
                              %d,\
                              %d,\
                              DATETIME('now')\
                             ); \
                           ",
                           name,
                           indexState,
                           indexMode
                          );
  if (error != ERROR_NONE)
  {
    return error;
  }
  (*storageId) = Database_getLastRowId(indexDatabaseHandle);

  return ERROR_NONE;
}

Errors Index_delete(DatabaseHandle *databaseHandle,
                    int64          storageId
                   )
{
  assert(databaseHandle != NULL);
  assert(storageId != 0LL);

  return Database_execute(databaseHandle,
                          NULL,
                          NULL,
                          "\
                           DELETE FROM files WHERE storageId=%ld;\
                           DELETE FROM images WHERE storageId=%ld;\
                           DELETE FROM directories WHERE storageId=%ld;\
                           DELETE FROM links WHERE storageId=%ld;\
                           DELETE FROM special WHERE storageId=%ld;\
                           DELETE FROM storage WHERE id=%ld;\
                          ",
                          storageId,
                          storageId,
                          storageId,
                          storageId,
                          storageId,
                          storageId
                         );
}

Errors Index_clear(DatabaseHandle *databaseHandle,
                   int64          storageId
                  )
{
  assert(databaseHandle != NULL);
  assert(storageId != 0LL);

  return Database_execute(databaseHandle,
                          NULL,
                          NULL,
                          "\
                           DELETE FROM files WHERE storageId=%ld;\
                           DELETE FROM images WHERE storageId=%ld;\
                           DELETE FROM directories WHERE storageId=%ld;\
                           DELETE FROM links WHERE storageId=%ld;\
                           DELETE FROM special WHERE storageId=%ld;\
                          ",
                          storageId,
                          storageId,
                          storageId,
                          storageId,
                          storageId
                         );  
}

Errors Index_update(DatabaseHandle *databaseHandle,
                    int64          storageId,
                    String         name,
                    uint64         size
                   )
{
  Errors error;

  assert(databaseHandle != NULL);

  if (name != NULL)
  {
    error = Database_execute(databaseHandle,
                             NULL,
                             NULL,
                             "UPDATE storage \
                              SET name=%'S,\
                                  size=%ld \
                              WHERE id=%ld;\
                             ",
                             name,
                             size,
                             storageId
                            );
  }
  else
  {
    error = Database_execute(databaseHandle,
                             NULL,
                             NULL,
                             "UPDATE storage \
                              SET size=%ld \
                              WHERE id=%ld;\
                             ",
                             size,
                             storageId
                            );
  }
  if (error != ERROR_NONE)
  {
    return error;
  }

  return ERROR_NONE;
}

Errors Index_getState(DatabaseHandle *databaseHandle,
                      int64          storageId,
                      IndexStates    *indexState,
                      uint64         *lastChecked,
                      String         errorMessage
                     )
{
  DatabaseQueryHandle databaseQueryHandle;
  Errors              error;

  error = Database_prepare(&databaseQueryHandle,
                           databaseHandle,
                           "SELECT state, \
                                   lastChecked, \
                                   errorMessage \
                            FROM storage \
                            WHERE id=%ld \
                           ",
                           storageId
                          );
  if (error != ERROR_NONE)
  {
    return error;
  }
  if (!Database_getNextRow(&databaseQueryHandle,
                           "%d %S",
                           indexState,
                           lastChecked,
                           errorMessage
                          )
     )
  {
    (*indexState) = INDEX_STATE_UNKNOWN;
    if (errorMessage != NULL) String_clear(errorMessage);
  }
  Database_finalize(&databaseQueryHandle);

  return ERROR_NONE;
}

Errors Index_setState(DatabaseHandle *databaseHandle,
                      int64          storageId,
                      IndexStates    indexState,
                      uint64         lastChecked,
                      const char     *errorMessage,
                      ...
                     )
{
  Errors  error;
  va_list arguments;
  String  s;
  

  assert(databaseHandle != NULL);

  error = Database_execute(databaseHandle,
                           NULL,
                           NULL,
                           "UPDATE storage \
                            SET state=%d, \
                                errorMessage=NULL \
                            WHERE id=%ld; \
                           ",
                           indexState,
                           storageId
                          );
  if (error != ERROR_NONE)
  {
    return error;
  }

  if (lastChecked != 0LL)
  {
    error = Database_execute(databaseHandle,
                             NULL,
                             NULL,
                             "UPDATE storage \
                              SET lastChecked=%ld \
                              WHERE id=%ld; \
                             ",
                             lastChecked
                            );
    if (error != ERROR_NONE)
    {
      return error;
    }
  }

  if (errorMessage != NULL)
  {
    va_start(arguments,errorMessage);
    s = String_vformat(String_new(),errorMessage,arguments);
    va_end(arguments);

    error = Database_execute(databaseHandle,
                             NULL,
                             NULL,
                             "UPDATE storage \
                              SET errorMessage=%'S \
                              WHERE id=%ld; \
                             ",
                             s,
                             storageId
                            );
    if (error != ERROR_NONE)
    {
      String_delete(s);
      return error;
    }

    String_delete(s);    
  }
  else
  {
  }

  return ERROR_NONE;
}

Errors Index_initListStorage(DatabaseQueryHandle *databaseQueryHandle,
                             DatabaseHandle      *databaseHandle,
                             String              pattern
                            )
{
  String regexpString;
  Errors error;

  assert(databaseQueryHandle != NULL);
  assert(databaseHandle != NULL);

  regexpString = String_new();
  error = Database_prepare(databaseQueryHandle,
                           databaseHandle,
                           "SELECT id, \
                                   name, \
                                   size, \
                                   STRFTIME('%%s',created), \
                                   state, \
                                   mode, \
                                   lastChecked, \
                                   errorMessage \
                            FROM storage \
                            WHERE %S \
                                  AND state!=%d \
                           ",
                           getREGEXPString(regexpString,"name",pattern),
                           INDEX_STATE_CREATE
                          );
  String_delete(regexpString);

  return error;
}

bool Index_getNextStorage(DatabaseQueryHandle *databaseQueryHandle,
                          DatabaseId          *databaseId,
                          String              storageName,
                          uint64              *size,
                          uint64              *datetime,
                          IndexStates         *indexState,
                          IndexModes          *indexMode,
                          uint64              *lastChecked,
                          String              errorMessage
                         )
{
  assert(databaseQueryHandle != NULL);

  return Database_getNextRow(databaseQueryHandle,
                             "%ld %S %ld %ld %d %d %ld %S",
                             databaseId,
                             &storageName,
                             size,
                             datetime,
                             indexState,
                             indexMode,
                             lastChecked,
                             &errorMessage
                            );
}

Errors Index_initListFiles(DatabaseQueryHandle *databaseQueryHandle,
                           DatabaseHandle      *databaseHandle,
                           String              pattern
                          )
{
  String regexpString;
  Errors error;

  assert(databaseQueryHandle != NULL);
  assert(databaseHandle != NULL);

  regexpString = String_new();
  error = Database_prepare(databaseQueryHandle,
                           databaseHandle,
                           "SELECT files.id, \
                                   storage.name, \
                                   files.name, \
                                   files.size, \
                                   files.timeModified, \
                                   files.userId, \
                                   files.groupId, \
                                   files.permission, \
                                   files.fragmentOffset, \
                                   files.fragmentSize\
                            FROM files\
                              LEFT JOIN storage ON storage.id=files.storageId \
                            WHERE %S \
                           ",
                           getREGEXPString(regexpString,"files.name",pattern)
                          );
  String_delete(regexpString);

  return error;
}

bool Index_getNextFile(DatabaseQueryHandle *databaseQueryHandle,
                       DatabaseId          *databaseId,
                       String              storageName,
                       String              fileName,
                       uint64              *size,
                       uint64              *timeModified,
                       uint32              *userId,
                       uint32              *groupId,
                       uint32              *permission,
                       uint64              *fragmentOffset,
                       uint64              *fragmentSize
                      )
{
  assert(databaseQueryHandle != NULL);

  return Database_getNextRow(databaseQueryHandle,
                             "%ld %S %S %ld %ld %d %d %d %ld %ld",
                             databaseId,
                             &storageName,
                             &fileName,
                             size,
                             timeModified,
                             userId,
                             groupId,
                             permission,
                             fragmentOffset,
                             fragmentSize
                            );
}

Errors Index_initListImages(DatabaseQueryHandle *databaseQueryHandle,
                            DatabaseHandle      *databaseHandle,
                            String              pattern
                           )
{
  String regexpString;
  Errors error;

  assert(databaseQueryHandle != NULL);
  assert(databaseHandle != NULL);

  regexpString = String_new();
  error = Database_prepare(databaseQueryHandle,
                           databaseHandle,
                           "SELECT images.id, \
                                   storage.name, \
                                   images.name, \
                                   images.size, \
                                   images.blockOffset, \
                                   images.blockCount \
                            FROM images \
                              LEFT JOIN storage ON storage.id=images.storageId \
                            WHERE %S \
                           ",
                           getREGEXPString(regexpString,"images.name",pattern)
                          );
  String_delete(regexpString);

  return error;
}

bool Index_getNextImage(DatabaseQueryHandle *databaseQueryHandle,
                        DatabaseId          *databaseId,
                        String              storageName,
                        String              imageName,
                        uint64              *size,
                        uint64              *blockOffset,
                        uint64              *blockCount
                       )
{
  assert(databaseQueryHandle != NULL);

  return Database_getNextRow(databaseQueryHandle,
                             "%ld %S %S %ld %ld %ld",
                             databaseId,
                             &storageName,
                             &imageName,
                             size,
                             blockOffset,
                             blockCount
                            );
}

Errors Index_initListDirectories(DatabaseQueryHandle *databaseQueryHandle,
                                 DatabaseHandle      *databaseHandle,
                                 String              pattern
                                )
{
  String regexpString;
  Errors error;

  assert(databaseQueryHandle != NULL);
  assert(databaseHandle != NULL);

  regexpString = String_new();
  error = Database_prepare(databaseQueryHandle,
                           databaseHandle,
                           "SELECT storage.name, \
                                   directories.name, \
                                   directories.timeModified, \
                                   directories.userId, \
                                   directories.groupId, \
                                   directories.permission \
                            FROM directories \
                              LEFT JOIN storage ON storage.id=directories.storageId \
                            WHERE %S \
                           ",
                           getREGEXPString(regexpString,"directories.name",pattern)
                          );
  String_delete(regexpString);

  return error;
}

bool Index_getNextDirectory(DatabaseQueryHandle *databaseQueryHandle,
                            DatabaseId          *databaseId,
                            String              storageName,
                            String              directoryName,
                            uint64              *timeModified,
                            uint32              *userId,
                            uint32              *groupId,
                            uint32              *permission
                           )
{
  assert(databaseQueryHandle != NULL);

  return Database_getNextRow(databaseQueryHandle,
                             "%ld %S %S %ld %d %d %d",
                             databaseId,
                             &storageName,
                             &directoryName,
                             timeModified,
                             userId,
                             groupId,
                             permission
                            );
}

Errors Index_initListLinks(DatabaseQueryHandle *databaseQueryHandle,
                           DatabaseHandle      *databaseHandle,
                           String              pattern
                          )
{
  String regexpString;
  Errors error;

  assert(databaseQueryHandle != NULL);
  assert(databaseHandle != NULL);

  regexpString = String_new();
  error = Database_prepare(databaseQueryHandle,
                           databaseHandle,
                           "SELECT storage.name, \
                                   links.name, \
                                   links.destinationName, \
                                   links.timeModified, \
                                   links.userId, \
                                   links.groupId, \
                                   links.permission \
                            FROM links \
                              LEFT JOIN storage ON storage.id=links.storageId \
                            WHERE %S \
                           ",
                           getREGEXPString(regexpString,"links.name",pattern)
                          );
  String_delete(regexpString);

  return error;
}

bool Index_getNextLink(DatabaseQueryHandle *databaseQueryHandle,
                       DatabaseId          *databaseId,
                       String              storageName,
                       String              linkName,
                       String              destinationName,
                       uint64              *timeModified,
                       uint32              *userId,
                       uint32              *groupId,
                       uint32              *permission
                      )
{
  assert(databaseQueryHandle != NULL);

  return Database_getNextRow(databaseQueryHandle,
                             "%ld %S %S %S %ld %d %d %d",
                             databaseId,
                             &storageName,
                             &linkName,
                             &destinationName,
                             timeModified,
                             userId,
                             groupId,
                             permission
                            );
}

Errors Index_initListSpecial(DatabaseQueryHandle *databaseQueryHandle,
                             DatabaseHandle      *databaseHandle,
                             String              pattern
                            )
{
  String regexpString;
  Errors error;

  assert(databaseQueryHandle != NULL);
  assert(databaseHandle != NULL);

  regexpString = String_new();
  error = Database_prepare(databaseQueryHandle,
                           databaseHandle,
                           "SELECT storage.name, \
                                   special.name, \
                                   special.timeModified, \
                                   special.userId, \
                                   special.groupId, \
                                   special.permission \
                            FROM special \
                              LEFT JOIN storage ON storage.id=special.storageId \
                            WHERE %S \
                           ",
                           getREGEXPString(regexpString,"special.name",pattern)
                          );
  String_delete(regexpString);

  return error;
}

bool Index_getNextSpecial(DatabaseQueryHandle *databaseQueryHandle,
                          DatabaseId          *databaseId,
                          String              storageName,
                          String              name,
                          uint64              *timeModified,
                          uint32              *userId,
                          uint32              *groupId,
                          uint32              *permission
                         )
{
  assert(databaseQueryHandle != NULL);

  return Database_getNextRow(databaseQueryHandle,
                             "%ld %S %S %ld %d %d %d",
                             databaseId,
                             &storageName,
                             &name,
                             timeModified,
                             userId,
                             groupId,
                             permission
                            );
}

void Index_doneList(DatabaseQueryHandle *databaseQueryHandle)
{
  assert(databaseQueryHandle != NULL);

  Database_finalize(databaseQueryHandle);
}

Errors Index_addFile(DatabaseHandle *databaseHandle,
                     int64          storageId,
                     const String   fileName,
                     uint64         size,
                     uint64         timeLastAccess,
                     uint64         timeModified,
                     uint64         timeLastChanged,
                     uint32         userId,
                     uint32         groupId,
                     uint32         permission,
                     uint64         fragmentOffset,
                     uint64         fragmentSize
                    )
{
  assert(databaseHandle != NULL);
  assert(fileName != NULL);

  return Database_execute(databaseHandle,
                          NULL,
                          NULL,
                          "INSERT INTO files \
                             (\
                              storageId,\
                              name,\
                              size,\
                              timeLastAccess,\
                              timeModified,\
                              timeLastChanged,\
                              userId,\
                              groupId,\
                              permission,\
                              fragmentOffset,\
                              fragmentSize\
                             ) \
                           VALUES \
                             (\
                              %lu,\
                              %'S,\
                              %lu,\
                              %lu,\
                              %lu,\
                              %lu,\
                              %u,\
                              %u,\
                              %u,\
                              %lu,\
                              %lu\
                             ); \
                          ",
                          storageId,
                          fileName,
                          size,
                          timeLastAccess,
                          timeModified,
                          timeLastChanged,
                          userId,
                          groupId,
                          permission,
                          fragmentOffset,
                          fragmentSize
                         );
}

Errors Index_addImage(DatabaseHandle *databaseHandle,
                      int64          storageId,
                      const String   imageName,
                      int64          size,
                      ulong          blockSize,
                      uint64         blockOffset,
                      uint64         blockCount
                     )
{
  assert(databaseHandle != NULL);
  assert(imageName != NULL);

  return Database_execute(databaseHandle,
                          NULL,
                          NULL,
                          "INSERT INTO images \
                             (\
                              storageId,\
                              name,\
                              fileSystemType,\
                              size,\
                              blockSize,\
                              blockOffset,\
                              blockCount\
                             ) \
                           VALUES \
                             (\
                              %lu,\
                              %'S,\
                              %d,\
                              %lu,\
                              %u,\
                              %lu,\
                              %lu\
                             );\
                          ",
                          storageId,
                          imageName,
                          size,
                          blockSize,
                          blockOffset,
                          blockCount
                         );
}

Errors Index_addDirectory(DatabaseHandle *databaseHandle,
                          int64          storageId,
                          String         directoryName,
                          uint64         timeLastAccess,
                          uint64         timeModified,
                          uint64         timeLastChanged,
                          uint32         userId,
                          uint32         groupId,
                          uint32         permission
                         )
{
  assert(databaseHandle != NULL);
  assert(directoryName != NULL);

  return Database_execute(databaseHandle,
                          NULL,
                          NULL,
                          "INSERT INTO directories \
                             (\
                              storageId,\
                              name,\
                              timeLastAccess,\
                              timeModified,\
                              timeLastChanged,\
                              userId,\
                              groupId,\
                              permission\
                             ) \
                           VALUES \
                             (\
                              %lu,\
                              %'S,\
                              %lu,\
                              %lu,\
                              %lu,\
                              %u,\
                              %u,\
                              %u \
                             );\
                          ",
                          storageId,
                          directoryName,
                          timeLastAccess,
                          timeModified,
                          timeLastChanged,
                          userId,
                          groupId,
                          permission
                         );
}

Errors Index_addLink(DatabaseHandle *databaseHandle,
                     int64          storageId,
                     const String   linkName,
                     const String   destinationName,
                     uint64         timeLastAccess,
                     uint64         timeModified,
                     uint64         timeLastChanged,
                     uint32         userId,
                     uint32         groupId,
                     uint32         permission
                    )
{
  assert(databaseHandle != NULL);
  assert(linkName != NULL);
  assert(destinationName != NULL);

  return Database_execute(databaseHandle,
                          NULL,
                          NULL,
                          "INSERT INTO links \
                             (\
                              storageId,\
                              name,\
                              destinationName,\
                              timeLastAccess,\
                              timeModified,\
                              timeLastChanged,\
                              userId,\
                              groupId,\
                              permission\
                             ) \
                           VALUES \
                             (\
                              %lu,\
                              %'S,\
                              %'S,\
                              %lu,\
                              %lu,\
                              %lu,\
                              %u,\
                              %u,\
                              %u\
                             );\
                           ",
                          storageId,
                          linkName,
                          destinationName,
                          timeLastAccess,
                          timeModified,
                          timeLastChanged,
                          userId,
                          groupId,
                          permission
                         );
}

Errors Index_addSpecial(DatabaseHandle   *databaseHandle,
                        int64            storageId,
                        const String     name,
                        FileSpecialTypes specialType,
                        uint64           timeLastAccess,
                        uint64           timeModified,
                        uint64           timeLastChanged,
                        uint32           userId,
                        uint32           groupId,
                        uint32           permission,
                        uint32           major,
                        uint32           minor
                       )
{
  assert(databaseHandle != NULL);
  assert(name != NULL);

  return Database_execute(databaseHandle,
                          NULL,
                          NULL,
                          "INSERT INTO special \
                             (\
                              storageId,\
                              name,\
                              specialType,\
                              timeLastAccess,\
                              timeModified,\
                              timeLastChanged,\
                              userId,\
                              groupId,\
                              permission,\
                              major,\
                              minor \
                             ) \
                           VALUES \
                             (\
                              %lu,\
                              %'S,\
                              %u,\
                              %lu,\
                              %lu,\
                              %lu,\
                              %u,\
                              %u,\
                              %u,\
                              %d,\
                              %u\
                             );\
                          ",
                          storageId,
                          name,
                          specialType,
                          timeLastAccess,
                          timeModified,
                          timeLastChanged,
                          userId,
                          groupId,
                          permission,
                          major,
                          minor
                         );
}

#ifdef __cplusplus
  }
#endif

/* end of file */
