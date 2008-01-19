/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/passwords.c,v $
* $Revision: 1.10 $
* $Author: torsten $
* Contents: functions for secure storage of passwords
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#ifdef HAVE_GCRYPT
  #include <gcrypt.h>
#endif /* HAVE_GCRYPT */
#include <termios.h>
#include <unistd.h>        
#include <assert.h>

#include "global.h"
#include "strings.h"

#include "errors.h"
#include "bar.h"

#include "passwords.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

/***************************** Datatypes *******************************/

/***************************** Variables *******************************/
#ifndef HAVE_GCRYPT
  LOCAL char obfuscator[MAX_PASSWORD_LENGTH];
#endif /* not HAVE_GCRYPT */

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

Errors Password_initAll(void)
{
  #ifndef HAVE_GCRYPT
    int z;
  #endif /* not HAVE_GCRYPT */

  #ifndef HAVE_GCRYPT
    /* if libgrypt is not available a simple obfuscator is used here to
       avoid plain text passwords in memory as much as possible
    */
    srandom((unsigned int)time(NULL));
    for (z = 0; z < MAX_PASSWORD_LENGTH; z++)
    {
      obfuscator[z] = (char)(random()%256);
    }
  #endif /* not HAVE_GCRYPT */

  return ERROR_NONE;
}

void Password_doneAll(void)
{
}

Password *Password_new(void)
{
  Password *password;

//fprintf(stderr,"%s,%d: \n",__FILE__,__LINE__);
  #ifdef HAVE_GCRYPT
    password = (Password*)gcry_malloc_secure(sizeof(Password));
  #else /* not HAVE_GCRYPT */
    password = (Password*)malloc(sizeof(Password));
  #endif /* HAVE_GCRYPT */
//fprintf(stderr,"%s,%d: %p\n",__FILE__,__LINE__,password);
  if (password == NULL)
  {
    HALT_INSUFFICIENT_MEMORY();
  }
  password->length = 0;

  return password;
}

Password *Password_newCString(const char *s)
{
  Password *password;

  password = Password_new();
  Password_setCString(password,s);

  return password;
}

void Password_delete(Password *password)
{
  if (password != NULL)
  {
//fprintf(stderr,"%s,%d: %p\n",__FILE__,__LINE__,password);
    #ifdef HAVE_GCRYPT
      gcry_free(password);
    #else /* not HAVE_GCRYPT */
      memset(password,0,sizeof(Password));
      free(password);
    #endif /* HAVE_GCRYPT */
  }
}

void Password_clear(Password *password)
{
  assert(password != NULL);

  password->length = 0;
  password->data[0] = '\0';
}

Password *Password_duplicate(const Password *sourcePassword)
{
  Password *destinationPassword;

  if (sourcePassword != NULL)
  {
    destinationPassword = Password_new();
    assert(destinationPassword != NULL);
    memcpy(destinationPassword,sourcePassword,sizeof(Password));
  }
  else
  {
    destinationPassword = NULL;
  }

  return destinationPassword;
}

void Password_set(Password *password, const Password *fromPassword)
{
  assert(password != NULL);

  if (fromPassword != NULL)
  {
    password->length = fromPassword->length;
    memcpy(password->data,fromPassword->data,sizeof(password->data));
  }
  else
  {
    password->length = 0;
  }
}

void Password_setString(Password *password, const String string)
{
  #ifdef HAVE_GCRYPT
  #else /* not HAVE_GCRYPT */
    uint z;
  #endif /* HAVE_GCRYPT */

  assert(password != NULL);

  password->length = MIN(String_length(string),MAX_PASSWORD_LENGTH);
  #ifdef HAVE_GCRYPT
    memcpy(password->data,String_cString(string),password->length);
  #else /* not HAVE_GCRYPT */
    for (z = 0; z < MIN(String_length(string),MAX_PASSWORD_LENGTH); z++)
    {
      password->data[z] = String_index(string,z)^obfuscator[z];
    }
  #endif /* HAVE_GCRYPT */
  password->data[password->length] = '\0';
}

void Password_setCString(Password *password, const char *s)
{
  #ifdef HAVE_GCRYPT
  #else /* not HAVE_GCRYPT */
    uint z;
  #endif /* HAVE_GCRYPT */

  assert(password != NULL);

  password->length = MIN(strlen(s),MAX_PASSWORD_LENGTH);
  #ifdef HAVE_GCRYPT
    memcpy(password->data,s,password->length);
  #else /* not HAVE_GCRYPT */
    for (z = 0; z < MIN(strlen(s),MAX_PASSWORD_LENGTH); z++)
    {
      password->data[z] = s[z]^obfuscator[z];
    }
  #endif /* HAVE_GCRYPT */
  password->data[password->length] = '\0';
}

void Password_appendChar(Password *password, char ch)
{
  assert(password != NULL);

  if (password->length < MAX_PASSWORD_LENGTH)
  {
    #ifdef HAVE_GCRYPT
      password->data[password->length] = ch;
    #else /* not HAVE_GCRYPT */
      password->data[password->length] = ch^obfuscator[password->length];
    #endif /* HAVE_GCRYPT */
    password->length++;
    password->data[password->length] = '\0';
  }
}

uint Password_length(const Password *password)
{
  return (password != NULL)?password->length:0;
}

char Password_getChar(const Password *password, uint index)
{
  if ((password != NULL) && (index < password->length))
  {
    #ifdef HAVE_GCRYPT
      return password->data[index];
    #else /* not HAVE_GCRYPT */
      return password->data[index]^obfuscator[index];
    #endif /* HAVE_GCRYPT */
  }
  else
  {
    return '\0';
  }
}

double Password_getQualityLevel(const Password *password)
{
  #define CHECK(condition) \
    do { \
      if (condition) browniePoints++; \
      maxBrowniePoints++; \
    } while (0)

  uint browniePoints,maxBrowniePoints;
  bool flag0,flag1;
  uint z;

  assert(password != NULL);

  browniePoints    = 0;
  maxBrowniePoints = 0;

  /* length >= 8 */
  CHECK(password->length >= 8);

  /* contain numbers */
  flag0 = FALSE;
  for (z = 0; z < password->length; z++)
  {
    flag0 |= isdigit(password->data[z]);
  }
  CHECK(flag0);

  /* contain special characters */
  flag0 = FALSE;
  for (z = 0; z < password->length; z++)
  {
    flag0 |= (strchr(" !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",password->data[z]) != NULL);
  }
  CHECK(flag0);

  /* capital/non-capital letters */
  flag0 = FALSE;
  flag1 = FALSE;
  for (z = 0; z < password->length; z++)
  {
    flag0 |= (toupper(password->data[z]) != password->data[z]);
    flag1 |= (tolower(password->data[z]) != password->data[z]);
  }
  CHECK(flag0 && flag1);

  /* estimate entropie by compression */
// ToDo

  return (double)browniePoints/(double)maxBrowniePoints;

  #undef CHECK
}

const char *Password_deploy(Password *password)
{
  #ifdef HAVE_GCRYPT
  #else /* not HAVE_GCRYPT */
    uint z;
  #endif /* HAVE_GCRYPT */

  if (password != NULL)
  {
    assert(password->length <= MAX_PASSWORD_LENGTH);

    #ifdef HAVE_GCRYPT
      return password->data;
    #else /* not HAVE_GCRYPT */
      for (z = 0; z < password->length; z++)
      {
        password->plain[z] = password->data[z]^obfuscator[z];
      }
      password->plain[password->length] = '\0';
      return password->plain;
    #endif /* HAVE_GCRYPT */
  }
  else
  {
    return "";
  }
}

void Password_undeploy(Password *password)
{
  if (password != NULL)
  {
    #ifdef HAVE_GCRYPT
      UNUSED_VARIABLE(password);
    #else /* not HAVE_GCRYPT */
      memset(password->plain,0,MAX_PASSWORD_LENGTH);
    #endif /* HAVE_GCRYPT */
  }
}

bool Password_input(Password *password, const char *title)
{
  bool okFlag;

  assert(password != NULL);

  Password_clear(password);

  okFlag = FALSE;

  /* input via SSH_ASKPASS program */
  if (!okFlag)
  {
    const char *sshAskPassword;
    String     command;
    FILE       *file;
    bool       eolFlag;
    int        ch;

    sshAskPassword = getenv("SSH_ASKPASS");
    if (sshAskPassword != NULL)
    {
      /* open pipe to external password program */
      command = String_newCString(sshAskPassword);
      if (title != NULL)
      {
        String_format(command," '%s:'",title);
      }
      file = popen(String_cString(command),"r");
      if (file == NULL)
      {
        String_delete(command);
        return FALSE;
      }
      String_delete(command);

      /* read password, discard last LF */
      printInfo(2,"Wait for password...");
      eolFlag = FALSE;
      do
      {
        ch = getc(file);
        if (ch != EOF)
        {
          switch ((char)ch)
          {
            case '\n':
            case '\r':
              eolFlag = TRUE;
              break;
            default:
              Password_appendChar(password,(char)ch);
              break;
          }
        }
        else
        {
          eolFlag = TRUE;
        }
      }
      while (!eolFlag);
      printInfo(2,"ok\n");

      /* close pipe */
      pclose(file);

      okFlag = TRUE;
    }
  }

  /* input via console */
  if (!okFlag)
  {
    struct termios oldTermioSettings;
    struct termios termioSettings;
    bool           eolFlag;
    char           ch;

    /* save current console settings */
    if (tcgetattr(STDIN_FILENO,&oldTermioSettings) != 0)
    {
      return FALSE;
    }

    /* disable echo */
    memcpy(&termioSettings,&oldTermioSettings,sizeof(struct termios));
    termioSettings.c_lflag &= ~ECHO;
    if (tcsetattr(STDIN_FILENO,TCSANOW,&termioSettings) != 0)
    {
      return FALSE;
    }

    /* input password */
    if (title != NULL)
    {
      printf("%s: ",title);
    }
    eolFlag = FALSE;
    do
    {
      read(STDIN_FILENO,&ch,1);
      switch (ch)
      {
        case '\n':
        case '\r':
          eolFlag = TRUE;
          break;
        default:
          Password_appendChar(password,ch);
          break;
      }
    }
    while (!eolFlag);
    if (title != NULL)
    {
      printf("\n");
    }

    /* restore console settings */
    tcsetattr(STDIN_FILENO,TCSANOW,&oldTermioSettings);

    okFlag = TRUE;
  }
//fprintf(stderr,"%s,%d: #%s#\n",__FILE__,__LINE__,password->data);

  return okFlag;
}

#ifdef __cplusplus
  }
#endif

/* end of file */
