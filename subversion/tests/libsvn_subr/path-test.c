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

/* Using a symbol, because I tried experimenting with different
   representations */
#define SVN_EMPTY_PATH ""

static svn_error_t *
test_path_is_child (const char **msg,
                    svn_boolean_t msg_only,
                    apr_pool_t *pool)
{
  int i, j;
#define NUM_TEST_PATHS 8

  static const char * const paths[NUM_TEST_PATHS] = { 
    "/foo/bar",
    "/foo/baz",
    "/foo/bar/baz",
    "/flu/blar/blaz",
    "/foo/bar/baz/bing/boom",
    SVN_EMPTY_PATH,
    "foo",
    ".foo"
    };
  
  static const char * const remainders[NUM_TEST_PATHS][NUM_TEST_PATHS] = {
    { 0, 0, "baz", 0, "baz/bing/boom", 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, "bing/boom", 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, "foo", ".foo" },
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0 }
  };
  
  *msg = "test svn_path_is_child";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < NUM_TEST_PATHS; i++)
    {
      for (j = 0; j < NUM_TEST_PATHS; j++)
        {
          const char *remainder;

          remainder = svn_path_is_child (paths[i], paths[j], pool);

          if (((remainder) && (! remainders[i][j]))
              || ((! remainder) && (remainders[i][j]))
              || (remainder && strcmp (remainder, remainders[i][j])))
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, 0, NULL,
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
test_path_split (const char **msg,
                 svn_boolean_t msg_only,
                 apr_pool_t *pool)
{
  int i;

  static const char * const paths[][3] = { 
    { "/foo/bar",        "/foo",          "bar" },
    { "/foo/bar/ ",      "/foo/bar",      " " },
    { "/foo",            "/",             "foo" },
    { "foo",             SVN_EMPTY_PATH,  "foo" },
    { "./foo",           SVN_EMPTY_PATH,  "foo" },
    { "././.bar",        SVN_EMPTY_PATH,  ".bar" },
    { "/././.bar",       "/",             ".bar" },
    { "foo///bar",       "foo",           "bar" },
    { "/foo///bar",      "/foo",          "bar" },
    { "foo//.//bar",     "foo",           "bar" },
    { "foo./.bar",       "foo.",          ".bar" },
    { "foo././.bar",     "foo.",          ".bar" },
    { "../foo",          "..",            "foo" },
    { SVN_EMPTY_PATH,   SVN_EMPTY_PATH,   SVN_EMPTY_PATH },
    { "/flu\\b/\\blarg", "/flu\\b",       "\\blarg" },
  };
  
  *msg = "test svn_path_split";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof (paths) / sizeof (paths[0]); i++)
    {
      const char *dir, *base_name;

      svn_path_split (paths[i][0], &dir, &base_name, pool);
      if (strcmp (dir, paths[i][1]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL,
             "svn_path_split (%s) returned dirname '%s' instead of '%s'",
             paths[i][0], dir, paths[i][1]);
        }
      if (strcmp (base_name, paths[i][2]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL,
             "svn_path_split (%s) returned basename '%s' instead of '%s'",
             paths[i][0], base_name, paths[i][2]);
        }
    }
  return SVN_NO_ERROR;
}


static svn_boolean_t
char_is_uri_safe (int c)
{
  /* Is this an alphanumeric character? */
  if (((c >= 'A') && (c <='Z'))
      || ((c >= 'a') && (c <='z'))
      || ((c >= '0') && (c <='9')))
    return TRUE;

  /* Is this a supported non-alphanumeric character? (these are sorted
     by estimated usage, most-to-least commonly used) */
  if (strchr ("/:.-_!~'()@=+$,&*", c) != NULL)
    return TRUE;

  return FALSE;
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

  /* First, test the helper function */
  {
    char foo[2];
    foo[1] = '\0';

    for (i = 0; i < 255; i++)
      {
        svn_boolean_t expected, actual;
        expected = char_is_uri_safe (i);

        foo[0] = (char)i;
        actual = svn_path_is_uri_safe (foo);

        if (expected && (! actual))
          {
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, 0, NULL,
               "svn_path_is_uri_safe (%d) returned FALSE instead of TRUE", i);
          }
        if ((! expected) && actual)
          {
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, 0, NULL,
               "svn_path_is_uri_safe (%d) returned TRUE instead of FALSE", i);
          }
      }
  }

  /* Now, do the tests. */
  for (i = 0; i < 5; i++)
    {
      svn_boolean_t retval;

      retval = svn_path_is_url (paths[i]);
      if (retvals[i] != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, 0, NULL,
           "svn_path_is_url (%s) returned %s instead of %s",
           paths[i], retval ? "TRUE" : "FALSE", retvals[i] ? "TRUE" : "FALSE");
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
      const char *en_path, *de_path;

      /* URI-encode the path, and verify the results. */
      en_path = svn_path_uri_encode (paths[i][0], pool);
      if (strcmp (en_path, paths[i][1]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL,
             "svn_path_uri_encode ('%s') returned '%s' instead of '%s'",
             paths[i][0], en_path, paths[i][1]);
        }
 
      /* URI-decode the path, and make sure we're back where we started. */
      de_path = svn_path_uri_decode (en_path, pool);
      if (strcmp (de_path, paths[i][0]))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, 0, NULL,
             "svn_path_uri_decode ('%s') returned '%s' instead of '%s'",
             paths[i][1], de_path, paths[i][0]);
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
        return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL,
                                 "svn_path_join(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);

      result = svn_path_join_many(pool, base, comp, NULL);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL,
                                 "svn_path_join_many(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);
    }

#define TEST_MANY(args, expect) \
  result = svn_path_join_many args ; \
  if (strcmp(result, expect) != 0) \
    return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL, \
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
test_basename (const char **msg,
               svn_boolean_t msg_only,
               apr_pool_t *pool)
{
  int i;
  char *result;

  static const char * const paths[][2] = {
    { "abc", "abc" },
    { "/abc", "abc" },
    { "//abc", "abc" },
    { "/x/abc", "abc" },
    { "/xx/abc", "abc" },
    { "/xx//abc", "abc" },
    { "/xx//abc", "abc" },
    { "a", "a" },
    { "/a", "a" },
    { "/b/a", "a" },
    { "/b//a", "a" },
    { "/", "/" },
    { SVN_EMPTY_PATH, SVN_EMPTY_PATH }
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
        return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL,
                                 "svn_path_basename(\"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 path, result, expect);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_decompose (const char **msg,
                svn_boolean_t msg_only,
                apr_pool_t *pool)
{
  static const char * const paths[] = {
    "/", "/", NULL,
    "foo", "foo", NULL,
    "/foo", "/", "foo", NULL,
    "/foo/bar", "/", "foo", "bar", NULL,
    "foo/bar", "foo", "bar", NULL,
    "foo/../bar", "foo", "..", "bar", NULL,
    "./foo", SVN_EMPTY_PATH, "foo", NULL,

    /* Are these canonical? Should the middle bits produce SVN_EMPTY_PATH? */
    "foo//bar", "foo", SVN_EMPTY_PATH, "bar", NULL,
    "foo//.//bar",
    "foo", SVN_EMPTY_PATH, SVN_EMPTY_PATH, SVN_EMPTY_PATH, "bar", NULL,

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
                return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL,
                                         "svn_path_decompose(\"%s\") returned "
                                         "unexpected component \"%s\"",
                                         paths[i], component);
              if (strcmp (component, paths[i+j+1])) 
                return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL,
                                         "svn_path_decompose(\"%s\") returned "
                                         "\"%s\" expected \"%s\"",
                                         paths[i], component, paths[i+j+1]);
            }
          if (paths[i+j+1])
            return svn_error_createf(SVN_ERR_TEST_FAILED, 0, NULL,
                                     "svn_path_decompose(\"%s\") failed "
                                     "to return \"%s\"",
                                     paths[i], paths[i+j+1]);
          i += components->nelts + 2;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_canonicalize (const char **msg,
                   svn_boolean_t msg_only,
                   apr_pool_t *pool)
{
  const char *paths[][2] = {
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

    /* Should canonicalization do these? */
    { "foo//.//bar",          "foo//.//bar" },
    { "///foo",               "///foo" },
    { "/.//./.foo",           "/.//./.foo" },
    { ".///.foo",             ".///.foo" },
    { NULL, NULL }
  };
  int i;

  *msg = "test svn_path_decompose";
  if (msg_only)
    return SVN_NO_ERROR;

  i = 0;
  while (paths[i][0])
    {
      const char *canonical = svn_path_canonicalize (paths[i][0], pool);

      if (strcmp (canonical, paths[i][1]))
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL,
                                  "svn_path_canonicalize(\"%s\") returned "
                                  "\"%s\" expected \"%s\"",
                                  paths[i][0], canonical, paths[i][1]);

      if (strcmp (paths[i][0], paths[i][1]) == 0 && canonical != paths[i][0])
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL,
                                  "svn_path_canonicalize(\"%s\") alloc'd",
                                  paths[i][0]);

      ++i;
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS (test_path_is_child),
    SVN_TEST_PASS (test_path_split),
    SVN_TEST_PASS (test_is_url),
    SVN_TEST_PASS (test_uri_encode),
    SVN_TEST_PASS (test_join),
    SVN_TEST_PASS (test_basename),
    SVN_TEST_PASS (test_decompose),
    SVN_TEST_PASS (test_canonicalize),
    SVN_TEST_NULL
  };
