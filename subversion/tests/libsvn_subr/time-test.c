/*
 * time-test.c -- test the time functions
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <apr_general.h>
#include "svn_time.h"
#include "svn_test.h"

/* All these variables should refer to the same point in time. */
apr_time_t test_timestamp = APR_TIME_C(1021316450966679);
const char *test_timestring =
"2002-05-13T19:00:50.966679Z";
const char *test_old_timestring = 
"Mon 13 May 2002 22:00:50.966679 (day 133, dst 1, gmt_off 010800)";


static svn_error_t *
test_time_to_nts (const char **msg,
                  svn_boolean_t msg_only,
                  apr_pool_t *pool)
{
  const char *timestring;

  *msg = "test svn_time_to_nts";

  if (msg_only)
    return SVN_NO_ERROR;

  timestring = svn_time_to_nts(test_timestamp,pool);

  if (strcmp(timestring,test_timestring) != 0)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, 0, NULL, pool,
         "svn_time_to_nts (%" APR_TIME_T_FMT
         ") returned date string '%s' instead of '%s'",
         test_timestamp,timestring,test_timestring);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_time_from_nts (const char **msg,
                    svn_boolean_t msg_only,
                    apr_pool_t *pool)
{
  apr_time_t timestamp;

  *msg = "test svn_time_from_nts";

  if (msg_only)
    return SVN_NO_ERROR;

  timestamp = svn_time_from_nts(test_timestring);

  if (timestamp != test_timestamp)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, 0, NULL, pool,
         "svn_time_from_nts (%s) returned time '%" APR_TIME_T_FMT
         "' instead of '%" APR_TIME_T_FMT "'",
         test_timestring,timestamp,test_timestamp);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_time_from_nts_old (const char **msg,
                    svn_boolean_t msg_only,
                    apr_pool_t *pool)
{
  apr_time_t timestamp;

  *msg = "test svn_time_from_nts (old format)";

  if (msg_only)
    return SVN_NO_ERROR;

  timestamp = svn_time_from_nts(test_old_timestring);

  if (timestamp != test_timestamp)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, 0, NULL, pool,
         "svn_time_from_nts (%s) returned time '%" APR_TIME_T_FMT
         "' instead of '%" APR_TIME_T_FMT "'",
         test_old_timestring,timestamp,test_timestamp);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_time_invariant (const char **msg,
                     svn_boolean_t msg_only,
                     apr_pool_t *pool)
{
  apr_time_t current_timestamp = apr_time_now();
  const char *timestring;
  apr_time_t timestamp;

  *msg = "test svn_time_to_nts and svn_time_from_nts invariant";

  if (msg_only)
    return SVN_NO_ERROR;

  timestring = svn_time_to_nts(current_timestamp,pool);
  timestamp = svn_time_from_nts(timestring);

  if (timestamp != current_timestamp)
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, 0, NULL, pool,
         "svn_time_from_nts ( svn_time_to_nts (n) ) returned time '%" APR_TIME_T_FMT
         "' instead of '%" APR_TIME_T_FMT "'",
         timestamp,current_timestamp);
    }

  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               svn_boolean_t msg_only,
                               apr_pool_t *pool) = {
  0,
  /* XXX: enable after new timestamps are in use */
  /* test_time_to_nts, */
  test_time_from_nts,
  test_time_from_nts_old,
  test_time_invariant,
  0
};



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */

