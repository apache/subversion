/*
 * dirent_uri-test.c -- test the directory entry and URI functions
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

#include <stdio.h>
#include <string.h>
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include <apr_general.h>

#include "../svn_test.h"


static svn_error_t *
test_dirent_is_root(const char **msg,
                    svn_boolean_t msg_only,
                    svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  apr_size_t i;

  /* Paths to test and their expected results. */
  struct {
    const char *path;
    svn_boolean_t result;
  } tests[] = {
    { "/foo/bar",      FALSE },
    { "/foo",          FALSE },
    { "/",             TRUE },
    { "",              FALSE },
#if defined(WIN32) || defined(__CYGWIN__)
    { "X:/foo",        FALSE },
    { "X:/",           TRUE },
    { "X:foo",         FALSE },
    { "X:",            TRUE },
    { "//srv/shr",     TRUE },
    { "//srv",         TRUE },
    { "//srv/shr/fld", FALSE },
#else /* WIN32 or Cygwin */
    { "/X:foo",        FALSE },
    { "/X:",           FALSE },
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_is_root";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_boolean_t retval;

      retval = svn_dirent_is_root(tests[i].path, strlen(tests[i].path));
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_dirent_is_root (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE",
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}



/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_dirent_is_root),
    SVN_TEST_NULL
  };
