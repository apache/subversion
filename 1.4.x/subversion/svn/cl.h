/*
 * cl.h:  shared stuff in the command line program
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_auth.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Option processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum {
  svn_cl__ancestor_path_opt = SVN_OPT_FIRST_LONGOPT_ID,
  svn_cl__auth_password_opt,
  svn_cl__auth_username_opt,
  svn_cl__autoprops_opt,
  svn_cl__config_dir_opt,
  svn_cl__diff_cmd_opt,
  svn_cl__dry_run_opt,
  svn_cl__editor_cmd_opt,
  svn_cl__encoding_opt,
  svn_cl__force_log_opt,
  svn_cl__force_opt,
  svn_cl__ignore_ancestry_opt,
  svn_cl__ignore_externals_opt,
  svn_cl__incremental_opt,
  svn_cl__limit_opt,
  svn_cl__merge_cmd_opt,
  svn_cl__native_eol_opt,
  svn_cl__new_cmd_opt,
  svn_cl__no_auth_cache_opt,
  svn_cl__no_autoprops_opt,
  svn_cl__no_diff_deleted,
  svn_cl__no_ignore_opt,
  svn_cl__no_unlock_opt,
  svn_cl__non_interactive_opt,
  svn_cl__notice_ancestry_opt,
  svn_cl__old_cmd_opt,
  svn_cl__relocate_opt,
  svn_cl__revprop_opt,
  svn_cl__stop_on_copy_opt,
  svn_cl__strict_opt,
  svn_cl__summarize,
  svn_cl__targets_opt,
  svn_cl__version_opt,
  svn_cl__xml_opt
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

  /* Max number of log messages to get back from svn_client_log2. */
  int limit;

  /* Note: these next two flags only reflect switches given on the
     commandline.  For example, 'svn up' (with no options) will *not*
     set either of these flags, but will be recursive anyway */
  svn_boolean_t recursive, nonrecursive;

  /* Was --no-unlock specified? */
  svn_boolean_t no_unlock;

  const char *message;           /* log message */
  const char *ancestor_path;     /* ### todo: who sets this? */
  svn_boolean_t force;           /* be more forceful, as in "svn rm -f ..." */
  svn_boolean_t force_log;       /* force validity of a suspect log msg file */
  svn_boolean_t incremental;     /* yield output suitable for concatenation */
  svn_boolean_t quiet;           /* sssh...avoid unnecessary output */
  svn_boolean_t non_interactive; /* do no interactive prompting */
  svn_boolean_t version;         /* print version information */
  svn_boolean_t verbose;         /* be verbose */
  svn_boolean_t update;          /* contact the server for the full story */
  svn_boolean_t strict;          /* do strictly what was requested */
  svn_stringbuf_t *filedata;     /* contents of file used as option data */
  const char *encoding;          /* the locale/encoding of the data*/
  svn_boolean_t help;            /* print usage message */
  const char *auth_username;     /* auth username */ /* UTF-8! */
  const char *auth_password;     /* auth password */ /* UTF-8! */
  const char *extensions;        /* subprocess extension args */ /* UTF-8! */
  apr_array_header_t *targets;   /* target list from file */ /* UTF-8! */
  svn_boolean_t xml;             /* output in xml, e.g., "svn log --xml" */
  svn_boolean_t no_ignore;       /* disregard default ignores & svn:ignore's */
  svn_boolean_t no_auth_cache;   /* do not cache authentication information */
  svn_boolean_t no_diff_deleted; /* do not show diffs for deleted files */
  svn_boolean_t notice_ancestry; /* notice ancestry for diff-y operations */
  svn_boolean_t ignore_ancestry; /* ignore ancestry for merge-y operations */
  svn_boolean_t ignore_externals;/* ignore externals definitions */
  svn_boolean_t stop_on_copy;    /* don't cross copies during processing */
  svn_boolean_t dry_run;         /* try operation but make no changes */
  svn_boolean_t revprop;         /* operate on a revision property */
  const char *diff_cmd;          /* the external diff command to use */
  const char *merge_cmd;         /* the external merge command to use */
  const char *editor_cmd;        /* external editor command. */
  const char *old_target;        /* diff target */
  const char *new_target;        /* diff target */
  svn_boolean_t relocate;        /* rewrite urls (svn switch) */
  const char * config_dir;       /* over-riding configuration directory */
  svn_boolean_t autoprops;       /* enable automatic properties */
  svn_boolean_t no_autoprops;    /* disable automatic properties */
  const char *native_eol;        /* override system standard eol marker */
  svn_boolean_t summarize;       /* create a summary of a diff */
} svn_cl__opt_state_t;


typedef struct
{
  svn_cl__opt_state_t *opt_state;
  svn_client_ctx_t *ctx;
} svn_cl__cmd_baton_t;


/* Declare all the command procedures */
svn_opt_subcommand_t
  svn_cl__add,
  svn_cl__blame,
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
  svn_cl__lock,
  svn_cl__log,
  svn_cl__list,
  svn_cl__merge,
  svn_cl__mkdir,
  svn_cl__move,
  svn_cl__propdel,
  svn_cl__propedit,
  svn_cl__propget,
  svn_cl__proplist,
  svn_cl__propset,
  svn_cl__revert,
  svn_cl__resolved,
  svn_cl__status,
  svn_cl__switch,
  svn_cl__unlock,
  svn_cl__update,
  svn_cl__cat;


/* See definition in main.c for documentation. */
extern const svn_opt_subcommand_desc2_t svn_cl__cmd_table[];

/* See definition in main.c for documentation. */
extern const apr_getopt_option_t svn_cl__options[];


/* A helper for the many subcommands that wish to merely warn when
 * invoked on an unversioned, nonexistent, or otherwise innocuously
 * errorful resource.  Meant to be wrapped with SVN_ERR().
 * 
 * If ERR is null, return SVN_NO_ERROR, setting *SUCCESS to TRUE
 * if SUCCESS is not NULL.
 *
 * Else if ERR->apr_err is one of the error codes supplied in varargs,
 * then handle ERR as a warning (unless QUIET is true), clear ERR, and
 * return SVN_NO_ERROR, setting *SUCCESS to FALSE if SUCCESS is not
 * NULL.
 *
 * Else return ERR, setting *SUCCESS to FALSE if SUCCESS is not NULL.
 *
 * Typically, error codes like SVN_ERR_UNVERSIONED_RESOURCE,
 * SVN_ERR_ENTRY_NOT_FOUND, etc, are supplied in varargs.  Don't
 * forget to terminate the argument list with SVN_NO_ERROR.
 */
svn_error_t *
svn_cl__try(svn_error_t *err,
            svn_boolean_t *success,
            svn_boolean_t quiet,
            ...);


/* Our cancellation callback. */
svn_error_t *svn_cl__check_cancel(void *baton);



/*** Command-line output functions -- printing to the user. ***/

/* Print out commit information found in COMMIT_INFO to the console.
 * POOL is used for temporay allocations. */
svn_error_t *svn_cl__print_commit_info(svn_commit_info_t *commit_info,
                                       apr_pool_t *pool);


/* Print STATUS for PATH to stdout for human consumption.  Prints in
   abbreviated format by default, or DETAILED format if flag is set.

   When DETAILED is set, use SHOW_LAST_COMMITTED to toggle display of
   the last-committed-revision and last-committed-author.

   If SKIP_UNRECOGNIZED is TRUE, this function will not print out
   unversioned items found in the working copy.

   When DETAILED is set, and REPOS_LOCKS is set, treat missing repository locks
   as broken WC locks. */
svn_error_t *svn_cl__print_status(const char *path,
                                  svn_wc_status2_t *status,
                                  svn_boolean_t detailed,
                                  svn_boolean_t show_last_committed,
                                  svn_boolean_t skip_unrecognized,
                                  svn_boolean_t repos_locks,
                                  apr_pool_t *pool);


/* Print STATUS for PATH in XML to stdout.  Use POOL for temporary
   allocations. */
svn_error_t *
svn_cl__print_status_xml(const char *path,
                         svn_wc_status2_t *status,
                         apr_pool_t *pool);


/* Print a hash that maps property names (char *) to property values
   (svn_string_t *).  The names are assumed to be in UTF-8 format;
   the values are either in UTF-8 (the special Subversion props) or
   plain binary values.

   If NAMES_ONLY is true, print just names, else print names and
   values. */
svn_error_t *
svn_cl__print_prop_hash(apr_hash_t *prop_hash,
                        svn_boolean_t names_only,
                        apr_pool_t *pool);

/* Do the following things that are commonly required before accessing revision
   properties.  Ensure that REVISION is specified explicitly and is not
   relative to a working-copy item.  Ensure that exactly one target is
   specified in TARGETS.  Set *URL to the URL of the target.  Return an
   appropriate error if any of those checks or operations fail.
 */
svn_error_t *
svn_cl__revprop_prepare(const svn_opt_revision_t *revision,
                        apr_array_header_t *targets,
                        const char **URL,
                        apr_pool_t *pool);

/* Search for a text editor command in standard environment variables,
   and invoke it to edit CONTENTS (using a temporary file created in
   directory BASE_DIR).  Return the new contents in *EDITED_CONTENTS,
   or set *EDITED_CONTENTS to NULL if no edit was performed.

   If EDITOR_CMD is not NULL, it is the name of the external editor
   command to use, overriding anything else that might determine the
   editor.

   If TMPFILE_LEFT is NULL, the temporary file will be destroyed.
   Else, the file will be left on disk, and its path returned in
   *TMPFILE_LEFT.

   CONFIG is a hash of svn_config_t * items keyed on a configuration
   category (SVN_CONFIG_CATEGORY_CONFIG et al), and may be NULL.

   If AS_TEXT is TRUE, recode CONTENTS and convert to native eol-style before
   editing and back again afterwards.  In this case, ENCODING determines the
   encoding used during editing.  If non-NULL, use the named encoding, else
   use the system encoding.  If AS_TEXT is FALSE, don't do any translation.
   In that case, ENCODING is ignored.

   Use POOL for all allocations.  Use PREFIX as the prefix for the
   temporary file used by the editor.

   If return error, *EDITED_CONTENTS is not touched. */
svn_error_t *
svn_cl__edit_externally(svn_string_t **edited_contents,
                        const char **tmpfile_left,
                        const char *editor_cmd,
                        const char *base_dir,
                        const svn_string_t *contents,
                        const char *prefix,
                        apr_hash_t *config,
                        svn_boolean_t as_text,
                        const char *encoding,
                        apr_pool_t *pool);



/*** Notification functions to display results on the terminal. */

/* Set *NOTIFY_FUNC_P and *NOTIFY_BATON_P to a notifier/baton for all
 * operations, allocated in POOL.
 * 
 * If this is a checkout, set IS_CHECKOUT to true, so that the
 * notifier will print the appropriate summary line at the end of the
 * output.
 * 
 * If this is an export, set IS_EXPORT to true, so that the
 * notifier will print the appropriate summary line at the end of the
 * output.
 * 
 * If don't want a summary line at the end of notifications, set
 * SUPPRESS_FINAL_LINE.
 */
void svn_cl__get_notifier(svn_wc_notify_func2_t *notify_func_p,
                          void **notify_baton_p,
                          svn_boolean_t is_checkout,
                          svn_boolean_t is_export,
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
   
   CONFIG is a client configuration hash of svn_config_t * items keyed
   on config categories, and may be NULL.

   NOTE: While the baton itself will be allocated from POOL, the items
   add to it are added by reference, not duped into POOL!*/
svn_error_t *svn_cl__make_log_msg_baton(void **baton,
                                        svn_cl__opt_state_t *opt_state,
                                        const char *base_dir,
                                        apr_hash_t *config,
                                        apr_pool_t *pool);

/* A function of type svn_client_get_commit_log2_t. */
svn_error_t *svn_cl__get_log_message(const char **log_msg,
                                     const char **tmp_file,
                                     const apr_array_header_t *commit_items,
                                     void *baton,
                                     apr_pool_t *pool);

/* Handle the cleanup of a log message, using the data in the
   LOG_MSG_BATON, in the face of COMMIT_ERR.  This may mean removing a
   temporary file left by an external editor, or it may be a complete
   no-op.  COMMIT_ERR may be NULL to indicate to indicate that the
   function should act as though no commit error occurred.

   All error returns from this function are guaranteed to at least
   include COMMIT_ERR, and perhaps additional errors attached to the
   end of COMMIT_ERR's chain.  */
svn_error_t *svn_cl__cleanup_log_msg(void *log_msg_baton,
                                     svn_error_t *commit_err);

/* Add a message about --force if appropriate */
svn_error_t *svn_cl__may_need_force(svn_error_t *err);

/* Write the STRING to the stdio STREAM, returning an error if it fails. */
svn_error_t *svn_cl__error_checked_fputs(const char *string,
                                         FILE* stream);

/* If STRING is non-null, append it, wrapped in a simple XML CDATA element
   named TAGNAME, to the string SB.  Use POOL for temporary allocations. */
void svn_cl__xml_tagged_cdata(svn_stringbuf_t **sb,
                              apr_pool_t *pool,
                              const char *tagname,
                              const char *string);

/* Return a (non-localised) string representation of KIND, being "dir" or
   "file" or, in any other case, the empty string. */
const char *svn_cl__node_kind_str(svn_node_kind_t kind);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_H */
