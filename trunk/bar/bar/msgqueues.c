/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: functions for inter-process message queues
* Systems: all POSIX
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>

#include "global.h"
#include "lists.h"

#include "msgqueues.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/
typedef struct MsgNode
{
  LIST_NODE_HEADER(struct MsgNode);

  ulong size;
  byte  data[0];
} MsgNode;

typedef bool MsgQueueLock;

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***********************************************************************\
* Name   : MSGQUEUE_LOCKED_DO
* Purpose: execute block with message queue locked
* Input  : msgQueueLock - lock flag variable (MsgQueueLock)
*          msgQueue     - message queue
* Output : -
* Return : -
* Notes  : usage:
*            MsgQueueLock msgQueueLock;
*            MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue)
*            {
*              ...
*            }
*
*          message queue must be unlocked manually if break is used!
\***********************************************************************/

#define MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue) \
  for (lock(msgQueue), msgQueueLock = TRUE; \
       msgQueueLock; \
       unlock(msgQueue), msgQueueLock = FALSE \
      )

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : lock
* Purpose: lock message queue
* Input  : msgQueue - message queue
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void lock(MsgQueue *msgQueue)
{
  assert(msgQueue != NULL);

  pthread_mutex_lock(&msgQueue->lock);
  msgQueue->lockCount++;
}

/***********************************************************************\
* Name   : unlock
* Purpose: message queue unlock
* Input  : msgQueue - message queue
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void unlock(MsgQueue *msgQueue)
{
  assert(msgQueue != NULL);
  assert(msgQueue->lockCount > 0);

  if (msgQueue->modifiedFlag && (msgQueue->lockCount == 1))
  {
    pthread_cond_broadcast(&msgQueue->modified);
  }

  msgQueue->lockCount--;
  pthread_mutex_unlock(&msgQueue->lock);
}

/***********************************************************************\
* Name   : waitModified
* Purpose: wait until message is modified
* Input  : msgQueue - message queue
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void waitModified(MsgQueue *msgQueue)
{
  uint lockCount;
  uint z;

  assert(msgQueue != NULL);
  assert(msgQueue->lockCount > 0);

  lockCount = msgQueue->lockCount;

  for (z = 1; z < lockCount; z++) pthread_mutex_unlock(&msgQueue->lock);
  msgQueue->lockCount  = 0;
  pthread_cond_wait(&msgQueue->modified,&msgQueue->lock);
  msgQueue->lockCount  = lockCount;
  for (z = 1; z < lockCount; z++) pthread_mutex_lock(&msgQueue->lock);
}

/*---------------------------------------------------------------------*/

bool MsgQueue_init(MsgQueue *msgQueue, ulong maxMsgs)
{
  assert(msgQueue != NULL);

  msgQueue->maxMsgs = maxMsgs;
  pthread_mutexattr_init(&msgQueue->lockAttributes);
  pthread_mutexattr_settype(&msgQueue->lockAttributes,PTHREAD_MUTEX_RECURSIVE_NP);
  if (pthread_mutex_init(&msgQueue->lock,&msgQueue->lockAttributes) != 0)
  {
    return FALSE;
  }
  if (pthread_cond_init(&msgQueue->modified,NULL) != 0)
  {
    pthread_mutex_destroy(&msgQueue->lock);
    return FALSE;
  }
  msgQueue->modifiedFlag = FALSE;
  msgQueue->lockCount    = 0;
  msgQueue->endOfMsgFlag = FALSE;
  List_init(&msgQueue->list);

  return TRUE;
}

void MsgQueue_done(MsgQueue *msgQueue, MsgQueueMsgFreeFunction msgQueueMsgFreeFunction, void *msgQueueMsgFreeUserData)
{
  MsgNode *msgNode;

  assert(msgQueue != NULL);

  // lock
  lock(msgQueue);

  // discard all remaining messages
  while (!List_isEmpty(&msgQueue->list))
  {
    msgNode = (MsgNode*)List_getFirst(&msgQueue->list);

    if (msgQueueMsgFreeFunction != NULL)
    {
      msgQueueMsgFreeFunction(msgNode->data,msgQueueMsgFreeUserData);
    }
    free(msgNode);
  }

  // free resources
  pthread_cond_destroy(&msgQueue->modified);
  pthread_mutex_destroy(&msgQueue->lock);
  pthread_mutexattr_destroy(&msgQueue->lockAttributes);
}

MsgQueue *MsgQueue_new(ulong maxMsgs)
{
  MsgQueue *msgQueue;

  msgQueue = (MsgQueue*)malloc(sizeof(MsgQueue));
  if (msgQueue != NULL)
  {
    if (!MsgQueue_init(msgQueue,maxMsgs))
    {
      free(msgQueue);
      return NULL;
    }
  }
  else
  {
    return NULL;
  }

  return msgQueue;
}

void MsgQueue_delete(MsgQueue *msgQueue, MsgQueueMsgFreeFunction msgQueueMsgFreeFunction, void *msgQueueMsgFreeUserData)
{
  assert(msgQueue != NULL);

  MsgQueue_done(msgQueue,msgQueueMsgFreeFunction,msgQueueMsgFreeUserData);
  free(msgQueue);
}

void MsgQueue_clear(MsgQueue *msgQueue, MsgQueueMsgFreeFunction msgQueueMsgFreeFunction, void *msgQueueMsgFreeUserData)
{
  MsgQueueLock msgQueueLock;
  MsgNode      *msgNode;

  assert(msgQueue != NULL);

  MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue)
  {
    // discard all remaining messages
    while (!List_isEmpty(&msgQueue->list))
    {
      msgNode = (MsgNode*)List_getFirst(&msgQueue->list);

      if (msgQueueMsgFreeFunction != NULL)
      {
        msgQueueMsgFreeFunction(msgNode->data,msgQueueMsgFreeUserData);
      }
      free(msgNode);
    }
  }
}

void MsgQueue_lock(MsgQueue *msgQueue)
{
  assert(msgQueue != NULL);

  lock(msgQueue);
}

void MsgQueue_unlock(MsgQueue *msgQueue)
{
  assert(msgQueue != NULL);

  unlock(msgQueue);
}

bool MsgQueue_get(MsgQueue *msgQueue, void *msg, ulong *size, ulong maxSize)
{
  MsgQueueLock msgQueueLock;
  MsgNode      *msgNode;
  ulong        n;

  assert(msgQueue != NULL);

  MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue)
  {
    // wait for message
    while (!msgQueue->endOfMsgFlag && (List_count(&msgQueue->list) <= 0))
    {
      waitModified(msgQueue);
    }

    // get message
    msgNode = (MsgNode*)List_getFirst(&msgQueue->list);

    // signal modify
    msgQueue->modifiedFlag = TRUE;
  }

  // copy data, free message
  if (msgNode == NULL)
  {
    return FALSE;
  }
  n = MIN(msgNode->size,maxSize);
  memcpy(msg,msgNode->data,n);
  if (size != NULL) (*size) = n;
  free(msgNode);

  return TRUE;
}

bool MsgQueue_put(MsgQueue *msgQueue, const void *msg, ulong size)
{
  MsgQueueLock msgQueueLock;
  MsgNode      *msgNode;

  assert(msgQueue != NULL);

  // allocate message
  msgNode = (MsgNode*)malloc(sizeof(MsgNode)+size);
  if (msgNode == NULL)
  {
    return FALSE;
  }
  msgNode->size = size;
  memcpy(msgNode->data,msg,size);

  MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue)
  {
    // check if end of message
    if (msgQueue->endOfMsgFlag)
    {
      free(msgNode);
      unlock(msgQueue);
      return FALSE;
    }

    // check number of messages
    if (msgQueue->maxMsgs > 0)
    {
      while (!msgQueue->endOfMsgFlag && (List_count(&msgQueue->list) >= msgQueue->maxMsgs))
      {
        waitModified(msgQueue);
      }
      if (List_count(&msgQueue->list) >= msgQueue->maxMsgs)
      {
        free(msgNode);
        unlock(msgQueue);
        return FALSE;
      }
    }

    // put message
    List_append(&msgQueue->list,msgNode);

    // signal modify
    msgQueue->modifiedFlag = TRUE;
  }

  return TRUE;
}

ulong MsgQueue_count(MsgQueue *msgQueue)
{
  MsgQueueLock msgQueueLock;
  ulong        count;

  assert(msgQueue != NULL);

  MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue)
  {
    // get count
    count = List_count(&msgQueue->list);
  }

  return count;
}

void MsgQueue_wait(MsgQueue *msgQueue)
{
  MsgQueueLock msgQueueLock;

  assert(msgQueue != NULL);

  MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue)
  {
    if (!msgQueue->endOfMsgFlag)
    {
      waitModified(msgQueue);
    }
  }
}

void MsgQueue_setEndOfMsg(MsgQueue *msgQueue)
{
  MsgQueueLock msgQueueLock;

  assert(msgQueue != NULL);

  MSGQUEUE_LOCKED_DO(msgQueueLock,msgQueue)
  {
    msgQueue->endOfMsgFlag = TRUE;

    // signal modify
    msgQueue->modifiedFlag = TRUE;
  }
}

#ifdef __cplusplus
  }
#endif

/* end of file */
