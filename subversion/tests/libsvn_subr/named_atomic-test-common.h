/*
 * named_atomic-test-common.h:  shared function implementations for
 *                              named_atomic-test
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



/* "pipeline" test: worker with ID 0 initializes the data; all workers
 * (COUNT in total) have one input and one output bucket that form a ring
 * spanning all workers. Each worker passes the value along ITERATIONS times.
 */
static svn_error_t *
test_pipeline(int id, int count, int iterations, apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomicIn;
  svn_named_atomic__t *atomicOut;
  svn_named_atomic__t *atomicCounter;
  apr_int64_t value, old_value, last_value = 0;
  apr_int64_t i, counter;

  /* get the two I/O atomics for this thread */
  SVN_ERR(svn_atomic_namespace__create(&ns, TEST_NAMESPACE, pool));
  SVN_ERR(svn_named_atomic__get(&atomicIn,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     id),
                                            NULL),
                                TRUE));
  SVN_ERR(svn_named_atomic__get(&atomicOut,
                                ns,
                                apr_pstrcat(pool,
                                            ATOMIC_NAME,
                                            apr_itoa(pool,
                                                     (id + 1) % count),
                                            NULL),
                                TRUE));

  /* our iteration counter */
  SVN_ERR(svn_named_atomic__get(&atomicCounter, ns, "counter", TRUE));

  if (id == 0)
    {
      /* Initialize values in thread 0, pass them along in other threads */

      for (i = 1; i <= count; ++i)
        do
          /* Generate new token (once the old one has been removed)*/
          SVN_ERR(svn_named_atomic__cmpxchg(&old_value,
                                            i,
                                            0,
                                            atomicOut));
        while (old_value != 0);
     }

   /* Pass the tokens along */

   do
     {
       /* Wait for and consume incoming token. */
       do
         {
           SVN_ERR(svn_named_atomic__write(&value, 0, atomicIn));
           SVN_ERR(svn_named_atomic__read(&counter, atomicCounter));
         }
       while ((value == 0) && (counter < iterations));

       /* All tokes must come in in the same order */
       if (counter < iterations)
         SVN_TEST_ASSERT((last_value % count) == (value - 1));
       last_value = value;

       /* Wait for the target atomic to become vacant and write the token */
       do
         {
           SVN_ERR(svn_named_atomic__cmpxchg(&old_value,
                                             value,
                                             0,
                                             atomicOut));
           SVN_ERR(svn_named_atomic__read(&counter, atomicCounter));
         }
       while ((old_value != 0) && (counter < iterations));

       /* Count the number of operations */
       SVN_ERR(svn_named_atomic__add(&counter, 1, atomicCounter));
     }
   while (counter < iterations);

   /* done */

   return SVN_NO_ERROR;
}
