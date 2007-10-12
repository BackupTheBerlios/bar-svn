/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar.h,v $
* $Revision: 1.17 $
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
#include "compress.h"
#include "crypt.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

typedef enum
{
  EXITCODE_OK=0,
  EXITCODE_FAIL=1,

  EXITCODE_INVALID_ARGUMENT=5,
  EXITCODE_CONFIG_ERROR,

  EXITCODE_INIT_FAIL=125,
  EXITCODE_FATAL_ERROR=126,

  EXITCODE_UNKNOWN=128
} ExitCodes;

typedef struct
{
  ulong              archivePartSize;
  const char         *tmpDirectory;
  uint64             maxTmpSize;
  uint               directoryStripCount;
  const char         *directory;

  PatternTypes       patternType;

  CompressAlgorithms compressAlgorithm;
  CryptAlgorithms    cryptAlgorithm;
  ulong              compressMinFileSize;
  const char         *cryptPassword;

  uint               sshPort;
  const char         *sshPublicKeyFileName;
  const char         *sshPrivatKeyFileName;
  const char         *sshPassword;

  bool               skipUnreadableFlag;
  bool               overwriteArchiveFilesFlag;
  bool               overwriteFilesFlag;
  bool               noDefaultConfigFlag;
  bool               quietFlag;
  long               verboseLevel;
} Options;

/***************************** Variables *******************************/
extern Options defaultOptions;

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

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
* Name   : warning
* Purpose: output warning
* Input  : format - format string (like printf)
*          ...    - optional arguments (like printf)
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

void warning(const char *format, ...);

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
