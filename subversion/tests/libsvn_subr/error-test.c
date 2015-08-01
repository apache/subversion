/*
 * error-test.c -- test the error functions
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
#include <apr_general.h>

#include "svn_error_codes.h"
#include "svn_error.h"
#include "private/svn_error_private.h"

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
                               "correct error from the chain");
    }

  svn_error_clear(err);
  return SVN_NO_ERROR;
}

static svn_error_t *
test_error_purge_tracing(apr_pool_t *pool)
{
  svn_error_t *err, *err2, *child;

  if (SVN_NO_ERROR != svn_error_purge_tracing(SVN_NO_ERROR))
    return svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                            "svn_error_purge_tracing() didn't return "
                            "SVN_NO_ERROR after being passed a "
                            "SVN_NO_ERROR.");

  err = svn_error_trace(svn_error_create(SVN_ERR_BASE, NULL, "root error"));
#ifdef SVN_ERR__TRACING
  if (! svn_error__is_tracing_link(err))
    {
      return svn_error_create(SVN_ERR_TEST_FAILED, err,
                              "The top error is not a tracing link:");
    }
#endif
  err = svn_error_trace(svn_error_create(SVN_ERR_BASE, err, "other error"));
#ifdef SVN_ERR__TRACING
  if (! svn_error__is_tracing_link(err))
    {
      return svn_error_create(SVN_ERR_TEST_FAILED, err,
                              "The top error is not a tracing link:");
    }
#endif

  err2 = svn_error_purge_tracing(err);
  for (child = err2; child; child = child->child)
    if (svn_error__is_tracing_link(child))
      {
        return svn_error_create(SVN_ERR_TEST_FAILED, err,
                                "Tracing link found after purging the "
                                "following chain:");
      }
  svn_error_clear(err);

#ifdef SVN_ERR__TRACING
  /* Make an error chain containing only tracing errors and check that
     svn_error_purge_tracing() asserts on it. */
  {
    svn_error_t err_copy;
    svn_error_malfunction_handler_t orig_handler;

    /* For this test, use a random error status. */
    err = svn_error_create(SVN_ERR_BAD_UUID, NULL, "");
    err = svn_error_trace(err);
    err->child->message = err->message;

    /* Register a malfunction handler that doesn't call abort() to
       check that a new error chain with an assertion error is
       returned. */
    orig_handler =
      svn_error_set_malfunction_handler(svn_error_raise_on_malfunction);
    err2 = svn_error_purge_tracing(err);
    svn_error_set_malfunction_handler(orig_handler);

    err_copy = *err;

    if (err2)
      {
        /* If err2 does share the same pool as err, then make a copy
           of err2 and err3 before err is cleared. */
        svn_error_t err2_copy = *err2;
        svn_error_t *err3 = err2;
        svn_error_t err3_copy;

        while (err3 && svn_error__is_tracing_link(err3))
          err3 = err3->child;
        if (err3)
          err3_copy = *err3;
        else
          err3_copy.apr_err = APR_SUCCESS;

        svn_error_clear(err);

        /* The returned error is only safe to clear if this assertion
           holds, otherwise it has the same pool as the original
           error. */
        SVN_TEST_ASSERT(err_copy.pool != err2_copy.pool);

        svn_error_clear(err2);

        SVN_TEST_ASSERT(err3);

        SVN_TEST_ASSERT(SVN_ERROR_IN_CATEGORY(err2_copy.apr_err,
                                              SVN_ERR_MALFUNC_CATEGORY_START));
        SVN_TEST_ASSERT(err3_copy.apr_err == err2_copy.apr_err);
        SVN_TEST_ASSERT(
          SVN_ERR_ASSERTION_ONLY_TRACING_LINKS == err3_copy.apr_err);
      }
    else
      {
        svn_error_clear(err);
        SVN_TEST_ASSERT(err2);
      }
  }
#endif

  return SVN_NO_ERROR;
}

static svn_error_t *
test_error_symbolic_name(apr_pool_t *pool)
{
  struct {
    svn_errno_t errcode;
    const char *errname;
  } errors[] = {
    { SVN_ERR_BAD_CONTAINING_POOL, "SVN_ERR_BAD_CONTAINING_POOL" },
    { SVN_ERR_BAD_FILENAME, "SVN_ERR_BAD_FILENAME" },
    { SVN_ERR_XML_ATTRIB_NOT_FOUND, "SVN_ERR_XML_ATTRIB_NOT_FOUND" },
    { SVN_ERR_ENTRY_NOT_FOUND, "SVN_ERR_ENTRY_NOT_FOUND" },
    { SVN_ERR_ENTRY_CATEGORY_START + 1, NULL }, /* unused slot */
    { SVN_ERR_ENTRY_EXISTS, "SVN_ERR_ENTRY_EXISTS" },
    { SVN_ERR_ASSERTION_ONLY_TRACING_LINKS, "SVN_ERR_ASSERTION_ONLY_TRACING_LINKS" },
    { SVN_ERR_FS_CORRUPT, "SVN_ERR_FS_CORRUPT" },
    /* The following two error codes can return either of their names
       as the string. For simplicity, test what the current implementation
       returns; but if it starts returning "SVN_ERR_WC_NOT_DIRECTORY",
       that's fine (and permitted by the API contract). */
    { SVN_ERR_WC_NOT_DIRECTORY, "SVN_ERR_WC_NOT_WORKING_COPY" },
    { SVN_ERR_WC_NOT_WORKING_COPY, "SVN_ERR_WC_NOT_WORKING_COPY" },
    /* Test an implementation detail. */
    { SVN_ERR_BAD_CATEGORY_START, "SVN_ERR_BAD_CONTAINING_POOL" },
#ifdef SVN_DEBUG
    { ENOENT, "ENOENT" },
    { APR_ENOPOOL, "APR_ENOPOOL" },
#endif
    /* Test non-errors. */
    { -1, NULL },
    { SVN_ERR_WC_CATEGORY_START - 1, NULL },
    /* Whitebox-test exceptional cases. */
    { SVN_WARNING, "SVN_WARNING" },
    { 0, "SVN_NO_ERROR" }
    /* No sentinel. */
  };
  int i;

  for (i = 0; i < sizeof(errors) / sizeof(errors[0]); i++)
    SVN_TEST_STRING_ASSERT(svn_error_symbolic_name(errors[i].errcode),
                           errors[i].errname);

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_error_root_cause,
                   "test svn_error_root_cause"),
    SVN_TEST_PASS2(test_error_purge_tracing,
                   "test svn_error_purge_tracing"),
    SVN_TEST_PASS2(test_error_symbolic_name,
                   "test svn_error_symbolic_name"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
