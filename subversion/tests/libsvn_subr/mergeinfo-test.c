/*
 * mergeinfo-test.c:  a collection of mergeinfo tests
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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



#include <stdio.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_io.h"
#include "svn_error.h"
#include "svn_mergeinfo.h"   /* This includes <apr_*.h> */

#include "../svn_test.h"


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


/* Some of our own global variables, for simplicity.  Yes,
   simplicity. */
apr_hash_t *info1, *info2, *info3;
const char *mergeinfo1 = "/trunk: 5,7-9,10,11,13,14";
const char *brokenmergeinfo1 = "/trunk: 5,7-9,10,11,13,14,";
const char *brokenmergeinfo2 = "/trunk 5,7-9,10,11,13,14";
const char *brokenmergeinfo3 = "/trunk:5 7--9 10 11 13 14";
const char *mergeinfo2 = "/trunk: 5,7-9,10,11,13,14,3\n/fred:8-10";
const char *mergeinfo3 = "/trunk: 1-4,6,3\n/fred:9-12";

static svn_error_t *
test1(const char **msg,
      svn_boolean_t msg_only,
      svn_test_opts_t *opts,
      apr_pool_t *pool)
{
  *msg = "parse single line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo1, &info1, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test2(const char **msg,
      svn_boolean_t msg_only,
      svn_test_opts_t *opts,
      apr_pool_t *pool)
{
  *msg = "parse broken single line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  if (svn_mergeinfo_parse(brokenmergeinfo1, &info1, pool) == SVN_NO_ERROR)
    return fail(pool, "Failed to detect error in brokenmergeinfo1");

  if (svn_mergeinfo_parse(brokenmergeinfo2, &info1, pool) == SVN_NO_ERROR)
    return fail(pool, "Failed to detect error in brokenmergeinfo2");

  if (svn_mergeinfo_parse(brokenmergeinfo3, &info1, pool) == SVN_NO_ERROR)
    return fail(pool, "Failed to detect error in brokenmergeinfo3");

  return SVN_NO_ERROR;
}

static svn_error_t *
test3(const char **msg,
      svn_boolean_t msg_only,
      svn_test_opts_t *opts,
      apr_pool_t *pool)
{
  *msg = "parse multi line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo2, &info1, pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
test4(const char **msg,
      svn_boolean_t msg_only,
      svn_test_opts_t *opts,
      apr_pool_t *pool)
{
  apr_array_header_t *result;
  svn_merge_range_t *resultrange;
  *msg = "merging of mergeinfo hashs";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo2, &info1, pool));
  SVN_ERR(svn_mergeinfo_parse(mergeinfo3, &info2, pool));

  SVN_ERR(svn_mergeinfo_merge(&info3, info1, info2, pool));

  if (apr_hash_count(info3) != 2)
    return fail(pool, "Wrong number of paths in merged mergeinfo");

  result = apr_hash_get(info3, "/fred", -1);
  if (!result)
    return fail(pool, "Missing path in merged mergeinfo");

  /* /fred should have one merged range, 8-12. */
  if (result->nelts != 1)
    return fail(pool, "Merging failed to combine ranges");

  resultrange = APR_ARRAY_IDX(result, 0, svn_merge_range_t *);
  
  if (resultrange->start != 8 || resultrange->end != 12)
    return fail(pool, "Range combining produced wrong result");

  result = apr_hash_get(info3, "/trunk", -1);
  if (!result)
    return fail(pool, "Missing path in merged mergeinfo");

  /* /trunk should have two merged ranges, 1-11, and 13-14. */

  if (result->nelts != 2)
    return fail(pool, "Merging failed to combine ranges");

  resultrange = APR_ARRAY_IDX(result, 0, svn_merge_range_t *);
  
  if (resultrange->start != 1 || resultrange->end != 11)
    return fail(pool, "Range combining produced wrong result");
  
  resultrange = APR_ARRAY_IDX(result, 1, svn_merge_range_t *);
  
  if (resultrange->start != 13 || resultrange->end != 14)
    return fail(pool, "Range combining produced wrong result");

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
    SVN_TEST_PASS(test3),
    SVN_TEST_PASS(test4),
    SVN_TEST_NULL
  };
