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


#include "svnadmin.h"

typedef enum svnadmin_cmd_t
{
  svnadmin_cmd_unknown = 0,

  svnadmin_cmd_create,
  svnadmin_cmd_createtxn,
  svnadmin_cmd_deltify,
  svnadmin_cmd_dump,
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
  
  SVN_ERR (svn_fs_dir_entries (&entries, root, path, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_fs_dirent_t *this_entry;
      const char *this_full_path;
      int is_dir;
      int i;
      svn_fs_id_t *id;
      svn_stringbuf_t *id_str;

      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;

      this_full_path = apr_psprintf (pool, "%s/%s", path, this_entry->name);

      /* Indent. */
      for (i = 0; i < indentation; i++)
        printf (" ");

      printf ("%s", this_entry->name);
      
      SVN_ERR (svn_fs_node_id (&id, root, this_full_path, pool));
      id_str = svn_fs_unparse_id (id, pool);

      SVN_ERR (svn_fs_is_dir (&is_dir, root, this_full_path, pool));
      if (is_dir)
        {

          printf ("/ <%s>\n", id_str->data);  /* trailing slash for dirs */
          print_tree (root, this_full_path, indentation + 1, pool);
        }
      else   /* assume it's a file */
        {
          apr_off_t len;
          SVN_ERR (svn_fs_file_length (&len, root, this_full_path, pool));
          printf (" <%s> [%ld]\n", id_str->data, (long int) len);
        }
    }

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
     "\n"
     "   deltify   REPOS_PATH REVISION PATH\n"
     "      Offer the repository a chance to deltify the storage\n"
     "      associated with PATH in REVISION.  If PATH represents\n"
     "      a directory, perform a recursive deltification of the\n"
     "      tree starting at PATH.\n"
     "\n"
     "   dump   REPOS_PATH [LOWER_REV [UPPER_REV]]\n"
     "      Dump the contents of filesystem to stdout in a 'dumpfile'\n"
     "      portable format.  Dump revisions LOWER_REV through UPPER_REV.\n"
     "      If no revisions are given, all revision trees are dumped.\n"
     "      If just LOWER_REV is given, that one revision tree is dumped.\n"
     "\n"
     "   lscr      REPOS_PATH PATH\n"
     "      Print, one-per-line and youngest-to-eldest, the revisions in\n"
     "      which PATH was modified.\n"
     "      (For directories, this is, for now, almost guaranteed to be\n"
     "      uninteresting.  Also, PATH must exist in the HEAD of the\n"
     "      repository.)\n"
     "\n"
     "   lsrevs    REPOS_PATH [LOWER_REV [UPPER_REV]]\n"
     "      If no revision is given, all revision trees are printed.\n"
     "      If just LOWER_REV is given, that revision tree is printed.\n"
     "      If two revisions are given, that range is printed, inclusive.\n"
     "\n"
     "   lstxns    [--long] REPOS_PATH\n"
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
  else if (! strcmp (command, "deltify"))
    return svnadmin_cmd_deltify;
  else if (! strcmp (command, "dump"))
    return svnadmin_cmd_dump;
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

  if (argc < 3)
    {
      usage (argv[0], 1);
      /* NOTREACHED */
    }

  path = argv[2];
  apr_initialize ();
  pool = svn_pool_create (NULL);

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
        printf ("%ld\n", (long int) youngest_rev);
      }
      break;

    case svnadmin_cmd_lscr:
      {
        svn_revnum_t youngest_rev;
        svn_fs_root_t *rev_root;
        apr_array_header_t *revs, *paths;
        int i;

        if (argc != 4)
          {
            usage (argv[0], 1);
            /* NOTREACHED */
          }

        paths = apr_array_make (pool, 1, sizeof (const char *));
        (*(const char **)apr_array_push(paths)) = argv[3];

        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        svn_fs_youngest_rev (&youngest_rev, fs, pool);
        INT_ERR (svn_fs_revision_root (&rev_root, fs, youngest_rev, pool));
        INT_ERR (svn_fs_revisions_changed (&revs, rev_root, paths, pool));
        for (i = 0; i < revs->nelts; i++)
          {
            svn_revnum_t this_rev = ((svn_revnum_t *)revs->elts)[i];
            printf ("%ld\n", (long int)this_rev);
          }
      }
      break;

    case svnadmin_cmd_lstxns:
      {
        char **txns;
        char *txn_name;
        svn_boolean_t show_extra = FALSE;

        if (argc >= 4) 
          {
            if (strcmp (argv[2], "--long") != 0)
              {
                usage (argv[0], 1);
                /* NOTREACHED */
              }
            show_extra = TRUE;
            path = argv[3];
          }

        INT_ERR (svn_repos_open (&repos, path, pool));
        fs = svn_repos_fs (repos);
        INT_ERR (svn_fs_list_transactions(&txns, fs, pool));
        
        /* Loop, printing revisions. */
        while ((txn_name = *txns++))
          {
            svn_fs_txn_t *txn;
            svn_fs_root_t *this_root;
            svn_string_t *datestamp;
            svn_string_t *author;
            svn_string_t *log;

            if (show_extra)
              {
                apr_pool_t *this_pool = svn_pool_create (pool);
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
                
                printf ("Txn %s:\n", txn_name);
                printf ("Created: %s\n", datestamp->data);
                printf ("Author: %s\n", author->data);
                printf ("Log (%lu bytes):\n%s\n",
                        (unsigned long int) log->len, log->data);
                printf ("==========================================\n");
                print_tree (this_root, "", 1, this_pool);
                printf ("\n");
                svn_pool_destroy (this_pool);
              }
            else
              printf ("%s\n", txn_name);
          }
      }
      break;

    case svnadmin_cmd_lsrevs:
      {
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
            apr_pool_t *this_pool = svn_pool_create (pool);
            
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
            
            printf ("Revision %ld\n", (long int) this);
            printf ("Created: %s\n", datestamp->data);
            printf ("Author: %s\n", author->data);
            printf ("Log (%lu bytes):\n%s\n",
                    (unsigned long int) log->len, log->data);
            printf ("==========================================\n");
            print_tree (this_root, "", 1, this_pool);
            printf ("\n");
            
            svn_pool_destroy (this_pool);
          }
      }
      break;

    case svnadmin_cmd_dump:
      {
        apr_file_t *outfile;
        apr_status_t status;
        svn_revnum_t
          lower = SVN_INVALID_REVNUM,
          upper = SVN_INVALID_REVNUM;

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
        
        /* Run the dump to STDOUT.  Let the user redirect output into
           a file if they want.  :-)  */
        if ((status = apr_file_open_stdout (&outfile, pool)))
          {
            svn_error_t *err = svn_error_create (status, 0, NULL,
                                                 pool, "can't open stdout");
            svn_handle_error (err, stderr, 0);
            return EXIT_FAILURE;
          }

        INT_ERR (svn_repos_dump_fs (fs, outfile, lower, upper, pool));

        fflush(stdout);                                   
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
            INT_ERR (svn_fs_open_txn (&txn, fs, argv[i], pool));
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
        svn_stringbuf_t *file_contents;
        svn_string_t log_contents;

        if (argc != 5)
          {
            printf ("Error: `setlog' requires exactly 3 arguments.\n");
            exit(1);
          }
      
        /* get revision and file from argv[] */
        the_rev = SVN_STR_TO_REV (argv[3]);
        INT_ERR (svn_string_from_file (&file_contents, argv[4], pool)); 
        log_contents.data = file_contents->data;
        log_contents.len = file_contents->len;

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
        node = argv[4];

        /* open the filesystem */
        INT_ERR (svn_repos_open (&repos, path, pool));      
        fs = svn_repos_fs (repos);

        /* open the revision root */
        INT_ERR (svn_fs_revision_root (&rev_root, fs, the_rev, pool));

        /* see if PATH represents a directory (this doubles as an
           existence check!) */
        INT_ERR (svn_fs_is_dir (&is_dir, rev_root, node, pool));

        /* do the (un-)deltification */
        printf ("%seltifying `%s' in revision %ld...", 
                is_deltify ? "D" : "Und", node, (long int)the_rev);
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

        /* Don't use svn_repos_open() here, because we don't want the
           usual locking behavior. */
        fs = svn_fs_new (pool);
        err = svn_fs_open_berkeley (fs, path);
        if (err && (err->src_err != DB_RUNRECOVERY))
          goto error;

        /* Exclusively lock the repository.  This blocks on other locks,
           including shared locks. */
        lockfile_path = svn_fs_db_lockfile (fs, pool);
        apr_err = apr_file_open (&lockfile_handle, lockfile_path,
                                 (APR_WRITE | APR_APPEND), APR_OS_DEFAULT, pool);
        if (! APR_STATUS_IS_SUCCESS (apr_err))
          {
            err = svn_error_createf
              (apr_err, 0, NULL, pool,
               "%s: error opening db lockfile `%s'", argv[0], lockfile_path);
            goto error;
          }

        apr_err = apr_file_lock (lockfile_handle, APR_FLOCK_EXCLUSIVE);
        if (! APR_STATUS_IS_SUCCESS (apr_err))
          {
            err = svn_error_createf
              (apr_err, 0, NULL, pool,
               "%s: exclusive lock on `%s' failed", argv[0], lockfile_path);
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
        if (! APR_STATUS_IS_SUCCESS (apr_err))
          {
            err = svn_error_createf
              (apr_err, 0, NULL, pool,
               "%s: error unlocking `%s'", argv[0], lockfile_path);
            goto error;
          }

        apr_err = apr_file_close (lockfile_handle);
        if (! APR_STATUS_IS_SUCCESS (apr_err))
          {
            err = svn_error_createf
              (apr_err, 0, NULL, pool,
               "%s: error closing `%s'", argv[0], lockfile_path);
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
