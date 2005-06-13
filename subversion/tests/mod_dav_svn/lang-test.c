/*
 * lang-test.c:  tests Accept-Language parsing module
 *
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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
#include <httpd.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "../../mod_dav_svn/dav_svn.h"

#include "../svn_test.h"


/* Initialize parameters for the tests. */
extern int test_argc;
extern const char **test_argv;

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
  /* The HTTP header value. */
  const char *header;

  /* The ordered list of locales represented by the header. */
  const char **locale_prefs;
} lang_pref_t;

/* ### Get sample data from RFC 2616. */
static lang_pref_t lang_prefs[] =
  {
    { "es-ES; q=0.2, en-US, en; q=0.9", {"en_US", "en", "es_ES"} },
    { NULL, NULL }
  };

static svn_error_t *
test1 (const char **msg, 
       svn_boolean_t msg_only,
       svn_test_opts_t *opts,
       apr_pool_t *pool)
{
  svn_error_t err;
  request_rec *r;

  *msg = "test mod_dav_svn's lang module";

  if (msg_only)
    return SVN_NO_ERROR;

  /* ### Setup request_rec */

  /*SVN_ERR(svn_dav__negotiate_lang_prefs(r));*/

  if (0)
    {
      return fail(pool, "svn_dav__negotiate_lang_prefs: x didn't match y");
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
