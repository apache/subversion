/*
 * path-test.c -- test the path functions
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

#include <svn_path.h>
#include <stdio.h>
#include <apr_general.h>
#include "svn_test.h"


static svn_error_t *
test_path_is_child (const char **msg,
                    apr_pool_t *pool)
{
  int i, j;

  const char *paths[5] = { 
    "/foo/bar",
    "/foo/baz",
    "/foo/bar/baz",
    "/flu/blar/blaz",
    "/foo/bar/baz/bing/boom"
    };
  
  const char *remainders[5][5] = {
    { 0, 0, "baz", 0, "baz/bing/boom" },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, "bing/boom" },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }
  };
  
  *msg = "test svn_path_is_child.";

  for (i = 0; i < 5; i++)
    {
      for (j = 0; j < 5; j++)
        {
          svn_stringbuf_t *path1 = svn_string_create (paths[i], pool);
          svn_stringbuf_t *path2 = svn_string_create (paths[j], pool);
          svn_stringbuf_t *remainder;

          remainder = svn_path_is_child (path1, path2, 
                                         svn_path_repos_style,
                                         pool);

          if (((remainder) && (! remainders[i][j]))
              || ((! remainder) && (remainders[i][j]))
              || (remainder && strcmp (remainder->data,
                                       remainders[i][j])))
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, 0, NULL, pool,
               "svn_path_is_child (%s, %s) returned '%s' instead of '%s'",
               path1->data, path2->data, 
               remainder ? remainder->data : "(null)",
               remainders[i][j] ? remainders[i][j] : "(null)" );
        }
    }
  return SVN_NO_ERROR;
}




/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               apr_pool_t *pool) = {
  0,
  test_path_is_child,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */

