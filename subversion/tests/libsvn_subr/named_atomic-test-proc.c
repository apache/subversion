/*
 * named_atomic-test-proc.c:  a collection of svn_named_atomic__t tests
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

/* ====================================================================
   To add tests, look toward the bottom of this file.
*/


#include <stdio.h>

/* shared test implementation */
#include "named_atomic-test-common.h"

/* Very simple process frame around the actual test code */
int
main(int argc, const char *argv[])
{
  svn_boolean_t got_error = FALSE;
  apr_pool_t *pool;
  svn_error_t *err;

  int id = 0;
  int count = 0;
  int iterations = 0;

  /* Initialize APR (Apache pools) */
  if (apr_initialize() != APR_SUCCESS)
    {
      printf("apr_initialize() failed.\n");
      exit(1);
    }

  pool = svn_pool_create(NULL);

  /* lean & mean parameter parsing */
  if (argc != 5)
    {
      if (argc == 1) /* used to test that this executable can be started */
        exit(0);

      printf("Usage: named_atomic-proc-test ID COUNT ITERATIONS NS.\n");
      exit(1);
    }

  id = (int)apr_atoi64(argv[1]);
  count = (int)apr_atoi64(argv[2]);
  iterations = (int)apr_atoi64(argv[3]);
  name_namespace = argv[4];

  /* run test routine */

  err = test_pipeline(id, count, iterations, pool);
  if (err)
  {
    const char *prefix = apr_psprintf(pool, "Process %d: ", id);
    got_error = TRUE;
    svn_handle_error2(err, stdout, FALSE, prefix);
    svn_error_clear(err);
  }

  /* Clean up APR */
  svn_pool_destroy(pool);
  apr_terminate();

  return got_error;
}
