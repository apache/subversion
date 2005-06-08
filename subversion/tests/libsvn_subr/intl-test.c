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
#ifdef PO_BUNDLES_FROM_SRC_DIR_USABLE  /* Sadly, they don't seem to be. */
#include <apr_file_info.h>
#endif
#include <apr_pools.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_intl.h"

#include "../svn_test.h"
#include "svn_private_config.h" /* for PACKAGE_NAME */


#define DEBUG 1

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
#ifdef PO_BUNDLES_FROM_SRC_DIR_USABLE
  char *gettext_path;
#endif

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

#ifdef PO_BUNDLES_FROM_SRC_DIR_USABLE
  if (!srcdir)
    return svn_error_create(SVN_ERR_TEST_FAILED, 0,
                            "missing required parameter '--srcdir'");

  /* Setup paths to our localization bundles from the source dir.
     Ideally, we'd point this to the subversion/po/ dir, but
     bindtextdomain expects a very specific directory structure. */
  apr_filepath_merge(&gettext_path, install_dir, "share", 0, pool);
  apr_filepath_merge(&gettext_path, gettext_path, "locale", 0, pool);
  printf("Path used by gettext is '%s'\n", gettext_path);
  if (bindtextdomain(PACKAGE_NAME, gettext_path) == NULL)
    {
      /* ### Handle error as in libsvn_subr/cmdline.c */
    }
#endif

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
    { "Could not save file", "No se pudo grabar el archivo", "es" },
    { "Error writing to '%s'", "Error escribiendo en '%s'", "es" },
    { NULL, 0 }
  };

static svn_error_t *
test1 (const char **msg, 
       svn_boolean_t msg_only,
       svn_test_opts_t *opts,
       apr_pool_t *pool)
{
  apr_status_t st;
  char **locale_prefs;

  *msg = "test locale preference retrieval of svn_intl";

  if (msg_only)
    return SVN_NO_ERROR;

  if (!srcdir)
    SVN_ERR(init_params(pool));

  /* ### Does this really belong here?  We need to assure that
     ### bindtextdomain() is called.  TODO: Check return code. */
  svn_cmdline_init(*msg, stderr);

  st = svn_intl_initialize(pool);
  if (st != APR_SUCCESS)
    {
      return fail(pool, "svn_intl_initialize failed with status of '%d'", st);
    }

  locale_prefs = svn_intl_get_locale_prefs(NULL, pool);
  if (locale_prefs == NULL)
    {
      /* This should never happen. */
      return fail(pool, "svn_intl_get_locale_prefs should never "
                  "return NULL, but did");
    }
  else if (*locale_prefs == NULL)
    {
      /* Locale not recorded in .po file. */
    }
#ifdef DEBUG
  else
    printf("System locale is '%s'\n", *locale_prefs);
#endif

  /* ### Set some contextual prefs and try again. */

  return SVN_NO_ERROR;
}


static svn_error_t *
test2 (const char **msg, 
       svn_boolean_t msg_only,
       svn_test_opts_t *opts,
       apr_pool_t *pool)
{
  l10n_t *l10n;
  apr_status_t st;
  apr_pool_t *subpool;

  *msg = "test l10n of svn_intl";

  if (msg_only)
    return SVN_NO_ERROR;

  if (!srcdir)
    SVN_ERR(init_params(pool));

  subpool = svn_pool_create(pool);
  st = svn_intl_initialize(subpool);
  if (st != APR_SUCCESS)
    {
      return fail(pool, "svn_intl_initialize failed with status of '%d'", st);
    }

  /* Test values retrieved from our intl module instance against
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

  return SVN_NO_ERROR;
}

/* ### Test re-initialization after sub-pool passed to
   ### svn_intl_initialize() is destroyed. */

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
    SVN_TEST_XFAIL (test2),
    SVN_TEST_NULL
  };
