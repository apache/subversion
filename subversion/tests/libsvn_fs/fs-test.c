/* fs-test.c --- tests for the filesystem
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>
#include "svn_error.h"
#include "svn_fs.h"

#include "../../libsvn_fs/fs.h"
#include "../../libsvn_fs/rev-table.h"
#include "../../libsvn_fs/trail.c"

/* A global pool, initialized by `main' for tests to use.  */
apr_pool_t *pool;


/*-------------------------------------------------------------------*/

/** Helper routines. **/

/* Create a berkeley db repository in a subdir NAME, and return a new
   FS object which points to it.  */
static svn_error_t *
create_fs_and_repos (svn_fs_t **fs, const char *name)
{
  *fs = svn_fs_new (pool);
  if (! fs)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Couldn't alloc a new fs object.");
  
  SVN_ERR (svn_fs_create_berkeley (*fs, name));
  
  return SVN_NO_ERROR;
}

/* Read all data from a generic read STREAM, and return it in STRING.
   Allocate the svn_string_t in POOL.  (All data in STRING will be
   dup'ed from STREAM using POOL too.) */
static svn_error_t *
stream_to_string (svn_string_t **string,
                  svn_stream_t *stream,
                  apr_pool_t *puel)
{
  char buf[50];
  apr_size_t len;
  svn_string_t *str = svn_string_create ("", puel);

  do 
    {
      /* "please read 40 bytes into buf" */
      len = 40;
      SVN_ERR (svn_stream_read (stream, buf, &len));
      
      /* Now copy however many bytes were *actually* read into str. */
      svn_string_appendbytes (str, buf, len);
      
    } while (len);  /* Continue until we're told that no bytes were
                       read. */

  *string = str;
  return SVN_NO_ERROR;
}

static svn_error_t *
set_file_contents (svn_fs_root_t *root,
                   const char *path,
                   const char *contents,
                   apr_pool_t *p00l)
{
  svn_txdelta_window_handler_t *consumer_func;
  void *consumer_baton;
  svn_string_t *wstring = svn_string_create (contents, p00l);

  SVN_ERR (svn_fs_apply_textdelta (&consumer_func, &consumer_baton,
                                   root, path, p00l));
  SVN_ERR (svn_txdelta_send_string (wstring, consumer_func,
                                    consumer_baton, p00l));

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/

/** The actual fs-tests called by `make check` **/

/* Create a filesystem.  */
static svn_error_t *
create_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs;

  *msg = "svn_fs_create_berkeley";

  /* Create and close a repository. */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-1")); /* helper */
  SVN_ERR (svn_fs_close_fs (fs));
  
  return SVN_NO_ERROR;
}


/* Open an existing filesystem.  */
static svn_error_t *
open_berkeley_filesystem (const char **msg)
{
  svn_fs_t *fs, *fs2;

  *msg = "open an existing Berkeley DB filesystem";

  /* Create and close a repository (using fs). */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-2")); /* helper */
  SVN_ERR (svn_fs_close_fs (fs));

  /* Create a different fs object, and use it to re-open the
     repository again.  */
  fs2 = svn_fs_new (pool);
  if (! fs2)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Couldn't alloc a new fs object.");

  SVN_ERR (svn_fs_open_berkeley (fs2, "test-repo-2"));
  SVN_ERR (svn_fs_close_fs (fs2));

  return SVN_NO_ERROR;
}


/* Fetch the youngest revision from a repos. */
static svn_error_t *
fetch_youngest_rev (const char **msg)
{
  svn_fs_t *fs;
  svn_revnum_t rev;

  *msg = "fetch the youngest revision from a filesystem";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-3")); /* helper */

  SVN_ERR (svn_fs_youngest_rev (&rev, fs, pool));
  
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Begin a txn, check its name, then close it */
static svn_error_t *
trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  char *txn_name;

  *msg = "begin a txn, check its name, then close it";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-4")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
      
  /* Test that the txn name is non-null. */
  SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));
  
  if (! txn_name)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Got a NULL txn name.");

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Open an existing transaction by name. */
static svn_error_t *
reopen_trivial_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  char *txn_name;

  *msg = "open an existing transaction by name";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-5")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
  SVN_ERR (svn_fs_txn_name (&txn_name, txn, pool));

  /* Close the transaction. */
  SVN_ERR (svn_fs_close_txn (txn));

  /* Reopen the transaction by name */
  SVN_ERR (svn_fs_open_txn (&txn, fs, txn_name, pool));

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Create a file! */
static svn_error_t *
create_file_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  *msg = "begin a txn, get the txn root, and add a file!";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-6")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));

  /* Get the txn root */
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  
  /* Create a new file in the root directory. */
  SVN_ERR (svn_fs_make_file (txn_root, "beer.txt", pool));

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


/* Make sure we get txn lists correctly. */
static svn_error_t *
verify_txn_list (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn1, *txn2;
  char *name1, *name2;
  char **txn_list;

  *msg = "create 2 txns, list them, and verify the list.";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-7")); /* helper */

  /* Begin a new transaction, get its name, close it.  */
  SVN_ERR (svn_fs_begin_txn (&txn1, fs, 0, pool));
  SVN_ERR (svn_fs_txn_name (&name1, txn1, pool));
  SVN_ERR (svn_fs_close_txn (txn1));

  /* Begin *another* transaction, get its name, close it.  */
  SVN_ERR (svn_fs_begin_txn (&txn2, fs, 0, pool));
  SVN_ERR (svn_fs_txn_name (&name2, txn2, pool));
  SVN_ERR (svn_fs_close_txn (txn2));

  /* Get the list of active transactions from the fs. */
  SVN_ERR (svn_fs_list_transactions (&txn_list, fs, pool));

  /* Check the list. It should have *exactly* two entries. */
  if ((txn_list[0] == NULL)
      || (txn_list[1] == NULL)
      || (txn_list[2] != NULL))
    goto all_bad;
  
  /* We should be able to find our 2 txn names in the list, in some
     order. */
  if ((! strcmp (txn_list[0], name1))
      && (! strcmp (txn_list[1], name2)))
    goto all_good;
  
  else if ((! strcmp (txn_list[1], name1))
           && (! strcmp (txn_list[0], name2)))
    goto all_good;
  
 all_bad:

  return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                           "Got a bogus txn list.");
 all_good:
  
  /* Close the fs. */
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Test writing & reading a file's contents. */
static svn_error_t *
write_and_read_file (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  svn_stream_t *rstream;
  svn_string_t *rstring;
  svn_string_t *wstring = svn_string_create ("Wicki wild, wicki wicki wild.",
                                             pool);

  *msg = "write and read a file's contents";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-8")); /* helper */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  
  /* Add an empty file. */
  SVN_ERR (svn_fs_make_file (txn_root, "beer.txt", pool));

  /* And write some data into this file. */
  SVN_ERR (set_file_contents (txn_root, "beer.txt", wstring->data, pool));
  
  /* Now let's read the data back from the file. */
  SVN_ERR (svn_fs_file_contents (&rstream, txn_root, "beer.txt", pool));  
  SVN_ERR (stream_to_string (&rstring, rstream, pool));

  /* Compare what was read to what was written. */
  if (! svn_string_compare (rstring, wstring))
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "data read != data written.");    

  /* Clean up the repos. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* Create a file, a directory, and a file in that directory! */
static svn_error_t *
create_mini_tree_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  *msg = "make a file, a subdir, and another file in that subdir!";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-9")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));

  /* Get the txn root */
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  
  /* Create a new file in the root directory. */
  SVN_ERR (svn_fs_make_file (txn_root, "wine.txt", pool));

  /* Create a new directory in the root directory. */
  SVN_ERR (svn_fs_make_dir (txn_root, "keg", pool));

  /* Now, create a file in our new directory. */
  SVN_ERR (svn_fs_make_file (txn_root, "keg/beer.txt", pool));

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


/* Create a file, a directory, and a file in that directory! */
static svn_error_t *
create_greek_tree_transaction (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;

  *msg = "make The Official Subversion Test Tree";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-10")); /* helper */

  /* Begin a new transaction that is based on revision 0.  */
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));

  /* Get the txn root */
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  
  /* Create a friggin' tree, already! */
  SVN_ERR (svn_fs_make_file (txn_root, "iota", pool));
  SVN_ERR (set_file_contents (txn_root, "iota",
                              "This is the file 'iota'.", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/mu", pool));
  SVN_ERR (set_file_contents (txn_root, "A/mu",
                              "This is the file 'mu'.", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A/B", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/lambda", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/lambda",
                              "This is the file 'lambda'.", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A/B/E", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/E/alpha", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/E/alpha",
                              "This is the file 'alpha'.", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/E/beta", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/E/beta",
                              "This is the file 'beta'.", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A/B/E/F", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A/B/C", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A/B/D", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/D/gamma", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/D/gamma",
                              "This is the file 'gamma'.", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A/B/D/G", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/D/G/pi", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/D/G/pi",
                              "This is the file 'pi'.", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/D/G/rho", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/D/G/rho",
                              "This is the file 'rho'.", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/D/G/tau", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/D/G/tau",
                              "This is the file 'tau'.", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A/B/D/H", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/D/H/chi", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/D/H/chi",
                              "This is the file 'chi'.", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/D/H/psi", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/D/H/psi",
                              "This is the file 'psi'.", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/B/D/H/omega", pool));
  SVN_ERR (set_file_contents (txn_root, "A/B/D/H/omega",
                              "This is the file 'omega'.", pool));

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}


/* Helper for list_directory. */
static svn_error_t *
verify_entry (apr_hash_t *entries, const char *key)
{
  svn_fs_dirent_t *ent = apr_hash_get (entries, key, strlen (key));

  if (ent == NULL)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "didn't find dir entry for \"%s\"", key);

  if ((ent->name == NULL) && (ent->id == NULL))
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "dir entry for \"%s\" has null name and null id", key);
  
  if (ent->name == NULL)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "dir entry for \"%s\" has null name", key);
  
  if (ent->id == NULL)
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "dir entry for \"%s\" has null id", key);
  
  if (strcmp (ent->name, key) != 0)
     return svn_error_createf
     (SVN_ERR_FS_GENERAL, 0, NULL, pool,
      "dir entry for \"%s\" contains wrong name (\"%s\")", key, ent->name);
        
  return SVN_NO_ERROR;
}


static svn_error_t *
list_directory (const char **msg)
{
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *txn_root;
  apr_hash_t *entries;

  *msg = "fill a directory, then list it";

  SVN_ERR (create_fs_and_repos (&fs, "test-repo-list-dir"));
  SVN_ERR (svn_fs_begin_txn (&txn, fs, 0, pool));
  SVN_ERR (svn_fs_txn_root (&txn_root, txn, pool));
  
  /* We create this tree
   *
   *         /q
   *         /A/x
   *         /A/y
   *         /A/z
   *         /B/m
   *         /B/n
   *         /B/o
   *
   * then list dir A.  It should have 3 files: "x", "y", and "z", no
   * more, no less.
   */

  /* Create the tree. */
  SVN_ERR (svn_fs_make_file (txn_root, "q", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "A", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/x", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/y", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "A/z", pool));
  SVN_ERR (svn_fs_make_dir  (txn_root, "B", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "B/m", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "B/n", pool));
  SVN_ERR (svn_fs_make_file (txn_root, "B/o", pool));

  /* Get A's entries. */
  SVN_ERR (svn_fs_dir_entries (&entries, txn_root, "A", pool));

  /* Make sure exactly the right set of entries is present. */
  if (apr_hash_count (entries) != 3)
    {
      return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                               "unexpected number of entries in dir");
    }
  else
    {
      SVN_ERR (verify_entry (entries, "x"));
      SVN_ERR (verify_entry (entries, "y"));
      SVN_ERR (verify_entry (entries, "z"));
    }

  /* Close the transaction and fs. */
  SVN_ERR (svn_fs_close_txn (txn));
  SVN_ERR (svn_fs_close_fs (fs));

  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg) = {
  0,
  create_berkeley_filesystem,
  open_berkeley_filesystem,
  fetch_youngest_rev,
  trivial_transaction,
  reopen_trivial_transaction,
  create_file_transaction,
  verify_txn_list,
  write_and_read_file,
  create_mini_tree_transaction,
  create_greek_tree_transaction,
  list_directory,
  verify_txn_list,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
