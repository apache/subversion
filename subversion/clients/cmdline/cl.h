/*
 * cl.h:  shared stuff in the command line program
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



#ifndef SVN_CL_H
#define SVN_CL_H

/*** Includes. ***/
#include <apr_tables.h>
#include <apr_getopt.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_opt.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Option processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum {
  svn_cl__ancestor_path_opt = SVN_OPT_FIRST_LONGOPT_ID,
  svn_cl__force_opt,
  svn_cl__msg_encoding_opt,
  svn_cl__version_opt,
  svn_cl__auth_username_opt,
  svn_cl__auth_password_opt,
  svn_cl__targets_opt,
  svn_cl__incremental_opt,
  svn_cl__xml_opt,
  svn_cl__strict_opt,
  svn_cl__no_ignore_opt,
  svn_cl__no_auth_cache_opt,
  svn_cl__non_interactive_opt
} svn_cl__longopt_t;



/*** Command dispatch. ***/

/* Hold results of option processing that are shared by multiple
   commands. */
typedef struct svn_cl__opt_state_t
{
  /* These get set as a result of revisions or dates being specified.
     When only one revision is given, it's start_revision, and
     end_revision remains `svn_opt_revision_unspecified'. */
  svn_opt_revision_t start_revision, end_revision;

  /* Note: these next two flags only reflect switches given on the
     commandline.  For example, 'svn up' (with no options) will *not*
     set either of these flags, but will be recursive anyway */
  svn_boolean_t recursive, nonrecursive;

  const char *message;           /* log message */
  const char *ancestor_path;     /* ### todo: who sets this? */
  svn_boolean_t force;           /* be more forceful, as in "svn rm -f ..." */
  svn_boolean_t incremental;     /* yield output suitable for concatenation */
  svn_boolean_t quiet;           /* sssh...avoid unnecessary output */
  svn_boolean_t non_interactive; /* do no interactive prompting */
  svn_boolean_t version;         /* print version information */
  svn_boolean_t verbose;         /* be verbose */
  svn_boolean_t update;          /* contact the server for the full story */
  svn_boolean_t strict;          /* do strictly what was requested */
  svn_stringbuf_t *filedata;     /* contents of file used as option data */
  const char *filedata_encoding; /* the locale/encoding of the filedata*/
  svn_boolean_t help;            /* print usage message */
  const char *auth_username;     /* auth username */ /* UTF-8! */
  const char *auth_password;     /* auth password */ /* UTF-8! */
  const char *extensions;        /* subprocess extension args */ /* UTF-8! */
  apr_array_header_t *targets;   /* target list from file */ /* UTF-8! */
  svn_boolean_t xml;             /* output in xml, e.g., "svn log --xml" */
  svn_boolean_t no_ignore;       /* disregard default ignores & svn:ignore's */
  svn_boolean_t no_auth_cache;   /* do not cache authentication information */
} svn_cl__opt_state_t;


/* Declare all the command procedures */
svn_opt_subcommand_t
  svn_cl__add,
  svn_cl__checkout,
  svn_cl__cleanup,
  svn_cl__commit,
  svn_cl__copy,
  svn_cl__delete,
  svn_cl__diff,
  svn_cl__export,
  svn_cl__help,
  svn_cl__import,
  svn_cl__info,
  svn_cl__log,
  svn_cl__ls,
  svn_cl__merge,
  svn_cl__mkdir,
  svn_cl__move,
  svn_cl__propdel,
  svn_cl__propedit,
  svn_cl__propget,
  svn_cl__proplist,
  svn_cl__propset,
  svn_cl__revert,
  svn_cl__resolve,
  svn_cl__status,
  svn_cl__switch,
  svn_cl__update;


/* See definition in main.c for documentation. */
extern const svn_opt_subcommand_desc_t svn_cl__cmd_table[];

/* See definition in main.c for documentation. */
extern const apr_getopt_option_t svn_cl__options[];

/* Header and footer text for svn_opt_print_generic_help. */
extern const char svn_cl__help_header[];
extern const char svn_cl__help_footer[];


/* Print out commit information found in COMMIT_INFO to the console. */
void
svn_cl__print_commit_info (svn_client_commit_info_t *commit_info);



/*** Command-line output functions -- printing to the user. ***/

/* Print a hash that maps (char *) names to (svn_wc_status_t *)
   structs to stdout for human consumption.  Prints in abbreviated
   format by default, or DETAILED format if flag is set.

   When DETAILED is set, use SHOW_LAST_COMMITTED to toggle display of
   the last-committed-revision and last-committed-author.  Also, print
   YOUNGEST as the youngest revision in the repository, unless it is
   SVN_INVALID_REVNUM, in which case don't print anything about the
   youngest revision.

   If SKIP_UNRECOGNIZED is TRUE, this function will not print out
   unversioned items found in the working copy. */
void svn_cl__print_status_list (apr_hash_t *statushash,
                                svn_revnum_t youngest,
                                svn_boolean_t detailed,
                                svn_boolean_t show_last_committed,
                                svn_boolean_t skip_unrecognized,
                                apr_pool_t *pool);

/* Print a hash that maps property names (char *) to property values
   (svn_string_t *).  The names are assumed to be in UTF-8 format;
   the values are either in UTF-8 (the special Subversion props) or
   plain binary values.  */
svn_error_t *
svn_cl__print_prop_hash (apr_hash_t *prop_hash, apr_pool_t *pool);

/* Print out the property names in a hash that maps property names (char *) 
   to property values (svn_string_t *).  The names are assumed to
   be in UTF-8 format.  */
svn_error_t *
svn_cl__print_prop_names (apr_hash_t *prop_hash, apr_pool_t *pool);

/* Search for a text editor command in standard environment variables,
   and invoke it to edit CONTENTS (using a temporary file created in
   directory BASE_DIR).  Return the new contents in *EDITED_CONTENTS,
   or set *EDITED_CONTENTS to NULL if no edit was performed.  Use POOL
   for all allocations. 

   If return error, *EDITED_CONTENTS is not touched. */
svn_error_t *
svn_cl__edit_externally (const char **edited_contents,
                         const char *base_dir,
                         const char *contents,
                         apr_pool_t *pool);


/* Our implementation of the 'auth info callback' routine, 
   as defined in svn_client.h.   This callback is passed to any
   libsvn_client routine that needs to authenticate against a
   repository. */

/* This implements the `svn_client_prompt_t' interface.

   Display PROMPT to the user, and read a reply back from stdin,
   allocated in POOL and returned in *RESULT.  If HIDE is set, the
   reply will not be echoed to the screen.  BATON is ignored (but
   required by the definition of svn_client_prompt_t.)  */
svn_error_t *
svn_cl__prompt_user (char **result,
                     const char *prompt,
                     svn_boolean_t hide,
                     void *baton,
                     apr_pool_t *pool);

/* Helper for subcommands: given parsed OPT_STATE arguments from the
   command-line, put auth info into a structure to pass to libsvn_client. */
svn_client_auth_baton_t *
svn_cl__make_auth_baton (svn_cl__opt_state_t *opt_state,
                         apr_pool_t *pool);


/*** Notification functions to display results on the terminal. */

/* Set *NOTIFY_FUNC_P and *NOTIFY_BATON_P to a notifier/baton for all
 * operations, allocated in POOL.
 * 
 * If this is a checkout, set IS_CHECKOUT to true, so that the
 * notifier will print the appropriate summary line at the end of the
 * output.
 * 
 * If don't want a summary line at the end of notifications, set
 * SUPPRESS_FINAL_LINE.
 */
void svn_cl__get_notifier (svn_wc_notify_func_t *notify_func_p,
                           void **notify_baton_p,
                           svn_boolean_t is_checkout,
                           svn_boolean_t suppress_final_line,
                           apr_pool_t *pool);


/*** Log message callback stuffs. ***/

/* Allocate in POOL a baton for use with svn_cl__get_log_message().

   OPT_STATE is the set of command-line options given.  

   BASE_DIR is a directory in which to create temporary files if an
   external editor is used to edit the log message.  If BASE_DIR is
   NULL, the current working directory (`.') will be used, and
   therefore the user must have the proper permissions on that
   directory.  ### todo: What *should* happen in the NULL case is that
   we ask APR to tell us where a suitable tmp directory is (like, /tmp
   on Unix and C:\Windows\Temp on Win32 or something), and use it.
   But APR doesn't yet have that capability.
   
   NOTE: While the baton itself will be allocated from POOL, the items
   add to it are added by reference, not duped into POOL!*/
void *svn_cl__make_log_msg_baton (svn_cl__opt_state_t *opt_state,
                                  const char *base_dir,
                                  apr_pool_t *pool);

/* A function of type svn_client_get_commit_log_t. */
svn_error_t *svn_cl__get_log_message (const char **log_msg,
                                      apr_array_header_t *commit_items,
                                      void *baton,
                                      apr_pool_t *pool);

/* Add a message about --force if appropriate */
svn_error_t *svn_cl__may_need_force (svn_error_t *err);


/*** Argument processing. ***/

/* Given some TARGET from the command line, set *URL to an associated url.
   If TARGET is already a url, set *URL to TARGET.  If TARGET is a
   versioned item, set *URL to TARGET's entry's URL.  If TARGET is
   unversioned (has no entry), set *URL to NULL. */
svn_error_t *
svn_cl__get_url_from_target (const char **URL,
                             const char *target,
                             apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_H */


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */

