/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/misc.h,v $
* $Revision: 1.4 $
* $Author: torsten $
* Contents: miscellaneous functions
* Systems: all
*
\***********************************************************************/

#ifndef __MISC__
#define __MISC__

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "global.h"
#include "strings.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/
typedef enum
{
  EXECUTE_MACRO_TYPE_INT,
  EXECUTE_MACRO_TYPE_INT64,
  EXECUTE_MACRO_TYPE_STRING,
} ExecuteMacroTypes;

typedef struct
{
  ExecuteMacroTypes type;
  const char        *name;
  int               i;
  int64             l;
  String            string;
} ExecuteMacro;

typedef void(*ExecuteIOFunction)(void         *userData,
                                 const String line
                                );

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : getTimestamp
* Purpose: get timestamp
* Input  : -
* Output : -
* Return : timestamp [us]
* Notes  : -
\***********************************************************************/

uint64 Misc_getTimestamp(void);

/***********************************************************************\
* Name   : Misc_getDateTime
* Purpose: get current date/time
* Input  : buffer     - buffer for date/time stirng
*          bufferSize - buffer size
* Output : -
* Return : date/time string
* Notes  : -
\***********************************************************************/

const char *Misc_getDateTime(char *buffer, uint bufferSize);

/***********************************************************************\
* Name   : udelay
* Purpose: delay program execution
* Input  : time - delay time [us]
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Misc_udelay(uint64 time);

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : Misc_executeCommand
* Purpose: execute external command
* Input  : commandTemplate - command template string
*          macros          - macros array
*          macroCount      - number of macros in array
* Output : -
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

Errors Misc_executeCommand(const char         *commandTemplate,
                           const ExecuteMacro macros[],
                           uint               macroCount,
                           ExecuteIOFunction  stdoutExecuteIOFunction,
                           ExecuteIOFunction  stderrExecuteIOFunction,
                           void               *executeIOUserData
                          );

/*---------------------------------------------------------------------*/

/***********************************************************************\
* Name   : Misc_waitEnter
* Purpose: wait until user press ENTER
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void Misc_waitEnter(void);

#ifdef __cplusplus
  }
#endif

#endif /* __MISC__ */

/* end of file */
