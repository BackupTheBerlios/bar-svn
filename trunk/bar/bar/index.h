/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: index database functions
* Systems: all
*
\***********************************************************************/

#ifndef __INDEX__
#define __INDEX__

/****************************** Includes *******************************/
#include <config.h>  // use <...> to support separated build directory

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "global.h"
#include "strings.h"
#include "files.h"
#include "database.h"
#include "errors.h"

#include "bar.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
#define INDEX_STORAGE_ID_NONE -1LL

// index states
typedef enum
{
  INDEX_STATE_NONE,

  INDEX_STATE_OK,
  INDEX_STATE_CREATE,
  INDEX_STATE_UPDATE_REQUESTED,
  INDEX_STATE_UPDATE,
  INDEX_STATE_ERROR,

  INDEX_STATE_UNKNOWN
} IndexStates;
typedef uint64 IndexStateSet;

#define INDEX_STATE_ALL (INDEX_STATE_OK|INDEX_STATE_CREATE|INDEX_STATE_UPDATE_REQUESTED|INDEX_STATE_UPDATE)

// index modes
typedef enum
{
  INDEX_MODE_MANUAL,
  INDEX_MODE_AUTO,

  INDEX_MODE_UNKNOWN
} IndexModes;
typedef uint64 IndexModeSet;

#define INDEX_MODE_ALL (INDEX_MODE_MANUAL|INDEX_MODE_AUTO)

// index query handle
typedef struct
{
  DatabaseQueryHandle databaseQueryHandle;
  struct
  {
    StorageTypes type;
    Pattern      *hostNamePattern;
    Pattern      *loginNamePattern;
    Pattern      *deviceNamePattern;
    Pattern      *fileNamePattern;
  } storage;
} IndexQueryHandle;

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

/****************************** Macros *********************************/

#define INDEX_STATE_SET(indexState) (1 << indexState)

#ifndef NDEBUG
  #define Index_init(...) __Index_init(__FILE__,__LINE__,__VA_ARGS__)
  #define Index_done(...) __Index_done(__FILE__,__LINE__,__VA_ARGS__)
#endif /* not NDEBUG */

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : Index_initAll
* Purpose: initialize index functions
* Input  : -
* Output : -
* Return : ERROR_NONE or errorcode
* Notes  : -
\***********************************************************************/

Errors Index_initAll(void);

/***********************************************************************\
* Name   : Index_doneAll
* Purpose: deinitialize index functions
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Index_doneAll(void);

/***********************************************************************\
* Name   : Index_stateToString
* Purpose: get name of index sIndex_donetateIndex_init
* Input  : indexState   - index state
*          defaultValue - default value
* Output : -
* Return : name
* Notes  : -
\***********************************************************************/

const char *Index_stateToString(IndexStates indexState, const char *defaultValue);

/***********************************************************************\
* Name   : Index_parseState
* Purpose: parse state string
* Input  : name - name
* Output : indexState - index state
* Return : TRUE iff parsed
* Notes  : -
\***********************************************************************/

bool Index_parseState(const char *name, IndexStates *indexState);

/***********************************************************************\
* Name   : Index_parseMode
* Purpose: get name of index mode
* Input  : indexMode    - index mode
*          defaultValue - default value
* Output : -
* Return : name
* Notes  : -
\***********************************************************************/

const char *Index_modeToString(IndexModes indexMode, const char *defaultValue);

/***********************************************************************\
* Name   : Index_parseMode
* Purpose: parse mode string
* Input  : name - name
* Output : indexMode - index mode
* Return : TRUE iff parsed
* Notes  : -
\***********************************************************************/

bool Index_parseMode(const char *name, IndexModes *indexMode);

/***********************************************************************\
* Name   : Index_init
* Purpose: initialize index database
* Input  : indexDatabaseHandle   - index database handle variable
*          indexDatabaseFileName - database file name
* Output : indexDatabaseHandle - index database handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
  Errors Index_init(DatabaseHandle *indexDatabaseHandle,
                    const char     *indexDatabaseFileName
                   );
#else /* not NDEBUG */
  Errors __Index_init(const char     *__fileName__,
                      uint           __lineNb__,
                      DatabaseHandle *indexDatabaseHandle,
                      const char     *indexDatabaseFileName
                     );
#endif /* NDEBUG */

/***********************************************************************\
* Name   : Index_done
* Purpose: deinitialize index database
* Input  : indexDatabaseHandle - index database handle
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
  void Index_done(DatabaseHandle *indexDatabaseHandle);
#else /* not NDEBUG */
  void __Index_done(const char     *__fileName__,
                    uint           __lineNb__,
                    DatabaseHandle *indexDatabaseHandle
                   );
#endif /* NDEBUG */

/***********************************************************************\
* Name   : Index_findById
* Purpose: find index by id
* Input  : databaseHandle - database handle
*          storageId   - database id of index
* Output : storageName          - storage name
*          indexState           - index state (can be NULL)
*          lastCheckedTimestamp - last checked date/time stamp [s] (can
*                                 be NULL)
* Return : TRUE if index found, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_findById(DatabaseHandle *databaseHandle,
                    int64          storageId,
                    String         storageName,
                    IndexStates    *indexState,
                    uint64         *lastCheckedTimestamp
                   );

/***********************************************************************\
* Name   : Index_findByName
* Purpose: find index by name
* Input  : databaseHandle  - database handle
*          findStorageType - storage type to find or STORAGE_TYPE_ANY
*          findHostName    - host naem to find or NULL
*          findLoginName   - login name to find or NULL
*          findDeviceName  - device name to find or NULL
*          findFileName    - file name to find or NULL
* Output : storageId            - database id of index
*          uuid                 - unique id (can be NULL)
*          indexState           - index state (can be NULL)
*          lastCheckedTimestamp - last checked date/time stamp [s] (can
*                                 be NULL)
* Return : TRUE if index found, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_findByName(DatabaseHandle *databaseHandle,
                      StorageTypes   findStorageType,
                      const String   findHostName,
                      const String   findLoginName,
                      const String   findDeviceName,
                      const String   findFileName,
                      int64          *storageId,
                      String         uuid,
                      IndexStates    *indexState,
                      uint64         *lastCheckedTimestamp
                     );

/***********************************************************************\
* Name   : Index_findByState
* Purpose: find index by state
* Input  : databaseHandle - database handle
*          indexState     - index state
* Output : storageId            - database id of index
*          storageName          - storage name (can be NULL)
*          uuid                 - unique id (can be NULL)
*          lastCheckedTimestamp - last checked date/time stamp [s] (can
*                                 be NULL)
* Return : TRUE if index found, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_findByState(DatabaseHandle *databaseHandle,
                       IndexStateSet  indexStateSet,
                       int64          *storageId,
                       String         storageName,
                       String         uuid,
                       uint64         *lastCheckedTimestamp
                      );

/***********************************************************************\
* Name   : Index_create
* Purpose: create new index
* Input  : databaseHandle - database handle
*          storageName    - storage name
*          uuid           - unique id
*          indexState     - index state
*          indexMode      - index mode
* Output : storageId - database id of index
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_create(DatabaseHandle *databaseHandle,
                    const String   storageName,
                    const String   uuid,
                    IndexStates    indexState,
                    IndexModes     indexMode,
                    int64          *storageId
                   );

/***********************************************************************\
* Name   : Index_delete
* Purpose: delete index
* Input  : databaseHandle - database handle
*          storageId      - database id of index
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_delete(DatabaseHandle *databaseHandle,
                    int64          storageId
                   );

/***********************************************************************\
* Name   : Index_clear
* Purpose: clear index content
* Input  : databaseHandle - database handle
*          storageId      - database id of index
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_clear(DatabaseHandle *databaseHandle,
                   int64          storageId
                  );

/***********************************************************************\
* Name   : Index_update
* Purpose: update index name/size
* Input  : databaseHandle - database handle
*          storageId      - database id of index
*          storageName    - storage name (can be NULL)
*          uuid           - uuid (can be NULL)
*          size           - size [bytes]
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_update(DatabaseHandle *databaseHandle,
                    int64          storageId,
                    String         storageName,
                    String         uuid,
                    uint64         size
                   );

/***********************************************************************\
* Name   : Index_getState
* Purpose: get index state
* Input  : databaseHandle - database handle
*          storageId      - database id of index
* Output : indexState           - index state; see IndexStates
*          lastCheckedTimestamp - last checked date/time stamp [s] (can
*                                 be NULL)
*          errorMessage         - error message (can be NULL)
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_getState(DatabaseHandle *databaseHandle,
                      int64          storageId,
                      IndexStates    *indexState,
                      uint64         *lastCheckedTimestamp,
                      String         errorMessage
                     );

/***********************************************************************\
* Name   : Index_setState
* Purpose: set index state
* Input  : databaseHandle       - database handle
*          storageId            - database id of index
*          indexState           - index state; see IndexStates
*          lastCheckedTimestamp - last checked date/time stamp [s] (can
*                                 be 0LL)
*          errorMessage         - error message (can be NULL)
*          ...                  - optional arguments for error message
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_setState(DatabaseHandle *databaseHandle,
                      int64          storageId,
                      IndexStates    indexState,
                      uint64         lastCheckedTimestamp,
                      const char     *errorMessage,
                      ...
                     );

/***********************************************************************\
* Name   : Index_countState
* Purpose: get number of storage entries
* Input  : databaseHandle - database handle
*          indexState     - index state; see IndexStates
* Output : -
* Return : number of entries or -1
* Notes  : -
\***********************************************************************/

long Index_countState(DatabaseHandle *databaseHandle,
                      IndexStates    indexState
                     );

/***********************************************************************\
* Name   : Index_initListStorage
* Purpose: list storage entries
* Input  : IndexQueryHandle - index query handle variable
*          databaseHandle   - database handle
*          storageType      - storage type to find or STORAGE_TYPE_ANY
*          hostName         - host name pattern or NULL
*          loginName        - login name pattern or NULL
*          deviceName       - device name pattern or NULL
*          fileName         - file name pattern or NULL
*          indexState       - index state
* Output : IndexQueryHandle - index query handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_initListStorage(IndexQueryHandle *indexQueryHandle,
                             DatabaseHandle   *databaseHandle,
                             StorageTypes     storageType,
                             const String     hostName,
                             const String     loginName,
                             const String     deviceName,
                             const String     fileName,
                             IndexStateSet    indexStateSet
                            );

/***********************************************************************\
* Name   : Index_getNextStorage
* Purpose: get next index storage entry
* Input  : IndexQueryHandle    - index query handle
* Output : databaseId          - database id of entry
*          storageName         - storage name (can be NULL)
*          uuid                - unique id (can be NULL)
*          createdDateTime     - date/time stamp [s]
*          size                - size [bytes]
*          indexState          - index state (can be NULL)
*          indexMode           - index mode (can be NULL)
*          lastCheckedDateTime - last checked date/time stamp [s] (can be NULL)
*          errorMessage        - last error message
* Return : TRUE if entry read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_getNextStorage(IndexQueryHandle *indexQueryHandle,
                          DatabaseId       *databaseId,
                          String           storageName,
                          String           uuid,
                          uint64           *createdDateTime,
                          uint64           *size,
                          IndexStates      *indexState,
                          IndexModes       *indexMode,
                          uint64           *lastCheckedDateTime,
                          String           errorMessage
                         );

/***********************************************************************\
* Name   : Index_initListFiles
* Purpose: list file entries
* Input  : indexQueryHandle - index query handle variable
*          databaseHandle   - database handle
*          pattern          - name pattern (can be NULL)
* Output : indexQueryHandle - index query handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_initListFiles(IndexQueryHandle *indexQueryHandle,
                           DatabaseHandle   *databaseHandle,
                           const DatabaseId storageIds[],
                           uint             storageIdCount,
                           String           pattern
                          );

/***********************************************************************\
* Name   : Index_getNextFile
* Purpose: get next file entry
* Input  : indexQueryHandle - index query handle
* Output : databaseId     - database id of entry
*          storageName    - storage name (can be NULL)
*          fileName       - name
*          size           - size [bytes]
*          timeModified   - modified date/time stamp [s]
*          userId         - user id
*          groupId        - group id
*          permission     - permission flags
*          fragmentOffset - fragment offset [bytes]
*          fragmentSize   - fragment size [bytes]
* Return : TRUE if entry read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_getNextFile(IndexQueryHandle *indexQueryHandle,
                       DatabaseId       *databaseId,
                       String           storageName,
                       uint64           *storageDateTime,
                       String           fileName,
                       uint64           *size,
                       uint64           *timeModified,
                       uint32           *userId,
                       uint32           *groupId,
                       uint32           *permission,
                       uint64           *fragmentOffset,
                       uint64           *fragmentSize
                      );

/***********************************************************************\
* Name   : Index_initListImages
* Purpose: list image entries
* Input  : databaseHandle - database handle
*          pattern        - name pattern (can be NULL)
* Output : indexQueryHandle - index query handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_initListImages(IndexQueryHandle *indexQueryHandle,
                            DatabaseHandle   *databaseHandle,
                            const DatabaseId *storageIds,
                            uint             storageIdCount,
                            String           pattern
                           );

/***********************************************************************\
* Name   : Index_getNextImage
* Purpose: get next image entry
* Input  : indexQueryHandle - index query handle
* Output : databaseId   - database id of entry
*          storageName  - storage name
*          imageName    - image name
*          size         - size [bytes]
*          blockOffset  - block offset [blocks]
*          blockCount   - number of blocks
* Return : TRUE if entry read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_getNextImage(IndexQueryHandle *indexQueryHandle,
                        DatabaseId       *databaseId,
                        String           storageName,
                        uint64           *storageDateTime,
                        String           imageName,
                        uint64           *size,
                        uint64           *blockOffset,
                        uint64           *blockCount
                       );

/***********************************************************************\
* Name   : Index_initListDirectories
* Purpose: list directory entries
* Input  : databaseHandle - database handle
*          pattern        - name pattern (can be NULL)
* Output : indexQueryHandle - index query handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_initListDirectories(IndexQueryHandle *indexQueryHandle,
                                 DatabaseHandle   *databaseHandle,
                                 const DatabaseId *storageIds,
                                 uint             storageIdCount,
                                 String           pattern
                                );

/***********************************************************************\
* Name   : Index_getNextDirectory
* Purpose: get next directory entry
* Input  : indexQueryHandle - index query handle
* Output : databaseId    - database id of entry
*          storageName   - storage name
*          directoryName - directory name
*          timeModified  - modified date/time stamp [s]
*          userId        - user id
*          groupId       - group id
*          permission    - permission flags
* Return : TRUE if entry read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_getNextDirectory(IndexQueryHandle *indexQueryHandle,
                            DatabaseId       *databaseId,
                            String           storageName,
                            uint64           *storageDateTime,
                            String           directoryName,
                            uint64           *timeModified,
                            uint32           *userId,
                            uint32           *groupId,
                            uint32           *permission
                           );

/***********************************************************************\
* Name   : Index_initListLinks
* Purpose: list link entries
* Input  : databaseHandle - database handle
*          pattern        - name pattern (can be NULL)
* Output : indexQueryHandle - inxe query handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_initListLinks(IndexQueryHandle *indexQueryHandle,
                           DatabaseHandle   *databaseHandle,
                           const DatabaseId *storageIds,
                           uint             storageIdCount,
                           String           pattern
                          );

/***********************************************************************\
* Name   : Index_getNextLink
* Purpose: get next link entry
* Input  : indexQueryHandle - index query handle
* Output : databaseId      - database id of entry
*          storageName     - storage name
*          linkName        - link name
*          destinationName - destination name
*          timeModified    - modified date/time stamp [s]
*          userId          - user id
*          groupId         - group id
*          permission      - permission flags
* Return : TRUE if entry read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_getNextLink(IndexQueryHandle *indexQueryHandle,
                       DatabaseId       *databaseId,
                       String           storageName,
                       uint64           *storageDateTime,
                       String           name,
                       String           destinationName,
                       uint64           *timeModified,
                       uint32           *userId,
                       uint32           *groupId,
                       uint32           *permission
                      );

/***********************************************************************\
* Name   : Index_initListHardLinks
* Purpose: list hard link entries
* Input  : databaseHandle - database handle
*          pattern        - name pattern (can be NULL)
* Output : indexQueryHandle - indxe query handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_initListHardLinks(IndexQueryHandle *indexQueryHandle,
                               DatabaseHandle   *databaseHandle,
                               const DatabaseId *storageIds,
                               uint             storageIdCount,
                               String           pattern
                               );

/***********************************************************************\
* Name   : Index_getNextHardLink
* Purpose: get next hard link entry
* Input  : indexQueryHandle - index query handle
* Output : databaseId          - database id of entry
*          storageName         - storage name
*          fileName            - file name
*          destinationFileName - destination file name
*          size                - size [bytes]
*          timeModified        - modified date/time stamp [s]
*          userId              - user id
*          groupId             - group id
*          permission          - permission flags
*          fragmentOffset      - fragment offset [bytes]
*          fragmentSize        - fragment size [bytes]
* Return : TRUE if entry read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_getNextHardLink(IndexQueryHandle *indexQueryHandle,
                           DatabaseId       *databaseId,
                           String           storageName,
                           uint64           *storageDateTime,
                           String           fileName,
                           uint64           *size,
                           uint64           *timeModified,
                           uint32           *userId,
                           uint32           *groupId,
                           uint32           *permission,
                           uint64           *fragmentOffset,
                           uint64           *fragmentSize
                          );

/***********************************************************************\
* Name   : Index_initListSpecial
* Purpose: list special entries
* Input  : databaseHandle - database handle
*          pattern        - name pattern (can be NULL)
* Output : indexQueryHandle - index query handle
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_initListSpecial(IndexQueryHandle *indexQueryHandle,
                             DatabaseHandle   *databaseHandle,
                             const DatabaseId *storageIds,
                             uint             storageIdCount,
                             String           pattern
                            );

/***********************************************************************\
* Name   : Index_getNextSpecial
* Purpose: get next special entry
* Input  : indexQueryHandle - index query handle
* Output : databaseId   - database id of entry
*          storageName  - storage name
*          name         - name
*          timeModified - modified date/time stamp [s]
*          userId       - user id
*          groupId      - group id
*          permission   - permission flags
* Return : TRUE if entry read, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool Index_getNextSpecial(IndexQueryHandle *indexQueryHandle,
                          DatabaseId       *databaseId,
                          String           storageName,
                          uint64           *storageDateTime,
                          String           name,
                          uint64           *timeModified,
                          uint32           *userId,
                          uint32           *groupId,
                          uint32           *permission
                         );

/***********************************************************************\
* Name   : Index_doneList
* Purpose: done index list
* Input  : indexQueryHandle - index query handle
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Index_doneList(IndexQueryHandle *indexQueryHandle);

/***********************************************************************\
* Name   : Index_addFile
* Purpose: add file entry
* Input  : databaseHandle  - database handle
*          storageId       - database id of index
*          name            - name
*          size            - size [bytes]
*          timeLastAccess  - last access date/time stamp [s]
*          timeModified    - modified date/time stamp [s]
*          timeLastChanged - last changed date/time stamp [s]
*          userId          - user id
*          groupId         - group id
*          permission      - permission flags
*          fragmentOffset  - fragment offset [bytes]
*          fragmentSize    - fragment size [bytes]
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

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
                    );

/***********************************************************************\
* Name   : Index_addImage
* Purpose: add image entry
* Input  : databaseHandle - database handle
*          storageId      - database id of index
*          imageName      - image name
*          size           - size [bytes]
*          blockSize      - block size [bytes]
*          blockOffset    - block offset [blocks]
*          blockCount     - number of blocks
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_addImage(DatabaseHandle *databaseHandle,
                      int64          storageId,
                      const String   imageName,
                      int64          size,
                      ulong          blockSize,
                      uint64         blockOffset,
                      uint64         blockCount
                     );

/***********************************************************************\
* Name   : Index_addDirectory
* Purpose: add directory entry
* Input  : databaseHandle  - database handle
*          storageId       - database id of index
*          directoryName   - name
*          timeLastAccess  - last access date/time stamp [s]
*          timeModified    - modified date/time stamp [s]
*          timeLastChanged - last changed date/time stamp [s]
*          userId          - user id
*          groupId         - group id
*          permission      - permission flags
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_addDirectory(DatabaseHandle *databaseHandle,
                          int64          storageId,
                          String         directoryName,
                          uint64         timeLastAccess,
                          uint64         timeModified,
                          uint64         timeLastChanged,
                          uint32         userId,
                          uint32         groupId,
                          uint32         permission
                         );

/***********************************************************************\
* Name   : Index_addLink
* Purpose: add link entry
* Input  : databaseHandle  - database handle
*          storageId       - database id of index
*          name            - linkName
*          destinationName - destination name
*          timeLastAccess  - last access date/time stamp [s]
*          timeModified    - modified date/time stamp [s]
*          timeLastChanged - last changed date/time stamp [s]
*          userId          - user id
*          groupId         - group id
*          permission      - permission flags
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

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
                    );

/***********************************************************************\
* Name   : Index_addHardLink
* Purpose: add hard link entry
* Input  : databaseHandle  - database handle
*          storageId       - database id of index
*          name            - name
*          size            - size [bytes]
*          timeLastAccess  - last access date/time stamp [s]
*          timeModified    - modified date/time stamp [s]
*          timeLastChanged - last changed date/time stamp [s]
*          userId          - user id
*          groupId         - group id
*          permission      - permission flags
*          fragmentOffset  - fragment offset [bytes]
*          fragmentSize    - fragment size [bytes]
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Index_addHardLink(DatabaseHandle *databaseHandle,
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
                        );

/***********************************************************************\
* Name   : Index_addSpecial
* Purpose: add special entry
* Input  : databaseHandle  - database handle
*          storageId       - database id of index
*          name            - name
*          specialType     - special type; see FileSpecialTypes
*          timeLastAccess  - last access date/time stamp [s]
*          timeModified    - modified date/time stamp [s]
*          timeLastChanged - last changed date/time stamp [s]
*          userId          - user id
*          groupId         - group id
*          permission      - permission flags
*          major,minor     - major,minor number
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

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
                       );

#ifdef __cplusplus
  }
#endif

#endif /* __INDEX__ */

/* end of file */
