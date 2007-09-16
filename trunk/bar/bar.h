/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar.h,v $
* $Revision: 1.9 $
* $Author: torsten $
* Contents: Backup ARchiver main program
* Systems :
*
\***********************************************************************/

#ifndef __BAR__
#define __BAR__

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "global.h"
#include "lists.h"
#include "strings.h"

#include "patterns.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

typedef enum
{
  EXITCODE_OK=0,
  EXITCODE_FAIL=1,

  EXITCODE_INVALID_ARGUMENT=5,

  EXITCODE_INIT_FAIL=125,
  EXITCODE_FATAL_ERROR=126,

  EXITCODE_UNKNOWN=128
} ExitCodes;

typedef struct
{
  const char    *tmpDirectory;
  bool          overwriteFlag;
  bool          quietFlag;
  uint          verboseLevel;
} GlobalOptions;

/***************************** Variables *******************************/

extern GlobalOptions globalOptions;

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : info
* Purpose: output info
* Input  : verboseLevel - verbosity level
*          format       - format string (like printf)
*          ...          - optional arguments (like printf)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void info(uint verboseLevel, const char *format, ...);

/***********************************************************************\
* Name   : getErrorText
* Purpose: get errror text of error code
* Input  : error - error
* Output : -
* Return : error text (read only!)
* Notes  : -
\***********************************************************************/

const char *getErrorText(Errors error);

/***********************************************************************\
* Name   : printError
* Purpose: print error message
*          text - format string (like printf)
*          ...  - optional arguments (like printf)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void printError(const char *text, ...);

#ifdef __cplusplus
  }
#endif

#endif /* __BAR__ */

/* end of file */
