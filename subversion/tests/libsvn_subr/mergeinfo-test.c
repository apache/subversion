/*
 * mergeinfo-test.c -- test the mergeinfo functions
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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

#include "svn_pools.h"
#include "svn_types.h"
#include "svn_mergeinfo.h"
#include "private/svn_mergeinfo_private.h"
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
  err = svn_mergeinfo_parse(&path_to_merge_ranges, input, pool);
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
      if (range->start != first_range->start
          || range->end != first_range->end
          || range->inheritable != first_range->inheritable)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_mergeinfo_parse (%s) failed to "
                                 "parse the correct range",
                                 input);
    }
  return SVN_NO_ERROR;
}


/* Some of our own global variables (for simplicity), which map paths
   -> merge ranges. */
static apr_hash_t *info1, *info2;

#define NBR_MERGEINFO_VALS 3
/* Valid mergeinfo values. */
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
    { 0, 1, TRUE },
    { 0, 6, TRUE },
    { 4, 5, TRUE }
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
test_mergeinfo_dup(const char **msg,
                   svn_boolean_t msg_only,
                   svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  apr_hash_t *orig_mergeinfo, *copied_mergeinfo;
  apr_pool_t *subpool;
  apr_array_header_t *rangelist;

  *msg = "copy a mergeinfo data structure";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Assure that copies which should be empty turn out that way. */
  subpool = svn_pool_create(pool);
  orig_mergeinfo = apr_hash_make(subpool);
  copied_mergeinfo = svn_mergeinfo_dup(orig_mergeinfo, subpool);
  if (apr_hash_count(copied_mergeinfo) != 0)
    return fail(pool, "Copied mergeinfo should be empty");

  /* Create some mergeinfo, copy it using another pool, then destroy
     the pool with which the original mergeinfo was created. */
  SVN_ERR(svn_mergeinfo_parse(&orig_mergeinfo, single_mergeinfo, subpool));
  copied_mergeinfo = svn_mergeinfo_dup(orig_mergeinfo, pool);
  apr_pool_destroy(subpool);
  if (apr_hash_count(copied_mergeinfo) != 1)
    return fail(pool, "Copied mergeinfo should contain one merge source");
  rangelist = apr_hash_get(copied_mergeinfo, "/trunk", APR_HASH_KEY_STRING);
  if (! rangelist)
    return fail(pool, "Expected copied mergeinfo; got nothing");
  if (rangelist->nelts != 3)
    return fail(pool, "Copied mergeinfo should contain 3 revision ranges, "
                "rather than the %d it contains", rangelist->nelts);

  return SVN_NO_ERROR;
}

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

  SVN_ERR(svn_mergeinfo_parse(&info1, single_mergeinfo, pool));

  if (apr_hash_count(info1) != 1)
    return fail(pool, "Wrong number of paths in parsed mergeinfo");

  result = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in parsed mergeinfo");

  /* /trunk should have three ranges, 5-5, 7-11, 13-14 */
  if (result->nelts != 3)
    return fail(pool, "Parsing failed to combine ranges");

  resultrange = APR_ARRAY_IDX(result, 0, svn_merge_range_t *);

  if (resultrange->start != 4 || resultrange->end != 5)
    return fail(pool, "Range combining produced wrong result");

  resultrange = APR_ARRAY_IDX(result, 1, svn_merge_range_t *);

  if (resultrange->start != 6 || resultrange->end != 11)
    return fail(pool, "Range combining produced wrong result");

  resultrange = APR_ARRAY_IDX(result, 2, svn_merge_range_t *);

  if (resultrange->start != 12 || resultrange->end != 14)
    return fail(pool, "Range combining produced wrong result");

  return SVN_NO_ERROR;
}


#define NBR_BROKEN_MERGEINFO_VALS 4
/* Invalid mergeinfo values. */
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
  svn_error_t *err;
  *msg = "parse broken single line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  /* Trigger some error(s) with mal-formed input. */
  for (i = 0; i < NBR_BROKEN_MERGEINFO_VALS; i++)
    {
      err = svn_mergeinfo_parse(&info1, broken_mergeinfo_vals[i], pool);
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
static const char *mergeinfo3 = "/trunk: 15-25, 35-45, 55-65";
static const char *mergeinfo4 = "/trunk: 15-25, 35-45";
static const char *mergeinfo5 = "/trunk: 10-30, 35-45, 55-65";
static const char *mergeinfo6 = "/trunk: 15-25";
static const char *mergeinfo7 = "/empty-rangelist:\n/with-trailing-space: ";

static svn_error_t *
test_parse_multi_line_mergeinfo(const char **msg,
                                svn_boolean_t msg_only,
                                svn_test_opts_t *opts,
                                apr_pool_t *pool)
{
  *msg = "parse multi line mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo1, pool));

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo7, pool));

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
      if (range->start != expected_ranges[i].start
          || range->end != expected_ranges[i].end
          || range->inheritable != expected_ranges[i].inheritable)
          return fail(pool, "%s should report range %ld-%ld%s, "
                      "but found %ld-%ld%s",
                      func_verified, expected_ranges[i].start,
                      expected_ranges[i].end,
                      expected_ranges[i].inheritable ? "*" : "",
                      range->start, range->end,
                      range->inheritable ? "*" : "");
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
    { {6, 7, TRUE}, {8, 9, TRUE}, {10, 11, TRUE}, {32, 34, TRUE} };
  svn_merge_range_t expected_rangelist_additions[NBR_RANGELIST_DELTAS] =
    { {1, 2, TRUE}, {4, 6, TRUE}, {12, 16, TRUE}, {29, 30, TRUE} };

  *msg = "diff of mergeinfo";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&from, "/trunk: 1,3-4,7,9,11-12,31-34", pool));
  SVN_ERR(svn_mergeinfo_parse(&to, "/trunk: 1-6,12-16,30-32", pool));
  /* On /trunk: deleted (7, 9, 11, 33-34) and added (2, 5-6, 13-16, 30) */
  SVN_ERR(svn_mergeinfo_diff(&deleted, &added, from, to,
                             svn_rangelist_ignore_inheritance, pool));

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
  svn_merge_range_t expected_rangelist[3] =
    { {10, 9, TRUE}, {7, 4, TRUE}, {3, 2, TRUE} };

  *msg = "reversal of rangelist";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&info1, "/trunk: 3,5-7,10", pool));
  rangelist = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);

  SVN_ERR(svn_rangelist_reverse(rangelist, pool));

  return verify_ranges_match(rangelist, expected_rangelist, 3,
                             "svn_rangelist_reverse", "reversal", pool);
}

static svn_error_t *
test_rangelist_count_revs(const char **msg,
                          svn_boolean_t msg_only,
                          svn_test_opts_t *opts,
                          apr_pool_t *pool)
{
  apr_array_header_t *rangelist;
  apr_uint64_t nbr_revs;

  *msg = "counting revs in rangelist";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&info1, "/trunk: 3,5-7,10", pool));
  rangelist = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);

  nbr_revs = svn_rangelist_count_revs(rangelist);

  if (nbr_revs != 5)
    return fail(pool, "expecting 5 revs in count, found %d", nbr_revs);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_to_revs(const char **msg,
                       svn_boolean_t msg_only,
                       svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  apr_array_header_t *revs, *rangelist;
  svn_revnum_t expected_revs[] = {3, 5, 6, 7, 10};
  int i;

  *msg = "returning revs in rangelist";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&info1, "/trunk: 3,5-7,10", pool));
  rangelist = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);

  SVN_ERR(svn_rangelist_to_revs(&revs, rangelist, pool));

  for (i = 0; i < revs->nelts; i++)
    {
      svn_revnum_t rev = APR_ARRAY_IDX(revs, i, svn_revnum_t);

      if (rev != expected_revs[i])
        return fail(pool, "rev mis-match at position %d: expecting %d, "
                    "found %d", i, expected_revs[i], rev);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_intersect(const char **msg,
                         svn_boolean_t msg_only,
                         svn_test_opts_t *opts,
                         apr_pool_t *pool)
{
  apr_array_header_t *rangelist1, *rangelist2, *intersection;
  svn_merge_range_t expected_intersection[4] =
    { {0, 1, TRUE}, {2, 4, TRUE}, {11, 12, TRUE}, {30, 32, TRUE} };

  *msg = "intersection of rangelists";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&info1, "/trunk: 1-6,12-16,30-32", pool));
  SVN_ERR(svn_mergeinfo_parse(&info2, "/trunk: 1,3-4,7,9,11-12,31-34", pool));
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

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo1, pool));
  SVN_ERR(svn_mergeinfo_parse(&info2, mergeinfo2, pool));

  SVN_ERR(svn_mergeinfo_merge(&info1, info2, svn_rangelist_ignore_inheritance,
                              pool));

  if (apr_hash_count(info1) != 2)
    return fail(pool, "Wrong number of paths in merged mergeinfo");

  result = apr_hash_get(info1, "/fred", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in merged mergeinfo");

  /* /fred should have one merged range, 8-12. */
  if (result->nelts != 1)
    return fail(pool, "Merging failed to combine ranges");

  resultrange = APR_ARRAY_IDX(result, 0, svn_merge_range_t *);

  if (resultrange->start != 7 || resultrange->end != 12)
    return fail(pool, "Range combining produced wrong result");

  result = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in merged mergeinfo");

  /* /trunk should have two merged ranges, 1-11, and 13-14. */

  if (result->nelts != 2)
    return fail(pool, "Merging failed to combine ranges");

  resultrange = APR_ARRAY_IDX(result, 0, svn_merge_range_t *);

  if (resultrange->start != 0 || resultrange->end != 11)
    return fail(pool, "Range combining produced wrong result");

  resultrange = APR_ARRAY_IDX(result, 1, svn_merge_range_t *);

  if (resultrange->start != 12 || resultrange->end != 14)
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

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo3, pool));

  whiteboard = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!whiteboard)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_mergeinfo_parse(&info2, mergeinfo4, pool));

  eraser = apr_hash_get(info2, "/trunk", APR_HASH_KEY_STRING);
  if (!eraser)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_remove(&result, eraser, whiteboard, TRUE, pool));

  SVN_ERR(svn_rangelist_to_stringbuf(&outputstring, result, pool));

  if (svn_stringbuf_compare(expected1, outputstring) != TRUE)
    return fail(pool, "Rangelist string not what we expected");

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo5, pool));

  whiteboard = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!whiteboard)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_remove(&result, eraser, whiteboard, TRUE, pool));

  SVN_ERR(svn_rangelist_to_stringbuf(&outputstring, result, pool));

  if (svn_stringbuf_compare(expected2, outputstring) != TRUE)
    return fail(pool, "Rangelist string not what we expected");

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo6, pool));

  eraser = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!eraser)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_remove(&result, eraser, whiteboard, TRUE, pool));

  SVN_ERR(svn_rangelist_to_stringbuf(&outputstring, result, pool));

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
    { {6, 7, TRUE}, {8, 9, TRUE}, {10, 11, TRUE}, {32, 34, TRUE} };

  *msg = "remove of mergeinfo";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&whiteboard,
                              "/trunk: 1,3-4,7,9,11-12,31-34", pool));
  SVN_ERR(svn_mergeinfo_parse(&eraser, "/trunk: 1-6,12-16,30-32", pool));

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

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo1, pool));

  result = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_to_stringbuf(&output, result, pool));

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
  svn_string_t *output;
  svn_string_t *expected;
  expected = svn_string_create("/fred:8-10\n/trunk:3,5,7-11,13-14", pool);

  *msg = "turning mergeinfo back into a string";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo1, pool));

  SVN_ERR(svn_mergeinfo__to_string(&output, info1, pool));

  if (svn_string_compare(expected, output) != TRUE)
    return fail(pool, "Mergeinfo string not what we expected");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_range_compact(const char **msg,
                   svn_boolean_t msg_only,
                   svn_test_opts_t *opts,
                   apr_pool_t *pool)
{
  #define SIZE_OF_TEST_ARRAY 44
  svn_merge_range_t rangelist[SIZE_OF_TEST_ARRAY][4] =
    /* For each ith element of rangelist[][], try to combine/compact
       rangelist[i][0] and rangelist[i][1].  If the combined ranges can
       be combined, then the expected range is rangelist[i][2] and
       rangelist[i][3] is {-1, -1, TRUE}.  If the ranges cancel each
       other out, then both rangelist[i][2] and rangelist[i][3] are
       {-1, -1, TRUE}.
           range1      +   range2      =   range3      ,   range4 */
    { /* Non-intersecting ranges */
      { { 2,  4, TRUE}, { 6, 13, TRUE}, { 2,  4, TRUE}, { 6, 13, TRUE} },
      { { 4,  2, TRUE}, { 6, 13, TRUE}, { 4,  2, TRUE}, { 6, 13, TRUE} },
      { { 4,  2, TRUE}, {13,  6, TRUE}, { 4,  2, TRUE}, {13,  6, TRUE} },
      { { 2,  4, TRUE}, {13,  6, TRUE}, { 2,  4, TRUE}, {13,  6, TRUE} },
      { { 6, 13, TRUE}, { 2,  4, TRUE}, { 6, 13, TRUE}, { 2,  4, TRUE} },
      { { 6, 13, TRUE}, { 4,  2, TRUE}, { 6, 13, TRUE}, { 4,  2, TRUE} },
      { {13,  6, TRUE}, { 4,  2, TRUE}, {13, 6,  TRUE}, { 4,  2, TRUE} },
      { {13,  6, TRUE}, { 2,  4, TRUE}, {13, 6,  TRUE}, { 2,  4, TRUE} },
      /* Intersecting ranges with no common start or end points */
      { { 2,  5, TRUE}, { 4,  6, TRUE}, { 2,  6, TRUE}, {-1, -1, TRUE} },
      { { 2,  5, TRUE}, { 6,  4, TRUE}, { 2,  4, TRUE}, { 6,  5, TRUE} },
      { { 5,  2, TRUE}, { 4,  6, TRUE}, { 4,  2, TRUE}, { 5,  6, TRUE} },
      { { 5,  2, TRUE}, { 6,  4, TRUE}, { 6,  2, TRUE}, {-1, -1, TRUE} },
      { { 4,  6, TRUE}, { 2,  5, TRUE}, { 2,  6, TRUE}, {-1, -1, TRUE} },
      { { 6,  4, TRUE}, { 2,  5, TRUE}, { 6,  5, TRUE}, { 2,  4, TRUE} },
      { { 4,  6, TRUE}, { 5,  2, TRUE}, { 5,  6, TRUE}, { 4,  2, TRUE} },
      { { 6,  4, TRUE}, { 5,  2, TRUE}, { 6,  2, TRUE}, {-1, -1, TRUE} },
      /* One range is a proper subset of the other. */
      { {33, 43, TRUE}, {37, 38, TRUE}, {33, 43, TRUE}, {-1, -1, TRUE} },
      { {33, 43, TRUE}, {38, 37, TRUE}, {33, 37, TRUE}, {38, 43, TRUE} },
      { {43, 33, TRUE}, {37, 38, TRUE}, {37, 33, TRUE}, {43, 38, TRUE} },
      { {43, 33, TRUE}, {38, 37, TRUE}, {43, 33, TRUE}, {-1, -1, TRUE} },
      { {37, 38, TRUE}, {33, 43, TRUE}, {33, 43, TRUE}, {-1, -1, TRUE} },
      { {38, 37, TRUE}, {33, 43, TRUE}, {33, 37, TRUE}, {38, 43, TRUE} },
      { {37, 38, TRUE}, {43, 33, TRUE}, {37, 33, TRUE}, {43, 38, TRUE} },
      { {38, 37, TRUE}, {43, 33, TRUE}, {43, 33, TRUE}, {-1, -1, TRUE} },
      /* Intersecting ranges share same start and end points */
      { { 4, 20, TRUE}, { 4, 20, TRUE}, { 4, 20, TRUE}, {-1, -1, TRUE} },
      { { 4, 20, TRUE}, {20,  4, TRUE}, {-1, -1, TRUE}, {-1, -1, TRUE} },
      { {20,  4, TRUE}, { 4, 20, TRUE}, {-1, -1, TRUE}, {-1, -1, TRUE} },
      { {20,  4, TRUE}, {20,  4, TRUE}, {20,  4, TRUE}, {-1, -1, TRUE} },
      /* Intersecting ranges share same start point */
      { { 7, 13, TRUE}, { 7, 19, TRUE}, { 7, 19, TRUE}, {-1, -1, TRUE} },
      { { 7, 13, TRUE}, {19,  7, TRUE}, {19, 13, TRUE}, {-1, -1, TRUE} },
      { {13,  7, TRUE}, {7,  19, TRUE}, {13, 19, TRUE}, {-1, -1, TRUE} },
      { {13,  7, TRUE}, {19,  7, TRUE}, {19,  7, TRUE}, {-1, -1, TRUE} },
      { { 7, 19, TRUE}, { 7, 13, TRUE}, { 7, 19, TRUE}, {-1, -1, TRUE} },
      { {19,  7, TRUE}, { 7, 13, TRUE}, {19, 13, TRUE}, {-1, -1, TRUE} },
      { { 7, 19, TRUE}, {13,  7, TRUE}, {13, 19, TRUE}, {-1, -1, TRUE} },
      { {19,  7, TRUE}, {13,  7, TRUE}, {19,  7, TRUE}, {-1, -1, TRUE} },
      /* Intersecting ranges share same end point */
      { {12, 23, TRUE}, {18, 23, TRUE}, {12, 23, TRUE}, {-1, -1, TRUE} },
      { {12, 23, TRUE}, {23, 18, TRUE}, {12, 18, TRUE}, {-1, -1, TRUE} },
      { {23, 12, TRUE}, {18, 23, TRUE}, {18, 12, TRUE}, {-1, -1, TRUE} },
      { {23, 12, TRUE}, {23, 18, TRUE}, {23, 12, TRUE}, {-1, -1, TRUE} },
      { {18, 23, TRUE}, {12, 23, TRUE}, {12, 23, TRUE}, {-1, -1, TRUE} },
      { {23, 18, TRUE}, {12, 23, TRUE}, {12, 18, TRUE}, {-1, -1, TRUE} },
      { {18, 23, TRUE}, {23, 12, TRUE}, {18, 12, TRUE}, {-1, -1, TRUE} },
      { {23, 18, TRUE}, {23, 12, TRUE}, {23, 12, TRUE}, {-1, -1, TRUE} } };
  int i;

  *msg = "combination of ranges";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < SIZE_OF_TEST_ARRAY; i++)
    {
      svn_merge_range_t *r1 = apr_palloc(pool, sizeof(*r1));
      svn_merge_range_t *r2 = apr_palloc(pool, sizeof(*r2));
      svn_merge_range_t *r1_expected = &(rangelist[i][2]);
      svn_merge_range_t *r2_expected = &(rangelist[i][3]);

      r1->start = rangelist[i][0].start;
      r1->end = rangelist[i][0].end;
      r1->inheritable = TRUE;

      r2->start = rangelist[i][1].start;
      r2->end = rangelist[i][1].end;
      r2->inheritable = TRUE;

      svn_range_compact(&r1, &r2);
      if (!(((!r1 && r1_expected->start == -1
              && r1_expected->end == -1)
           || (r1 && (r1->start == r1_expected->start
               && r1->end == r1_expected->end)))
          && ((!r2 && r2_expected->start == -1
               && r2_expected->end == -1)
              || (r2 && (r2->start == r2_expected->start
                  && r2->end == r2_expected->end)))))
        {
          const char *fail_msg = "svn_range_compact() should combine ranges ";
          fail_msg = apr_pstrcat(pool, fail_msg,
                                 apr_psprintf(pool, "(%ld-%ld),(%ld-%ld) "
                                              "into ",
                                              rangelist[i][0].start,
                                              rangelist[i][0].end,
                                              rangelist[i][1].start,
                                              rangelist[i][1].end), NULL);
          if (r1_expected->start == -1)
            fail_msg = apr_pstrcat(pool, fail_msg, "(NULL),",NULL);
          else
            fail_msg = apr_pstrcat(pool, fail_msg,
                                   apr_psprintf(pool, "(%ld-%ld),",
                                                r1_expected->start,
                                                r1_expected->end), NULL);
          if (r2_expected->start == -1)
            fail_msg = apr_pstrcat(pool, fail_msg, "(NULL) ",NULL);
          else
            fail_msg = apr_pstrcat(pool, fail_msg,
                                   apr_psprintf(pool, "(%ld-%ld) ",
                                                r2_expected->start,
                                                r2_expected->end), NULL);
          fail_msg = apr_pstrcat(pool, fail_msg, "but instead resulted in ",
                                 NULL);
          if (r1)
            fail_msg = apr_pstrcat(pool, fail_msg,
                                   apr_psprintf(pool, "(%ld-%ld),",
                                                r1->start, r1->end), NULL);
          else
            fail_msg = apr_pstrcat(pool, fail_msg, "(NULL),",NULL);
          if (r2)
            fail_msg = apr_pstrcat(pool, fail_msg,
                                   apr_psprintf(pool, "(%ld-%ld),",
                                                r2->start, r2->end), NULL);
          else
            fail_msg = apr_pstrcat(pool, fail_msg, "(NULL)",NULL);

          return fail(pool, fail_msg);
        }
    }
  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_parse_single_line_mergeinfo),
    SVN_TEST_PASS(test_mergeinfo_dup),
    SVN_TEST_PASS(test_parse_combine_rangeinfo),
    SVN_TEST_PASS(test_parse_broken_mergeinfo),
    SVN_TEST_PASS(test_parse_multi_line_mergeinfo),
    SVN_TEST_PASS(test_remove_rangelist),
    SVN_TEST_PASS(test_remove_mergeinfo),
    SVN_TEST_PASS(test_rangelist_reverse),
    SVN_TEST_PASS(test_rangelist_count_revs),
    SVN_TEST_PASS(test_rangelist_to_revs),
    SVN_TEST_PASS(test_rangelist_intersect),
    SVN_TEST_PASS(test_diff_mergeinfo),
    SVN_TEST_PASS(test_merge_mergeinfo),
    SVN_TEST_PASS(test_rangelist_to_string),
    SVN_TEST_PASS(test_mergeinfo_to_string),
    SVN_TEST_PASS(test_range_compact),
    SVN_TEST_NULL
  };
