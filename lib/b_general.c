#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_general.c,v 1.21 2001/11/07 21:35:32 seth Exp $";
static char libbk__copyright[] = "Copyright (c) 2001";
static char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2001 The Authors.  All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright LIBBK--
 */

/**
 * @file
 * The BAKA Thread/global state manipulation routines, providing basic, common, core libbk functionality
 */

#include <libbk.h>
#include "libbk_internal.h"



static struct bk_proctitle *bk_general_proctitle_init(bk_s B, int argc, char ***argv, char ***envp, char **program, bk_flags flags);
static void bk_general_proctitle_destroy(bk_s B, struct bk_proctitle *bkp, bk_flags flags);



/**
 * Process title information--saved arguments, environment, and current title
 */
struct bk_proctitle
{
  int		bp_argc;			///< Number of program arguments
  char		**bp_argv;			///< Program and arguments
  char		**bp_envp;			///< Environment
  bk_vptr	bp_title;			///< Original vector for overwriting
  bk_flags	bp_flags;			///< Flags
#define BK_PROCTITLE_OFF	1		///< Process title is disabled
};



extern char **environ;				/**< Some morons (e.g. linux) do not define the environment */



/**
 * @name Global NULL/zero values for macros to return when ``B'' is not defined
 * (should be const, but this is not allowed in this context)
 */
// @{
void *bk_nullptr = NULL;
int bk_zeroint = 0;
unsigned bk_zerouint = 0;
// @}



/**
 * Grand creation of libbk state structure.
 *
 *	@param argc Number of arguments to program's argv
 *	@param argv Pointer to @a argv from @a main() which will be modified/replicated
 *	@param envp Pointer to @a ergv from @a main() which will be modified/replicated
 *	@param configfile Location of configuration file to gather defaults from
 *	@param bcup Structure containing configuration file parameters (e.g. seperator character).  Typically NULL
 *	@param error_queue_length Number of messages to store in both high and low priority error queues
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on allocation or other failure
 *	@return <br><i>baka structure</i> on success
 */
bk_s bk_general_init(int argc, char ***argv, char ***envp, const char *configfile, struct bk_config_user_pref *bcup, int error_queue_length, int log_facility, bk_flags flags)
{
  bk_s B;
  char *program;

  if (!(B = bk_general_thread_init(NULL, "*MAIN*")))
    goto error;

  if (!BK_MALLOC(BK_BT_GENERAL(B)))
    goto error;
  BK_ZERO(BK_BT_GENERAL(B));

  BK_FLAG_SET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_FUNON);

  if (!(BK_GENERAL_DEBUG(B) = bk_debug_init(B, 0)))
    goto error;

  if (!(BK_GENERAL_ERROR(B) = bk_error_init(B, error_queue_length, NULL, log_facility, 0)))
    goto error;

  if (!(BK_GENERAL_DESTROY(B) = bk_funlist_init(B, 0)))
    goto error;

  if (!(BK_GENERAL_REINIT(B) = bk_funlist_init(B,0)))
    goto error;

  // Config files should not be required, generally
  BK_GENERAL_CONFIG(B) = bk_config_init(B, configfile, bcup, 0);

  if (!(BK_GENERAL_PROCTITLE(B) = bk_general_proctitle_init(B, argc, argv, envp, &program, 0)))
    goto error;

  BK_GENERAL_PROGRAM(B) = program;

#if !defined(_WIN32) || defined(__CYGWIN32__)
  if (log_facility && BK_GENERAL_PROGRAM(B))
  {
    openlog(BK_GENERAL_PROGRAM(B), LOG_NDELAY|LOG_PID, log_facility);
    BK_FLAG_SET(BK_GENERAL_FLAGS(B),BK_BGFLAGS_SYSLOGON);
  }
#endif /* !_WIN32 || __CYGWIN32__ */

  return(B);

 error:
  bk_general_destroy(B);
  return(NULL);
}



/**
 * Destroy libbk state (generally not a good idea :-)
 *
 *	@param B BAKA Thread/global state
 */
void bk_general_destroy(bk_s B)
{
  if (B)
  {
    if (BK_BT_GENERAL(B))
    {
      if (BK_GENERAL_DESTROY(B))
      {
	bk_funlist_call(B,BK_GENERAL_DESTROY(B), 0, 0);
	bk_funlist_destroy(B,BK_GENERAL_DESTROY(B));
      }

      if (BK_GENERAL_PROCTITLE(B))
	bk_general_proctitle_destroy(B,BK_GENERAL_PROCTITLE(B), 0);

      if (BK_GENERAL_CONFIG(B))
	bk_config_destroy(B, BK_GENERAL_CONFIG(B));

      if (BK_GENERAL_REINIT(B))
	bk_funlist_destroy(B,BK_GENERAL_REINIT(B));

      if (BK_GENERAL_ERROR(B))
	bk_error_destroy(B,BK_GENERAL_ERROR(B));

      if (BK_GENERAL_DEBUG(B))
	bk_debug_destroy(B,BK_GENERAL_DEBUG(B));

      free(BK_BT_GENERAL(B));
    }
    bk_general_thread_destroy(B);
  }

  return;
}



/**
 * Go through reinitialization of core BAKA routines and anyone else who has registered for reinit() services
 *
 *	@param B BAKA Thread/global information
 */
void bk_general_reinit(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (BK_GENERAL_REINIT(B))
    bk_funlist_call(B, BK_GENERAL_REINIT(B), 0, 0);

  BK_VRETURN(B);
}



/**
 * Add to the reinitialization database
 *
 *	@param B BAKA Thread/global information
 *	@param bf_fun Function to be called on reinit
 *	@param args Opaque argument to function
 *	@see bk_funlist_insert
 */
int bk_general_reinit_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_insert(B, BK_GENERAL_REINIT(B), bf_fun, args, 0));
}



/**
 * Remove from the reinitialization database
 *
 *	@param B BAKA Thread/global information
 *	@param bf_fun Function to be deleted
 *	@param args Argument to function to be deleted
 *	@see bk_funlist_delete
 */
int bk_general_reinit_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_delete(B, BK_GENERAL_REINIT(B), bf_fun, args, 0));
}



/**
 * Add to the destruction database
 *
 *	@param B BAKA Thread/global information
 *	@param bf_fun Function to be called on reinit
 *	@param args Opaque argument to function
 *	@see bk_funlist_insert
 */
int bk_general_destroy_insert(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_insert(B, BK_GENERAL_REINIT(B), bf_fun, args, 0));
}



/**
 * Remove from the reinitialization database
 *
 *	@param B BAKA Thread/global information
 *	@param bf_fun Function to be deleted
 *	@param args Argument to function to be deleted
 *	@see bk_funlist_delete
 */
int bk_general_destroy_delete(bk_s B, void (*bf_fun)(bk_s, void *, u_int), void *args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, bk_funlist_delete(B, BK_GENERAL_REINIT(B), bf_fun, args, 0));
}



/**
 * Set up the per-thread information
 *
 *	@param B BAKA Thread/global information
 *	@param name Name of thread
 *	@return <i>NULL</i> on call failure, allocation failure, other failure
 *	@return <br><i>Baka thread state</i> on success
 */
bk_s bk_general_thread_init(bk_s B, char *name)
{
  bk_s B1 = NULL;

  if (!name)
  {
    if (B)
      bk_error_printf(B, BK_ERR_WARN, "Invalid argument\n");
    return(NULL);
  }

  if (!BK_MALLOC(B1))
  {
    if (B)
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate new bk_thread information\n");
    return(NULL);
  }
  BK_ZERO(B1);

  if (!(BK_BT_FUNSTACK(B1) = bk_fun_init()))
    goto error;

  /* Preserve function tracing flag or turn on by default */
  if (B && BK_GENERAL_FLAG_ISFUNON(B))
    BK_FLAG_SET(BK_GENERAL_FLAGS(B1), BK_BGFLAGS_FUNON);

  if (!(BK_BT_THREADNAME(B1) = strdup(name)))
    goto error;

  if (B)
    BK_BT_GENERAL(B1) = BK_BT_GENERAL(B);

  return(B1);

 error:
  bk_general_thread_destroy(B1);
  return(NULL);
}



/**
 * Destroy per-thread information
 *
 *	@param B BAKA Thread/global state
 */
void bk_general_thread_destroy(bk_s B)
{
  if (B)
  {
    if (BK_BT_FUNSTACK(B))
      bk_fun_destroy(BK_BT_FUNSTACK(B));
    if (BK_BT_THREADNAME(B))
      free((char *)BK_BT_THREADNAME(B));

    /* Specifically do not muck with bt_general which is shared */

    free(B);
  }
}



/**
 * Set the process title (if possible)
 *
 *	@param B BAKA Thread/global state
 *	@param title The process title we want
 */
void bk_general_proctitle_set(bk_s B, char *title)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  size_t len, rest;

  if (!title || !B)
  {
    bk_error_printf(B,BK_ERR_ERR,"Invalid arguments\n");
    BK_VRETURN(B);
  }

  len = MIN(strlen(title),BK_GENERAL_PROCTITLE(B)->bp_title.len-1);
  rest = BK_GENERAL_PROCTITLE(B)->bp_title.len - len;

  memcpy(BK_GENERAL_PROCTITLE(B)->bp_title.ptr, title, len);
  memset((char *)BK_GENERAL_PROCTITLE(B)->bp_title.ptr + len, 0, rest);

  BK_VRETURN(B);
}



/**
 * Syslog a message out to the system (if we were initialized)
 *
 * Note that %m (strerror(errno)) is NOT supported
 *
 *	@param B BAKA Thread/global state
 *	@param level The syslog level we wish to use
 *	@param flags What additional data is logged
 *	@param format The printf-style format of the message
 *	@param ... The printf-style arguments
 *	@see bk_general_vsyslog
 */
void bk_general_syslog(bk_s B, int level, bk_flags flags, char *format, ...)
{
  va_list args;

  va_start(args, format);
  bk_general_vsyslog(B, level, flags, format, args);
  va_end(args);

  return;
}



/**
 * Syslog a message out to the system (if we were initialized)
 *
 * Note that %m (strerror(errno)) is NOT supported
 *
 *	@param B BAKA Thread/global state
 *	@param level The syslog level we wish to use
 *	@param flags What added information to syslog (BK_SYSLOG_FLAG_NOLEVEL--error level to character string not desired; BK_SYSLOG_FLAG_NOFUN--function name not desired)
 *	@param format The printf-style format of the message
 *	@param ... The printf-style arguments
 */
void bk_general_vsyslog(bk_s B, int level, bk_flags flags, char *format, va_list args)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *buffer;
  const char *parentname, *errorstr;
  int len;

  if (!BK_GENERAL_FLAG_ISSYSLOGON(B))
    BK_VRETURN(B);

  if (BK_FLAG_ISSET(flags, BK_SYSLOG_FLAG_NOLEVEL) || !(errorstr = bk_general_errorstr(B, level)))
    errorstr = "";

  if (BK_FLAG_ISSET(flags, BK_SYSLOG_FLAG_NOFUN) || !(parentname = bk_fun_funname(B, 1, 0)))
    parentname = "";

  if (!(buffer = (char *)alloca(BK_SYSLOG_MAXLEN)))
  {
    /*
     * Potentially recursive
     *
     * bk_error_printf(B, BK_ERR_ERR, "Could not allocate storage for buffer to syslog\n");
     */
    BK_VRETURN(B);
  }
  vsnprintf(buffer,BK_SYSLOG_MAXLEN, format, args);

#if !defined(_WIN32) || defined(__CYGWIN32__)
  if (*errorstr == 0 && *parentname == 0)
    syslog(level, "%s",buffer);
  else if (*errorstr == 0)
    syslog(level, "%s: %s",parentname,buffer);
  else
    syslog(level, "%s[%s]: %s",parentname,errorstr,buffer);
#endif /* !_WIN32 || __CYGWIN32__ */

  BK_VRETURN(B);
}



/**
 * Decode error level to error string
 *
 *	@param B BAKA Thread/state information
 *	@param level The baka log level to decode
 *	@return <i>Error level string</i> which was found ("Unknown" if not known)
 */
const char *bk_general_errorstr(bk_s B, int level)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *answer;

  switch(level)
  {
  case BK_ERR_CRIT:	answer = "Critical"; break;
  case BK_ERR_ERR:	answer = "Error"; break;
  case BK_ERR_WARN:	answer = "Warning"; break;
  case BK_ERR_NOTICE:	answer = "Notice"; break;
  case BK_ERR_DEBUG:	answer = "Debug"; break;
  case BK_ERR_NONE:	answer = ""; break;
  default:		answer = "Unknown"; break;
  }

  BK_RETURN(B, answer);
}



/**
 * Configure debugging on or off, and specify debugging destination.
 *
 *	@param B BAKA Thread/global state
 *	@param fh File handle to send debugging info to (NULL to disable)
 *	@param sysloglevel The system log level to log debugging info at (BK_ERR_NONE to disable)
 *	@param flags Fun for the future
 *	@return <i>-1</i> on call failure
 *	@return <br><i>0</i> on success
 */
int bk_general_debug_config(bk_s B, FILE *fh, int sysloglevel, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!B)
    BK_RETURN(B, -1);

  bk_debug_config(B, BK_GENERAL_DEBUG(B), fh, sysloglevel, flags);

  if (!fh && sysloglevel == BK_ERR_NONE)
  {						/* Turning off debugging */
    BK_FLAG_CLEAR(BK_GENERAL_FLAGS(B), BK_BGFLAGS_DEBUGON);
  }
  else
  {						/* Turning debugging on */
    BK_FLAG_SET(BK_GENERAL_FLAGS(B), BK_BGFLAGS_DEBUGON);
    bk_debug_setconfig(B, BK_GENERAL_DEBUG(B), BK_GENERAL_CONFIG(B), BK_GENERAL_PROGRAM(B));
    bk_fun_reset_debug(B, flags);
  }

  BK_RETURN(B,0);
}



/**
 * Initialize process title information & process name.
 *
 * Argv/envp is copied and original argv/envp is reset to new copies so that original memory can be reused
 * for process title stuff.  Also sets the program name.
 *
 *	@param B BAKA Thread/global state
 *	@param argc The number of argument to the program
 *	@param argv Pointer to @a argv from @a main() which will be modified/replicated
 *	@param envp Pointer to @a ergv from @a main() which will be modified/replicated
 *	@param program Copy-out program name
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on allocation failure
 *	@return <br><i>process title handle</i> on success
 *	@return <br>@a argv @a envp @a program as copy-out
 */
static struct bk_proctitle *bk_general_proctitle_init(bk_s B, int argc, char ***argv, char ***envp, char **program, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_proctitle *PT;

  if (!BK_MALLOC(PT))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate proctitle buffer: %s\n",strerror(errno));
    BK_RETURN(B, (struct bk_proctitle *)NULL);
  }
  BK_ZERO(PT);

  PT->bp_argc = argc;
  PT->bp_argv = argv?*argv:NULL;
  PT->bp_envp = envp?*envp:NULL;
  PT->bp_title.ptr = NULL;
  PT->bp_title.len = 0;
  PT->bp_flags = flags;

  if (!argv || !envp)
    BK_FLAG_SET(PT->bp_flags, BK_PROCTITLE_OFF);

  /*
   * If the OS can support this functionality, create new copy of argv&envp,
   * repoint original pointers to this location, and set up proctitle vector
   */
  if (BK_FLAG_ISCLEAR(flags, BK_PROCTITLE_OFF))
  {
    int envc = 0;

#define ARRAY_DUPLICATE(new,orig,size) \
    { \
      int tmp; \
      \
      if (!((new) = (char **)malloc(((size)+1)*sizeof(char *)))) \
      { \
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate duplicate array: %s\n",strerror(errno)); \
	goto error; \
      } \
      \
      for(tmp=0;tmp<size;tmp++) \
      { \
	if (!((new)[tmp] = strdup((orig)[tmp]))) \
	{ \
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate duplicate array entry %d: %s\n",tmp,strerror(errno)); \
	  goto error; \
	} \
      } \
      (new)[tmp] = NULL; \
    }

    /* Create duplicate argv */
    ARRAY_DUPLICATE(PT->bp_argv,*argv,argc);

    /* Spin through envp array looking for last element */
    for(envc = 0; (*envp)[envc]; envc++) ;

    /* Create duplicate envp */
    ARRAY_DUPLICATE(PT->bp_envp,*envp,envc);
    environ = PT->bp_envp;

    /* Figure out the usable vector for overwriting */
    PT->bp_title.ptr = **argv;

    /* If we have an environment--go to end of last element */
    if (envc > 0)
      PT->bp_title.len = ((*envp)[envc-1] + strlen((*envp)[envc-1])) - (char *)PT->bp_title.ptr;
    else
      PT->bp_title.len = (char *)(*envp) - (char *)PT->bp_title.ptr;

    PT->bp_title.len -= 2;			/* BSD wierdness... */
  }

  /* Discover and save program name if required and able */
  if (program)
  {
    char *tmp = NULL;

    if (PT->bp_argv)				/* Do we have info to find program name? */
    {
      if (tmp = strrchr(*PT->bp_argv,'/'))
	tmp++;
      else
	tmp = *PT->bp_argv;
    }

    *program = tmp;
  }

  BK_RETURN(B, PT);

 error:
  bk_general_proctitle_destroy(B, PT, 0);
  BK_RETURN(B, (struct bk_proctitle *)NULL);
}



/**
 * Destroy process title information
 *
 * N.B. Normally processes feel free to keep items like argv pointers
 * around for long periods of time--after all main() is not going to exit.
 * However, once you destroy, the (changed after proctitle_init) argv pointers
 * will be invalid.  Sigh.  Same with environment.
 *
 *	@param B BAKA Thread/global state
 *	@param PT Process title handle
 *	@param flags Fun for the future
 */
static void bk_general_proctitle_destroy(bk_s B, struct bk_proctitle *PT, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!PT)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISCLEAR(PT->bp_flags, BK_PROCTITLE_OFF))
  {
    int cntr;

    if (PT->bp_argv)
    {
      for(cntr=0;PT->bp_argv[cntr];cntr++)
	free(PT->bp_argv[cntr]);
      free(PT->bp_argv);
    }
    if (PT->bp_envp)
    {
      for(cntr=0;PT->bp_envp[cntr];cntr++)
	free(PT->bp_envp[cntr]);
      free(PT->bp_envp);
    }
  }
  free(PT);

  BK_VRETURN(B);
}
