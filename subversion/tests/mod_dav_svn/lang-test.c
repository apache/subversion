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
#include <httpd.h>  /* for server_rec, request_rec, etc. */
#include <http_config.h>  /* for ap_create_request_config */

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

/* The HTTP Accept-Language header value. */
static const char *HTTP_HEADER = "es-ES; q=0.2, en-US, en; q=0.9";

/* The ordered list of locales represented by the header. */
static const char *LOCALE_PREFS[] = { "en_US", "en", "es_ES", NULL };


static svn_error_t *
test1 (const char **msg, 
       svn_boolean_t msg_only,
       svn_test_opts_t *opts,
       apr_pool_t *pool)
{
  svn_error_t *err;
  const char **prefs;
  int i;
  server_rec *server;
  request_rec *req;

  *msg = "test mod_dav_svn's lang module";

  if (msg_only)
    return SVN_NO_ERROR;

  err = svn_intl_initialize(pool);
  if (err)
    {
      return svn_error_create (SVN_ERR_TEST_FAILED, err,
                               "svn_intl_initialize failed");
    }

  /* Setup a mock server_rec. */
  server = apr_pcalloc(pool, sizeof(server_rec));
  server->defn_name = "test";
  server->server_admin = (char *) "dev@subversion.tigris.org";
  server->server_hostname = (char *) "localhost";
  server->port = 80;
  server->is_virtual = 1;

  /* Setup a mock request_rec. */
  req = apr_pcalloc(pool, sizeof(request_rec));
  req->pool = pool;
  req->hostname = server->server_hostname;
  req->request_time = 0;
  /*r->connection = connection;*/
  req->server = server;
  /*req->request_config = ap_create_request_config(req->pool);*/
  req->method = "GET";
  req->method_number = M_GET;
  /* ### Use HTTP_HEADER to set headers_in, an apr_table_t
  req->headers_in = ...;
  */

  /*SVN_ERR(svn_dav__negotiate_lang_prefs(req));*/
  prefs = svn_intl_get_locale_prefs(req->pool);

  for (i = 0; LOCALE_PREFS[i] != NULL; i++)
    {
      if ((LOCALE_PREFS[i] == NULL) != (prefs[i] == NULL)
          || (LOCALE_PREFS[i] != NULL && prefs[i] != NULL
              && apr_strnatcmp(LOCALE_PREFS[i], prefs[i]) != 0))
        {
          return fail(pool, "svn_dav__negotiate_lang_prefs: %s "
                      "didn't match %s", LOCALE_PREFS[i], prefs[i]);
        }
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
    SVN_TEST_XFAIL (test1),
    SVN_TEST_NULL
  };
