/*
 * svn_opt.h :  option and argument parsing for Subversion command lines
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


#ifndef SVN_OPTS_H
#define SVN_OPTS_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_getopt.h>

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* All subcommand procedures in Subversion conform to this prototype.
 * (### todo: svnlook and svnadmin not yet converted, as of 2002-09-26.) 
 *
 * OS is the apr option state after getopt processing has been run; in
 * other words, it still contains the non-option arguments following
 * the subcommand.  See OS->argv and OS->ind.
 *
 * BATON is anything you need it to be.
 * 
 * POOL is used for allocating errors, and for any other allocation
 * unless the instance is explicitly documented to allocate from a
 * pool in BATON.
 */
typedef svn_error_t *(svn_opt_subcommand_t)
       (apr_getopt_t *os, void *baton, apr_pool_t *pool);


/* The maximum number of aliases a subcommand can have. */
#define SVN_OPT_MAX_ALIASES 3

/* The maximum number of options that can be accepted by a subcommand. */
#define SVN_OPT_MAX_OPTIONS 50

/* Options that have no short option char should use an identifying
   integer equal to or greater than this. */
#define SVN_OPT_FIRST_LONGOPT_ID 256


/* One element of a subcommand dispatch table. */
typedef struct svn_opt_subcommand_desc_t
{
  /* The full name of this command. */
  const char *name;

  /* The function this command invokes. */
  svn_opt_subcommand_t *cmd_func;

  /* A list of alias names for this command (e.g., 'up' for 'update'). */
  const char *aliases[SVN_OPT_MAX_ALIASES];

  /* A brief string describing this command, for usage messages. */
  const char *help;

  /* A list of options accepted by this command.  Each value in the
     array is a unique enum (the 2nd field in apr_getopt_option_t) */
  int valid_options[SVN_OPT_MAX_OPTIONS];

} svn_opt_subcommand_desc_t;


/* Return the entry in TABLE whose name matches CMD_NAME, or NULL if none.
   CMD_NAME may be an alias. */  
const svn_opt_subcommand_desc_t *
svn_opt_get_canonical_subcommand (const svn_opt_subcommand_desc_t *table,
                                  const char *cmd_name);


/* Return the first entry from OPTION_TABLE whose option code is CODE,
   or null if no match.  OPTION_TABLE must end with an element whose
   every field is zero. */
const apr_getopt_option_t *
svn_opt_get_option_from_code (int code,
                              const apr_getopt_option_t *option_table);


/* Return TRUE iff subcommand COMMAND supports option OPTION_CODE,
   else return FALSE. */
svn_boolean_t
svn_opt_subcommand_takes_option (const svn_opt_subcommand_desc_t *command,
                                 int option_code);


/* Print a generic (not command-specific) usage message to STREAM.
 * (### todo: why is STREAM a stdio file instead of an svn stream?)
 *
 * If HEADER is non-null, print HEADER followed by a newline.  Then
 * loop over CMD_TABLE printing the usage for each command (getting
 * option usages from OPT_TABLE).  Then if FOOTER is non-null, print
 * FOOTER followed by a newline.
 *
 * Use POOL for temporary allocation.
 */
void
svn_opt_print_generic_help (const char *header,
                            const svn_opt_subcommand_desc_t *cmd_table,
                            const apr_getopt_option_t *opt_table,
                            const char *footer,
                            apr_pool_t *pool,
                            FILE *stream);


/* Print an option OPT nicely into a STRING allocated in POOL.  If DOC
   is set, include the generic documentation string of option.*/
void
svn_opt_format_option (const char **string,
                       const apr_getopt_option_t *opt,
                       svn_boolean_t doc,
                       apr_pool_t *pool);



/* Get SUBCOMMAND's usage from TABLE, and print it to stdout.  Obtain
   option usage from OPTIONS_TABLE.  Use POOL for temporary
   allocation.  SUBCOMMAND may be a canonical command name or an
   alias.  (### todo: why does this only print to stdout, whereas
   svn_opt_print_generic_help gives us a choice?) */
void
svn_opt_subcommand_help (const char *subcommand, 
                         const svn_opt_subcommand_desc_t *table,
                         const apr_getopt_option_t *options_table,
                         apr_pool_t *pool);



/*** Parsing revision and date options. ***/

/* Various ways of specifying revisions. 
 *   
 * Note:
 * In contexts where local mods are relevant, the `working' kind
 * refers to the uncommitted "working" revision, which may be modified
 * with respect to its base revision.  In other contexts, `working'
 * should behave the same as `committed' or `current'.
 */
enum svn_opt_revision_kind {
  svn_opt_revision_unspecified,   /* No revision information given. */
  svn_opt_revision_number,        /* revision given as number */
  svn_opt_revision_date,          /* revision given as date */
  svn_opt_revision_committed,     /* rev of most recent change */
  svn_opt_revision_previous,      /* (rev of most recent change) - 1 */
  svn_opt_revision_base,          /* .svn/entries current revision */
  svn_opt_revision_working,       /* current, plus local mods */
  svn_opt_revision_head           /* repository youngest */
};


/* A revision, specified in one of `svn_opt_revision_kind' ways. */
typedef struct svn_opt_revision_t {
  enum svn_opt_revision_kind kind;
  union {
    svn_revnum_t number;
    apr_time_t date;
  } value;
} svn_opt_revision_t;


/* Set *START_REVISION and/or *END_REVISION according to ARG, where
 * ARG is "N" or "N:M", like so:
 * 
 *    - If ARG is "N", set *START_REVISION's kind to
 *      svn_opt_revision_number and its value to the number N; and
 *      leave *END_REVISION untouched.
 *
 *    - If ARG is "N:M", set *START_REVISION's and *END_REVISION's
 *      kinds to svn_opt_revision_number and values to N and M
 *      respectively. 
 * 
 * N and/or M may be one of the special revision descriptors
 * recognized by revision_from_word().
 *
 * If ARG is invalid, return -1; else return 0.
 * It is invalid to omit a revision (as in, ":", "N:" or ":M").
 *
 * Note:
 *
 * It is typical, though not required, for *START_REVISION and
 * *END_REVISION to be svn_opt_revision_unspecified kind on entry.
 */
int svn_opt_parse_revision (svn_opt_revision_t *start_revision,
                            svn_opt_revision_t *end_revision,
                            const char *arg,
                            apr_pool_t *pool);



/*** Parsing arguments. ***/

/* Pull remaining target arguments from OS into *TARGETS_P, including
   targets stored in KNOWN_TARGETS (which might come from, for
   example, the "--targets" command line option), converting them to
   UTF-8.  Allocate *TARGETS_P and its elements in POOL.

   If EXTRACT_REVISIONS is set, then this function will attempt to
   look for trailing "@rev" syntax on the paths.  If one @rev is
   found, it will overwrite the value of *START_REVISION.  If a second
   one is found, it will overwrite *END_REVISION.  (Extra revisions
   beyond that are ignored.)  */
svn_error_t *
svn_opt_args_to_target_array (apr_array_header_t **targets_p,
                              apr_getopt_t *os,
                              apr_array_header_t *known_targets,
                              svn_opt_revision_t *start_revision,
                              svn_opt_revision_t *end_revision,
                              svn_boolean_t extract_revisions,
                              apr_pool_t *pool);


/* If no targets exist in *TARGETS, add `.' as the lone target.
 *
 * (Some commands take an implicit "." string argument when invoked
 * with no arguments. Those commands make use of this function to
 * add "." to the target array if the user passes no args.)
 */
void svn_opt_push_implicit_dot_target (apr_array_header_t *targets,
                                       apr_pool_t *pool);


/* Parse NUM_ARGS non-target arguments from the list of arguments in
   OS->argv, return them as `const char *' in *ARGS_P, without doing
   any UTF-8 conversion.  Allocate *ARGS_P and its values in POOL. */
svn_error_t *
svn_opt_parse_num_args (apr_array_header_t **args_p,
                        apr_getopt_t *os,
                        int num_args,
                        apr_pool_t *pool);


/* Parse all remaining arguments from OS->argv, return them as
   `const char *' in *ARGS_P, without doing any UTF-8 conversion.
   Allocate *ARGS_P and its values in POOL. */
svn_error_t *
svn_opt_parse_all_args (apr_array_header_t **args_p,
                        apr_getopt_t *os,
                        apr_pool_t *pool);


/* Print either generic help, or command-specific help for PGM_NAME.
 * If there are arguments in OS, then try printing help for them as
 * though they are subcommands, using  CMD_TABLE and OPTION_TABLE for
 * option information.
 *
 * If OS is null, or there are no targets in OS, then:
 *
 *    - If PRINT_VERSION is true, then print version info, in brief
 *      form if QUIET is also true; if QUIET is false, then if
 *      VERSION_FOOTER is non-null, print it following the version
 *      information.
 *
 *    - Else if PRINT_VERSION is not true, then print generic help,
 *      via svn_opt_print_generic_help with the HEADER, CMD_TABLE,
 *      OPTION_TABLE, and FOOTER arguments.
 *
 * Use POOL for temporary allocations.
 *
 * Notes: The reason this function handles both version printing and
 * general usage help is that a confused user might put both the
 * --version flag *and* subcommand arguments on a help command line.
 * The logic for handling such a situation should be in one place.
 */
svn_error_t *
svn_opt_print_help (apr_getopt_t *os,
                    const char *pgm_name,
                    svn_boolean_t print_version,
                    svn_boolean_t quiet,
                    const char *version_footer,
                    const char *header,
                    const svn_opt_subcommand_desc_t *cmd_table,
                    const apr_getopt_option_t *option_table,
                    const char *footer,
                    apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_OPTS_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
