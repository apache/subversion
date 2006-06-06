/*
 * mergeinfo-test.c -- test the merge info functions
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

#include <stdio.h>
#include <string.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_mergeinfo.h"
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

/* Verify that INPUT is parsed properly, and returns an error if
   parsing fails, or incorret parsing is detected.  Assumes that INPUT
   contains only one path -> ranges mapping, and that FIRST_RANGE is
   the first range in the set. */
static svn_error_t *
verify_mergeinfo_parse(const char *input,
                       const char *expected_path,
                       const svn_merge_range_t *first_range,
                       apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *path_to_merge_ranges;
  apr_hash_index_t *hi;

  /* Test valid input. */
  err = svn_mergeinfo_parse(input, &path_to_merge_ranges, pool);
  if (err || apr_hash_count(path_to_merge_ranges) != 1)
      return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                               "svn_mergeinfo_parse (%s) failed "
                               "unexpectedly", input);
  for (hi = apr_hash_first(pool, path_to_merge_ranges); hi;
       hi = apr_hash_next(hi))
    {
      const void *path;
      void *ranges;
      svn_merge_range_t *range;

      apr_hash_this(hi, &path, NULL, &ranges);
      if (strcmp((const char *) path, expected_path) != 0)
        return fail(pool, "svn_mergeinfo_parse (%s) failed to parse the "
                    "correct path (%s)", input, expected_path);

      /* Test ranges.  For now, assume only 1 range. */
      range = APR_ARRAY_IDX((apr_array_header_t *) ranges, 0,
                            svn_merge_range_t *);
      if (range->start != first_range->start ||
          range->end != first_range->end)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_mergeinfo_parse (%s) failed to "
                                 "parse the correct range",
                                 input);
    }
  return SVN_NO_ERROR;
}


/* Some of our own global variables (for simplicity), which map paths
   -> merge ranges. */
static apr_hash_t *info1, *info2, *info3;

#define NBR_MERGEINFO_VALS 3
/* Valid merge info values. */
static const char * const mergeinfo_vals[NBR_MERGEINFO_VALS] =
  {
    "/trunk:1",
    "/trunk/foo:1-6",
    "/trunk: 5,7-9,10,11,13,14"
  };
/* Paths corresponding to mergeinfo_vals. */
static const char * const mergeinfo_paths[NBR_MERGEINFO_VALS] =
  {
    "/trunk",
    "/trunk/foo",
    "/trunk"
  };
/* First ranges from the paths identified by mergeinfo_paths. */
static svn_merge_range_t mergeinfo_ranges[NBR_MERGEINFO_VALS] =
  {
    { 1, 1 },
    { 1, 6 },
    { 5, 5 }
  };

static svn_error_t *
test_parse_single_line_mergeinfo(const char **msg,
                                 svn_boolean_t msg_only,
                                 svn_test_opts_t *opts,
                                 apr_pool_t *pool)
{
  int i;
  
  *msg = "parse single line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < NBR_MERGEINFO_VALS; i++)
    verify_mergeinfo_parse(mergeinfo_vals[i], mergeinfo_paths[i],
                           &mergeinfo_ranges[i], pool);

  return SVN_NO_ERROR;
}

static const char *single_mergeinfo = "/trunk: 5,7-9,10,11,13,14";

static svn_error_t *
test_parse_combine_rangeinfo(const char **msg,
                             svn_boolean_t msg_only,
                             svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  apr_array_header_t *result;
  svn_merge_range_t *resultrange;
  
  *msg = "parse single line mergeinfo and combine ranges";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(single_mergeinfo, &info1, pool));

  if (apr_hash_count(info1) != 1)
    return fail(pool, "Wrong number of paths in parsed mergeinfo");

  result = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in parsed mergeinfo");

  /* /trunk should have three ranges, 5-5, 7-11, 13-14 */
  if (result->nelts != 3)
    return fail(pool, "Parsing failed to combine ranges");

  resultrange = APR_ARRAY_IDX(result, 0, svn_merge_range_t *);
  
  if (resultrange->start != 5 || resultrange->end != 5)
    return fail(pool, "Range combining produced wrong result");

  resultrange = APR_ARRAY_IDX(result, 1, svn_merge_range_t *);
  
  if (resultrange->start != 7 || resultrange->end != 11)
    return fail(pool, "Range combining produced wrong result");
  
  resultrange = APR_ARRAY_IDX(result, 2, svn_merge_range_t *);
  
  if (resultrange->start != 13 || resultrange->end != 14)
    return fail(pool, "Range combining produced wrong result");

  return SVN_NO_ERROR;
}


#define NBR_BROKEN_MERGEINFO_VALS 4
/* Invalid merge info values. */
static const char * const broken_mergeinfo_vals[NBR_BROKEN_MERGEINFO_VALS] =
  {
    "/missing-revs",
    "/trunk: 5,7-9,10,11,13,14,",
    "/trunk 5,7-9,10,11,13,14",
    "/trunk:5 7--9 10 11 13 14"
  };

static svn_error_t *
test_parse_broken_mergeinfo(const char **msg,
                            svn_boolean_t msg_only,
                            svn_test_opts_t *opts,
                            apr_pool_t *pool)
{
  int i;
  *msg = "parse broken single line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Trigger some error(s) with mal-formed input. */
  for (i = 0; i < NBR_BROKEN_MERGEINFO_VALS; i++)
    if (svn_mergeinfo_parse(broken_mergeinfo_vals[i], &info1, pool) ==
        SVN_NO_ERROR)
        return fail(pool, "svn_mergeinfo_parse (%s) failed to detect an error",
                    broken_mergeinfo_vals[i]);

  return SVN_NO_ERROR;
}


static const char *mergeinfo1 = "/trunk: 5,7-9,10,11,13,14,3\n/fred:8-10";
static const char *mergeinfo2 = "/trunk: 1-4,6,3\n/fred:9-12";

static svn_error_t *
test_parse_multi_line_mergeinfo(const char **msg,
                                svn_boolean_t msg_only,
                                svn_test_opts_t *opts,
                                apr_pool_t *pool)
{
  *msg = "parse multi line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo1, &info1, pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
test_diff_mergeinfo(const char **msg,
                    svn_boolean_t msg_only,
                    svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  int i;
  apr_array_header_t *rangelist;
  svn_merge_range_t *range;
  apr_hash_t *deleted, *added, *from, *to;
  svn_merge_range_t expected_deletions[2] = { {1, 2}, {4, 4} };
  svn_merge_range_t expected_additions[2] = { {5, 5}, {7, 8} };

  *msg = "diff of mergeinfo";
  if (msg_only)
    return SVN_NO_ERROR;

  from = apr_hash_make(pool);
  to = apr_hash_make(pool);
  /* On /trunk: deleted (1, 2, 4) and added (5, 7, 8) */
  apr_hash_set(from, "/trunk", APR_HASH_KEY_STRING, "1-4");
  apr_hash_set(to, "/trunk", APR_HASH_KEY_STRING, "3,5,7-8");

  SVN_ERR(svn_mergeinfo_diff(&deleted, &added, from, to, pool));

  if (apr_hash_count(deleted) != 1 || apr_hash_count(added) != 1)
    return fail(pool, "svn_mergeinfo_diff failed to calculate the "
                "correct number of path deltas");

  /* Verify calculation of deletion deltas. */
  rangelist = apr_hash_get(deleted, "/trunk", APR_HASH_KEY_STRING);
  if (rangelist->nelts != 2)
    return fail(pool, "svn_mergeinfo_diff failed to calculate the "
                "correct number of revision deletions");
  for (i = 0; i < rangelist->nelts - 1; i++)
    {
      range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      if (range->start != expected_deletions[i].start ||
          range->end != expected_deletions[i].end)
        return fail(pool, "svn_mergeinfo_diff failed to calculate the "
                    "correct merge range");
    }

  /* Verify calculation of addition deltas. */
  rangelist = apr_hash_get(added, "/trunk", APR_HASH_KEY_STRING);
  if (rangelist->nelts != 2)
    return fail(pool, "svn_mergeinfo_diff failed to calculate the "
                "correct number of revision additions");
  for (i = 0; i < rangelist->nelts - 1; i++)
    {
      range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      if (range->start != expected_additions[i].start ||
          range->end != expected_additions[i].end)
        return fail(pool, "svn_mergeinfo_diff failed to calculate the "
                    "correct merge range");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_merge_mergeinfo(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  apr_array_header_t *result;
  svn_merge_range_t *resultrange;
  *msg = "merging of mergeinfo hashs";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo1, &info1, pool));
  SVN_ERR(svn_mergeinfo_parse(mergeinfo2, &info2, pool));

  SVN_ERR(svn_mergeinfo_merge(&info3, info1, info2, pool));

  if (apr_hash_count(info3) != 2)
    return fail(pool, "Wrong number of paths in merged mergeinfo");

  result = apr_hash_get(info3, "/fred", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in merged mergeinfo");

  /* /fred should have one merged range, 8-12. */
  if (result->nelts != 1)
    return fail(pool, "Merging failed to combine ranges");

  resultrange = APR_ARRAY_IDX(result, 0, svn_merge_range_t *);
  
  if (resultrange->start != 8 || resultrange->end != 12)
    return fail(pool, "Range combining produced wrong result");

  result = apr_hash_get(info3, "/trunk", APR_HASH_KEY_STRING);
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

static svn_error_t *
test_remove_mergeinfo(const char **msg,
                      svn_boolean_t msg_only,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  *msg = "remove of mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  /* ### TODO: Implement me! */

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_to_string(const char **msg,
                         svn_boolean_t msg_only,
                         svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  apr_array_header_t *result;
  svn_stringbuf_t *output;
  svn_stringbuf_t *expected = svn_stringbuf_create("3,5,7-11,13-14", pool);

  *msg = "turning rangelist back into a string";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo1, &info1, pool));
  
  result = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in parsed mergeinfo");
  
  SVN_ERR(svn_rangelist_to_string(&output, result, pool));

  if (svn_stringbuf_compare(expected, output) != TRUE)
    fail(pool, "Rangelist string not what we expected");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_mergeinfo_to_string(const char **msg,
                         svn_boolean_t msg_only,
                         svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *output;
  svn_stringbuf_t *expected;
  expected = svn_stringbuf_create("/fred:8-11\n/trunk:3,5,7-11,13-14", pool);

  *msg = "turning mergeinfo back into a string";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo1, &info1, pool));
  
  SVN_ERR(svn_mergeinfo_to_string(&output, info1, pool));

  if (svn_stringbuf_compare(expected, output) != TRUE)
    fail(pool, "Mergeinfo string not what we expected");

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_parse_single_line_mergeinfo),
    SVN_TEST_PASS(test_parse_combine_rangeinfo),
    SVN_TEST_PASS(test_parse_broken_mergeinfo),
    SVN_TEST_PASS(test_parse_multi_line_mergeinfo),
    SVN_TEST_PASS(test_diff_mergeinfo),
    SVN_TEST_PASS(test_merge_mergeinfo),
    SVN_TEST_PASS(test_remove_mergeinfo),
    SVN_TEST_PASS(test_rangelist_to_string),
    SVN_TEST_PASS(test_mergeinfo_to_string),
    SVN_TEST_NULL
  };
