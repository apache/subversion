/*
 * ra-dav-local.c :  basic tests for the RA LOCAL library
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



#include <apr_general.h>
#include <apr_pools.h>

/* Commenting out the following includes by default since they aren't
   perfectly portable.  */
#ifdef ENABLE_SPLIT_URL_TESTS
#include <unistd.h> /* for getcwd() */
#include <string.h> /* for strcat() */
#endif /* ENABLE_SPLIT_URL_TESTS */

#include "svn_string.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_fs.h"
#include "svn_client.h"
#include "svn_test.h"
#include "../../libsvn_ra_local/ra_local.h"

/* Notice that we're including the FS API above.  This isn't because
   the RA API needs to know about it;  rather, it's so our tests can
   create a repository to play with.  After all, ra_local is all about
   reading repositories directly. */


/* A global pool, initialized by `main' for tests to use.  */
apr_pool_t *pool;


#define NOT_FIXED_YET

#ifndef NOT_FIXED_YET

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
  svn_fs_set_warning_func (*fs_p, svn_handle_warning, stderr);

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
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL,
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
get_ra_local_plugin (svn_ra_plugin_t **plugin)
{
  void *ra_baton;

  /* Load all available RA implementations. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));

  /* Get the plugin which handles "file:" URLs */
  SVN_ERR (svn_ra_get_ra_library (plugin, ra_baton, "file:", pool));

  return SVN_NO_ERROR;
}


/*-------------------------------------------------------------------*/

/** The tests **/

/* Open an ra session to a local repository. */
static svn_error_t *
open_ra_session (const char **msg)
{
  svn_fs_t *fs;
  svn_ra_plugin_t *plugin;
  void *session;

  *msg = "open an ra session to a local repository.";

  /* Create a repository and get the ra_local vtable. */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-open"));
  SVN_ERR (get_ra_local_plugin (&plugin));

  /* Open an ra session into this repository. */
  SVN_ERR (plugin->open (&session,
                         svn_stringbuf_create ("file:test-repo-open", pool),
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
  svn_ra_plugin_t *plugin;
  void *session;
  svn_revnum_t latest_rev;

  *msg = "get the youngest revision in a repository";

  /* Create a repository and get the ra_local vtable. */
  SVN_ERR (create_fs_and_repos (&fs, "test-repo-getrev"));
  SVN_ERR (get_ra_local_plugin (&plugin));

  /* Open an ra session into this repository. */
  SVN_ERR (plugin->open (&session,
                         svn_stringbuf_create ("file:test-repo-getrev", pool),
                         pool));

  /* Get the youngest revision and make sure it's 0. */
  SVN_ERR (plugin->get_latest_revnum (session, &latest_rev));
  
  if (latest_rev != 0)
      return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL,
                               "youngest rev isn't 0!");

  /* Close the session. */
  SVN_ERR (plugin->close (session));


  return SVN_NO_ERROR;
}

#endif /* ! NOT_FIXED_YET */

#ifdef ENABLE_SPLIT_URL_TESTS

/* Helper function.  Run svn_ra_local__split_URL with interest only in
   the return value, not the populated path items */
static svn_error_t *
try_split_url (const char *url)
{
  svn_stringbuf_t *repos_path, *fs_path;

  SVN_ERR (svn_ra_local__split_URL (&repos_path, &fs_path, 
                                    svn_stringbuf_create (url, pool),
                                    pool));
  return SVN_NO_ERROR;
}



static svn_error_t *
split_url_test_1 (const char **msg)
{
  svn_error_t *err;
  
  *msg = "test svn_ra_local__split_URL's URL-validating abilities";

  /* TEST 1:  Make sure we can recognize bad URLs (this should not
     require a filesystem) */

  /* Use `blah' for scheme instead of `file' */
  err = try_split_url ("blah:///bin/svn/");
  if (err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, 0, NULL,
       "svn_ra_local__split_URL failed to catch bad URL (scheme)");

  /* Use only single slash after scheme */
  err = try_split_url ("file:/path/to/repos/");
  if (err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, 0, NULL,
       "svn_ra_local__split_URL failed to catch bad URL (slashes)");
  
  /* Use only a hostname, with no path */  
  err = try_split_url ("file://hostname");
  if (err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, 0, NULL,
       "svn_ra_local__split_URL failed to catch bad URL (no path)");

  /* Give a hostname other than `' or `localhost' */
  err = try_split_url ("file://myhost/repos/path/");
  if (err->apr_err != SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, 0, NULL,
       "svn_ra_local__split_URL failed to catch bad URL (hostname)");

  /* Make sure we *don't* fuss about a good URL (note that this URL
     still doesn't point to an existing versioned resource) */
  err = try_split_url ("file:///repos/path/");
  if (err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, 0, NULL,
       "svn_ra_local__split_URL cried foul about a good URL (no hostname)");
  err = try_split_url ("file://localhost/repos/path/");
  if (err->apr_err == SVN_ERR_RA_ILLEGAL_URL)
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, 0, NULL,
       "svn_ra_local__split_URL cried foul about a good URL (localhost)");

  return SVN_NO_ERROR;
}


/* Helper function.  Creates a filesystem in the current working
   directory named FS_PATH, then assembes a URL that points to that
   FS, plus addition cruft (REPOS_PATH) that theoretically refers to a
   versioned resource in that filesystem.  Finally, it runs this URL
   through svn_ra_local__split_URL to verify that it accurately
   separates the filesystem path and the repository path cruft. */
static svn_error_t *
check_split_url (const char *repos_path,
                 const char *fs_path)
{
  svn_fs_t *fs;
  char repos_loc[PATH_MAX], url[PATH_MAX];
  svn_stringbuf_t *repos_part, *fs_part;

  /* Because the URLs are absolute paths, we have to figure out where
     this system.   */
  getcwd (repos_loc, PATH_MAX - 1);
  strcat (repos_loc, "/");
  strcat (repos_loc, repos_path);

  /* Create a filesystem and repository */
  SVN_ERR (create_fs_and_repos (&fs, repos_loc));

  /* Now, assemble the test URL */
  sprintf (url, "file://%s%s", repos_loc, fs_path);

  /* Run this URL through our splitter... */
  SVN_ERR (svn_ra_local__split_URL (&repos_part, &fs_part, 
                                    svn_stringbuf_create (url, pool),
                                    pool));
  if ((strcmp (repos_part->data, repos_loc))
      || (strcmp (fs_part->data, fs_path)))
    return svn_error_create 
      (SVN_ERR_TEST_FAILED, 0, NULL,
       "svn_ra_local__split_URL failed to properly split the URL");
  
  return SVN_NO_ERROR;
}


static svn_error_t *
split_url_test_2 (const char **msg)
{
  *msg = "test svn_ra_local__split_URL's URL-validating abilities";

  /* TEST 2: Given well-formed URLs, make sure that we can correctly
     find where the filesystem portion of the path ends and the
     repository path begins.  */
  SVN_ERR (check_split_url ("test-repo-split-fs1",
                            "/path/to/repos"));
  SVN_ERR (check_split_url ("test-repo-split-fs2",
                            "/big/old/long/path/to/my/other/repository"));

  return SVN_NO_ERROR;
}

#endif /* ENABLE_SPLIT_URL_TESTS */




/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
#ifdef ENABLE_SPLIT_URL_TESTS
    SVN_TEST_PASS (split_url_test_1),
    SVN_TEST_PASS (split_url_test_2),
#endif /* ENABLE_SPLIT_URL_TESTS */
#ifndef NOT_FIXED_YET
    SVN_TEST_PASS (open_ra_session),
    SVN_TEST_PASS (get_youngest_rev),
#endif /* NOT_FIXED_YET */
    SVN_TEST_NULL
  };

#undef NOT_FIXED_YET



/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
