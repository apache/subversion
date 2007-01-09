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
                             "svn_mergeinfo_parse (%s) failed unexpectedly",
                             input);
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
    SVN_ERR(verify_mergeinfo_parse(mergeinfo_vals[i], mergeinfo_paths[i],
                                   &mergeinfo_ranges[i], pool));

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


#define NBR_BROKEN_MERGEINFO_VALS 5
/* Invalid merge info values. */
static const char * const broken_mergeinfo_vals[NBR_BROKEN_MERGEINFO_VALS] =
  {
    "/missing-revs",
    "/missing-revs-with-colon: ",
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
  svn_error_t *err;
  *msg = "parse broken single line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Trigger some error(s) with mal-formed input. */
  for (i = 0; i < NBR_BROKEN_MERGEINFO_VALS; i++)
    {
      err = svn_mergeinfo_parse(broken_mergeinfo_vals[i], &info1, pool);
      if (err == SVN_NO_ERROR)
        return fail(pool, "svn_mergeinfo_parse (%s) failed to detect an error",
                    broken_mergeinfo_vals[i]);
      else
        svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}


static const char *mergeinfo1 = "/trunk: 5,7-9,10,11,13,14,3\n/fred:8-10";
static const char *mergeinfo2 = "/trunk: 1-4,6,3\n/fred:9-12";
static const char *mergeinfo3 = "/trunk: 3-7, 13\n/fred:9";
static const char *mergeinfo4 = "/trunk: 5-8, 13\n/fred:9";
static const char *mergeinfo5 = "/trunk: 15-25, 35-45, 55-65";
static const char *mergeinfo6 = "/trunk: 15-25, 35-45";
static const char *mergeinfo7 = "/trunk: 10-30, 35-45, 55-65";
static const char *mergeinfo8 = "/trunk: 15-25";

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


#define NBR_RANGELIST_DELTAS 4

/* Verify that ACTUAL_RANGELIST matches EXPECTED_RANGES (an array of
   NBR_EXPECTED length).  Return an error based careful examination if
   they do not match.  FUNC_VERIFIED is the name of the API being
   verified (e.g. "svn_rangelist_intersect"), while TYPE is a word
   describing what the ranges being examined represent. */
static svn_error_t *
verify_ranges_match(apr_array_header_t *actual_rangelist,
                    svn_merge_range_t *expected_ranges, int nbr_expected,
                    const char *func_verified, const char *type,
                    apr_pool_t *pool)
{
  int i;

  if (actual_rangelist->nelts != nbr_expected)
    return fail(pool, "%s should report %d range %ss, but found %d",
                func_verified, nbr_expected, type, actual_rangelist->nelts);

  for (i = 0; i < actual_rangelist->nelts; i++)
    {
      svn_merge_range_t *range = APR_ARRAY_IDX(actual_rangelist, i,
                                               svn_merge_range_t *);
      if (range->start != expected_ranges[i].start ||
          range->end != expected_ranges[i].end)
        return fail(pool, "%s should report range %ld-%ld, but found %ld-%ld",
                    func_verified, expected_ranges[i].start,
                    expected_ranges[i].end, range->start, range->end);
    }
  return SVN_NO_ERROR;
}

/* Verify that DELTAS matches EXPECTED_DELTAS (both expected to
   contain only a rangelist for "/trunk").  Return an error based
   careful examination if they do not match.  FUNC_VERIFIED is the
   name of the API being verified (e.g. "svn_mergeinfo_diff"), while
   TYPE is a word describing what the deltas being examined
   represent. */
static svn_error_t *
verify_mergeinfo_deltas(apr_hash_t *deltas, svn_merge_range_t *expected_deltas,
                        const char *func_verified, const char *type,
                        apr_pool_t *pool)
{
  apr_array_header_t *rangelist;

  if (apr_hash_count(deltas) != 1)
    /* Deltas on "/trunk" expected. */
    return fail(pool, "%s should report 1 path %s, but found %d",
                func_verified, type, apr_hash_count(deltas));

  rangelist = apr_hash_get(deltas, "/trunk", APR_HASH_KEY_STRING);
  if (rangelist == NULL)
    return fail(pool, "%s failed to produce a rangelist for /trunk",
                func_verified);

  return verify_ranges_match(rangelist, expected_deltas, NBR_RANGELIST_DELTAS,
                             func_verified, type, pool);
}

static svn_error_t *
test_diff_mergeinfo(const char **msg,
                    svn_boolean_t msg_only,
                    svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  apr_hash_t *deleted, *added, *from, *to;
  svn_merge_range_t expected_rangelist_deletions[NBR_RANGELIST_DELTAS] =
    { {7, 7}, {9, 9}, {11, 11}, {33, 34} };
  svn_merge_range_t expected_rangelist_additions[NBR_RANGELIST_DELTAS] =
    { {2, 2}, {5, 6}, {13, 16}, {30, 30} };

  *msg = "diff of mergeinfo";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse("/trunk: 1,3-4,7,9,11-12,31-34", &from, pool));
  SVN_ERR(svn_mergeinfo_parse("/trunk: 1-6,12-16,30-32", &to, pool));
  /* On /trunk: deleted (7, 9, 11, 33-34) and added (2, 5-6, 13-16, 30) */
  SVN_ERR(svn_mergeinfo_diff(&deleted, &added, from, to, pool));

  /* Verify calculation of range list deltas. */
  SVN_ERR(verify_mergeinfo_deltas(deleted, expected_rangelist_deletions,
                                  "svn_mergeinfo_diff", "deletion", pool));
  SVN_ERR(verify_mergeinfo_deltas(added, expected_rangelist_additions,
                                  "svn_mergeinfo_diff", "addition", pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_reverse(const char **msg,
                       svn_boolean_t msg_only,
                       svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  apr_array_header_t *rangelist;
  svn_merge_range_t expected_rangelist[3] = { {10, 10}, {7, 5}, {3, 3} };

  *msg = "reversal of rangelist";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse("/trunk: 3,5-7,10", &info1, pool));
  rangelist = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);

  SVN_ERR(svn_rangelist_reverse(rangelist, pool));

  return verify_ranges_match(rangelist, expected_rangelist, 3,
                             "svn_rangelist_reverse", "reversal", pool);
}

static svn_error_t *
test_rangelist_intersect(const char **msg,
                         svn_boolean_t msg_only,
                         svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  apr_array_header_t *rangelist1, *rangelist2, *intersection;
  svn_merge_range_t expected_intersection[4] =
    { {1, 1}, {3, 4}, {12, 12}, {31, 32} };

  *msg = "intersection of rangelists";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse("/trunk: 1-6,12-16,30-32", &info1, pool));
  SVN_ERR(svn_mergeinfo_parse("/trunk: 1,3-4,7,9,11-12,31-34", &info2, pool));
  rangelist1 = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  rangelist2 = apr_hash_get(info2, "/trunk", APR_HASH_KEY_STRING);
  
  SVN_ERR(svn_rangelist_intersect(&intersection, rangelist1, rangelist2,
                                  pool));

  return verify_ranges_match(intersection, expected_intersection, 4,
                             "svn_rangelist_intersect", "intersect", pool);
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
test_remove_rangelist(const char **msg,
                      svn_boolean_t msg_only,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  apr_array_header_t *whiteboard;
  apr_array_header_t *eraser;
  apr_array_header_t *result;
  svn_stringbuf_t *outputstring;
  svn_stringbuf_t *expected1 = svn_stringbuf_create("55-65", pool);
  svn_stringbuf_t *expected2 = svn_stringbuf_create("10-14,26-30,55-65",
                                                    pool);
  svn_stringbuf_t *expected3 = svn_stringbuf_create("10-14,26-30,35-45,55-65",
                                                    pool);
  *msg = "remove of rangelist";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo5, &info1, pool));
  
  whiteboard = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!whiteboard)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_mergeinfo_parse(mergeinfo6, &info2, pool));
  
  eraser = apr_hash_get(info2, "/trunk", APR_HASH_KEY_STRING);
  if (!eraser)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_remove(&result, eraser, whiteboard, pool));
  
  SVN_ERR(svn_rangelist_to_string(&outputstring, result, pool));

  if (svn_stringbuf_compare(expected1, outputstring) != TRUE)
    return fail(pool, "Rangelist string not what we expected");

  SVN_ERR(svn_mergeinfo_parse(mergeinfo7, &info1, pool));
  
  whiteboard = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!whiteboard)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_remove(&result, eraser, whiteboard, pool));
  
  SVN_ERR(svn_rangelist_to_string(&outputstring, result, pool));

  if (svn_stringbuf_compare(expected2, outputstring) != TRUE)
    return fail(pool, "Rangelist string not what we expected");
  
  SVN_ERR(svn_mergeinfo_parse(mergeinfo8, &info1, pool));
  
  eraser = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!eraser)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_remove(&result, eraser, whiteboard, pool));
  
  SVN_ERR(svn_rangelist_to_string(&outputstring, result, pool));

  if (svn_stringbuf_compare(expected3, outputstring) != TRUE)
    return fail(pool, "Rangelist string not what we expected");

  return SVN_NO_ERROR;
}

/* ### Share code with test_diff_mergeinfo() and test_remove_rangelist(). */
static svn_error_t *
test_remove_mergeinfo(const char **msg,
                      svn_boolean_t msg_only,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  apr_hash_t *output, *whiteboard, *eraser;
  svn_merge_range_t expected_rangelist_remainder[NBR_RANGELIST_DELTAS] =
    { {7, 7}, {9, 9}, {11, 11}, {33, 34} };

  *msg = "remove of mergeinfo";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse("/trunk: 1,3-4,7,9,11-12,31-34", &whiteboard,
                              pool));
  SVN_ERR(svn_mergeinfo_parse("/trunk: 1-6,12-16,30-32", &eraser, pool));

  /* Leftover on /trunk should be the set (7, 9, 11, 33-34) */
  SVN_ERR(svn_mergeinfo_remove(&output, eraser, whiteboard, pool));

  /* Verify calculation of range list remainder. */
  return verify_mergeinfo_deltas(output, expected_rangelist_remainder,
                                 "svn_mergeinfo_remove", "leftover", pool);
}
#undef NBR_RANGELIST_DELTAS

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
    return fail(pool, "Rangelist string not what we expected");

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
  expected = svn_stringbuf_create("/fred:8-10\n/trunk:3,5,7-11,13-14", pool);

  *msg = "turning mergeinfo back into a string";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(mergeinfo1, &info1, pool));
  
  SVN_ERR(svn_mergeinfo_to_string(&output, info1, pool));

  if (svn_stringbuf_compare(expected, output) != TRUE)
    return fail(pool, "Mergeinfo string not what we expected");

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
    SVN_TEST_PASS(test_remove_rangelist),
    SVN_TEST_PASS(test_remove_mergeinfo),
    SVN_TEST_PASS(test_rangelist_reverse),
    SVN_TEST_PASS(test_rangelist_intersect),
    SVN_TEST_PASS(test_diff_mergeinfo),
    SVN_TEST_PASS(test_merge_mergeinfo),
    SVN_TEST_PASS(test_rangelist_to_string),
    SVN_TEST_PASS(test_mergeinfo_to_string),
    SVN_TEST_NULL
  };
