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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


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
  svn_cl__auth_username_opt,
  svn_cl__auth_password_opt,
  svn_cl__targets_opt,
  svn_cl__xml_opt,
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

  const char *message;  /* log message */

  const char *xml_file;  /* F in "svn blah --xml-file F" */

  const char *target;  /* Target dir, T in "svn co -d T" */

  const char *ancestor_path;  /* ### todo: who sets this? */

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
  svn_stringbuf_t *filedata;
  svn_boolean_t help;
  const char *auth_username;
  const char *auth_password;
  const char *extensions;        /* for extension args to subprocesses */
  apr_array_header_t *targets;   /* when target list supplied from file */
  svn_boolean_t xml;             /* output in xml, e.g., "svn log --xml" */
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
  svn_cl__info,
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
  svn_cl__resolve,
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
   --targets.

   If EXTRACT_REVISIONS is set, then this function will attempt to
  look for trailing "@rev" syntax on the paths.   If one @rev is
  found, it will overwrite the value of opt_state->start_revision.  If
  a second one is found, it will overwrite opt_state->end_revision.
  (Extra revisions beyond that are ignored.)
  */
apr_array_header_t*
svn_cl__args_to_target_array (apr_getopt_t *os,
			      svn_cl__opt_state_t *opt_state,
                              svn_boolean_t extract_revisions,
                              apr_pool_t *pool);

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


/* Set OPT_STATE->start_revision and/or OPT_STATE->end_revision
 * according to ARG, where ARG is "N" or "N:M", like so:
 * 
 *    - If ARG is "N", set OPT_STATE->start_revision's kind to
 *      svn_client_revision_number and its value to the number N; and
 *      leave OPT_STATE->end_revision untouched.
 *
 *    - If ARG is "N:M", set OPT_STATE->start_revision's and
 *      OPT_STATE->end_revision's kinds to svn_client_revision_number
 *      and values to N and M respectively.
 * 
 * N and/or M may be one of the special revision descriptors
 * recognized by revision_from_word().
 *
 * If ARG is invalid, return TRUE; else return FALSE.
 * It is invalid to omit a revision (as in, ":", "N:" or ":M").
 *
 * Note:
 *
 * It is typical, though not required, for OPT_STATE->start_revision
 * and OPT_STATE->end_revision to be svn_client_revision_unspecified
 * kind on entry.
 */
svn_boolean_t svn_cl__parse_revision (svn_cl__opt_state_t *os,
                                      const char *arg,
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

/* Returns an editor that prints out events in an update or checkout.
   The IS_CHECKOUT boolean tells the editor what kind of final
   revision line to print;  the SUPPRESS_FINAL_LINE flag indicates
   whether to print the final revision line at all. */
svn_error_t *
svn_cl__get_trace_update_editor (const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 const char *initial_path,
                                 svn_boolean_t is_checkout,
                                 svn_boolean_t suppress_final_line,
                                 apr_pool_t *pool);

/* Returns an editor that prints out events in a commit. */
svn_error_t *
svn_cl__get_trace_commit_editor (const svn_delta_editor_t **editor,
                                 void **edit_baton,
                                 const char *initial_path,
                                 apr_pool_t *pool);


/* Search for a text editor command in standard environment variables,
   and invoke it to edit CONTENTS (using a temporary file created in
   directory BASE_DIR).  Return the new contents in EDITED_CONTENTS,
   or set EDITED_CONTENTS to NULL if no edit was performed.  Use POOL
   for all allocations. */
svn_error_t *
svn_cl__edit_externally (const char **edited_contents,
                         const char *base_dir,
                         const char *contents,
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_H */


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */

