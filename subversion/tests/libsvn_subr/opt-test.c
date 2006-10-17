/*
 * opt-test.c -- test the option functions
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include <string.h>
#include "svn_opt.h"
#include <apr_general.h>

#include "../svn_test.h"


static svn_error_t *
test_parse_peg_rev(const char **msg,
                   svn_boolean_t msg_only,
                   svn_test_opts_t *opts,
                   apr_pool_t *pool)
{      
  apr_size_t i;
  static struct {
      const char *input;
      const char *path; /* NULL means an error is expected. */
      svn_opt_revision_t peg;
  } const tests[] = {
    { "foo/bar",              "foo/bar",      {svn_opt_revision_unspecified} },
    { "foo/bar@13",           "foo/bar",      {svn_opt_revision_number, {13}} },
    { "foo/bar@HEAD",         "foo/bar",      {svn_opt_revision_head} },
    { "foo/bar@{1999-12-31}", "foo/bar",      {svn_opt_revision_date, {0}} },
    { "http://a/b@27",        "http://a/b",   {svn_opt_revision_number, {27}} },
    { "http://a/b@COMMITTED", "http://a/b",   {svn_opt_revision_committed} },
    { "foo/bar@1:2",          NULL,           {svn_opt_revision_unspecified} },
    { "foo/bar@baz",          NULL,           {svn_opt_revision_unspecified} },
    { "foo/bar@",             "foo/bar",      {svn_opt_revision_base} },
    { "foo/bar/@13",          "foo/bar",      {svn_opt_revision_number, {13}} },
    { "foo/bar@@13",          "foo/bar@",     {svn_opt_revision_number, {13}} },
    { "foo/@bar@HEAD",        "foo/@bar",     {svn_opt_revision_head} },
    { "foo@/bar",             "foo@/bar",     {svn_opt_revision_unspecified} },
    { "foo@HEAD/bar",         "foo@HEAD/bar", {svn_opt_revision_unspecified} },
  };

  *msg = "test svn_opt_parse_path";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      const char *path;
      svn_opt_revision_t peg;
      svn_error_t *err;

      err = svn_opt_parse_path(&peg, &path, tests[i].input, pool);
      if (err)
        {
          svn_error_clear(err);
          if (tests[i].path)
            {
              return svn_error_createf
                (SVN_ERR_TEST_FAILED, NULL,
                 "svn_opt_parse_path ('%s') returned an error instead of '%s'",
                 tests[i].input, tests[i].path);
            }
        }
      else
        {
          if ((path == NULL)
              || (tests[i].path == NULL)
              || (strcmp(path, tests[i].path) != 0)
              || (peg.kind != tests[i].peg.kind)
              || (peg.kind == svn_opt_revision_number && peg.value.number != tests[i].peg.value.number))
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, NULL,
               "svn_opt_parse_path ('%s') returned '%s' instead of '%s'", tests[i].input,
               path ? path : "NULL", tests[i].path ? tests[i].path : "NULL");
        }
    }
  
  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_parse_peg_rev),
    SVN_TEST_NULL
  };
