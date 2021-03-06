/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/lists.h,v $
* $Revision: 1.1.1.1 $
* $Author: torsten $
* Contents: dynamic list functions
* Systems : all
*
\***********************************************************************/

#ifndef __LISTS__
#define __LISTS__

/****************************** Includes *******************************/
#include <stdlib.h>

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

#define LIST_START NULL
#define LIST_END   NULL

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

#define NODE_HEADER(type) \
  type *prev; \
  type *next

#define LIST_HEADER(type) \
  type          *head; \
  type          *tail; \
  unsigned long count

typedef struct Node
{
  NODE_HEADER(struct Node);
} Node;

typedef struct
{
  LIST_HEADER(Node);
} List;

typedef void(*NodeFreeFunction)(void *Node, void *userData);
typedef int(*NodeCompareFunction)(void *Node, void *userData);

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : List_init
* Purpose: initialise list
* Input  : -
* Output : -
* Return : list or NULL on insufficient memory
* Notes  : -
\***********************************************************************/

void List_init(void *list);

/***********************************************************************\
* Name   : List_done
* Purpose: deinitialize list
* Input  : list             - list to free
*          nodeFreeFunction - free function for single node or NULL
*          userData         - user data for free function
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_done(void *list, NodeFreeFunction nodeFreeFunction, void *userData);

/***********************************************************************\
* Name   : List_new
* Purpose: allocate new list
* Input  : -
* Output : -
* Return : list or NULL on insufficient memory
* Notes  : -
\***********************************************************************/

List *List_new(void);

/***********************************************************************\
* Name   : List_delete
* Purpose: free all node and delete list
* Input  : list             - list to free
*          nodeFreeFunction - free function for single node or NULL
*          userData         - user data for free function
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_delete(void *list, NodeFreeFunction nodeFreeFunction, void *userData);

/***********************************************************************\
* Name   : List_move
* Purpose: move contents of list
* Input  : fromList                        - from list
*          toList                          - to list
*          fromListFromNode,fromListToNode - from/to node (could be
*                                            NULL)
*          toListNextNode                  - insert node before nextNode
*                                            (could be NULL)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_move(void *fromList, void *toList, void *fromListFromNode, void *fromListToNode, void *toListNextNode);

/***********************************************************************\
* Name   : List_empty
* Purpose: check if list is empty
* Input  : list - list
* Output : -
* Return : TRUE if list is empty, FALSE otherwise
* Notes  : -
\***********************************************************************/

unsigned long List_empty(void *list);

/***********************************************************************\
* Name   : List_count
* Purpose: get number of elements in list
* Input  : list - list to free
* Output : -
* Return : number of elements
* Notes  : -
\***********************************************************************/

unsigned long List_count(void *list);

/***********************************************************************\
* Name   : List_ins
* Purpose: insert node into list
* Input  : list     - list
*          node     - node to insert
*          nextNode - insert node before nextNode (could be NULL)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_ins(void *list, void *node, void *nextNode);

/***********************************************************************\
* Name   : List_add
* Purpose: add node to end of list
* Input  : list - list
*          node - node to add
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_add(void *list, void *node);

/***********************************************************************\
* Name   : List_rem
* Purpose: remove node from list
* Input  : list - list
*          node - node to remove
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_rem(void *list, void *node);

/***********************************************************************\
* Name   : List_getFirst
* Purpose: remove first node from list
* Input  : list - list
* Output : -
* Return : removed node or NULL if list is empty
* Notes  : -
\***********************************************************************/

Node *List_getFirst(void *list);

/***********************************************************************\
* Name   : List_getLast
* Purpose: remove last node from list
* Input  : list - list
* Output : -
* Return : removed node or NULL if list is empty
* Notes  : -
\***********************************************************************/

Node *List_getLast(void *list);

/***********************************************************************\
* Name   : List_findFirst
* Purpose: find node in list
* Input  : list                - list
*          nodeCompareFunction - compare function
*          userData            - user data for compare function
* Output : -
* Return : node or NULL if not found
* Notes  : -
\***********************************************************************/

Node *List_findFirst(void *list, NodeCompareFunction nodeCompareFunction, void *userData);

/***********************************************************************\
* Name   : List_findNext
* Purpose: find next node in list
* Input  : list                - list
*          node                - previous found node
*          nodeCompareFunction - compare function
*          userData            - user data for compare function
* Output : -
* Return : next node or NULL if no next node found
* Notes  : -
\***********************************************************************/

Node *List_findNext(void *list, void *node, NodeCompareFunction nodeCompareFunction, void *userData);

#ifdef __cplusplus
  }
#endif

#endif /* __LISTS__ */

/* end of file */
