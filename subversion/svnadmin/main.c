/*
 * main.c: Subversion server administration tool.
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


#include <locale.h>
#include <apr_file_io.h>
#include "svn_opt.h"
#include "svn_utf.h"
#include "svn_subst.h"
#include "svnadmin.h"


/*** Code. ***/

/* Helper to open stdio streams */

/* NOTE: we used to call svn_stream_from_stdio(), which wraps a stream
   around a standard stdio.h FILE pointer.  The problem is that these
   pointers operate through C Run Time (CRT) on Win32, which does all
   sorts of translation on them: LF's become CRLF's, and ctrl-Z's
   embedded in Word documents are interpreted as premature EOF's.

   So instead, we use apr_file_open_std*, which bypass the CRT and
   directly wrap the OS's file-handles, which don't know or care about
   translation.  Thus dump/load works correctly on Win32.
 */
static svn_error_t *
create_stdio_stream (svn_stream_t **stream,
                     APR_DECLARE(apr_status_t) open_fn (apr_file_t **, 
                                                        apr_pool_t *),
                     apr_pool_t *pool)
{
  apr_file_t *stdio_file;

  apr_status_t apr_err = open_fn (&stdio_file, pool);  
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL,
                             "error opening stdio file");
  
  *stream = svn_stream_from_aprfile (stdio_file, pool);
  return SVN_NO_ERROR;   
}



/** Subcommands. **/

static svn_opt_subcommand_t
  subcommand_create,
  subcommand_createtxn,
  subcommand_dump,
  subcommand_help,
  subcommand_load,
  subcommand_lscr,
  subcommand_lstxns,
  subcommand_recover,
  subcommand_rmtxns,
  subcommand_setlog;

enum 
  { 
    svnadmin__incremental = SVN_OPT_FIRST_LONGOPT_ID,
    svnadmin__follow_copies,
    svnadmin__long_output
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

    {"revision",      'r', 1,
     "specify revision number ARG (or X:Y range)"},

    {"incremental",   svnadmin__incremental, 0,
     "dump incrementally"},

    {"copies",   svnadmin__follow_copies, 0,
     "follow copy history"},

    {0,               0, 0, 0}
  };


/* Array of available subcommands.
 * The entire list must be terminated with an entry of nulls.
 */
static const svn_opt_subcommand_desc_t cmd_table[] =
  {
    {"create", subcommand_create, {0},
     "usage: svnadmin create REPOS_PATH\n\n"
     "Create a new, empty repository at REPOS_PATH.\n",
     {0} },
    
    {"createtxn", subcommand_createtxn, {0},
     "usage: svnadmin createtxn REPOS_PATH -r REVISION\n\n"
     "Create a new transaction based on REVISION.\n",
     {'r'} },
    
    {"dump", subcommand_dump, {0},
     "usage: svnadmin dump REPOS_PATH [-r LOWER[:UPPER]] [--incremental]\n\n"
     "Dump the contents of filesystem to stdout in a 'dumpfile'\n"
     "portable format, sending feedback to stderr.  Dump revisions\n"
     "LOWER rev through UPPER rev.  If no revisions are given, dump all\n"
     "revision trees.  If only LOWER is given, dump that one revision tree.\n"
     "If --incremental is passed, then the first revision dumped will be\n"
     "a diff against the previous revision, instead of the usual fulltext.\n",
     {'r', svnadmin__incremental} },

    {"help", subcommand_help, {"?", "h"},
     "usage: svn help [SUBCOMMAND1 [SUBCOMMAND2] ...]\n\n"
     "Display this usage message.\n",
     {0} },

    {"load", subcommand_load, {0},
     "usage: svnadmin load REPOS_PATH\n\n"
     "Read a 'dumpfile'-formatted stream from stdin, committing\n"
     "new revisions into the repository's filesystem.\n"
     "Send progress feedback to stdout.\n",
     {0} },

    {"lscr", subcommand_lscr, {0},
     "usage: svnadmin lscr REPOS_PATH PATH [--copies]\n\n"
     "Print, one-per-line and youngest-to-eldest, the revisions in\n"
     "which PATH was modified.  Use the COPIES flag to allow this\n"
     "operation to cross copy history while searching for revisions.\n"
     "(For directories, this is, for now, almost guaranteed to be\n"
     "uninteresting.  Also, PATH must exist in the HEAD of the\n"
     "repository.)\n",
     {svnadmin__follow_copies} },

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
     "usage: svnadmin rmtxns REPOS_PATH TXN_NAME [TXN_NAME2 ...]\n\n"
     "Delete the named transaction(s).\n",
     {0} },

    {"setlog", subcommand_setlog, {0},
     "usage: svnadmin setlog REPOS_PATH -r REVISION FILE\n\n"
     "Set the log-message on revision REVISION to the contents of FILE.\n"
     "(Note that revision properties are not historied, so this command\n"
     "will permanently overwrite the previous log message.)\n",
     {'r'} },

    { NULL, NULL, {0}, NULL, {0} }
  };


/* Baton for passing option/argument state to a subcommand function. */
struct svnadmin_opt_state
{
  const char *repository_path;
  svn_opt_revision_t start_revision, end_revision;  /* -r X[:Y] */
  svn_boolean_t help;                               /* --help or -? */
  svn_boolean_t incremental;                        /* --incremental */
  svn_boolean_t follow_copies;                      /* --copies */
  svn_boolean_t long_output;                        /* --long */
};

/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_create (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;

  SVN_ERR (svn_repos_create (&repos, opt_state->repository_path, pool));
  SVN_ERR (svn_repos_close (repos));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_createtxn (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;

  if (opt_state->start_revision.kind != svn_opt_revision_number)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
                              "missing revision");
  else if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
                              "only one revision allowed");
    
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);
  SVN_ERR (svn_fs_begin_txn (&txn, fs, opt_state->start_revision.value.number,
                             pool));
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_repos_close (repos));
  
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_dump (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_stream_t *stdout_stream, *stderr_stream;
  svn_revnum_t
    lower = SVN_INVALID_REVNUM,
    upper = SVN_INVALID_REVNUM;

  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);

  /* ### We only handle revision numbers right now, not dates. */
  if (opt_state->start_revision.kind == svn_opt_revision_number)
    lower = opt_state->start_revision.value.number;
  else
    lower = SVN_INVALID_REVNUM;

  if (opt_state->end_revision.kind == svn_opt_revision_number)
    upper = opt_state->end_revision.value.number;
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
      (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
       "first revision cannot be higher than second");

  /* Run the dump to STDOUT.  Let the user redirect output into
     a file if they want.  :-)  Progress feedback goes to stderr. */
  SVN_ERR (create_stdio_stream (&stdout_stream,
                                apr_file_open_stdout, pool));
  SVN_ERR (create_stdio_stream (&stderr_stream,
                                apr_file_open_stderr, pool));

  SVN_ERR (svn_repos_dump_fs (repos, stdout_stream, stderr_stream,
                              lower, upper, opt_state->incremental, pool));

  SVN_ERR (svn_repos_close (repos));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_help (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  const char *header =
    "general usage: svnadmin SUBCOMMAND REPOS_PATH  [ARGS & OPTIONS ...]\n"
    "Type \"svnadmin help <subcommand>\" for help on a specific subcommand.\n"
    "\n"
    "Available subcommands:\n";

  SVN_ERR (svn_opt_print_help (os, "svnadmin", FALSE, FALSE, NULL,
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
  
  SVN_ERR (svn_repos_load_fs (repos, stdin_stream, stdout_stream, pool));

  SVN_ERR (svn_repos_close (repos));

  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
static svn_error_t *
subcommand_lscr (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  struct svnadmin_opt_state *opt_state = baton;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_root_t *rev_root;
  svn_revnum_t youngest_rev;
  apr_array_header_t *revs, *args, *paths;
  int i;

  SVN_ERR (svn_opt_parse_all_args (&args, os, pool));
  
  if (args->nelts != 1)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
                              "exactly one path argument required");

  paths = apr_array_make (pool, 1, sizeof (const char *));
  SVN_ERR (svn_utf_cstring_to_utf8 ((const char **)apr_array_push(paths),
                                    APR_ARRAY_IDX (args, 0, const char *),
                                    NULL, pool));
  
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  fs = svn_repos_fs (repos);
  svn_fs_youngest_rev (&youngest_rev, fs, pool);
  SVN_ERR (svn_fs_revision_root (&rev_root, fs, youngest_rev, pool));
  SVN_ERR (svn_fs_revisions_changed (&revs, rev_root, paths,
                                     opt_state->follow_copies, pool));
  for (i = 0; i < revs->nelts; i++)
    {
      svn_revnum_t this_rev = ((svn_revnum_t *)revs->elts)[i];
      printf ("%" SVN_REVNUM_T_FMT "\n", this_rev);
    }
  
  SVN_ERR (svn_repos_close (repos));

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

  SVN_ERR (svn_repos_close (repos));
  
  return SVN_NO_ERROR;
}


/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
subcommand_recover (apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svn_revnum_t youngest_rev;
  svn_repos_t *repos;
  struct svnadmin_opt_state *opt_state = baton;

  printf ("Acquiring exclusive lock on repository db, and running "
          "recovery procedures.\nPlease stand by...");
  fflush(stdout);

  SVN_ERR (svn_repos_recover (opt_state->repository_path, pool));

  printf ("\nRecovery completed.\n");

  /* Since db transactions may have been replayed, it's nice to tell
     people what the latest revision is.  It also proves that the
     recovery actually worked. */
  SVN_ERR (svn_repos_open (&repos, opt_state->repository_path, pool));
  SVN_ERR (svn_fs_youngest_rev (&youngest_rev, svn_repos_fs (repos), pool));
  printf ("The latest repos revision is %"
          SVN_REVNUM_T_FMT ".\n", youngest_rev);
  SVN_ERR (svn_repos_close (repos));

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
                                        NULL, subpool));
      SVN_ERR (svn_fs_open_txn (&txn, fs, txn_name_utf8, subpool));
      SVN_ERR (svn_fs_abort_txn (txn));
      svn_pool_clear (subpool);
    }

  SVN_ERR (svn_repos_close (repos));

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
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
                              "missing revision");
  else if (opt_state->end_revision.kind != svn_opt_revision_unspecified)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
                              "only one revision allowed");
    
  SVN_ERR (svn_opt_parse_all_args (&args, os, pool));

  if (args->nelts != 1)
    return svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, 0, NULL,
                              "exactly one file argument required");
  
  SVN_ERR (svn_utf_cstring_to_utf8 (&filename_utf8,
                                    APR_ARRAY_IDX (args, 0, const char *),
                                    NULL, pool));
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

  SVN_ERR (svn_repos_close (repos));

  return SVN_NO_ERROR;
}



/** Main. **/

#define INT_ERR(expr)                                       \
  do {                                                      \
    svn_error_t *svnadmin_err__temp = (expr);               \
    if (svnadmin_err__temp) {                               \
      svn_handle_error (svnadmin_err__temp, stderr, FALSE); \
      return EXIT_FAILURE; }                                \
  } while (0)


int
main (int argc, const char * const *argv)
{
  svn_error_t *err;
  apr_status_t apr_err;
  int err2;
  apr_pool_t *pool;

  const svn_opt_subcommand_desc_t *subcommand = NULL;
  struct svnadmin_opt_state opt_state;
  apr_getopt_t *os;  
  int opt_id;
  int received_opts[SVN_OPT_MAX_OPTIONS];
  int i, num_opts = 0;

  setlocale (LC_CTYPE, "");

  apr_err = apr_initialize ();
  if (apr_err)
    {
      fprintf (stderr, "error: apr_initialize\n");
      return EXIT_FAILURE;
    }
  err2 = atexit (apr_terminate);
  if (err2)
    {
      fprintf (stderr, "error: atexit returned %d\n", err2);
      return EXIT_FAILURE;
    }

  pool = svn_pool_create (NULL);

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
                                (SVN_ERR_CL_ARG_PARSING_ERROR,
                                 0, NULL,
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
                                             NULL, pool);

              if (err)
                svn_handle_error (err, stderr, FALSE);
              else
                svn_handle_error (svn_error_createf
                                  (SVN_ERR_CL_ARG_PARSING_ERROR,
                                   0, NULL,
                                   "Syntax error in revision argument \"%s\"",
                                   utf8_opt_arg),
                                  stderr, FALSE);
              svn_pool_destroy (pool);
              return EXIT_FAILURE;
            }
        }
        break;
      case 'h':
      case '?':
        opt_state.help = TRUE;
        break;
      case svnadmin__incremental:
        opt_state.incremental = TRUE;
        break;
      case svnadmin__follow_copies:
        opt_state.follow_copies = TRUE;
        break;
      case svnadmin__long_output:
        opt_state.long_output = TRUE;
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

          INT_ERR (svn_utf_cstring_to_utf8 (&(opt_state.repository_path),
                                            opt_state.repository_path,
                                            NULL, pool));
          repos_path 
            = svn_path_canonicalize (opt_state.repository_path, pool);
        }

      if (repos_path == NULL)
        {
          fprintf (stderr, "repository argument required\n");
          subcommand_help (NULL, NULL, pool);
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
