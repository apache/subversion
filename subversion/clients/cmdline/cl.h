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


/*** Option processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum {
  svn_cl__xml_file_opt = 256,
  svn_cl__ancestor_path_opt,
  svn_cl__force_opt,
  svn_cl__recursive_opt,
  svn_cl__locale_opt,
  svn_cl__version_opt,
  /* Here are authentication args: */
  svn_cl__auth_username_opt,
  svn_cl__auth_password_opt,
} svn_cl__longopt_t;


/*** Command dispatch. ***/

/* Hold results of option processing that are shared by multiple
   commands. */
typedef struct svn_cl__opt_state_t
{
  /* You probably want start_revision to default SVN_INVALID_REVNUM
     (which means `head' to the RA layer), and end_revision to default
     to 0 or 1, which are the two possibilities for oldest revision. */
  svn_revnum_t start_revision; /* X in "svn blah -r X" or "svn blah -r X:Y" */
  svn_revnum_t end_revision;   /* Y in "svn blah -r X:Y" */

  /* These should default to 0. */
  apr_time_t start_date;  /* X in "svn blah -D X" or "svn blah -D X:Y" */
  apr_time_t end_date;    /* Y in "svn blah -D X:Y" */

  svn_stringbuf_t *message;  /* log message */

  svn_stringbuf_t *xml_file;  /* F in "svn blah --xml-file F" */

  svn_stringbuf_t *target;  /* Target dir, T in "svn co -d T" */

  svn_stringbuf_t *ancestor_path;  /* ### todo: who sets this? */

  svn_boolean_t force;  /* Be more forceful, as in "svn rm -f ..." */

  /* Note: these next two flags only reflect switches given on the
     commandline.  For example, 'svn up' (with no options) will *not*
     set either of these flags, but will be recursive anyway */
  svn_boolean_t recursive;
  svn_boolean_t nonrecursive;

  svn_boolean_t quiet;
  svn_boolean_t version;
  svn_boolean_t verbose;
  svn_boolean_t very_verbose;
  svn_boolean_t update;
  svn_boolean_t modified;
  apr_array_header_t *args;
  /* TODO fixme. This still doesn't handle binary data from a file! */
  svn_stringbuf_t *filedata;
  svn_boolean_t help;
  /* Here begin authentication args;  add more as needed. */
  svn_stringbuf_t *auth_username;
  svn_stringbuf_t *auth_password;
  /* Extension arguments (eg arguments to be passed into GNU diff) */
  svn_stringbuf_t *extensions;
} svn_cl__opt_state_t;


/* All client command procedures conform to this prototype.  OPT_STATE
 * likewise should hold the result of processing the options.  OS is a
 * list of filenames and directories, a-la CVS (which really only
 * becomes useful if you pass it into svn_cl__args_to_target_array()
 * to convert OS to an APR arra of svn_stringbuf_t * targets).
 *
 * TARGETS is normalized by main before being passed to any command
 * (with the exception of svn_cl__help, which will oftentime be passed
 * an empty array of targets. That is, all duplicates are removed, and
 * all paths are made relative to the working copy root directory). */
typedef svn_error_t *(svn_cl__cmd_proc_t) (apr_getopt_t *os,
                                           svn_cl__opt_state_t *opt_state,
                                           apr_pool_t *pool);


/* One element of the command dispatch table. */
typedef struct svn_cl__cmd_desc_t
{
  /* The name of this command.  Might be a full name, such as
     "commit", or a short name, such as "ci". */
  const char *name;

  /* If name is a short synonym, such as "ci", then is_alias
     is set `TRUE'.  If it is the base command entry, then `FALSE'.
     The alias entries will always immediately follow the base entry. */
  svn_boolean_t is_alias;

  /* The function this command invokes.  NULL if alias. */
  svn_cl__cmd_proc_t *cmd_func;

  /* A brief string describing this command, for usage messages. */
  const char *help;

} svn_cl__cmd_desc_t;


/* Declare all the command procedures */
svn_cl__cmd_proc_t
  svn_cl__add,
  svn_cl__checkout,
  svn_cl__cleanup,
  svn_cl__commit,
  svn_cl__copy,
  svn_cl__delete,
  svn_cl__diff,
  svn_cl__help,
  svn_cl__import,
  svn_cl__log,
  svn_cl__mkdir,
  svn_cl__move,
  svn_cl__propdel,
  svn_cl__propedit,
  svn_cl__propget,
  svn_cl__proplist,
  svn_cl__propset,
  svn_cl__revert,
  svn_cl__status,
  svn_cl__switch,
  svn_cl__update;


/* make the command table information available to all commands */ 
extern const svn_cl__cmd_desc_t svn_cl__cmd_table[];


/* Return the canonical command table entry for CMD (which may be the
 * entry for CMD itself, or some other entry if CMD is an alias).
 * If CMD is not found, return null.
 */
const svn_cl__cmd_desc_t *
svn_cl__get_canonical_command (const char *cmd);

/* Print out commit information found in COMMIT_INFO to the console. */
void
svn_cl__print_commit_info (svn_client_commit_info_t *commit_info);


/*** Miscellaneous utility commands ***/
void svn_cl__push_svn_string (apr_array_header_t *array,
                              const char *str,
                              apr_pool_t *pool);

/* subcommands call this to pull any args left into the array of targets. */
apr_array_header_t*
svn_cl__args_to_target_array (apr_getopt_t *os,
                              apr_pool_t *pool);

/* Splits a list of whitespace-separated values into an apr_array_header_t */
apr_array_header_t*
svn_cl__stringlist_to_array (svn_stringbuf_t *buffer, apr_pool_t *pool);

void svn_cl__push_implicit_dot_target (apr_array_header_t *targets,
                                       apr_pool_t *pool);

svn_error_t *
svn_cl__parse_num_args (apr_getopt_t *os,
                        svn_cl__opt_state_t *opt_state,
                        const char *subcommand,
                        int num_args,
                        apr_pool_t *pool);

svn_error_t *
svn_cl__parse_all_args (apr_getopt_t *os,
                        svn_cl__opt_state_t *opt_state,
                        const char *subcommand,
                        apr_pool_t *pool);

void
svn_cl__subcommand_help (const char *subcommand,
                         apr_pool_t *pool);


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
   (svn_stringbuf_t *). */
void svn_cl__print_prop_hash (apr_hash_t *prop_hash, apr_pool_t *pool);

/* Returns an editor that prints out events in an update or checkout. */
svn_error_t *
svn_cl__get_trace_update_editor (const svn_delta_edit_fns_t **editor,
                                 void **edit_baton,
                                 svn_stringbuf_t *initial_path,
                                 apr_pool_t *pool);

/* Returns an editor that prints out events in a commit. */
svn_error_t *
svn_cl__get_trace_commit_editor (const svn_delta_edit_fns_t **editor,
                                 void **edit_baton,
                                 svn_stringbuf_t *initial_path,
                                 apr_pool_t *pool);


/* Initialize the top-pool's feedback vtable to contain functions
   specific to the command-line client. */
void svn_cl__init_feedback_vtable (apr_pool_t *top_pool);


/* Our implementation of the 'auth info callback' routine, 
   as defined in svn_client.h.   This callback is passed to any
   libsvn_client routine that needs to authenticate against a
   repository. */

/* Display PROMPT to the user, and read a reply back from stdin,
   allocated in POOL and returned in *RESULT.  If HIDE is set, the
   reply will not be echoed to the screen.  BATON is ignored (but
   required by the definition of svn_client_auth_info_callback_t.) */
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



#endif /* SVN_CL_H */

/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */

