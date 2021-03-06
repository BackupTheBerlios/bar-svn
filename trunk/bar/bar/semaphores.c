/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: functions for inter-process semaphores
* Systems: all POSIX
*
\***********************************************************************/

#define __SEMAPHORES_IMPLEMENATION__

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#if   defined(PLATFORM_LINUX)
  #include <pthread.h>
#elif defined(PLATFORM_WINDOWS)
  #include <windows.h>
#endif /* PLATFORM_... */

#include "global.h"
#include "lists.h"

#include "semaphores.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

#ifndef NDEBUG
  #define DEBUG_FLAG_READ       FALSE
  #define DEBUG_FLAG_READ_WRITE FALSE

  typedef enum
  {
    DEBUG_LOCK_TYPE_READ,
    DEBUG_LOCK_TYPE_WRITE,
    DEBUG_LOCK_TYPE_READ_WRITE,
    DEBUG_LOCK_TYPE_DELETE
  } DebugLockTypes;
#endif /* not NDEBUG */

/***************************** Datatypes *******************************/

#ifndef NDEBUG
typedef struct
{
  LIST_HEADER(Semaphore);
} DebugSemaphoreList;
#endif /* not NDEBUG */

/***************************** Variables *******************************/

#ifndef NDEBUG
  LOCAL pthread_once_t     debugSemaphoreInitFlag = PTHREAD_ONCE_INIT;
  LOCAL pthread_mutex_t    debugSemaphoreLock;
  LOCAL DebugSemaphoreList debugSemaphoreList;
  LOCAL void(*debugSemaphorePrevSignalHandler)(int);
#endif /* not NDEBUG */

/****************************** Macros *********************************/

#if   defined(PLATFORM_LINUX)
  #define __SEMAPHORE_REQUEST_LOCK(semaphore) \
    do \
    { \
      pthread_mutex_lock(&semaphore->requestLock); \
    } \
    while (0)

  #define __SEMAPHORE_REQUEST_UNLOCK(semaphore) \
    do \
    { \
      pthread_mutex_unlock(&semaphore->requestLock); \
    } \
    while (0)
#elif defined(PLATFORM_WINDOWS)
  #define __SEMAPHORE_REQUEST_LOCK(semaphore) \
    do \
    { \
      WaitForSingleObject(semaphore->requestLock,INFINITE); \
    } \
    while (0)

  #define __SEMAPHORE_REQUEST_UNLOCK(semaphore) \
    do \
    { \
      ReleaseMutex(semaphore->requestLock); \
    } \
    while (0)
#endif /* PLATFORM_... */

#ifndef NDEBUG

  #if   defined(PLATFORM_LINUX)
    #define __SEMAPHORE_LOCK(debugFlag,type,text,semaphore) \
      do \
      { \
        bool __locked; \
        \
        pthread_once(&debugSemaphoreInitFlag,debugInit); \
        \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x wait lock %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        \
        pthread_mutex_lock(&debugSemaphoreLock); \
        { \
          if (type == DEBUG_LOCK_TYPE_DELETE) checkUnlocked(semaphore); \
          __locked = (pthread_mutex_trylock(&semaphore->lock) == 0); \
          if (!__locked) \
          { \
            checkForDeadLock(semaphore,type); \
          } \
        } \
        pthread_mutex_unlock(&debugSemaphoreLock); \
        \
        if (!__locked) pthread_mutex_lock(&semaphore->lock); \
        \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x locked %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_LOCK_TIMEOUT(debugFlag,type,text,semaphore,timeout,lockedFlag) \
      do \
      { \
        struct timespec __tp; \
        \
        pthread_once(&debugSemaphoreInitFlag,debugInit); \
        pthread_mutex_lock(&debugSemaphoreLock); \
        { \
          checkForDeadLock(semaphore,type); \
        } \
        pthread_mutex_unlock(&debugSemaphoreLock); \
        \
        clock_gettime(CLOCK_REALTIME,&__tp); \
        __tp.tv_sec  = __tp.tv_sec+((__tp.tv_nsec/10000000L)+(timeout))/1000L; \
        __tp.tv_nsec = __tp.tv_nsec+((timeout)%1000L)*10000000L; \
        \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x wait lock %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        if (pthread_mutex_timedlock(&semaphore->lock,&__tp) != 0) lockedFlag = FALSE; \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x locked %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_TRYLOCK(debugFlag,type,text,semaphore,lockedFlag) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x wait lock %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        if (pthread_mutex_trylock(&semaphore->lock) != 0) lockedFlag = FALSE; \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x locked %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_UNLOCK(debugFlag,text,semaphore,n) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x unlock %s n=%d\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text,n); \
        pthread_mutex_unlock(&semaphore->lock); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT(debugFlag,text,condition,mutex) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x unlock+wait %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        pthread_cond_wait(condition,mutex); \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x waited+locked %s done\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT_TIMEOUT(debugFlag,text,condition,mutex,timeout,lockedFlag) \
      do \
      { \
        struct timespec __tp; \
        \
        clock_gettime(CLOCK_REALTIME,&__tp); \
        __tp.tv_sec  = __tp.tv_sec+((__tp.tv_nsec/10000000L)+(timeout))/1000L; \
        __tp.tv_nsec = __tp.tv_nsec+((timeout)%1000L)*10000000L; \
        \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x unlock+wait %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        if (pthread_cond_timedwait(condition,mutex,&__tp) != 0) lockedFlag = FALSE; \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x waited+locked %s done\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_SIGNAL(debugFlag,text,condition) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x signal %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        pthread_cond_signal(condition); \
      } \
      while (0)
  #elif defined(PLATFORM_WINDOWS)
    #define __SEMAPHORE_LOCK(debugFlag,type,text,semaphore) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x wait lock %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        WaitForSingleObject(semaphore,INFINITE); \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x locked %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_LOCK_TIMEOUT(debugFlag,type,text,semaphore,timeout,lockedFlag) \
      do \
      { \
        struct timespec __tp; \
        \
        clock_gettime(CLOCK_REALTIME,&__tp); \
        __tp.tv_sec  = __tp.tv_sec+((__tp.tv_nsec/10000000L)+(timeout))/1000L; \
        __tp.tv_nsec = __tp.tv_nsec+((timeout)%1000L)*10000000L; \
        \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x wait lock %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        if (WaitForSingleObject(semaphore,&__tp) != WAIT_OBJECT_0) lockedFlag = FALSE; \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x locked %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_TRYLOCK(debugFlag,text,semaphore,lockedFlag) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x wait lock %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        if (WaitForSingleObject(semaphore,0) != WAIT_OBJECT_0) lockedFlag = FALSE; \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x locked %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_UNLOCK(debugFlag,text,semaphore,n) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x unlock %s n=%d\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text,n); \
        ReleaseMutext(semaphore); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT(debugFlag,text,condition,mutex) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x unlock+wait %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        pthread_cond_wait(condition,mutex); \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x waited+locked %s done\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT_TIMEOUT(debugFlag,text,condition,mutex,timeout,lockedFlag) \
      do \
      { \
        struct timespec __tp; \
        \
        clock_gettime(CLOCK_REALTIME,&__tp); \
        __tp.tv_sec  = __tp.tv_sec+((__tp.tv_nsec/10000000L)+(timeout))/1000L; \
        __tp.tv_nsec = __tp.tv_nsec+((timeout)%1000L)*10000000L; \
        \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x unlock+wait %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        if (pthread_cond_timedwait(condition,mutex,&__tp) != 0) lockedFlag = FALSE; \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x waited+locked %s done\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
      } \
      while (0)

    #define __SEMAPHORE_SIGNAL(debugFlag,text,condition) \
      do \
      { \
        if (debugFlag) fprintf(stderr,"%s, %4d: 0x%x signal %s\n",__FILE__,__LINE__,(unsigned int)pthread_self(),text); \
        pthread_cond_signal(condition); \
      } \
      while (0)
  #endif /* PLATFORM_... */

#else /* NDEBUG */

  #if   defined(PLATFORM_LINUX)
    #define __SEMAPHORE_LOCK(debugFlag,type,text,semaphore) \
      do \
      { \
        pthread_mutex_lock(&semaphore->lock); \
      } \
      while (0)

    #define __SEMAPHORE_LOCK_TIMEOUT(debugFlag,type,text,semaphore,timeout,lockedFlag) \
      do \
      { \
        struct timespec __tp; \
        \
        clock_gettime(CLOCK_REALTIME,&__tp); \
        __tp.tv_sec  = __tp.tv_sec+((__tp.tv_nsec/10000000L)+(timeout))/1000L; \
        __tp.tv_nsec = __tp.tv_nsec+((timeout)%1000L)*10000000L; \
        \
        if (pthread_mutex_timedlock(&semaphore->lock,&__tp) != 0) lockedFlag = FALSE; \
      } \
      while (0)

    #define __SEMAPHORE_TRYLOCK(debugFlag,text,semaphore,lockedFlag) \
      do \
      { \
        if (pthread_mutex_trylock(&semaphore->lock) != 0) lockedFlag = FALSE; \
      } \
      while (0)

    #define __SEMAPHORE_UNLOCK(debugFlag,text,semaphore,n) \
      do \
      { \
        pthread_mutex_unlock(&semaphore->lock); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT(debugFlag,text,condition,mutex) \
      do \
      { \
        pthread_cond_wait(condition,mutex); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT_TIMEOUT(debugFlag,text,condition,mutex,timeout,lockedFlag) \
      do \
      { \
        struct timespec __tp; \
        \
        clock_gettime(CLOCK_REALTIME,&__tp); \
        __tp.tv_sec  = __tp.tv_sec+((__tp.tv_nsec/10000000L)+(timeout))/1000L; \
        __tp.tv_nsec = __tp.tv_nsec+((timeout)%1000L)*10000000L; \
        \
        if (pthread_cond_timedwait(condition,mutex,&__tp) != 0) lockedFlag = FALSE; \
      } \
      while (0)

    #define __SEMAPHORE_SIGNAL(debugFlag,text,condition) \
      do \
      { \
        pthread_cond_broadcast(condition); \
      } \
      while (0)
  #elif defined(PLATFORM_WINDOWS)
    #define __SEMAPHORE_LOCK(debugFlag,type,text,semaphore) \
      do \
      { \
        pthread_mutex_lock(semaphore); \
      } \
      while (0)

    #define __SEMAPHORE_LOCK_TIMEOUT(debugFlag,type,text,semaphore,timeout,lockedFlag) \
      do \
      { \
        if (WaitForSingleObject(semaphore,timeout) != WAIT_OBJECT_0) lockedFlag = FALSE; \
      } \
      while (0)

    #define __SEMAPHORE_TRYLOCK(debugFlag,text,semaphore,lockedFlag) \
      do \
      { \
        if (WaitForSingleObject(semaphore,0) != WAIT_OBJECT_0) lockedFlag = FALSE; \
      } \
      while (0)

    #define __SEMAPHORE_UNLOCK(debugFlag,text,semaphore,n) \
      do \
      { \
        pthread_mutex_unlock(semaphore); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT(debugFlag,text,condition,semaphore) \
      do \
      { \
        pthread_cond_wait(condition,semaphore); \
      } \
      while (0)

    #define __SEMAPHORE_WAIT_TIMEOUT(debugFlag,text,condition,semaphore,timeout,lockedFlag) \
      do \
      { \
        if (pthread_cond_timedwait(condition,semaphore,timeout) == ETIMEDOUT) lockedFlag = FALSE; \
      } \
      while (0)

    #define __SEMAPHORE_SIGNAL(debugFlag,text,condition) \
      do \
      { \
        pthread_cond_broadcast(condition); \
      } \
      while (0)
  #endif /* PLATFORM_... */

#endif /* not NDEBUG */

/***************************** Forwards ********************************/
#ifndef NDEBUG
  LOCAL void debugSemaphoreSignalHandler(int signalNumber);
#endif /* not NDEBUG */

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

#ifndef NDEBUG
/***********************************************************************\
* Name   : debugInit
* Purpose: initialize debug functions
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void debugInit(void)
{
  // init variables
  pthread_mutex_init(&debugSemaphoreLock,NULL);
  List_init(&debugSemaphoreList);

  // install signal handler for Ctrl-\ (SIGQUIT) for printing debug information
  debugSemaphorePrevSignalHandler = signal(SIGQUIT,debugSemaphoreSignalHandler);
}

/***********************************************************************\
* Name   : signalHandler
* Purpose: signal handler
* Input  : signalNumber - signal number
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void debugSemaphoreSignalHandler(int signalNumber)
{
  if (signalNumber == SIGQUIT)
  {
    Semaphore_debugPrintInfo();
  }
  if (debugSemaphorePrevSignalHandler != NULL)
  {
    debugSemaphorePrevSignalHandler(signalNumber);
  }
}

LOCAL void checkForDeadLock(Semaphore *semaphore, DebugLockTypes debugLockType)
{
  assert(semaphore != NULL);

  UNUSED_VARIABLE(semaphore);
  UNUSED_VARIABLE(debugLockType);

//fprintf(stderr,"%s, %d: \n",__FILE__,__LINE__);
}

LOCAL void checkUnlocked(Semaphore *semaphore)
{
  assert(semaphore != NULL);

  if (semaphore->lockedByCount > 0)
  {
    HALT_INTERNAL_ERROR("Thread 0x%lx did not unlock semaphore '%s' which was locked at %s, line %lu!",
                        semaphore->lockedBy[0].thread,
                        semaphore->name,
                        semaphore->lockedBy[0].fileName,
                        semaphore->lockedBy[0].lineNb
                       );
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
  bool lockedFlag;

  assert(semaphore != NULL);
  assert((semaphoreLockType == SEMAPHORE_LOCK_TYPE_READ) || (semaphoreLockType == SEMAPHORE_LOCK_TYPE_READ_WRITE));

  lockedFlag = TRUE;

  switch (semaphoreLockType)
  {
    case SEMAPHORE_LOCK_TYPE_NONE:
      break;
    case SEMAPHORE_LOCK_TYPE_READ:
      /* request read lock
         Note: for a read lock semaphore is locked temporary and read-lock is stored
      */

      // increment read request counter atomically
      __SEMAPHORE_REQUEST_LOCK(semaphore);
      {
        semaphore->readRequestCount++;
      }
      __SEMAPHORE_REQUEST_UNLOCK(semaphore);

      // lock temporary
      if (timeout != SEMAPHORE_WAIT_FOREVER)
      {
        __SEMAPHORE_LOCK_TIMEOUT(DEBUG_FLAG_READ,DEBUG_LOCK_TYPE_READ,"R",semaphore,timeout,lockedFlag);
        if (!lockedFlag)
        {
          __SEMAPHORE_REQUEST_LOCK(semaphore);
          {
            semaphore->readRequestCount++;
          }
          __SEMAPHORE_REQUEST_UNLOCK(semaphore);
          return FALSE;
        }
      }
      else
      {
        __SEMAPHORE_LOCK(DEBUG_FLAG_READ,DEBUG_LOCK_TYPE_READ,"R",semaphore);
      }
      {
        if (semaphore->readWriteLockCount > 0)
        {
          #ifndef NDEBUG
            assert(semaphore->lockedByCount > 0);

            HALT_INTERNAL_ERROR("Thread 0x%lx try to lock semaphore '%s' with weaker access 'read' at %s, line %lu which was previously locked 'read/write' at %s, line %lu !",
                                pthread_self(),
                                semaphore->name,
                                fileName,
                                lineNb,
                                semaphore->lockedBy[semaphore->lockedByCount-1].fileName,
                                semaphore->lockedBy[semaphore->lockedByCount-1].lineNb
                               );
          #else /* NDEBUG */
            HALT_INTERNAL_ERROR("Thread 0x%lx try to lock semaphore with weaker 'read' access!",
                                pthread_self()
                               );
          #endif /* not NDEBUG */
        }

        #ifndef NDEBUG
          // debug trace code: store lock information
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

#if 0
        // wait until no more other read/write-locks
        if      (timeout != SEMAPHORE_WAIT_FOREVER)
        {
          while (semaphore->readWriteLockCount > 0)
          {
            __SEMAPHORE_WAIT_TIMEOUT(DEBUG_FLAG_READ_WRITE,"R",&semaphore->modified,&semaphore->lock,timeout,lockedFlag);
            if (lockedFlag)
            {
              assert(semaphore->lockedByCount > 0);
              semaphore->lockedByCount--;
              __SEMAPHORE_UNLOCK(DEBUG_FLAG_READ,"R",semaphore,semaphore->readLockCount);
              __SEMAPHORE_REQUEST_LOCK(semaphore);
              {
                semaphore->readRequestCount++;
              }
              __SEMAPHORE_REQUEST_UNLOCK(semaphore);
              return FALSE;
            }
          }
        }
        else
        {
          while (semaphore->readWriteLockCount > 0)
          {
            __SEMAPHORE_WAIT(DEBUG_FLAG_READ_WRITE,"R",&semaphore->modified,&semaphore->lock);
          }
        }
        assert(semaphore->readWriteLockCount == 0);
#endif /* 0 */

        // set/increment read-lock if there is no read/write-lock
        semaphore->readLockCount++;
        semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ;

        // decrement read request counter atomically
        __SEMAPHORE_REQUEST_LOCK(semaphore);
        {
          assert(semaphore->readRequestCount > 0);
          semaphore->readRequestCount--;
        }
        __SEMAPHORE_REQUEST_UNLOCK(semaphore);
      }
      __SEMAPHORE_UNLOCK(DEBUG_FLAG_READ,"R",semaphore,semaphore->readLockCount);
      break;
    case SEMAPHORE_LOCK_TYPE_READ_WRITE:
      /* request write lock
         Note: for a read/write lock semaphore is locked permanent
      */

      // increment read/write request counter atomically
      __SEMAPHORE_REQUEST_LOCK(semaphore);
      {
        semaphore->readWriteRequestCount++;
      }
      __SEMAPHORE_REQUEST_UNLOCK(semaphore);

      // lock permanent
      if (timeout != SEMAPHORE_WAIT_FOREVER)
      {
        __SEMAPHORE_LOCK_TIMEOUT(DEBUG_FLAG_READ_WRITE,DEBUG_LOCK_TYPE_READ_WRITE,"RW",semaphore,timeout,lockedFlag);
        if (!lockedFlag)
        {
          __SEMAPHORE_REQUEST_LOCK(semaphore);
          {
            assert(semaphore->readWriteRequestCount > 0);
            semaphore->readWriteRequestCount--;
          }
          __SEMAPHORE_REQUEST_UNLOCK(semaphore);
          return FALSE;
        }
      }
      else
      {
        __SEMAPHORE_LOCK(DEBUG_FLAG_READ_WRITE,DEBUG_LOCK_TYPE_READ_WRITE,"RW",semaphore);
      }

      #ifndef NDEBUG
        // debug trace code: store lock information
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
        while (semaphore->readLockCount > 0)
        {
          __SEMAPHORE_WAIT_TIMEOUT(DEBUG_FLAG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock,timeout,lockedFlag);
          if (!lockedFlag)
          {
            #ifndef NDEBUG
              assert(semaphore->lockedByCount > 0);
              semaphore->lockedByCount--;
            #endif /* not NDEBUG */
            __SEMAPHORE_REQUEST_LOCK(semaphore);
            {
              assert(semaphore->readWriteRequestCount > 0);
              semaphore->readWriteRequestCount--;
            }
            __SEMAPHORE_REQUEST_UNLOCK(semaphore);
            return FALSE;
          }
        }
      }
      else
      {
        while (semaphore->readLockCount > 0)
        {
          __SEMAPHORE_WAIT(DEBUG_FLAG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock);
        }
      }
      assert(semaphore->readLockCount == 0);

      // set/increment read/write-lock
      semaphore->readWriteLockCount++;
      semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ_WRITE;

      // decrement read/write request counter atomically
      __SEMAPHORE_REQUEST_LOCK(semaphore);
      {
        assert(semaphore->readWriteRequestCount > 0);
        semaphore->readWriteRequestCount--;
      }
      __SEMAPHORE_REQUEST_UNLOCK(semaphore);
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
      __SEMAPHORE_LOCK(DEBUG_FLAG_READ,DEBUG_LOCK_TYPE_READ,"R",semaphore);
      {
        assert(semaphore->readLockCount > 0);

        #ifndef NDEBUG
          // debug lock code: remove lock information
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
          __SEMAPHORE_SIGNAL(DEBUG_FLAG_READ,"READ0 (unlock)",&semaphore->readLockZero);
        }
      }
      __SEMAPHORE_UNLOCK(DEBUG_FLAG_READ,"R",semaphore,semaphore->readLockCount);
      break;
    case SEMAPHORE_LOCK_TYPE_READ_WRITE:
      assert(semaphore->readLockCount == 0);
      assert(semaphore->readWriteLockCount > 0);

      #ifndef NDEBUG
        // debug trace code: remove lock information
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
        __SEMAPHORE_SIGNAL(DEBUG_FLAG_READ_WRITE,"MODIFIED",&semaphore->modified);
      }

      // unlock
      __SEMAPHORE_UNLOCK(DEBUG_FLAG_READ_WRITE,"RW",semaphore,semaphore->readLockCount);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }
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
  uint savedReadWriteLockCount;
  bool lockedFlag;

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
      // semaphore is read-locked -> temporary revert own read-lock and wait for modification signal
      __SEMAPHORE_LOCK(DEBUG_FLAG_READ,DEBUG_LOCK_TYPE_READ,"R",semaphore);
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
          __SEMAPHORE_SIGNAL(DEBUG_FLAG_READ,"READ0 (wait)",&semaphore->readLockZero);
        }
        __SEMAPHORE_SIGNAL(DEBUG_FLAG_READ_WRITE,"MODIFIED",&semaphore->modified);

        if (timeout != SEMAPHORE_WAIT_FOREVER)
        {
          // wait for modification
          __SEMAPHORE_WAIT_TIMEOUT(DEBUG_FLAG_READ,"MODIFIED",&semaphore->modified,&semaphore->lock,timeout,lockedFlag);

          // wait until there are no more write-locks
          while (semaphore->readWriteLockCount > 0)
          {
            __SEMAPHORE_WAIT_TIMEOUT(DEBUG_FLAG_READ,"W",&semaphore->modified,&semaphore->lock,timeout,lockedFlag);
          }
        }
        else
        {
          // wait for modification
          __SEMAPHORE_WAIT(DEBUG_FLAG_READ,"MODIFIED",&semaphore->modified,&semaphore->lock);

          // wait until there are no more write-locks
          while (semaphore->readWriteLockCount > 0)
          {
            __SEMAPHORE_WAIT(DEBUG_FLAG_READ,"W",&semaphore->modified,&semaphore->lock);
          }
        }

        // restore temporary reverted read-lock
        semaphore->readLockCount++;
        semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ;
      }
      __SEMAPHORE_UNLOCK(DEBUG_FLAG_READ,"R",semaphore,semaphore->readLockCount);
      break;
    case SEMAPHORE_LOCK_TYPE_READ_WRITE:
      // semaphore is read/write-locked -> temporary revert own read/write-lock and wait for modification signal
      assert(semaphore->readLockCount == 0);
      assert(semaphore->readWriteLockCount > 0);

      // temporary revert write-lock (Note: no locking is required, because read/write-lock is already exclusive)
      savedReadWriteLockCount = semaphore->readWriteLockCount;
      semaphore->readWriteLockCount = 0;
      semaphore->lockType = SEMAPHORE_LOCK_TYPE_NONE;
      __SEMAPHORE_SIGNAL(DEBUG_FLAG_READ_WRITE,"MODIFIED",&semaphore->modified);

      // wait for modification
      if (timeout != SEMAPHORE_WAIT_FOREVER)
      {
        __SEMAPHORE_WAIT_TIMEOUT(DEBUG_FLAG_READ_WRITE,"MODIFIED",&semaphore->modified,&semaphore->lock,timeout,lockedFlag);
      }
      else
      {
        __SEMAPHORE_WAIT(DEBUG_FLAG_READ_WRITE,"MODIFIED",&semaphore->modified,&semaphore->lock);
      }

      // request write-lock
      __SEMAPHORE_REQUEST_LOCK(semaphore);
      {
        semaphore->readWriteRequestCount++;
      }
      __SEMAPHORE_REQUEST_UNLOCK(semaphore);

      // wait until no more read-locks
      if (timeout != SEMAPHORE_WAIT_FOREVER)
      {
        while (semaphore->readLockCount > 0)
        {
          __SEMAPHORE_WAIT_TIMEOUT(DEBUG_FLAG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock,timeout,lockedFlag);
        }
      }
      else
      {
        while (semaphore->readLockCount > 0)
        {
          __SEMAPHORE_WAIT(DEBUG_FLAG_READ_WRITE,"R",&semaphore->readLockZero,&semaphore->lock);
        }
      }
      assert(semaphore->readLockCount == 0);

      // restore temporary reverted write-lock
      __SEMAPHORE_REQUEST_LOCK(semaphore);
      {
        assert(semaphore->readWriteRequestCount > 0);
        semaphore->readWriteRequestCount--;

        assert(semaphore->readWriteLockCount == 0);
        semaphore->readWriteLockCount = savedReadWriteLockCount;
        semaphore->lockType = SEMAPHORE_LOCK_TYPE_READ_WRITE;
      }
      __SEMAPHORE_REQUEST_UNLOCK(semaphore);
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
bool __Semaphore_init(const char *fileName,
                      ulong      lineNb,
                      const char *name,
                      Semaphore  *semaphore)
#endif /* NDEBUG */
{
  assert(semaphore != NULL);

  if (pthread_mutex_init(&semaphore->requestLock,NULL) != 0)
  {
    return FALSE;
  }
  semaphore->readRequestCount      = 0;
  semaphore->readWriteRequestCount = 0;

  pthread_mutexattr_init(&semaphore->lockAttributes);
  pthread_mutexattr_settype(&semaphore->lockAttributes,PTHREAD_MUTEX_RECURSIVE);
  if (pthread_mutex_init(&semaphore->lock,&semaphore->lockAttributes) != 0)
  {
    pthread_mutexattr_destroy(&semaphore->lockAttributes);
    pthread_mutex_destroy(&semaphore->requestLock);
    return FALSE;
  }
  if (pthread_cond_init(&semaphore->readLockZero,NULL) != 0)
  {
    pthread_mutex_destroy(&semaphore->lock);
    pthread_mutexattr_destroy(&semaphore->lockAttributes);
    pthread_mutex_destroy(&semaphore->requestLock);
    return FALSE;
  }
  if (pthread_cond_init(&semaphore->modified,NULL) != 0)
  {
    pthread_cond_destroy(&semaphore->readLockZero);
    pthread_mutex_destroy(&semaphore->lock);
    pthread_mutexattr_destroy(&semaphore->lockAttributes);
    pthread_mutex_destroy(&semaphore->requestLock);
    return FALSE;
  }
  semaphore->lockType           = SEMAPHORE_LOCK_TYPE_NONE;
  semaphore->readLockCount      = 0;
  semaphore->readWriteLockCount = 0;
  semaphore->endFlag            = FALSE;

  #ifndef NDEBUG
    pthread_once(&debugSemaphoreInitFlag,debugInit);

    if (List_contains(&debugSemaphoreList,semaphore,CALLBACK(NULL,NULL)))
    {
      HALT_INTERNAL_ERROR_AT(fileName,lineNb,"Semaphore '%s' was already initialized at %s, line %lu!",
                             semaphore->name,
                             semaphore->fileName,
                             semaphore->lineNb
                            );
    }

    semaphore->fileName      = fileName;
    semaphore->lineNb        = lineNb;
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

  // lock to avoid further usage
  __SEMAPHORE_LOCK(DEBUG_FLAG_READ_WRITE,DEBUG_LOCK_TYPE_DELETE,"D",semaphore);

  #ifndef NDEBUG
    pthread_once(&debugSemaphoreInitFlag,debugInit);

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

  // free resources
  pthread_cond_destroy(&semaphore->modified);
  pthread_cond_destroy(&semaphore->readLockZero);
  pthread_mutex_destroy(&semaphore->lock);
  pthread_mutexattr_destroy(&semaphore->lockAttributes);
  pthread_mutex_destroy(&semaphore->requestLock);
}

#ifdef NDEBUG
Semaphore *Semaphore_new(void)
#else /* not NDEBUG */
Semaphore *__Semaphore_new(const char *fileName,
                           ulong      lineNb,
                           const char *name
                          )
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
      if (!__Semaphore_init(fileName,lineNb,name,semaphore))
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

bool Semaphore_isLockPending(Semaphore *semaphore, SemaphoreLockTypes semaphoreLockType)
{
  bool pendingFlag;

  assert(semaphore != NULL);

  pendingFlag = FALSE;
  if (!semaphore->endFlag)
  {
    switch (semaphoreLockType)
    {
      case SEMAPHORE_LOCK_TYPE_NONE:
        break;
      case SEMAPHORE_LOCK_TYPE_READ:
        pendingFlag = (semaphore->readRequestCount > 0) || (semaphore->readWriteRequestCount > 0);
        break;
      case SEMAPHORE_LOCK_TYPE_READ_WRITE:
        pendingFlag = (semaphore->readWriteRequestCount > 0);
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
  __SEMAPHORE_SIGNAL(DEBUG_FLAG_READ_WRITE,"MODIFIED",&semaphore->modified);

  // unlock
  #ifdef NDEBUG
    unlock(semaphore);
  #else /* not NDEBUG */
    unlock(__FILE__,__LINE__,semaphore);
  #endif /* NDEBUG */
}

#ifndef NDEBUG
/***********************************************************************\
* Name   : Semaphore_debugPrintInfo
* Purpose: print debug info
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Semaphore_debugPrintInfo(void)
{
  const Semaphore *semaphore;
  uint            z;

  fprintf(stderr,"\n");
  fprintf(stderr,"Semaphore debug info:\n");
  LIST_ITERATE(&debugSemaphoreList,semaphore)
  {
    switch (semaphore->lockType)
    {
      case SEMAPHORE_LOCK_TYPE_NONE:
        assert(semaphore->readLockCount == 0);
        assert(semaphore->readWriteLockCount == 0);
        break;
      case SEMAPHORE_LOCK_TYPE_READ:
        fprintf(stderr,"  '%s':\n",semaphore->name);
        fprintf(stderr,"    locked 'read'\n");
        for (z = 0; z < semaphore->lockedByCount; z++)
        {
          fprintf(stderr,
                  "    by thread 0x%lx at %s, line %lu\n",
                  semaphore->lockedBy[z].thread,
                  semaphore->lockedBy[z].fileName,
                  semaphore->lockedBy[z].lineNb
                 );
        }
        break;
      case SEMAPHORE_LOCK_TYPE_READ_WRITE:
        fprintf(stderr,"  '%s':\n",semaphore->name);
        fprintf(stderr,"    locked 'read/write'\n");
        for (z = 0; z < semaphore->lockedByCount; z++)
        {
          fprintf(stderr,
                  "    by thread 0x%lx at %s, line %lu\n",
                  semaphore->lockedBy[z].thread,
                  semaphore->lockedBy[z].fileName,
                  semaphore->lockedBy[z].lineNb
                 );
        }
        break;
      #ifndef NDEBUG
        default:
          HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
          break; /* not reached */
      #endif /* NDEBUG */
    }
  }
}
#endif /* not NDEBUG */

#ifdef __cplusplus
  }
#endif

/* end of file */
