/*
 * priority-queue-test.c:  a collection of svn_priority_queue__* tests
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

#include "svn_error.h"
#include "private/svn_sorts_private.h"

/* priority queue test:
 * items in the queue are simple integers, in ascending order */

/* number of items to put into the queue */
enum {NUMBER_COUNT = 11};

/* the actual values in the order we add them to the queue */
static const int numbers[NUMBER_COUNT]
  = { 8395, 0, -1, 3885, 1,  -435, 99993, 10, 0, 1,  8395 };

/* test_update will modify in-queue data and expects the queue to return
   the values in the following order: */
static const int expected_modified[NUMBER_COUNT]
  = { -431, 0, 1, 3,  5, 10, 16, 3889, 8395, 8403,  99997 };

/* standard compare function for integers */
static int
compare_func(const void *lhs, const void *rhs)
{
  return *(const int *)lhs - *(const int *)rhs;
}

/* Check that QUEUE is empty and the usual operations still work */
static svn_error_t *
verify_empty_queue(svn_priority_queue__t *queue)
{
  /* it's an empty queue */
  SVN_TEST_ASSERT(svn_priority_queue__size(queue) == 0);
  SVN_TEST_ASSERT(svn_priority_queue__peek(queue) == NULL);

  /* these should be no-ops */
  svn_priority_queue__update(queue);
  svn_priority_queue__pop(queue);

  return SVN_NO_ERROR;
}

/* check that the tip of QUEUE equals EXPECTED and remove the first element */
static svn_error_t *
extract_expected(svn_priority_queue__t *queue, int expected)
{
  int value = *(int *)svn_priority_queue__peek(queue);
  SVN_TEST_ASSERT(value == expected);
  svn_priority_queue__pop(queue);

  return SVN_NO_ERROR;
}

/* Verify that QUEUE returns all elements in the proper order.
   Also check that data can be added & removed without disturbing the order.
 */
static svn_error_t *
verify_queue_order(svn_priority_queue__t *queue)
{
  int sorted[NUMBER_COUNT];
  int i;

  /* reference order */
  memcpy(sorted, numbers, sizeof(numbers));
  qsort(sorted, NUMBER_COUNT, sizeof(sorted[0]), compare_func);

  /* verify that the queue returns the data in the same order */
  for (i = 0; i < NUMBER_COUNT; ++i)
    {
      int item = *(int *)svn_priority_queue__peek(queue);
      int to_insert;

      /* is this the value we expected? */
      SVN_TEST_ASSERT(item == sorted[i]);

      /* add two items at the tip of the queue */
      to_insert = item - 1;
      svn_priority_queue__push(queue, &to_insert);
      svn_priority_queue__push(queue, &item);

      /* check queue length */
      SVN_TEST_ASSERT(svn_priority_queue__size(queue) == NUMBER_COUNT-i+2);

      /* now, lets extract all 3 of them */
      SVN_ERR(extract_expected(queue, item-1));
      SVN_ERR(extract_expected(queue, item));
      SVN_ERR(extract_expected(queue, item));

      /* check queue length */
      SVN_TEST_ASSERT(svn_priority_queue__size(queue) == NUMBER_COUNT-i-1);
    }

  /* the queue should now be empty */
  verify_empty_queue(queue);

  return SVN_NO_ERROR;
}

/* return a queue allocated in POOL containing all items of NUMBERS */
static svn_priority_queue__t *
create_standard_queue(apr_pool_t *pool)
{
  apr_array_header_t *elements
    = apr_array_make(pool, 11, sizeof(numbers[0]));

  /* build queue */
  int i;
  for (i = 0; i < NUMBER_COUNT; ++i)
    APR_ARRAY_PUSH(elements, int) = numbers[i];

  return svn_priority_queue__create(elements, compare_func);
}


static svn_error_t *
test_empty_queue(apr_pool_t *pool)
{
  apr_array_header_t *elements
    = apr_array_make(pool, 0, sizeof(int));
  svn_priority_queue__t *queue
    = svn_priority_queue__create(elements, compare_func);

  verify_empty_queue(queue);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_sort_queue(apr_pool_t *pool)
{
  svn_priority_queue__t *queue = create_standard_queue(pool);

  /* data should come out of the queue in sorted order */
  SVN_ERR(verify_queue_order(queue));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_push(apr_pool_t *pool)
{
  apr_array_header_t *elements
    = apr_array_make(pool, 3, sizeof(int));
  svn_priority_queue__t *queue
    = svn_priority_queue__create(elements, compare_func);

  /* build queue */
  int i;
  for (i = 0; i < NUMBER_COUNT; ++i)
    svn_priority_queue__push(queue, &numbers[i]);

  /* data should come out of the queue in sorted order */
  SVN_ERR(verify_queue_order(queue));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_update(apr_pool_t *pool)
{
  svn_priority_queue__t *queue = create_standard_queue(pool);

  /* modify all items in the queue */
  int i;
  for (i = 0; i < NUMBER_COUNT; ++i)
    {
      int *tip = svn_priority_queue__peek(queue);
      *tip += 4;
      svn_priority_queue__update(queue);

      /* extract and verify tip */
      SVN_TEST_ASSERT(*(int *)svn_priority_queue__peek(queue)
                      == expected_modified[i]);
      svn_priority_queue__pop(queue);

      /* this should be a no-op now */
      svn_priority_queue__update(queue);

      SVN_TEST_ASSERT(svn_priority_queue__size(queue) == NUMBER_COUNT-i-1);
    }

  /* the queue should now be empty */
  verify_empty_queue(queue);

  return SVN_NO_ERROR;
}

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_empty_queue,
                   "test empty queue"),
    SVN_TEST_PASS2(test_sort_queue,
                   "data returned by a priority queue shall be ordered"),
    SVN_TEST_PASS2(test_push,
                   "priority queues can be built up incrementally"),
    SVN_TEST_PASS2(test_update,
                   "updating the head of the queue"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
