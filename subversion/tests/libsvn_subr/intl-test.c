/*
 * intl-test.c:  tests svn_intl
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
#include "svn_pools.h"
#include "svn_intl.h"

#include "../svn_test.h"
#include "svn_private_config.h" /* for PACKAGE_NAME */


/* Initialize parameters for the tests. */
extern int test_argc;
extern const char **test_argv;

static const apr_getopt_option_t opt_def[] =
  {
    {"srcdir", 'S', 1, "the source directory for VPATH test runs"},
    {0, 0, 0, 0}
  };
static const char *srcdir = NULL;

static svn_error_t *init_params (apr_pool_t *pool)
{
  apr_getopt_t *opt;
  int optch;
  const char *opt_arg;
  apr_status_t status;

  apr_getopt_init (&opt, pool, test_argc, test_argv);
  while (!(status = apr_getopt_long (opt, opt_def, &optch, &opt_arg)))
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
fail (apr_pool_t *pool, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_error_create (SVN_ERR_TEST_FAILED, 0, msg);
}

typedef struct
{
  const char *key;
  const char *value;
  const char *locale;
} l10n_t;

static l10n_t l10n_list[] =
  {
    { "Skipping binary file: '%s'\n", "Omitiendo el archivo binario: '%s'\n",
      "es" },
    { "Error writing to '%s'", "Error escribiendo en '%s'", "es" },
    { NULL, 0 }
  };

static svn_error_t *
test1 (const char **msg, 
       svn_boolean_t msg_only,
       svn_test_opts_t *opts,
       apr_pool_t *pool)
{
  l10n_t *l10n;
  apr_status_t st;
  apr_pool_t *subpool;

  *msg = "test init, l10n, and shutdown of svn_intl";

  if (msg_only)
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);
  st = svn_intl_initialize(subpool);
  if (st != APR_SUCCESS)
    {
      return fail(pool, "svn_intl_initialize failed with status of '%d'", st);
    }

  /* Test values retrieved from our IntlParser instance against
     values retrieved using svn_intl. */
  for (l10n = l10n_list; l10n->key != NULL; l10n++)
    {
      /* ### Account for a not-yet-installed resource bundle by using
         ### srcdir instead of SVN_LOCALE_DIR to remove XFAIL. */

      /* ### Test that svn_intl_dgettext(PACKAGE_NAME, l10n->key)
         ### returns the key when in "en" locale, or lang not
         ### available. */

      const char *intl_value = svn_intl_dlgettext (PACKAGE_NAME, l10n->locale,
                                                   l10n->key);
      if ((l10n->value == NULL) != (intl_value == NULL)
          || (l10n->value != NULL && intl_value != NULL
              && apr_strnatcmp(l10n->value, intl_value) != 0))
        return fail(pool, "Expected value '%s' not equal to '%s' for "
                    "text '%s'", l10n->value, intl_value, l10n->key);
    }

  apr_pool_destroy(subpool);

  /* ### Test re-initialization after pool passed to
     ### svn_intl_initialize() is destroyed. */

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
    /* ### XFAIL is a work-around for not-yet-installed bundles. */
    SVN_TEST_XFAIL (test1),
    SVN_TEST_NULL
  };
