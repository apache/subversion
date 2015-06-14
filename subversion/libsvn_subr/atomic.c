/* atomic.c : perform atomic initialization
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

#include <assert.h>
#include <apr_time.h>
#include "private/svn_atomic.h"

/* Magic values for atomic initialization */
#define SVN_ATOMIC_UNINITIALIZED 0
#define SVN_ATOMIC_START_INIT    1
#define SVN_ATOMIC_INIT_FAILED   2
#define SVN_ATOMIC_INITIALIZED   3


/*
 * This is the actual atomic initialization driver.  The caller must
 * provide either ERR_INIT_FUNC and ERR_P, or STR_INIT_FUNC and
 * ERRSTR_P, but never both.
 */
static void
init_once(svn_atomic__err_init_func_t err_init_func,
          svn_error_t **err_p,
          svn_atomic__str_init_func_t str_init_func,
          const char **errstr_p,
          volatile svn_atomic_t *global_status,
          void *baton,
          apr_pool_t* pool)
{
  /* !! Don't use localizable strings in this function, because these
     !! might cause deadlocks. This function can be used to initialize
     !! libraries that are used for generating error messages. */

  /* We have to call init_func exactly once.  Because APR
     doesn't have statically-initialized mutexes, we implement a poor
     man's spinlock using svn_atomic_cas. */

  svn_error_t *err = SVN_NO_ERROR;
  const char *errstr = NULL;
  svn_atomic_t status = svn_atomic_cas(global_status,
                                       SVN_ATOMIC_START_INIT,
                                       SVN_ATOMIC_UNINITIALIZED);

#ifdef SVN_DEBUG
  /* Check that the parameters are valid. */
  assert(!err_init_func != !str_init_func);
  assert(!err_init_func == !err_p);
  assert(!str_init_func == !errstr_p);
#endif /* SVN_DEBUG */

  for (;;)
    {
      switch (status)
        {
        case SVN_ATOMIC_UNINITIALIZED:
          if (err_init_func)
            err = err_init_func(baton, pool);
          else
            errstr = str_init_func(baton);
          if (err || errstr)
            {
              status = svn_atomic_cas(global_status,
                                      SVN_ATOMIC_INIT_FAILED,
                                      SVN_ATOMIC_START_INIT);
            }
          else
            {
              status = svn_atomic_cas(global_status,
                                      SVN_ATOMIC_INITIALIZED,
                                      SVN_ATOMIC_START_INIT);
            }

          /* Take another spin through the switch to report either
             failure or success. */
          continue;

        case SVN_ATOMIC_START_INIT:
          /* Wait for the init function to complete. */
          apr_sleep(APR_USEC_PER_SEC / 1000);
          status = svn_atomic_cas(global_status,
                                  SVN_ATOMIC_UNINITIALIZED,
                                  SVN_ATOMIC_UNINITIALIZED);
          continue;

        case SVN_ATOMIC_INIT_FAILED:
          if (err_init_func)
            *err_p = svn_error_create(SVN_ERR_ATOMIC_INIT_FAILURE, err,
                                      "Couldn't perform atomic initialization");
          else
            *errstr_p = errstr;
          return;

        case SVN_ATOMIC_INITIALIZED:
          if (err_init_func)
            *err_p = SVN_NO_ERROR;
          else
            *errstr_p = NULL;
          return;

        default:
          /* Something went seriously wrong with the atomic operations. */
          abort();
        }
    }
}


svn_error_t *
svn_atomic__init_once(volatile svn_atomic_t *global_status,
                      svn_atomic__err_init_func_t err_init_func,
                      void *baton,
                      apr_pool_t* pool)
{
  svn_error_t *err;
  init_once(err_init_func, &err, NULL, NULL, global_status, baton, pool);
  return err;
}

const char *
svn_atomic__init_once_no_error(volatile svn_atomic_t *global_status,
                               svn_atomic__str_init_func_t str_init_func,
                               void *baton)
{
  const char *errstr;
  init_once(NULL, NULL, str_init_func, &errstr, global_status, baton, NULL);
  return errstr;
}
