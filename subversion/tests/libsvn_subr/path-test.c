/*
 * path-test.c -- test the path functions
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include <apr_general.h>

#include "../svn_test.h"

/* Using a symbol, because I tried experimenting with different
   representations */
#define SVN_EMPTY_PATH ""

static svn_error_t *
test_path_is_child(const char **msg,
                   svn_boolean_t msg_only,
                   svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  int i, j;

/* The path checking code is platform specific, so we shouldn't run 
   the Windows path handling testcases on non-Windows platforms.
   */
#if defined(WIN32)
#define NUM_TEST_PATHS 16
#define RUN_NUM_TEST_PATHS NUM_TEST_PATHS
#else
#define NUM_TEST_PATHS 16
#define RUN_NUM_TEST_PATHS NUM_TEST_PATHS - 7
#endif

  static const char * const paths[NUM_TEST_PATHS] = { 
    "/foo/bar",
    "/foo/baz",
    "/foo/bar/baz",
    "/flu/blar/blaz",
    "/foo/bar/baz/bing/boom",
    SVN_EMPTY_PATH,
    "foo",
    ".foo",
    "/",
    "H:/foo/bar",
    "H:/foo/baz",
    "H:/foo/bar/baz",
    "H:/flu/blar/blaz",
    "H:/foo/bar/baz/bing/boom",
    "H:/",
    "H:/iota"
    };
  
  static const char * const remainders[NUM_TEST_PATHS][NUM_TEST_PATHS] = {
    { 0, 0, "baz", 0, "baz/bing/boom", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, "bing/boom", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, "foo", ".foo", 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "foo/bar", "foo/baz", "foo/bar/baz", "flu/blar/blaz",
      "foo/bar/baz/bing/boom", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "baz", 0, "baz/bing/boom", 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "bing/boom", 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, "foo/bar", "foo/baz", "foo/bar/baz", 
      "flu/blar/blaz", "foo/bar/baz/bing/boom", 0, "iota" },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
  };
  
  *msg = "test svn_path_is_child";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < RUN_NUM_TEST_PATHS; i++)
    {
      for (j = 0; j < RUN_NUM_TEST_PATHS; j++)
        {
          const char *remainder;

          remainder = svn_path_is_child(paths[i], paths[j], pool);

          if (((remainder) && (! remainders[i][j]))
              || ((! remainder) && (remainders[i][j]))
              || (remainder && strcmp(remainder, remainders[i][j])))
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, NULL,
               "svn_path_is_child (%s, %s) returned '%s' instead of '%s'",
               paths[i], paths[j], 
               remainder ? remainder : "(null)",
               remainders[i][j] ? remainders[i][j] : "(null)" );
        }
    }
#undef NUM_TEST_PATHS
  return SVN_NO_ERROR;
}


static svn_error_t *
test_path_split(const char **msg,
                svn_boolean_t msg_only,
                svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  apr_size_t i;

  static const char * const paths[][3] = { 
    { "/foo/bar",        "/foo",          "bar" },
    { "/foo/bar/ ",       "/foo/bar",      " " },
    { "/foo",            "/",             "foo" },
    { "foo",             SVN_EMPTY_PATH,  "foo" },
    { ".bar",            SVN_EMPTY_PATH,  ".bar" },
    { "/.bar",           "/",             ".bar" },
    { "foo/bar",         "foo",           "bar" },
    { "/foo/bar",        "/foo",          "bar" },
    { "foo/bar",         "foo",           "bar" },
    { "foo./.bar",       "foo.",          ".bar" },
    { "../foo",          "..",            "foo" },
    { SVN_EMPTY_PATH,   SVN_EMPTY_PATH,   SVN_EMPTY_PATH },
    { "/flu\\b/\\blarg", "/flu\\b",       "\\blarg" },
    { "/",               "/",             "/" },
#if defined(WIN32)
    { "X:/",             "X:/",           "X:/" },
    { "X:/foo",          "X:/",           "foo" },
#endif /* WIN32 */
  };
  
  *msg = "test svn_path_split";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
    {
      const char *dir, *base_name;

      svn_path_split(paths[i][0], &dir, &base_name, pool);
      if (strcmp(dir, paths[i][1]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "svn_path_split (%s) returned dirname '%s' instead of '%s'",
             paths[i][0], dir, paths[i][1]);
        }
      if (strcmp(base_name, paths[i][2]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "svn_path_split (%s) returned basename '%s' instead of '%s'",
             paths[i][0], base_name, paths[i][2]);
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
test_is_url(const char **msg,
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
    { "://blah/blah",                     FALSE },
    { "a:abb://boo/",                     FALSE },
    { "http://svn.collab.net/repos/svn",  TRUE  },
    { "scheme/with://slash/",             FALSE },
    { "file:///path/to/repository",       TRUE  },
    { "file://",                          TRUE  },
    { "file:/",                           FALSE },
  };

  *msg = "test svn_path_is_url";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_boolean_t retval;

      retval = svn_path_is_url(tests[i].path);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_is_url (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE", 
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_is_uri_safe(const char **msg,
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
    { "http://svn.collab.net/repos",        TRUE  },
    { "http://svn.collab.net/repos%",       FALSE },
    { "http://svn.collab.net/repos%/svn",   FALSE },
    { "http://svn.collab.net/repos%2g",     FALSE },
    { "http://svn.collab.net/repos%2g/svn", FALSE },
    { "http://svn.collab.net/repos%%",      FALSE },
    { "http://svn.collab.net/repos%%/svn",  FALSE },
    { "http://svn.collab.net/repos%2a",     TRUE  },
    { "http://svn.collab.net/repos%2a/svn", TRUE  },
  };

  *msg = "test svn_path_is_uri_safe";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < (sizeof(tests) / sizeof(tests[0])); i++)
    {
      svn_boolean_t retval;

      retval = svn_path_is_uri_safe(tests[i].path);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_is_uri_safe (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE", 
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_uri_encode(const char **msg,
                svn_boolean_t msg_only,
                svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  int i;

  struct {
    const char *path;
    const char *result;
  } tests[] = {
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
      const char *en_path, *de_path;

      /* URI-encode the path, and verify the results. */
      en_path = svn_path_uri_encode(tests[i].path, pool);
      if (strcmp(en_path, tests[i].result))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "svn_path_uri_encode ('%s') returned '%s' instead of '%s'",
             tests[i].path, en_path, tests[i].result);
        }
 
      /* URI-decode the path, and make sure we're back where we started. */
      de_path = svn_path_uri_decode(en_path, pool);
      if (strcmp(de_path, tests[i].path))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "svn_path_uri_decode ('%s') returned '%s' instead of '%s'",
             tests[i].result, de_path, tests[i].path);
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
test_uri_decode(const char **msg,
                svn_boolean_t msg_only,
                svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  int i;

  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "http://c.r.a/s%\0008me", 
         "http://c.r.a/s%"},
    { "http://c.r.a/s%6\000me",
         "http://c.r.a/s%6" },
    { "http://c.r.a/s%68me",
         "http://c.r.a/shme" },
  };
  
  *msg = "test svn_path_uri_decode with invalid escape";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 3; i++)
    {
      const char *de_path;

      /* URI-decode the path, and verify the results. */
      de_path = svn_path_uri_decode(tests[i].path, pool);
      if (strcmp(de_path, tests[i].result))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "svn_path_uri_decode ('%s') returned '%s' instead of '%s'",
             tests[i].path, de_path, tests[i].result);
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
test_uri_autoescape(const char **msg,
                    svn_boolean_t msg_only,
                    svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "http://svn.collab.net/", "http://svn.collab.net/" },
    { "file:///<>\" {}|\\^`", "file:///%3C%3E%22%20%7B%7D%7C%5C%5E%60" },
    { "http://[::1]", "http://[::1]" }
  };
  int i;

  *msg = "test svn_path_uri_autoescape";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 3; ++i)
    {
      const char* uri = svn_path_uri_autoescape(tests[i].path, pool);
      if (strcmp(uri, tests[i].result) != 0)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_uri_autoescape on '%s' returned '%s' instead of '%s'",
           tests[i].path, uri, tests[i].result);
      if (strcmp(tests[i].path, tests[i].result) == 0
          && tests[i].path != uri)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_uri_autoescape on '%s' returned identical but not same"
           " string", tests[i].path);
    }
                                  
  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_from_iri(const char **msg,
                  svn_boolean_t msg_only,
                  svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  /* We have to code the IRIs like this because the compiler might translate
     character and string literals outside of ASCII to some character set,
     but here we are hard-coding UTF-8.  But we all read UTF-8 codes like
     poetry, don't we. */
  static const char p1[] = {
    '\x66', '\x69', '\x6C', '\x65', '\x3A', '\x2F', '\x2F', '\x2F',
    '\x72', '\xC3', '\xA4', '\x6B', '\x73', '\x6D', '\xC3', '\xB6', '\x72',
    '\x67', '\xC3', '\xA5', '\x73', '\0' };
  static const char p2[] = {
    '\x66', '\x69', '\x6C', '\x65', '\x3A', '\x2F', '\x2F', '\x2F',
    '\x61', '\x62', '\x25', '\x32', '\x30', '\x63', '\x64', '\0' };
  static const char *paths[2][2] = {
    { p1,
      "file:///r%C3%A4ksm%C3%B6rg%C3%A5s" },
    { p2,
      "file:///ab%20cd" }
  };
  int i;

  *msg = "test svn_path_uri_from_iri";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 2; ++i)
    {
      const char *uri = svn_path_uri_from_iri(paths[i][0], pool);
      if (strcmp(paths[i][1], uri) != 0)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_uri_from_iri on '%s' returned '%s' instead of '%s'",
           paths[i][0], uri, paths[i][1]);
      if (strcmp(paths[i][0], uri) == 0
          && paths[i][0] != uri)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_uri_from_iri on '%s' returned identical but not same"
           " string", paths[i][0]);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_join(const char **msg,
          svn_boolean_t msg_only,
          svn_test_opts_t *opts,
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
    { SVN_EMPTY_PATH, "/", "/" },
    { "/", SVN_EMPTY_PATH, "/" },
    { SVN_EMPTY_PATH, "abc", "abc" },
    { "abc", SVN_EMPTY_PATH, "abc" },
    { SVN_EMPTY_PATH, "/abc", "/abc" },
    { SVN_EMPTY_PATH, SVN_EMPTY_PATH, SVN_EMPTY_PATH },
#if defined(WIN32)
    { "X:/",SVN_EMPTY_PATH, "X:/" },
    { "X:/","abc", "X:/abc" },
    { "X:/", "/def", "/def" },
    { "X:/abc", "/d", "/d" },
    { "X:/abc", "/", "/" },
    { "X:/abc", "X:/", "X:/" },
    { "X:/abc", "X:/def", "X:/def" },
    { "X:",SVN_EMPTY_PATH, "X:" },
    { "X:","abc", "X:abc" },
    { "X:", "/def", "/def" },
    { "X:abc", "/d", "/d" },
    { "X:abc", "/", "/" },
    { "X:abc", "X:/", "X:/" },
    { "X:abc", "X:/def", "X:/def" },
#endif /* WIN32 */
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
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_join(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);

      result = svn_path_join_many(pool, base, comp, NULL);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_join_many(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);
    }

#define TEST_MANY(args, expect) \
  result = svn_path_join_many args ; \
  if (strcmp(result, expect) != 0) \
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL, \
                             "svn_path_join_many" #args " returns \"%s\". " \
                             "expected \"%s\"", \
                             result, expect); \
  else

  TEST_MANY((pool, "abc", NULL), "abc");
  TEST_MANY((pool, "/abc", NULL), "/abc");
  TEST_MANY((pool, "/", NULL), "/");

  TEST_MANY((pool, "abc", "def", "ghi", NULL), "abc/def/ghi");
  TEST_MANY((pool, "abc", "/def", "ghi", NULL), "/def/ghi");
  TEST_MANY((pool, "/abc", "def", "ghi", NULL), "/abc/def/ghi");
  TEST_MANY((pool, "abc", "def", "/ghi", NULL), "/ghi");
  TEST_MANY((pool, "/", "def", "/ghi", NULL), "/ghi");
  TEST_MANY((pool, "/", "/def", "/ghi", NULL), "/ghi");

  TEST_MANY((pool, SVN_EMPTY_PATH, "def", "ghi", NULL), "def/ghi");
  TEST_MANY((pool, "abc", SVN_EMPTY_PATH, "ghi", NULL), "abc/ghi");
  TEST_MANY((pool, "abc", "def", SVN_EMPTY_PATH, NULL), "abc/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "def", SVN_EMPTY_PATH, NULL), "def");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "ghi", NULL), "ghi");
  TEST_MANY((pool, "abc", SVN_EMPTY_PATH, SVN_EMPTY_PATH, NULL), "abc");
  TEST_MANY((pool, SVN_EMPTY_PATH, "def", "/ghi", NULL), "/ghi");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "/ghi", NULL), "/ghi");

  TEST_MANY((pool, "/", "def", "ghi", NULL), "/def/ghi");
  TEST_MANY((pool, "abc", "/", "ghi", NULL), "/ghi");
  TEST_MANY((pool, "abc", "def", "/", NULL), "/");
  TEST_MANY((pool, "/", "/", "ghi", NULL), "/ghi");
  TEST_MANY((pool, "/", "/", "/", NULL), "/");
  TEST_MANY((pool, "/", SVN_EMPTY_PATH, "ghi", NULL), "/ghi");
  TEST_MANY((pool, "/", "def", SVN_EMPTY_PATH, NULL), "/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "/", "ghi", NULL), "/ghi");
  TEST_MANY((pool, "/", SVN_EMPTY_PATH, SVN_EMPTY_PATH, NULL), "/");
  TEST_MANY((pool, SVN_EMPTY_PATH, "/", SVN_EMPTY_PATH, NULL), "/");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "/", NULL), "/");

#if defined(WIN32)
  TEST_MANY((pool, "X:/", "def", "ghi", NULL), "X:/def/ghi");
  TEST_MANY((pool, "abc", "X:/", "ghi", NULL), "X:/ghi");
  TEST_MANY((pool, "abc", "def", "X:/", NULL), "X:/");
  TEST_MANY((pool, "X:/", "X:/", "ghi", NULL), "X:/ghi");
  TEST_MANY((pool, "X:/", "X:/", "/", NULL), "/");
  TEST_MANY((pool, "X:/", SVN_EMPTY_PATH, "ghi", NULL), "X:/ghi");
  TEST_MANY((pool, "X:/", "def", SVN_EMPTY_PATH, NULL), "X:/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:/", "ghi", NULL), "X:/ghi");
  TEST_MANY((pool, "X:/", SVN_EMPTY_PATH, SVN_EMPTY_PATH, NULL), "X:/");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:/", SVN_EMPTY_PATH, NULL), "X:/");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "X:/", NULL), "X:/");

  TEST_MANY((pool, "X:", "def", "ghi", NULL), "X:def/ghi");
  TEST_MANY((pool, "X:", "X:/", "ghi", NULL), "X:/ghi");
  TEST_MANY((pool, "X:", "X:/", "/", NULL), "/");
  TEST_MANY((pool, "X:", SVN_EMPTY_PATH, "ghi", NULL), "X:ghi");
  TEST_MANY((pool, "X:", "def", SVN_EMPTY_PATH, NULL), "X:def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:", "ghi", NULL), "X:ghi");
#endif /* WIN32 */

  /* ### probably need quite a few more tests... */

  return SVN_NO_ERROR;
}


static svn_error_t *
test_basename(const char **msg,
              svn_boolean_t msg_only,
              svn_test_opts_t *opts,
              apr_pool_t *pool)
{
  int i;
  char *result;

  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "abc", "abc" },
    { "/abc", "abc" },
    { "/abc", "abc" },
    { "/x/abc", "abc" },
    { "/xx/abc", "abc" },
    { "/xx/abc", "abc" },
    { "/xx/abc", "abc" },
    { "a", "a" },
    { "/a", "a" },
    { "/b/a", "a" },
    { "/b/a", "a" },
    { "/", "/" },
    { SVN_EMPTY_PATH, SVN_EMPTY_PATH },
#if defined(WIN32)
    { "X:/", "X:/" },
    { "X:/abc", "abc" },
    { "X:", "X:" },
    { "X:abc", "abc" },
#endif /* WIN32 */
  };

  *msg = "test svn_path_basename";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = sizeof(tests) / sizeof(tests[0]); i--; )
    {
      const char *path = tests[i].path;
      const char *expect = tests[i].result;

      result = svn_path_basename(path, pool);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_basename(\"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 path, result, expect);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_dirname(const char **msg,
             svn_boolean_t msg_only,
             svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  int i;
  char *result;

  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "abc", "" },
    { "/abc", "/" },
    { "/x/abc", "/x" },
    { "/xx/abc", "/xx" },
    { "a", "" },
    { "/a", "/" },
    { "/b/a", "/b" },
    { "/", "/" },
    { SVN_EMPTY_PATH, SVN_EMPTY_PATH },
#if defined(WIN32)
    { "X:/", "X:/" },
    { "X:/abc", "X:/" },
    { "X:abc", "X:" },
    { "X:", "X:" },
#else
    /* on non-Windows platforms, ':' is allowed in pathnames */
    { "X:", "" },
    { "X:abc", "" },
#endif /* WIN32 */
  };

  *msg = "test svn_path_dirname";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = sizeof(tests) / sizeof(tests[0]); i--; )
    {
      const char *path = tests[i].path;
      const char *expect = tests[i].result;

      result = svn_path_dirname(path, pool);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_dirname(\"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 path, result, expect);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_decompose(const char **msg,
               svn_boolean_t msg_only,
               svn_test_opts_t *opts,
               apr_pool_t *pool)
{
  static const char * const paths[] = {
    "/", "/", NULL,
    "foo", "foo", NULL,
    "/foo", "/", "foo", NULL,
    "/foo/bar", "/", "foo", "bar", NULL,
    "foo/bar", "foo", "bar", NULL,

    /* Are these canonical? Should the middle bits produce SVN_EMPTY_PATH? */
    "foo/bar", "foo", "bar", NULL,
    NULL,
  };
  int i = 0;

  *msg = "test svn_path_decompose";
  if (msg_only)
    return SVN_NO_ERROR;

  for (;;)
    {
      if (! paths[i])
        break;
      else
        {
          apr_array_header_t *components = svn_path_decompose(paths[i], pool);
          int j;
          for (j = 0; j < components->nelts; ++j)
            {
              const char *component = APR_ARRAY_IDX(components, j, const char*);
              if (! paths[i+j+1])
                return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                         "svn_path_decompose(\"%s\") returned "
                                         "unexpected component \"%s\"",
                                         paths[i], component);
              if (strcmp(component, paths[i+j+1])) 
                return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                         "svn_path_decompose(\"%s\") returned "
                                         "\"%s\" expected \"%s\"",
                                         paths[i], component, paths[i+j+1]);
            }
          if (paths[i+j+1])
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "svn_path_decompose(\"%s\") failed "
                                     "to return \"%s\"",
                                     paths[i], paths[i+j+1]);
          i += components->nelts + 2;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_canonicalize(const char **msg,
                  svn_boolean_t msg_only,
                  svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "",                     "" },
    { ".",                    "" },
    { "/",                    "/" },
    { "/.",                   "/" },
    { "./",                   "" },
    { "./.",                  "" },
    { "//",                   "/" },
    { "/////",                "/" },
    { "./././.",              "" },
    { "////././.",            "/" },
    { "foo",                  "foo" },
    { ".foo",                 ".foo" },
    { "foo.",                 "foo." },
    { "/foo",                 "/foo" },
    { "foo/",                 "foo" },
    { "foo./",                "foo." },
    { "foo./.",               "foo." },
    { "foo././/.",            "foo." },
    { "/foo/bar",             "/foo/bar" },
    { "foo/..",               "foo/.." },
    { "foo/../",              "foo/.." },
    { "foo/../.",             "foo/.." },
    { "foo//.//bar",          "foo/bar" },
    { "///foo",               "/foo" },
    { "/.//./.foo",           "/.foo" },
    { ".///.foo",             ".foo" },
    { "../foo",               "../foo" },
    { "../../foo/",           "../../foo" },
    { "../../foo/..",         "../../foo/.." },
    { "/../../",              "/../.." },
    { "http://hst",           "http://hst" },
    { "http://hst/foo/../bar","http://hst/foo/../bar" },
    { "http://hst/",          "http://hst" },
#if defined(WIN32) || defined(__CYGWIN__)
    /* We permit UNC paths on Windows.  By definition UNC
     * paths must have two components so we should remove the
     * double slash if there is only one component. */
    { "//hst/foo",            "//hst/foo" },
    { "//hst",                "/hst" },
    { "//hst/./",             "/hst" },
    { "X:/foo",               "X:/foo" },
    { "X:/",                  "X:/" },
    { "X:",                   "X:" },
    { "X:foo",                "X:foo" },
#endif /* WIN32 or Cygwin */
    { NULL, NULL }
  };
  int i;

  *msg = "test svn_path_canonicalize";
  if (msg_only)
    return SVN_NO_ERROR;

  i = 0;
  while (tests[i].path)
    {
      const char *canonical = svn_path_canonicalize(tests[i].path, pool);

      if (strcmp(canonical, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_canonicalize(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, canonical, tests[i].result);
      ++i;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_remove_component(const char **msg,
                      svn_boolean_t msg_only,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "",                     "" },
    { "/",                    "/" },
    { "foo",                  "" },
    { "foo/bar",              "foo" },
    { "/foo/bar",             "/foo" },
    { "/foo",                 "/" },
#if defined(WIN32)
    { "X:/foo/bar",           "X:/foo" },
    { "X:/foo",               "X:/" },
    { "X:/",                  "X:/" },
    { "X:foo",                "X:" },
    { "X:",                   "X:" },
#endif /* WIN32 */
    { NULL, NULL }
  };
  int i;
  svn_stringbuf_t *buf;

  *msg = "test svn_path_remove_component";
  if (msg_only)
    return SVN_NO_ERROR;

  buf = svn_stringbuf_create("", pool);
  
  i = 0;
  while (tests[i].path)
    {
      svn_stringbuf_set(buf, tests[i].path);

      svn_path_remove_component(buf);
      
      if (strcmp(buf->data, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_remove_component(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, buf->data, tests[i].result);
      ++i;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_is_root(const char **msg,
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
#if defined(WIN32)
    { "X:/foo",        FALSE },
    { "X:/",           TRUE },
    { "X:foo",         FALSE },
    { "X:",            TRUE },
#endif /* WIN32 */
    { "",              FALSE },
  };

  *msg = "test svn_path_is_root";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_boolean_t retval;

      retval = svn_path_is_root(tests[i].path, strlen(tests[i].path), pool);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_is_root (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE", 
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_is_absolute(const char **msg,
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
    { "/foo/bar",      TRUE },
    { "/foo",          TRUE },
    { "/",             TRUE },
    { "foo/bar",       FALSE },
    { "foo",           FALSE },
#if defined(WIN32)
    { "X:/foo",        TRUE },
    { "X:/",           TRUE },
    { "X:foo",         TRUE },
    { "X:foo/bar",     TRUE },
    { "X:",            TRUE },
#endif /* WIN32 */
    { "",              FALSE },
  };

  *msg = "test svn_path_is_absolute";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_boolean_t retval;

      retval = svn_path_is_absolute(tests[i].path, strlen(tests[i].path), pool);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_is_absolute (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE", 
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_path_check_valid(const char **msg,
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
    { "/foo/bar",      TRUE },
    { "/foo",          TRUE },
    { "/",             TRUE },
    { "foo/bar",       TRUE },
    { "foo bar",       TRUE },
    { "foo\7bar",      FALSE },
    { "foo\31bar",     FALSE },
    { "\7foo\31bar",   FALSE },
    { "\7",            FALSE },
    { "",              TRUE },
  };

  *msg = "test svn_path_check_valid";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_error_t *err = svn_path_check_valid(tests[i].path, pool);
      svn_boolean_t retval = (err == SVN_NO_ERROR);

      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_check_valid (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE", 
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_path_is_ancestor(const char **msg,
                      svn_boolean_t msg_only,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  apr_size_t i;

  /* Paths to test and their expected results. */
  struct { 
    const char *path1;
    const char *path2;
    svn_boolean_t result;
  } tests[] = {
    { "/foo",            "/foo/bar",      TRUE},
    { "/foo/bar",        "/foo/bar/",     TRUE},
    { "/",               "/foo",          TRUE},
    { SVN_EMPTY_PATH,    "foo",           TRUE},
    { SVN_EMPTY_PATH,    ".bar",          TRUE},
                                         
    { "/.bar",           "/",             FALSE},
    { "foo/bar",         "foo",           FALSE},
    { "/foo/bar",        "/foo",          FALSE},
    { "foo",             "foo/bar",       TRUE},
    { "foo.",            "foo./.bar",     TRUE},
                                         
    { "../foo",          "..",            FALSE},
    { SVN_EMPTY_PATH,    SVN_EMPTY_PATH,  TRUE},
    { "/",               "/",             TRUE},
                                         
#if defined(WIN32)
    { "X:/",             "X:/",           TRUE},
    { "X:/foo",          "X:/",           FALSE},
    { "X:/",             "X:/foo",        TRUE},
    { "X:",              "X:foo",         TRUE},
    { "X:foo",           "X:bar",         FALSE},
#endif /* WIN32 */
  };

  *msg = "test svn_path_is_ancestor";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_boolean_t retval;

      retval = svn_path_is_ancestor(tests[i].path1, tests[i].path2);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_is_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path1, tests[i].path2, retval ? "TRUE" : "FALSE", 
           tests[i].result ? "TRUE" : "FALSE");
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_is_single_path_component(const char **msg,
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
    { "/",             FALSE },
    { "foo/bar",       FALSE },
    { "foo",           TRUE },
    { ".",             TRUE },
    { "..",            FALSE },
    { "",              FALSE },
  };

  *msg = "test svn_path_is_single_path_component";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_boolean_t retval;

      retval = svn_path_is_single_path_component(tests[i].path);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_is_single_path_component (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE", 
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_path_is_child),
    SVN_TEST_PASS(test_path_split),
    SVN_TEST_PASS(test_is_url),
    SVN_TEST_PASS(test_is_uri_safe),
    SVN_TEST_PASS(test_uri_encode),
    SVN_TEST_PASS(test_uri_decode),
    SVN_TEST_PASS(test_uri_autoescape),
    SVN_TEST_PASS(test_uri_from_iri),
    SVN_TEST_PASS(test_join),
    SVN_TEST_PASS(test_basename),
    SVN_TEST_PASS(test_dirname),
    SVN_TEST_PASS(test_decompose),
    SVN_TEST_PASS(test_canonicalize),
    SVN_TEST_PASS(test_remove_component),
    SVN_TEST_PASS(test_is_root),
    SVN_TEST_PASS(test_is_absolute),
    SVN_TEST_PASS(test_path_is_ancestor),
    SVN_TEST_PASS(test_path_check_valid),
    SVN_TEST_PASS(test_is_single_path_component),
    SVN_TEST_NULL
  };
