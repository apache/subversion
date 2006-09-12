/*
 * config-test.c:  tests svn_config
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

/* ====================================================================
   To add tests, look toward the bottom of this file.

*/



#include <string.h>

#include <apr_getopt.h>
#include <apr_pools.h>

#include "svn_error.h"
#include "svn_config.h"

#include "../svn_test.h"


/* Initialize parameters for the tests. */
extern int test_argc;
extern const char **test_argv;

static const apr_getopt_option_t opt_def[] =
  {
    {"srcdir", 'S', 1, "the source directory for VPATH test runs"},
    {0, 0, 0, 0}
  };
static const char *srcdir = NULL;

static svn_error_t *init_params(apr_pool_t *pool)
{
  apr_getopt_t *opt;
  int optch;
  const char *opt_arg;
  apr_status_t status;

  apr_getopt_init(&opt, pool, test_argc, test_argv);
  while (!(status = apr_getopt_long(opt, opt_def, &optch, &opt_arg)))
    {
      switch (optch)
        {
        case 'S':
          srcdir = opt_arg;
          break;
        }
    }

  if (!srcdir)
    return svn_error_create(SVN_ERR_TEST_FAILED, 0,
                            "missing required parameter '--srcdir'");

  return SVN_NO_ERROR;
}

/* A quick way to create error messages.  */
static svn_error_t *
fail(apr_pool_t *pool, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_error_create(SVN_ERR_TEST_FAILED, 0, msg);
}


static const char *config_keys[] = { "foo", "a", "b", "c", "d", "e", "f", "g",
                                     "h", "i", NULL };
static const char *config_values[] = { "bar", "Aa", "100", "bar",
                                       "a %(bogus)s oyster bar",
                                       "%(bogus)s shmoo %(",
                                       "%Aa", "lyrical bard", "%(unterminated",
                                       "Aa 100", NULL };

static svn_error_t *
test1(const char **msg, 
      svn_boolean_t msg_only,
      svn_test_opts_t *opts,
      apr_pool_t *pool)
{
  svn_config_t *cfg;
  int i;
  const char *cfg_file;

  *msg = "test svn_config";

  if (msg_only)
    return SVN_NO_ERROR;

  if (!srcdir)
    SVN_ERR(init_params(pool));

  cfg_file = apr_pstrcat(pool, srcdir, "/", "config-test.cfg", NULL);
  SVN_ERR(svn_config_read(&cfg, cfg_file, TRUE, pool));

  /* Test values retrieved from our ConfigParser instance against
     values retrieved using svn_config. */
  for (i = 0; config_keys[i] != NULL; i++)
    {
      const char *key, *py_val, *c_val;
      
      key = config_keys[i];
      py_val = config_values[i];
      svn_config_get(cfg, &c_val, "section1", key, "default value");
#if 0
      printf("Testing expected value '%s' against '%s' for "
             "option '%s'\n", py_val, c_val, key);
#endif
      /* Fail iff one value is null, or the strings don't match. */
      if ((c_val == NULL) != (py_val == NULL)
          || (c_val != NULL && py_val != NULL && strcmp(c_val, py_val) != 0))
        return fail(pool, "Expected value '%s' not equal to '%s' for "
                    "option '%s'", py_val, c_val, key);
    }
  return SVN_NO_ERROR;
}


static const char *true_keys[] = {"true1", "true2", "true3", "true4",
                                  NULL};
static const char *false_keys[] = {"false1", "false2", "false3", "false4",
                                   NULL};

static svn_error_t *
test2(const char **msg, 
      svn_boolean_t msg_only,
      svn_test_opts_t *opts,
      apr_pool_t *pool)
{
  svn_config_t *cfg;
  int i;
  const char *cfg_file;

  *msg = "test svn_config boolean conversion";

  if (msg_only)
    return SVN_NO_ERROR;

  if (!srcdir)
    SVN_ERR(init_params(pool));

  cfg_file = apr_pstrcat(pool, srcdir, "/", "config-test.cfg", NULL);
  SVN_ERR(svn_config_read(&cfg, cfg_file, TRUE, pool));

  for (i = 0; true_keys[i] != NULL; i++)
    {
      svn_boolean_t value;
      SVN_ERR(svn_config_get_bool(cfg, &value, "booleans", true_keys[i],
                                  FALSE));
      if (!value)
        return fail(pool, "Value of option '%s' is not true", true_keys[i]);
    }

  for (i = 0; false_keys[i] != NULL; i++)
    {
      svn_boolean_t value;
      SVN_ERR(svn_config_get_bool(cfg, &value, "booleans", false_keys[i],
                                  TRUE));
      if (value)
        return fail(pool, "Value of option '%s' is not true", false_keys[i]);
    }

  {
    svn_error_t *err;
    svn_boolean_t value;

    svn_error_clear((err = svn_config_get_bool(cfg, &value,
                                               "booleans", "bad_true",
                                               TRUE)));
    if (!err)
      return fail(pool, "No error on bad truth value");

    svn_error_clear((err = svn_config_get_bool(cfg, &value,
                                               "booleans", "bad_false",
                                               FALSE)));
    if (!err)
      return fail(pool, "No error on bad truth value");
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
has_section_test(const char **msg, 
                 svn_boolean_t msg_only,
                 svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *cfg_file;

  *msg = "test svn_config_has_section";

  if (msg_only)
    return SVN_NO_ERROR;

  if (!srcdir)
    SVN_ERR(init_params(pool));

  cfg_file = apr_pstrcat(pool, srcdir, "/", "config-test.cfg", NULL);
  SVN_ERR(svn_config_read(&cfg, cfg_file, TRUE, pool));

  if (! svn_config_has_section(cfg, "section1"))
    return fail(pool, "Failed to find section1");

  if (svn_config_has_section(cfg, "notthere"))
    return fail(pool, "Returned true on missing section");

  return SVN_NO_ERROR;
}

/*
   ====================================================================
   If you add a new test to this file, update this array.

   (These globals are required by our included main())
*/

/* An array of all test functions */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test1),
    SVN_TEST_PASS(test2),
    SVN_TEST_PASS(has_section_test),
    SVN_TEST_NULL
  };
