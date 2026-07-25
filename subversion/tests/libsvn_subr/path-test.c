/*
 * path-test.c -- test the path functions
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifdef _MSC_VER
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h> /* for getcwd() */
#endif

#include <stdio.h>
#include <string.h>
#include <apr_general.h>

#include "svn_pools.h"

#include "../svn_test.h"

/* Make sure SVN_DEPRECATED is defined as empty before including svn_path.h.
   We don't want to trigger deprecation warnings by the tests of those
   functions.  */
#ifdef SVN_DEPRECATED
#undef SVN_DEPRECATED
#endif
#define SVN_DEPRECATED

#include "svn_path.h"


/* Using a symbol, because I tried experimenting with different
   representations */
#define SVN_EMPTY_PATH ""

/* This check must match the check on top of dirent_uri.c and
   dirent_uri-tests.c */
#if defined(WIN32) || defined(__CYGWIN__) || defined(__OS2__)
#define SVN_USE_DOS_PATHS
#endif

static svn_error_t *
test_path_is_child(apr_pool_t *pool)
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
test_path_split(apr_pool_t *pool)
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
test_path_is_url(apr_pool_t *pool)
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
    { "http://svn.apache.org/repos/asf/subversion",  TRUE  },
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
#ifdef SVN_USE_DOS_PATHS
    { "X:/foo",        FALSE },
    { "X:foo",         FALSE },
    { "X:",            FALSE },
#endif /* non-WIN32 */
    { "X:/",           FALSE },
    { "//srv/shr",     FALSE },
    { "//srv/shr/fld", FALSE },
  };

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
test_path_is_uri_safe(apr_pool_t *pool)
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
test_uri_encode(apr_pool_t *pool)
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
test_uri_decode(apr_pool_t *pool)
{
  int i;

  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "http://c.r.a/s%\0" "8me",
         "http://c.r.a/s%"},
    { "http://c.r.a/s%6\0" "me",
         "http://c.r.a/s%6" },
    { "http://c.r.a/s%68me",
         "http://c.r.a/shme" },
  };

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
test_uri_autoescape(apr_pool_t *pool)
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
test_uri_from_iri(apr_pool_t *pool)
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
test_path_join(apr_pool_t *pool)
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
    { "X:/abc", "/d", "/d" },
    { "X:/abc", "/", "/" },
    { "X:",SVN_EMPTY_PATH, "X:" },
    { "X:", "/def", "/def" },
    { "X:abc", "/d", "/d" },
    { "X:abc", "/", "/" },
    { "file://", "foo", "file:///foo" },
    { "file:///foo", "bar", "file:///foo/bar" },
    { "file:///foo", SVN_EMPTY_PATH, "file:///foo" },
    { SVN_EMPTY_PATH, "file:///foo", "file:///foo" },
    { "file:///X:", "bar", "file:///X:/bar" },
    { "file:///X:foo", "bar", "file:///X:foo/bar" },
    { "http://svn.dm.net", "repos", "http://svn.dm.net/repos" },
#ifdef SVN_USE_DOS_PATHS
/* These will fail, see issue #2028
    { "//srv/shr",     "fld",     "//srv/shr/fld" },
    { "//srv",         "shr/fld", "//srv/shr/fld" },
    { "//srv/shr/fld", "subfld",  "//srv/shr/fld/subfld" },
    { "//srv/shr/fld", "//srv/shr", "//srv/shr" },
    { "//srv",         "//srv/fld", "//srv/fld" },
    { "X:abc", "X:/def", "X:/def" },    { "X:/",SVN_EMPTY_PATH, "X:/" },
    { "X:/","abc", "X:/abc" },
    { "X:/", "/def", "/def" },
    { "X:/abc", "X:/", "X:/" },
    { "X:abc", "X:/", "X:/" },
    { "X:abc", "X:/def", "X:/def" },
    { "X:","abc", "X:abc" },
    { "X:/abc", "X:/def", "X:/def" },
*/
#else /* WIN32 or Cygwin */
    { "X:abc", "X:/def", "X:abc/X:/def" },
    { "X:","abc", "X:/abc" },
    { "X:/abc", "X:/def", "X:/abc/X:/def" },
#endif /* non-WIN32 */
  };

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

      /* svn_path_join_many does not support URLs, so skip the URL tests. */
      if (svn_path_is_url(base))
        continue;

      result = svn_path_join_many(pool, base, comp, SVN_VA_NULL);
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
                             result, expect);

  TEST_MANY((pool, "abc", SVN_VA_NULL), "abc");
  TEST_MANY((pool, "/abc", SVN_VA_NULL), "/abc");
  TEST_MANY((pool, "/", SVN_VA_NULL), "/");

  TEST_MANY((pool, "abc", "def", "ghi", SVN_VA_NULL), "abc/def/ghi");
  TEST_MANY((pool, "abc", "/def", "ghi", SVN_VA_NULL), "/def/ghi");
  TEST_MANY((pool, "/abc", "def", "ghi", SVN_VA_NULL), "/abc/def/ghi");
  TEST_MANY((pool, "abc", "def", "/ghi", SVN_VA_NULL), "/ghi");
  TEST_MANY((pool, "/", "def", "/ghi", SVN_VA_NULL), "/ghi");
  TEST_MANY((pool, "/", "/def", "/ghi", SVN_VA_NULL), "/ghi");

  TEST_MANY((pool, SVN_EMPTY_PATH, "def", "ghi", SVN_VA_NULL), "def/ghi");
  TEST_MANY((pool, "abc", SVN_EMPTY_PATH, "ghi", SVN_VA_NULL), "abc/ghi");
  TEST_MANY((pool, "abc", "def", SVN_EMPTY_PATH, SVN_VA_NULL), "abc/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "def", SVN_EMPTY_PATH, SVN_VA_NULL), "def");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "ghi", SVN_VA_NULL), "ghi");
  TEST_MANY((pool, "abc", SVN_EMPTY_PATH, SVN_EMPTY_PATH, SVN_VA_NULL), "abc");
  TEST_MANY((pool, SVN_EMPTY_PATH, "def", "/ghi", SVN_VA_NULL), "/ghi");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "/ghi", SVN_VA_NULL), "/ghi");

  TEST_MANY((pool, "/", "def", "ghi", SVN_VA_NULL), "/def/ghi");
  TEST_MANY((pool, "abc", "/", "ghi", SVN_VA_NULL), "/ghi");
  TEST_MANY((pool, "abc", "def", "/", SVN_VA_NULL), "/");
  TEST_MANY((pool, "/", "/", "ghi", SVN_VA_NULL), "/ghi");
  TEST_MANY((pool, "/", "/", "/", SVN_VA_NULL), "/");
  TEST_MANY((pool, "/", SVN_EMPTY_PATH, "ghi", SVN_VA_NULL), "/ghi");
  TEST_MANY((pool, "/", "def", SVN_EMPTY_PATH, SVN_VA_NULL), "/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "/", "ghi", SVN_VA_NULL), "/ghi");
  TEST_MANY((pool, "/", SVN_EMPTY_PATH, SVN_EMPTY_PATH, SVN_VA_NULL), "/");
  TEST_MANY((pool, SVN_EMPTY_PATH, "/", SVN_EMPTY_PATH, SVN_VA_NULL), "/");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "/", SVN_VA_NULL), "/");

#ifdef SVN_USE_DOS_PATHS
/* These will fail, see issue #2028
  TEST_MANY((pool, "X:", "def", "ghi", SVN_VA_NULL), "X:def/ghi");
  TEST_MANY((pool, "X:", SVN_EMPTY_PATH, "ghi", SVN_VA_NULL), "X:ghi");
  TEST_MANY((pool, "X:", "def", SVN_EMPTY_PATH, SVN_VA_NULL), "X:def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:", "ghi", SVN_VA_NULL), "X:ghi");
  TEST_MANY((pool, "X:/", "def", "ghi", SVN_VA_NULL), "X:/def/ghi");
  TEST_MANY((pool, "abc", "X:/", "ghi", SVN_VA_NULL), "X:/ghi");
  TEST_MANY((pool, "abc", "def", "X:/", SVN_VA_NULL), "X:/");
  TEST_MANY((pool, "X:/", "X:/", "ghi", SVN_VA_NULL), "X:/ghi");
  TEST_MANY((pool, "X:/", "X:/", "/", SVN_VA_NULL), "/");
  TEST_MANY((pool, "X:/", SVN_EMPTY_PATH, "ghi", SVN_VA_NULL), "X:/ghi");
  TEST_MANY((pool, "X:/", "def", SVN_EMPTY_PATH, SVN_VA_NULL), "X:/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:/", "ghi", SVN_VA_NULL), "X:/ghi");
  TEST_MANY((pool, "X:/", SVN_EMPTY_PATH, SVN_EMPTY_PATH, SVN_VA_NULL), "X:/");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:/", SVN_EMPTY_PATH, SVN_VA_NULL), "X:/");
  TEST_MANY((pool, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "X:/", SVN_VA_NULL), "X:/");
  TEST_MANY((pool, "X:", "X:/", "ghi", SVN_VA_NULL), "X:/ghi");
  TEST_MANY((pool, "X:", "X:/", "/", SVN_VA_NULL), "/");

  TEST_MANY((pool, "//srv/shr", "def", "ghi", SVN_VA_NULL), "//srv/shr/def/ghi");
  TEST_MANY((pool, "//srv", "shr", "def", "ghi", SVN_VA_NULL), "//srv/shr/def/ghi");
  TEST_MANY((pool, "//srv/shr/fld", "def", "ghi", SVN_VA_NULL),
            "//srv/shr/fld/def/ghi");
  TEST_MANY((pool, "//srv/shr/fld", "def", "//srv/shr", SVN_VA_NULL), "//srv/shr");
  TEST_MANY((pool, "//srv", "shr", "//srv/shr", SVN_VA_NULL), "//srv/shr");
  TEST_MANY((pool, SVN_EMPTY_PATH, "//srv/shr/fld", "def", "ghi", SVN_VA_NULL),
            "//srv/shr/fld/def/ghi");
  TEST_MANY((pool, SVN_EMPTY_PATH, "//srv/shr/fld", "def", "//srv/shr", SVN_VA_NULL),
            "//srv/shr");
*/
#else /* WIN32 or Cygwin */
  TEST_MANY((pool, "X:", "def", "ghi", SVN_VA_NULL), "X:/def/ghi");
  TEST_MANY((pool, "X:", SVN_EMPTY_PATH, "ghi", SVN_VA_NULL), "X:/ghi");
  TEST_MANY((pool, "X:", "def", SVN_EMPTY_PATH, SVN_VA_NULL), "X:/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:", "ghi", SVN_VA_NULL), "X:/ghi");
#endif /* non-WIN32 */

  /* ### probably need quite a few more tests... */

  return SVN_NO_ERROR;
}


static svn_error_t *
test_path_basename(apr_pool_t *pool)
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
    { "X:/abc", "abc" },
    { "X:", "X:" },

#ifdef SVN_USE_DOS_PATHS
/* These will fail, see issue #2028
    { "X:/", "X:/" },
    { "X:abc", "abc" },
    { "//srv/shr",      "//srv/shr" },
    { "//srv",          "//srv" },
    { "//srv/shr/fld",  "fld" },
    { "//srv/shr/fld/subfld", "subfld" },
*/
#else /* WIN32 or Cygwin */
    { "X:abc", "X:abc" },
#endif /* non-WIN32 */
  };

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
test_path_dirname(apr_pool_t *pool)
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
    { "X:abc/def", "X:abc" },
#ifdef SVN_USE_DOS_PATHS
    { "//srv/shr/fld",  "//srv/shr" },
    { "//srv/shr/fld/subfld", "//srv/shr/fld" },

/* These will fail, see issue #2028
    { "X:/", "X:/" },
    { "X:/abc", "X:/" },
    { "X:", "X:" },
    { "X:abc", "X:" },
    { "//srv/shr",      "//srv/shr" },
*/
#else  /* WIN32 or Cygwin */
    /* on non-Windows platforms, ':' is allowed in pathnames */
    { "X:", "" },
    { "X:abc", "" },
#endif /* non-WIN32 */
  };

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
test_path_decompose(apr_pool_t *pool)
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
test_path_canonicalize(apr_pool_t *pool)
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
    { "fiLE:///",             "file://" },
    { "fiLE://",              "file://" },
    { "X:/foo",               "X:/foo" },
    { "X:",                   "X:" },
    { "X:foo",                "X:foo" },
#ifdef SVN_USE_DOS_PATHS
    { "file:///c:/temp/repos", "file:///C:/temp/repos" },
    { "file:///c:/temp/REPOS", "file:///C:/temp/REPOS" },
    { "file:///C:/temp/REPOS", "file:///C:/temp/REPOS" },
    { "C:/folder/subfolder/file", "C:/folder/subfolder/file" },
    /* We permit UNC paths on Windows.  By definition UNC
     * paths must have two components so we should remove the
     * double slash if there is only one component. */
    { "//hst",                "/hst" },
    { "//hst/./",             "/hst" },
    { "//server/share/",      "//server/share" },
    { "//server/SHare/",      "//server/SHare" },
    { "//SERVER/SHare/",      "//server/SHare" },
    { "X:/",                  "X:/" },
#else /* WIN32 or Cygwin */
    { "file:///c:/temp/repos", "file:///c:/temp/repos" },
    { "file:///c:/temp/REPOS", "file:///c:/temp/REPOS" },
    { "file:///C:/temp/REPOS", "file:///C:/temp/REPOS" },
#endif /* non-WIN32 */
    { NULL, NULL }
  };
  int i;

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
test_path_remove_component(apr_pool_t *pool)
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
#ifdef SVN_USE_DOS_PATHS
    { "X:/foo/bar",           "X:/foo" },
    { "//srv/shr/fld",        "//srv/shr" },
    { "//srv/shr/fld/subfld", "//srv/shr/fld" },
/* These will fail, see issue #2028
    { "X:/foo",               "X:/" },
    { "X:/",                  "X:/" },
    { "X:foo",                "X:" },
    { "X:",                   "X:" },
    { "//srv/shr",            "//srv/shr" },
*/
#else /* WIN32 or Cygwin */
    { "X:foo",                "" },
    { "X:",                   "" },
#endif /* non-WIN32 */
    { NULL, NULL }
  };
  int i;
  svn_stringbuf_t *buf;

  buf = svn_stringbuf_create_empty(pool);

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
test_path_check_valid(apr_pool_t *pool)
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
test_path_is_ancestor(apr_pool_t *pool)
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
    { "X:foo",           "X:bar",         FALSE},
#ifdef SVN_USE_DOS_PATHS
    { "//srv/shr",       "//srv",         FALSE},
    { "//srv/shr",       "//srv/shr/fld", TRUE },
    { "//srv",           "//srv/shr/fld", TRUE },
    { "//srv/shr/fld",   "//srv/shr",     FALSE },
    { "//srv/shr/fld",   "//srv2/shr/fld", FALSE },
/* These will fail, see issue #2028
    { "X:/",             "X:/",           TRUE},
    { "X:/foo",          "X:/",           FALSE},
    { "X:/",             "X:/foo",        TRUE},
    { "X:",              "X:foo",         TRUE},
*/
#else /* WIN32 or Cygwin */
    { "X:",              "X:foo",         FALSE},

#endif /* non-WIN32 */
  };

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
test_is_single_path_component(apr_pool_t *pool)
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
test_compare_paths(apr_pool_t *pool)
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
    { "X:/foo",       "X:/foo",        0},
    { "X:foo",        "X:foo",         0},
    { "X:",           "X:foo",         -1},
    { "X:foo",        "X:",            1},
#ifdef SVN_USE_DOS_PATHS
    { "//srv/shr",    "//srv",         1},
    { "//srv/shr",    "//srv/shr/fld", -1 },
    { "//srv/shr/fld", "//srv/shr",    1 },
    { "//srv/shr/fld", "//abc/def/ghi", 1 },
/* These will fail, see issue #2028
    { "X:/",          "X:/",           0},
    { "X:/",          "X:/foo",        -1},
    { "X:/foo",       "X:/",           1},
*/
#endif /* WIN32 or Cygwin */
  };

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
test_path_get_longest_ancestor(apr_pool_t *pool)
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
    { "http://test",    "http://tests",    ""},
    { "http://",        "http://test",     ""},
    { "file:///A/C",    "file:///B/D",     ""},
    { "file:///A/C",    "file:///A/D",     "file:///A"},

#ifdef SVN_USE_DOS_PATHS
    { "X:/",            "X:/",             "X:/"},
    { "X:/foo/bar/A/D/H/psi", "X:/foo/bar/A/B", "X:/foo/bar/A" },
    { "X:/foo/bar/boo", "X:/foo/bar/baz/boz", "X:/foo/bar"},
    { "X:foo/bar",      "X:foo/bar/boo",   "X:foo/bar"},
    { "//srv/shr",      "//srv/shr/fld",   "//srv/shr" },
    { "//srv/shr/fld",  "//srv/shr",       "//srv/shr" },

/* These will fail, see issue #2028
    { "//srv/shr/fld",  "//srv2/shr/fld",  "" },
    { "X:/foo",         "X:/",             "X:/"},
    { "X:/folder1",     "X:/folder2",      "X:/"},
    { "X:/",            "X:/foo",          "X:/"},
    { "X:",             "X:foo",           "X:"},
    { "X:",             "X:/",             ""},
    { "X:foo",          "X:bar",           "X:"},
*/
#else /* WIN32 or Cygwin */
    { "X:/foo",         "X:",              "X:"},
    { "X:/folder1",     "X:/folder2",      "X:"},
    { "X:",             "X:foo",           ""},
    { "X:foo",          "X:bar",           ""},
#endif /* non-WIN32 */
  };

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
test_path_splitext(apr_pool_t *pool)
{
  apr_size_t i;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Paths to test and their expected results. */
  struct {
    const char *path;
    const char *path_root;
    const char *path_ext;
  } tests[] = {
    { "no-ext",                    "no-ext",                 "" },
    { "test-file.py",              "test-file.",             "py" },
    { "period.file.ext",           "period.file.",           "ext" },
    { "multi-component/file.txt",  "multi-component/file.",  "txt" },
    { "yep.still/no-ext",          "yep.still/no-ext",       "" },
    { "folder.with/period.log",    "folder.with/period.",    "log" },
    { "period.",                   "period.",                "" },
    { "dir/period.",               "dir/period.",            "" },
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
test_path_compose(apr_pool_t *pool)
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
test_path_is_canonical(apr_pool_t *pool)
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
    { "fiLE:///",              FALSE },
    { "fiLE://",               FALSE },
#ifdef SVN_USE_DOS_PATHS
    { "file:///c:/temp/repos", FALSE },
    { "file:///c:/temp/REPOS", FALSE },
    { "file:///C:/temp/REPOS", TRUE },
    { "//server/share/",       FALSE },
    { "//server/share",        TRUE },
    { "//server/SHare",        TRUE },
    { "//SERVER/SHare",        FALSE },
    { "C:/folder/subfolder/file", TRUE },
#else /* WIN32 or Cygwin */
    { "file:///c:/temp/repos", TRUE },
    { "file:///c:/temp/REPOS", TRUE },
    { "file:///C:/temp/REPOS", TRUE },
#endif /* non-WIN32 */
    { NULL, FALSE },
  };
  int i;

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

static svn_error_t *
test_path_local_style(apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "",                     "." },
    { ".",                    "." },
    { "http://host/dir",      "http://host/dir" }, /* Not with local separator */
#ifdef SVN_USE_DOS_PATHS
    { "A:/",                 "A:\\" },
    { "a:/",                 "a:\\" },
    { "A:/file",             "A:\\file" },
    { "dir/file",            "dir\\file" },
    { "/",                   "\\" },
    { "//server/share/dir",  "\\\\server\\share\\dir" },
#else
    { "a:/file",             "a:/file" },
    { "dir/file",            "dir/file" },
    { "/",                   "/" },
#endif
    { NULL, NULL }
  };
  int i = 0;

  while (tests[i].path)
    {
      const char *local = svn_path_local_style(tests[i].path, pool);

      if (strcmp(local, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_local_style(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, local, tests[i].result);
      ++i;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_path_internal_style(apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "",                     "" },
    { ".",                    "" },
    { "http://host/dir",      "http://host/dir" },
    { "/",                    "/" },
#ifdef SVN_USE_DOS_PATHS
    { "a:\\",                 "A:/" },
    { "a:\\file",             "A:/file" },
    { "dir\\file",            "dir/file" },
    { "\\",                   "/" },
    { "\\\\server/share/dir",  "//server/share/dir" },
#else
    { "a:/",                 "a:" },
    { "a:/file",             "a:/file" },
    { "dir/file",            "dir/file" },
    { "/",                   "/" },
    { "//server/share/dir",  "/server/share/dir" },
#endif
    { NULL, NULL }
  };
  int i;

  i = 0;
  while (tests[i].path)
    {
      const char *local = svn_path_internal_style(tests[i].path, pool);

      if (strcmp(local, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_internal_style(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, local, tests[i].result);
      ++i;
    }

  return SVN_NO_ERROR;
}


/* The type of a function to be tested by condense_targets_tests_helper().
 * Matches svn_path_condense_targets().
 */
typedef svn_error_t *(*condense_targets_func_t)
                           (const char **pcommon,
                            apr_array_header_t **pcondensed_targets,
                            const apr_array_header_t *targets,
                            svn_boolean_t remove_redundancies,
                            apr_pool_t *pool);

/** Executes function CONDENSE_TARGETS twice - with and without requesting the
 * condensed targets list -  on TEST_TARGETS (comma sep. string) and compares
 * the results with EXP_COMMON and EXP_TARGETS (comma sep. string).
 *
 * @note: a '%' character at the beginning of EXP_COMMON or EXP_TARGETS will
 * be replaced by the current working directory.
 *
 * Returns an error if any of the comparisons fail.
 */
static svn_error_t *
condense_targets_tests_helper(const char* title,
                              const char* test_targets,
                              const char* exp_common,
                              const char* exp_targets,
                              const char* func_name,
                              condense_targets_func_t condense_targets,
                              apr_pool_t *pool)
{
  apr_array_header_t *targets;
  apr_array_header_t *condensed_targets;
  const char *common_path, *common_path2, *curdir;
  char *token, *iter;
  const char *exp_common_abs = exp_common;
  int i;
  char buf[8192];

  if (! getcwd(buf, sizeof(buf)))
    return svn_error_create(SVN_ERR_BASE, NULL, "getcwd() failed");
  curdir = svn_path_internal_style(buf, pool);

  /* Create the target array */
  targets = apr_array_make(pool, sizeof(test_targets), sizeof(const char *));
  token = apr_strtok(apr_pstrdup(pool, test_targets), ",", &iter);
  while (token)
    {
      APR_ARRAY_PUSH(targets, const char *) =
        svn_path_internal_style(token, pool);
      token = apr_strtok(NULL, ",", &iter);
    };

  /* Call the function */
  SVN_ERR(condense_targets(&common_path, &condensed_targets, targets,
                           TRUE, pool));

  /* Verify the common part with the expected (prefix with cwd). */
  if (*exp_common == '%')
    exp_common_abs = apr_pstrcat(pool, curdir, exp_common + 1, SVN_VA_NULL);

  if (strcmp(common_path, exp_common_abs) != 0)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, NULL,
         "%s (test %s) returned %s instead of %s",
           func_name, title,
           common_path, exp_common_abs);
    }

  /* Verify the condensed targets */
  token = apr_strtok(apr_pstrdup(pool, exp_targets), ",", &iter);
  for (i = 0; i < condensed_targets->nelts; i++)
    {
      const char * target = APR_ARRAY_IDX(condensed_targets, i, const char*);
      if (token && (*token == '%'))
        token = apr_pstrcat(pool, curdir, token + 1, SVN_VA_NULL);
      if (! token ||
          (target && (strcmp(target, token) != 0)))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "%s (test %s) couldn't find %s in expected targets list",
               func_name, title,
               target);
        }
      token = apr_strtok(NULL, ",", &iter);
    }

  /* Now ensure it works without the pbasename */
  SVN_ERR(condense_targets(&common_path2, NULL, targets, TRUE, pool));

  /* Verify the common part again */
  if (strcmp(common_path, common_path2) != 0)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, NULL,
         "%s (test %s): Common path without getting targets %s does not match" \
         "common path with targets %s",
          func_name, title,
          common_path2, common_path);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_path_condense_targets(apr_pool_t *pool)
{
  int i;
  struct {
    const char* title;
    const char* targets;
    const char* exp_common;
    const char* exp_targets;
  } tests[] = {
    { "normal use", "z/A/B,z/A,z/A/C,z/D/E,z/D/F,z/D,z/G,z/G/H,z/G/I",
      "%/z", "A,D,G" },
    {"identical dirs", "z/A,z/A,z/A,z/A",
     "%/z/A", "" },
    {"identical files", "z/A/file,z/A/file,z/A/file,z/A/file",
     "%/z/A/file", "" },
    {"single dir", "z/A",
     "%/z/A", "" },
    {"single file", "z/A/file",
     "%/z/A/file", "" },
    {"URLs", "http://host/A/C,http://host/A/C/D,http://host/A/B/D",
     "http://host/A", "C,B/D" },
    {"URLs with no common prefix",
     "http://host1/A/C,http://host2/A/C/D,http://host3/A/B/D",
     "", "http://host1/A/C,http://host2/A/C/D,http://host3/A/B/D" },
    {"file URLs with no common prefix", "file:///A/C,file:///B/D",
     "", "file:///A/C,file:///B/D" },
    {"URLs with mixed protocols",
     "http://host/A/C,file:///B/D,gopher://host/A",
     "", "http://host/A/C,file:///B/D,gopher://host/A" },
    {"mixed paths and URLs",
     "z/A/B,z/A,http://host/A/C/D,http://host/A/C",
     "", "%/z/A,http://host/A/C" },
  };

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      SVN_ERR(condense_targets_tests_helper(tests[i].title,
                                            tests[i].targets,
                                            tests[i].exp_common,
                                            tests[i].exp_targets,
                                            "svn_path_condense_targets",
                                            svn_path_condense_targets,
                                            pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_path_is_repos_relative_url(apr_pool_t *pool)
{
  int i;
  struct {
    const char* path;
    svn_boolean_t result;
  } tests[] = {
    { "^/A",           TRUE },
    { "http://host/A", FALSE },
    { "/A/B",          FALSE },
  };

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      svn_boolean_t result = svn_path_is_repos_relative_url(tests[i].path);

      if (tests[i].result != result)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_is_repos_relative_url(\"%s\")"
                                 " returned \"%s\" expected \"%s\"",
                                 tests[i].path,
                                 result ? "TRUE" : "FALSE",
                                 tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_path_resolve_repos_relative_url(apr_pool_t *pool)
{
  int i;
  struct {
    const char *relative_url;
    const char *repos_root_url;
    const char *absolute_url;
  } tests[] = {
    { "^/A", "file:///Z/X", "file:///Z/X/A" },
    { "^/A", "file:///Z/X/", "file:///Z/X//A" }, /* doesn't canonicalize */
    { "^/A@2", "file:///Z/X", "file:///Z/X/A@2" }, /* peg rev */
    { "^/A", "/Z/X", "/Z/X/A" }, /* doesn't verify repos_root is URL */
  };

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      const char *result;

      SVN_ERR(svn_path_resolve_repos_relative_url(&result,
                                                  tests[i].relative_url,
                                                  tests[i].repos_root_url,
                                                  pool));

      if (strcmp(tests[i].absolute_url,result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_path_resolve_repos_relative_url(\"%s\","
                                 "\"%s\") returned \"%s\" expected \"%s\"",
                                 tests[i].relative_url,
                                 tests[i].repos_root_url,
                                 result, tests[i].absolute_url);
    }

  return SVN_NO_ERROR;
}


/* local define to support XFail-ing tests on Windows/Cygwin only */
#ifdef SVN_USE_DOS_PATHS
#define WINDOWS_OR_CYGWIN TRUE
#else
#define WINDOWS_OR_CYGWIN FALSE
#endif /* WIN32 or Cygwin */


/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_path_is_child,
                   "test svn_path_is_child"),
    SVN_TEST_PASS2(test_path_split,
                   "test svn_path_split"),
    SVN_TEST_PASS2(test_path_is_url,
                   "test svn_path_is_url"),
    SVN_TEST_PASS2(test_path_is_uri_safe,
                   "test svn_path_is_uri_safe"),
    SVN_TEST_PASS2(test_uri_encode,
                   "test svn_path_uri_[en/de]code"),
    SVN_TEST_PASS2(test_uri_decode,
                   "test svn_path_uri_decode with invalid escape"),
    SVN_TEST_PASS2(test_uri_autoescape,
                   "test svn_path_uri_autoescape"),
    SVN_TEST_PASS2(test_uri_from_iri,
                   "test svn_path_uri_from_iri"),
    SVN_TEST_PASS2(test_path_join,
                   "test svn_path_join(_many)"),
    SVN_TEST_PASS2(test_path_basename,
                   "test svn_path_basename"),
    SVN_TEST_PASS2(test_path_dirname,
                   "test svn_path_dirname"),
    SVN_TEST_PASS2(test_path_decompose,
                   "test svn_path_decompose"),
    SVN_TEST_PASS2(test_path_canonicalize,
                   "test svn_path_canonicalize"),
    SVN_TEST_PASS2(test_path_remove_component,
                   "test svn_path_remove_component"),
    SVN_TEST_PASS2(test_path_is_ancestor,
                   "test svn_path_is_ancestor"),
    SVN_TEST_PASS2(test_path_check_valid,
                   "test svn_path_check_valid"),
    SVN_TEST_PASS2(test_is_single_path_component,
                   "test svn_path_is_single_path_component"),
    SVN_TEST_PASS2(test_compare_paths,
                   "test svn_path_compare_paths"),
    SVN_TEST_PASS2(test_path_get_longest_ancestor,
                   "test svn_path_get_longest_ancestor"),
    SVN_TEST_PASS2(test_path_splitext,
                   "test svn_path_splitext"),
    SVN_TEST_PASS2(test_path_compose,
                   "test svn_path_decompose"),
    SVN_TEST_PASS2(test_path_is_canonical,
                   "test svn_path_is_canonical"),
    SVN_TEST_PASS2(test_path_local_style,
                   "test svn_path_local_style"),
    SVN_TEST_PASS2(test_path_internal_style,
                   "test svn_path_internal_style"),
    SVN_TEST_PASS2(test_path_condense_targets,
                   "test svn_path_condense_targets"),
    SVN_TEST_PASS2(test_path_is_repos_relative_url,
                   "test svn_path_is_repos_relative_url"),
    SVN_TEST_PASS2(test_path_resolve_repos_relative_url,
                   "test svn_path_resolve_repos_relative_url"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
