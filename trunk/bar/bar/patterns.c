/***********************************************************************\
*
* $Revision$
* $Date$
* $Author$
* Contents: Backup ARchiver pattern functions
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include <config.h>  // use <...> to support separated build directory

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#if defined(HAVE_PCRE)
  #include <pcreposix.h>
#elif defined(HAVE_REGEX_H)
  #include <regex.h>
#else
  #error No regular expression library available!
#endif /* HAVE_PCRE || HAVE_REGEX_H */
#include <assert.h>

#include "global.h"
#include "strings.h"

#include "errors.h"

#include "patterns.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/
LOCAL const struct
{
  const char   *name;
  PatternTypes patternType;
} PATTERN_TYPES[] =
{
  { "glob",           PATTERN_TYPE_GLOB           },
  { "regex",          PATTERN_TYPE_REGEX          },
  { "extended_regex", PATTERN_TYPE_EXTENDED_REGEX }
};

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : compilePattern
* Purpose: compile pattern
* Input  : pattern      - pattern to compile
*          patternType  - pattern type
*          patternFlags - pattern flags
* Output : regexBegin - regular expression for matching begin
*          regexEnd   - regular expression for matching end
*          regexExact - regular expression for exact matching
*          regexAny   - regular expression for matching anywhere
* Return : ERROR_NONE or error code
* Notes  : -
\***********************************************************************/

LOCAL Errors compilePattern(const char   *pattern,
                            PatternTypes patternType,
                            uint         patternFlags,
                            regex_t      *regexBegin,
                            regex_t      *regexEnd,
                            regex_t      *regexExact,
                            regex_t      *regexAny
                           )
{
  String matchString;
  String regexString;
  int    regexFlags;
  ulong  z;
  int    error;
  char   buffer[256];

  assert(pattern != NULL);
  assert(regexBegin != NULL);
  assert(regexEnd != NULL);
  assert(regexExact != NULL);
  assert(regexAny != NULL);

  matchString = String_new();
  regexString = String_new();

  regexFlags = REG_NOSUB;
  if ((patternFlags & PATTERN_FLAG_IGNORE_CASE) == PATTERN_FLAG_IGNORE_CASE) regexFlags |= REG_ICASE;
  switch (patternType)
  {
    case PATTERN_TYPE_GLOB:
      z = 0;
      while (pattern[z] != '\0')
      {
        switch (pattern[z])
        {
          case '*':
            String_appendCString(matchString,".*");
            z++;
            break;
          case '?':
            String_appendChar(matchString,'.');
            z++;
            break;
          case '.':
            String_appendCString(matchString,"\\.");
            z++;
            break;
          case '\\':
            String_appendChar(matchString,'\\');
            z++;
            if (pattern[z] != '\0')
            {
              String_appendChar(matchString,pattern[z]);
              z++;
            }
            break;
          case '[':
          case ']':
          case '^':
          case '$':
          case '(':
          case ')':
          case '{':
          case '}':
          case '+':
          case '|':
            String_appendChar(matchString,'\\');
            String_appendChar(matchString,pattern[z]);
            z++;
            break;
          default:
            String_appendChar(matchString,pattern[z]);
            z++;
            break;
        }
      }
      break;
    case PATTERN_TYPE_REGEX:
      String_setCString(matchString,pattern);
      break;
    case PATTERN_TYPE_EXTENDED_REGEX:
      regexFlags |= REG_EXTENDED;
      String_setCString(matchString,pattern);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  String_set(regexString,matchString);
  if (String_index(regexString,STRING_BEGIN) != '^') String_insertChar(regexString,STRING_BEGIN,'^');
  error = regcomp(regexBegin,String_cString(regexString),regexFlags);
  if (error != 0)
  {
    regerror(error,regexBegin,buffer,sizeof(buffer)-1); buffer[sizeof(buffer)-1] = '\0';
    String_delete(regexString);
    String_delete(matchString);
    return ERRORX_(INVALID_PATTERN,0,buffer);
  }

  String_set(regexString,matchString);
  if (String_index(regexString,STRING_END) != '$') String_insertChar(regexString,STRING_BEGIN,'$');
  if (regcomp(regexEnd,String_cString(regexString),regexFlags) != 0)
  {
    regerror(error,regexEnd,buffer,sizeof(buffer)-1); buffer[sizeof(buffer)-1] = '\0';
    regfree(regexBegin);
    String_delete(regexString);
    String_delete(matchString);
    return ERRORX_(INVALID_PATTERN,0,buffer);
  }

  String_set(regexString,matchString);
  if (String_index(regexString,STRING_BEGIN) != '^') String_insertChar(regexString,STRING_BEGIN,'^');
  if (String_index(regexString,STRING_END) != '$') String_insertChar(regexString,STRING_END,'$');
  if (regcomp(regexExact,String_cString(regexString),regexFlags) != 0)
  {
    regerror(error,regexExact,buffer,sizeof(buffer)-1); buffer[sizeof(buffer)-1] = '\0';
    regfree(regexEnd);
    regfree(regexBegin);
    String_delete(regexString);
    String_delete(matchString);
    return ERRORX_(INVALID_PATTERN,0,buffer);
  }

  String_set(regexString,matchString);
  if (regcomp(regexAny,String_cString(regexString),regexFlags) != 0)
  {
    regerror(error,regexAny,buffer,sizeof(buffer)-1); buffer[sizeof(buffer)-1] = '\0';
    regfree(regexExact);
    regfree(regexEnd);
    regfree(regexBegin);
    String_delete(regexString);
    String_delete(matchString);
    return ERRORX_(INVALID_PATTERN,0,buffer);
  }

  // free resources
  String_delete(regexString);
  String_delete(matchString);

  return ERROR_NONE;
}

/*---------------------------------------------------------------------*/

Errors Pattern_initAll(void)
{
  return ERROR_NONE;
}

void Pattern_doneAll(void)
{
}

const char *Pattern_patternTypeToString(PatternTypes patternType, const char *defaultValue)
{
  uint       z;
  const char *name;

  z = 0;
  while (   (z < SIZE_OF_ARRAY(PATTERN_TYPES))
         && (PATTERN_TYPES[z].patternType != patternType)
        )
  {
    z++;
  }
  if (z < SIZE_OF_ARRAY(PATTERN_TYPES))
  {
    name = PATTERN_TYPES[z].name;
  }
  else
  {
    name = defaultValue;
  }

  return name;
}

bool Pattern_parsePatternType(const char *name, PatternTypes *patternType)
{
  uint z;

  assert(name != NULL);
  assert(patternType != NULL);

  z = 0;
  while (   (z < SIZE_OF_ARRAY(PATTERN_TYPES))
         && !stringEqualsIgnoreCase(PATTERN_TYPES[z].name,name)
        )
  {
    z++;
  }
  if (z < SIZE_OF_ARRAY(PATTERN_TYPES))
  {
    (*patternType) = PATTERN_TYPES[z].patternType;
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

Errors Pattern_init(Pattern *pattern, const String string, PatternTypes patternType, uint patternFlags)
{
  return Pattern_initCString(pattern,String_cString(string),patternType,patternFlags);
}

Errors Pattern_initCString(Pattern *pattern, const char *string, PatternTypes patternType, uint patternFlags)
{
  Errors error;

  assert(pattern != NULL);

  // initialize pattern
  pattern->type = patternType;

  // compile pattern
  error = compilePattern(string,
                         patternType,
                         patternFlags,
                         &pattern->regexBegin,
                         &pattern->regexEnd,
                         &pattern->regexExact,
                         &pattern->regexAny
                        );
  if (error != ERROR_NONE)
  {
    return error;
  }

  return ERROR_NONE;
}

void Pattern_done(Pattern *pattern)
{
  assert(pattern != NULL);

  regfree(&pattern->regexAny);
  regfree(&pattern->regexExact);
  regfree(&pattern->regexEnd);
  regfree(&pattern->regexBegin);
}

Pattern *Pattern_new(const String string, PatternTypes patternType, uint patternFlags)
{
  Pattern *pattern;
  Errors  error;

  assert(string != NULL);

  pattern = (Pattern*)malloc(sizeof(Pattern));
  if (pattern == NULL)
  {
    return NULL;
  }

  error = Pattern_init(pattern,string,patternType,patternFlags);
  if (error != ERROR_NONE)
  {
    free(pattern);
    return NULL;
  }

  return pattern;
}

void Pattern_delete(Pattern *pattern)
{
  assert(pattern != NULL);

  Pattern_done(pattern);
  free(pattern);
}

bool Pattern_match(const Pattern     *pattern,
                   const String      string,
                   PatternMatchModes patternMatchMode
                  )
{
  bool matchFlag;

  assert(pattern != NULL);
  assert(string != NULL);

  matchFlag = FALSE;
  switch (patternMatchMode)
  {
    case PATTERN_MATCH_MODE_BEGIN:
      matchFlag = (regexec(&pattern->regexBegin,String_cString(string),0,NULL,0) == 0);
      break;
    case PATTERN_MATCH_MODE_END:
      matchFlag = (regexec(&pattern->regexEnd,String_cString(string),0,NULL,0) == 0);
      break;
    case PATTERN_MATCH_MODE_EXACT:
      matchFlag = (regexec(&pattern->regexExact,String_cString(string),0,NULL,0) == 0);
      break;
    case PATTERN_MATCH_MODE_ANY:
      matchFlag = (regexec(&pattern->regexAny,String_cString(string),0,NULL,0) == 0);
      break;
    #ifndef NDEBUG
      default:
        HALT_INTERNAL_ERROR_UNHANDLED_SWITCH_CASE();
        break; /* not reached */
    #endif /* NDEBUG */
  }

  return matchFlag;
}

bool Pattern_checkIsPattern(const String string)
{
  const char *PATTERNS_CHARS = "*?[{";

  ulong i;
  bool  patternFlag;

  assert(string != NULL);

  i = 0L;
  patternFlag = FALSE;
  while ((i < String_length(string)) && !patternFlag)
  {
    if (String_index(string,i) != '\\')
    {
      patternFlag = (strchr(PATTERNS_CHARS,String_index(string,i)) != NULL);
    }
    else
    {
      i++;
    }
    i++;
  }

  return patternFlag;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
