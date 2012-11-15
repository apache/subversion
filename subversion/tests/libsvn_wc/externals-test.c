/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

/*
 * externals-test.c -- test externals
 */

#include <stdio.h>
#include <apr_tables.h>

#include "svn_wc.h"
#include "utils.h"

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

static svn_error_t *
test_parse_erratic_externals_definition(apr_pool_t *pool)
{
  svn_error_t *err;
  apr_array_header_t *list = NULL;

  err = svn_wc_parse_externals_description3(
          &list, "parent_dir",
          "^/valid/but/should/not/be/on/record wc_target\n"
           "because_this_is_an_error",
          FALSE, pool);

  if (err == NULL)
    return fail(pool,
            "expected error from svn_wc_parse_externals_description3().\n"
            );
  else
    svn_error_clear(err);

  if (list != NULL)
    {
      int i;
      printf("LIST now has items:\n");
      for (i = 0; i < list->nelts; i++)
        {
          svn_wc_external_item2_t *item = APR_ARRAY_IDX(list, i,
                                                        svn_wc_external_item2_t*);
          printf("- target_dir='%s' url='%s'\n", item->target_dir, item->url);
        }

      return fail(pool,
                  "svn_wc_parse_externals_description3() should not "
                  "touch LIST when DESC had an error.\n");
    }

  return SVN_NO_ERROR;
}

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_parse_erratic_externals_definition,
                   "parse erratic externals definition"),
    SVN_TEST_NULL
  };

