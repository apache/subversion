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
  svn_cl__msg_encoding_opt,
  svn_cl__version_opt,
  svn_cl__auth_username_opt,
  svn_cl__auth_password_opt,
  svn_cl__targets_opt,
  svn_cl__xml_opt,
  svn_cl__strict_opt
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

  /* Note: these next two flags only reflect switches given on the
     commandline.  For example, 'svn up' (with no options) will *not*
     set either of these flags, but will be recursive anyway */
  svn_boolean_t recursive, nonrecursive;

  const char *message;           /* log message */
  const char *xml_file;          /* xml source/target file */ /* UTF-8! */
  const char *ancestor_path;     /* ### todo: who sets this? */
  svn_boolean_t force;           /* be more forceful, as in "svn rm -f ..." */
  svn_boolean_t quiet;           /* sssh...avoid unnecessary output */
  svn_boolean_t version;         /* print version information */
  svn_boolean_t verbose;         /* be verbose */
  svn_boolean_t very_verbose;    /* be obnoxious, or at least really verbose */
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
  svn_cl__export,
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

/* Pull remaining target arguments from OS into *TARGETS_P, including
   targets stored in OPT_STATE->targets (that is, passed via the
   "--targets" command line option), converting them to UTF-8.
   Allocate *TARGETS_P and its elements in POOL.

   If EXTRACT_REVISIONS is set, then this function will attempt to
   look for trailing "@rev" syntax on the paths.  If one @rev is
   found, it will overwrite the value of OPT_STATE->start_revision.
   If a second one is found, it will overwrite OPT_STATE->end_revision.  
   (Extra revisions beyond that are ignored.)  */
svn_error_t *
svn_cl__args_to_target_array (apr_array_header_t **targets_p,
                              apr_getopt_t *os,
			      svn_cl__opt_state_t *opt_state,
                              svn_boolean_t extract_revisions,
                              apr_pool_t *pool);


/* If no targets exist in *TARGETS, add `.' as the lone target.
 *
 * (Some commands take an implicit "." string argument when invoked
 * with no arguments. Those commands make use of this function to
 * add "." to the target array if the user passes no args.)
 */
void svn_cl__push_implicit_dot_target (apr_array_header_t *targets,
                                       apr_pool_t *pool);


/* Parse NUM_ARGS non-target arguments from the list of arguments in
   OS->argv, return them as `const char *' in *ARGS_P, without doing
   any UTF-8 conversion.  Allocate *ARGS_P and its values in POOL. */
svn_error_t *
svn_cl__parse_num_args (apr_array_header_t **args_p,
                        apr_getopt_t *os,
                        int num_args,
                        apr_pool_t *pool);


/* Parse all remaining arguments from OS->argv, return them as
   `const char *' in *ARGS_P, without doing any UTF-8 conversion.
   Allocate *ARGS_P and its values in POOL. */
svn_error_t *
svn_cl__parse_all_args (apr_array_header_t **args_p,
                        apr_getopt_t *os,
                        apr_pool_t *pool);


/* Print the usage message for SUBCOMMAND. */
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
   (svn_stringbuf_t *).  The names are assumed to be in UTF-8 format;
   the values are either in UTF-8 (the special Subversion props) or
   plain binary values.  */
svn_error_t *
svn_cl__print_prop_hash (apr_hash_t *prop_hash, apr_pool_t *pool);

/* Print out the property names in a hash that maps property names (char *) 
   to property values (svn_stringbuf_t *).  The names are assumed to
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_H */


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */

