/*
 * error-test.c -- test the error functions
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
#include <apr_general.h>

#include "svn_error_codes.h"
#include "svn_error.h"

#include "../svn_test.h"

static svn_error_t *
test_error_root_cause(apr_pool_t *pool)
{
  apr_status_t secondary_err_codes[] = { SVN_ERR_STREAM_UNRECOGNIZED_DATA,
                                         SVN_ERR_STREAM_MALFORMED_DATA };
  apr_status_t root_cause_err_code = SVN_ERR_STREAM_UNEXPECTED_EOF;
  int i;
  svn_error_t *err, *root_err;

  /* Nest several errors. */
  err = svn_error_create(root_cause_err_code, NULL, "root cause");
  for (i = 0; i < 2; i++)
    err = svn_error_create(secondary_err_codes[i], err, NULL);

  /* Verify that the error is detected at the proper location in the
     error chain. */
  root_err = svn_error_root_cause(err);
  if (root_err == NULL)
    {
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "svn_error_root_cause failed to locate any "
                               "root error in the chain");
    }

  for (i = 0; i < 2; i++)
    {
      if (root_err->apr_err == secondary_err_codes[i])
        {
          svn_error_clear(err);
          return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "svn_error_root_cause returned the "
                                   "wrong error from the chain");
        }
    }

  if (root_err->apr_err != root_cause_err_code)
    {
      svn_error_clear(err);
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "svn_error_root_cause failed to locate the "
                               "correct error from teh chain");
    }

  svn_error_clear(err);
  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_error_root_cause,
                   "test svn_error_root_cause"),
    SVN_TEST_NULL
  };
