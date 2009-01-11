/*
 * cl.h:  shared stuff in the command line program
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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
#include "svn_cmdline.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Option processing ***/

/* --accept actions */
typedef enum
{
  /* invalid accept action */
  svn_cl__accept_invalid = -2,

  /* unspecified accept action */
  svn_cl__accept_unspecified = -1,

  /* Leave conflicts alone, for later resolution. */
  svn_cl__accept_postpone,

  /* Resolve the conflict with the pre-conflict base file. */
  svn_cl__accept_base,

  /* Resolve the conflict with the current working file. */
  svn_cl__accept_working,

  /* Resolve the conflicted hunks by choosing the corresponding text
     from the pre-conflict working copy file.

     Note: this is a placeholder, not actually implemented in 1.5. */
  svn_cl__accept_mine_conflict,

  /* Resolve the conflicted hunks by choosing the corresponding text
     from the post-conflict base copy file.

     Note: this is a placeholder, not actually implemented in 1.5. */
  svn_cl__accept_theirs_conflict,

  /* Resolve the conflict by taking the entire pre-conflict working
     copy file. */
  svn_cl__accept_mine_full,

  /* Resolve the conflict by taking the entire post-conflict base file. */
  svn_cl__accept_theirs_full,

  /* Launch user's editor and resolve conflict with edited file. */
  svn_cl__accept_edit,

  /* Launch user's resolver and resolve conflict with edited file. */
  svn_cl__accept_launch

} svn_cl__accept_t;

/* --accept action user input words */
#define SVN_CL__ACCEPT_POSTPONE "postpone"
#define SVN_CL__ACCEPT_BASE "base"
#define SVN_CL__ACCEPT_WORKING "working"
#define SVN_CL__ACCEPT_MINE_CONFLICT "mine-conflict"
#define SVN_CL__ACCEPT_THEIRS_CONFLICT "theirs-conflict"
#define SVN_CL__ACCEPT_MINE_FULL "mine-full"
#define SVN_CL__ACCEPT_THEIRS_FULL "theirs-full"
#define SVN_CL__ACCEPT_EDIT "edit"
#define SVN_CL__ACCEPT_LAUNCH "launch"

/* Return the svn_cl__accept_t value corresponding to WORD, using exact
 * case-sensitive string comparison. Return svn_cl__accept_invalid if WORD
 * is empty or is not one of the known values. */
svn_cl__accept_t
svn_cl__accept_from_word(const char *word);


/*** Mergeinfo flavors. ***/

/* --show-revs values */
typedef enum {
  svn_cl__show_revs_invalid = -1,
  svn_cl__show_revs_merged,
  svn_cl__show_revs_eligible
} svn_cl__show_revs_t;

/* --show-revs user input words */
#define SVN_CL__SHOW_REVS_MERGED   "merged"
#define SVN_CL__SHOW_REVS_ELIGIBLE "eligible"

/* Return svn_cl__show_revs_t value corresponding to word. */
svn_cl__show_revs_t
svn_cl__show_revs_from_word(const char *word);


/*** Command dispatch. ***/

/* Hold results of option processing that are shared by multiple
   commands. */
typedef struct svn_cl__opt_state_t
{
  /* An array of svn_opt_revision_range_t *'s representing revisions
     ranges indicated on the command-line via the -r and -c options.
     For each range in the list, if only one revision was provided
     (-rN), its 'end' member remains `svn_opt_revision_unspecified'.

     NOTE: This is currently used only by merge subcommand. */
  apr_array_header_t *revision_ranges;

  /* These are simply a copy of the range start and end values present
     in the first item of the revision_ranges list. */
  svn_opt_revision_t start_revision;
  svn_opt_revision_t end_revision;

  /* Flag which is only set if the '-c' option was used. */
  svn_boolean_t used_change_arg;

  /* Flag which is only set if the '-r' option was used. */
  svn_boolean_t used_revision_arg;

  /* Max number of log messages to get back from svn_client_log2. */
  int limit;

  /* After option processing is done, reflects the switch actually
     given on the command line, or svn_depth_unknown if none. */
  svn_depth_t depth;

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
  svn_boolean_t record_only;     /* whether to record mergeinfo */
  const char *old_target;        /* diff target */
  const char *new_target;        /* diff target */
  svn_boolean_t relocate;        /* rewrite urls (svn switch) */
  const char *config_dir;        /* over-riding configuration directory */
  apr_array_header_t *config_options; /* over-riding configuration options */
  svn_boolean_t autoprops;       /* enable automatic properties */
  svn_boolean_t no_autoprops;    /* disable automatic properties */
  const char *native_eol;        /* override system standard eol marker */
  svn_boolean_t summarize;       /* create a summary of a diff */
  svn_boolean_t remove;          /* deassociate a changelist */
  apr_array_header_t *changelists; /* changelist filters */
  const char *changelist;        /* operate on this changelist
                                    THIS IS TEMPORARY (LAST OF CHANGELISTS) */
  svn_boolean_t keep_changelists;/* don't remove changelists after commit */
  svn_boolean_t keep_local;      /* delete path only from repository */
  svn_boolean_t all_revprops;    /* retrieve all revprops */
  svn_boolean_t no_revprops;     /* retrieve no revprops */
  apr_hash_t *revprop_table;     /* table of revision properties to get/set */
  svn_boolean_t parents;         /* create intermediate directories */
  svn_boolean_t use_merge_history; /* use/display extra merge information */
  svn_cl__accept_t accept_which; /* how to handle conflicts */
  svn_cl__show_revs_t show_revs; /* mergeinfo flavor */
  svn_depth_t set_depth;         /* new sticky ambient depth value */
  svn_boolean_t reintegrate;     /* use "reintegrate" merge-source heuristic */
  svn_boolean_t trust_server_cert; /* trust server SSL certs that would
                                      otherwise be rejected as "untrusted" */
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
  svn_cl__cat,
  svn_cl__changelist,
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
  svn_cl__mergeinfo,
  svn_cl__mkdir,
  svn_cl__move,
  svn_cl__propdel,
  svn_cl__propedit,
  svn_cl__propget,
  svn_cl__proplist,
  svn_cl__propset,
  svn_cl__revert,
  svn_cl__resolve,
  svn_cl__resolved,
  svn_cl__status,
  svn_cl__switch,
  svn_cl__unlock,
  svn_cl__update;


/* See definition in main.c for documentation. */
extern const svn_opt_subcommand_desc2_t svn_cl__cmd_table[];

/* See definition in main.c for documentation. */
extern const int svn_cl__global_options[];

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
svn_error_t *
svn_cl__check_cancel(void *baton);



/* Various conflict-resolution callbacks. */

typedef struct {
  svn_cl__accept_t accept_which;
  apr_hash_t *config;
  const char *editor_cmd;
  svn_boolean_t external_failed;
  svn_cmdline_prompt_baton_t *pb;
} svn_cl__conflict_baton_t;

/* Create and return a conflict baton, allocated from POOL, with the values
   ACCEPT_WHICH, CONFIG, EDITOR_CMD and PB placed in the same-named fields
   of the baton, and its 'external_failed' field initialised to FALSE. */
svn_cl__conflict_baton_t *
svn_cl__conflict_baton_make(svn_cl__accept_t accept_which,
                            apr_hash_t *config,
                            const char *editor_cmd,
                            svn_cmdline_prompt_baton_t *pb,
                            apr_pool_t *pool);

/* A conflict-resolution callback which prompts the user to choose
   one of the 3 fulltexts, edit the merged file on the spot, or just
   skip the conflict (to be resolved later).
   Implements @c svn_wc_conflict_resolver_func_t. */
svn_error_t *
svn_cl__conflict_handler(svn_wc_conflict_result_t **result,
                         const svn_wc_conflict_description_t *desc,
                         void *baton,
                         apr_pool_t *pool);



/*** Command-line output functions -- printing to the user. ***/

/* Print out commit information found in COMMIT_INFO to the console.
 * POOL is used for temporay allocations.
 * COMMIT_INFO should not be NULL.
 */
svn_error_t *
svn_cl__print_commit_info(svn_commit_info_t *commit_info, apr_pool_t *pool);


/* Convert the date in DATA to a human-readable UTF-8-encoded string
 * *HUMAN_CSTRING, or set the latter to "(invalid date)" if DATA is not
 * a valid date.  DATA should be as expected by svn_time_from_cstring().
 *
 * Do all allocations in POOL.
 */
svn_error_t *
svn_cl__time_cstring_to_human_cstring(const char **human_cstring,
                                      const char *data,
                                      apr_pool_t *pool);


/* Print STATUS for PATH to stdout for human consumption.  Prints in
   abbreviated format by default, or DETAILED format if flag is set.

   When DETAILED is set, use SHOW_LAST_COMMITTED to toggle display of
   the last-committed-revision and last-committed-author.

   If SKIP_UNRECOGNIZED is TRUE, this function will not print out
   unversioned items found in the working copy.

   When DETAILED is set, and REPOS_LOCKS is set, treat missing repository locks
   as broken WC locks. */
svn_error_t *
svn_cl__print_status(const char *path,
                     const svn_wc_status2_t *status,
                     svn_boolean_t detailed,
                     svn_boolean_t show_last_committed,
                     svn_boolean_t skip_unrecognized,
                     svn_boolean_t repos_locks,
                     apr_pool_t *pool);


/* Print STATUS for PATH in XML to stdout.  Use POOL for temporary
   allocations. */
svn_error_t *
svn_cl__print_status_xml(const char *path,
                         const svn_wc_status2_t *status,
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

/* Same as svn_cl__print_prop_hash(), only output xml to OUTSTR.  If OUTSTR is
   NULL, allocate it first from pool, otherwise append the xml to it. */
svn_error_t *
svn_cl__print_xml_prop_hash(svn_stringbuf_t **outstr,
                            apr_hash_t *prop_hash,
                            svn_boolean_t names_only,
                            apr_pool_t *pool);

/* Output a commit xml element to OUTSTR.  IF OUTSTR is NULL, allocate it
   first from pool, otherwise appen the xml to it.  If AUTHOR or DATE is
   NULL, it will be omitted. */
void
svn_cl__print_xml_commit(svn_stringbuf_t **outstr,
                         svn_revnum_t revision,
                         const char *author,
                         const char *date,
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
svn_cl__edit_string_externally(svn_string_t **edited_contents,
                               const char **tmpfile_left,
                               const char *editor_cmd,
                               const char *base_dir,
                               const svn_string_t *contents,
                               const char *prefix,
                               apr_hash_t *config,
                               svn_boolean_t as_text,
                               const char *encoding,
                               apr_pool_t *pool);


/* Search for a text editor command in standard environment variables,
   and invoke it to edit PATH.  Use POOL for all allocations.

   If EDITOR_CMD is not NULL, it is the name of the external editor
   command to use, overriding anything else that might determine the
   editor.

   CONFIG is a hash of svn_config_t * items keyed on a configuration
   category (SVN_CONFIG_CATEGORY_CONFIG et al), and may be NULL.  */
svn_error_t *
svn_cl__edit_file_externally(const char *path,
                             const char *editor_cmd,
                             apr_hash_t *config,
                             apr_pool_t *pool);

/* Search for a merge tool command in environment variables,
   and use it to perform the merge of the four given files.
   Use POOL for all allocations.

   CONFIG is a hash of svn_config_t * items keyed on a configuration
   category (SVN_CONFIG_CATEGORY_CONFIG et al), and may be NULL.
   */
svn_error_t *
svn_cl__merge_file_externally(const char *base_path,
                              const char *their_path,
                              const char *my_path,
                              const char *merged_path,
                              apr_hash_t *config,
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
svn_error_t *
svn_cl__make_log_msg_baton(void **baton,
                           svn_cl__opt_state_t *opt_state,
                           const char *base_dir,
                           apr_hash_t *config,
                           apr_pool_t *pool);

/* A function of type svn_client_get_commit_log3_t. */
svn_error_t *
svn_cl__get_log_message(const char **log_msg,
                        const char **tmp_file,
                        const apr_array_header_t *commit_items,
                        void *baton,
                        apr_pool_t *pool);

/* Handle the cleanup of a log message, using the data in the
   LOG_MSG_BATON, in the face of COMMIT_ERR.  This may mean removing a
   temporary file left by an external editor, or it may be a complete
   no-op.  COMMIT_ERR may be NULL to indicate to indicate that the
   function should act as though no commit error occurred. Use POOL
   for temporary allocations.

   All error returns from this function are guaranteed to at least
   include COMMIT_ERR, and perhaps additional errors attached to the
   end of COMMIT_ERR's chain.  */
svn_error_t *
svn_cl__cleanup_log_msg(void *log_msg_baton,
                        svn_error_t *commit_err,
                        apr_pool_t *pool);

/* Add a message about --force if appropriate */
svn_error_t *
svn_cl__may_need_force(svn_error_t *err);

/* Write the STRING to the stdio STREAM, returning an error if it fails. */
svn_error_t *
svn_cl__error_checked_fputs(const char *string, FILE* stream);

/* If STRING is non-null, append it, wrapped in a simple XML CDATA element
   named TAGNAME, to the string SB.  Use POOL for temporary allocations. */
void
svn_cl__xml_tagged_cdata(svn_stringbuf_t **sb,
                         apr_pool_t *pool,
                         const char *tagname,
                         const char *string);

/* Print the XML prolog and document root element start-tag to stdout, using
   TAGNAME as the root element name.  Use pool for temporary allocations. */
svn_error_t *
svn_cl__xml_print_header(const char *tagname, apr_pool_t *pool);

/* Print the XML document root element end-tag to stdout, using TAGNAME as the
   root element name.  Use pool for temporary allocations. */
svn_error_t *
svn_cl__xml_print_footer(const char *tagname, apr_pool_t *pool);


/* For use in XML output, return a non-localised string representation
 * of KIND, being "none" or "dir" or "file" or, in any other case,
 * the empty string. */
const char *
svn_cl__node_kind_str_xml(svn_node_kind_t kind);

/* Return a (possibly localised) string representation of KIND, being "none" or
   "dir" or "file" or, in any other case, the empty string. */
const char *
svn_cl__node_kind_str_human_readable(svn_node_kind_t kind);


/** Provides an XML name for a given OPERATION.
 * Note: POOL is currently not used.
 */
const char *
svn_cl__operation_str_xml(svn_wc_operation_t operation, apr_pool_t *pool);

/** Return a possibly localized human readable string for
 * a given OPERATION.
 * Note: POOL is currently not used.
 */
const char *
svn_cl__operation_str_human_readable(svn_wc_operation_t operation,
                                     apr_pool_t *pool);


/* If PROPNAME is one of the svn: properties with a boolean value, and
 * PROPVAL looks like an attempt to turn the property off (i.e., it's
 * "off", "no", "false", or ""), then print a warning to the user that
 * setting the property to this value might not do what they expect.
 * Perform temporary allocations in POOL.
 */
void
svn_cl__check_boolean_prop_val(const char *propname,
                               const char *propval,
                               apr_pool_t *pool);

/* De-streamifying wrapper around svn_client_get_changelists(), which
   is called for each target in TARGETS to populate *PATHS (a list of
   paths assigned to one of the CHANGELISTS. */
svn_error_t *
svn_cl__changelist_paths(apr_array_header_t **paths,
                         const apr_array_header_t *changelists,
                         const apr_array_header_t *targets,
                         svn_depth_t depth,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool);

svn_error_t *
svn_cl__args_to_target_array_print_reserved(apr_array_header_t **targets_p,
                                            apr_getopt_t *os,
                                            apr_array_header_t *known_targets,
                                            svn_client_ctx_t *ctx,
                                            apr_pool_t *pool);

/* Return a string allocated in POOL that is a copy of STR but with each
 * line prefixed with INDENT. A line is all characters up to the first
 * CR-LF, LF-CR, CR or LF, or the end of STR if sooner. */
const char *
svn_cl__indent_string(const char *str,
                      const char *indent,
                      apr_pool_t *pool);


/* Return a string showing NODE's kind, URL and revision, to the extent that
 * that information is available in NODE. */
const char *
svn_cl__node_description(const svn_wc_conflict_version_t *node,
                         apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CL_H */
