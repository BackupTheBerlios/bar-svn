/***********************************************************************\
*
* $Source: /home/torsten/cvs/bar/bar.c,v $
* $Revision: 1.32 $
* $Author: torsten $
* Contents: Backup ARchiver main program
* Systems: all
*
\***********************************************************************/

/****************************** Includes *******************************/
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include "global.h"
#include "cmdoptions.h"
#include "strings.h"
#include "stringlists.h"
#include "arrays.h"

#include "errors.h"
#include "files.h"
#include "patterns.h"
#include "compress.h"
#include "passwords.h"
#include "crypt.h"
#include "archive.h"
#include "network.h"
#include "storage.h"

#include "commands_create.h"
#include "commands_list.h"
#include "commands_restore.h"
#include "commands_test.h"
#include "server.h"

#include "bar.h"

/****************** Conditional compilation switches *******************/

/***************************** Constants *******************************/

#define __VERSION_TO_STRING(z) __VERSION_TO_STRING_TMP(z)
#define __VERSION_TO_STRING_TMP(z) #z
#define VERSION __VERSION_TO_STRING(VERSION_MAJOR) "." __VERSION_TO_STRING(VERSION_MINOR)

#define DEFAULT_CONFIG_FILE_NAME       "bar.cfg"
#define DEFAULT_TMP_DIRECTORY          "/tmp"
#define DEFAULT_COMPRESS_MIN_FILE_SIZE 32
#define DEFAULT_SERVER_PORT            38523
#ifdef HAVE_GNU_TLS
  #define DEFAULT_SERVER_TLS_PORT        38524
#else /* not HAVE_GNU_TLS */
  #define DEFAULT_SERVER_TLS_PORT        0
#endif /* HAVE_GNU_TLS */

/***************************** Datatypes *******************************/

typedef enum
{
  COMMAND_NONE,

  COMMAND_CREATE,
  COMMAND_LIST,
  COMMAND_TEST,
  COMMAND_RESTORE,

  COMMAND_UNKNOWN,
} Commands;

/***************************** Variables *******************************/
Options defaultOptions;

LOCAL Commands    command;
LOCAL const char  *archiveFileName;
LOCAL PatternList includePatternList;
LOCAL PatternList excludePatternList;
LOCAL bool        daemonFlag;
LOCAL uint        serverPort;
LOCAL bool        serverTLSPort;
LOCAL const char  *serverCAFileName;
LOCAL const char  *serverCertFileName;
LOCAL const char  *serverKeyFileName;
LOCAL Password    *serverPassword;
LOCAL bool        versionFlag;
LOCAL bool        helpFlag;

const CommandLineUnit COMMAND_LINE_BYTES_UNITS[] =
{
  {"K",1024},
  {"M",1024*1024},
  {"G",1024*1024*1024},
};

const CommandLineUnit COMMAND_LINE_BITS_UNITS[] =
{
  {"K",1024},
};

const CommandLineOptionSelect COMMAND_LINE_OPTIONS_PATTERN_TYPE[] =
{
  {"glob",  PATTERN_TYPE_GLOB,    "glob patterns: * and ?"},
  {"basic", PATTERN_TYPE_BASIC,   "basic pattern matching"},
  {"extend",PATTERN_TYPE_EXTENDED,"extended pattern matching"},
};

const CommandLineOptionSelect COMMAND_LINE_OPTIONS_COMPRESS_ALGORITHM[] =
{
  {"none", COMPRESS_ALGORITHM_NONE,   "no compression"           },

  {"zip0", COMPRESS_ALGORITHM_ZIP_0,  "ZIP compression level 0"  },
  {"zip1", COMPRESS_ALGORITHM_ZIP_1,  "ZIP compression level 1"  },
  {"zip2", COMPRESS_ALGORITHM_ZIP_2,  "ZIP compression level 2"  },
  {"zip3", COMPRESS_ALGORITHM_ZIP_3,  "ZIP compression level 3"  },
  {"zip4", COMPRESS_ALGORITHM_ZIP_4,  "ZIP compression level 4"  },
  {"zip5", COMPRESS_ALGORITHM_ZIP_5,  "ZIP compression level 5"  },
  {"zip6", COMPRESS_ALGORITHM_ZIP_6,  "ZIP compression level 6"  },
  {"zip7", COMPRESS_ALGORITHM_ZIP_7,  "ZIP compression level 7"  },
  {"zip8", COMPRESS_ALGORITHM_ZIP_8,  "ZIP compression level 8"  },
  {"zip9", COMPRESS_ALGORITHM_ZIP_9,  "ZIP compression level 9"  },

  {"bzip1",COMPRESS_ALGORITHM_BZIP2_1,"BZIP2 compression level 1"},
  {"bzip2",COMPRESS_ALGORITHM_BZIP2_2,"BZIP2 compression level 2"},
  {"bzip3",COMPRESS_ALGORITHM_BZIP2_3,"BZIP2 compression level 3"},
  {"bzip4",COMPRESS_ALGORITHM_BZIP2_4,"BZIP2 compression level 4"},
  {"bzip5",COMPRESS_ALGORITHM_BZIP2_5,"BZIP2 compression level 5"},
  {"bzip6",COMPRESS_ALGORITHM_BZIP2_6,"BZIP2 compression level 6"},
  {"bzip7",COMPRESS_ALGORITHM_BZIP2_7,"BZIP2 compression level 7"},
  {"bzip8",COMPRESS_ALGORITHM_BZIP2_8,"BZIP2 compression level 8"},
  {"bzip9",COMPRESS_ALGORITHM_BZIP2_9,"BZIP2 compression level 9"},
};

const CommandLineOptionSelect COMMAND_LINE_OPTIONS_CRYPT_ALGORITHM[] =
{
  {"none",      CRYPT_ALGORITHM_NONE,      "no crypting"          },

  {"3DES",      CRYPT_ALGORITHM_3DES,      "3DES cipher"          },
  {"CAST5",     CRYPT_ALGORITHM_CAST5,     "CAST5 cipher"         },
  {"BLOWFISH",  CRYPT_ALGORITHM_BLOWFISH,  "Blowfish cipher"      },
  {"AES128",    CRYPT_ALGORITHM_AES128,    "AES cipher 128bit"    },
  {"AES192",    CRYPT_ALGORITHM_AES192,    "AES cipher 192bit"    },
  {"AES256",    CRYPT_ALGORITHM_AES256,    "AES cipher 256bit"    },
  {"TWOFISH128",CRYPT_ALGORITHM_TWOFISH128,"Twofish cipher 128bit"},
  {"TWOFISH256",CRYPT_ALGORITHM_TWOFISH256,"Twofish cipher 256bit"},
};

LOCAL bool cmdParseConfigFile(void *variable, const char *name, const char *value, const void *defaultValue, void *userData);
LOCAL bool cmdParseIncludeExclude(void *variable, const char *name, const char *value, const void *defaultValue, void *userData);
LOCAL bool cmdParsePassword(void *variable, const char *name, const char *value, const void *defaultValue, void *userData);

LOCAL const CommandLineOption COMMAND_LINE_OPTIONS[] =
{
  CMD_OPTION_ENUM     ("create",                   'c',0,command,                                 COMMAND_NONE,COMMAND_CREATE,                                       "create new archive"                            ),
  CMD_OPTION_ENUM     ("list",                     'l',0,command,                                 COMMAND_NONE,COMMAND_LIST,                                         "list contents of archive"                      ),
  CMD_OPTION_ENUM     ("test",                     't',0,command,                                 COMMAND_NONE,COMMAND_TEST,                                         "test contents of ardhive"                      ),
  CMD_OPTION_ENUM     ("extract",                  'x',0,command,                                 COMMAND_NONE,COMMAND_RESTORE,                                      "restore archive"                               ),

  CMD_OPTION_SPECIAL  ("config",                   0,  0,NULL,                                    NULL,cmdParseConfigFile,NULL,                                      "configuration file","file name"                ),

  CMD_OPTION_INTEGER  ("archive-part-size",        's',0,defaultOptions.archivePartSize,          0,0,LONG_MAX,COMMAND_LINE_BYTES_UNITS,                             "approximated part size"                        ),
  CMD_OPTION_STRING   ("tmp-directory",            0,  0,defaultOptions.tmpDirectory,             DEFAULT_TMP_DIRECTORY,                                             "temporary directory","path"                    ),
  CMD_OPTION_INTEGER64("max-tmp-size",             0,  0,defaultOptions.maxTmpSize,               0,0,LONG_MAX,COMMAND_LINE_BYTES_UNITS,                             "max. size of temporary files"                  ),
  CMD_OPTION_INTEGER  ("directory-strip",          'p',0,defaultOptions.directoryStripCount,      0,0,LONG_MAX,NULL,                                                 "number of directories to strip on extract"     ),
  CMD_OPTION_STRING   ("directory",                0,  0,defaultOptions.directory,                NULL,                                                              "directory to restore files","path"             ),

  CMD_OPTION_INTEGER  ("max-band-width",           0,  0,defaultOptions.maxBandWidth,             0,0,LONG_MAX,COMMAND_LINE_BITS_UNITS,                              "max. network band width to use"                 ),

  CMD_OPTION_SELECT   ("pattern-type",             0,  0,defaultOptions.patternType,              PATTERN_TYPE_GLOB,COMMAND_LINE_OPTIONS_PATTERN_TYPE,               "select pattern type"                           ),

  CMD_OPTION_SPECIAL  ("include",                  'i',1,&includePatternList,                     NULL,cmdParseIncludeExclude,NULL,                                  "include pattern","pattern"                     ),
  CMD_OPTION_SPECIAL  ("exclude",                  '!',1,&excludePatternList,                     NULL,cmdParseIncludeExclude,NULL,                                  "exclude pattern","pattern"                     ),
 
  CMD_OPTION_SELECT   ("compress-algorithm",       'z',0,defaultOptions.compressAlgorithm,        COMPRESS_ALGORITHM_NONE,COMMAND_LINE_OPTIONS_COMPRESS_ALGORITHM,   "select compress algorithm to use"              ),
  CMD_OPTION_INTEGER  ("compress-min-size",        0,  0,defaultOptions.compressMinFileSize,      DEFAULT_COMPRESS_MIN_FILE_SIZE,0,LONG_MAX,COMMAND_LINE_BYTES_UNITS,"minimal size of file for compression"          ),

  CMD_OPTION_SELECT   ("crypt-algorithm",          'y',0,defaultOptions.cryptAlgorithm,           CRYPT_ALGORITHM_NONE,COMMAND_LINE_OPTIONS_CRYPT_ALGORITHM,         "select crypt algorithm to use"                 ),
  CMD_OPTION_SPECIAL  ("crypt-password",           0,  0,&defaultOptions.cryptPassword,           NULL,cmdParsePassword,NULL,                                        "crypt password (use with care!)","password"    ),

  CMD_OPTION_INTEGER  ("ssh-port",                 0,  0,defaultOptions.sshPort,                  0,0,65535,NULL,                                                    "ssh port"                                      ),
  CMD_OPTION_STRING   ("ssh-public-key",           0,  0,defaultOptions.sshPublicKeyFileName,     NULL,                                                              "ssh public key file name","file name"          ),
  CMD_OPTION_STRING   ("ssh-privat-key",           0,  0,defaultOptions.sshPrivatKeyFileName,     NULL,                                                              "ssh privat key file name","file name"          ),
  CMD_OPTION_SPECIAL  ("ssh-password",             0,  0,&defaultOptions.sshPassword,             NULL,cmdParsePassword,NULL,                                        "ssh password (use with care!)","password"      ),

  CMD_OPTION_BOOLEAN  ("daemon",                   0,  0,daemonFlag,                              FALSE,                                                             "run in daemon mode"                            ),
  CMD_OPTION_INTEGER  ("port",                     0,  0,serverPort,                              DEFAULT_SERVER_PORT,0,65535,NULL,                                  "server port"                                   ),
  CMD_OPTION_INTEGER  ("tls-port",                 0,  0,serverTLSPort,                           DEFAULT_SERVER_TLS_PORT,0,65535,NULL,                              "server TLS (SSL) port"                         ),
  CMD_OPTION_STRING   ("server-ca-file",           0,  0,serverCAFileName,                        NULL,                                                              "server TLS CA file","file name"                ),
  CMD_OPTION_STRING   ("server-cert-file",         0,  0,serverCertFileName,                      NULL,                                                              "server TLS certificate file","file name"       ),
  CMD_OPTION_STRING   ("server-key-file",          0,  0,serverKeyFileName,                       NULL,                                                              "server TLS key file","file name"               ),
  CMD_OPTION_SPECIAL  ("server-password",          0,  0,&serverPassword,                         NULL,cmdParsePassword,NULL,                                        "server password (use with care!)","password"   ),

//  CMD_OPTION_BOOLEAN  ("incremental",              0,  0,defaultOptions.incrementalFlag,        FALSE,                                                             "overwrite existing files"                      ),
  CMD_OPTION_BOOLEAN  ("skip-unreadable",          0,  0,defaultOptions.skipUnreadableFlag,       TRUE,                                                              "skip unreadable files"                         ),
  CMD_OPTION_BOOLEAN  ("overwrite-archives-files", 0,  0,defaultOptions.overwriteArchiveFilesFlag,FALSE,                                                             "overwrite existing archive files"              ),
  CMD_OPTION_BOOLEAN  ("overwrite-files",          0,  0,defaultOptions.overwriteFilesFlag,       FALSE,                                                             "overwrite existing files"                      ),
  CMD_OPTION_BOOLEAN  ("no-default-config",        0,  0,defaultOptions.noDefaultConfigFlag,      FALSE,                                                             "do not read personal config file ~/bar/bar.cfg"),
  CMD_OPTION_BOOLEAN  ("quiet",                    0,  0,defaultOptions.quietFlag,                FALSE,                                                             "surpress any output"                           ),
  CMD_OPTION_INTEGER_RANGE("verbose",            'v',0,defaultOptions.verboseLevel,             1,0,3,NULL,                                                     "verbosity level"                               ),

  CMD_OPTION_BOOLEAN("version",                    0  ,0,versionFlag,                             FALSE,                                                             "print version"                                 ),
  CMD_OPTION_BOOLEAN("help",                       'h',0,helpFlag,                                FALSE,                                                             "print this help"                               ),
};

/****************************** Macros *********************************/

/***************************** Forwards ********************************/

/***************************** Functions *******************************/

#ifdef __cplusplus
  extern "C" {
#endif

/***********************************************************************\
* Name   : readConfigFile
* Purpose: read configuration from file
* Input  : fileName - file name
* Output : -
* Return : TRUE iff configuration read, FALSE otherwise (error)
* Notes  : -
\***********************************************************************/

LOCAL bool readConfigFile(String fileName)
{
  Errors                  error;
  FileHandle              fileHandle;
  bool                    failFlag;
  uint                    lineNb;
  String                  line;
  String                  name,value;
  const CommandLineOption *commandLineOption;

  assert(fileName != NULL);

  /* open file */
  error = File_open(&fileHandle,fileName,FILE_OPENMODE_READ);
  if (error != ERROR_NONE)
  {
    printError("Cannot open file '%s' (error: %s)!\n",
               String_cString(fileName),
               getErrorText(error)
              );
    return FALSE;
  }

  /* parse file */
  failFlag = FALSE;
  lineNb   = 0;
  line     = String_new();
  name     = String_new();
  value    = String_new();
  while (!File_eof(&fileHandle) && !failFlag)
  {
    /* read line */
    error = File_readLine(&fileHandle,line);
    if (error != ERROR_NONE)
    {
      printError("Cannot open file '%s' (error: %s)!\n",
                 String_cString(fileName),
                 getErrorText(error)
                );
      failFlag = TRUE;
      break;
    }
    lineNb++;

    /* skip comments, empty lines */
    if ((String_length(line) == 0) || (String_index(line,0) == '#'))
    {
      continue;
    }

    /* parse line */
    if (String_parse(line,"%S=%S",NULL,name,value))
    {
//fprintf(stderr,"%s,%d: %s %s\n",__FILE__,__LINE__,String_cString(name),String_cString(value));
      /* find command line option */
      commandLineOption = CmdOption_find(String_cString(name),
                                         COMMAND_LINE_OPTIONS,SIZE_OF_ARRAY(COMMAND_LINE_OPTIONS)
                                        );
      if (commandLineOption == NULL)
      {
        printError("Unknown option '%s' in %s, line %ld\n",
                   String_cString(name),
                   String_cString(fileName),
                   lineNb
                  );
        failFlag = TRUE;
        break;
      }

      /* parse command line option */
      if (!CmdOption_parseString(commandLineOption,
                                 String_cString(value)
                                )
         )
      {
        printError("Error in option '%s' in %s, line %ld\n",
                   String_cString(name),
                   String_cString(fileName),
                   lineNb
                  );
        failFlag = TRUE;
        break;
      }
    }
    else
    {
      printError("Error in %s, line %ld: %s\n",
                 String_cString(fileName),
                 lineNb,
                 String_cString(line)
                );
      failFlag = TRUE;
      break;
    }
  }
  String_delete(value);
  String_delete(name);
  String_delete(line);

  /* close file */
  File_close(&fileHandle);

  /* free resources */

  return !failFlag;
}

/***********************************************************************\
* Name   : cmdParseConfigFile
* Purpose: command line option call back for parsing config file
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL bool cmdParseConfigFile(void *variable, const char *name, const char *value, const void *defaultValue, void *userData)
{
  String fileName;
  bool   result;

  assert(value != NULL);

  UNUSED_VARIABLE(variable);
  UNUSED_VARIABLE(name);
  UNUSED_VARIABLE(defaultValue);
  UNUSED_VARIABLE(userData);

  fileName = String_newCString(value);
  result = readConfigFile(fileName);
  String_delete(fileName);

  return TRUE;
}

/***********************************************************************\
* Name   : cmdParseIncludeExclude
* Purpose: command line option call back for parsing include/exclude
*          patterns
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL bool cmdParseIncludeExclude(void *variable, const char *name, const char *value, const void *defaultValue, void *userData)
{
  assert(variable != NULL);
  assert(value != NULL);

  UNUSED_VARIABLE(defaultValue);
  UNUSED_VARIABLE(userData);

  if (Pattern_appendList((PatternList*)variable,value,defaultOptions.patternType) != ERROR_NONE)
  {
    fprintf(stderr,"Cannot parse varlue '%s' of option '%s'!\n",value,name);
    return FALSE;
  }

  return TRUE;
}

/***********************************************************************\
* Name   : cmdParsePassword
* Purpose: command line option call back for parsing password
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL bool cmdParsePassword(void *variable, const char *name, const char *value, const void *defaultValue, void *userData)
{
  assert(variable != NULL);
  assert(value != NULL);

  UNUSED_VARIABLE(name);
  UNUSED_VARIABLE(defaultValue);
  UNUSED_VARIABLE(userData);

  Password_setCString((*(Password**)variable),value);

  return TRUE;
}

/***********************************************************************\
* Name       : printUsage
* Purpose    : print "usage" help
* Input      : -
* Output     : -
* Return     : -
* Side-effect: unknown
* Notes      : -
\***********************************************************************/

LOCAL void printUsage(const char *programName)
{
  assert(programName != NULL);

  printf("Usage: %s [<options>] [--] <archive name>|scp:<name>@<host name>:<archive name>... [<files>...]\n",programName);
  printf("\n");
  CmdOption_printHelp(stdout,
                      COMMAND_LINE_OPTIONS,SIZE_OF_ARRAY(COMMAND_LINE_OPTIONS)
                     );
}

/***********************************************************************\
* Name   : init
* Purpose: initialize
* Input  : -
* Output : -
* Return : TRUE if init ok, FALSE on error
* Notes  : -
\***********************************************************************/

LOCAL bool init(void)
{
  Errors error;

  error = Password_init();
  if (error != ERROR_NONE)
  {
    return FALSE;
  }
  error = Crypt_init();
  if (error != ERROR_NONE)
  {
    Password_done();
    return FALSE;
  }
  error = Pattern_init();
  if (error != ERROR_NONE)
  {
    Crypt_done();
    Password_done();
    return FALSE;
  }
  error = Archive_init();
  if (error != ERROR_NONE)
  {
    Pattern_done();
    Crypt_done();
    Password_done();
    return FALSE;
  }
  error = Storage_init();
  if (error != ERROR_NONE)
  {
    Archive_done();
    Pattern_done();
    Crypt_done();
    Password_done();
    return FALSE;
  }
  error = Network_init();
  if (error != ERROR_NONE)
  {
    Storage_done();
    Archive_done();
    Pattern_done();
    Crypt_done();
    Password_done();
    return FALSE;
  }
  error = Server_init();
  if (error != ERROR_NONE)
  {
    Network_done();
    Storage_done();
    Archive_done();
    Pattern_done();
    Crypt_done();
    Password_done();
    return FALSE;
  }

  return TRUE;
}

/***********************************************************************\
* Name   : done
* Purpose: deinitialize
* Input  : -
* Output : -
* Return : -
* Notes  : -
\***********************************************************************/

LOCAL void done(void)
{
  Server_done();
  Network_done();
  Storage_done();
  Archive_done();
  Pattern_done();
  Crypt_done();
  Password_done();
}

/*---------------------------------------------------------------------*/

void info(uint verboseLevel, const char *format, ...)
{
  va_list arguments;

  assert(format != NULL);

  if (!defaultOptions.quietFlag && (defaultOptions.verboseLevel >= verboseLevel))
  {
    va_start(arguments,format);
    vprintf(format,arguments);
    va_end(arguments);
    fflush(stdout);
  }
}

void warning(const char *format, ...)
{
  va_list arguments;

  assert(format != NULL);

  printf("Warning: ");
  va_start(arguments,format);
  vprintf(format,arguments);
  va_end(arguments);
  fflush(stdout);
}

void printError(const char *text, ...)
{
  va_list arguments;

  assert(text != NULL);

  va_start(arguments,text);
  fprintf(stderr,"ERROR: ");
  vfprintf(stderr,text,arguments);
  va_end(arguments);
}

/*---------------------------------------------------------------------*/

int main(int argc, const char *argv[])
{
  String fileName;
  Errors error;

  /* init */
  if (!init())
  {
    return EXITCODE_INIT_FAIL;
  }

  /* initialise variables */
  defaultOptions.cryptPassword = Password_new();
  defaultOptions.sshPassword = Password_new();
  serverPassword = Password_new();
  CmdOption_init(COMMAND_LINE_OPTIONS,SIZE_OF_ARRAY(COMMAND_LINE_OPTIONS));
  Pattern_initList(&includePatternList);
  Pattern_initList(&excludePatternList);

  /* parse command line: pre-options */
  if (!CmdOption_parse(argv,&argc,
                       COMMAND_LINE_OPTIONS,SIZE_OF_ARRAY(COMMAND_LINE_OPTIONS),
                       0,
                       stderr,NULL
                      )
     )
  {
    Pattern_doneList(&excludePatternList);
    Pattern_doneList(&includePatternList);
    Password_delete(serverPassword);
    Password_delete(defaultOptions.sshPassword);
    Password_delete(defaultOptions.cryptPassword);
    done();
    return EXITCODE_INVALID_ARGUMENT;
  }

  if (!defaultOptions.noDefaultConfigFlag)
  {
    /* read default configuration from $HOME/.bar/bar.cfg (if exists) */
    fileName = String_new();
    File_setFileNameCString(fileName,getenv("HOME"));
    File_appendFileNameCString(fileName,".bar");
    File_appendFileNameCString(fileName,DEFAULT_CONFIG_FILE_NAME);
    if (File_exists(fileName))
    {
      if (!readConfigFile(fileName))
      {
        String_delete(fileName);
        #ifndef NDEBUG
          String_debug();
        #endif /* not NDEBUG */
        Pattern_doneList(&excludePatternList);
        Pattern_doneList(&includePatternList);
        Password_delete(serverPassword);
        Password_delete(defaultOptions.sshPassword);
        Password_delete(defaultOptions.cryptPassword);
        done();
        return EXITCODE_CONFIG_ERROR;
      }
    }
    String_delete(fileName);
  }

  /* parse command line: all */
  if (!CmdOption_parse(argv,&argc,
                       COMMAND_LINE_OPTIONS,SIZE_OF_ARRAY(COMMAND_LINE_OPTIONS),
                       CMD_PRIORITY_ANY,
                       stderr,NULL
                      )
     )
  {
    Pattern_doneList(&excludePatternList);
    Pattern_doneList(&includePatternList);
    Password_delete(serverPassword);
    Password_delete(defaultOptions.sshPassword);
    Password_delete(defaultOptions.cryptPassword);
    done();
    return EXITCODE_INVALID_ARGUMENT;
  }
  if (versionFlag)
  {
    printf("BAR version %s\n",VERSION);
    Pattern_doneList(&excludePatternList);
    Pattern_doneList(&includePatternList);
    Password_delete(serverPassword);
    Password_delete(defaultOptions.sshPassword);
    Password_delete(defaultOptions.cryptPassword);
    done();
    return EXITCODE_OK;
  }
  if (helpFlag)
  {
    printUsage(argv[0]);

    Pattern_doneList(&excludePatternList);
    Pattern_doneList(&includePatternList);
    Password_delete(serverPassword);
    Password_delete(defaultOptions.sshPassword);
    Password_delete(defaultOptions.cryptPassword);
    done();
    return EXITCODE_OK;
  }

  error = ERROR_NONE;
  if (!daemonFlag)
  {
    switch (command)
    {
      case COMMAND_CREATE:
        {
          int    z;
          Errors error;

          /* get archive filename */
          if (argc < 1)
          {
            printError("No archive filename given!\n");
            error = ERROR_INVALID_ARGUMENT;
            break;
          }
          archiveFileName = argv[1];

          /* get include patterns */
          for (z = 2; z < argc; z++)
          {
            error = Pattern_appendList(&includePatternList,argv[z],defaultOptions.patternType);
          }

          /* create archive */
          error = Command_create(archiveFileName,
                                 &includePatternList,
                                 &excludePatternList,
                                 &defaultOptions,
                                 NULL,
                                 NULL,
                                 NULL
                                );
        }
        break;
      case COMMAND_LIST:
      case COMMAND_TEST:
      case COMMAND_RESTORE:
        {
          StringList fileNameList;
          int        z;

          /* get archive files */
          StringList_init(&fileNameList);
          for (z = 1; z < argc; z++)
          {
            StringList_appendCString(&fileNameList,argv[z]);
          }

          switch (command)
          {
            case COMMAND_LIST:
              error = Command_list(&fileNameList,
                                   &includePatternList,
                                   &excludePatternList,
                                   &defaultOptions
                                  );
              break;
            case COMMAND_TEST:
              error = Command_test(&fileNameList,
                                   &includePatternList,
                                   &excludePatternList,
                                   &defaultOptions
                                  );
              break;
            case COMMAND_RESTORE:
              error = Command_restore(&fileNameList,
                                      &includePatternList,
                                      &excludePatternList,
                                      &defaultOptions
                                     );
              break;
            default:
              break;
          }

          /* free resources */
          StringList_done(&fileNameList);
        }
        break;
      default:
        printError("No command given!\n");
        error = ERROR_INVALID_ARGUMENT;
        break;
    }
  }
  else
  {
    /* daemon mode -> run server */
    if ((serverPort != 0) || (serverTLSPort != 0))
    {
      error = Server_run(serverPort,
                         serverTLSPort,
                         serverCAFileName,
                         serverCertFileName,
                         serverKeyFileName,
                         serverPassword
                        );
    }
    else
    {
      printError("No port number specified!\n");
      error = ERROR_INVALID_ARGUMENT;
    }
  }

  /* free resources */
  CmdOption_done(COMMAND_LINE_OPTIONS,SIZE_OF_ARRAY(COMMAND_LINE_OPTIONS));
  Pattern_doneList(&excludePatternList);
  Pattern_doneList(&includePatternList);
  Password_delete(serverPassword);
  Password_delete(defaultOptions.sshPassword);
  Password_delete(defaultOptions.cryptPassword);
  done();

  #ifndef NDEBUG
    Array_debug();
    String_debug();
  #endif /* not NDEBUG */

  switch (error)
  {
    case ERROR_NONE:
      return EXITCODE_OK;
      break;
    case ERROR_INVALID_ARGUMENT:
      return EXITCODE_INVALID_ARGUMENT;
      break;
    case ERROR_CONFIG:
      return EXITCODE_CONFIG_ERROR;
    case ERROR_FUNCTION_NOT_SUPPORTED:
      return EXITCODE_FUNCTION_NOT_SUPPORTED;
      break;
    default:
      return EXITCODE_FAIL;
      break;
  }
 }

#ifdef __cplusplus
  }
#endif

/* end of file */
