/*
 * ra-dav-local.c :  basic tests for the RA LOCAL library
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



#include <apr_general.h>
#include <apr_pools.h>

#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_fs.h"

/* Notice that we're including the FS API above.  This isn't because
   the RA API needs to know about it;  rather, it's so our tests can
   create a repository to play with.  After all, ra_local is all about
   reading repositories directly. */


/* A global pool, initialized by `main' for tests to use.  */
apr_pool_t *pool;


/*-------------------------------------------------------------------*/

/** Helper routines. **/

/* (all stolen from fs-test.c) */

static void
berkeley_error_handler (const char *errpfx,
                        char *msg)
{
  fprintf (stderr, "%s%s\n", errpfx ? errpfx : "", msg);
}


/* Set *FS_P to a fresh, unopened FS object, with the right warning
   handling function set.  */
static svn_error_t *
fs_new (svn_fs_t **fs_p)
{
  *fs_p = svn_fs_new (pool);
  if (! *fs_p)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Couldn't alloc a new fs object.");

  /* Provide a warning function that just dumps the message to stderr.  */
  svn_fs_set_warning_func (*fs_p, svn_handle_warning, 0);

  return SVN_NO_ERROR;
}


/* Create a berkeley db repository in a subdir NAME, and return a new
   FS object which points to it.  */
static svn_error_t *
create_fs_and_repos (svn_fs_t **fs_p, const char *name)
{
  apr_finfo_t finfo;

  /* If there's already a repository named NAME, delete it.  Doing
     things this way means that repositories stick around after a
     failure for postmortem analysis, but also that tests can be
     re-run without cleaning out the repositories created by prior
     runs.  */
  if (apr_stat (&finfo, name, APR_FINFO_TYPE, pool) == APR_SUCCESS)
    {
      if (finfo.filetype == APR_DIR)
        SVN_ERR (svn_fs_delete_berkeley (name, pool));
      else
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                  "there is already a file named `%s'", name);
    }

  SVN_ERR (fs_new (fs_p));
  SVN_ERR (svn_fs_create_berkeley (*fs_p, name));
  
  /* Provide a handler for Berkeley DB error messages.  */
  SVN_ERR (svn_fs_set_berkeley_errcall (*fs_p, berkeley_error_handler));

  return SVN_NO_ERROR;
}


/* Utility:  return the vtable for ra_local.  */
static svn_error_t *
get_ra_local_plugin (const svn_ra_plugin_t **plugin)
{
  /* Someday, we need to rewrite svn_client_init_ra_libs() so that it
     returns a hash of loaded vtables *regardless* of whether the
     application is statically or dynamically linked!  

     When that happens, call that function here, then do a simple
     apr_hash_get() on the "file" key.  Voila.

     Instead, for now, we assume that ra-local-test is statically
     linked, and thus that svn_ra_local_init() is already in our
     address space. */

  const char *url_scheme;

  SVN_ERR (svn_ra_local_init (1, /* abi version 1 */
                              pool, &url_scheme, plugin));
  return SVN_NO_ERROR;
}



/*-------------------------------------------------------------------*/

/** The tests **/

/* Open an ra session to a local repository. */
static svn_error_t *
open_ra_session (const char **msg)
{
  svn_fs_t *fs;
  const svn_ra_plugin_t *plugin;
  void *session;

  *msg = "open an ra session to a local repository.";

  /* Create a repository and get the ra_local vtable. */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-open"));
  SVN_ERR (get_ra_local_plugin (&plugin));

  /* Open an ra session into this repository. */
  SVN_ERR (plugin->open (&session,
                         svn_string_create ("file:test-repo-open", pool),
                         pool));

  /* Close the session. */
  SVN_ERR (plugin->close (session));

  return SVN_NO_ERROR;
}




/* Discover the youngest revision in a repository.  */
static svn_error_t *
get_youngest_rev (const char **msg)
{
  svn_fs_t *fs;
  const svn_ra_plugin_t *plugin;
  void *session;
  svn_revnum_t latest_rev;

  *msg = "get the youngest revision in a repository";

  /* Create a repository and get the ra_local vtable. */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-getrev"));
  SVN_ERR (get_ra_local_plugin (&plugin));

  /* Open an ra session into this repository. */
  SVN_ERR (plugin->open (&session,
                         svn_string_create ("file:test-repo-getrev", pool),
                         pool));

  /* Get the youngest revision and make sure it's 0. */
  SVN_ERR (plugin->get_latest_revnum (session, &latest_rev));
  
  if (latest_rev != 0)
      return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                               "youngest rev isn't 0!");

  /* Close the session. */
  SVN_ERR (plugin->close (session));


  return SVN_NO_ERROR;
}







/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg) = {
  0,
  open_ra_session,
  get_youngest_rev,
  0
};





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
