/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/arrays.c,v $
* $Revision: 1.1 $
* $Author: torsten $
* Contents: dynamic array functions
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "global.h"
#ifndef NDEBUG
  #include "lists.h"
#endif /* not NDEBUG */

#include "arrays.h"

/****************** Conditional compilation switches *******************/
#define HALT_ON_INSUFFICIENT_MEMORY

/***************************** Constants *******************************/

#define DEFAULT_LENGTH 8
#define DELTA_LENGTH 8

/***************************** Datatypes *******************************/

struct __Array
{
  ulong elementSize;
  ulong length;
  ulong maxLength;
  byte  *data;
};

#ifndef NDEBUG
  typedef struct DebugArrayNode
  {
    NODE_HEADER(struct DebugArrayNode);

    const char     *fileName;
    ulong          lineNb;
    struct __Array *array;
  } DebugArrayNode;

  typedef struct
  {
    LIST_HEADER(DebugArrayNode);
  } DebugArrayList;
#endif /* not NDEBUG */

/***************************** Variables *******************************/
#ifndef NDEBUG
  DebugArrayList debugArrayList = LIST_STATIC_INIT;
#endif /* not NDEBUG */

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

#ifdef NDEBUG
void *Array_new(ulong elementSize, ulong length)
#else /* not NDEBUG */
void *__Array_new(const char *fileName, ulong lineNb, ulong elementSize, ulong length)
#endif /* NDEBUG */
{
  struct __Array *array;
  #ifndef NDEBUG
    DebugArrayNode *debugArrayNode;;
  #endif /* not NDEBUG */

  assert(elementSize > 0);

  array = (struct __Array*)malloc(sizeof(struct __Array));
  if (array == NULL)
  {
    #ifdef HALT_ON_INSUFFICIENT_MEMORY
      HALT_INSUFFICIENT_MEMORY();
    #else /* not HALT_ON_INSUFFICIENT_MEMORY */
      return NULL;
    #endif /* HALT_ON_INSUFFICIENT_MEMORY */
  }
  if (length == 0) length = DEFAULT_LENGTH;
  array->data = (byte*)malloc(length*elementSize);
  if (array->data == NULL)
  {
    free(array);
    #ifdef HALT_ON_INSUFFICIENT_MEMORY
      HALT_INSUFFICIENT_MEMORY();
    #else /* not HALT_ON_INSUFFICIENT_MEMORY */
      return NULL;
    #endif /* HALT_ON_INSUFFICIENT_MEMORY */
  }

  array->elementSize = elementSize;
  array->length      = 0;
  array->maxLength   = length;

  #ifndef NDEBUG
    debugArrayNode = LIST_NEW_NODE(DebugArrayNode);
    if (debugArrayNode == NULL)
    {
      HALT_INSUFFICIENT_MEMORY();
    }
    debugArrayNode->fileName = fileName;
    debugArrayNode->lineNb   = lineNb;
    debugArrayNode->array    = array;
    List_append(&debugArrayList,debugArrayNode);
  #endif /* not NDEBUG */

  return array;
}

void Array_delete(Array array, ArrayElementFreeFunction arrayElementFreeFunction, void *arrayElementFreeUserData)
{
  ulong z;

  #ifndef NDEBUG
    DebugArrayNode *debugArrayNode;;
  #endif /* not NDEBUG */

  if (array != NULL)
  {
    assert(array->data != NULL);

    if (arrayElementFreeFunction != NULL)
    {
      for (z = 0; z < array->length; z++)
      {
        arrayElementFreeFunction(array->data+z*array->elementSize,arrayElementFreeUserData);
      }
    }

    #ifndef NDEBUG
      debugArrayNode = debugArrayList.head;
      while ((debugArrayNode != NULL) && (debugArrayNode->array != array))
      {
        debugArrayNode = debugArrayNode->next;
      }
      if (debugArrayNode != NULL)
      {
        List_remove(&debugArrayList,debugArrayNode);
      }
      else
      {
        fprintf(stderr,"DEBUG WARNING: array %p not found in debug list!\n",
                array
               );
      }
    #endif /* not NDEBUG */

    free(array->data);
    free(array);
  }
}

void Array_clear(Array array)
{
  if (array != NULL)
  {
    assert(array->data != NULL);

    array->length = 0;
  }
}

ulong Array_length(Array array)
{
  return (array != NULL)?array->length:0;
}

bool Array_put(Array array, ulong index, const void *data)
{
  void *newData;

  if (array != NULL)
  {
    assert(array->data != NULL);

    /* extend array if needed */
    if (index >= array->maxLength)
    {
      newData = realloc(array->data,(index+1)*array->elementSize);
      if (newData == NULL)
      {
        return FALSE;
      }
      array->maxLength = index+1;
      array->data      = newData;
    }

    /* store element */
    memcpy(array->data+index*array->elementSize,data,array->elementSize);
    if (index > array->length) array->length = index+1;

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

void Array_get(Array array, ulong index, void *data)
{
  if (array != NULL)
  {
    assert(array->data != NULL);

    /* get element */
    if (index < array->length)
    {
      memcpy(data,array->data+index*array->elementSize,array->elementSize);
    }
  }
}

bool Array_insert(Array array, long nextIndex, const void *data)
{
  byte *newData;

  if (array != NULL)
  {
    assert(array->data != NULL);

    if      (nextIndex == ARRAY_END)
    {
    }
    else if (nextIndex+1 >= array->maxLength)
    {
      newData = realloc(array->data,(nextIndex+1)*array->elementSize);
      if (newData == NULL)
      {
        return FALSE;
      }
      array->maxLength = nextIndex+1;
      array->data      = newData;
    }

    /* insert element */
    if (nextIndex < array->length)
    {
      memmove(array->data+(nextIndex+1)*array->elementSize,
              array->data+nextIndex*array->elementSize,
              array->elementSize*(array->length-nextIndex)
             );
    }
    memcpy(array->data+nextIndex*array->elementSize,data,array->elementSize);
    if (nextIndex > array->length) array->length = nextIndex+1;

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

bool Array_append(Array array, const void *data)
{
  byte *newData;

  if (array != NULL)
  {
    assert(array->data != NULL);

    /* extend array if needed */
    if (array->length >= array->maxLength)
    {
      newData = realloc(array->data,(array->maxLength+DELTA_LENGTH)*array->elementSize);
      if (newData == NULL)
      {
        return FALSE;
      }
      array->maxLength = array->maxLength+DELTA_LENGTH;
      array->data      = newData;
    }

    /* store element */
    memcpy(array->data+array->length*array->elementSize,data,array->elementSize);
    array->length++;

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

void Array_remove(Array array, ulong index, ArrayElementFreeFunction arrayElementFreeFunction, void *arrayElementFreeUserData)
{
  if (array != NULL)
  {
    assert(array->data != NULL);

    /* remove element */
    if (index < array->length)
    {
      if (arrayElementFreeFunction != NULL)
      {
        arrayElementFreeFunction(array->data+index*array->elementSize,arrayElementFreeUserData);
      }

      if (index < array->length-1)
      {
        memmove(array->data+index*array->elementSize,
                array->data+(index+1)*array->elementSize,
                array->elementSize*(array->length-index)
               );
      }
      array->length--;
    }
    else
    {
    }
  }
}

void *Array_cArray(Array array)
{
  return (array != NULL)?array->data:0;
}

#ifndef NDEBUG
void Array_debug(void)
{
  #ifndef NDEBUG
    DebugArrayNode *debugArrayNode;
  #endif /* not NDEBUG */

  #ifndef NDEBUG
    for (debugArrayNode = debugArrayList.head; debugArrayNode != NULL; debugArrayNode = debugArrayNode->next)
    {
      fprintf(stderr,"DEBUG WARNING: array %p[%ld] allocated at %s, %ld!\n",
              debugArrayNode->array->data,
              debugArrayNode->array->maxLength,
              debugArrayNode->fileName,
              debugArrayNode->lineNb
             );
    }
  #endif /* not NDEBUG */
}
#endif /* not NDEBUG */

#ifdef __cplusplus
  }
#endif

/* end of file */
