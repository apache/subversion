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

static svn_error_t *
test_parse_mergeinfo(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  int i;
#define NBR_MERGEINFO_LINES 2
#define BOGUS_MERGEINFO_LINE "/missing-revs"

  static const char * const mergeinfo_lines[NBR_MERGEINFO_LINES] =
    {
      "/trunk:1",
      "/trunk/foo:1-6"
    };
  static const char * const mergeinfo_paths[NBR_MERGEINFO_LINES] =
    {
      "/trunk",
      "/trunk/foo"
    };
  static svn_merge_range_t mergeinfo_ranges[NBR_MERGEINFO_LINES] =
    {
      { 1, 1 },
      { 1, 6 }
    };
  
  *msg = "test svn_parse_mergeinfo";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < NBR_MERGEINFO_LINES; i++)
    {
      svn_error_t *err;
      apr_hash_t *path_to_merge_ranges;
      apr_hash_index_t *hi;

      /* Trigger some error(s) with mal-formed input. */
      err = svn_parse_mergeinfo(BOGUS_MERGEINFO_LINE, &path_to_merge_ranges,
                                pool);
      if (!err)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "svn_parse_mergeinfo (%s) succeeded "
                                 "unexpectedly", BOGUS_MERGEINFO_LINE);

      /* Test valid input. */
      err = svn_parse_mergeinfo(mergeinfo_lines[i], &path_to_merge_ranges,
                                pool);
      if (err || apr_hash_count(path_to_merge_ranges) != 1)
        return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                                 "svn_parse_mergeinfo (%s) failed "
                                 "unexpectedly", mergeinfo_lines[i]);
      for (hi = apr_hash_first(pool, path_to_merge_ranges); hi;
           hi = apr_hash_next(hi))
        {
          const void *path;
          void *val;
          apr_array_header_t *ranges;

          apr_hash_this(hi, &path, NULL, &val);
          if (strcmp((const char *) path, mergeinfo_paths[i]) != 0)
            return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                                     "svn_parse_mergeinfo (%s) failed to "
                                     "parse the correct path (%s)",
                                     mergeinfo_lines[i], mergeinfo_paths[i]);

          /* Test ranges.  For now, assume only 1 range. */
          ranges = (apr_array_header_t *) val;
          if (APR_ARRAY_IDX(ranges, 0, svn_merge_range_t *)->start
              != mergeinfo_ranges[i].start ||
              APR_ARRAY_IDX(ranges, 0, svn_merge_range_t *)->end
              != mergeinfo_ranges[i].end)
            return svn_error_createf(SVN_ERR_TEST_FAILED, err,
                                     "svn_parse_mergeinfo (%s) failed to "
                                     "parse the correct range",
                                     mergeinfo_lines[i]);
        }
    }
#undef BOGUS_MERGEINFO_LINE
#undef NBR_MERGEINFO_LINES
  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_parse_mergeinfo),
    SVN_TEST_NULL
  };
