/*
 * mergeinfo-test.c -- test the mergeinfo functions
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <stdio.h>
#include <string.h>
#include <apr_hash.h>
#include <apr_tables.h>

#define SVN_DEPRECATED

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_mergeinfo.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_error_private.h"
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

#define MAX_NBR_RANGES 5

/* Verify that INPUT is parsed properly, and returns an error if
   parsing fails, or incorret parsing is detected.  Assumes that INPUT
   contains only one path -> ranges mapping, and that EXPECTED_RANGES points
   to the first range in an array whose size is greater than or equal to
   the number of ranges in INPUTS path -> ranges mapping but less than
   MAX_NBR_RANGES.  If fewer than MAX_NBR_RANGES ranges are present, then the
   trailing expected_ranges should be have their end revision set to 0. */
static svn_error_t *
verify_mergeinfo_parse(const char *input,
                       const char *expected_path,
                       const svn_merge_range_t *expected_ranges,
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
      void *val;
      svn_rangelist_t *ranges;
      svn_merge_range_t *range;
      int j;

      apr_hash_this(hi, &path, NULL, &val);
      ranges = val;
      if (strcmp((const char *) path, expected_path) != 0)
        return fail(pool, "svn_mergeinfo_parse (%s) failed to parse the "
                    "correct path (%s)", input, expected_path);

      /* Test each parsed range. */
      for (j = 0; j < ranges->nelts; j++)
        {
          range = APR_ARRAY_IDX(ranges, j, svn_merge_range_t *);
          if (range->start != expected_ranges[j].start
              || range->end != expected_ranges[j].end
              || range->inheritable != expected_ranges[j].inheritable)
            return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "svn_mergeinfo_parse (%s) failed to "
                                     "parse the correct range",
                                     input);
        }

      /* Were we expecting any more ranges? */
      if (j < MAX_NBR_RANGES - 1
          && expected_ranges[j].end != 0)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_mergeinfo_parse (%s) failed to "
                                 "produce the expected number of ranges",
                                  input);
    }
  return SVN_NO_ERROR;
}


#define NBR_MERGEINFO_VALS 25

/* Valid mergeinfo values. */
static const char * const mergeinfo_vals[NBR_MERGEINFO_VALS] =
  {
    "/trunk:1",
    "/trunk/foo:1-6",
    "/trunk: 5,7-9,10,11,13,14",
    "/trunk: 3-10,11*,13,14",
    "/branch: 1,2-18*,33*",
    /* Path names containing ':'s */
    "patch-common::netasq-bpf.c:25381",
    "patch-common_netasq-bpf.c::25381",
    ":patch:common:netasq:bpf.c:25381",
    /* Unordered rangelists */
    "/trunk:3-6,15,18,9,22",
    "/trunk:5,3",
    "/trunk:3-6*,15*,18*,9,22*",
    "/trunk:5,3*",
    "/trunk:100,3-7,50,99,1-2",
    /* Overlapping rangelists */
    "/gunther_branch:5-10,7-12",
    "/gunther_branch:5-10*,7-12*",
    "/branches/branch1:43832-45742,49990-53669,43832-49987",
    /* Unordered and overlapping rangelists */
    "/gunther_branch:7-12,1,5-10",
    "/gunther_branch:7-12*,1,5-10*",
    /* Adjacent rangelists of differing inheritability. */
    "/b5:5-53,1-4,54-90*",
    "/c0:1-77,12-44",
    /* Non-canonical paths. */
    "/A/:7-8",
    "/A///:7-8",
    "/A/.:7-8",
    "/A/./B:7-8",
    ":7-8",
  };
/* Paths corresponding to mergeinfo_vals. */
static const char * const mergeinfo_paths[NBR_MERGEINFO_VALS] =
  {
    "/trunk",
    "/trunk/foo",
    "/trunk",
    "/trunk",
    "/branch",

    /* svn_mergeinfo_parse converts relative merge soure paths to absolute. */
    "/patch-common::netasq-bpf.c",
    "/patch-common_netasq-bpf.c:",
    "/:patch:common:netasq:bpf.c",

    "/trunk",
    "/trunk",
    "/trunk",
    "/trunk",
    "/trunk",
    "/gunther_branch",
    "/gunther_branch",
    "/branches/branch1",
    "/gunther_branch",
    "/gunther_branch",
    "/b5",
    "/c0",

    /* non-canonical paths converted to canonical */
    "/A",
    "/A",
    "/A",
    "/A/B",
    "/",
  };
/* First ranges from the paths identified by mergeinfo_paths. */
static svn_merge_range_t mergeinfo_ranges[NBR_MERGEINFO_VALS][MAX_NBR_RANGES] =
  {
    { {0, 1,  TRUE} },
    { {0, 6,  TRUE} },
    { {4, 5,  TRUE}, { 6, 11, TRUE }, {12, 14, TRUE } },
    { {2, 10, TRUE}, {10, 11, FALSE}, {12, 14, TRUE } },
    { {0, 1,  TRUE}, { 1, 18, FALSE}, {32, 33, FALSE} },
    { {25380, 25381, TRUE } },
    { {25380, 25381, TRUE } },
    { {25380, 25381, TRUE } },
    { {2, 6, TRUE}, {8, 9, TRUE}, {14, 15, TRUE}, {17, 18, TRUE},
      {21, 22, TRUE} },
    { {2, 3, TRUE}, {4, 5, TRUE} },
    { {2, 6, FALSE}, {8, 9, TRUE}, {14, 15, FALSE}, {17, 18, FALSE},
      {21, 22, FALSE} },
    { {2, 3, FALSE}, {4, 5, TRUE} },
    { {0, 7, TRUE}, {49, 50, TRUE}, {98, 100, TRUE} },
    { {4, 12, TRUE} },
    { {4, 12, FALSE} },
    { {43831, 49987, TRUE}, {49989, 53669, TRUE} },
    { {0, 1, TRUE}, {4, 12, TRUE} },
    { {0, 1, TRUE}, {4, 12, FALSE} },
    { {0, 53, TRUE}, {53, 90, FALSE} },
    { {0, 77, TRUE} },
    { {6, 8, TRUE} },
    { {6, 8, TRUE} },
    { {6, 8, TRUE} },
    { {6, 8, TRUE} },
    { {6, 8, TRUE} },
  };

static svn_error_t *
test_parse_single_line_mergeinfo(apr_pool_t *pool)
{
  int i;

  for (i = 0; i < NBR_MERGEINFO_VALS; i++)
    SVN_ERR(verify_mergeinfo_parse(mergeinfo_vals[i], mergeinfo_paths[i],
                                   mergeinfo_ranges[i], pool));

  return SVN_NO_ERROR;
}

static const char *single_mergeinfo = "/trunk: 5,7-9,10,11,13,14";

static svn_error_t *
test_mergeinfo_dup(apr_pool_t *pool)
{
  apr_hash_t *orig_mergeinfo, *copied_mergeinfo;
  apr_pool_t *subpool;
  svn_rangelist_t *rangelist;

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
  svn_pool_destroy(subpool);
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
test_parse_combine_rangeinfo(apr_pool_t *pool)
{
  apr_array_header_t *result;
  svn_merge_range_t *resultrange;
  apr_hash_t *info1;

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


#define NBR_BROKEN_MERGEINFO_VALS 26
/* Invalid mergeinfo values. */
static const char * const broken_mergeinfo_vals[NBR_BROKEN_MERGEINFO_VALS] =
  {
    /* Invalid grammar  */
    "/missing-revs",
    "/trunk: 5,7-9,10,11,13,14,",
    "/trunk 5,7-9,10,11,13,14",
    "/trunk:5 7--9 10 11 13 14",
    /* Overlapping revs differing inheritability */
    "/trunk:5-9*,9",
    "/trunk:5,5-9*",
    "/trunk:5-9,9*",
    "/trunk:5*,5-9",
    "/trunk:4,4*",
    "/trunk:4*,4",
    "/trunk:3-7*,4-23",
    "/trunk:3-7,4-23*",
    /* Reversed revision ranges */
    "/trunk:22-20",
    "/trunk:22-20*",
    "/trunk:3,7-12,22-20,25",
    "/trunk:3,7,22-20*,25-30",
    /* Range with same start and end revision */
    "/trunk:22-22",
    "/trunk:22-22*",
    "/trunk:3,7-12,20-20,25",
    "/trunk:3,7,20-20*,25-30",
    /* path mapped to range with no revisions */
    "/trunk:",
    "/trunk:2-9\n/branch:",
    "::",
    /* Invalid revisions */
    "trunk:a-3",
    "branch:3-four",
    "trunk:yadayadayada"
  };

static svn_error_t *
test_parse_broken_mergeinfo(apr_pool_t *pool)
{
  int i;
  svn_error_t *err;
  apr_hash_t *info1;

  /* Trigger some error(s) with mal-formed input. */
  for (i = 0; i < NBR_BROKEN_MERGEINFO_VALS; i++)
    {
      err = svn_mergeinfo_parse(&info1, broken_mergeinfo_vals[i], pool);
      if (err == SVN_NO_ERROR)
        {
          return fail(pool, "svn_mergeinfo_parse (%s) failed to detect an error",
                      broken_mergeinfo_vals[i]);
        }
      else if (err->apr_err != SVN_ERR_MERGEINFO_PARSE_ERROR)
        {
          svn_error_clear(err);
          return fail(pool, "svn_mergeinfo_parse (%s) returned some error other"
                      " than SVN_ERR_MERGEINFO_PARSE_ERROR",
                      broken_mergeinfo_vals[i]);
        }
      else
        {
          svn_error_clear(err);
        }
    }

  return SVN_NO_ERROR;
}


static const char *mergeinfo1 = "/trunk: 3,5,7-9,10,11,13,14\n/fred:8-10";

#define NBR_RANGELIST_DELTAS 4


/* Convert a single svn_merge_range_t * back into an svn_stringbuf_t *.  */
static char *
range_to_string(svn_merge_range_t *range,
                apr_pool_t *pool)
{
  if (range->start == range->end - 1)
    return apr_psprintf(pool, "%ld%s", range->end,
                        range->inheritable
                        ? "" : SVN_MERGEINFO_NONINHERITABLE_STR);
  else
    return apr_psprintf(pool, "%ld-%ld%s", range->start + 1,
                        range->end, range->inheritable
                        ? "" : SVN_MERGEINFO_NONINHERITABLE_STR);
}


/* Verify that ACTUAL_RANGELIST matches EXPECTED_RANGES (an array of
   NBR_EXPECTED length).  Return an error based careful examination if
   they do not match.  FUNC_VERIFIED is the name of the API being
   verified (e.g. "svn_rangelist_intersect"), while TYPE is a word
   describing what the ranges being examined represent. */
static svn_error_t *
verify_ranges_match(const svn_rangelist_t *actual_rangelist,
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
        return fail(pool, "%s should report range %s, but found %s",
                    func_verified,
                    range_to_string(&expected_ranges[i], pool),
                    range_to_string(range, pool));
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
  svn_rangelist_t *rangelist;

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
test_diff_mergeinfo(apr_pool_t *pool)
{
  apr_hash_t *deleted, *added, *from, *to;
  svn_merge_range_t expected_rangelist_deletions[NBR_RANGELIST_DELTAS] =
    { {6, 7, TRUE}, {8, 9, TRUE}, {10, 11, TRUE}, {32, 34, TRUE} };
  svn_merge_range_t expected_rangelist_additions[NBR_RANGELIST_DELTAS] =
    { {1, 2, TRUE}, {4, 6, TRUE}, {12, 16, TRUE}, {29, 30, TRUE} };

  SVN_ERR(svn_mergeinfo_parse(&from, "/trunk: 1,3-4,7,9,11-12,31-34", pool));
  SVN_ERR(svn_mergeinfo_parse(&to, "/trunk: 1-6,12-16,30-32", pool));
  /* On /trunk: deleted (7, 9, 11, 33-34) and added (2, 5-6, 13-16, 30) */
  SVN_ERR(svn_mergeinfo_diff(&deleted, &added, from, to,
                             FALSE, pool));

  /* Verify calculation of range list deltas. */
  SVN_ERR(verify_mergeinfo_deltas(deleted, expected_rangelist_deletions,
                                  "svn_mergeinfo_diff", "deletion", pool));
  SVN_ERR(verify_mergeinfo_deltas(added, expected_rangelist_additions,
                                  "svn_mergeinfo_diff", "addition", pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_reverse(apr_pool_t *pool)
{
  svn_rangelist_t *rangelist;
  svn_merge_range_t expected_rangelist[3] =
    { {10, 9, TRUE}, {7, 4, TRUE}, {3, 2, TRUE} };

  SVN_ERR(svn_rangelist__parse(&rangelist, "3,5-7,10", pool));

  SVN_ERR(svn_rangelist_reverse(rangelist, pool));

  return verify_ranges_match(rangelist, expected_rangelist, 3,
                             "svn_rangelist_reverse", "reversal", pool);
}

static svn_error_t *
test_rangelist_intersect(apr_pool_t *pool)
{
  svn_rangelist_t *rangelist1, *rangelist2, *intersection;

  /* Expected intersection when considering inheritance. */
  svn_merge_range_t intersection_consider_inheritance[] =
    { {0, 1, TRUE}, {11, 12, TRUE}, {30, 32, FALSE}, {39, 42, TRUE} };

  /* Expected intersection when ignoring inheritance. */
  svn_merge_range_t intersection_ignore_inheritance[] =
    { {0, 1, TRUE}, {2, 4, TRUE}, {11, 12, TRUE}, {30, 32, FALSE},
      {39, 42, TRUE} };

  SVN_ERR(svn_rangelist__parse(&rangelist1, "1-6,12-16,30-32*,40-42", pool));
  SVN_ERR(svn_rangelist__parse(&rangelist2, "1,3-4*,7,9,11-12,31-34*,38-44",
                               pool));

  /* Check the intersection while considering inheritance twice, reversing
     the order of the rangelist arguments on the second call to
     svn_rangelist_intersection.  The order *should* have no effect on
     the result -- see http://svn.haxx.se/dev/archive-2010-03/0351.shtml.

     '3-4*' has different inheritance than '1-6', so no intersection is
     expected.  '30-32*' and '31-34*' have the same inheritance, so intersect
     at '31-32*'.  Per the svn_rangelist_intersect API, since both ranges
     are non-inheritable, so is the result. */
  SVN_ERR(svn_rangelist_intersect(&intersection, rangelist1, rangelist2,
                                  TRUE, pool));

  SVN_ERR(verify_ranges_match(intersection,
                              intersection_consider_inheritance,
                              4, "svn_rangelist_intersect", "intersect",
                              pool));

  SVN_ERR(svn_rangelist_intersect(&intersection, rangelist2, rangelist1,
                                  TRUE, pool));

  SVN_ERR(verify_ranges_match(intersection,
                              intersection_consider_inheritance,
                              4, "svn_rangelist_intersect", "intersect",
                              pool));

  /* Check the intersection while ignoring inheritance.  The one difference
     from when we consider inheritance is that '3-4*' and '1-6' now intersect,
     since we don't care about inheritability, just the start and end ranges.
     Per the svn_rangelist_intersect API, since only one range is
     non-inheritable the result is inheritable. */
  SVN_ERR(svn_rangelist_intersect(&intersection, rangelist1, rangelist2,
                                  FALSE, pool));

  SVN_ERR(verify_ranges_match(intersection,
                              intersection_ignore_inheritance,
                              5, "svn_rangelist_intersect", "intersect",
                              pool));

  SVN_ERR(svn_rangelist_intersect(&intersection, rangelist2, rangelist1,
                                  FALSE, pool));

  SVN_ERR(verify_ranges_match(intersection,
                              intersection_ignore_inheritance,
                              5, "svn_rangelist_intersect", "intersect",
                              pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
test_mergeinfo_intersect(apr_pool_t *pool)
{
  svn_merge_range_t expected_intersection[3] =
    { {0, 1, TRUE}, {2, 4, TRUE}, {11, 12, TRUE} };
  svn_rangelist_t *rangelist;
  apr_hash_t *intersection;
  apr_hash_t *info1, *info2;

  SVN_ERR(svn_mergeinfo_parse(&info1, "/trunk: 1-6,12-16\n/foo: 31", pool));
  SVN_ERR(svn_mergeinfo_parse(&info2, "/trunk: 1,3-4,7,9,11-12", pool));

  SVN_ERR(svn_mergeinfo_intersect(&intersection, info1, info2, pool));
  if (apr_hash_count(intersection) != 1)
    return fail(pool, "Unexpected number of rangelists in mergeinfo "
                "intersection: Expected %d, found %d", 1,
                apr_hash_count(intersection));

  rangelist = apr_hash_get(intersection, "/trunk", APR_HASH_KEY_STRING);
  return verify_ranges_match(rangelist, expected_intersection, 3,
                             "svn_rangelist_intersect", "intersect", pool);
}

static svn_error_t *
test_merge_mergeinfo(apr_pool_t *pool)
{
  int i;

  /* Structures and constants for test_merge_mergeinfo() */
  /* Number of svn_mergeinfo_merge test sets */
  #define NBR_MERGEINFO_MERGES 12

  /* Maximum number of expected paths in the results
     of the svn_mergeinfo_merge tests */
  #define MAX_NBR_MERGEINFO_PATHS 4

  /* Maximum number of expected ranges in the results
     of the svn_mergeinfo_merge tests */
  #define MAX_NBR_MERGEINFO_RANGES 10

  /* Struct to store a path and it's expected ranges,
     i.e. the expected result of an svn_mergeinfo_merge
     test. */
  struct mergeinfo_merge_path_range
    {
      const char *path;
      int expected_n;
      svn_merge_range_t expected_rngs[MAX_NBR_MERGEINFO_RANGES];
    };

  /* Struct for svn_mergeinfo_merge test data.
     If MERGEINFO1 and MERGEINFO2 are parsed to a hash with
     svn_mergeinfo_parse() and then merged with svn_mergeinfo_merge(),
     the resulting hash should have EXPECTED_PATHS number of paths
     mapped to rangelists and each mapping is described by PATH_RNGS
     where PATH_RNGS->PATH is not NULL. */
  struct mergeinfo_merge_test_data
    {
      const char *mergeinfo1;
      const char *mergeinfo2;
      int expected_paths;
      struct mergeinfo_merge_path_range path_rngs[MAX_NBR_MERGEINFO_PATHS];
    };

  static struct mergeinfo_merge_test_data mergeinfo[NBR_MERGEINFO_MERGES] =
    {
      /* One path, intersecting inheritable ranges */
      { "/trunk: 5-10",
        "/trunk: 6", 1,
        { {"/trunk", 1, { {4, 10, TRUE} } } } },

      /* One path, intersecting non-inheritable ranges */
      { "/trunk: 5-10*",
        "/trunk: 6*", 1,
        { {"/trunk", 1, { {4, 10, FALSE} } } } },

      /* One path, intersecting ranges with different inheritability */
      { "/trunk: 5-10",
        "/trunk: 6*", 1,
        { {"/trunk", 1, { {4, 10, TRUE} } } } },

      /* One path, intersecting ranges with different inheritability */
      { "/trunk: 5-10*",
        "/trunk: 6", 1,
        { {"/trunk", 3, { {4, 5, FALSE}, {5, 6, TRUE}, {6, 10, FALSE} } } } },

      /* Adjacent ranges all inheritable ranges */
      { "/trunk: 1,3,5-11,13",
        "/trunk: 2,4,12,14-22", 1,
        { {"/trunk", 1, { {0, 22, TRUE} } } } },

      /* Adjacent ranges all non-inheritable ranges */
      { "/trunk: 1*,3*,5-11*,13*",
        "/trunk: 2*,4*,12*,14-22*", 1,
        { {"/trunk", 1, { {0, 22, FALSE} } } } },

      /* Adjacent ranges differing inheritability */
      { "/trunk: 1*,3*,5-11*,13*",
        "/trunk: 2,4,12,14-22", 1,
        { {"/trunk", 8, { { 0,  1, FALSE}, { 1,  2, TRUE},
                          { 2,  3, FALSE}, { 3,  4, TRUE},
                          { 4, 11, FALSE}, {11, 12, TRUE},
                          {12, 13, FALSE}, {13, 22, TRUE} } } } },

      /* Adjacent ranges differing inheritability */
      { "/trunk: 1,3,5-11,13",
        "/trunk: 2*,4*,12*,14-22*", 1,
        { {"/trunk", 8, { { 0,  1, TRUE}, { 1,  2, FALSE},
                          { 2,  3, TRUE}, { 3,  4, FALSE},
                          { 4, 11, TRUE}, {11, 12, FALSE},
                          {12, 13, TRUE}, {13, 22, FALSE} } } } },

      /* Two paths all inheritable ranges */
      { "/trunk::1: 3,5,7-9,10,11,13,14\n/fred:8-10",
        "/trunk::1: 1-4,6\n/fred:9-12", 2,
        { {"/trunk::1", 2, { {0, 11, TRUE}, {12, 14, TRUE} } },
          {"/fred",     1, { {7, 12, TRUE} } } } },

      /* Two paths all non-inheritable ranges */
      { "/trunk: 3*,5*,7-9*,10*,11*,13*,14*\n/fred:8-10*",
        "/trunk: 1-4*,6*\n/fred:9-12*", 2,
        { {"/trunk", 2, { {0, 11, FALSE}, {12, 14, FALSE} } },
          {"/fred",  1, { {7, 12, FALSE} } } } },

      /* Two paths mixed inheritability */
      { "/trunk: 3,5*,7-9,10,11*,13,14\n/fred:8-10",
        "/trunk: 1-4,6\n/fred:9-12*", 2,
        { {"/trunk", 5, { { 0,  4, TRUE }, { 4,  5, FALSE}, {5, 10, TRUE},
                          {10, 11, FALSE}, {12, 14, TRUE } } },
          {"/fred",  2, { { 7, 10, TRUE }, {10, 12, FALSE} } } } },

      /* A slew of different paths but no ranges to be merged */
      { "/trunk: 3,5-9*\n/betty: 2-4",
        "/fred: 1-18\n/:barney: 1,3-43", 4,
        { {"/trunk",   2, { {2,  3, TRUE}, {4,  9, FALSE} } },
          {"/betty",   1, { {1,  4, TRUE} } },
          {"/:barney", 2, { {0,  1, TRUE}, {2, 43, TRUE} } },
          {"/fred",    1, { {0, 18, TRUE} } } } }
    };

  for (i = 0; i < NBR_MERGEINFO_MERGES; i++)
    {
      int j;
      svn_string_t *info2_starting, *info2_ending;
      apr_hash_t *info1, *info2;

      SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo[i].mergeinfo1, pool));
      SVN_ERR(svn_mergeinfo_parse(&info2, mergeinfo[i].mergeinfo2, pool));

      /* Make a copy of info2.  We will merge it into info1, but info2
         should remain unchanged.  Store the mergeinfo as a svn_string_t
         rather than making a copy and using svn_mergeinfo_diff().  Since
         that API uses some of the underlying code as svn_mergeinfo_merge
         we might mask potential errors. */
      SVN_ERR(svn_mergeinfo_to_string(&info2_starting, info2, pool));

      SVN_ERR(svn_mergeinfo_merge(info1, info2, pool));
      if (mergeinfo[i].expected_paths != (int)apr_hash_count(info1))
        return fail(pool, "Wrong number of paths in merged mergeinfo");

      /* Check that info2 remained unchanged. */
      SVN_ERR(svn_mergeinfo_to_string(&info2_ending, info2, pool));

      if (strcmp(info2_ending->data, info2_starting->data))
        return fail(pool,
                    apr_psprintf(pool,
                                 "svn_mergeinfo_merge case %i "
                                 "modified its CHANGES arg from "
                                 "%s to %s", i, info2_starting->data,
                                 info2_ending->data));

      for (j = 0; j < mergeinfo[i].expected_paths; j++)
        {
          svn_rangelist_t *rangelist =
            apr_hash_get(info1, mergeinfo[i].path_rngs[j].path,
                         APR_HASH_KEY_STRING);
          if (!rangelist)
            return fail(pool, "Missing path '%s' in merged mergeinfo",
                        mergeinfo[i].path_rngs[j].path);
          SVN_ERR(verify_ranges_match(
                    rangelist,
                    mergeinfo[i].path_rngs[j].expected_rngs,
                    mergeinfo[i].path_rngs[j].expected_n,
                    apr_psprintf(pool, "svn_rangelist_merge case %i:%i", i, j),
                    "merge", pool));
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_remove_rangelist(apr_pool_t *pool)
{
  int i, j;
  svn_error_t *err, *child_err;
  svn_rangelist_t *output, *eraser, *whiteboard;

  /* Struct for svn_rangelist_remove test data.
     Parse WHITEBOARD and ERASER to hashes and then get the rangelist for
     path 'A' from both.

     Remove ERASER's rangelist from WHITEBOARD's twice, once while
     considering inheritance and once while not.  In the first case the
     resulting rangelist should have EXPECTED_RANGES_CONSIDER_INHERITANCE
     number of ranges and these ranges should match the ranges in
     EXPECTED_REMOVED_CONSIDER_INHERITANCE.  In the second case there
     should be EXPECTED_RANGES_IGNORE_INHERITANCE number of ranges and
     these should match EXPECTED_REMOVED_IGNORE_INHERITANCE */
  struct rangelist_remove_test_data
  {
    const char *whiteboard;
    const char *eraser;
    int expected_ranges_consider_inheritance;
    svn_merge_range_t expected_removed_consider_inheritance[10];
    int expected_ranges_ignore_inheritance;
    svn_merge_range_t expected_removed_ignore_inheritance[10];
  };

  #define SIZE_OF_RANGE_REMOVE_TEST_ARRAY 15

  /* The actual test data */
  struct rangelist_remove_test_data test_data[SIZE_OF_RANGE_REMOVE_TEST_ARRAY] =
    {
      /* Eraser is a proper subset of whiteboard */
      {"1-44",  "5",  2, { {0,  4, TRUE }, {5, 44, TRUE }},
                      2, { {0,  4, TRUE }, {5, 44, TRUE }}},
      {"1-44*", "5",  1, { {0, 44, FALSE} },
                      2, { {0,  4, FALSE}, {5, 44, FALSE}}},
      {"1-44",  "5*", 1, { {0, 44, TRUE } },
                      2, { {0,  4, TRUE }, {5, 44, TRUE }}},
      {"1-44*", "5*", 2, { {0,  4, FALSE}, {5, 44, FALSE}},
                      2, { {0,  4, FALSE}, {5, 44, FALSE}}},
      /* Non-intersecting ranges...nothing is removed */
      {"2-9,14-19",   "12",  2, { {1, 9, TRUE }, {13, 19, TRUE }},
                             2, { {1, 9, TRUE }, {13, 19, TRUE }}},
      {"2-9*,14-19*", "12",  2, { {1, 9, FALSE}, {13, 19, FALSE}},
                             2, { {1, 9, FALSE}, {13, 19, FALSE}}},
      {"2-9,14-19",   "12*", 2, { {1, 9, TRUE }, {13, 19, TRUE }},
                             2, { {1, 9, TRUE }, {13, 19, TRUE }}},
      {"2-9*,14-19*", "12*", 2, { {1, 9, FALSE}, {13, 19, FALSE}},
                             2, { {1, 9, FALSE}, {13, 19, FALSE}}},
      /* Eraser overlaps whiteboard */
      {"1,9-17",  "12-20",  2, { {0,  1, TRUE }, {8, 11, TRUE }},
                            2, { {0,  1, TRUE }, {8, 11, TRUE }}},
      {"1,9-17*", "12-20",  2, { {0,  1, TRUE }, {8, 17, FALSE}},
                            2, { {0,  1, TRUE }, {8, 11, FALSE}}},
      {"1,9-17",  "12-20*", 2, { {0,  1, TRUE }, {8, 17, TRUE }},
                            2, { {0,  1, TRUE }, {8, 11, TRUE }}},
      {"1,9-17*", "12-20*", 2, { {0,  1, TRUE }, {8, 11, FALSE}},
                            2, { {0,  1, TRUE }, {8, 11, FALSE}}},
      /* Empty rangelist */
      {"",  "",           0, { {0, 0, FALSE}},
                          0, { {0, 0, FALSE}}},
      {"",  "5-8,10-100", 0, { {0, 0, FALSE}},
                          0, { {0, 0, FALSE}}},
      {"5-8,10-100",  "", 2, { {4, 8, TRUE }, {9, 100, TRUE }},
                          2, { {4, 8, TRUE }, {9, 100, TRUE }}}
    };

  err = child_err = SVN_NO_ERROR;
  for (j = 0; j < 2; j++)
    {
      for (i = 0; i < SIZE_OF_RANGE_REMOVE_TEST_ARRAY; i++)
        {
          int expected_nbr_ranges;
          svn_merge_range_t *expected_ranges;
          svn_string_t *eraser_starting;
          svn_string_t *eraser_ending;
          svn_string_t *whiteboard_starting;
          svn_string_t *whiteboard_ending;

          SVN_ERR(svn_rangelist__parse(&eraser, test_data[i].eraser, pool));
          SVN_ERR(svn_rangelist__parse(&whiteboard, test_data[i].whiteboard, pool));

          /* Represent empty mergeinfo with an empty rangelist. */
          if (eraser == NULL)
            eraser = apr_array_make(pool, 0, sizeof(*eraser));
          if (whiteboard == NULL)
            whiteboard = apr_array_make(pool, 0, sizeof(*whiteboard));

          /* First pass try removal considering inheritance, on the
             second pass ignore it. */
          if (j == 0)
            {
              expected_nbr_ranges = (test_data[i]).expected_ranges_consider_inheritance;
              expected_ranges = (test_data[i]).expected_removed_consider_inheritance;

            }
          else
            {
              expected_nbr_ranges = (test_data[i]).expected_ranges_ignore_inheritance;
              expected_ranges = (test_data[i]).expected_removed_ignore_inheritance;

            }

         /* Make a copies of whiteboard and eraser.  They should not be
            modified by svn_rangelist_remove(). */
         SVN_ERR(svn_rangelist_to_string(&eraser_starting, eraser, pool));
         SVN_ERR(svn_rangelist_to_string(&whiteboard_starting, whiteboard,
                                         pool));

          SVN_ERR(svn_rangelist_remove(&output, eraser, whiteboard,
                                       j == 0,
                                       pool));
          child_err = verify_ranges_match(output, expected_ranges,
                                          expected_nbr_ranges,
                                          apr_psprintf(pool,
                                                       "svn_rangelist_remove "
                                                       "case %i", i),
                                          "remove", pool);

          /* Collect all the errors rather than returning on the first. */
          if (child_err)
            {
              if (err)
                svn_error_compose(err, child_err);
              else
                err = child_err;
            }

          /* Check that eraser and whiteboard were not modified. */
          SVN_ERR(svn_rangelist_to_string(&eraser_ending, eraser, pool));
          SVN_ERR(svn_rangelist_to_string(&whiteboard_ending, whiteboard,
                                          pool));
          if (strcmp(eraser_starting->data, eraser_ending->data))
            {
              child_err = fail(pool,
                               apr_psprintf(pool,
                                            "svn_rangelist_remove case %i "
                                            "modified its ERASER arg from "
                                            "%s to %s when %sconsidering "
                                            "inheritance", i,
                                            eraser_starting->data,
                                            eraser_ending->data,
                                            j ? "" : "not "));
              if (err)
                svn_error_compose(err, child_err);
              else
                err = child_err;
            }
          if (strcmp(whiteboard_starting->data, whiteboard_ending->data))
            {
              child_err = fail(pool,
                               apr_psprintf(pool,
                                            "svn_rangelist_remove case %i "
                                            "modified its WHITEBOARD arg "
                                            "from %s to %s when "
                                            "%sconsidering inheritance", i,
                                            whiteboard_starting->data,
                                            whiteboard_ending->data,
                                            j ? "" : "not "));
              if (err)
                svn_error_compose(err, child_err);
              else
                err = child_err;
            }
        }
    }
  return err;
}

#define RANDOM_REV_ARRAY_LENGTH 100

/* Random number seed. */
static apr_uint32_t random_rev_array_seed;

/* Set a random 3/4-ish of the elements of array REVS[RANDOM_REV_ARRAY_LENGTH]
 * to TRUE and the rest to FALSE. */
static void
randomly_fill_rev_array(svn_boolean_t *revs)
{
  int i;
  for (i = 0; i < RANDOM_REV_ARRAY_LENGTH; i++)
    {
      apr_uint32_t next = svn_test_rand(&random_rev_array_seed);
      revs[i] = (next < 0x40000000) ? 0 : 1;
    }
}

/* Set *RANGELIST to a rangelist representing the revisions that are marked
 * with TRUE in the array REVS[RANDOM_REV_ARRAY_LENGTH]. */
static svn_error_t *
rev_array_to_rangelist(svn_rangelist_t **rangelist,
                       svn_boolean_t *revs,
                       apr_pool_t *pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create("/trunk: ", pool);
  svn_boolean_t first = TRUE;
  apr_hash_t *mergeinfo;
  int i;

  for (i = 0; i < RANDOM_REV_ARRAY_LENGTH; i++)
    {
      if (revs[i])
        {
          if (first)
            first = FALSE;
          else
            svn_stringbuf_appendcstr(buf, ",");
          svn_stringbuf_appendcstr(buf, apr_psprintf(pool, "%d", i));
        }
    }

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo, buf->data, pool));
  *rangelist = apr_hash_get(mergeinfo, "/trunk", APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_remove_randomly(apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool;

  random_rev_array_seed = (apr_uint32_t) apr_time_now();

  iterpool = svn_pool_create(pool);

  for (i = 0; i < 20; i++)
    {
      svn_boolean_t first_revs[RANDOM_REV_ARRAY_LENGTH],
        second_revs[RANDOM_REV_ARRAY_LENGTH],
        expected_revs[RANDOM_REV_ARRAY_LENGTH];
      svn_rangelist_t *first_rangelist, *second_rangelist,
        *expected_rangelist, *actual_rangelist;
      /* There will be at most RANDOM_REV_ARRAY_LENGTH ranges in
         expected_rangelist. */
      svn_merge_range_t expected_range_array[RANDOM_REV_ARRAY_LENGTH];
      int j;

      svn_pool_clear(iterpool);

      randomly_fill_rev_array(first_revs);
      randomly_fill_rev_array(second_revs);
      /* There is no change numbered "r0" */
      first_revs[0] = FALSE;
      second_revs[0] = FALSE;
      for (j = 0; j < RANDOM_REV_ARRAY_LENGTH; j++)
        expected_revs[j] = second_revs[j] && !first_revs[j];

      SVN_ERR(rev_array_to_rangelist(&first_rangelist, first_revs, iterpool));
      SVN_ERR(rev_array_to_rangelist(&second_rangelist, second_revs, iterpool));
      SVN_ERR(rev_array_to_rangelist(&expected_rangelist, expected_revs,
                                     iterpool));

      for (j = 0; j < expected_rangelist->nelts; j++)
        {
          expected_range_array[j] = *(APR_ARRAY_IDX(expected_rangelist, j,
                                                    svn_merge_range_t *));
        }

      SVN_ERR(svn_rangelist_remove(&actual_rangelist, first_rangelist,
                                   second_rangelist, TRUE, iterpool));

      SVN_ERR(verify_ranges_match(actual_rangelist,
                                  expected_range_array,
                                  expected_rangelist->nelts,
                                  "svn_rangelist_remove random call",
                                  "remove", iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_intersect_randomly(apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool;

  random_rev_array_seed = (apr_uint32_t) apr_time_now();

  iterpool = svn_pool_create(pool);

  for (i = 0; i < 20; i++)
    {
      svn_boolean_t first_revs[RANDOM_REV_ARRAY_LENGTH],
        second_revs[RANDOM_REV_ARRAY_LENGTH],
        expected_revs[RANDOM_REV_ARRAY_LENGTH];
      svn_rangelist_t *first_rangelist, *second_rangelist,
        *expected_rangelist, *actual_rangelist;
      /* There will be at most RANDOM_REV_ARRAY_LENGTH ranges in
         expected_rangelist. */
      svn_merge_range_t expected_range_array[RANDOM_REV_ARRAY_LENGTH];
      int j;

      svn_pool_clear(iterpool);

      randomly_fill_rev_array(first_revs);
      randomly_fill_rev_array(second_revs);
      /* There is no change numbered "r0" */
      first_revs[0] = FALSE;
      second_revs[0] = FALSE;
      for (j = 0; j < RANDOM_REV_ARRAY_LENGTH; j++)
        expected_revs[j] = second_revs[j] && first_revs[j];

      SVN_ERR(rev_array_to_rangelist(&first_rangelist, first_revs, iterpool));
      SVN_ERR(rev_array_to_rangelist(&second_rangelist, second_revs, iterpool));
      SVN_ERR(rev_array_to_rangelist(&expected_rangelist, expected_revs,
                                     iterpool));

      for (j = 0; j < expected_rangelist->nelts; j++)
        {
          expected_range_array[j] = *(APR_ARRAY_IDX(expected_rangelist, j,
                                                    svn_merge_range_t *));
        }

      SVN_ERR(svn_rangelist_intersect(&actual_rangelist, first_rangelist,
                                      second_rangelist, TRUE, iterpool));

      SVN_ERR(verify_ranges_match(actual_rangelist,
                                  expected_range_array,
                                  expected_rangelist->nelts,
                                  "svn_rangelist_intersect random call",
                                  "intersect", iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* ### Share code with test_diff_mergeinfo() and test_remove_rangelist(). */
static svn_error_t *
test_remove_mergeinfo(apr_pool_t *pool)
{
  apr_hash_t *output, *whiteboard, *eraser;
  svn_merge_range_t expected_rangelist_remainder[NBR_RANGELIST_DELTAS] =
    { {6, 7, TRUE}, {8, 9, TRUE}, {10, 11, TRUE}, {32, 34, TRUE} };

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
test_rangelist_to_string(apr_pool_t *pool)
{
  svn_rangelist_t *result;
  svn_string_t *output;
  svn_string_t *expected = svn_string_create("3,5,7-11,13-14", pool);
  apr_hash_t *info1;

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo1, pool));

  result = apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING);
  if (!result)
    return fail(pool, "Missing path in parsed mergeinfo");

  SVN_ERR(svn_rangelist_to_string(&output, result, pool));

  if (!svn_string_compare(expected, output))
    return fail(pool, "Rangelist string not what we expected");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_mergeinfo_to_string(apr_pool_t *pool)
{
  svn_string_t *output;
  svn_string_t *expected;
  apr_hash_t *info1, *info2;
  expected = svn_string_create("/fred:8-10\n/trunk:3,5,7-11,13-14", pool);

  SVN_ERR(svn_mergeinfo_parse(&info1, mergeinfo1, pool));

  SVN_ERR(svn_mergeinfo_to_string(&output, info1, pool));

  if (!svn_string_compare(expected, output))
    return fail(pool, "Mergeinfo string not what we expected");

  /* Manually construct some mergeinfo with relative path
     merge source keys.  These should be tolerated as input
     to svn_mergeinfo_to_string(), but the resulting svn_string_t
     should have absolute keys. */
  info2 = apr_hash_make(pool);
  apr_hash_set(info2, "fred",
               APR_HASH_KEY_STRING,
               apr_hash_get(info1, "/fred", APR_HASH_KEY_STRING));
  apr_hash_set(info2, "trunk",
               APR_HASH_KEY_STRING,
               apr_hash_get(info1, "/trunk", APR_HASH_KEY_STRING));
  SVN_ERR(svn_mergeinfo_to_string(&output, info2, pool));

  if (!svn_string_compare(expected, output))
    return fail(pool, "Mergeinfo string not what we expected");

  return SVN_NO_ERROR;
}


static svn_error_t *
test_rangelist_merge(apr_pool_t *pool)
{
  int i;
  svn_error_t *err, *child_err;
  svn_rangelist_t *rangelist1, *rangelist2;

  /* Struct for svn_rangelist_merge test data.  Similar to
     mergeinfo_merge_test_data struct in svn_mergeinfo_merge() test. */
  struct rangelist_merge_test_data
  {
    const char *mergeinfo1;
    const char *mergeinfo2;
    int expected_ranges;
    svn_merge_range_t expected_merge[6];
  };

  #define SIZE_OF_RANGE_MERGE_TEST_ARRAY 68
  /* The actual test data. */
  struct rangelist_merge_test_data test_data[SIZE_OF_RANGE_MERGE_TEST_ARRAY] =
    {
      /* Non-intersecting ranges */
      {"1-44",    "70-101",  2, {{ 0, 44, TRUE }, {69, 101, TRUE }}},
      {"1-44*",   "70-101",  2, {{ 0, 44, FALSE}, {69, 101, TRUE }}},
      {"1-44",    "70-101*", 2, {{ 0, 44, TRUE }, {69, 101, FALSE}}},
      {"1-44*",   "70-101*", 2, {{ 0, 44, FALSE}, {69, 101, FALSE}}},
      {"70-101",  "1-44",    2, {{ 0, 44, TRUE }, {69, 101, TRUE }}},
      {"70-101*", "1-44",    2, {{ 0, 44, TRUE }, {69, 101, FALSE}}},
      {"70-101",  "1-44*",   2, {{ 0, 44, FALSE}, {69, 101, TRUE }}},
      {"70-101*", "1-44*",   2, {{ 0, 44, FALSE}, {69, 101, FALSE}}},

      /* Intersecting ranges with same starting and ending revisions */
      {"4-20",  "4-20",  1, {{3, 20, TRUE }}},
      {"4-20*", "4-20",  1, {{3, 20, TRUE }}},
      {"4-20",  "4-20*", 1, {{3, 20, TRUE }}},
      {"4-20*", "4-20*", 1, {{3, 20, FALSE}}},

      /* Intersecting ranges with same starting revision */
      {"6-17",  "6-12",  1, {{5, 17, TRUE}}},
      {"6-17*", "6-12",  2, {{5, 12, TRUE }, {12, 17, FALSE}}},
      {"6-17",  "6-12*", 1, {{5, 17, TRUE }}},
      {"6-17*", "6-12*", 1, {{5, 17, FALSE}}},
      {"6-12",  "6-17",  1, {{5, 17, TRUE }}},
      {"6-12*", "6-17",  1, {{5, 17, TRUE }}},
      {"6-12",  "6-17*", 2, {{5, 12, TRUE }, {12, 17, FALSE}}},
      {"6-12*", "6-17*", 1, {{5, 17, FALSE}}},

      /* Intersecting ranges with same ending revision */
      {"5-77",   "44-77",  1, {{4, 77, TRUE }}},
      {"5-77*",  "44-77",  2, {{4, 43, FALSE}, {43, 77, TRUE}}},
      {"5-77",   "44-77*", 1, {{4, 77, TRUE }}},
      {"5-77*",  "44-77*", 1, {{4, 77, FALSE}}},
      {"44-77",  "5-77",   1, {{4, 77, TRUE }}},
      {"44-77*", "5-77",   1, {{4, 77, TRUE }}},
      {"44-77",  "5-77*",  2, {{4, 43, FALSE}, {43, 77, TRUE}}},
      {"44-77*", "5-77*",  1, {{4, 77, FALSE}}},

      /* Intersecting ranges with different starting and ending revision
         where one range is a proper subset of the other. */
      {"12-24",  "20-23",  1, {{11, 24, TRUE }}},
      {"12-24*", "20-23",  3, {{11, 19, FALSE}, {19, 23, TRUE },
                               {23, 24, FALSE}}},
      {"12-24",  "20-23*", 1, {{11, 24, TRUE }}},
      {"12-24*", "20-23*", 1, {{11, 24, FALSE}}},
      {"20-23",  "12-24",  1, {{11, 24, TRUE }}},
      {"20-23*", "12-24",  1, {{11, 24, TRUE }}},
      {"20-23",  "12-24*", 3, {{11, 19, FALSE}, {19, 23, TRUE },
                               {23, 24, FALSE}}},
      {"20-23*", "12-24*", 1, {{11, 24, FALSE}}},

      /* Intersecting ranges with different starting and ending revision
         where neither range is a proper subset of the other. */
      {"50-73",  "60-99",  1, {{49, 99, TRUE }}},
      {"50-73*", "60-99",  2, {{49, 59, FALSE}, {59, 99, TRUE }}},
      {"50-73",  "60-99*", 2, {{49, 73, TRUE }, {73, 99, FALSE}}},
      {"50-73*", "60-99*", 1, {{49, 99, FALSE}}},
      {"60-99",  "50-73",  1, {{49, 99, TRUE }}},
      {"60-99*", "50-73",  2, {{49, 73, TRUE }, {73, 99, FALSE}}},
      {"60-99",  "50-73*", 2, {{49, 59, FALSE}, {59, 99, TRUE }}},
      {"60-99*", "50-73*", 1, {{49, 99, FALSE}}},

      /* Multiple ranges. */
      {"1-5,7,12-13",    "2-17",  1, {{0,  17, TRUE }}},
      {"1-5*,7*,12-13*", "2-17*", 1, {{0,  17, FALSE}}},

      {"1-5,7,12-13",    "2-17*", 6,
       {{0,  5, TRUE }, { 5,  6, FALSE}, { 6,  7, TRUE },
        {7, 11, FALSE}, {11, 13, TRUE }, {13, 17, FALSE}}},

      {"1-5*,7*,12-13*", "2-17", 2,
       {{0, 1, FALSE}, {1, 17, TRUE }}},

      {"2-17",  "1-5,7,12-13",    1, {{0,  17, TRUE }}},
      {"2-17*", "1-5*,7*,12-13*", 1, {{0,  17, FALSE}}},

      {"2-17*", "1-5,7,12-13", 6,
       {{0,  5, TRUE }, { 5,  6, FALSE}, { 6,  7, TRUE },
        {7, 11, FALSE}, {11, 13, TRUE }, {13, 17, FALSE}}},

      {"2-17", "1-5*,7*,12-13*", 2,
       {{0, 1, FALSE}, {1, 17, TRUE}}},

      {"3-4*,10-15,20", "5-60*", 5,
       {{2, 9, FALSE}, {9, 15, TRUE}, {15, 19, FALSE},{19, 20, TRUE},
        {20, 60, FALSE}}},

      {"5-60*", "3-4*,10-15,20", 5,
       {{2, 9, FALSE}, {9, 15, TRUE}, {15, 19, FALSE},{19, 20, TRUE},
        {20, 60, FALSE}}},

      {"3-4*,50-100*", "5-60*", 1, {{2, 100, FALSE}}},

      {"5-60*", "3-4*,50-100*", 1, {{2, 100, FALSE}}},

      {"3-4*,50-100", "5-60*", 2, {{2, 49, FALSE}, {49, 100, TRUE}}},

      {"5-60*", "3-4*,50-100", 2, {{2, 49, FALSE}, {49, 100, TRUE}}},

      {"3-4,50-100*", "5-60", 2, {{2, 60, TRUE}, {60, 100, FALSE}}},

      {"5-60", "3-4,50-100*", 2, {{2, 60, TRUE}, {60, 100, FALSE}}},

      {"5,9,11-15,17,200-300,999", "7-50", 4,
       {{4, 5, TRUE}, {6, 50, TRUE}, {199, 300, TRUE}, {998, 999, TRUE}}},

      /* A rangelist merged with an empty rangelist should equal the
         non-empty rangelist but in compacted form. */
      {"1-44,45,46,47-50",       "",  1, {{ 0, 50, TRUE }}},
      {"1,2,3,4,5,6,7,8",        "",  1, {{ 0, 8,  TRUE }}},
      {"6-10,12-13,14,15,16-22", "",  2,
       {{ 5, 10, TRUE }, { 11, 22, TRUE }}},
      {"", "1-44,45,46,47-50",        1, {{ 0, 50, TRUE }}},
      {"", "1,2,3,4,5,6,7,8",         1, {{ 0, 8,  TRUE }}},
      {"", "6-10,12-13,14,15,16-22",  2,
       {{ 5, 10, TRUE }, { 11, 22, TRUE }}},

      /* An empty rangelist merged with an empty rangelist is, drum-roll
         please, an empty rangelist. */
      {"", "", 0, {{0, 0, FALSE}}}
    };

  err = child_err = SVN_NO_ERROR;
  for (i = 0; i < SIZE_OF_RANGE_MERGE_TEST_ARRAY; i++)
    {
      svn_string_t *rangelist2_starting, *rangelist2_ending;

      SVN_ERR(svn_rangelist__parse(&rangelist1, test_data[i].mergeinfo1, pool));
      SVN_ERR(svn_rangelist__parse(&rangelist2, test_data[i].mergeinfo2, pool));

      /* Create empty rangelists if necessary. */
      if (rangelist1 == NULL)
        rangelist1 = apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
      if (rangelist2 == NULL)
        rangelist2 = apr_array_make(pool, 0, sizeof(svn_merge_range_t *));

      /* Make a copy of rangelist2.  We will merge it into rangelist1, but
         rangelist2 should remain unchanged. */
      SVN_ERR(svn_rangelist_to_string(&rangelist2_starting, rangelist2,
                                      pool));
      SVN_ERR(svn_rangelist_merge(&rangelist1, rangelist2, pool));
      child_err = verify_ranges_match(rangelist1,
                                     (test_data[i]).expected_merge,
                                     (test_data[i]).expected_ranges,
                                     apr_psprintf(pool,
                                                  "svn_rangelist_merge "
                                                  "case %i", i),
                                     "merge", pool);

      /* Collect all the errors rather than returning on the first. */
      if (child_err)
        {
          if (err)
            svn_error_compose(err, child_err);
          else
            err = child_err;
        }

      /* Check that rangelist2 remains unchanged. */
      SVN_ERR(svn_rangelist_to_string(&rangelist2_ending, rangelist2, pool));
      if (strcmp(rangelist2_ending->data, rangelist2_starting->data))
        {
          child_err = fail(pool,
                           apr_psprintf(pool,
                                        "svn_rangelist_merge case %i "
                                        "modified its CHANGES arg from "
                                        "%s to %s", i,
                                        rangelist2_starting->data,
                                        rangelist2_ending->data));
          if (err)
            svn_error_compose(err, child_err);
          else
            err = child_err;
        }
    }
  return err;
}

static svn_error_t *
test_rangelist_diff(apr_pool_t *pool)
{
  int i;
  svn_error_t *err, *child_err;
  svn_rangelist_t *from, *to, *added, *deleted;

  /* Structure containing two ranges to diff and the expected output of the
     diff both when considering and ignoring range inheritance. */
  struct rangelist_diff_test_data
  {
    /* svn:mergeinfo string representations */
    const char *from;
    const char *to;

    /* Expected results for performing svn_rangelist_diff
       while considering differences in inheritability to be real
       differences. */
    int expected_add_ranges;
    svn_merge_range_t expected_adds[10];
    int expected_del_ranges;
    svn_merge_range_t expected_dels[10];

    /* Expected results for performing svn_rangelist_diff
       while ignoring differences in inheritability. */
    int expected_add_ranges_ignore_inheritance;
    svn_merge_range_t expected_adds_ignore_inheritance[10];
    int expected_del_ranges_ignore_inheritance;
    svn_merge_range_t expected_dels_ignore_inheritance[10];
  };

  #define SIZE_OF_RANGE_DIFF_TEST_ARRAY 16
  /* The actual test data array.

                    'from' --> {"1,5-8",  "1,6,10-12", <-- 'to'
      Number of adds when  -->  1, { { 9, 12, TRUE } },
      considering inheritance

      Number of dels when  -->  2, { { 4,  5, TRUE }, { 6, 8, TRUE } },
      considering inheritance

      Number of adds when  -->  1, { { 9, 12, TRUE } },
      ignoring inheritance

      Number of dels when  -->  2, { { 4,  5, TRUE }, { 6, 8, TRUE } } },
      ignoring inheritance
                                            ^               ^
                                    The expected svn_merge_range_t's
  */
  struct rangelist_diff_test_data test_data[SIZE_OF_RANGE_DIFF_TEST_ARRAY] =
    {
      /* Add and Delete */
      {"1",  "3",
       1, { { 2, 3, TRUE } },
       1, { { 0, 1, TRUE } },
       1, { { 2, 3, TRUE } },
       1, { { 0, 1, TRUE } } },

      /* Add only */
      {"1",  "1,3",
       1, { { 2, 3, TRUE } },
       0, { { 0, 0, FALSE } },
       1, { { 2, 3, TRUE } },
       0, { { 0, 0, FALSE } } },

      /* Delete only */
      {"1,3",  "1",
       0, { { 0, 0, FALSE } },
       1, { { 2, 3, TRUE  } },
       0, { { 0, 0, FALSE } },
       1, { { 2, 3, TRUE  } } },

      /* No diff */
      {"1,3",  "1,3",
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } } },

      {"1,3*",  "1,3*",
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } } },

      /* Adds and Deletes */
      {"1,5-8",  "1,6,10-12",
       1, { { 9, 12, TRUE } },
       2, { { 4, 5, TRUE }, { 6, 8, TRUE } },
       1, { { 9, 12, TRUE } },
       2, { { 4, 5, TRUE }, { 6, 8, TRUE } } },

      {"6*",  "6",
       1, { { 5, 6, TRUE  } },
       1, { { 5, 6, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } } },

      /* Intersecting range with different inheritability */
      {"6",  "6*",
       1, { { 5, 6, FALSE } },
       1, { { 5, 6, TRUE  } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } } },

      {"6*",  "6",
       1, { { 5, 6, TRUE  } },
       1, { { 5, 6, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } } },

      {"1,5-8",  "1,6*,10-12",
       2, { { 5,  6, FALSE }, { 9, 12, TRUE } },
       1, { { 4,  8, TRUE  } },
       1, { { 9, 12, TRUE  } },
       2, { { 4,  5, TRUE  }, { 6,  8, TRUE } } },

     {"1,5-8*",  "1,6,10-12",
       2, { { 5,  6, TRUE  }, { 9, 12, TRUE  } },
       1, { { 4,  8, FALSE } },
       1, { { 9, 12, TRUE  } },
       2, { { 4,  5, FALSE }, { 6,  8, FALSE } } },

      /* Empty range diffs */
      {"3-9",  "",
       0, { { 0, 0, FALSE } },
       1, { { 2, 9, TRUE  } },
       0, { { 0, 0, FALSE } },
       1, { { 2, 9, TRUE  } } },

      {"3-9*",  "",
       0, { { 0, 0, FALSE } },
       1, { { 2, 9, FALSE } },
       0, { { 0, 0, FALSE } },
       1, { { 2, 9, FALSE } } },

      {"",  "3-9",
       1, { { 2, 9, TRUE  } },
       0, { { 0, 0, FALSE } },
       1, { { 2, 9, TRUE  } },
       0, { { 0, 0, FALSE } } },

      {"",  "3-9*",
       1, { { 2, 9, FALSE } },
       0, { { 0, 0, FALSE } },
       1, { { 2, 9, FALSE } },
       0, { { 0, 0, FALSE } } },

       /* Empty range no diff */
      {"",  "",
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } },
       0, { { 0, 0, FALSE } } },
    };

  err = child_err = SVN_NO_ERROR;
  for (i = 0; i < SIZE_OF_RANGE_DIFF_TEST_ARRAY; i++)
    {
      SVN_ERR(svn_rangelist__parse(&to, test_data[i].to, pool));
      SVN_ERR(svn_rangelist__parse(&from, test_data[i].from, pool));

      /* Represent empty mergeinfo with an empty rangelist. */
      if (to == NULL)
        to = apr_array_make(pool, 0, sizeof(*to));
      if (from == NULL)
        from = apr_array_make(pool, 0, sizeof(*from));

      /* First diff the ranges while considering
         differences in inheritance. */
      SVN_ERR(svn_rangelist_diff(&deleted, &added, from, to, TRUE, pool));

      child_err = verify_ranges_match(added,
                                     (test_data[i]).expected_adds,
                                     (test_data[i]).expected_add_ranges,
                                     apr_psprintf(pool,
                                                  "svn_rangelist_diff"
                                                  "case %i", i),
                                     "diff", pool);
      if (!child_err)
        child_err = verify_ranges_match(deleted,
                                        (test_data[i]).expected_dels,
                                        (test_data[i]).expected_del_ranges,
                                        apr_psprintf(pool,
                                                     "svn_rangelist_diff"
                                                     "case %i", i),
                                                     "diff", pool);
      if (!child_err)
        {
          /* Now do the diff while ignoring differences in inheritance. */
          SVN_ERR(svn_rangelist_diff(&deleted, &added, from, to, FALSE,
                                     pool));
          child_err = verify_ranges_match(
            added,
            (test_data[i]).expected_adds_ignore_inheritance,
            (test_data[i]).expected_add_ranges_ignore_inheritance,
            apr_psprintf(pool, "svn_rangelist_diff case %i", i),
            "diff", pool);

          if (!child_err)
            child_err = verify_ranges_match(
              deleted,
              (test_data[i]).expected_dels_ignore_inheritance,
              (test_data[i]).expected_del_ranges_ignore_inheritance,
              apr_psprintf(pool, "svn_rangelist_diff case %i", i),
              "diff", pool);
        }

      /* Collect all the errors rather than returning on the first. */
      if (child_err)
        {
          if (err)
            svn_error_compose(err, child_err);
          else
            err = child_err;
        }
    }
  return err;
}


/* Test data structure for test_remove_prefix_from_catalog(). */
struct catalog_bits
{
  const char *orig_path;
  const char *new_path;
  const char *mergeinfo;
};


/* Helper for test_remove_prefix_from_catalog(). */
static svn_error_t *
remove_prefix_helper(struct catalog_bits *test_data,
                     const char *prefix_path,
                     apr_pool_t *pool)
{
  svn_mergeinfo_catalog_t in_catalog, out_catalog, exp_out_catalog;
  apr_hash_index_t *hi;
  int i = 0;

  in_catalog = apr_hash_make(pool);
  exp_out_catalog = apr_hash_make(pool);
  while (test_data[i].orig_path)
    {
      struct catalog_bits data = test_data[i];
      const char *orig_path = apr_pstrdup(pool, data.orig_path);
      const char *new_path = apr_pstrdup(pool, data.new_path);
      svn_mergeinfo_t mergeinfo;
      SVN_ERR(svn_mergeinfo_parse(&mergeinfo, data.mergeinfo, pool));
      apr_hash_set(in_catalog, orig_path, APR_HASH_KEY_STRING, mergeinfo);
      apr_hash_set(exp_out_catalog, new_path, APR_HASH_KEY_STRING, mergeinfo);
      i++;
    }
  SVN_ERR(svn_mergeinfo__remove_prefix_from_catalog(&out_catalog, in_catalog,
                                                    prefix_path, pool));
  if (apr_hash_count(exp_out_catalog) != apr_hash_count(out_catalog))
    return svn_error_create(SVN_ERR_TEST_FAILED, 0,
                            "Got unexpected number of catalog entries");
  for (hi = apr_hash_first(pool, out_catalog); hi; hi = apr_hash_next(hi))
    {
      const void *path;
      apr_ssize_t path_len;
      void *out_mergeinfo, *exp_out_mergeinfo;
      apr_hash_this(hi, &path, &path_len, &out_mergeinfo);
      exp_out_mergeinfo = apr_hash_get(exp_out_catalog, path, path_len);
      if (! exp_out_mergeinfo)
        return svn_error_createf(SVN_ERR_TEST_FAILED, 0,
                                 "Found unexpected key '%s' in catalog",
                                 (const char *)path);
      if (exp_out_mergeinfo != out_mergeinfo)
        return svn_error_create(SVN_ERR_TEST_FAILED, 0,
                                "Detected value tampering in catalog");
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
test_remove_prefix_from_catalog(apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);

  /* For testing the remove of the prefix "/trunk"  */
  struct catalog_bits test_data_1[] =
    {
      { "/trunk",           "",          "/A:1" },
      { "/trunk/foo",       "foo",       "/A/foo:1,3*" },
      { "/trunk/foo/bar",   "foo/bar",   "/A/foo:1-4" },
      { "/trunk/baz",       "baz",       "/A/baz:2" },
      { NULL, NULL, NULL }
    };

  /* For testing the remove of the prefix "/"  */
  struct catalog_bits test_data_2[] =
    {
      { "/",                "",                "/:2" },
      { "/trunk",           "trunk",           "/A:1" },
      { "/trunk/foo",       "trunk/foo",       "/A/foo:1,3*" },
      { "/trunk/foo/bar",   "trunk/foo/bar",   "/A/foo:1-4" },
      { "/trunk/baz",       "trunk/baz",       "/A/baz:2" },
      { NULL, NULL, NULL }
    };

  svn_pool_clear(subpool);
  SVN_ERR(remove_prefix_helper(test_data_1, "/trunk", subpool));

  svn_pool_clear(subpool);
  SVN_ERR(remove_prefix_helper(test_data_2, "/", subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_merge_overlap(apr_pool_t *pool)
{
  const char *rangelist_str = "19473-19612*,19615-19630*,19631-19634";
  const char *changes_str = "15014-20515*";
  const char *expected_str = "15014-19630*,19631-19634,19635-20515*";
  /* wrong result: "15014-19630*,19634-19631*,19631-19634,19635-20515*" */
  svn_rangelist_t *rangelist, *changes;
  svn_string_t *result_string;

  /* prepare the inputs */
  SVN_ERR(svn_rangelist__parse(&rangelist, rangelist_str, pool));
  SVN_ERR(svn_rangelist__parse(&changes, changes_str, pool));
  SVN_TEST_ASSERT(svn_rangelist__is_canonical(rangelist));
  SVN_TEST_ASSERT(svn_rangelist__is_canonical(changes));

  /* perform the merge */
  SVN_ERR(svn_rangelist_merge2(rangelist, changes, pool, pool));

  /* check the output */
  SVN_TEST_ASSERT(svn_rangelist__is_canonical(rangelist));
  SVN_ERR(svn_rangelist_to_string(&result_string, rangelist, pool));
  SVN_TEST_STRING_ASSERT(result_string->data, expected_str);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_rangelist_loop(apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int x, y;

  for (x = 0; x < 62; x++)
    for (y = x + 1; y < 63; y++)
      {
        svn_rangelist_t *base_list;
        svn_rangelist_t *change_list;
        svn_merge_range_t *mrange;
        svn_pool_clear(iterpool);

        SVN_ERR(svn_rangelist__parse(&base_list,
                                     "2,4,7-9,12-15,18-20,"
                                     "22*,25*,28-30*,33-35*,"
                                     "38-40,43-45*,48-50,52-54,56-59*",
                                     iterpool));

        change_list = apr_array_make(iterpool, 1, sizeof(mrange));

        mrange = apr_pcalloc(pool, sizeof(*mrange));
        mrange->start = x;
        mrange->end = y;
        APR_ARRAY_PUSH(change_list, svn_merge_range_t *) = mrange;

        {
          svn_rangelist_t *bl = svn_rangelist_dup(base_list, iterpool);
          svn_rangelist_t *cl = svn_rangelist_dup(change_list, iterpool);

          SVN_TEST_ASSERT(svn_rangelist__is_canonical(bl));
          SVN_TEST_ASSERT(svn_rangelist__is_canonical(cl));

          SVN_ERR(svn_rangelist_merge2(bl, cl, iterpool, iterpool));

          SVN_TEST_ASSERT(svn_rangelist__is_canonical(bl));
          SVN_TEST_ASSERT(svn_rangelist__is_canonical(cl));

          /* TODO: Verify result */
        }

        {
          svn_rangelist_t *bl = svn_rangelist_dup(base_list, iterpool);
          svn_rangelist_t *cl = svn_rangelist_dup(change_list, iterpool);

          SVN_ERR(svn_rangelist_merge2(cl, bl, iterpool, iterpool));

          SVN_TEST_ASSERT(svn_rangelist__is_canonical(bl));
          SVN_TEST_ASSERT(svn_rangelist__is_canonical(cl));

          /* TODO: Verify result */
        }

        mrange->inheritable = TRUE;

        {
          svn_rangelist_t *bl = svn_rangelist_dup(base_list, iterpool);
          svn_rangelist_t *cl = svn_rangelist_dup(change_list, iterpool);

          SVN_TEST_ASSERT(svn_rangelist__is_canonical(bl));
          SVN_TEST_ASSERT(svn_rangelist__is_canonical(cl));

          SVN_ERR(svn_rangelist_merge2(bl, cl, iterpool, iterpool));

          SVN_TEST_ASSERT(svn_rangelist__is_canonical(bl));
          SVN_TEST_ASSERT(svn_rangelist__is_canonical(cl));

          /* TODO: Verify result */
        }

        {
          svn_rangelist_t *bl = svn_rangelist_dup(base_list, iterpool);
          svn_rangelist_t *cl = svn_rangelist_dup(change_list, iterpool);

          SVN_ERR(svn_rangelist_merge2(cl, bl, iterpool, iterpool));

          SVN_TEST_ASSERT(svn_rangelist__is_canonical(bl));
          SVN_TEST_ASSERT(svn_rangelist__is_canonical(cl));

          /* TODO: Verify result */
        }
      }

  return SVN_NO_ERROR;
}

/* A specific case where result was non-canonical, around svn 1.10 ~ 1.13. */
static svn_error_t *
test_rangelist_merge_canonical_result(apr_pool_t *pool)
{
  const char *rangelist_str = "8-10";
  const char *changes_str = "5-10*,11-24";
  const char *expected_str = "5-7*,8-24";
  /* wrong result: "5-7*,8-10,11-24" */
  svn_rangelist_t *rangelist, *changes;
  svn_string_t *result_string;

  /* prepare the inputs */
  SVN_ERR(svn_rangelist__parse(&rangelist, rangelist_str, pool));
  SVN_ERR(svn_rangelist__parse(&changes, changes_str, pool));
  SVN_TEST_ASSERT(svn_rangelist__is_canonical(rangelist));
  SVN_TEST_ASSERT(svn_rangelist__is_canonical(changes));

  /* perform the merge */
  SVN_ERR(svn_rangelist_merge2(rangelist, changes, pool, pool));

  /* check the output */
  SVN_TEST_ASSERT(svn_rangelist__is_canonical(rangelist));
  SVN_ERR(svn_rangelist_to_string(&result_string, rangelist, pool));
  SVN_TEST_STRING_ASSERT(result_string->data, expected_str);

  return SVN_NO_ERROR;
}

/* Test svn_rangelist_merge2() with specific inputs derived from an
 * occurrence of issue #4840 "in the wild", that triggered a hard
 * assertion failure (abort) in a 1.10.6 release-mode build.
 */
static svn_error_t *
test_rangelist_merge_array_insert_failure(apr_pool_t *pool)
{
  svn_rangelist_t *rx, *ry;
  svn_string_t *rxs;

  /* Simplified case with same failure mode as reported case: error
   * "E200004: svn_sort__array_insert2:
   *  Attempted insert at index 4 in array length 3" */
  SVN_ERR(svn_rangelist__parse(&rx, "2*,4*,6*,8", pool));
  SVN_ERR(svn_rangelist__parse(&ry, "1-9*,11", pool));
  SVN_ERR(svn_rangelist_merge2(rx, ry, pool, pool));
  SVN_ERR(svn_rangelist_to_string(&rxs, rx, pool));
  SVN_TEST_STRING_ASSERT(rxs->data, "1-7*,8,9*,11");

  /* Actual reported case: in v1.10.6, aborted; after r1872118, error
   * "E200004: svn_sort__array_insert2:
   *  Attempted insert at index 57 in array length 55".  The actual "index"
   *  and "array length" numbers vary with changes such as r1823728. */
  SVN_ERR(svn_rangelist__parse(&rx, "997347-997597*,997884-1000223*,1000542-1000551*,1001389-1001516,1002139-1002268*,1002896-1003064*,1003320-1003468,1005939-1006089*,1006443-1006630*,1006631-1006857,1007028-1007116*,1009467-1009629,1009630-1010007*,1010774-1010860,1011036-1011502,1011672-1014004*,1014023-1014197,1014484-1014542*,1015077-1015568,1016219-1016365,1016698-1016845,1017331-1018616,1027032-1027180,1027855-1028051,1028261-1028395,1028553-1028663,1028674-1028708,1028773-1028891*,1029223-1030557,1032239-1032284*,1032801-1032959,1032960-1033074*,1033745-1033810,1034990-1035104,1035435-1036108*,1036109-1036395,1036396-1036865*,1036866-1036951,1036952-1037647*,1037648-1037750,1037751-1038548*,1038549-1038700,1038701-1042103*,1042104-1042305,1042306-1046626*,1046627-1046910,1046911-1047676*,1047677-1047818,1047819-1047914*,1047915-1048025,1048026-1048616*,1048617-1048993,1048994-1050066*,1054605-1054739,1054854-1055021", pool));
  SVN_ERR(svn_rangelist__parse(&ry, "1035435-1050066*,1052459-1054617", pool));
  SVN_ERR(svn_rangelist_merge2(rx, ry, pool, pool));
  /* Here we don't care to check the result; just that it returns "success". */
  return SVN_NO_ERROR;
}


/* Random testing parameters and coverage
 *
 * The parameters for testing random inputs, in conjunction with the
 * specific test case generation code, were adjusted so as to observe the
 * tests generating each of the known failure modes.  The aim is also to
 * have sufficient coverage of inputs to discover other failure modes in
 * future if the code is changed.
 *
 * There are neither theoretic nor empirical guarantees on the coverage.
 */

/* Randomize revision numbers over this small range.
 * (With a larger range, we would find edge cases more rarely.)
 * See comment "Random testing parameters and coverage" */
#define RANGELIST_TESTS_MAX_REV 15

/* A representation of svn_rangelist_t in which
 *   root[R]    := (revision R is in the rangelist)
 *   inherit[R] := (revision R is in the rangelist and inheritable)
 *
 * Assuming all forward ranges.
 */
typedef struct rl_array_t {
    svn_boolean_t root[RANGELIST_TESTS_MAX_REV + 1];
    svn_boolean_t inherit[RANGELIST_TESTS_MAX_REV + 1];
} rl_array_t;

static void
rangelist_to_array(rl_array_t *a,
                   const svn_rangelist_t *rl)
{
  int i;

  memset(a, 0, sizeof(*a));
  for (i = 0; i < rl->nelts; i++)
    {
      svn_merge_range_t *range = APR_ARRAY_IDX(rl, i, svn_merge_range_t *);
      svn_revnum_t r;

      for (r = range->start + 1; r <= range->end; r++)
        {
          a->root[r] = TRUE;
          a->inherit[r] = range->inheritable;
        }
    }
}

/* Compute the union of two rangelists arrays.
 * Let MA := union(BA, CA)
 */
static void
rangelist_array_union(rl_array_t *ma,
                      const rl_array_t *ba,
                      const rl_array_t *ca)
{
  svn_revnum_t r;

  for (r = 0; r <= RANGELIST_TESTS_MAX_REV; r++)
    {
      ma->root[r]    = ba->root[r]    || ca->root[r];
      ma->inherit[r] = ba->inherit[r] || ca->inherit[r];
    }
}

/* Return TRUE iff two rangelist arrays are equal.
 */
static svn_boolean_t
rangelist_array_equal(const rl_array_t *ba,
                      const rl_array_t *ca)
{
  svn_revnum_t r;

  for (r = 0; r <= RANGELIST_TESTS_MAX_REV; r++)
    {
      if (ba->root[r]    != ca->root[r]
       || ba->inherit[r] != ca->inherit[r])
        {
          return FALSE;
        }
    }
  return TRUE;
}

#define IS_VALID_FORWARD_RANGE(range) \
  (SVN_IS_VALID_REVNUM((range)->start) && ((range)->start < (range)->end))

/* Check rangelist is sorted and contains forward ranges. */
static svn_boolean_t
rangelist_is_sorted(const svn_rangelist_t *rangelist)
{
  int i;

  for (i = 0; i < rangelist->nelts; i++)
    {
      const svn_merge_range_t *thisrange
        = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);

      if (!IS_VALID_FORWARD_RANGE(thisrange))
        return FALSE;
    }
  for (i = 1; i < rangelist->nelts; i++)
    {
      const svn_merge_range_t *lastrange
        = APR_ARRAY_IDX(rangelist, i-1, svn_merge_range_t *);
      const svn_merge_range_t *thisrange
        = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);

      if (svn_sort_compare_ranges(&lastrange, &thisrange) > 0)
        return FALSE;
    }
  return TRUE;
}

/* Return a random number R, where 0 <= R < N.
 */
static int rand_less_than(int n, apr_uint32_t *seed)
{
  apr_uint32_t next = svn_test_rand(seed);
  return ((apr_uint64_t)next * n) >> 32;
}

/* Return a random integer in a triangular (centre-weighted) distribution in
 * the inclusive interval [MIN, MAX]. */
static int
rand_interval_triangular(int min, int max, apr_uint32_t *seed)
{
  int span = max - min + 1;

  return min + rand_less_than(span/2 + 1, seed)
             + rand_less_than((span+1)/2, seed);
}

/* Generate a rangelist with a random number of random ranges.
 * Choose from 0 to NON_V_MAX_RANGES ranges, biased towards the middle.
 */
#define NON_V_MAX_RANGES 4  /* See "Random testing parameters and coverage" */
static void
rangelist_random_non_validated(svn_rangelist_t **rl,
                               apr_uint32_t *seed,
                               apr_pool_t *pool)
{
  svn_rangelist_t *r = apr_array_make(pool, NON_V_MAX_RANGES,
                                      sizeof(svn_merge_range_t *));
  int n_ranges = rand_interval_triangular(0, NON_V_MAX_RANGES, seed);
  int i;

  for (i = 0; i < n_ranges; i++)
    {
      svn_merge_range_t *mrange = apr_pcalloc(pool, sizeof(*mrange));

      mrange->start = rand_less_than(RANGELIST_TESTS_MAX_REV + 1, seed);
      mrange->end = rand_less_than(RANGELIST_TESTS_MAX_REV + 1, seed);
      mrange->inheritable = rand_less_than(2, seed);
      APR_ARRAY_PUSH(r, svn_merge_range_t *) = mrange;
    }
  *rl = r;
}

/* Compare two integers pointed to by A_P and B_P, for use with qsort(). */
static int
int_compare(const void *a_p, const void *b_p)
{
  const int *a = a_p, *b = b_p;

  return (*a < *b) ? -1 : (*a > *b) ? 1 : 0;
}

/* Fill an ARRAY[ARRAY_LENGTH] with values each in the inclusive range
 * [0, MAX].  The values are in ascending order, possibly with the same
 * value repeated any number of times.
 */
static void
ascending_values(int *array,
                 int array_length,
                 int max,
                 apr_uint32_t *seed)
{
  int i;

  for (i = 0; i < array_length; i++)
    array[i] = rand_less_than(max + 1, seed);
  /* Sort them. (Some values will be repeated.) */
  qsort(array, array_length, sizeof(*array), int_compare);
}

/* Generate a random rangelist that is not necessarily canonical
 * but is at least sorted according to svn_sort_compare_ranges()
 * and on which svn_rangelist__canonicalize() would succeed.
 * Choose from 0 to SEMI_C_MAX_RANGES ranges, biased towards the middle.
 */
#define SEMI_C_MAX_RANGES 8
static void
rangelist_random_semi_canonical(svn_rangelist_t **rl,
                                apr_uint32_t *seed,
                                apr_pool_t *pool)
{
  svn_rangelist_t *r = apr_array_make(pool, 4, sizeof(svn_merge_range_t *));
  int n_ranges = rand_interval_triangular(0, SEMI_C_MAX_RANGES, seed);
  int start_and_end_revs[SEMI_C_MAX_RANGES * 2];
  int i;

  /* Choose start and end revs of the ranges. To end up with ranges evenly
   * distributed up to RANGELIST_TESTS_MAX_REV, we start with them evenly
   * distributed up to RANGELIST_TESTS_MAX_REV - N_RANGES, before spreading. */
  ascending_values(start_and_end_revs, n_ranges * 2,
                   RANGELIST_TESTS_MAX_REV - n_ranges,
                   seed);
  /* Some values will be repeated. Within one range, that is not allowed,
   * so add 1 to the length of each range, spreading the ranges out so they
   * still don't overlap:
   * [(6,6), (6,8)] becomes [(6,7), (7, 10)] */
  for (i = 0; i < n_ranges; i++)
    {
      start_and_end_revs[i*2] += i;
      start_and_end_revs[i*2 + 1] += i+1;
    }

  for (i = 0; i < n_ranges; i++)
    {
      svn_merge_range_t *mrange = apr_pcalloc(pool, sizeof(*mrange));

      mrange->start = start_and_end_revs[i * 2];
      mrange->end = start_and_end_revs[i * 2 + 1];
      mrange->inheritable = rand_less_than(2, seed);
      APR_ARRAY_PUSH(r, svn_merge_range_t *) = mrange;
    }
  *rl = r;

  /* check postconditions */
  {
    svn_rangelist_t *dup;
    svn_error_t *err;

    SVN_ERR_ASSERT_NO_RETURN(rangelist_is_sorted(*rl));
    dup = svn_rangelist_dup(*rl, pool);
    err = svn_rangelist__canonicalize(dup, pool);
    SVN_ERR_ASSERT_NO_RETURN(!err);
  }
}

/* Generate a random rangelist that satisfies svn_rangelist__is_canonical().
 */
static void
rangelist_random_canonical(svn_rangelist_t **rl,
                           apr_uint32_t *seed,
                           apr_pool_t *pool)
{
  svn_rangelist_t *r;
  int i;

  rangelist_random_semi_canonical(&r, seed, pool);
  for (i = 1; i < r->nelts; i++)
    {
      svn_merge_range_t *prev_mrange = APR_ARRAY_IDX(r, i-1, svn_merge_range_t *);
      svn_merge_range_t *mrange = APR_ARRAY_IDX(r, i, svn_merge_range_t *);

      /* to be canonical: adjacent ranges need differing inheritability */
      if (mrange->start == prev_mrange->end)
        {
          mrange->inheritable = !prev_mrange->inheritable;
        }
    }
  *rl = r;

  /* check postconditions */
  SVN_ERR_ASSERT_NO_RETURN(svn_rangelist__is_canonical(*rl));
}

static const char *
rangelist_to_string(const svn_rangelist_t *rl,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  svn_string_t *ss;

  err = svn_rangelist_to_string(&ss, rl, pool);
  if (err)
    {
      const char *s
        = apr_psprintf(pool, "<rangelist[%d ranges]: %s>",
                       rl->nelts, svn_error_purge_tracing(err)->message);
      svn_error_clear(err);
      return s;
    }
  return ss->data;
}

/* Try svn_rangelist_merge2(rlx, rly) and check errors and result */
static svn_error_t *
rangelist_merge_random_inputs(svn_rangelist_t *rlx,
                              svn_rangelist_t *rly,
                              apr_pool_t *pool)
{
  rl_array_t ax, ay, a_expected, a_actual;
  svn_rangelist_t *rlm;

  rangelist_to_array(&ax, rlx);
  rangelist_to_array(&ay, rly);

  rlm = svn_rangelist_dup(rlx, pool);
  /*printf("testcase: %s / %s\n", rangelist_to_string(rlx, pool), rangelist_to_string(rly, pool));*/
  SVN_ERR(svn_rangelist_merge2(rlm, rly, pool, pool));

  if (!svn_rangelist__is_canonical(rlm))
    {
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "non-canonical result %s",
                               rangelist_to_string(rlm, pool));
    }

  /*SVN_TEST_ASSERT(rangelist_equal(rlm, ...));*/
  rangelist_array_union(&a_expected, &ax, &ay);
  rangelist_to_array(&a_actual, rlm);
  if (!rangelist_array_equal(&a_actual, &a_expected))
    {
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "wrong result: (c? %d / %d) -> %s",
                               svn_rangelist__is_canonical(rlx),
                               svn_rangelist__is_canonical(rly),
                               rangelist_to_string(rlm, pool));
    }

  return SVN_NO_ERROR;
}

/* Insert a failure mode (ERR_CHAIN) into RESPONSES, keyed by a message
 * representing its failure mode.  The failure mode message is the lowest
 * level error message in ERR_CHAIN, with some case-specific details
 * removed to aid de-duplication.  If it is new failure mode (not already in
 * RESPONSES), store the error and return the message (hash key), else
 * clear the error and return NULL.
 */
static const char *
add_failure_mode(svn_error_t *err_chain,
                 apr_hash_t *failure_modes)
{
  svn_error_t *err = err_chain;
  char buf[100];
  const char *message;

  if (!err_chain)
    return NULL;

  while (err->child)
    err = err->child;
  message = svn_err_best_message(err, buf, sizeof(buf));

  /* For deduplication, ignore case-specific data in certain messages. */
  if (strstr(message, "Unable to parse overlapping revision ranges '"))
            message = "Unable to parse overlapping revision ranges '...";
  if (strstr(message, "wrong result: (c?"))
            message = "wrong result: (c?...";
  if (strstr(message, "svn_sort__array_insert2: Attempted insert at index "))
            message = "svn_sort__array_insert2: Attempted insert at index ...";

  if (!svn_hash_gets(failure_modes, message))
    {
      svn_hash_sets(failure_modes, message, err);
      return message;
    }
  else
    {
      svn_error_clear(err_chain);
      return NULL;
    }
}

static void
clear_failure_mode_errors(apr_hash_t *failure_modes, apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, failure_modes);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_error_t *err = apr_hash_this_val(hi);
      svn_error_clear(err);
    }
}

static svn_error_t *
test_rangelist_merge_random_canonical_inputs(apr_pool_t *pool)
{
  static apr_uint32_t seed = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_t *failure_modes = apr_hash_make(pool);
  svn_boolean_t pass = TRUE;
  int ix, iy;

  /* "300": See comment "Random testing parameters and coverage" */
  for (ix = 0; ix < 300; ix++)
   {
    svn_rangelist_t *rlx;

    rangelist_random_canonical(&rlx, &seed, pool);

    for (iy = 0; iy < 300; iy++)
      {
        svn_rangelist_t *rly;
        svn_error_t *err;
        const char *failure_mode;

        svn_pool_clear(iterpool);

        rangelist_random_canonical(&rly, &seed, iterpool);

        err = svn_error_trace(rangelist_merge_random_inputs(rlx, rly, iterpool));
        failure_mode = add_failure_mode(err, failure_modes);
        if (failure_mode)
          {
            printf("first example of a failure mode: %s / %s\n"
                   "  %s\n",
                   rangelist_to_string(rlx, iterpool),
                   rangelist_to_string(rly, iterpool),
                   failure_mode);
            /*svn_handle_error(err, stdout, FALSE);*/
            pass = FALSE;
          }
      }
   }

  clear_failure_mode_errors(failure_modes, pool);

  if (!pass)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Test failed: %d failure modes",
                             apr_hash_count(failure_modes));
  return SVN_NO_ERROR;
}

/* Test svn_rangelist_merge2() with inputs that confirm to its doc-string.
 * Fail if any errors are produced.
 */
static svn_error_t *
test_rangelist_merge_random_semi_c_inputs(apr_pool_t *pool)
{
  static apr_uint32_t seed = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_t *failure_modes = apr_hash_make(pool);
  svn_boolean_t pass = TRUE;
  int ix, iy;

  /* "300": See comment "Random testing parameters and coverage" */
  for (ix = 0; ix < 300; ix++)
   {
    svn_rangelist_t *rlx;

    rangelist_random_semi_canonical(&rlx, &seed, pool);

    for (iy = 0; iy < 300; iy++)
      {
        svn_rangelist_t *rly;
        svn_error_t *err;
        const char *failure_mode;

        svn_pool_clear(iterpool);

        rangelist_random_semi_canonical(&rly, &seed, iterpool);

        err = svn_error_trace(rangelist_merge_random_inputs(rlx, rly, iterpool));
        failure_mode = add_failure_mode(err, failure_modes);
        if (failure_mode)
          {
            printf("first example of a failure mode: %s / %s\n"
                   "  %s\n",
                   rangelist_to_string(rlx, iterpool),
                   rangelist_to_string(rly, iterpool),
                   failure_mode);
            /*svn_handle_error(err, stdout, FALSE);*/
            pass = FALSE;
          }
      }
   }

  clear_failure_mode_errors(failure_modes, pool);

  if (!pass)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Test failed: %d failure modes",
                             apr_hash_count(failure_modes));
  return SVN_NO_ERROR;
}

/* Test svn_rangelist_merge2() with random non-validated inputs.
 *
 * Unlike the tests with valid inputs, this test expects many assertion
 * failures.  We don't care about those.  All we care about is that it does
 * not crash. */
static svn_error_t *
test_rangelist_merge_random_non_validated_inputs(apr_pool_t *pool)
{
  static apr_uint32_t seed = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_t *failure_modes = apr_hash_make(pool);
  int ix, iy;

  /* "300": See comment "Random testing parameters and coverage" */
  for (ix = 0; ix < 300; ix++)
   {
    svn_rangelist_t *rlx;

    rangelist_random_non_validated(&rlx, &seed, pool);

    for (iy = 0; iy < 300; iy++)
      {
        svn_rangelist_t *rly;
        svn_error_t *err;

        svn_pool_clear(iterpool);

        rangelist_random_non_validated(&rly, &seed, iterpool);

        err = svn_error_trace(rangelist_merge_random_inputs(rlx, rly, iterpool));
        add_failure_mode(err, failure_modes);
      }
   }

  clear_failure_mode_errors(failure_modes, pool);

  return SVN_NO_ERROR;
}

/* Generate random mergeinfo, in which the paths and rangelists are not
 * necessarily valid. */
static svn_error_t *
mergeinfo_random_non_validated(svn_mergeinfo_t *mp,
                               apr_uint32_t *seed,
                               apr_pool_t *pool)
{
  svn_mergeinfo_t m = apr_hash_make(pool);
  int n_paths = 3;  /* See comment "Random testing parameters and coverage" */
  int i;

  for (i = 0; i < n_paths; i++)
    {
      const char *path;
      svn_rangelist_t *rl;

      /* A manually chosen distribution of valid and invalid paths:
         See comment "Random testing parameters and coverage" */
      switch (rand_less_than(8, seed))
        {
        case 0: case 1: case 2: case 3:
          path = apr_psprintf(pool, "/path%d", i); break;
        case 4:
          path = apr_psprintf(pool, "path%d", i); break;
        case 5:
          path = apr_psprintf(pool, "//path%d", i); break;
        case 6:
          path = "/"; break;
        case 7:
          path = ""; break;
        }
      rangelist_random_non_validated(&rl, seed, pool);
      svn_hash_sets(m, path, rl);
    }
  *mp = m;
  return SVN_NO_ERROR;
}

#if 0
static const char *
mergeinfo_to_string_debug(svn_mergeinfo_t m,
                          apr_pool_t *pool)
{
  svn_string_t *s;
  svn_error_t *err;

  err = svn_mergeinfo_to_string(&s, m, pool);
  if (err)
    {
      const char *s2 = err->message;
      svn_error_clear(err);
      return s2;
    }
  return s->data;
}
#endif

/* Try a mergeinfo merge.  This does not check the result. */
static svn_error_t *
mergeinfo_merge_random_inputs(const svn_mergeinfo_t mx,
                              const svn_mergeinfo_t my,
                              apr_pool_t *pool)
{
  svn_mergeinfo_t mm = svn_mergeinfo_dup(mx, pool);

  SVN_ERR(svn_mergeinfo_merge2(mm, my, pool, pool));
  return SVN_NO_ERROR;
}

/* Test svn_mergeinfo_merge2() with random non-validated inputs.
 *
 * Unlike the tests with valid inputs, this test expects many assertion
 * failures.  We don't care about those.  All we care about is that it does
 * not crash. */
static svn_error_t *
test_mergeinfo_merge_random_non_validated_inputs(apr_pool_t *pool)
{
  static apr_uint32_t seed = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);
  int ix, iy;

  for (ix = 0; ix < 300; ix++)
   {
    svn_mergeinfo_t mx;

    SVN_ERR(mergeinfo_random_non_validated(&mx, &seed, pool));

    for (iy = 0; iy < 300; iy++)
      {
        svn_mergeinfo_t my;
        svn_error_t *err;

        svn_pool_clear(iterpool);

        SVN_ERR(mergeinfo_random_non_validated(&my, &seed, iterpool));

        err = mergeinfo_merge_random_inputs(mx, my, iterpool);
        if (err)
          {
            /*
            printf("testcase FAIL: %s / %s\n",
                   mergeinfo_to_string_debug(mx, iterpool),
                   mergeinfo_to_string_debug(my, iterpool));
            svn_handle_error(err, stdout, FALSE);
            */
            svn_error_clear(err);
          }
      }
   }

  return SVN_NO_ERROR;
}

/* The test table.  */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_parse_single_line_mergeinfo,
                   "parse single line mergeinfo"),
    SVN_TEST_PASS2(test_mergeinfo_dup,
                   "copy a mergeinfo data structure"),
    SVN_TEST_PASS2(test_parse_combine_rangeinfo,
                   "parse single line mergeinfo and combine ranges"),
    SVN_TEST_PASS2(test_parse_broken_mergeinfo,
                   "parse broken single line mergeinfo"),
    SVN_TEST_PASS2(test_remove_rangelist,
                   "remove rangelists"),
    SVN_TEST_PASS2(test_rangelist_remove_randomly,
                   "test rangelist remove with random data"),
    SVN_TEST_PASS2(test_remove_mergeinfo,
                   "remove of mergeinfo"),
    SVN_TEST_PASS2(test_rangelist_reverse,
                   "reversal of rangelist"),
    SVN_TEST_PASS2(test_rangelist_intersect,
                   "intersection of rangelists"),
    SVN_TEST_PASS2(test_rangelist_intersect_randomly,
                   "test rangelist intersect with random data"),
    SVN_TEST_PASS2(test_diff_mergeinfo,
                   "diff of mergeinfo"),
    SVN_TEST_PASS2(test_merge_mergeinfo,
                   "merging of mergeinfo hashes"),
    SVN_TEST_PASS2(test_mergeinfo_intersect,
                   "intersection of mergeinfo"),
    SVN_TEST_PASS2(test_rangelist_to_string,
                   "turning rangelist back into a string"),
    SVN_TEST_PASS2(test_mergeinfo_to_string,
                   "turning mergeinfo back into a string"),
    SVN_TEST_PASS2(test_rangelist_merge,
                   "merge of rangelists"),
    SVN_TEST_PASS2(test_rangelist_diff,
                   "diff of rangelists"),
    SVN_TEST_PASS2(test_remove_prefix_from_catalog,
                   "removal of prefix paths from catalog keys"),
    SVN_TEST_PASS2(test_rangelist_merge_overlap,
                   "merge of rangelists with overlaps (issue 4686)"),
    SVN_TEST_PASS2(test_rangelist_loop,
                    "test rangelist edgecases via loop"),
    SVN_TEST_PASS2(test_rangelist_merge_canonical_result,
                   "test rangelist merge canonical result (#4840)"),
    SVN_TEST_PASS2(test_rangelist_merge_array_insert_failure,
                   "test rangelist merge array insert failure (#4840)"),
    SVN_TEST_PASS2(test_rangelist_merge_random_canonical_inputs,
                   "test rangelist merge random canonical inputs"),
    SVN_TEST_PASS2(test_rangelist_merge_random_semi_c_inputs,
                   "test rangelist merge random semi-c inputs"),
    SVN_TEST_PASS2(test_rangelist_merge_random_non_validated_inputs,
                   "test rangelist merge random non-validated inputs"),
    SVN_TEST_PASS2(test_mergeinfo_merge_random_non_validated_inputs,
                   "test mergeinfo merge random non-validated inputs"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
