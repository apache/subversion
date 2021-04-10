
/*
 * task-test.c:  a collection of svn_task__* tests
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
#include <string.h>
#include <apr_pools.h>

#include "../svn_test.h"

#include "svn_sorts.h"
#include "private/svn_atomic.h"
#include "private/svn_task.h"

static svn_error_t *
test_null_task(apr_pool_t *pool)
{
  SVN_ERR(svn_task__run(1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        pool, pool));
  SVN_ERR(svn_task__run(2, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        pool, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
noop_process_func(void **result,
                  svn_task__t *task,
                  void *thread_context,
                  void *process_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  *result = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
noop_output_func(svn_task__t *task,
                 void *result,
                 void *output_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
noop_thead_context_constructor(void **thread_context,
                               void *baton,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  *thread_context = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
noop_cancel_func(void *baton)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
test_noop_task(apr_pool_t *pool)
{
  SVN_ERR(svn_task__run(1,
                        noop_process_func, NULL,
                        noop_output_func, NULL,
                        noop_thead_context_constructor, NULL,
                        noop_cancel_func, NULL, pool, pool));
  SVN_ERR(svn_task__run(2,
                        noop_process_func, NULL,
                        noop_output_func, NULL,
                        noop_thead_context_constructor, NULL,
                        noop_cancel_func, NULL, pool, pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
counter_func(void **result,
             svn_task__t *task,
             void *thread_context,
             void *process_baton,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  apr_int64_t value = *(apr_int64_t*)process_baton;

  apr_pool_t *sub_task_pool;
  apr_int64_t *partial_result;
  apr_int64_t *partial_baton;

  if (value > 1)
    {
      partial_result = apr_palloc(result_pool, sizeof(partial_result));
      *partial_result = 1;
      value -= *partial_result;

      sub_task_pool = svn_task__create_process_pool(task);

      partial_baton = apr_palloc(sub_task_pool, sizeof(partial_baton));      
      *partial_baton = MAX(1, value / 2);
      value -= *partial_baton;

      SVN_ERR(svn_task__add_similar(task, sub_task_pool, 
                                    partial_result, partial_baton));
    }

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));
    
  if (value > 1)
    {
      partial_result = apr_palloc(result_pool, sizeof(partial_result));
      *partial_result = 1;
      value -= *partial_result;

      sub_task_pool = svn_task__create_process_pool(task);

      partial_baton = apr_palloc(sub_task_pool, sizeof(partial_baton));    
      *partial_baton = value - 1;
      value -= *partial_baton;

      SVN_ERR(svn_task__add_similar(task, sub_task_pool,
                                    partial_result, partial_baton));
    }

  partial_result = apr_palloc(result_pool, sizeof(partial_result));
  *partial_result = value;
  *result = partial_result;

  return SVN_NO_ERROR;
}

static svn_error_t *
sum_func(svn_task__t *task,
         void *result,
         void *output_baton,
         svn_cancel_func_t cancel_func,
         void *cancel_baton,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  apr_int64_t *result_p = result;
  apr_int64_t *output_p = output_baton;
  
  if (result_p)
    *output_p += *result_p;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_counting(apr_pool_t *pool)
{
  apr_int64_t start = 1000000;
  apr_int64_t result = 0;
  SVN_ERR(svn_task__run(1, counter_func, &start, sum_func, &result,
                        NULL, NULL, NULL, NULL, pool, pool));
  SVN_TEST_ASSERT(result == start);

  result = 0;
  SVN_ERR(svn_task__run(4, counter_func, &start, sum_func, &result,
                        NULL, NULL, NULL, NULL, pool, pool));
  SVN_TEST_ASSERT(result == start);

  return SVN_NO_ERROR;
}

static svn_error_t *
cancel_at_10k(void *baton)
{
  if (*(apr_int64_t*)baton == 10000)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_cancellation(apr_pool_t *pool)
{
  apr_int64_t start = 1000000;
  apr_int64_t result = 0;
  SVN_TEST_ASSERT_ERROR(svn_task__run(1, counter_func, &start, sum_func, &result,
                                      NULL, NULL, cancel_at_10k, &result,
                                      pool, pool),
                        SVN_ERR_CANCELLED);
  SVN_TEST_ASSERT(result == 10000);

  result = 0;
  SVN_TEST_ASSERT_ERROR(svn_task__run(8, counter_func, &start, sum_func, &result,
                                      NULL, NULL, cancel_at_10k, &result,
                                      pool, pool),
                        SVN_ERR_CANCELLED);
  SVN_TEST_ASSERT(result == 10000);
  
  return SVN_NO_ERROR;
}

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_null_task,
                   "null-task"),
    SVN_TEST_PASS2(test_noop_task,
                   "no-op task"),
    SVN_TEST_PASS2(test_counting,
                   "concurrent counting"),
    SVN_TEST_PASS2(test_cancellation,
                   "cancelling tasks"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
