/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/commands_list.h,v $
* $Revision: 1.2 $
* $Author: torsten $
* Contents: Backup ARchiver archive functions: list
* Systems : all
*
\***********************************************************************/

#ifndef __COMMAND_LIST__
#define __COMMAND_LIST__

/****************************** Includes *******************************/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "bar.h"
#include "patterns.h"
#include "crypt.h"

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
* Name   : command_list
* Purpose: 
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

bool command_list(FileNameList *fileNameList,
                  PatternList  *includePatternList,
                  PatternList  *excludePatternList,
                  const char   *password
                 );

#ifdef __cplusplus
  }
#endif

#endif /* __COMMAND_LIST__ */

/* end of file */
