/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar/stringlists.c,v $
* $Revision: 1.6 $
* $Author: torsten $
* Contents:
* Systems :
*
\***********************************************************************/

#define __STRINGLISTS_IMPLEMENATION__

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <regex.h>
#include <assert.h>

#include "lists.h"
#include "strings.h"

#include "stringlists.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : insertString
* Purpose: insert string in string list
* Input  : stringList - string list
*          string     - string to insert
*          nextNode   - next string list node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void insertString(StringList *stringList, const String string, StringNode *nextStringNode)
{
  StringNode *stringNode;

  assert(stringList != NULL);

  stringNode = LIST_NEW_NODE(StringNode);
  if (stringNode == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  stringNode->string = string;
  List_insert(stringList,stringNode,nextStringNode);
}

/***********************************************************************\
* Name   : freeStringNode
* Purpose: free allocated file name node
* Input  : fileNameNode - file name node
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void freeStringNode(StringNode *stringNode, void *userData)
{
  assert(stringNode != NULL);

  UNUSED_VARIABLE(userData);

  String_delete(stringNode->string);
}

/*---------------------------------------------------------------------*/

void StringList_init(StringList *stringList)
{
  assert(stringList != NULL);

  List_init(stringList);
}

void StringList_done(StringList *stringList)
{
  assert(stringList != NULL);

  List_done(stringList,(ListNodeFreeFunction)freeStringNode,NULL);
}

StringList *StringList_new(void)
{
  return (StringList*)List_new();
}

StringList *StringList_duplicate(const StringList *stringList)
{
  StringList *newStringList;

  assert(stringList != NULL);

  newStringList = StringList_new();
  if (newStringList == NULL)
  {
    return NULL;
  }

  StringList_copy(newStringList,stringList);

  return newStringList;
}

void StringList_copy(StringList *stringList, const StringList *fromStringList)
{
  StringNode *stringNode;

  assert(stringList != NULL);
  assert(fromStringList != NULL);

  stringNode = fromStringList->head;
  while (stringNode != NULL)
  {
    StringList_append(stringList,stringNode->string);
    stringNode = stringNode->next;
  }
}

void StringList_delete(StringList *stringList)
{
  assert(stringList != NULL);

  List_delete(stringList,(ListNodeFreeFunction)freeStringNode,NULL);
}

void StringList_clear(StringList *stringList)
{
  assert(stringList != NULL);

  List_clear(stringList,(ListNodeFreeFunction)freeStringNode,NULL);
}

void StringList_move(StringList *fromStringList, StringList *toStringList)
{
  assert(fromStringList != NULL);
  assert(toStringList != NULL);

  List_move(fromStringList,toStringList,NULL,NULL,NULL);
}

#ifdef NDEBUG
void StringList_insert(StringList *stringList, const String string, StringNode *nextStringNode)
#else /* not NDEBUG */
void __StringList_insert(const char *fileName, ulong lineNb, StringList *stringList, const String string, StringNode *nextStringNode)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_duplicate(string),nextStringNode);
  #else /* not NDEBUG */
    insertString(stringList,__String_duplicate(fileName,lineNb,string),nextStringNode);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void StringList_insertCString(StringList *stringList, const char *s, StringNode *nextStringNode)
#else /* not NDEBUG */
void __StringList_insertCString(const char *fileName, ulong lineNb, StringList *stringList, const char *s, StringNode *nextStringNode)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_newCString(s),nextStringNode);
  #else /* not NDEBUG */
    insertString(stringList,__String_newCString(fileName,lineNb,s),nextStringNode);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void StringList_insertChar(StringList *stringList, char ch, StringNode *nextStringNode)
#else /* not NDEBUG */
void __StringList_insertChar(const char *fileName, ulong lineNb, StringList *stringList, char ch, StringNode *nextStringNode)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_newChar(ch),nextStringNode);
  #else /* not NDEBUG */
    insertString(stringList,__String_newChar(fileName,lineNb,ch),nextStringNode);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void StringList_insertBuffer(StringList *stringList, char *buffer, ulong bufferLength, StringNode *nextStringNode)
#else /* not NDEBUG */
void __StringList_insertBuffer(const char *fileName, ulong lineNb, StringList *stringList, char *buffer, ulong bufferLength, StringNode *nextStringNode)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_newBuffer(buffer,bufferLength),nextStringNode);
  #else /* not NDEBUG */
    insertString(stringList,__String_newBuffer(fileName,lineNb,buffer,bufferLength),nextStringNode);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void StringList_append(StringList *stringList, const String string)
#else /* not NDEBUG */
void __StringList_append(const char *fileName, ulong lineNb, StringList *stringList, const String string)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_duplicate(string),NULL);
  #else /* not NDEBUG */
    insertString(stringList,__String_duplicate(fileName,lineNb,string),NULL);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void StringList_appendCString(StringList *stringList, const char *s)
#else /* not NDEBUG */
void __StringList_appendCString(const char *fileName, ulong lineNb, StringList *stringList, const char *s)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_newCString(s),NULL);
  #else /* not NDEBUG */
    insertString(stringList,__String_newCString(fileName,lineNb,s),NULL);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void StringList_appendChar(StringList *stringList, char ch)
#else /* not NDEBUG */
void __StringList_appendChar(const char *fileName, ulong lineNb, StringList *stringList, char ch)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_newChar(ch),NULL);
  #else /* not NDEBUG */
    insertString(stringList,__String_newChar(fileName,lineNb,ch),NULL);
  #endif /* NDEBUG */
}

#ifdef NDEBUG
void StringList_appendBuffer(StringList *stringList, char *buffer, ulong bufferLength)
#else /* not NDEBUG */
void __StringList_appendBuffer(const char *fileName, ulong lineNb, StringList *stringList, char *buffer, ulong bufferLength)
#endif /* NDEBUG */
{
  #ifdef NDEBUG
    insertString(stringList,String_newBuffer(buffer,bufferLength),NULL);
  #else /* not NDEBUG */
    insertString(stringList,__String_newBuffer(fileName,lineNb,buffer,bufferLength),NULL);
  #endif /* NDEBUG */
}

StringNode *StringList_remove(StringList *stringList, StringNode *stringNode)
{
  assert(stringList != NULL);
  assert(stringNode != NULL);

  return (StringNode*)List_remove(stringList,stringNode);
}

String StringList_getFirst(StringList *stringList, String string)
{
  StringNode *stringNode;

  assert(stringList != NULL);

  stringNode = (StringNode*)List_getFirst(stringList);
  if (stringNode != NULL)
  {
    if (string != NULL)
    {
      String_set(string,stringNode->string);
      String_delete(stringNode->string);
    }
    else
    {
      string = stringNode->string;
    }
    free(stringNode);

    return string;
  }
  else
  {
    if (string != NULL)
    {
      String_clear(string);
    }

    return NULL;
  }
}

String StringList_getLast(StringList *stringList, String string)
{
  StringNode *stringNode;

  assert(stringList != NULL);

  stringNode = (StringNode*)List_getLast(stringList);
  if (stringNode != NULL)
  {
    if (string != NULL)
    {
      String_set(string,stringNode->string);
      String_delete(stringNode->string);
    }
    else
    {
      string = stringNode->string;
    }
    free(stringNode);

    return string;
  }
  else
  {
    if (string != NULL)
    {
      String_clear(string);
    }

    return NULL;
  }
}

StringNode *StringList_find(StringList *stringList, const String string)
{
  assert(stringList != NULL);

  return StringList_findCString(stringList,String_cString(string));
}

StringNode *StringList_findCString(StringList *stringList, const char *s)
{
  StringNode *stringNode;

  assert(stringList != NULL);

  stringNode = stringList->head;
  while (   (stringNode != NULL)
         && !String_equalsCString(stringNode->string,s)
        )
  {
    stringNode = stringNode->next;
  }

  return stringNode;
}

bool StringList_contain(StringList *stringList, const String string)
{
  assert(stringList != NULL);

  return (StringList_find(stringList,string) != NULL);
}

bool StringList_containCString(StringList *stringList, const char *s)
{
  assert(stringList != NULL);

  return (StringList_findCString(stringList,s) != NULL);
}

StringNode *StringList_match(StringList *stringList, const String pattern)
{
  return StringList_matchCString(stringList,String_cString(pattern));
}

StringNode *StringList_matchCString(StringList *stringList, const char *pattern)
{
  regex_t    regex;
  StringNode *stringNode;

  assert(stringList != NULL);
  assert(pattern != NULL);

  /* compile pattern */
  if (regcomp(&regex,pattern,REG_ICASE|REG_EXTENDED) != 0)
  {
    return NULL;
  }

  /* search in list */
  stringNode = stringList->head;
  while (   (stringNode != NULL)
         && (regexec(&regex,String_cString(stringNode->string),0,NULL,0) != 0)
        )
  {
    stringNode = stringNode->next;
  }

  /* free resources */
  regfree(&regex);

  return stringNode;
}

const char* const *StringList_toCStringArray(const StringList *stringList)
{
  char const **cStringArray;
  StringNode *stringNode;
  uint       z;

  assert(stringList != NULL);

  cStringArray = (const char**)malloc(stringList->count*sizeof(char*));
  if (cStringArray != NULL)
  {
    stringNode = stringList->head;
    z = 0;
    while (stringNode != NULL)
    {
      cStringArray[z] = String_cString(stringNode->string);
      stringNode = stringNode->next;
      z++;
    }
  }

  return cStringArray;
}

#ifndef NDEBUG
void StringList_debugDumpInfo(FILE *handle, const StringList *stringList)
{
  StringNode *stringNode;
  uint       z;

  assert(stringList != NULL);

  stringNode = stringList->head;
  z = 1;
  while (stringNode != NULL)
  {
    fprintf(handle,"DEBUG %d: %s\n",z,String_cString(stringNode->string));
    stringNode = stringNode->next;
    z++;
  }
}

void StringList_debugPrintInfo(const StringList *stringList)
{
  StringList_debugDumpInfo(stderr,stringList);
}
#endif /* not NDEBUG */

#ifdef __cplusplus
  }
#endif

/* end of file */
