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
#include "svnadmin.h"

typedef enum svnadmin_cmd_t
{
  svnadmin_cmd_unknown = 0,

  svnadmin_cmd_create,
  svnadmin_cmd_createtxn,
  svnadmin_cmd_deltify,
  svnadmin_cmd_dump,
  svnadmin_cmd_load,
  svnadmin_cmd_lscr,
  svnadmin_cmd_lsrevs,
  svnadmin_cmd_lstxns,
  svnadmin_cmd_recover,
  svnadmin_cmd_rmtxns,
  svnadmin_cmd_setlog,
  svnadmin_cmd_shell,
  svnadmin_cmd_undeltify,
  svnadmin_cmd_youngest

} svnadmin_cmd_t;


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
    return svn_error_create (apr_err, 0, NULL, pool,
                             "error opening stdio file");
  
  *stream = svn_stream_from_aprfile (stdio_file, pool);
  return SVN_NO_ERROR;   
}
                     



/*** Tree printing. ***/

/* Print the tree at ROOT:PATH, indenting by INDENTATION spaces.
   Use POOL for any allocation.  */
static svn_error_t *
print_tree (svn_fs_root_t *root,
            const char *path,
            int indentation,
            apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  SVN_ERR (svn_fs_dir_entries (&entries, root, path, pool));
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_fs_dirent_t *this_entry;
      const char *this_full_path, *native_name;
      int is_dir;
      int i;
      const svn_fs_id_t *id;
      svn_string_t *id_str;

      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;

      this_full_path = apr_psprintf (subpool, "%s/%s", path, this_entry->name);

      /* Indent. */
      for (i = 0; i < indentation; i++)
        printf (" ");

      SVN_ERR (svn_utf_cstring_from_utf8 (&native_name, this_entry->name,
                                          subpool));
      printf ("%s", native_name);
      
      SVN_ERR (svn_fs_node_id (&id, root, this_full_path, subpool));
      id_str = svn_fs_unparse_id (id, pool);

      SVN_ERR (svn_fs_is_dir (&is_dir, root, this_full_path, subpool));
      if (is_dir)
        {
          printf ("/ <%s>\n", id_str->data);  /* trailing slash for dirs */
          print_tree (root, this_full_path, indentation + 1, subpool);
        }
      else   /* assume it's a file */
        {
          apr_off_t len;
          SVN_ERR (svn_fs_file_length (&len, root, this_full_path, subpool));
          printf (" <%s> [%" APR_OFF_T_FMT "]\n", id_str->data, len);
        }
      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}




/*** Argument parsing and usage. ***/
static void
usage (const char *progname, int exit_code)
{
  /*** PLEASE: Keep the following commands in alphabetical order!! ***/
  fprintf
    (exit_code ? stderr : stdout,
     "usage: %s SUBCOMMAND REPOS_PATH [ARGS...]\n"
     "\n"
     "Subcommands are: \n"
     "\n"
     "   create    REPOS_PATH\n"
     "      Create a new, empty repository at REPOS_PATH.\n"
     "\n"
     "   createtxn REPOS_PATH BASE_REV\n"
     "      Create a new transaction based on BASE_REV.\n"
#if 0 /* svn_fs_deltify() is not currently implemented. */
     "\n"
     "   deltify   REPOS_PATH REVISION PATH\n"
     "      Offer the repository a chance to deltify the storage\n"
     "      associated with PATH in REVISION.  If PATH represents\n"
     "      a directory, perform a recursive deltification of the\n"
     "      tree starting at PATH.\n"
#endif /* 0 */
     "\n"
     "   dump      REPOS_PATH [LOWER_REV [UPPER_REV]] [--incremental]\n"
     "      Dump the contents of filesystem to stdout in a 'dumpfile'\n"
     "      portable format, sending feedback to stderr.  Dump revisions\n"
     "      LOWER_REV through UPPER_REV. If no revisions are given, all\n"
     "      revision trees are dumped.  If just LOWER_REV is given, that one\n"
     "      revision tree is dumped.\n"
     "\n"
     "   load      REPOS_PATH\n"
     "      Read a 'dumpfile'-formatted stream from stdin, committing\n"
     "      new revisions into the repository's filesystem.\n"
     "      Send progress feedback to stdout.\n"
     "\n"
     "   lscr      REPOS_PATH PATH [--copies]\n"
     "      Print, one-per-line and youngest-to-eldest, the revisions in\n"
     "      which PATH was modified.  Use the COPIES flag to allow this\n"
     "      operation to cross copy history while searching for revisions.\n"
     "      (For directories, this is, for now, almost guaranteed to be\n"
     "      uninteresting.  Also, PATH must exist in the HEAD of the\n"
     "      repository.)\n"
     "\n"
     "   lsrevs    REPOS_PATH [LOWER_REV [UPPER_REV]]\n"
     "      If no revision is given, all revision trees are printed.\n"
     "      If just LOWER_REV is given, that revision tree is printed.\n"
     "      If two revisions are given, that range is printed, inclusive.\n"
     "\n"
     "   lstxns    REPOS_PATH [--long]\n"
     "      Print all txn names and, if \"--long\" is specified, their\n"
     "      metadata and trees.\n"
     "\n"
#if 0 
/* see TODO below at next `#if 0' */
     "   recover   REPOS_PATH\n"
     "      Run the Berkeley DB recovery procedure on a repository.  Do\n"
     "      this if you've been getting errors indicating that recovery\n"
     "      ought to be run.\n"
     "\n"
#endif /* 0 */
     "   rmtxns    REPOS_PATH TXN_NAME [...]\n"
     "      Delete the transaction(s) named TXN_NAME.\n"
     "\n"
     "   setlog    REPOS_PATH REVNUM FILE\n"
     "      Set the log-message on revision REVNUM to the contents of FILE.\n"
     "      (Careful!  Revision props are not historied, so this command\n"
     "       will -permanently- overwrite the previous log message.)\n"
     "\n"
     "   shell     REPOS_PATH\n"
     "      Enter interactive shell for exploring the repository.\n"
     "\n"
     "   undeltify REPOS_PATH REVISION PATH\n"
     "      Undeltify (ensure fulltext storage for) PATH in REVISION.\n"
     "      If PATH represents a directory, perform a recursive\n"
     "      undeltification of the tree starting at PATH.\n"
     "\n"
     "   youngest  REPOS_PATH\n"
     "      Print the latest revision number.\n"
     "\n"
     "Printing a tree shows its structure, node ids, and file sizes.\n"
     "\n",
     progname);

  exit (exit_code);
}



/*** Main. ***/

static svnadmin_cmd_t 
parse_command (const char *command)
{
  if (! strcmp (command, "create"))
    return svnadmin_cmd_create;
  else if (! strcmp (command, "youngest"))
    return svnadmin_cmd_youngest;
  else if (! strcmp (command, "lscr"))
    return svnadmin_cmd_lscr;
  else if (! strcmp (command, "lstxns"))
    return svnadmin_cmd_lstxns;
  else if (! strcmp (command, "lsrevs"))
    return svnadmin_cmd_lsrevs;
  else if (! strcmp (command, "rmtxns"))
    return svnadmin_cmd_rmtxns;
  else if (! strcmp (command, "createtxn"))
    return svnadmin_cmd_createtxn;
  else if (! strcmp (command, "setlog"))
    return svnadmin_cmd_setlog;
  else if (! strcmp (command, "shell"))
    return svnadmin_cmd_shell;
  else if (! strcmp (command, "undeltify"))
    return svnadmin_cmd_undeltify;
#if 0 /* svn_fs_deltify() is not currently implemented. */
  else if (! strcmp (command, "deltify"))
    return svnadmin_cmd_deltify;
#endif /* 0 */
  else if (! strcmp (command, "dump"))
    return svnadmin_cmd_dump;
  else if (! strcmp (command, "load"))
    return svnadmin_cmd_load;
  else if (! strcmp (command, "recover"))
    return svnadmin_cmd_recover;

  return svnadmin_cmd_unknown;
}



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
  apr_pool_t *pool;
  svn_repos_t *repos;
  svn_fs_t *fs;
  svnadmin_cmd_t command = svnadmin_cmd_unknown;
  const char *path = NULL;

  /* ### this whole thing needs to be cleaned up once client/main.c
     ### is refactored. for now, let's just get the tool up and
     ### running. */

  setlocale (LC_CTYPE, "");

  if (argc < 3)
    {
      usage (argv[0], 1);
      /* NOTREACHED */
    }

  apr_initialize ();
  pool = svn_pool_create (NULL);

  INT_ERR (svn_utf_cstring_to_utf8 (&path, argv[2], NULL, pool));

  command = parse_command (argv[1]);
  switch (command)
    {
    case svnadmin_cmd_unknown:
    default:
      {
        usage (argv[0], 1);
        /* NOTREACHED */
      }
      break;

    case svnadmin_cmd_create:
      {
        INT_ERR (svn_repos_create (&repos, path, pool));
      }
      break;

    case svnadmin_cmd_youngest:
      {
        svn_revnum_t youngest_rev;

        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        svn_fs_youngest_rev (&youngest_rev, fs, pool);
        printf ("%" SVN_REVNUM_T_FMT "\n", youngest_rev);
      }
      break;

    case svnadmin_cmd_lscr:
      {
        svn_revnum_t youngest_rev;
        svn_fs_root_t *rev_root;
        apr_array_header_t *revs, *paths;
        int i, copies = 0;

        /* There are either 4 arguments (no "copies"), or there are 5
           arguments, the last of which is "--copies".  Anything else is
           bogus.  */
        if ((argc == 4) 
            || ((argc == 5) && (! strcmp (argv[4], "--copies"))))
          {
            if (argc == 5)
              copies = 1;
          }
        else
          {
            usage (argv[0], 1);
            /* NOTREACHED */
          }

        paths = apr_array_make (pool, 1, sizeof (const char *));
        INT_ERR (svn_utf_cstring_to_utf8 ((const char **)apr_array_push(paths),
                                          argv[3], NULL, pool));

        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        svn_fs_youngest_rev (&youngest_rev, fs, pool);
        INT_ERR (svn_fs_revision_root (&rev_root, fs, youngest_rev, pool));
        INT_ERR (svn_fs_revisions_changed (&revs, rev_root, paths,
                                           copies, pool));
        for (i = 0; i < revs->nelts; i++)
          {
            svn_revnum_t this_rev = ((svn_revnum_t *)revs->elts)[i];
            printf ("%" SVN_REVNUM_T_FMT "\n", this_rev);
          }
      }
      break;

    case svnadmin_cmd_lstxns:
      {
        apr_array_header_t *txns;
        const char *txn_name;
        svn_boolean_t show_extra = FALSE;
        apr_pool_t *this_pool = NULL; /* only used if SHOW_EXTRA is TRUE */
        int i;

        if (argc >= 4) 
          {
            if (strcmp (argv[3], "--long") != 0)
              {
                usage (argv[0], 1);
                /* NOTREACHED */
              }
            show_extra = TRUE;
          }

        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        INT_ERR (svn_fs_list_transactions(&txns, fs, pool));
        
        if (show_extra)
          this_pool = svn_pool_create (pool);

        /* Loop, printing revisions. */
        for (i = 0; i < txns->nelts; i++)
          {
            svn_fs_txn_t *txn;
            svn_fs_root_t *this_root;
            svn_string_t *datestamp;
            svn_string_t *author;
            svn_string_t *log;

            txn_name = APR_ARRAY_IDX (txns, i, const char *);
            if (! show_extra)
              {
                printf ("%s\n", txn_name);
              }
            else
              {
                const svn_fs_id_t *root_id;
                svn_string_t *id_str;
                const char *txn_name_native, *datestamp_native;
                const char *author_native, *log_native;
                
                INT_ERR (svn_fs_open_txn (&txn, fs, txn_name, this_pool));
                INT_ERR (svn_fs_txn_root (&this_root, txn, this_pool));
                INT_ERR (svn_fs_txn_prop (&datestamp, txn,
                                          SVN_PROP_REVISION_DATE, 
                                          this_pool));
                INT_ERR (svn_fs_txn_prop (&author, txn,
                                          SVN_PROP_REVISION_AUTHOR, 
                                          this_pool));
                if ((! datestamp) || (! datestamp->data))
                  datestamp = svn_string_create ("", this_pool);
                if ((! author) || (! author->data))
                  author = svn_string_create ("", this_pool);
                INT_ERR (svn_fs_txn_prop (&log, txn,
                                          SVN_PROP_REVISION_LOG, 
                                          this_pool));
                if (! log)
                  log = svn_string_create ("", this_pool);
                
                INT_ERR (svn_utf_cstring_from_utf8 (&txn_name_native, txn_name,
                                                    this_pool));
                INT_ERR (svn_utf_cstring_from_utf8 (&datestamp_native,
                                                    datestamp->data,
                                                    this_pool));
                INT_ERR (svn_utf_cstring_from_utf8 (&author_native,
                                                    author->data,
                                                    this_pool));
                INT_ERR (svn_utf_cstring_from_utf8 (&log_native, log->data,
                                                    this_pool));

                printf ("Txn %s:\n", txn_name_native);
                printf ("Created: %s\n", datestamp_native);
                printf ("Author: %s\n", author_native);
                printf ("Log (%" APR_SIZE_T_FMT " bytes):\n%s\n",
                        log->len, log_native);
                printf ("==========================================\n");
                INT_ERR (svn_fs_node_id (&root_id, this_root, "", pool));
                id_str = svn_fs_unparse_id (root_id, pool);
                printf ("/ <%s>\n", id_str->data);
                print_tree (this_root, "", 1, this_pool);
                printf ("\n");
                svn_pool_clear (this_pool);
              }
          }

        if (show_extra)
          svn_pool_destroy (this_pool);
      }
      break;

    case svnadmin_cmd_lsrevs:
      {
        apr_pool_t *this_pool = svn_pool_create (pool);
        svn_revnum_t
          lower = SVN_INVALID_REVNUM,
          upper = SVN_INVALID_REVNUM,
          this;

        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);

        /* Do the args tell us what revisions to inspect? */
        if (argv[3])
          {
            lower = SVN_STR_TO_REV (argv[3]);
            if (argv[4])
              upper = SVN_STR_TO_REV (argv[4]);
          }
        
        /* Fill in for implied args. */
        if (lower == SVN_INVALID_REVNUM)
          {
            lower = 0;
            svn_fs_youngest_rev (&upper, fs, pool);
          }
        else if (upper == SVN_INVALID_REVNUM)
          upper = lower;
        
        /* Loop, printing revisions. */
        for (this = lower; this <= upper; this++)
          {
            svn_fs_root_t *this_root;
            svn_string_t *datestamp;
            svn_string_t *author;
            svn_string_t *log;
            const svn_fs_id_t *root_id;
            svn_string_t *id_str;
            const char *datestamp_native, *author_native, *log_native;

            INT_ERR (svn_fs_revision_root (&this_root, fs, this, this_pool));
            INT_ERR (svn_fs_revision_prop (&datestamp, fs, this, 
                                           SVN_PROP_REVISION_DATE, this_pool));
            INT_ERR (svn_fs_revision_prop (&author, fs, this, 
                                           SVN_PROP_REVISION_AUTHOR, 
                                           this_pool));
            if (! author)
              author = svn_string_create ("", this_pool);
            
            INT_ERR (svn_fs_revision_prop (&log, fs, this,
                                           SVN_PROP_REVISION_LOG, this_pool));
            if (! log)
              log = svn_string_create ("", this_pool);
            
            INT_ERR (svn_utf_cstring_from_utf8 (&datestamp_native,
                                                datestamp->data,
                                                this_pool));
            INT_ERR (svn_utf_cstring_from_utf8 (&author_native,
                                                author->data, this_pool));
            INT_ERR (svn_utf_cstring_from_utf8 (&log_native, log->data,
                                                this_pool));

            printf ("Revision %" SVN_REVNUM_T_FMT "\n", this);
            printf ("Created: %s\n", datestamp_native);
            printf ("Author: %s\n", author_native);
            printf ("Log (%" APR_SIZE_T_FMT " bytes):\n%s\n",
                    log->len, log_native);
            printf ("==========================================\n");
            INT_ERR (svn_fs_node_id (&root_id, this_root, "", this_pool));
            id_str = svn_fs_unparse_id (root_id, this_pool);
            printf ("/ <%s>\n", id_str->data);
            print_tree (this_root, "", 1, this_pool);
            printf ("\n");

            svn_pool_clear (this_pool);
          }
        svn_pool_destroy (this_pool);
      }
      break;

    case svnadmin_cmd_dump:
      {
        svn_boolean_t incremental = FALSE;
        int arg_count = argc;
        svn_stream_t *stdout_stream, *stderr_stream;
        svn_revnum_t
          lower = SVN_INVALID_REVNUM,
          upper = SVN_INVALID_REVNUM;

        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);

        if (arg_count > 6)
          {
            usage (argv[0], 1);
            /* NOT REACHED */
          }

        /* Check to see if the last argument is "--incremental",
           meaning this is an incremental dump.  */
        if ((arg_count > 3) 
            && (strcmp (argv[arg_count - 1], "--incremental") == 0))
          {
            arg_count--;
            incremental = TRUE;
          }

        /* Do the args tell us what revisions to inspect? */
        if (arg_count > 3)
          {
            lower = SVN_STR_TO_REV (argv[3]);
            if (arg_count > 4)
              upper = SVN_STR_TO_REV (argv[4]);
          }
        
        /* Fill in for implied args. */
        if (lower == SVN_INVALID_REVNUM)
          {
            lower = 0;
            svn_fs_youngest_rev (&upper, fs, pool);
          }
        else if (upper == SVN_INVALID_REVNUM)
          upper = lower;
        
        /* Run the dump to STDOUT.  Let the user redirect output into
           a file if they want.  :-)  Progress feedback goes to stderr. */
        INT_ERR (create_stdio_stream (&stdout_stream,
                                      apr_file_open_stdout, pool));
        INT_ERR (create_stdio_stream (&stderr_stream,
                                      apr_file_open_stderr, pool));

        INT_ERR (svn_repos_dump_fs (repos, stdout_stream, stderr_stream,
                                    lower, upper, incremental, pool));
      }
      break;

    case svnadmin_cmd_load:
      {
        svn_stream_t *stdin_stream, *stdout_stream;

        INT_ERR (svn_repos_open (&repos, path, pool));

        /* Read the stream from STDIN.  Users can redirect a file. */
        INT_ERR (create_stdio_stream (&stdin_stream,
                                      apr_file_open_stdin, pool));

        /* Have the parser dump feedback to STDOUT. */
        INT_ERR (create_stdio_stream (&stdout_stream,
                                      apr_file_open_stdout, pool));

        INT_ERR (svn_repos_load_fs (repos, stdin_stream, stdout_stream, pool));
      }
      break;

    case svnadmin_cmd_rmtxns:
      {
        svn_fs_txn_t *txn;
        int i;
        
        if (! argv[3])
          {
            usage (argv[0], 1);
            /* NOTREACHED */
          }
        
        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        
        /* All the rest of the arguments are transaction names. */
        for (i = 3; i < argc; i++)
          {
            const char *txn_name_utf8;
            INT_ERR (svn_utf_cstring_to_utf8 (&txn_name_utf8, argv[i],
                                              NULL, pool));
            INT_ERR (svn_fs_open_txn (&txn, fs, txn_name_utf8, pool));
            INT_ERR (svn_fs_abort_txn (txn));
          }
      }
      break;

    case svnadmin_cmd_createtxn:
      {
        svn_fs_txn_t *txn;
        
        if (! argv[3])
          {
            usage (argv[0], 1);
            /* NOTREACHED */
          }
        
        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        INT_ERR (svn_fs_begin_txn (&txn, fs, SVN_STR_TO_REV (argv[3]), pool));
        INT_ERR (svn_fs_close_txn (txn));
      }
      break;

    case svnadmin_cmd_setlog:
      {
        svn_revnum_t the_rev;
        svn_stringbuf_t *file_contents, *file_contents_utf8;
        svn_string_t log_contents;
        const char *filename_utf8;

        if (argc != 5)
          {
            printf ("Error: `setlog' requires exactly 3 arguments.\n");
            exit(1);
          }
      
        /* get revision and file from argv[] */
        the_rev = SVN_STR_TO_REV (argv[3]);
        INT_ERR (svn_utf_cstring_to_utf8 (&filename_utf8, argv[4],
                                          NULL, pool));
        INT_ERR (svn_string_from_file (&file_contents, filename_utf8, pool)); 
        INT_ERR (svn_utf_stringbuf_to_utf8 (&file_contents_utf8, file_contents,
                                            pool));
        log_contents.data = file_contents_utf8->data;
        log_contents.len = file_contents_utf8->len;

        /* open the filesystem  */
        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);

        /* set the revision property */
        INT_ERR (svn_fs_change_rev_prop (fs, the_rev, SVN_PROP_REVISION_LOG,
                                         &log_contents, pool));
      }
      break;

    case svnadmin_cmd_deltify:
    case svnadmin_cmd_undeltify:
      {
        svn_revnum_t the_rev;
        int is_dir = 0;
        svn_fs_root_t *rev_root;
        const char *node;
        int is_deltify = (command == svnadmin_cmd_deltify);

        if (argc != 5)
          {
            usage (argv[0], 1);
            /* NOTREACHED */
          }

        /* get revision and path from argv[] */
        the_rev = SVN_STR_TO_REV (argv[3]);
        INT_ERR (svn_utf_cstring_to_utf8 (&node, argv[4], NULL, pool));

        /* open the filesystem */
        INT_ERR (svn_repos_open (&repos, path, pool));      
        fs = svn_repos_fs (repos);

        /* open the revision root */
        INT_ERR (svn_fs_revision_root (&rev_root, fs, the_rev, pool));

        /* see if PATH represents a directory (this doubles as an
           existence check!) */
        INT_ERR (svn_fs_is_dir (&is_dir, rev_root, node, pool));

        /* do the (un-)deltification */
        printf ("%seltifying `%s' in revision %" SVN_REVNUM_T_FMT "...", 
                is_deltify ? "D" : "Und", argv[4], the_rev);
        if (is_deltify)
          {
            INT_ERR (svn_fs_deltify (rev_root, node, is_dir ? 1 : 0, pool));
          }
        else
          {
            INT_ERR (svn_fs_undeltify (rev_root, node, is_dir ? 1 : 0, pool));
          }
        printf ("done.\n");
      }
      break;

#if 0
      /* ### TODO: Get this working with new libsvn_repos API.  We need
     the repos API to access the lockfile paths and such, but we
     apparently don't want the locking that comes along with the repos
     API. */
    case svnadmin_cmd_recover:
      {
        apr_status_t apr_err;
        const char *lockfile_path, *env_path;
        apr_file_t *lockfile_handle = NULL;
        svn_error_t *err;
        const char *progname_utf8;

        INT_ERR (svn_utf_cstring_to_utf8 (argv[0], &progname_urf8,
                                          NULL, pool));

        /* Don't use svn_repos_open() here, because we don't want the
           usual locking behavior. */
        fs = svn_fs_new (pool);
        err = svn_fs_open_berkeley (fs, path);
        if (err && (err->src_err != DB_RUNRECOVERY))
          goto error;

        /* Exclusively lock the repository.  This blocks on other locks,
           including shared locks. */
        lockfile_path = svn_fs_db_lockfile (fs, pool);
        err = svn_io_file_open (&lockfile_handle, lockfile_path,
                                (APR_WRITE | APR_APPEND), APR_OS_DEFAULT, pool);
        if (err)
          {
            err = svn_error_createf
              (err->apr_err, err->src_err, err, pool,
               "%s: error opening db lockfile `%s'", progname_utf8, lockfile_path);
            goto error;
          }

        apr_err = apr_file_lock (lockfile_handle, APR_FLOCK_EXCLUSIVE);
        if (apr_err)
          {
            err = svn_error_createf
              (apr_err, 0, NULL, pool,
               "%s: exclusive lock on `%s' failed", progname_utf8, lockfile_path);
            goto error;
          }

        /* Run recovery on the Berkeley environment, using FS to get the
           path to said environment. */ 
        env_path = svn_fs_db_env (fs, pool);
        /* ### todo: this usually seems to get an error -- namely, that
           the DB needs recovery!  Why would that be, when we just
           recovered it?  Is it an error to recover a DB that doesn't
           need recovery, perhaps?  See issue #430. */
        INT_ERR (svn_fs_berkeley_recover (env_path, pool));

        /* Release the exclusive lock. */
        apr_err = apr_file_unlock (lockfile_handle);
        if (apr_err)
          {
            err = svn_error_createf
              (apr_err, 0, NULL, pool,
               "%s: error unlocking `%s'", progname_utf8, lockfile_path);
            goto error;
          }

        apr_err = apr_file_close (lockfile_handle);
        if (apr_err)
          {
            err = svn_error_createf
              (apr_err, 0, NULL, pool,
               "%s: error closing `%s'", progname_utf8, lockfile_path);
            goto error;
          }

      error:
        svn_handle_error(err, stderr, FALSE);
        return EXIT_FAILURE;

      }
      break;
#endif /* 0 */

    case svnadmin_cmd_shell:
      {
        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        INT_ERR (svnadmin_run_shell (fs, pool));
      }

    } /* switch ... */

  INT_ERR (svn_repos_close (repos));

  svn_pool_destroy (pool);
  apr_terminate();

  return EXIT_SUCCESS;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
