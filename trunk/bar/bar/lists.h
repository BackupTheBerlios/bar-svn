/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: dynamic list functions
* Systems: all
*
\***********************************************************************/

#ifndef __LISTS__
#define __LISTS__

/****************************** Includes *******************************/
#include <stdlib.h>

#include "global.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

#define LIST_START NULL
#define LIST_END   NULL

/***************************** Datatypes *******************************/

#define LIST_NODE_HEADER(type) \
  type *prev; \
  type *next

#define LIST_HEADER(type) \
  type          *head; \
  type          *tail; \
  unsigned long count

// list node
typedef struct Node
{
  LIST_NODE_HEADER(struct Node);
} Node;

// list
typedef struct
{
  LIST_HEADER(Node);
} List;

// delete list node function
typedef void(*ListNodeFreeFunction)(void *node, void *userData);

/***********************************************************************\
* Name   : ListNodeCopyFunction
* Purpose: copy list node function
* Input  : fromNode - copy from node
*          userData - user data
* Output : -
* Return : duplicated list node
* Notes  : -
\***********************************************************************/

typedef void*(*ListNodeCopyFunction)(const void *fromNode, void *userData);

/***********************************************************************\
* Name   : ListNodeEqualsFunction
* Purpose: list node equals function
* Input  : node     - node to check
*          userData - user data
* Output : -
* Return : TRUE iff node equals
* Notes  : -
\***********************************************************************/

typedef bool(*ListNodeEqualsFunction)(const void *node, void *userData);

/***********************************************************************\
* Name   : ListNodeCompareFunction
* Purpose: compare list nodes function
* Input  : node1,node2 - nodes to compare
*          userData - user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

typedef int(*ListNodeCompareFunction)(const void *node1, const void *node2, void *userData);

/***************************** Variables *******************************/

/****************************** Macros *********************************/

#ifndef NDEBUG
  #define List_newNode(...) __List_newNode(__FILE__,__LINE__,__VA_ARGS__)
  #define List_deleteNode(...) __List_deleteNode(__FILE__,__LINE__,__VA_ARGS__)
  #define List_insert(...) __List_insert(__FILE__,__LINE__,__VA_ARGS__)
  #define List_append(...) __List_append(__FILE__,__LINE__,__VA_ARGS__)
  #define List_appendUniq(...) __List_appendUniq(__FILE__,__LINE__,__VA_ARGS__)
#endif /* not NDEBUG */

#define LIST_STATIC_INIT {NULL,NULL}

#define LIST_NEW_NODE(type) (type*)List_newNode(sizeof(type))
#define LIST_DELETE_NODE(node) List_deleteNode((Node*)node)
#ifndef NDEBUG
  #define LIST_NEW_NODEX(fileName,lineNb,type) (type*)__List_newNode(fileName,lineNb,sizeof(type))
  #define LIST_DELETE_NODEX(fileName,lineNb,node) __List_deleteNode(fileName,lineNb,(Node*)node)
#endif /* not NDEBUG */

#define LIST_DEFINE(type,define) \
  typedef struct { define; } type; \
  typedef struct type ## Node\
  { \
    LIST_NODE_HEADER(struct type ## Node); \
    define; \
  } type ## Node; \
  typedef struct \
  { \
    LIST_HEADER(type ## Node); \
  } type ## List

/***********************************************************************\
* Name   : LIST_DONE
* Purpose: iterated over list and execute block, delete node
* Input  : list     - list
*          variable - iterator variable
* Output : -
* Return : -
* Notes  : usage:
*            ListNode *variable;
*
*            LIST_DONE(list,variable)
*            {
*              ... = variable->...
*            }
\***********************************************************************/

#define LIST_DONE(list,variable) \
  for ((variable) = (list)->head; \
       (variable) != NULL; \
       (variable) = (typeof(variable))List_deleteNode((Node*)variable) \
      )

/***********************************************************************\
* Name   : LIST_ITERATE
* Purpose: iterated over list and execute block
* Input  : list     - list
*          variable - iteration variable
* Output : -
* Return : -
* Notes  : variable will contain all entries in list
*          usage:
*            LIST_ITERATE(list,variable)
*            {
*              ... = variable->...
*            }
\***********************************************************************/

#define LIST_ITERATE(list,variable) \
  for ((variable) = (list)->head; \
       (variable) != NULL; \
       (variable) = (variable)->next \
      )

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : List_newNode
* Purpose: allocate new list node
* Input  : size - size of node
* Output : -
* Return : node or NULL if insufficient memory
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
Node *List_newNode(ulong size);
#else /* not NDEBUG */
Node *__List_newNode(const char *__fileName__, ulong __lineNb__, ulong size);
#endif /* NDEBUG */

/***********************************************************************\
* Name   : List_deleteNode
* Purpose: delete list node
* Input  : node - list node
* Output : -
* Return : next node in list or NULL
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
Node *List_deleteNode(Node *node);
#else /* not NDEBUG */
Node *__List_deleteNode(const char *__fileName__, ulong __lineNb__, Node *node);
#endif /* NDEBUG */

/***********************************************************************\
* Name   : List_init
* Purpose: initialize list
* Input  : list - list to initialize
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_init(void *list);

/***********************************************************************\
* Name   : List_initDuplicate
* Purpose: initialize a duplicated list
* Input  : list                            - list to initialize
*          fromList                        - from list
*          fromListFromNode,fromListToNode - from/to node (could be
*                                            NULL)
*          listNodeCopyFunction            - node copy function
*          listNodeCopyUserData            - node copy user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_initDuplicate(void                 *list,
                        const void           *fromList,
                        const void           *fromListFromNode,
                        const void           *fromListToNode,
                        ListNodeCopyFunction listNodeCopyFunction,
                        void                 *listNodeCopyUserData
                       );

/***********************************************************************\
* Name   : List_done
* Purpose: free all nodes
* Input  : list                 - list to free
*          listNodeFreeFunction - free function for single node or NULL
*          listNodeFreeUserData - user data for free function
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_done(void                 *list,
               ListNodeFreeFunction listNodeFreeFunction,
               void                 *listNodeFreeUserData
              );

/***********************************************************************\
* Name   : List_new
* Purpose: allocate new list
* Input  : -
* Output : -
* Return : list or NULL on insufficient memory
* Notes  : -
\***********************************************************************/

List *List_new(void);
#if 0
#ifdef NDEBUG
void List_new(void *list,
              void *node,
              void *nextNode
             );
#else /* not NDEBUG */
void __List_new(const char *fileName,
                ulong      lineNb,
                void       *list,
                void       *node,
                void       *nextNode
               );
#endif /* NDEBUG */
#endif /* 0 */

/***********************************************************************\
* Name   : List_duplicate
* Purpose: duplicate list
* Input  : fromList                        - from list
*          fromListFromNode,fromListToNode - from/to node (could be
*                                            NULL)
*          listNodeCopyFunction            - node copy function
*          listNodeCopyUserData            - node copy user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

List *List_duplicate(const void           *fromList,
                     const void           *fromListFromNode,
                     const void           *fromListToNode,
                     ListNodeCopyFunction listNodeCopyFunction,
                     void                 *listNodeCopyUserData
                    );

/***********************************************************************\
* Name   : List_delete
* Purpose: free all nodes and delete list
* Input  : list                 - list to free
*          listNodeFreeFunction - free function for single node or NULL
*          listNodeFreeUserData - user data for free function
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_delete(void                 *list,
                 ListNodeFreeFunction listNodeFreeFunction,
                 void                 *listNodeFreeUserData
                );

/***********************************************************************\
* Name   : List_clear
* Purpose: free all nodes in list
* Input  : list                 - list
*          listNodeFreeFunction - free function for single node or NULL
*          listNodeFreeUserData - user data for free function
* Output : -
* Return : list
* Notes  : -
\***********************************************************************/

void *List_clear(void                 *list,
                 ListNodeFreeFunction listNodeFreeFunction,
                 void                 *listNodeFreeUserData
                );

/***********************************************************************\
* Name   : List_copy
* Purpose: copy contents of list
* Input  : fromList                        - from list
*          toList                          - to list
*          fromListFromNode,fromListToNode - from/to node (could be
*                                            NULL)
*          toListNextNode                  - insert node before nextNode
*                                            (could be NULL)
*          listNodeCopyFunction            - node copy function
*          listNodeCopyUserData            - node copy user data
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_copy(const void           *fromList,
               void                 *toList,
               const void           *fromListFromNode,
               const void           *fromListToNode,
               void                 *toListNextNode,
               ListNodeCopyFunction listNodeCopyFunction,
               void                 *listNodeCopyUserData
              );

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

void List_move(void       *fromList,
               void       *toList,
               const void *fromListFromNode,
               const void *fromListToNode,
               void       *toListNextNode
              );

/***********************************************************************\
* Name   : List_isEmpty
* Purpose: check if list is empty
* Input  : list - list
* Output : -
* Return : TRUE if list is empty, FALSE otherwise
* Notes  : -
\***********************************************************************/

INLINE bool List_isEmpty(const void *list);
#if defined(NDEBUG) || defined(__LISTS_IMPLEMENATION__)
INLINE bool List_isEmpty(const void *list)
{
  assert(list != NULL);
  assert(((((List*)list)->count == 0) && (((List*)list)->head == NULL) && (((List*)list)->tail == NULL)) ||
         ((((List*)list)->count > 0) && (((List*)list)->head != NULL) && (((List*)list)->tail != NULL))
        );

  return (((List*)list)->count == 0);
}
#endif /* NDEBUG || __LISTS_IMPLEMENATION__ */

/***********************************************************************\
* Name   : List_count
* Purpose: get number of elements in list
* Input  : list - list
* Output : -
* Return : number of elements
* Notes  : -
\***********************************************************************/

INLINE unsigned long List_count(const void *list);
#if defined(NDEBUG) || defined(__LISTS_IMPLEMENATION__)
INLINE unsigned long List_count(const void *list)
{
  assert(list != NULL);
  assert(((((List*)list)->count == 0) && (((List*)list)->head == NULL) && (((List*)list)->tail == NULL)) ||
         ((((List*)list)->count > 0) && (((List*)list)->head != NULL) && (((List*)list)->tail != NULL))
        );

  return ((List*)list)->count;
}
#endif /* NDEBUG || __LISTS_IMPLEMENATION__ */

/***********************************************************************\
* Name   : List_insert
* Purpose: insert node into list
* Input  : list     - list
*          node     - node to insert
*          nextNode - insert node before nextNode (could be NULL)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
void List_insert(void *list,
                 void *node,
                 void *nextNode
                );
#else /* not NDEBUG */
void __List_insert(const char *fileName,
                   ulong      lineNb,
                   void       *list,
                   void       *node,
                   void       *nextNode
                  );
#endif /* NDEBUG */

/***********************************************************************\
* Name   : List_append
* Purpose: append node to end of list
* Input  : list - list
*          node - node to add
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
void List_append(void *list,
                 void *node
                );
#else /* not NDEBUG */
void __List_append(const char *fileName,
                   ulong      lineNb,
                   void       *list,
                   void       *node
                  );
#endif /* NDEBUG */

/***********************************************************************\
* Name   : List_appendUniq
* Purpose: append node to end of list if not already in list
* Input  : list - list
*          node - node to add
*
* Output : -
* Return : TRUE iff node added
* Notes  : -
\***********************************************************************/

#ifdef NDEBUG
bool List_appendUniq(void                   *list,
                     void                   *node,
                     ListNodeEqualsFunction listNodeEqualsFunction,
                     void                   *listNodeEqualsUserData
                    );
#else /* not NDEBUG */
bool __List_appendUniq(const char             *fileName,
                       ulong                  lineNb,
                       void                   *list,
                       void                   *node,
                       ListNodeEqualsFunction listNodeEqualsFunction,
                       void                   *listNodeEqualsUserData
                      );
#endif /* NDEBUG */


/***********************************************************************\
* Name   : List_remove
* Purpose: remove node from list
* Input  : list - list
*          node - node to remove
* Output : -
* Return : next node in list or NULL
* Notes  : -
\***********************************************************************/

void *List_remove(void *list,
                  void *node
                 );

/***********************************************************************\
* Name   : List_removeAndFree
* Purpose: remove node from list and free
* Input  : list                 - list
*          node                 - node to remove
*          listNodeFreeFunction - free function for single node or NULL
*          listNodeFreeUserData - user data for free function
* Output : -
* Return : next node in list or NULL
* Notes  : -
\***********************************************************************/

void *List_removeAndFree(void                 *list,
                         void                 *node,
                         ListNodeFreeFunction listNodeFreeFunction,
                         void                 *listNodeFreeUserData
                        );

/***********************************************************************\
* Name   : List_first
* Purpose: first node from list
* Input  : list - list
* Output : -
* Return : node or NULL if list is empty
* Notes  : -
\***********************************************************************/

INLINE Node *List_first(const void *list);
#if defined(NDEBUG) || defined(__LISTS_IMPLEMENATION__)
INLINE Node *List_first(const void *list)
{
  assert(list != NULL);
  assert(((((List*)list)->count == 0) && (((List*)list)->head == NULL) && (((List*)list)->tail == NULL)) ||
         ((((List*)list)->count > 0) && (((List*)list)->head != NULL) && (((List*)list)->tail != NULL))
        );

  return ((List*)list)->head;
}
#endif /* NDEBUG || __LISTS_IMPLEMENATION__ */

/***********************************************************************\
* Name   : List_last
* Purpose: last node from list
* Input  : list - list
* Output : -
* Return : node or NULL if list is empty
* Notes  : -
\***********************************************************************/

INLINE Node *List_last(const void *list);
#if defined(NDEBUG) || defined(__LISTS_IMPLEMENATION__)
INLINE Node *List_last(const void *list)
{
  assert(list != NULL);
  assert(((((List*)list)->count == 0) && (((List*)list)->head == NULL) && (((List*)list)->tail == NULL)) ||
         ((((List*)list)->count > 0) && (((List*)list)->head != NULL) && (((List*)list)->tail != NULL))
        );

  return ((List*)list)->tail;
}
#endif /* NDEBUG || __LISTS_IMPLEMENATION__ */

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
* Name   : List_contains
* Purpose: check if list contain node
* Input  : list                   - list
*          node                   - node
*          listNodeEqualsFunction - equals function or NULL
*          listNodeEqualsUserData - user data for equals function
* Output : -
* Return : TRUE if list contain node, FALSE otherwise
* Notes  : -
\***********************************************************************/

bool List_contains(const void             *list,
                   const void             *node,
                   ListNodeEqualsFunction listNodeEqualsFunction,
                   void                   *listNodeEqualsUserData
                  );

/***********************************************************************\
* Name   : List_findFirst
* Purpose: find node in list
* Input  : list                   - list
*          listNodeEqualsFunction - equals function
*          listNodeEqualsUserData - user data for equals function
* Output : -
* Return : node or NULL if not found
* Notes  : -
\***********************************************************************/

const Node *List_findFirst(const void             *list,
                           ListNodeEqualsFunction listNodeEqualsFunction,
                           void                   *listNodeEqualsUserData
                          );

/***********************************************************************\
* Name   : List_findNext
* Purpose: find next node in list
* Input  : list                    - list
*          node                    - previous found node
*          listNodeEqualsFunction - equals function
*          listNodeEqualsUserData - user data for equals function
* Output : -
* Return : next node or NULL if no next node found
* Notes  : -
\***********************************************************************/

const Node *List_findNext(const void             *list,
                          const void             *node,
                          ListNodeEqualsFunction listNodeEqualsFunction,
                          void                   *listNodeEqualsUserData
                         );

/***********************************************************************\
* Name   : List_sort
* Purpose: sort list
* Input  : list                - list
*          listNodeCompareFunction - compare function
*          listNodeCmpUserData - user data for compare function
* Output : -
* Return : -
* Notes  : use temporary O(n) memory
\***********************************************************************/

void List_sort(void                    *list,
               ListNodeCompareFunction listNodeCompareFunction,
               void                    *listNodeCompareUserData
              );

#ifndef NDEBUG
/***********************************************************************\
* Name   : List_debugInit
* Purpose: init list debug functions
* Input  : -
* Output : -
* Return : -
* Notes  : called automatically
\***********************************************************************/

void List_debugInit(void);

/***********************************************************************\
* Name   : List_debugDone
* Purpose: done list debug functions
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_debugDone(void);

/***********************************************************************\
* Name   : List_debugDumpInfo, List_debugPrintInfo
* Purpose: list debug function: output allocated list nodes
* Input  : handle - output channel
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_debugDumpInfo(FILE *handle);
void List_debugPrintInfo(void);

/***********************************************************************\
* Name   : List_debugPrintStatistics
* Purpose: list debug function: output list statistics
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_debugPrintStatistics(void);

/***********************************************************************\
* Name   : List_debugCheck
* Purpose: list debug function: output allocated list nodes and
*          statistics, check lost resources
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void List_debugCheck(void);
#endif /* not NDEBUG */

#ifdef __cplusplus
  }
#endif

#endif /* __LISTS__ */

/* end of file */
