/*
 * config-test.c:  tests svn_config
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_config.h"
#include "svn_test.h"


/* A quick way to create error messages.  */
static svn_error_t *
fail (apr_pool_t *pool, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_error_create (SVN_ERR_TEST_FAILED, 0, msg);
}


static const char *config_keys[] = { "a", "b", NULL };
static const char *config_values[] = { "Aa", "100", NULL };


static svn_error_t *
test1 (const char **msg, 
       svn_boolean_t msg_only,
       apr_pool_t *pool)
{
  svn_error_t *err;
  svn_config_t *cfg;
  int i;
  char *key, *py_val, *c_val;

  *msg = "test svn_config";

  if (msg_only)
    return SVN_NO_ERROR;

  err = svn_config_read(&cfg, "config-test.cfg", TRUE, pool);
  if (err != SVN_NO_ERROR)
    return err;

  /* Test values retrieved from our ConfigParser instance against
     values retrieved using svn_config. */
  for (i = 0; (char *) config_keys[i] != NULL; i++)
    {
      key = (char *) config_keys[i];
      py_val = (char *) config_values[i];
      svn_config_get(cfg, (const char **) &c_val, "section1", key,
                     "default value");
      if (c_val != NULL && py_val != NULL && strcmp(c_val, py_val) != 0)
        return fail(pool, "Expected value '%s' not equal to '%s' for "
                    "option '%s'", py_val, c_val, key);
    }
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
    SVN_TEST_PASS (test1),
    SVN_TEST_NULL
  };
