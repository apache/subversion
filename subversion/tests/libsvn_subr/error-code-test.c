/*
 * error-code-test.c -- tests for error codes
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

#include "svn_error.h"

/* Duplicate of the same typedef in libsvn_subr/error.c */
typedef struct err_defn {
  svn_errno_t errcode; /* 160004 */
  const char *errname; /* SVN_ERR_FS_CORRUPT */
  const char *errdesc; /* default message */
} err_defn;

/* To understand what is going on here, read svn_error_codes.h. */
#define SVN_ERROR_BUILD_ARRAY
#include "svn_error_codes.h"

#include "../svn_test.h"

#define NUM_ERRORS (sizeof(error_table)/sizeof(error_table[0]))

static svn_error_t *
check_error_codes_unique(apr_pool_t *pool)
{
  int i;
  struct err_defn e = error_table[0];

  /* Ensure error codes are strictly monotonically increasing. */
  for (i = 1; i < NUM_ERRORS; i++)
    {
      struct err_defn e2 = error_table[i];

      /* Don't fail the test if there is an odd number of errors.
       * The error array's sentinel has an error code of zero. */
      if (i == NUM_ERRORS - 1 && e2.errcode == 0)
        break;

      /* SVN_ERR_WC_NOT_DIRECTORY is an alias for SVN_ERR_WC_NOT_WORKING_COPY
       * and shares the same error code. */
      if (e.errcode != SVN_ERR_WC_NOT_DIRECTORY &&
          e.errcode >= e2.errcode)
        return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "Error 0x%x (%s) is not < 0x%x (%s)\n",
                                 e.errcode, e.errdesc, e2.errcode, e2.errdesc);
      e = e2;
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(check_error_codes_unique,
                   "check that error codes are unique"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
