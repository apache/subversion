/*
 * path-test.c -- test the path functions
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

#include <stdio.h>
#include <string.h>
#include <svn_path.h>
#include <apr_general.h>
#include "svn_test.h"


static svn_error_t *
test_path_is_child (const char **msg,
                    svn_boolean_t msg_only,
                    apr_pool_t *pool)
{
  int i, j;

  static const char * const paths[] = { 
    "/foo/bar",
    "/foo/baz",
    "/foo/bar/baz",
    "/flu/blar/blaz",
    "/foo/bar/baz/bing/boom"
    };
  
  static const char * const remainders[][5] = {
    { 0, 0, "baz", 0, "baz/bing/boom" },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, "bing/boom" },
    { 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0 }
  };
  
  *msg = "test svn_path_is_child";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 5; i++)
    {
      for (j = 0; j < 5; j++)
        {
          svn_stringbuf_t *path1 = svn_stringbuf_create (paths[i], pool);
          svn_stringbuf_t *path2 = svn_stringbuf_create (paths[j], pool);
          svn_stringbuf_t *remainder;

          remainder = svn_path_is_child (path1, path2, pool);

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


static svn_error_t *
test_path_split (const char **msg,
                 svn_boolean_t msg_only,
                 apr_pool_t *pool)
{
  int i;

  static const char * const paths[][3] = { 
    { "/foo/bar",        "/foo",     "bar" },
    { "/foo/bar/",       "/foo",     "bar" },
    { "/foo/bar/ ",      "/foo/bar", " " },
    { "/foo",            "",         "foo" },
    { "/flu\\b/\\blarg", "/flu\\b",  "\\blarg" },
  };
  
  *msg = "test svn_path_split";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 5; i++)
    {
      svn_stringbuf_t *path = svn_stringbuf_create (paths[i][0], pool);
      svn_stringbuf_t *dir, *basename;

      svn_path_split (path, &dir, &basename, pool);

      if (strcmp (dir->data, paths[i][1]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL, pool,
             "svn_path_split (%s) returned dirname '%s' instead of '%s'",
             path->data, dir->data, paths[i][1]);
        }
      if (strcmp (basename->data, paths[i][2]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL, pool,
             "svn_path_split (%s) returned basename '%s' instead of '%s'",
             path->data, basename->data, paths[i][2]);
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
test_is_url (const char **msg,
             svn_boolean_t msg_only,
             apr_pool_t *pool)
{
  int i;

  /* Paths to test. */
  static const char * const paths[] = { 
    "://blah/blah",
    "a:abb://boo/",
    "http://svn.collab.net/repos/svn",
    "scheme/with://slash/",
    "file:///path/to/repository",
  };

  /* Expected results of the tests. */
  static const svn_boolean_t retvals[] = {
    FALSE,
    FALSE,
    TRUE,
    FALSE,
    TRUE };

  *msg = "test svn_path_is_url";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Now, do the tests. */
  for (i = 0; i < 5; i++)
    {
      svn_boolean_t retval;
      svn_string_t str;

      str.data = paths[i];
      str.len  = strlen (str.data);
      retval = svn_path_is_url (&str);
      if (retvals[i] != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, 0, NULL, pool,
           "svn_path_is_url (%s) returned %s instead of %s",
           paths[i], retvals[i] ? "TRUE" : "FALSE", retval ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_encode (const char **msg,
                 svn_boolean_t msg_only,
                 apr_pool_t *pool)
{
  int i;

  const char *paths[5][2] = { 
    { "http://subversion.tigris.org", 
         "http://subversion.tigris.org"},
    { " special_at_beginning",
         "%20special_at_beginning" },
    { "special_at_end ",
         "special_at_end%20" },
    { "special in middle",
         "special%20in%20middle" },
    { "\"Ouch!\"  \"Did that hurt?\"", 
         "%22Ouch!%22%20%20%22Did%20that%20hurt%3F%22" }
  };
  
  *msg = "test svn_path_uri_[en/de]code";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 5; i++)
    {
      svn_string_t path;
      svn_stringbuf_t *en_path, *de_path;

      /* Make a path. */
      path.data = paths[i][0];
      path.len = strlen (paths[i][0]);

      /* URI-encode the path, and verify the results. */
      en_path = svn_path_uri_encode (&path, pool);
      if (strcmp (en_path->data, paths[i][1]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL, pool,
             "svn_path_uri_encode ('%s') returned '%s' instead of '%s'",
             path.data, en_path->data, paths[i][1]);
        }
 
      /* Make a string from our stringbuf. */
      path.data = en_path->data;
      path.len = en_path->len;

      /* URI-decode the path, and make sure we're back where we started. */
      de_path = svn_path_uri_decode (&path, pool);
      if (strcmp (de_path->data, paths[i][0]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL, pool,
             "svn_path_uri_decode ('%s') returned '%s' instead of '%s'",
             path.data, de_path->data, paths[i][0]);
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
test_join (const char **msg,
           svn_boolean_t msg_only,
           apr_pool_t *pool)
{
  int i;
  char *result;

  static const char * const joins[][3] = {
    { "abc", "def", "abc/def" },
    { "a", "def", "a/def" },
    { "a", "d", "a/d" },
    { "/", "d", "/d" },
    { "/abc", "d", "/abc/d" },
    { "/abc", "def", "/abc/def" },
    { "/abc", "/def", "/def" },
    { "/abc", "/d", "/d" },
    { "/abc", "/", "/" },
    { "/abc/", "def", "/abc/def" },
    { "/abc/", "/def", "/def" },
    { "", "def", "def" },
    { "abc", "", "abc" },
    { "", "/def", "/def" },
    { "/", "", "/" },
    { "", "/", "/" },
  };

  *msg = "test svn_path_join(_many)";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = sizeof(joins) / sizeof(joins[0]); i--; )
    {
      const char *base = joins[i][0];
      const char *comp = joins[i][1];
      const char *expect = joins[i][2];

      result = svn_path_join(base, comp, pool);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                 "svn_path_join(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);

      result = svn_path_join_many(pool, base, comp, NULL);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                 "svn_path_join_many(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);
    }

#define TEST_MANY(args, expect) \
  result = svn_path_join_many args ; \
  if (strcmp(result, expect) != 0) \
    return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL, pool, \
                             "svn_path_join_many" #args " returns \"%s\". " \
                             "expected \"%s\"", \
                             result, expect); \
  else

  TEST_MANY((pool, "abc", NULL), "abc");
  TEST_MANY((pool, "abc/", NULL), "abc");
  TEST_MANY((pool, "/abc", NULL), "/abc");
  TEST_MANY((pool, "/abc/", NULL), "/abc");
  TEST_MANY((pool, "/", NULL), "/");

  TEST_MANY((pool, "abc", "def", "ghi", NULL), "abc/def/ghi");
  TEST_MANY((pool, "abc", "/def", "ghi", NULL), "/def/ghi");
  TEST_MANY((pool, "/abc", "def", "ghi", NULL), "/abc/def/ghi");
  TEST_MANY((pool, "abc", "def", "/ghi", NULL), "/ghi");
  TEST_MANY((pool, "abc", "def/", "ghi", NULL), "abc/def/ghi");
  TEST_MANY((pool, "abc/", "def", "ghi", NULL), "abc/def/ghi");
  TEST_MANY((pool, "abc", "def", "ghi/", NULL), "abc/def/ghi");

  TEST_MANY((pool, "", "def", "ghi", NULL), "def/ghi");
  TEST_MANY((pool, "abc", "", "ghi", NULL), "abc/ghi");
  TEST_MANY((pool, "abc", "def", "", NULL), "abc/def");
  TEST_MANY((pool, "", "def", "", NULL), "def");
  TEST_MANY((pool, "", "", "ghi", NULL), "ghi");
  TEST_MANY((pool, "abc", "", "", NULL), "abc");
  TEST_MANY((pool, "", "def", "", NULL), "def");

  TEST_MANY((pool, "/", "def", "ghi", NULL), "/def/ghi");
  TEST_MANY((pool, "abc", "/", "ghi", NULL), "/ghi");
  TEST_MANY((pool, "abc", "def", "/", NULL), "/");
  TEST_MANY((pool, "/", "/", "ghi", NULL), "/ghi");
  TEST_MANY((pool, "/", "/", "/", NULL), "/");

  /* ### probably need quite a few more tests... */

  return SVN_NO_ERROR;
}


static svn_error_t *
test_basename (const char **msg,
               svn_boolean_t msg_only,
               apr_pool_t *pool)
{
  int i;
  char *result;

  static const char * const paths[][2] = {
    { "abc", "abc" },
    { "/abc", "abc" },
    { "/abc/", "abc" },
    { "/abc//", "abc" },
    { "//abc", "abc" },
    { "//abc/", "abc" },
    { "//abc//", "abc" },
    { "/x/abc", "abc" },
    { "/x/abc/", "abc" },
    { "/xx/abc", "abc" },
    { "/xx//abc", "abc" },
    { "/xx//abc", "abc" },
    { "a", "a" },
    { "/a", "a" },
    { "/a/", "a" },
    { "/b/a", "a" },
    { "/b/a/", "a" },
    { "/b/a///", "a" },
    { "/b//a", "a" },
    { "/", "/" },
    { "//", "/" },
    { "///", "/" },
    { "", "" },
  };

  *msg = "test svn_path_basename";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = sizeof(paths) / sizeof(paths[0]); i--; )
    {
      const char *path = paths[i][0];
      const char *expect = paths[i][1];

      result = svn_path_basename(path, pool);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                 "svn_path_basename(\"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 path, result, expect);
    }

  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               svn_boolean_t msg_only,
                               apr_pool_t *pool) = {
  0,
  test_path_is_child,
  test_path_split,
  test_is_url,
  test_uri_encode,
  test_join,
  test_basename,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */

