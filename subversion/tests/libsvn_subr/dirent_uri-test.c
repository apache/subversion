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

#ifdef _MSC_VER
#include <direct.h>
#define getcwd _getcwd
#define getdcwd _getdcwd
#else
#include <unistd.h> /* for getcwd() */
#endif

#include <apr_general.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"

#include "../svn_test.h"
#include "../../libsvn_subr/private_uri.h"

#define SVN_EMPTY_PATH ""

#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))

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
    { "",              FALSE },
#if defined(WIN32) || defined(__CYGWIN__)
    { "X:/foo",        FALSE },
    { "X:/",           TRUE },
    { "X:foo",         FALSE },
    { "X:",            TRUE },
    { "//srv/shr",     TRUE },
    { "//srv/shr/fld", FALSE },
#else /* WIN32 or Cygwin */
    { "/X:foo",        FALSE },
    { "/X:",           FALSE },
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_is_root";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
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
test_uri_is_root(const char **msg,
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
    { "X:/foo",        FALSE },
    { "X:/",           FALSE },
    { "X:foo",         FALSE },
    { "X:",            FALSE },
  };

  *msg = "test svn_uri_is_root";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      svn_boolean_t retval;

      retval = svn_uri_is_root(tests[i].path, strlen(tests[i].path));
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_uri_is_root (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE",
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_is_absolute(const char **msg,
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
    { "",              FALSE },
#if defined(WIN32) || defined(__CYGWIN__)
    { "X:/foo",        TRUE },
    { "X:/",           TRUE },
    { "//srv/shr",     TRUE },
    { "//srv/shr/fld", TRUE },
#else/* WIN32 or Cygwin */
    { "X:/foo",        FALSE },
    { "X:/",           FALSE },
    { "X:foo",         FALSE },
    { "X:foo/bar",     FALSE },
    { "X:",            FALSE },
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_is_absolute";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      svn_boolean_t retval;

      retval = svn_dirent_is_absolute(tests[i].path);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_dirent_is_absolute (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE",
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_is_absolute(const char **msg,
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
    { "",              FALSE },
    { "X:/foo",        FALSE },
    { "X:foo",         FALSE },
    { "X:foo/bar",     FALSE },
    { "X:",            FALSE },
    { "http://",       TRUE },
    { "http://test",   TRUE },
    { "http://foo/bar",TRUE },
  };

  *msg = "test svn_uri_is_absolute";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      svn_boolean_t retval;

      retval = svn_uri_is_absolute(tests[i].path);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_uri_is_absolute (%s) returned %s instead of %s",
           tests[i].path, retval ? "TRUE" : "FALSE",
           tests[i].result ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_join(const char **msg,
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
#if defined(WIN32) || defined(__CYGWIN__)
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
    { "//srv/shr",     "fld",     "//srv/shr/fld" },
    { "//srv/shr/fld", "subfld",  "//srv/shr/fld/subfld" },
    { "//srv/shr/fld", "//srv/shr", "//srv/shr" },
#else /* WIN32 or Cygwin */
    { "X:abc", "X:/def", "X:abc/X:/def" },
    { "X:","abc", "X:/abc" },
    { "X:/abc", "X:/def", "X:/abc/X:/def" },
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_join(_many)";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(joins); i++ )
    {
      const char *base = joins[i][0];
      const char *comp = joins[i][1];
      const char *expect = joins[i][2];

      result = svn_dirent_join(base, comp, pool);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_join(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);

      result = svn_dirent_join_many(pool, base, comp, NULL);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_join_many(\"%s\", \"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 base, comp, result, expect);
    }

#define TEST_MANY(args, expect) \
  result = svn_dirent_join_many args ; \
  if (strcmp(result, expect) != 0) \
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL, \
                             "svn_dirent_join_many" #args " returns \"%s\". " \
                             "expected \"%s\"", \
                             result, expect);

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

#if defined(WIN32) || defined(__CYGWIN__)
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
  TEST_MANY((pool, "//srv/shr", "def", "ghi", NULL), "//srv/shr/def/ghi");
  TEST_MANY((pool, "//srv/shr/fld", "def", "ghi", NULL), "//srv/shr/fld/def/ghi");
  TEST_MANY((pool, "//srv/shr/fld", "def", "//srv/shr", NULL), "//srv/shr");
  TEST_MANY((pool, SVN_EMPTY_PATH, "//srv/shr/fld", "def", "ghi", NULL), "//srv/shr/fld/def/ghi");
  TEST_MANY((pool, SVN_EMPTY_PATH, "//srv/shr/fld", "def", "//srv/shr", NULL), "//srv/shr");
#else /* WIN32 or Cygwin */
  TEST_MANY((pool, "X:", "def", "ghi", NULL), "X:/def/ghi");
  TEST_MANY((pool, "X:", SVN_EMPTY_PATH, "ghi", NULL), "X:/ghi");
  TEST_MANY((pool, "X:", "def", SVN_EMPTY_PATH, NULL), "X:/def");
  TEST_MANY((pool, SVN_EMPTY_PATH, "X:", "ghi", NULL), "X:/ghi");
#endif /* non-WIN32 */

  /* ### probably need quite a few more tests... */

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_dirname(const char **msg,
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
    { "X:abc/def", "X:abc" },
#if defined(WIN32) || defined(__CYGWIN__)
    { "X:/", "X:/" },
    { "X:/abc", "X:/" },
    { "X:abc", "X:" },
    { "X:", "X:" },
    { "//srv/shr",      "//srv/shr" },
    { "//srv/shr/fld",  "//srv/shr" },
    { "//srv/shr/fld/subfld", "//srv/shr/fld" },
#else  /* WIN32 or Cygwin */
    /* on non-Windows platforms, ':' is allowed in pathnames */
    { "X:", "" },
    { "X:abc", "" },
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_dirname";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *path = tests[i].path;
      const char *expect = tests[i].result;

      result = svn_dirent_dirname(path, pool);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_dirname(\"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 path, result, expect);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_dirname(const char **msg,
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
    { "http://server/dir", "http://server" },
    { "http://server/dir/file", "http://server/dir" },
    { "http://server", "http:/" }, /* ### Current behavior */
  };

  *msg = "test svn_dirent_dirname";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *path = tests[i].path;
      const char *expect = tests[i].result;

      result = svn_uri_dirname(path, pool);
      if (strcmp(result, expect))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_uri_dirname(\"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 path, result, expect);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_canonicalize(const char **msg,
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
    { "X:/foo",               "X:/foo" },
    { "X:",                   "X:" },
    { "X:foo",                "X:foo" },
    { "C:/folder/subfolder/file", "C:/folder/subfolder/file" },
#if defined(WIN32) || defined(__CYGWIN__)
    { "X:/",                  "X:/" },
    { "X:/./",                "X:/" },
    /* We permit UNC dirents on Windows.  By definition UNC
     * dirents must have two components so we should remove the
     * double slash if there is only one component. */
    { "//hst/foo",            "//hst/foo" },
    { "//hst",                "/hst" },
    { "//hst/./",             "/hst" },
    { "//server/share/",      "//server/share" },
    { "//server/SHare/",      "//server/SHare" },
    { "//SERVER/SHare/",      "//server/SHare" },
#endif /* WIN32 or Cygwin */
  };
  int i;

  *msg = "test svn_dirent_canonicalize";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *canonical = svn_dirent_canonicalize(tests[i].path, pool);

      if (strcmp(canonical, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_canonicalize(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, canonical, tests[i].result);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_canonicalize(const char **msg,
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
    { "X:/foo",               "X:/foo" },
    { "X:",                   "X:" },
    { "X:foo",                "X:foo" },
    { "C:/folder/subfolder/file", "C:/folder/subfolder/file" },
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
    { "file://SRV/shr/repos",  "file://srv/shr/repos" },
    { "file://SRV/SHR/REPOS",  "file://srv/SHR/REPOS" },
#if defined(WIN32) || defined(__CYGWIN__)
    { "file:///c:/temp/repos", "file:///C:/temp/repos" },
    { "file:///c:/temp/REPOS", "file:///C:/temp/REPOS" },
    { "file:///C:/temp/REPOS", "file:///C:/temp/REPOS" },
#else /* WIN32 or Cygwin */
    { "file:///c:/temp/repos", "file:///c:/temp/repos" },
    { "file:///c:/temp/REPOS", "file:///c:/temp/REPOS" },
    { "file:///C:/temp/REPOS", "file:///C:/temp/REPOS" },
#endif /* non-WIN32 */
  };
  int i;

  *msg = "test svn_uri_canonicalize";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)

    {
      const char *canonical = svn_uri_canonicalize(tests[i].path, pool);

      if (strcmp(canonical, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_uri_canonicalize(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, canonical, tests[i].result);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_is_canonical(const char **msg,
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
    { "foo/./bar",             FALSE },
    { "C:/folder/subfolder/file", TRUE },
    { "X:/foo",                TRUE },
    { "X:",                    TRUE },
    { "X:foo",                 TRUE },
    { "X:foo/",                FALSE },
    { "file with spaces",      TRUE },
#if defined(WIN32) || defined(__CYGWIN__)
    { "X:/",                   TRUE },
    /* We permit UNC dirents on Windows.  By definition UNC
     * dirents must have two components so we should remove the
     * double slash if there is only one component. */
    { "//hst",                 FALSE },
    { "//hst/./",              FALSE },
    { "//server/share/",       FALSE },
    { "//server/share",        TRUE },
    { "//server/SHare",        TRUE },
    { "//SERVER/SHare",        FALSE },
#else /* WIN32 or Cygwin */
    { "X:/",                   FALSE },
    /* Some people use colons in their filenames. */
    { ":", TRUE },
    { ".:", TRUE },
    { "foo/.:", TRUE },
#endif /* non-WIN32 */
  };
  int i;

  *msg = "test svn_dirent_is_canonical";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      svn_boolean_t canonical;
      const char* canonicalized;

      canonical = svn_dirent_is_canonical(tests[i].path, pool);
      if (tests[i].canonical != canonical)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_is_canonical(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path,
                                 canonical ? "TRUE" : "FALSE",
                                 tests[i].canonical ? "TRUE" : "FALSE");

      canonicalized = svn_dirent_canonicalize(tests[i].path, pool);

      if (canonical && (strcmp(tests[i].path, canonicalized) != 0))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_canonicalize(\"%s\") returned \"%s\" "
                                 "while svn_dirent_is_canonical returned TRUE",
                                 tests[i].path,
                                 canonicalized);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_is_canonical(const char **msg,
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
    { "foo/./bar",             FALSE },
    { "http://hst",            TRUE },
    { "http://hst/foo/../bar", TRUE },
    { "http://hst/",           FALSE },
    { "http://HST/",           FALSE },
    { "http://HST/FOO/BaR",    FALSE },
    { "svn+ssh://jens@10.0.1.1", TRUE },
    { "svn+ssh://j.raNDom@HST/BaR", FALSE },
    { "svn+SSH://j.random:jRaY@HST/BaR", FALSE },
    { "SVN+ssh://j.raNDom:jray@HST/BaR", FALSE },
    { "svn+ssh://j.raNDom:jray@hst/BaR", TRUE },
    { "fILe:///Users/jrandom/wc", FALSE },
    { "fiLE:///",              FALSE },
    { "fiLE://",               FALSE },
    { "C:/folder/subfolder/file", TRUE },
    { "X:/foo",                TRUE },
    { "X:",                    TRUE },
    { "X:foo",                 TRUE },
    { "X:foo/",                FALSE },
    /* Some people use colons in their filenames. */
    { ":", TRUE },
    { ".:", TRUE },
    { "foo/.:", TRUE },
    { "file://SRV/share/repos", FALSE },
    { "file://srv/SHARE/repos", TRUE },
    { "file://srv/share/repos", TRUE },
    { "//server/share",         FALSE }, /* Only valid as dirent */
    { "//server",               FALSE },
    { "//",                     FALSE },
#if defined(WIN32) || defined(__CYGWIN__)
    { "file:///c:/temp/repos", FALSE },
    { "file:///c:/temp/REPOS", FALSE },
    { "file:///C:/temp/REPOS", TRUE },
#else /* WIN32 or Cygwin */
    { "file:///c:/temp/repos", TRUE },
    { "file:///c:/temp/REPOS", TRUE },
    { "file:///C:/temp/REPOS", TRUE },
#endif /* non-WIN32 */
  };
  int i;

  *msg = "test svn_uri_is_canonical";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      svn_boolean_t canonical;
      const char* canonicalized;

      canonical = svn_uri_is_canonical(tests[i].path, pool);
      if (tests[i].canonical != canonical)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_uri_is_canonical(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path,
                                 canonical ? "TRUE" : "FALSE",
                                 tests[i].canonical ? "TRUE" : "FALSE");

      canonicalized = svn_uri_canonicalize(tests[i].path, pool);

      if (canonical && (strcmp(tests[i].path, canonicalized) != 0))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_uri_canonicalize(\"%s\") returned \"%s\" "
                                 "while svn_uri_is_canonical returned %s",
                                 tests[i].path,
                                 canonicalized,
                                 canonical ? "TRUE" : "FALSE");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_is_ancestor(const char **msg,
                        svn_boolean_t msg_only,
                        svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  apr_size_t i;

  /* Dirents to test and their expected results. */
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
    { SVN_EMPTY_PATH,    "/",             FALSE},
    { SVN_EMPTY_PATH,    "/foo",          FALSE},
    { "/.bar",           "/",             FALSE},
    { "foo/bar",         "foo",           FALSE},
    { "/foo/bar",        "/foo",          FALSE},
    { "foo",             "foo/bar",       TRUE},
    { "foo.",            "foo./.bar",     TRUE},

    { "../foo",          "..",            FALSE},
    { SVN_EMPTY_PATH,    SVN_EMPTY_PATH,  TRUE},
    { "/",               "/",             TRUE},
    { "X:foo",           "X:bar",         FALSE},
#if defined(WIN32) || defined(__CYGWIN__)
    { "//srv/shr",       "//srv",         FALSE},
    { "//srv/shr",       "//srv/shr/fld", TRUE },
    { "//srv",           "//srv/shr/fld", TRUE },
    { "//srv/shr/fld",   "//srv/shr",     FALSE },
    { "//srv/shr/fld",   "//srv2/shr/fld", FALSE },
    { "X:/",             "X:/",           TRUE},
    { "X:/foo",          "X:/",           FALSE},
    { "X:/",             "X:/foo",        TRUE},
    { "X:",              "X:foo",         TRUE},
    { SVN_EMPTY_PATH,    "C:/",           FALSE},
#else /* WIN32 or Cygwin */
    { "X:",              "X:foo",         FALSE},
    { SVN_EMPTY_PATH,    "C:/",           TRUE},
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_is_ancestor";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      svn_boolean_t retval;

      retval = svn_dirent_is_ancestor(tests[i].path1, tests[i].path2);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_dirent_is_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path1, tests[i].path2, retval ? "TRUE" : "FALSE",
           tests[i].result ? "TRUE" : "FALSE");
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_is_ancestor(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  apr_size_t i;

  /* URIs to test and their expected results. */
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
    { SVN_EMPTY_PATH,    "/",             FALSE},
    { SVN_EMPTY_PATH,    "/foo",          FALSE},
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
    { "http://",        "http://test",     TRUE},
    { SVN_EMPTY_PATH,   "http://test",     FALSE},
    { "X:foo",          "X:bar",           FALSE},
    { "X:",             "X:foo",           FALSE},
  };

  *msg = "test svn_uri_is_ancestor";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      svn_boolean_t retval;

      retval = svn_uri_is_ancestor(tests[i].path1, tests[i].path2);
      if (tests[i].result != retval)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_uri_is_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path1, tests[i].path2, retval ? "TRUE" : "FALSE",
           tests[i].result ? "TRUE" : "FALSE");
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_get_longest_ancestor(const char **msg,
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
    { "/foo/bar",       "foo/bar",         SVN_EMPTY_PATH},
    { "/",              "/foo",            "/"},
    { SVN_EMPTY_PATH,   "foo",             SVN_EMPTY_PATH},
    { SVN_EMPTY_PATH,   ".bar",            SVN_EMPTY_PATH},
    { "/.bar",          "/",               "/"},
    { "foo/bar",        "foo",             "foo"},
    { "/foo/bar",       "/foo",            "/foo"},
    { "/rif",           "/raf",            "/"},
    { "foo",            "bar",             SVN_EMPTY_PATH},
    { "foo",            "foo/bar",         "foo"},
    { "foo.",           "foo./.bar",       "foo."},
    { SVN_EMPTY_PATH,   SVN_EMPTY_PATH,    SVN_EMPTY_PATH},
    { "/",              "/",               "/"},
    { "X:foo",          "Y:foo",           SVN_EMPTY_PATH},
    { "X:/folder1",     "Y:/folder2",      SVN_EMPTY_PATH},
#if defined(WIN32) || defined(__CYGWIN__)
    { "X:/",            "X:/",             "X:/"},
    { "X:/foo/bar/A/D/H/psi", "X:/foo/bar/A/B", "X:/foo/bar/A" },
    { "X:/foo/bar/boo", "X:/foo/bar/baz/boz", "X:/foo/bar"},
    { "X:foo/bar",      "X:foo/bar/boo",   "X:foo/bar"},
    { "//srv/shr",      "//srv/shr/fld",   "//srv/shr" },
    { "//srv/shr/fld",  "//srv/shr",       "//srv/shr" },
    { "//srv/shr/fld",  "//srv2/shr/fld",  SVN_EMPTY_PATH },
    { "X:/foo",         "X:/",             "X:/"},
    { "X:/folder1",     "X:/folder2",      "X:/"},
    { "X:/",            "X:/foo",          "X:/"},
    { "X:",             "X:foo",           "X:"},
    { "X:",             "X:/",             SVN_EMPTY_PATH},
    { "X:foo",          "X:bar",           "X:"},
#else /* WIN32 or Cygwin */
    { "X:/foo",         "X:",              "X:"},
    { "X:/folder1",     "X:/folder2",      "X:"},
    { "X:",             "X:foo",           SVN_EMPTY_PATH},
    { "X:foo",          "X:bar",           SVN_EMPTY_PATH},
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_get_longest_ancestor";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *retval;

      retval = svn_dirent_get_longest_ancestor(tests[i].path1, tests[i].path2,
                                               pool);

      if (strcmp(tests[i].result, retval))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_dirent_get_longest_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path1, tests[i].path2, retval, tests[i].result);

      /* changing the order of the paths should return the same results */
      retval = svn_dirent_get_longest_ancestor(tests[i].path2, tests[i].path1,
                                               pool);

      if (strcmp(tests[i].result, retval))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_dirent_get_longest_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path2, tests[i].path1, retval, tests[i].result);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_get_longest_ancestor(const char **msg,
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
    { "/foo/bar",       "foo/bar",         SVN_EMPTY_PATH},
    { "/",              "/foo",            "/"},
    { SVN_EMPTY_PATH,   "foo",             SVN_EMPTY_PATH},
    { SVN_EMPTY_PATH,   ".bar",            SVN_EMPTY_PATH},
    { "/.bar",          "/",               "/"},
    { "foo/bar",        "foo",             "foo"},
    { "/foo/bar",       "/foo",            "/foo"},
    { "/rif",           "/raf",            "/"},
    { "foo",            "bar",             SVN_EMPTY_PATH},
    { "foo",            "foo/bar",         "foo"},
    { "foo.",           "foo./.bar",       "foo."},
    { SVN_EMPTY_PATH,   SVN_EMPTY_PATH,    SVN_EMPTY_PATH},
    { "/",              "/",               "/"},
    { "http://test",    "http://test",     "http://test"},
    { "http://test",    "http://taste",    SVN_EMPTY_PATH},
    { "http://test",    "http://test/foo", "http://test"},
    { "http://test",    "file://test/foo", SVN_EMPTY_PATH},
    { "http://test",    "http://testF",    SVN_EMPTY_PATH},
    { "http://",        "http://test",     SVN_EMPTY_PATH},
    { "file:///A/C",    "file:///B/D",     SVN_EMPTY_PATH},
    { "file:///A/C",    "file:///A/D",     "file:///A"},
    { "X:/foo",         "X:",              "X:"},
    { "X:/folder1",     "X:/folder2",      "X:"},
    { "X:",             "X:foo",           SVN_EMPTY_PATH},
    { "X:foo",          "X:bar",           SVN_EMPTY_PATH},
  };

  *msg = "test svn_uri_get_longest_ancestor";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *retval;

      retval = svn_uri_get_longest_ancestor(tests[i].path1, tests[i].path2,
                                             pool);

      if (strcmp(tests[i].result, retval))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_uri_get_longest_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path1, tests[i].path2, retval, tests[i].result);

      /* changing the order of the paths should return the same results */
      retval = svn_uri_get_longest_ancestor(tests[i].path2, tests[i].path1,
                                             pool);

      if (strcmp(tests[i].result, retval))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_uri_get_longest_ancestor (%s, %s) returned %s instead of %s",
           tests[i].path2, tests[i].path1, retval, tests[i].result);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_is_child(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  int i, j;

  static const char * const paths[] = {
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
#if defined(WIN32) || defined(__CYGWIN__)
    "//srv",
    "//srv2",
    "//srv/shr",
    "//srv/shr/fld",
    "H:/foo/bar",
    "H:/foo/baz",
    "H:/foo/bar/baz",
    "H:/flu/blar/blaz",
    "H:/foo/bar/baz/bing/boom",
    "H:/",
    "H:/iota",
    "H:",
    "H:foo",
    "H:foo/baz",
#endif /* Win32 and Cygwin */
    };

  /* Maximum number of path[] items for all platforms */
#define MAX_PATHS 32

  static const char * const
    remainders[COUNT_OF(paths)][MAX_PATHS] = {
    { 0, 0, 0, "baz", 0, "baz/bing/boom", 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, "bing/boom", 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, "foo", ".foo", 0, "foo2",
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "foo/bar", "foo/bars", "foo/baz", "foo/bar/baz", "flu/blar/blaz",
      "foo/bar/baz/bing/boom", 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
#if defined(WIN32) || defined(__CYGWIN__)
    /* //srv paths */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, "shr", "shr/fld", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, "fld", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* H:/ paths */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, "baz", 0, "baz/bing/boom", 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, "bing/boom", 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, "foo/bar", "foo/baz", "foo/bar/baz", "flu/blar/blaz",
      "foo/bar/baz/bing/boom", 0, "iota", 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* H: paths */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "foo", "foo/baz" },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "baz" },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
#endif /* Win32 and Cygwin */
  };

  *msg = "test svn_dirent_is_child";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(paths); i++)
    {
      for (j = 0; j < COUNT_OF(paths); j++)
        {
          const char *remainder;

          remainder = svn_dirent_is_child(paths[i], paths[j], pool);

          if (((remainder) && (! remainders[i][j]))
              || ((! remainder) && (remainders[i][j]))
              || (remainder && strcmp(remainder, remainders[i][j])))
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, NULL,
               "svn_dirent_is_child (%s, %s) returned '%s' instead of '%s'",
               paths[i], paths[j],
               remainder ? remainder : "(null)",
               remainders[i][j] ? remainders[i][j] : "(null)" );
        }
    }

#undef NUM_TEST_PATHS
  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_is_child(const char **msg,
                  svn_boolean_t msg_only,
                  svn_test_opts_t *opts,
                  apr_pool_t *pool)
{
  int i, j;

#define NUM_TEST_PATHS 16

  static const char * const paths[] = {
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
    "H:/foo/bar",
    "H:/foo/baz",
    "H:",
    "H:foo",
    "H:foo/baz",
    };

  static const char * const
    remainders[COUNT_OF(paths)][COUNT_OF(paths)] = {
    { 0, 0, 0, "baz", 0, "baz/bing/boom", 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, "bing/boom", 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, "foo", ".foo", 0, "foo2",
      "H:/foo/bar", "H:/foo/baz", "H:", "H:foo", "H:foo/baz" },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { "foo/bar", "foo/bars", "foo/baz", "foo/bar/baz", "flu/blar/blaz",
      "foo/bar/baz/bing/boom", 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    /* H:/ paths */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
    /* H: paths */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      "foo/bar", "foo/baz", 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, "baz" },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0 },
  };

  *msg = "test svn_uri_is_child";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(paths); i++)
    {
      for (j = 0; j < COUNT_OF(paths); j++)
        {
          const char *remainder;

          remainder = svn_uri_is_child(paths[i], paths[j], pool);

          if (((remainder) && (! remainders[i][j]))
              || ((! remainder) && (remainders[i][j]))
              || (remainder && strcmp(remainder, remainders[i][j])))
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, NULL,
               "svn_uri_is_child (%s, %s) returned '%s' instead of '%s'",
               paths[i], paths[j],
               remainder ? remainder : "(null)",
               remainders[i][j] ? remainders[i][j] : "(null)" );
        }
    }

#undef NUM_TEST_PATHS
  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_get_absolute(const char **msg,
                         svn_boolean_t msg_only,
                         svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  int i;
  const char *curdir;
  char buf[8192];
#if defined(WIN32) || defined(__CYGWIN__)
  const char *curdironc;
  char curdrive[3] = "C:";
#endif /* WIN32 */

  struct {
    const char *path;
    const char *result;
  } tests[] = {
    /* '%' will be replaced by the current working dir. */
    { "abc", "%/abc" },
    { SVN_EMPTY_PATH, "%" },
#if defined(WIN32) || defined(__CYGWIN__)
    /* '@' will be replaced by the current working dir on C:\. */
    /* '$' will be replaced by the current drive */
    { "C:/", "C:/" },
    { "C:/abc", "C:/abc" },
    { "C:abc", "@/abc" },
    { "C:", "@" },
    { "/", "$/" },
    { "/x/abc", "$/x/abc" },
    /* svn_dirent_get_absolute will check existence of this UNC shares on the
       test machine, so we can't really test this.
    { "//srv/shr",      "//srv/shr" },
    { "//srv/shr/fld",  "//srv/shr" },
    { "//srv/shr/fld/subfld", "//srv/shr/fld" }, */
#else  /* WIN32 or Cygwin */
    { "/abc", "/abc" },
    { "/x/abc", "/x/abc" },
    { "X:", "%/X:" },
    { "X:abc", "%/X:abc" },
#endif /* non-WIN32 */
  };

  *msg = "test svn_dirent_get_absolute";
  if (msg_only)
    return SVN_NO_ERROR;

  if (! getcwd(buf, sizeof(buf)))
    return svn_error_create(SVN_ERR_BASE, NULL, "getcwd() failed");

  curdir = svn_dirent_internal_style(buf, pool);

#if defined(WIN32) || defined(__CYGWIN__)
  if (! getdcwd(3, buf, sizeof(buf))) /* 3 stands for drive C: */
    return svn_error_create(SVN_ERR_BASE, NULL, "getdcwd() failed");

  curdironc = svn_dirent_internal_style(buf, pool);
  curdrive[0] = curdir[0];
#endif /* WIN32 */

  for (i = 0 ; i < COUNT_OF(tests) ; i++ )
    {
      const char *path = tests[i].path;
      const char *expect = tests[i].result;
      const char *expect_abs, *result;

      expect_abs = expect;
      if (*expect == '%')
        expect_abs = apr_pstrcat(pool, curdir, expect + 1, NULL);
#if defined(WIN32) || defined(__CYGWIN__)
      if (*expect == '@')
        expect_abs = apr_pstrcat(pool, curdironc, expect + 1, NULL);

      if (*expect == '$')
        expect_abs = apr_pstrcat(pool, curdrive, expect + 1, NULL);

      /* Remove double '/' when CWD was the root dir (E.g. C:/) */
      expect_abs = svn_dirent_canonicalize(expect_abs, pool);
#endif /* WIN32 */

      SVN_ERR(svn_dirent_get_absolute(&result, path, pool));
      if (strcmp(result, expect_abs))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_get_absolute(\"%s\") returned "
                                 "\"%s\". expected \"%s\"",
                                 path, result, expect_abs);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_local_style(const char **msg,
                        svn_boolean_t msg_only,
                        svn_test_opts_t *opts,
                        apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "",                     "." },
    { ".",                    "." },
#if defined(WIN32) || defined(__CYGWIN__)
    { "a:/",                 "a:\\" },
    { "a:/file",             "a:\\file" },
    { "dir/file",            "dir\\file" },
    { "/",                   "\\" },
    { "//server/share/dir",  "\\\\server\\share\\dir" },
#else
    { "a:/",                 "a:" }, /* Wrong but expected for svn_path_*() */
    { "a:/file",             "a:/file" },
    { "dir/file",            "dir/file" },
    { "/",                   "/" },
    { "//server/share/dir",  "/server/share/dir" },
#endif
  };
  int i;

  *msg = "test svn_dirent_local_style";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *local = svn_dirent_local_style(tests[i].path, pool);

      if (strcmp(local, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_local_style(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, local, tests[i].result);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_local_style(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "",                     "." },
    { ".",                    "." },
#if defined(WIN32) || defined(__CYGWIN__)
    /* Rules are as uri, but paths are shown with local separator */
    { "a:/",                 "a:" },
    { "a:/file",             "a:\\file" },
    { "dir/file",            "dir\\file" },
    { "/",                   "\\" },
    { "//server/share/dir",  "\\server\\share\\dir" },
#else
    { "a:/",                 "a:" },
    { "a:/file",             "a:/file" },
    { "dir/file",            "dir/file" },
    { "/",                   "/" },
    { "//server/share/dir",  "/server/share/dir" },
#endif
  };
  int i;

  *msg = "test svn_uri_local_style";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *local = svn_uri_local_style(tests[i].path, pool);

      if (strcmp(local, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_uri_local_style(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, local, tests[i].result);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_dirent_internal_style(const char **msg,
                           svn_boolean_t msg_only,
                           svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  struct {
    const char *path;
    const char *result;
  } tests[] = {
    { "",                    "" },
    { ".",                   "" },
    { "/",                   "/" },
    { "file",                "file" },
    { "dir/file",            "dir/file" },
#if defined(WIN32) || defined(__CYGWIN__)
    { "a:\\",                "a:/" },
    { "a:\\file",            "a:/file" },
    { "a:file",              "a:file" },
    { "dir\\file",           "dir/file" },
    { "\\\\srv\\shr\\dir",   "//srv/shr/dir" },
    { "\\\\srv\\shr\\",      "//srv/shr" },
    { "//srv/shr",           "//srv/shr" },
#else
    { "a:/",                 "a:" }, /* Wrong but expected for svn_path_*() */
    { "a:/file",             "a:/file" },
    { "dir/file",            "dir/file" },
    { "/",                   "/" },
    { "//server/share/dir",  "/server/share/dir" },
#endif
  };
  int i;

  *msg = "test svn_dirent_internal_style";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *internal = svn_dirent_internal_style(tests[i].path, pool);

      if (strcmp(internal, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_dirent_internal_style(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, internal, tests[i].result);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uri_internal_style(const char **msg,
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
    { "/",                   "/" },
    { "file",                "file" },
    { "dir/file",            "dir/file" },
#if defined(WIN32) || defined(__CYGWIN__)
    /* Rules are as uri, but paths are shown with internal separator */
    { "a:\\",                "a:" },
    { "a:\\file",            "a:/file" },
    { "a:file",              "a:file" },
    { "dir\\file",           "dir/file" },
    { "//server/share/dir",  "/server/share/dir" },
    { "\\\\srv\\shr\\dir",   "/srv/shr/dir" },
#else
    { "a:/",                 "a:" },
    { "a:/file",             "a:/file" },
    { "dir/file",            "dir/file" },
    { "/",                   "/" },
    { "//server/share/dir",  "/server/share/dir" },
#endif
  };
  int i;

  *msg = "test svn_uri_internal_style";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < COUNT_OF(tests); i++)
    {
      const char *internal = svn_uri_internal_style(tests[i].path, pool);

      if (strcmp(internal, tests[i].result))
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_uri_internal_style(\"%s\") returned "
                                 "\"%s\" expected \"%s\"",
                                 tests[i].path, internal, tests[i].result);
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_dirent_is_root),
    SVN_TEST_PASS(test_uri_is_root),
    SVN_TEST_PASS(test_dirent_is_absolute),
    SVN_TEST_PASS(test_uri_is_absolute),
    SVN_TEST_PASS(test_dirent_join),
    SVN_TEST_PASS(test_dirent_dirname),
    SVN_TEST_PASS(test_uri_dirname),
    SVN_TEST_PASS(test_dirent_canonicalize),
    SVN_TEST_PASS(test_uri_canonicalize),
    SVN_TEST_PASS(test_dirent_is_canonical),
    SVN_TEST_PASS(test_uri_is_canonical),
    SVN_TEST_PASS(test_dirent_get_longest_ancestor),
    SVN_TEST_PASS(test_uri_get_longest_ancestor),
    SVN_TEST_PASS(test_dirent_is_child),
    SVN_TEST_PASS(test_uri_is_child),
    SVN_TEST_PASS(test_dirent_is_ancestor),
    SVN_TEST_PASS(test_uri_is_ancestor),
    SVN_TEST_PASS(test_dirent_get_absolute),
    SVN_TEST_PASS(test_dirent_local_style),
    SVN_TEST_PASS(test_uri_local_style),
    SVN_TEST_PASS(test_dirent_internal_style),
    SVN_TEST_PASS(test_uri_internal_style),
    SVN_TEST_NULL
  };
