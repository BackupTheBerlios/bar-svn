/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: functions for inter-process semaphores
* Systems: all POSIX
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include "global.h"
#include "lists.h"

#include "semaphores.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

#ifndef NDEBUG
typedef struct
{
  LIST_HEADER(Semaphore);
} DebugSemaphoreList;
#endif /* not NDEBUG */

/***************************** Variables *******************************/

#define DEBUG_READ       FALSE
#define DEBUG_READ_WRITE FALSE

#ifndef NDEBUG
  LOCAL pthread_once_t     debugSemaphoreInitFlag = PTHREAD_ONCE_INIT;
  LOCAL DebugSemaphoreList debugSemaphoreList;
  LOCAL void(*debugPrevSignalHandler)(int)       ;
#endif /* not NDEBUG */

/****************************** Macros *********************************/

#ifndef NDEBUG

  #define LOCK(debugFlag,type,semaphore) \
    do \
    { \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x wait lock %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type); \
      pthread_mutex_lock(semaphore); \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x locked %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type); \
    } \
    while (0)

  #define UNLOCK(debugFlag,type,semaphore,n) \
    do \
    { \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x unlock %s n=%d\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type,n); \
      pthread_mutex_unlock(semaphore); \
    } \
    while (0)

  #define WAIT(debugFlag,type,condition,semaphore) \
    do \
    { \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x unlock+wait %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type); \
      pthread_cond_wait(condition,semaphore); \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x waited+locked %s done\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type); \
    } \
    while (0)

  #define WAIT_TIMEOUT(debugFlag,type,condition,semaphore,timeout,lockedFlag) \
    do \
    { \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x unlock+wait %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type); \
      if (pthread_cond_timedwait(condition,semaphore,timeout) == ETIMEDOUT) lockedFlag = FALSE; \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x waited+locked %s done\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type); \
    } \
    while (0)

  #define SIGNAL(debugFlag,type,condition) \
    do \
    { \
      if (debugFlag) fprintf(stderr,"%s,%4d: 0x%x signal %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),type); \
      pthread_cond_signal(condition); \
    } \
    while (0)

#else /* NDEBUG */

  #define LOCK(debugFlag,type,semaphore) \
    do \
    { \
      pthread_mutex_lock(semaphore); \
    } \
    while (0)

  #define UNLOCK(debugFlag,type,semaphore,n) \
    do \
    { \
      pthread_mutex_unlock(semaphore); \
    } \
    while (0)

  #define WAIT(debugFlag,type,condition,semaphore) \
    do \
    { \
      pthread_cond_wait(condition,semaphore); \
    } \
    while (0)

  #define WAIT_TIMEOUT(debugFlag,type,condition,semaphore,timeout,lockedFlag) \
    do \
    { \
      if (pthread_cond_timedwait(condition,semaphore,timeout) == ETIMEDOUT) lockedFlag = FALSE; \
    } \
    while (0)

  #define SIGNAL(debugFlag,type,condition) \
    do \
    { \
      pthread_cond_broadcast(condition); \
    } \
    while (0)

#endif /* not NDEBUG */

/***************************** Forwards ********************************/
#ifndef NDEBUG
  LOCAL void signalHandler(int signalNumber);
#endif /* not NDEBUG */

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

#ifndef NDEBUG
/***********************************************************************\
* Name   : Semaphore_debugInit
* Purpose: initialize debug functions
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void Semaphore_debugInit(void)
{
  // init variables
  List_init(&debugSemaphoreList);

  // init signal handler for Ctrl-\ (SIGQUIT) for printing debug information
  debugPrevSignalHandler = signal(SIGQUIT,signalHandler);
}

/***********************************************************************\
* Name   : Semaphore_debugPrintInfo
* Purpose:
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void Semaphore_debugPrintInfo(void)
{
  Semaphore  *semaphore;
  const char *semaphoreState;
  uint       z;

  fprintf(stderr,"Semaphore debug info:\n");
  LIST_ITERATE(&debugSemaphoreList,semaphore)
  {
    switch (semaphore->lockType)
    {
      case SEMAPHORE_LOCK_TYPE_NONE:
        assert(semaphore->readLockCount == 0);
        assert(semaphore->readWriteLockCount == 0);
        semaphoreState = "none";
        break;
      case SEMAPHORE_LOCK_TYPE_READ:
        assert(semaphore->readWriteLockCount == 0);
        semaphoreState = "read";
        break;
      case SEMAPHORE_LOCK_TYPE_READ_WRITE:
        assert(semaphore->readLockCount == 0);
        semaphoreState = "read/write";
        break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break; /* not reached */
      #endif /* NDEBUG */
    }
    fprintf(stderr,"  '%s':\n",semaphore->name);
    fprintf(stderr,"    locked '%s'\n",semaphoreState);
    for (z = 0; z < semaphore->lockedByCount; z++)
    {
      fprintf(stderr,
              "    by thread 0x%lx at %s, line %lu\n",
              semaphore->lockedBy[z].thread,
              semaphore->lockedBy[z].fileName,
              semaphore->lockedBy[z].lineNb
             );
    }
  }
}

/***********************************************************************\
* Name   : signalHandler
* Purpose: signal handler
* Input  : signalNumber - signal number
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void signalHandler(int signalNumber)
{
  if (signalNumber == SIGQUIT)
  {
    Semaphore_debugPrintInfo();
  }
  if (debugPrevSignalHandler != NULL)
  {
    debugPrevSignalHandler(signalNumber);
  }
}
#endif /* not NDEBUG */

/***********************************************************************\
* Name   : lock
* Purpose: lock semaphore
* Input  : semaphore         - semaphore
*          semaphoreLockType - lock type; see SemaphoreLockTypes
*          timeout           - timeout [ms] or SEMAPHORE_WAIT_FOREVER
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
LOCAL bool lock(Semaphore          *semaphore,
                SemaphoreLockTypes semaphoreLockType,
                long               timeout
               )
#else /* not NDEBUG */
LOCAL bool lock(const char         *fileName,
                ulong              lineNb,
                Semaphore          *semaphore,
                SemaphoreLockTypes semaphoreLockType,
                long               timeout
               )
#endif /* NDEBUG */
{
  struct timespec tp;
  bool            lockedFlag;

  assert(semaphore != NULL);
  assert((semaphoreLockType == SEMAPHORE_LOCK_TYPE_READ) || (semaphoreLockType == SEMAPHORE_LOCK_TYPE_READ_WRITE));

  lockedFlag = TRUE;

  switch (semaphoreLockType)
  {
    case SEMAPHORE_LOCK_TYPE_NONE:
      break;
    case SEMAPHORE_LOCK_TYPE_READ:
      // request read lock
      pthread_mutex_lock(&semaphore->requestLock);
      {
        semaphore->readRequestCount++;
      }
      pthread_mutex_unlock(&semaphore->requestLock);

      LOCK(DEBUG_READ,"R",&semaphore->lock);
      {
        assert(semaphore->readWriteLockCount == 0);

        #ifndef NDEBUG
          // debug lock trace code
          if (semaphore->lockedByCount < SIZE_OF_ARRAY(semaphore->lockedBy))
          {
            semaphore->lockedBy[semaphore->lockedByCount].thread   = pthread_self();
            semaphore->lockedBy[semaphore->lockedByCount].fileName = fileName;
            semaphore->lockedBy[semaphore->lockedByCount].lineNb   = lineNb;
            semaphore->lockedByCount++;
          }
          else
          {
            fprintf(stderr,
                    "DEBUG WARNING: too many thread locks for semaphore %p at %s, line %lu (max. %lu)!\n",
                    semaphore,
                    fileName,
                    lineNb,
                    SIZE_OF_ARRAY(semaphore->lockedBy)
                   );
          }
        #endif /* not NDEBUG */

        // wait until no more read/write-locks
        if (timeout != SEMAPHORE_WAIT_FOREVER)
        {
          clock_gettime(CLOCK_REALTIME,&tp);
          tp.tv_sec  = tp.tv_sec+((tp.tv_nsec/10000000L)+timeout)/1000L;
          tp.tv_nsec = tp.tv_nsec+(timeout%1000L)*10000000L;
          while (semaphore->readWriteLockCount > 0)
          {
            WAIT_TIMEOUT(DEBUG_READ_WRITE,"R",&semaphore->modified,&semaphore->lock,&tp,lockedFlag);
          }
        }
        else
        {
          while (semaphore->readWriteLockCount > 0)
          {
            WAIT(DEBUG_READ_WRITE,"R",&semaphore->modified,&semaphore->lock);
          }
        }
        assert(semaphore->readWriteLockCount == 0);

        // set/increment read-lock if there is no read/write-lock
        semaphore->readLockCount++;
        semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ;

        // decrement read request counter
        pthread_mutex_lock(&semaphore->requestLock);
        {
          assert(semaphore->readRequestCount > 0);
          semaphore->readRequestCount--;
        }
        pthread_mutex_unlock(&semaphore->requestLock);
      }
      UNLOCK(DEBUG_READ,"R",&semaphore->lock,semaphore->readLockCount);
      break;
    case SEMAPHORE_LOCK_TYPE_READ_WRITE:
      // request write lock
      pthread_mutex_lock(&semaphore->requestLock);
      {
        semaphore->readWriteRequestCount++;
      }
      pthread_mutex_unlock(&semaphore->requestLock);

      // lock
      LOCK(DEBUG_READ_WRITE,"RW",&semaphore->lock);

      #ifndef NDEBUG
        // debug lock trace code
        if (semaphore->lockedByCount < SIZE_OF_ARRAY(semaphore->lockedBy))
        {
          semaphore->lockedBy[semaphore->lockedByCount].thread   = pthread_self();
          semaphore->lockedBy[semaphore->lockedByCount].fileName = fileName;
          semaphore->lockedBy[semaphore->lockedByCount].lineNb   = lineNb;
          semaphore->lockedByCount++;
        }
        else
        {
          fprintf(stderr,
                  "DEBUG WARNING: too many thread locks for semaphore '%s' at %s, line %lu (max. %lu)!\n",
                  semaphore->name,
                  fileName,
                  lineNb,
                  SIZE_OF_ARRAY(semaphore->lockedBy)
                 );
        }
      #endif /* not NDEBUG */

      // wait until no more read-locks
      if (timeout != SEMAPHORE_WAIT_FOREVER)
      {
        clock_gettime(CLOCK_REALTIME,&tp);
        tp.tv_sec  = tp.tv_sec+((tp.tv_nsec/10000000L)+timeout)/1000L;
        tp.tv_nsec = tp.tv_nsec+(timeout%1000L)*10000000L;
        while (semaphore->readLockCount > 0)
        {
          WAIT_TIMEOUT(DEBUG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock,&tp,lockedFlag);
        }
      }
      else
      {
        while (semaphore->readLockCount > 0)
        {
          WAIT(DEBUG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock);
        }
      }
      assert(semaphore->readLockCount == 0);

      // set/increment read/write-lock
      semaphore->readWriteLockCount++;
      semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ_WRITE;

      // decrement write request counter
      pthread_mutex_lock(&semaphore->requestLock);
      {
        assert(semaphore->readWriteRequestCount > 0);
        semaphore->readWriteRequestCount--;
      }
      pthread_mutex_unlock(&semaphore->requestLock);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return lockedFlag;
}

/***********************************************************************\
* Name   : unlock
* Purpose: unlock semaphore
* Input  : semaphore - semaphore
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
LOCAL void unlock(Semaphore *semaphore)
#else /* not NDEBUG */
LOCAL void unlock(const char *fileName, ulong lineNb, Semaphore *semaphore)
#endif /* NDEBUG */
{
  #ifndef NDEBUG
    pthread_t threadSelf;
    uint      z;
  #endif /* not NDEBUG */

  assert(semaphore != NULL);

  switch (semaphore->lockType)
  {
    case SEMAPHORE_LOCK_TYPE_NONE:
      break;
    case SEMAPHORE_LOCK_TYPE_READ:
      LOCK(DEBUG_READ,"R",&semaphore->lock);
      {
        assert(semaphore->readLockCount > 0);
        assert(semaphore->readWriteLockCount == 0);

        #ifndef NDEBUG
          // debug lock trace code
          threadSelf = pthread_self();
          z = 0;
          while (   (z < semaphore->lockedByCount)
                 && (pthread_equal(threadSelf,semaphore->lockedBy[z].thread) == 0)
                )
          {
            z++;
          }
          if (z < semaphore->lockedByCount)
          {
            memset(&semaphore->lockedBy[z],0,sizeof(semaphore->lockedBy[z]));
            if (semaphore->lockedByCount > 1)
            {
              semaphore->lockedBy[z] = semaphore->lockedBy[semaphore->lockedByCount-1];
              memset(&semaphore->lockedBy[semaphore->lockedByCount-1],0,sizeof(semaphore->lockedBy[semaphore->lockedByCount-1]));
            }
            semaphore->lockedByCount--;
          }
          else
          {
            Semaphore_debugPrintInfo();
            HALT_INTERNAL_ERROR("Thread 0x%lx try to unlock not locked semaphore '%s' at %s, line %lu!",
                                threadSelf,
                                semaphore->name,
                                fileName,
                                lineNb
                               );
          }
        #endif /* not NDEBUG */

        // do one read-unlock
        semaphore->readLockCount--;
        if (semaphore->readLockCount == 0)
        {
          // semaphore is free
          semaphore->lockType = SEMAPHORE_LOCK_TYPE_NONE;

          // signal that read-lock count become 0
          SIGNAL(DEBUG_READ,"READ0 (unlock)",&semaphore->readLockZero);
        }
      }
      UNLOCK(DEBUG_READ,"R",&semaphore->lock,semaphore->readLockCount);
      break;
    case SEMAPHORE_LOCK_TYPE_READ_WRITE:
      assert(semaphore->readLockCount == 0);
      assert(semaphore->readWriteLockCount > 0);

      #ifndef NDEBUG
        // debug lock trace code
        threadSelf = pthread_self();
        z = 0;
        while (   (z < semaphore->lockedByCount)
               && (pthread_equal(threadSelf,semaphore->lockedBy[z].thread) == 0)
              )
        {
          z++;
        }
        if (z < semaphore->lockedByCount)
        {
          memset(&semaphore->lockedBy[z],0,sizeof(semaphore->lockedBy[z]));
          if (semaphore->lockedByCount > 1)
          {
            semaphore->lockedBy[z] = semaphore->lockedBy[semaphore->lockedByCount-1];
            memset(&semaphore->lockedBy[semaphore->lockedByCount-1],0,sizeof(semaphore->lockedBy[semaphore->lockedByCount-1]));
          }
          semaphore->lockedByCount--;
        }
        else
        {
          Semaphore_debugPrintInfo();
          HALT_INTERNAL_ERROR("Thread 0x%lx try to unlock not locked semaphore '%s' at %s, line %lu!",
                              threadSelf,
                              semaphore->name,
                              fileName,
                              lineNb
                             );
        }
      #endif /* not NDEBUG */

      // do one read/write-unlock
      semaphore->readWriteLockCount--;
      if (semaphore->readWriteLockCount == 0)
      {
        // semaphore is free
        semaphore->lockType = SEMAPHORE_LOCK_TYPE_NONE;

        // send modified signal
        SIGNAL(DEBUG_READ_WRITE,"MODIFIED",&semaphore->modified);

        // unlock
        UNLOCK(DEBUG_READ_WRITE,"RW",&semaphore->lock,semaphore->readLockCount);
      }
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }
}

/***********************************************************************\
* Name   : isLocked
* Purpose: check if semaphore is locked
* Input  : semaphore - semaphore
* Output : -
* Return : TRUE if semaphore is locked, FALSE otherwise
* Notes  : -
\***********************************************************************/

LOCAL bool isLocked(Semaphore *semaphore)
{
  assert(semaphore != NULL);

  return semaphore->lockType != SEMAPHORE_LOCK_TYPE_NONE;
}

/***********************************************************************\
* Name   : waitModified
* Purpose: wait until semaphore is modified
* Input  : semaphore - semaphore
*          timeout   - timeout [ms] or SEMAPHORE_WAIT_FOREVER
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
LOCAL bool waitModified(Semaphore *semaphore,
                        long      timeout
                       )
#else /* not NDEBUG */
LOCAL bool waitModified(const char *fileName,
                        ulong      lineNb,
                        Semaphore  *semaphore,
                        long       timeout
                       )
#endif /* NDEBUG */
{
  uint            savedReadWriteLockCount;
  struct timespec tp;
  bool            lockedFlag;

  assert(semaphore != NULL);
  assert(semaphore->lockType != SEMAPHORE_LOCK_TYPE_NONE);
  assert((semaphore->readLockCount > 0) || (semaphore->lockType == SEMAPHORE_LOCK_TYPE_READ_WRITE));

  #ifndef NDEBUG
    UNUSED_VARIABLE(fileName);
    UNUSED_VARIABLE(lineNb);
  #endif /* not NDEBUG */

  lockedFlag = TRUE;

  switch (semaphore->lockType)
  {
    case SEMAPHORE_LOCK_TYPE_NONE:
      break;
    case SEMAPHORE_LOCK_TYPE_READ:
      LOCK(DEBUG_READ,"R",&semaphore->lock);
      {
        assert(semaphore->readLockCount > 0);
        assert(semaphore->readWriteLockCount == 0);

        // temporary revert read-lock
        semaphore->readLockCount--;
        if (semaphore->readLockCount == 0)
        {
          // semaphore is free
          semaphore->lockType = SEMAPHORE_LOCK_TYPE_NONE;

          // signal that read-lock count become 0
          SIGNAL(DEBUG_READ,"READ0 (wait)",&semaphore->readLockZero);
        }
        SIGNAL(DEBUG_READ_WRITE,"MODIFIED",&semaphore->modified);

        if (timeout != SEMAPHORE_WAIT_FOREVER)
        {
          clock_gettime(CLOCK_REALTIME,&tp);
          tp.tv_sec  = tp.tv_sec+((tp.tv_nsec/10000000L)+timeout)/1000L;
          tp.tv_nsec = tp.tv_nsec+(timeout%1000L)*10000000L;

          // wait for modification
          WAIT_TIMEOUT(DEBUG_READ,"MODIFIED",&semaphore->modified,&semaphore->lock,&tp,lockedFlag);

          // wait until there are no more write-locks
          while (semaphore->readWriteLockCount > 0)
          {
            WAIT_TIMEOUT(DEBUG_READ,"W",&semaphore->modified,&semaphore->lock,&tp,lockedFlag);
          }
        }
        else
        {
          // wait for modification
          WAIT(DEBUG_READ,"MODIFIED",&semaphore->modified,&semaphore->lock);

          // wait until there are no more write-locks
          while (semaphore->readWriteLockCount > 0)
          {
            WAIT(DEBUG_READ,"W",&semaphore->modified,&semaphore->lock);
          }
        }

        // restore temporary reverted read-lock
        semaphore->readLockCount++;
        semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ;
      }
      UNLOCK(DEBUG_READ,"R",&semaphore->lock,semaphore->readLockCount);
      break;
    case SEMAPHORE_LOCK_TYPE_READ_WRITE:
      assert(semaphore->readLockCount == 0);
      assert(semaphore->readWriteLockCount > 0);

      // temporary revert write-lock
      savedReadWriteLockCount = semaphore->readWriteLockCount;
      semaphore->readWriteLockCount = 0;
      semaphore->lockType = SEMAPHORE_LOCK_TYPE_NONE;
      SIGNAL(DEBUG_READ_WRITE,"MODIFIED",&semaphore->modified);

      // wait for modification
      if (timeout != SEMAPHORE_WAIT_FOREVER)
      {
        clock_gettime(CLOCK_REALTIME,&tp);
        tp.tv_sec  = tp.tv_sec+((tp.tv_nsec/10000000L)+timeout)/1000L;
        tp.tv_nsec = tp.tv_nsec+(timeout%1000L)*10000000L;
        WAIT_TIMEOUT(DEBUG_READ_WRITE,"MODIFIED",&semaphore->modified,&semaphore->lock,&tp,lockedFlag);
      }
      else
      {
        WAIT(DEBUG_READ_WRITE,"MODIFIED",&semaphore->modified,&semaphore->lock);
      }

      // request write-lock
      pthread_mutex_lock(&semaphore->requestLock);
      {
        semaphore->readWriteRequestCount++;
      }
      pthread_mutex_unlock(&semaphore->requestLock);

      // wait until no more read-locks
      if (timeout != SEMAPHORE_WAIT_FOREVER)
      {
        clock_gettime(CLOCK_REALTIME,&tp);
        tp.tv_sec  = tp.tv_sec+((tp.tv_nsec/10000000L)+timeout)/1000L;
        tp.tv_nsec = tp.tv_nsec+(timeout%1000L)*10000000L;
        while (semaphore->readLockCount > 0)
        {
          WAIT_TIMEOUT(DEBUG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock,&tp,lockedFlag);
        }
      }
      else
      {
        while (semaphore->readLockCount > 0)
        {
          WAIT(DEBUG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock);
        }
      }
      assert(semaphore->readLockCount == 0);

      // decrement write request counter
      pthread_mutex_lock(&semaphore->requestLock);
      {
        assert(semaphore->readWriteRequestCount > 0);
        semaphore->readWriteRequestCount--;
      }
      pthread_mutex_unlock(&semaphore->requestLock);

      // restore temporary reverted write-lock
      assert(semaphore->readWriteLockCount == 0);
      semaphore->readWriteLockCount = savedReadWriteLockCount;
      semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ_WRITE;
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return lockedFlag;
}

/*---------------------------------------------------------------------*/

#ifdef NDEBUG
bool Semaphore_init(Semaphore *semaphore)
#else /* not NDEBUG */
bool __Semaphore_init(const char *name, Semaphore *semaphore)
#endif /* NDEBUG */
{
  assert(semaphore != NULL);

  if (pthread_mutex_init(&semaphore->requestLock,NULL) != 0)
  {
    return FALSE;
  }
  semaphore->readRequestCount      = 0;
  semaphore->readWriteRequestCount = 0;

#if 0
  pthread_mutexattr_init(&semaphore->lockAttributes);
  pthread_mutexattr_settype(&semaphore->lockAttributes,PTHREAD_MUTEX_RECURSIVE);
#endif /* 0 */
  if (pthread_mutex_init(&semaphore->lock,NULL) != 0)
  {
    pthread_mutex_destroy(&semaphore->requestLock);
    return FALSE;
  }
  if (pthread_cond_init(&semaphore->readLockZero,NULL) != 0)
  {
    pthread_mutex_destroy(&semaphore->lock);
    pthread_mutex_destroy(&semaphore->requestLock);
    return FALSE;
  }
  if (pthread_cond_init(&semaphore->modified,NULL) != 0)
  {
    pthread_cond_destroy(&semaphore->readLockZero);
    pthread_mutex_destroy(&semaphore->lock);
//    pthread_mutexattr_destroy(&semaphore->lockAttributes);
    pthread_mutex_destroy(&semaphore->requestLock);
    return FALSE;
  }
  semaphore->lockType           = SEMAPHORE_LOCK_TYPE_NONE;
  semaphore->readLockCount      = 0;
  semaphore->readWriteLockCount = 0;
  semaphore->endFlag            = FALSE;

  #ifndef NDEBUG
    pthread_once(&debugSemaphoreInitFlag,Semaphore_debugInit);

    semaphore->name          = name;
    memset(semaphore->lockedBy,0,sizeof(semaphore->lockedBy));
    semaphore->lockedByCount = 0;

    List_append(&debugSemaphoreList,semaphore);
  #endif /* not NDEBUG */

  return TRUE;
}

void Semaphore_done(Semaphore *semaphore)
{
  #ifndef NDEBUG
    uint z;
  #endif /* not NDEBUG */

  assert(semaphore != NULL);

  /* lock to avoid further usage */
  LOCK(DEBUG_READ_WRITE,"D",&semaphore->lock);

  #ifndef NDEBUG
    pthread_once(&debugSemaphoreInitFlag,Semaphore_debugInit);

    for (z = 0; z < semaphore->lockedByCount; z++)
    {
      fprintf(stderr,
              "DEBUG WARNING: thread 0x%lx did not unlocked semaphore '%s' locked at %s, line %lu!\n",
              semaphore->lockedBy[z].thread,
              semaphore->name,
              semaphore->lockedBy[z].fileName,
              semaphore->lockedBy[z].lineNb
             );
    }

    List_remove(&debugSemaphoreList,semaphore);
  #endif /* not NDEBUG */

  /* free resources */
  pthread_cond_destroy(&semaphore->modified);
  pthread_cond_destroy(&semaphore->readLockZero);
  pthread_mutex_destroy(&semaphore->lock);
//  pthread_mutexattr_destroy(&semaphore->lockAttributes);
  pthread_mutex_destroy(&semaphore->requestLock);
}

#ifdef NDEBUG
Semaphore *Semaphore_new(void)
#else /* not NDEBUG */
Semaphore *__Semaphore_new(const char *name)
#endif /* NDEBUG */
{
  Semaphore *semaphore;

  semaphore = (Semaphore*)malloc(sizeof(Semaphore));
  if (semaphore != NULL)
  {
    #ifdef NDEBUG
      if (!Semaphore_init(semaphore))
      {
        free(semaphore);
        return NULL;
      }
    #else /* not NDEBUG */
      if (!__Semaphore_init(name,semaphore))
      {
        free(semaphore);
        return NULL;
      }
    #endif /* NDEBUG */
  }
  else
  {
    return NULL;
  }

  return semaphore;
}

void Semaphore_delete(Semaphore *semaphore)
{
  assert(semaphore != NULL);

  Semaphore_done(semaphore);
  free(semaphore);
}

#ifdef NDEBUG
bool Semaphore_lock(Semaphore          *semaphore,
                    SemaphoreLockTypes semaphoreLockType,
                    long               timeout
                   )
#else /* not NDEBUG */
bool __Semaphore_lock(const char         *fileName,
                      ulong              lineNb,
                      Semaphore          *semaphore,
                      SemaphoreLockTypes semaphoreLockType,
                      long               timeout
                     )
#endif /* NDEBUG */
{
  assert(semaphore != NULL);

  #ifdef NDEBUG
    return lock(semaphore,semaphoreLockType,timeout);
  #else /* not NDEBUG */
    return lock(fileName,lineNb,semaphore,semaphoreLockType,timeout);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void Semaphore_unlock(Semaphore *semaphore)
#else /* not NDEBUG */
void __Semaphore_unlock(const char *fileName, ulong lineNb, Semaphore *semaphore)
#endif /* NDEBUG */
{
  assert(semaphore != NULL);

  #ifdef NDEBUG
    unlock(semaphore);
  #else /* not NDEBUG */
    unlock(fileName,lineNb,semaphore);
  #endif /* NDEBUG */
}

bool Semaphore_isLocked(Semaphore *semaphore)
{
  assert(semaphore != NULL);

  return isLocked(semaphore);
}

#ifdef NDEBUG
bool Semaphore_waitModified(Semaphore *semaphore,
                            long      timeout
                           )
#else /* not NDEBUG */
bool __Semaphore_waitModified(const char *fileName,
                              ulong      lineNb,
                              Semaphore  *semaphore,
                              long       timeout
                             )
#endif /* NDEBUG */
{
  assert(semaphore != NULL);

  if (!semaphore->endFlag)
  {
    #ifdef NDEBUG
      return waitModified(semaphore,timeout);
    #else /* not NDEBUG */
      return waitModified(fileName,lineNb,semaphore,timeout);
    #endif /* NDEBUG */
  }
  else
  {
    return TRUE;
  }
}

bool Semaphore_checkPending(Semaphore *semaphore)
{
  bool pendingFlag;

  assert(semaphore != NULL);

  pendingFlag = FALSE;
  if (!semaphore->endFlag)
  {
    switch (semaphore->lockType)
    {
      case SEMAPHORE_LOCK_TYPE_NONE:
        pendingFlag = FALSE;
        break;
      case SEMAPHORE_LOCK_TYPE_READ:
        pendingFlag = (semaphore->readWriteRequestCount > 0);
        break;
      case SEMAPHORE_LOCK_TYPE_READ_WRITE:
        pendingFlag = (semaphore->readRequestCount > 0);
        break;
    }
  }

  return pendingFlag;
}

void Semaphore_setEnd(Semaphore *semaphore)
{
  assert(semaphore != NULL);

  // lock
  #ifdef NDEBUG
    lock(semaphore,SEMAPHORE_LOCK_TYPE_READ,SEMAPHORE_WAIT_FOREVER);
  #else /* not NDEBUG */
    lock(__FILE__,__LINE__,semaphore,SEMAPHORE_LOCK_TYPE_READ,SEMAPHORE_WAIT_FOREVER);
  #endif /* NDEBUG */

  // set end flag
  semaphore->endFlag = TRUE;

  // send modified signal
  SIGNAL(DEBUG_READ_WRITE,"MODIFIED",&semaphore->modified);

  // unlock
  #ifdef NDEBUG
    unlock(semaphore);
  #else /* not NDEBUG */
    unlock(__FILE__,__LINE__,semaphore);
  #endif /* NDEBUG */
}

#ifdef __cplusplus
  }
#endif

/* end of file */
