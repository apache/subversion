/*
 * main.c: Subversion server administration tool.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#include <apr_file_io.h>

#include "svn_error.h"
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svn_path.h"
#include "svn_config.h"

#include "svnadmin.h"


/*** Code. ***/

/* Helper to open stdio streams */

static svn_error_t *
create_stdio_stream (svn_stream_t **stream,
                     APR_DECLARE(apr_status_t) open_fn (apr_file_t **, 
                                                        apr_pool_t *),
                     apr_pool_t *pool)
{
  apr_file_t *stdio_file;

  apr_status_t apr_err = open_fn (&stdio_file, pool);  
  if (apr_err)
    return svn_error_create (apr_err, NULL,
                             "error opening stdio file");
  
  *stream = svn_stream_from_aprfile (stdio_file, pool);
  return SVN_NO_ERROR;   
}



/** Subcommands. **/

static svn_opt_subcommand_t
  subcommand_create,
  subcommand_dump,
  subcommand_help,
  subcommand_load,
  subcommand_lscr,
  subcommand_lstxns,
  subcommand_list_dblogs,
  subcommand_list_unused_dblogs,
  subcommand_recover,
  subcommand_rmtxns,
  subcommand_setlog,
  subcommand_verify;

enum 
  { 
    svnadmin__version = SVN_OPT_FIRST_LONGOPT_ID,
    svnadmin__incremental,
    svnadmin__follow_copies,
    svnadmin__ignore_uuid,
    svnadmin__force_uuid,
    svnadmin__parent_dir,
    svnadmin__bdb_txn_nosync,
    svnadmin__config_dir
  };

/* Option codes and descriptions.
 *
 * This must not have more than SVN_OPT_MAX_OPTIONS entries; if you
 * need more, increase that limit first. 
 *
 * The entire list must be terminated with an entry of nulls.
 */
static const apr_getopt_option_t options_table[] =
  {
    {"help",          'h', 0,
     "show help on a subcommand"},

    {NULL,            '?', 0,
     "show help on a subcommand"},

    {"version",       svnadmin__version, 0,
     "show version information"},

    {"revision",      'r', 1,
     "specify revision number ARG (or X:Y range)"},

    {"incremental",   svnadmin__incremental, 0,
     "dump incrementally"},

    {"copies",   svnadmin__follow_copies, 0,
     "follow copy history"},

    {"quiet",           'q', 0,
     "no progress (only errors) to stderr"},

    {"ignore-uuid", svnadmin__ignore_uuid, 0,
     "ignore any repos UUID found in the stream"},

    {"force-uuid", svnadmin__force_uuid, 0,
     "set repos UUID to that found in stream, if any"},

    {"parent-dir", svnadmin__parent_dir, 1,
     "load at specified directory in repository"},

    {SVN_FS_CONFIG_BDB_TXN_NOSYNC, svnadmin__bdb_txn_nosync, 0,
     "disable fsync at transaction commit [Berkeley DB]"},

    {"config-dir", svnadmin__config_dir, 1,
     "read user configuration files from directory ARG"},

    {NULL}
  };


/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc_t cmd_table[] =
  {
    {"create", subcommand_create, {0},
     "usage: svnadmin create REPOS_PATH\n\n"
     "Create a new, empty repository at REPOS_PATH.\n",
     {svnadmin__bdb_txn_nosync, svnadmin__config_dir} },
    
    {"dump", subcommand_dump, {0},
     "usage: svnadmin dump REPOS_PATH [-r LOWER[:UPPER]] [--incremental]\n\n"
     "Dump the contents of filesystem to stdout in a 'dumpfile'\n"
     "portable format, sending feedback to stderr.  Dump revisions\n"
     "LOWER rev through UPPER rev.  If no revisions are given, dump all\n"
     "revision trees.  If only LOWER is given, dump that one revision tree.\n"
     "If --incremental is passed, then the first revision dumped will be\n"
     "a diff against the previous revision, instead of the usual fulltext.\n",
     {'r', svnadmin__incremental, 'q'} },

    {"help", subcommand_help, {"?", "h"},
     "usage: svnadmin help [SUBCOMMAND...]\n\n"
     "Display this usage message.\n",
     {svnadmin__version} },

    {"load", subcommand_load, {0},
     "usage: svnadmin load REPOS_PATH\n\n"
     "Read a 'dumpfile'-formatted stream from stdin, committing\n"
     "new revisions into the repository's filesystem.  If the repository\n"
     "was previously empty, its UUID will, by default, be changed to the\n"
     "one specified in the stream.  Progress feedback is sent to stdout.\n",
     {svnadmin__ignore_uuid, svnadmin__force_uuid, svnadmin__parent_dir} },

    {"lscr", subcommand_lscr, {0},
     "usage: svnadmin lscr REPOS_PATH PATH [--copies]\n\n"
     "Print, one-per-line and youngest-to-eldest, the revisions in\n"
     "which PATH was modified.  Use the COPIES flag to allow this\n"
     "operation to cross copy history while searching for revisions.\n"
     "(For directories, this is, for now, almost guaranteed to be\n"
     "uninteresting.  Also, PATH must exist in the HEAD of the\n"
     "repository.)\n",
     {svnadmin__follow_copies} },

    {"list-dblogs", subcommand_list_dblogs, {0},
     "usage: svnadmin list-dblogs REPOS_PATH\n\n"
     "List all Berkeley DB log files.\n\n"
     "WARNING: Modifying or deleting logfiles which are still in use\n"
     "will cause your repository to be corrupted.\n",
     {0} },

    {"list-unused-dblogs", subcommand_list_unused_dblogs, {0},
     "usage: svnadmin list-unused-dblogs REPOS_PATH\n\n"
     "List unused Berkeley DB log files.\n\n",
     {0} },

    {"lstxns", subcommand_lstxns, {0},
     "usage: svnadmin lstxns REPOS_PATH\n\n"
     "Print the names of all uncommitted transactions.\n",
     {0} },

    {"recover", subcommand_recover, {0},
     "usage: svnadmin recover REPOS_PATH\n\n"
     "Run the Berkeley DB recovery procedure on a repository.  Do\n"
     "this if you've been getting errors indicating that recovery\n"
     "ought to be run.\n\n"
     "WARNING: only run this when you are SURE you're the only process\n"
     "accessing the repository.  Requires exclusive access.\n\n",
     {0} },

    {"rmtxns", subcommand_rmtxns, {0},
     "usage: svnadmin rmtxns REPOS_PATH TXN_NAME...\n\n"
     "Delete the named transaction(s).\n",
     {0} },

    {"setlog", subcommand_setlog, {0},
     "usage: svnadmin setlog REPOS_PATH -r REVISION FILE\n\n"
     "Set the log-message on revision REVISION to the contents of FILE.\n"
     "(Note that revision properties are not historied, so this command\n"
     "will permanently overwrite the previous log message.)\n",
     {'r'} },

    {"verify", subcommand_verify, {0},
     "usage: svnadmin verify REPOS_PATH\n\n"
     "Verifies the data stored in the repository.\n",
     {0} },

    { NULL, NULL, {0}, NULL, {0} }
  };


/* Baton for passing option/argument state to a subcommand function. */
struct svnadmin_opt_state
{
  const char *repository_path;
  svn_opt_revision_t start_revision, end_revision;  /* -r X[:Y] */
  svn_boolean_t help;                               /* --help or -? */
  svn_boolean_t version;                            /* --version */
  svn_boolean_t incremental;                        /* --incremental */
  svn_boolean_t follow_copies;                      /* --copies */
  svn_boolean_t quiet;                              /* --quiet */
  svn_boolean_t bdb_txn_nosync;                     /* --bdb-txn-nosync */
  enum svn_repos_load_uuid uuid_action;             /* --ignore-uuid,
                                                       --force-uuid */
  const char *parent_dir;

  const char *config_dir;    /* Overriding Configuration Directory */
};


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_create (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  apr_hash_t *config;
  apr_hash_t *fs_config = NULL;

  if (opt_state->bdb_txn_nosync)
    {
      fs_config = apr_hash_make (pool);
      apr_hash_set (fs_config, SVN_FS_CONFIG_BDB_TXN_NOSYNC,
                    APR_HASH_KEY_STRING, "1");
    }

  SVN_ERR (svn_config_get_config (&config, opt_state->config_dir, pool));
  SVN_ERR (svn_repos_create (&repos, opt_state->repository_path,
                             NULL, NULL, 
                             config, fs_config, pool));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_dump (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_stream_t *stdout_stream, *stderr_stream = NULL;
  svn_revnum_t
    lower = SVN_INVALID_REVNUM,
    upper = SVN_INVALID_REVNUM;

  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);

  /* ### We only handle revision numbers right now, not dates. */
  if (opt_state->start_revision.kind == svn_opt_revision_number)
    lower = opt_state->start_revision.value.number;
  else if (opt_state->start_revision.kind == svn_opt_revision_head)
    svn_fs_youngest_rev (&lower, fs, pool);
  else
    lower = SVN_INVALID_REVNUM;

  if (opt_state->end_revision.kind == svn_opt_revision_number)
    upper = opt_state->end_revision.value.number;
  else if (opt_state->end_revision.kind == svn_opt_revision_head)
    svn_fs_youngest_rev (&upper, fs, pool);
  else
    upper = SVN_INVALID_REVNUM;

  /* Fill in implied revisions if necessary. */
  if (lower == SVN_INVALID_REVNUM)
    {
      lower = 0;
      svn_fs_youngest_rev (&upper, fs, pool);
    }
  else if (upper == SVN_INVALID_REVNUM)
    upper = lower;
        
  if (lower > upper)
    return svn_error_createf
      (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
       "first revision cannot be higher than second");

  /* Run the dump to STDOUT.  Let the user redirect output into
     a file if they want.  :-)  */
  SVN_ERR (create_stdio_stream (&stdout_stream, apr_file_open_stdout, pool));

  /* Progress feedback goes to stderr, unless they asked to suppress
     it. */
  if (! opt_state->quiet)
    SVN_ERR (create_stdio_stream (&stderr_stream,
                                  apr_file_open_stderr, pool));

  SVN_ERR (svn_repos_dump_fs (repos, stdout_stream, stderr_stream,
                              lower, upper, opt_state->incremental, pool));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_help (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  const char *header =
    "general usage: svnadmin SUBCOMMAND REPOS_PATH  [ARGS & OPTIONS ...]\n"
    "Type \"svnadmin help <subcommand>\" for help on a specific subcommand.\n"
    "\n"
    "Available subcommands:\n";

  SVN_ERR (svn_opt_print_help (os, "svnadmin", 
                               opt_state ? opt_state->version : FALSE,
                               FALSE, NULL,
                               header, cmd_table, options_table, NULL,
                               pool));
  
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_load (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_stream_t *stdin_stream, *stdout_stream;

  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  
  /* Read the stream from STDIN.  Users can redirect a file. */
  SVN_ERR (create_stdio_stream (&stdin_stream,
                                apr_file_open_stdin, pool));
  
  /* Have the parser dump feedback to STDOUT. */
  SVN_ERR (create_stdio_stream (&stdout_stream,
                                apr_file_open_stdout, pool));
  
  SVN_ERR (svn_repos_load_fs (repos, stdin_stream, stdout_stream,
                              opt_state->uuid_action, opt_state->parent_dir,
                              pool));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_lscr (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_revnum_t youngest_rev;
  apr_array_header_t *revs, *args;
  const char *path_utf8;
  int i;

  SVN_ERR (svn_opt_parse_all_args (&args, os, pool));
  
  if (args->nelts != 1)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              "exactly one path argument required");

  SVN_ERR (svn_utf_cstring_to_utf8 (&path_utf8,
                                    APR_ARRAY_IDX (args, 0, const char *),
                                    pool));
  path_utf8 = svn_path_internal_style (path_utf8, pool);
  
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);
  svn_fs_youngest_rev (&youngest_rev, fs, pool);
  SVN_ERR (svn_repos_revisions_changed (&revs, fs, path_utf8, youngest_rev, 0,
                                        opt_state->follow_copies, pool));
  for (i = 0; i < revs->nelts; i++)
    {
      svn_revnum_t this_rev = ((svn_revnum_t *)revs->elts)[i];
      printf ("%" SVN_REVNUM_T_FMT "\n", this_rev);
    }

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_lstxns (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  apr_array_header_t *txns;
  int i;
  
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);
  SVN_ERR (svn_fs_list_transactions (&txns, fs, pool));
  
  /* Loop, printing revisions. */
  for (i = 0; i < txns->nelts; i++)
    {
      printf ("%s\n", APR_ARRAY_IDX (txns, i, const char *));
    }
  
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_recover (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_revnum_t youngest_rev;
  svn_repos_t *repos;
  struct svnadmin_opt_state *opt_state = baton;

  printf ("Acquiring exclusive lock on repository db.\n"
          "Recovery is running, please stand by...");
  fflush (stdout);

  SVN_ERR (svn_repos_recover (opt_state->repository_path, pool));

  printf ("\nRecovery completed.\n");

  /* Since db transactions may have been replayed, it's nice to tell
     people what the latest revision is.  It also proves that the
     recovery actually worked. */
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  SVN_ERR (svn_fs_youngest_rev (&youngest_rev, svn_repos_fs (repos), pool));
  printf ("The latest repos revision is %"
          SVN_REVNUM_T_FMT ".\n", youngest_rev);

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
list_dblogs (apr_getopt_t *os, void *baton, svn_boolean_t only_unused, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  apr_array_header_t *logfiles;
  int i;
  
  SVN_ERR (svn_repos_db_logfiles (&logfiles,
                                  opt_state->repository_path,
                                  only_unused,
                                  pool));
  
  /* Loop, printing log files.  We append the log paths to the
     repository path, making sure to return everything to the native
     style and encoding before printing. */
  for (i = 0; i < logfiles->nelts; i++)
    {
      const char *log_utf8, *log_native;
      log_utf8 = svn_path_join (opt_state->repository_path,
                                APR_ARRAY_IDX (logfiles, i, const char *),
                                pool);
      SVN_ERR (svn_utf_cstring_from_utf8 (&log_native, log_utf8, pool));
      printf ("%s\n", svn_path_local_style (log_native, pool));
    }
  
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_list_dblogs (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  SVN_ERR (list_dblogs (os, baton, FALSE, pool));
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_list_unused_dblogs (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  SVN_ERR (list_dblogs (os, baton, TRUE, pool));
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_rmtxns (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  apr_array_header_t *args;
  int i;
  apr_pool_t *subpool = svn_pool_create (pool);
  
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);
  
  SVN_ERR (svn_opt_parse_all_args (&args, os, pool));

  /* All the rest of the arguments are transaction names. */
  for (i = 0; i < args->nelts; i++)
    {
      const char *txn_name_utf8;
      SVN_ERR (svn_utf_cstring_to_utf8 (&txn_name_utf8, 
                                        APR_ARRAY_IDX (args, i, const char *),
                                        subpool));
      SVN_ERR (svn_fs_open_txn (&txn, fs, txn_name_utf8, subpool));
      SVN_ERR (svn_fs_abort_txn (txn));
      svn_pool_clear (subpool);
    }

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_setlog (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_stringbuf_t *file_contents;
  const char *filename_utf8;
  apr_array_header_t *args;
  svn_string_t *log_contents = svn_string_create ("", pool);

  if (opt_state->start_revision.kind != svn_opt_revision_number)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              "missing revision");
  else if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              "only one revision allowed");
    
  SVN_ERR (svn_opt_parse_all_args (&args, os, pool));

  if (args->nelts != 1)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              "exactly one file argument required");
  
  SVN_ERR (svn_utf_cstring_to_utf8 (&filename_utf8,
                                    APR_ARRAY_IDX (args, 0, const char *),
                                    pool));
  filename_utf8 = svn_path_internal_style (filename_utf8, pool);
  SVN_ERR (svn_stringbuf_from_file (&file_contents, filename_utf8, pool)); 

  log_contents->data = file_contents->data;
  log_contents->len = file_contents->len;

  SVN_ERR (svn_subst_translate_string (&log_contents, log_contents,
                                       NULL, pool));

  /* open the filesystem  */
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);
  
  /* set the revision property */
  SVN_ERR (svn_fs_change_rev_prop (fs, opt_state->start_revision.value.number,
                                   SVN_PROP_REVISION_LOG,
                                   log_contents, pool));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_verify (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_stream_t *stderr_stream = NULL;
  svn_revnum_t youngest;

  /* This whole process is basically just a dump of the repository
     with no interest in the output. */
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  SVN_ERR (svn_fs_youngest_rev (&youngest, svn_repos_fs (repos), pool));
  SVN_ERR (create_stdio_stream (&stderr_stream, apr_file_open_stderr, pool));
  SVN_ERR (svn_repos_dump_fs (repos, NULL, stderr_stream, 
                              0, youngest, FALSE, pool));
  return SVN_NO_ERROR;
}



/** Main. **/

int
main (int argc, const char * const *argv)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_allocator_t *allocator;
  apr_pool_t *pool;

  const svn_opt_subcommand_desc_t *subcommand = NULL;
  struct svnadmin_opt_state opt_state;
  apr_getopt_t *os;  
  int opt_id;
  int received_opts[SVN_OPT_MAX_OPTIONS];
  int i, num_opts = 0;

  /* Initialize the app. */
  if (svn_cmdline_init ("svnadmin", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a seperate mutexless allocator,
   * given this application is single threaded.
   */
  if (apr_allocator_create (&allocator))
    return EXIT_FAILURE;

  apr_allocator_max_free_set (allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  pool = svn_pool_create_ex (NULL, allocator);
  apr_allocator_owner_set (allocator, pool);

  if (argc <= 1)
    {
      subcommand_help (NULL, NULL, pool);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }

  /* Initialize opt_state. */
  memset (&opt_state, 0, sizeof (opt_state));
  opt_state.start_revision.kind = svn_opt_revision_unspecified;
  opt_state.end_revision.kind = svn_opt_revision_unspecified;

  /* Parse options. */
  apr_getopt_init (&os, pool, argc, argv);
  os->interleave = 1;

  while (1)
    {
      const char *opt_arg;
      const char *utf8_opt_arg;

      /* Parse the next option. */
      apr_err = apr_getopt_long (os, options_table, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF (apr_err))
        break;
      else if (apr_err)
        {
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      /* Stash the option code in an array before parsing it. */
      received_opts[num_opts] = opt_id;
      num_opts++;

      switch (opt_id) {
      case 'r':
        {
          if (opt_state.start_revision.kind != svn_opt_revision_unspecified)
            {
              svn_handle_error (svn_error_create
                                (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                 "Multiple revision arguments encountered; "
                                 "try '-r M:N' instead of '-r M -r N'"),
                                stderr, FALSE);
              svn_pool_destroy (pool);
              return EXIT_FAILURE;
            }
          if (svn_opt_parse_revision (&(opt_state.start_revision),
                                      &(opt_state.end_revision),
                                      opt_arg, pool) != 0)
            {
              err = svn_utf_cstring_to_utf8 (&utf8_opt_arg, opt_arg,
                                             pool);

              if (err)
                svn_handle_error (err, stderr, FALSE);
              else
                svn_handle_error (svn_error_createf
                                  (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                                   "Syntax error in revision argument \"%s\"",
                                   utf8_opt_arg),
                                  stderr, FALSE);
              svn_pool_destroy (pool);
              return EXIT_FAILURE;
            }
        }
        break;
      case 'q':
        opt_state.quiet = TRUE;
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case svnadmin__version:
        opt_state.version = TRUE;
        opt_state.help = TRUE;
        break;
      case svnadmin__incremental:
        opt_state.incremental = TRUE;
        break;
      case svnadmin__follow_copies:
        opt_state.follow_copies = TRUE;
        break;
      case svnadmin__ignore_uuid:
        opt_state.uuid_action = svn_repos_load_uuid_ignore;
        break;
      case svnadmin__force_uuid:
        opt_state.uuid_action = svn_repos_load_uuid_force;
        break;
      case svnadmin__parent_dir:
        err = svn_utf_cstring_to_utf8 (&opt_state.parent_dir, opt_arg,
                                       pool);
        if (err)
          {
            svn_handle_error (err, stderr, FALSE);
            svn_pool_destroy (pool);
            return EXIT_FAILURE;
          }
        opt_state.parent_dir = svn_path_internal_style (opt_state.parent_dir,
                                                        pool);
        break;
      case svnadmin__bdb_txn_nosync:
        opt_state.bdb_txn_nosync = TRUE;
        break;
      case svnadmin__config_dir:
        opt_state.config_dir = apr_pstrdup (pool, svn_path_canonicalize(opt_arg,
                                                                       pool));
        break;
      default:
        {
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
      }  /* close `switch' */
    }  /* close `while' */
  
  /* If the user asked for help, then the rest of the arguments are
     the names of subcommands to get help on (if any), or else they're
     just typos/mistakes.  Whatever the case, the subcommand to
     actually run is subcommand_help(). */
  if (opt_state.help)
    subcommand = svn_opt_get_canonical_subcommand (cmd_table, "help");

  /* If we're not running the `help' subcommand, then look for a
     subcommand in the first argument. */
  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          fprintf (stderr, "subcommand argument required\n");
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
          subcommand = svn_opt_get_canonical_subcommand (cmd_table, first_arg);
          if (subcommand == NULL)
            {
              fprintf (stderr, "unknown command: %s\n", first_arg);
              subcommand_help (NULL, NULL, pool);
              svn_pool_destroy (pool);
              return EXIT_FAILURE;
            }
        }
    }

  /* If there's a second argument, it's probably the repository.
     Every subcommand except `help' requires one, so we parse it out
     here and store it in opt_state. */
  if (subcommand->cmd_func != subcommand_help)
    {
      const char *repos_path = NULL;

      if (os->ind < os->argc)
        {
          opt_state.repository_path = os->argv[os->ind++];
          SVN_INT_ERR (svn_utf_cstring_to_utf8 (&(opt_state.repository_path),
                                                opt_state.repository_path,
                                                pool));
          repos_path 
            = svn_path_internal_style (opt_state.repository_path, pool);
        }

      if (repos_path == NULL)
        {
          fprintf (stderr, "repository argument required\n");
          subcommand_help (NULL, NULL, pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
      else if (svn_path_is_url (repos_path))
        {
          fprintf (stderr,
                   "'%s' is a URL when it should be a path\n",
                   repos_path);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }

      /* Copy repos path into the OPT_STATE structure. */
      opt_state.repository_path = repos_path;      
    }

  /* Check that the subcommand wasn't passed any inappropriate options. */
  for (i = 0; i < num_opts; i++)
    {
      opt_id = received_opts[i];

      /* All commands implicitly accept --help, so just skip over this
         when we see it. Note that we don't want to include this option
         in their "accepted options" list because it would be awfully
         redundant to display it in every commands' help text. */
      if (opt_id == 'h' || opt_id == '?')
        continue;

      if (! svn_opt_subcommand_takes_option (subcommand, opt_id))
        {
          const char *optstr;
          const apr_getopt_option_t *badopt = 
            svn_opt_get_option_from_code (opt_id, options_table);
          svn_opt_format_option (&optstr, badopt, FALSE, pool);
          fprintf (stderr,
                   "\nError: subcommand '%s' doesn't accept option '%s'\n\n",
                   subcommand->name, optstr);
          svn_opt_subcommand_help (subcommand->name,
                                   cmd_table,
                                   options_table,
                                   pool);
          svn_pool_destroy (pool);
          return EXIT_FAILURE;
        }
    }

  /* Run the subcommand. */
  err = (*subcommand->cmd_func) (os, &opt_state, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          svn_handle_error (err, stderr, 0);
          svn_opt_subcommand_help (subcommand->name, cmd_table,
                                   options_table, pool);
        }
      else
        svn_handle_error (err, stderr, 0);
      svn_pool_destroy (pool);
      return EXIT_FAILURE;
    }
  else
    {
      svn_pool_destroy (pool);
      return EXIT_SUCCESS;
    }
}
