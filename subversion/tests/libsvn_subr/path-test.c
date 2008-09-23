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
#include "svn_pools.h"
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
#define NUM_TEST_PATHS 11

  static const char * const paths[NUM_TEST_PATHS] = {
    "/foo/bar",
    "/foo/bars",
    "/foo/baz",
    "/foo/bar/baz",
    "/flu/blar/blaz",
    "/foo/bar/baz/bing/boom",
    SVN_EMPTY_PATH,
    "foo",
    ".foo",
    "/",
    "foo2",
    };

  static const char * const remainders[NUM_TEST_PATHS][NUM_TEST_PATHS] = {
    { 0, 0, 0, "baz", 0, "baz/bing/boom", 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, "bing/boom", 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, "foo", ".foo", 0, "foo2" },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "foo/bar", "foo/bars", "foo/baz", "foo/bar/baz", "flu/blar/blaz",
      "foo/bar/baz/bing/boom", 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  };

  *msg = "test svn_path_is_child";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < NUM_TEST_PATHS; i++)
    {
      for (j = 0; j < NUM_TEST_PATHS; j++)
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
    { "",                                 FALSE },
    { "/blah/blah",                       FALSE },
    { "//blah/blah",                      FALSE },
    { "://blah/blah",                     FALSE },
    { "a:abb://boo/",                     FALSE },
    { "http://svn.collab.net/repos/svn",  TRUE  },
    { "scheme/with",                      FALSE },
    { "scheme/with:",                     FALSE },
    { "scheme/with:/",                    FALSE },
    { "scheme/with://",                   FALSE },
    { "scheme/with://slash/",             FALSE },
    { "file:///path/to/repository",       TRUE  },
    { "file://",                          TRUE  },
    { "file:/",                           FALSE },
    { "file:",                            FALSE },
    { "file",                             FALSE },
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
              const char *component = APR_ARRAY_IDX(components,
                                                    j,
                                                    const char*);
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
    { "foo//",                "foo" },
    { "foo///",               "foo" },
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
    { "dirA",                 "dirA" },
    { "foo/dirA",             "foo/dirA" },
    { "http://hst",           "http://hst" },
    { "http://hst/foo/../bar","http://hst/foo/../bar" },
    { "http://hst/",          "http://hst" },
    { "http:///",             "http://" },
    { "https://",             "https://" },
    { "file:///",             "file://" },
    { "file://",              "file://" },
    { "svn:///",              "svn://" },
    { "svn+ssh:///",          "svn+ssh://" },
    { "http://HST/",          "http://hst" },
    { "http://HST/FOO/BaR",   "http://hst/FOO/BaR" },
    { "svn+ssh://j.raNDom@HST/BaR", "svn+ssh://j.raNDom@hst/BaR" },
    { "svn+SSH://j.random:jRaY@HST/BaR", "svn+ssh://j.random:jRaY@hst/BaR" },
    { "SVN+ssh://j.raNDom:jray@HST/BaR", "svn+ssh://j.raNDom:jray@hst/BaR" },
    { "fILe:///Users/jrandom/wc", "file:///Users/jrandom/wc" },
#if defined(WIN32) || defined(__CYGWIN__)
    { "file:///c:/temp/repos", "file:///C:/temp/repos" },
    { "file:///c:/temp/REPOS", "file:///C:/temp/REPOS" },
    { "file:///C:/temp/REPOS", "file:///C:/temp/REPOS" },
    { "//server/share/",       "//server/share" },
    { "//server/SHare/",       "//server/SHare" },
    { "//SERVER/SHare/",       "//server/SHare" },
#else /* WIN32 or Cygwin */
    { "file:///c:/temp/repos", "file:///c:/temp/repos" },
    { "file:///c:/temp/REPOS", "file:///c:/temp/REPOS" },
    { "file:///C:/temp/REPOS", "file:///C:/temp/REPOS" },
#endif /* non-WIN32 */
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

      svn_error_clear(err);
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

    { "http://test",    "http://test",     TRUE},
    { "http://test",    "http://taste",    FALSE},
    { "http://test",    "http://test/foo", TRUE},
    { "http://test",    "file://test/foo", FALSE},
    { "http://test",    "http://testF",    FALSE},
/*
    TODO: this testcase fails, showing that svn_path_is_ancestor
    shouldn't be used on urls. This is related to issue #1711.

    { "http://",        "http://test",     FALSE},
*/
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

  /* Paths to test and their expected results.
   * Note that these paths need to be canonical,
   * else we might trigger an abort(). */
  struct {
    const char *path;
    svn_boolean_t result;
  } tests[] = {
    { "/foo/bar",      FALSE },
    { "/foo",          FALSE },
    { "/",             FALSE },
    { "foo/bar",       FALSE },
    { "foo",           TRUE },
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

static svn_error_t *
test_compare_paths(const char **msg,
                   svn_boolean_t msg_only,
                   svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  apr_size_t i;

  /* Paths to test and their expected results. */
  struct {
    const char *path1;
    const char *path2;
    int result;
  } tests[] = {
    { "/foo",         "/foo",         0},
    { "/foo/bar",     "/foo/bar",     0},
    { "/",            "/",            0},
    { SVN_EMPTY_PATH, SVN_EMPTY_PATH, 0},
    { "foo",          "foo",          0},
    { "foo",          "foo/bar",      -1},
    { "foo/bar",      "foo/boo",      -1},
    { "boo",          "foo",          -1},
    { "foo",          "boo",          1},
    { "foo/bar",      "foo",          1},
    { "/",            "/foo",         -1},
    { "/foo",         "/foo/bar",     -1},
    { "/foo",         "/foo/bar/boo", -1},
    { "foo",          "/foo",         1},
    { "foo\xe0""bar", "foo",          1},
  };

  *msg = "test svn_path_compare_paths";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      int retval;

      retval = svn_path_compare_paths(tests[i].path1, tests[i].path2);
      /* tests if expected and actual result are both < 0,
         equal to 0 or greater than 0. */
      if (! (tests[i].result * retval > 0 ||
            (tests[i].result == 0 && retval == 0)) )
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_compare_paths (%s, %s) returned %d instead of %d",
           tests[i].path1, tests[i].path2, retval, tests[i].result);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_get_longest_ancestor(const char **msg,
                          svn_boolean_t msg_only,
                          svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  apr_size_t i;

  /* Paths to test and their expected results. */
  struct {
    const char *path1;
    const char *path2;
    const char *result;
  } tests[] = {
    { "/foo",           "/foo/bar",        "/foo"},
    { "/foo/bar",       "foo/bar",         ""},
    { "/",              "/foo",            "/"},
    { SVN_EMPTY_PATH,   "foo",             SVN_EMPTY_PATH},
    { SVN_EMPTY_PATH,   ".bar",            SVN_EMPTY_PATH},
    { "/.bar",          "/",               "/"},
    { "foo/bar",        "foo",             "foo"},
    { "/foo/bar",       "/foo",            "/foo"},
    { "/rif",           "/raf",            "/"},
    { "foo",            "foo/bar",         "foo"},
    { "foo.",           "foo./.bar",       "foo."},
    { SVN_EMPTY_PATH,   SVN_EMPTY_PATH,    SVN_EMPTY_PATH},
    { "/",              "/",               "/"},
    { "http://test",    "http://test",     "http://test"},
    { "http://test",    "http://taste",    ""},
    { "http://test",    "http://test/foo", "http://test"},
    { "http://test",    "file://test/foo", ""},
    { "http://test",    "http://testF",    ""},
    { "http://",        "http://test",     ""},
  };

  *msg = "test svn_path_get_longest_ancestor";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      const char *retval;

      retval = svn_path_get_longest_ancestor(tests[i].path1, tests[i].path2,
                                             pool);

      if (strcmp(tests[i].result, retval))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_get_longest_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path1, tests[i].path2, retval, tests[i].result);

      /* changing the order of the paths should return the same results */
      retval = svn_path_get_longest_ancestor(tests[i].path2, tests[i].path1,
                                             pool);

      if (strcmp(tests[i].result, retval))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_get_longest_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path2, tests[i].path1, retval, tests[i].result);
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
test_splitext(const char **msg,
              svn_boolean_t msg_only,
              svn_test_opts_t *opts,
              apr_pool_t *pool)
{
  apr_size_t i;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Paths to test and their expected results. */
  struct {
    const char *path;
    const char *path_root;
    const char *path_ext;
    svn_boolean_t result;
  } tests[] = {
    { "no-ext",                    "no-ext",                 "" },
    { "test-file.py",              "test-file.",             "py" },
    { "period.file.ext",           "period.file.",           "ext" },
    { "multi-component/file.txt",  "multi-component/file.",  "txt" },
    { "yep.still/no-ext",          "yep.still/no-ext",       "" },
    { "folder.with/period.log",    "folder.with/period.",    "log" },
    { "period.",                   "period.",                "" },
    { "file.ends-with/period.",    "file.ends-with/period.", "" },
    { "two-periods..txt",          "two-periods..",          "txt" },
    { ".dot-file",                 ".dot-file",              "" },
    { "sub/.dot-file",             "sub/.dot-file",          "" },
    { ".dot-file.withext",         ".dot-file.",             "withext" },
    { "sub/.dot-file.withext",     "sub/.dot-file.",         "withext" },
    { "sub/a.out",                 "sub/a.",                 "out" },
    { "a.out",                     "a.",                     "out" },
    { "",                          "",                       "" },
  };

  *msg = "test svn_path_splitext";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      const char *path = tests[i].path;
      const char *path_root;
      const char *path_ext;

      svn_pool_clear(subpool);

      /* First, we'll try splitting and fetching both root and
         extension to see if they match our expected results. */
      svn_path_splitext(&path_root, &path_ext, path, subpool);
      if ((strcmp(tests[i].path_root, path_root))
          || (strcmp(tests[i].path_ext, path_ext)))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_splitext (%s) returned ('%s', '%s') "
           "instead of ('%s', '%s')",
           tests[i].path, path_root, path_ext,
           tests[i].path_root, tests[i].path_ext);

      /* Now, let's only fetch the root. */
      svn_path_splitext(&path_root, NULL, path, subpool);
      if (strcmp(tests[i].path_root, path_root))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_splitext (%s) with a NULL path_ext returned '%s' "
           "for the path_root instead of '%s'",
           tests[i].path, path_root, tests[i].path_root);

      /* Next, let's only fetch the extension. */
      svn_path_splitext(NULL, &path_ext, path, subpool);
      if ((strcmp(tests[i].path_root, path_root))
          || (strcmp(tests[i].path_ext, path_ext)))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_path_splitext (%s) with a NULL path_root returned '%s' "
           "for the path_ext instead of '%s'",
           tests[i].path, path_ext, tests[i].path_ext);
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
test_compose(const char **msg,
             svn_boolean_t msg_only,
             svn_test_opts_t *opts,
             apr_pool_t *pool)
{
  static const char * const paths[] = {
    "",
    "/",
    "/foo",
    "/foo/bar",
    "/foo/bar/baz",
    "foo",
    "foo/bar",
    "foo/bar/baz",
    NULL,
  };
  const char * const *path_ptr = paths;
  const char *input_path;

  *msg = "test svn_path_decompose";
  if (msg_only)
    return SVN_NO_ERROR;

  for (input_path = *path_ptr; *path_ptr; input_path = *++path_ptr)
    {
      apr_array_header_t *components = svn_path_decompose(input_path, pool);
      const char *output_path = svn_path_compose(components, pool);

      if (strcmp(input_path, output_path))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_compose("
                                 "svn_path_decompose(\"%s\")) "
                                 "returned \"%s\" expected \"%s\"",
                                 input_path, output_path, input_path);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_is_canonical(const char **msg,
                  svn_boolean_t msg_only,
                  svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  struct {
    const char *path;
    svn_boolean_t canonical;
  } tests[] = {
    { "",                      TRUE },
    { ".",                     FALSE },
    { "/",                     TRUE },
    { "/.",                    FALSE },
    { "./",                    FALSE },
    { "./.",                   FALSE },
    { "//",                    FALSE },
    { "/////",                 FALSE },
    { "./././.",               FALSE },
    { "////././.",             FALSE },
    { "foo",                   TRUE },
    { ".foo",                  TRUE },
    { "foo.",                  TRUE },
    { "/foo",                  TRUE },
    { "foo/",                  FALSE },
    { "foo./",                 FALSE },
    { "foo./.",                FALSE },
    { "foo././/.",             FALSE },
    { "/foo/bar",              TRUE },
    { "foo/..",                TRUE },
    { "foo/../",               FALSE },
    { "foo/../.",              FALSE },
    { "foo//.//bar",           FALSE },
    { "///foo",                FALSE },
    { "/.//./.foo",            FALSE },
    { ".///.foo",              FALSE },
    { "../foo",                TRUE },
    { "../../foo/",            FALSE },
    { "../../foo/..",          TRUE },
    { "/../../",               FALSE },
    { "dirA",                  TRUE },
    { "foo/dirA",              TRUE },
    { "http://hst",            TRUE },
    { "http://hst/foo/../bar", TRUE },
    { "http://hst/",           FALSE },
    { "foo/./bar",             FALSE },
    { "http://HST/",           FALSE },
    { "http://HST/FOO/BaR",    FALSE },
    { "svn+ssh://j.raNDom@HST/BaR", FALSE },
    { "svn+SSH://j.random:jRaY@HST/BaR", FALSE },
    { "SVN+ssh://j.raNDom:jray@HST/BaR", FALSE },    
    { "svn+ssh://j.raNDom:jray@hst/BaR", TRUE },
    { "fILe:///Users/jrandom/wc", FALSE },
#if defined(WIN32) || defined(__CYGWIN__)
    { "file:///c:/temp/repos", FALSE },
    { "file:///c:/temp/REPOS", FALSE },
    { "file:///C:/temp/REPOS", TRUE },
    { "//server/share/",       FALSE },
    { "//server/share",        TRUE },
    { "//server/SHare",        TRUE },
    { "//SERVER/SHare",        FALSE },
#else /* WIN32 or Cygwin */
    { "file:///c:/temp/repos", TRUE },
    { "file:///c:/temp/REPOS", TRUE },
    { "file:///C:/temp/REPOS", TRUE },
#endif /* non-WIN32 */
    { NULL, FALSE },
  };
  int i;

  *msg = "test svn_path_is_canonical";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; tests[i].path; i++)
    {
      svn_boolean_t canonical;

      canonical = svn_path_is_canonical(tests[i].path, pool);
      if (tests[i].canonical != canonical)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_is_canonical(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path,
                                 canonical ? "TRUE" : "FALSE",
                                 tests[i].canonical ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

/* local define to support XFail-ing tests on Windows/Cygwin only */
#if defined(WIN32) || defined(__CYGWIN__)
#define WINDOWS_OR_CYGWIN TRUE
#else
#define WINDOWS_OR_CYGWIN FALSE
#endif /* WIN32 or Cygwin */


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
    SVN_TEST_PASS(test_path_is_ancestor),
    SVN_TEST_PASS(test_path_check_valid),
    SVN_TEST_PASS(test_is_single_path_component),
    SVN_TEST_PASS(test_compare_paths),
    SVN_TEST_PASS(test_get_longest_ancestor),
    SVN_TEST_PASS(test_splitext),
    SVN_TEST_PASS(test_compose),
    SVN_TEST_PASS(test_is_canonical),
    SVN_TEST_NULL
  };
