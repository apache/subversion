/*
 * Regression tests for logic in the libsvn_client library.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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


#include "svn_mergeinfo.h"
#include "../../libsvn_client/mergeinfo.h"
#include "svn_pools.h"

#include "../svn_test.h"

typedef struct {
  const char *path;
  const char *unparsed_mergeinfo;
  svn_boolean_t remains;
} mergeinfo_catalog_item;

#define MAX_ITEMS 10

static mergeinfo_catalog_item elide_testcases[][MAX_ITEMS] = {
  { {"/foo", "/bar: 1-4", TRUE},
    {"/foo/beep/baz", "/bar/beep/baz: 1-4", FALSE},
    { NULL }},
  { {"/foo", "/bar: 1-4", TRUE},
    {"/foo/beep/baz", "/blaa/beep/baz: 1-4", TRUE},
    { NULL }},
  { {"/", "/gah: 1-4", TRUE},
    {"/foo/beep/baz", "/gah/foo/beep/baz: 1-4", FALSE},
    { NULL }}
};

static svn_error_t *
test_elide_mergeinfo_catalog(const char **msg,
                             svn_boolean_t msg_only,
                             svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool;

  *msg = "test svn_client__elide_mergeinfo_catalog";
  if (msg_only)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(pool);

  for (i = 0; 
       i < sizeof(elide_testcases) / sizeof(elide_testcases[0]);
       i++)
    {
      apr_hash_t *catalog;
      mergeinfo_catalog_item *item;

      svn_pool_clear(iterpool);

      catalog = apr_hash_make(iterpool);
      for (item = elide_testcases[i]; item->path; item++)
        {
          apr_hash_t *mergeinfo;

          SVN_ERR(svn_mergeinfo_parse(&mergeinfo, item->unparsed_mergeinfo, 
                                      iterpool));
          apr_hash_set(catalog, item->path, APR_HASH_KEY_STRING, mergeinfo);
        }
      
      SVN_ERR(svn_client__elide_mergeinfo_catalog(catalog, iterpool));

      for (item = elide_testcases[i]; item->path; item++)
        {
          apr_hash_t *mergeinfo = apr_hash_get(catalog, item->path, 
                                               APR_HASH_KEY_STRING);
          if (item->remains && !mergeinfo)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Elision for test case #%d incorrectly "
                                     "elided '%s'", i, item->path);
          if (!item->remains && mergeinfo)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "Elision for test case #%d failed to "
                                     "elide '%s'", i, item->path);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;  
}

/* ========================================================================== */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_elide_mergeinfo_catalog),
    SVN_TEST_NULL
  };
