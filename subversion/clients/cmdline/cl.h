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
  svn_cl__recursive_opt,
  svn_cl__force_opt,
  svn_cl__locale_opt,
  svn_cl__version_opt,
  /* Here are authentication args: */
  svn_cl__auth_username_opt,
  svn_cl__auth_password_opt,
  svn_cl__targets_opt,
} svn_cl__longopt_t;



/*** Command dispatch. ***/

/* Hold results of option processing that are shared by multiple
   commands. */
typedef struct svn_cl__opt_state_t
{
  /* These get set as a result of revisions or dates being specified.
     When only one revision is given, it's start_revision, and
     end_revision remains `svn_client_revision_unspecified'. */
  svn_client_revision_t start_revision, end_revision;

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
  apr_array_header_t *args;
  /* TODO fixme. This still doesn't handle binary data from a file! */
  svn_stringbuf_t *filedata;
  svn_boolean_t help;
  /* Here begin authentication args;  add more as needed. */
  svn_stringbuf_t *auth_username;
  svn_stringbuf_t *auth_password;
  /* Extension arguments (eg arguments to be passed into GNU diff) */
  svn_stringbuf_t *extensions;
  /* Targets supplied from a file with --targets */
  apr_array_header_t *targets;
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
  svn_cl__merge,
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


/* Print a generic (non-command-specific) usage message. */
void
svn_cl__print_generic_help (apr_pool_t *pool, FILE *stream);


/* Print out commit information found in COMMIT_INFO to the console. */
void
svn_cl__print_commit_info (svn_client_commit_info_t *commit_info);


/*** Miscellaneous utility commands ***/

/* Look up CODE in OPTION_TABLE.   If any option in the table has this
   enum code, return a pointer to the option.  Else return NULL. */
const apr_getopt_option_t *
svn_cl__get_option_from_enum (int code,
                              const apr_getopt_option_t *option_table);


void svn_cl__push_svn_string (apr_array_header_t *array,
                              const char *str,
                              apr_pool_t *pool);

/* Subcommands call this to pull any args left into the array of targets.
   This includes any extra args passed in the file specified by
   --targets.  */
apr_array_header_t*
svn_cl__args_to_target_array (apr_getopt_t *os,
			      svn_cl__opt_state_t *opt_state,
                              apr_pool_t *pool);

/* Splits a list of whitespace-separated values into an apr_array_header_t */
apr_array_header_t*
svn_cl__stringlist_to_array (svn_stringbuf_t *buffer, apr_pool_t *pool);

/* Splits a list of newline seperated values into an apr_array_header_t */
apr_array_header_t*
svn_cl__newlinelist_to_array (svn_stringbuf_t *buffer, apr_pool_t *pool);

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

/* Print out the property names in a hash that maps property names (char *) 
   to property values (svn_stringbuf_t *). */
void svn_cl__print_prop_names (apr_hash_t *prop_hash, apr_pool_t *pool);

/* Returns an editor that prints out events in an update or checkout. */
svn_error_t *
svn_cl__get_trace_update_editor (const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 svn_stringbuf_t *initial_path,
                                 apr_pool_t *pool);

/* Returns an editor that prints out events in a commit. */
svn_error_t *
svn_cl__get_trace_commit_editor (const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 svn_stringbuf_t *initial_path,
                                 apr_pool_t *pool);


/* Search for a text editor command in standard environment variables,
   and invoke it to edit CONTENTS (using a temporary file based on
   working copy directory BASE_DIR).  Return the new contents in
   EDITED_CONTENTS, or set EDITED_CONTENTS to NULL if no edit was
   performed.  Use POOL for all allocations. */
svn_error_t *
svn_cl__edit_externally (svn_stringbuf_t **edited_contents,
                         svn_stringbuf_t *base_dir,
                         const svn_string_t *contents,
                         apr_pool_t *pool);


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


/* Fills in the first four characters of STR_STATUS with status code
   characters, based on TEXT_STATUS, PROP_STATUS, LOCKED, and COPIED.*/
void
svn_cl__generate_status_codes (char *str_status,
                               enum svn_wc_status_kind text_status,
                               enum svn_wc_status_kind prop_status,
                               svn_boolean_t locked,
                               svn_boolean_t copied);


/*** Notification functions to display results on the terminal. */

void svn_cl__notify_func (void *baton, 
                          svn_wc_notify_action_t action,
                          const char *path);
void *svn_cl__make_notify_baton (apr_pool_t *pool);

/* This macro is used to specify a notification function, or NULL if the
   user has requested "quiet" mode. */
#define SVN_CL_NOTIFY(opt_state) \
		((opt_state)->quiet ? NULL : svn_cl__notify_func)



#endif /* SVN_CL_H */

/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */

