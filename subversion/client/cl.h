/*
 * cl.h:  shared stuff in the command line program
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



#ifndef SVN_CL_H
#define SVN_CL_H

/*** Includes. ***/
#include <apr_tables.h>
#include <apr_getopt.h>

#include "svn_wc.h"
#include "svn_string.h"


/*** Option processing ***/

/* Add an identifier here for long options that don't have a short
   option. Options that have both long and short options should just
   use the short option letter as identifier.  */
typedef enum {
  svn_cl__xml_file_opt = 256,
  svn_cl__ancestor_path_opt,
  svn_cl__force_opt
} svn_cl__longopt_t;


/*** Command dispatch. ***/

/* Hold results of option processing that are shared by multiple
   commands. */
typedef struct svn_cl__opt_state_t
{
  svn_revnum_t revision;
  svn_string_t *message;
  svn_string_t *xml_file;
  svn_string_t *target;
  svn_string_t *ancestor_path;
  svn_boolean_t force;
  svn_boolean_t version;
  apr_array_header_t *args;
  /* TODO fixme. This still doesn't handly binary data from a file! */
  svn_string_t *filedata;
  svn_boolean_t help;
} svn_cl__opt_state_t;


/* All client command procedures conform to this prototype.
 * OPT_STATE likewise should hold the result of processing the options.
 * TARGETS is an apr array of filenames and directories, a-la CVS.
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
  svn_cl__unadd,
  svn_cl__commit,
  svn_cl__checkout,
  svn_cl__delete,
  svn_cl__undelete,
  svn_cl__help,
  svn_cl__import,
  svn_cl__proplist,
  svn_cl__propget,
  svn_cl__propset,
  svn_cl__status,
  svn_cl__diff,
  svn_cl__update;


/* make the command table information available to all commands */ 
extern const svn_cl__cmd_desc_t svn_cl__cmd_table[];

const svn_cl__cmd_desc_t *
svn_cl__get_canonical_command (const char *cmd);


/*** Miscellaneous utility commands ***/
void svn_cl__push_svn_string (apr_array_header_t *array,
                              const char *str,
                              apr_pool_t *pool);

/* subcommands call this to pull any args left into the array of targets. */
apr_array_header_t*
svn_cl__args_to_target_array (apr_getopt_t *os,
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


/*** Command-line output functions -- printing to the user. ***/

/* Print PATH's status line using STATUS. */
void svn_cl__print_status (svn_string_t *path, svn_wc_status_t *status);

/* Print a hash that maps names to status-structs to stdout for human
   consumption. */
void svn_cl__print_status_list (apr_hash_t *statushash, apr_pool_t *pool);

/* Print a hash that maps property names (char *) to property values
   (svn_string_t *). */
void svn_cl__print_prop_hash (apr_hash_t *prop_hash, apr_pool_t *pool);

/* Print a context diff showing local changes made to PATH */
svn_error_t *svn_cl__print_file_diff (svn_string_t *path, apr_pool_t *pool);

/* Returns an editor that prints out events in an update or checkout. */
svn_error_t *
svn_cl__get_trace_update_editor (const svn_delta_edit_fns_t **editor,
                                 void **edit_baton,
                                 svn_string_t *initial_path,
                                 apr_pool_t *pool);

/* Returns an editor that prints out events in a commit. */
svn_error_t *
svn_cl__get_trace_commit_editor (const svn_delta_edit_fns_t **editor,
                                 void **edit_baton,
                                 svn_string_t *initial_path,
                                 apr_pool_t *pool);


#endif /* SVN_CL_H */

/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
