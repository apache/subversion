/*
 * named_atomic-test.c:  a collection of svn_named_atomic__t tests
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
#include <apr_file_io.h>

#include "svn_io.h"

/* shared test implementation */
#include "named_atomic-test-common.h"

/* Name of the worker process executable */
#define TEST_PROC "named_atomic-proc-test"

/* number of hardware threads (logical cores) that we may use.
 * Will be set to at least 2 - even on unicore machines. */
static int hw_thread_count = 0;

/* number of iterations that we should perform on concurrency tests
 * (will be calibrated to about 1s runtime)*/
static int suggested_iterations = 0;

/* If possible, translate PROC to a global path and set DIRECTORY to
 * the current directory.
 */
static svn_error_t *
adjust_proc_path(const char **proc, const char **directory, apr_pool_t *pool)
{
#ifdef WIN32
  /* Under Windows, the test will not be in the current directory
   * and neither will be PROC. Therefore, determine its full path */
  char path [MAX_PATH] = { 0 };
  GetModuleFileNameA(NULL, path, sizeof(path));
  *(strrchr(path, '\\') + 1) = 0;
  *proc = apr_pstrcat(pool, path, *proc, ".exe", NULL);

  /* And we need to set the working dir to our working dir to make
   * our sub-processes find all DLLs. */
  GetCurrentDirectoryA(sizeof(path), path);
  *directory = apr_pstrdup(pool, path);
#endif

  return SVN_NO_ERROR;
}

/* Returns true if PROC can be found and executed.
 */
static svn_boolean_t
proc_found(const char *proc, apr_pool_t *pool)
{
  static svn_tristate_t result = svn_tristate_unknown;

  if (result == svn_tristate_unknown)
    {
      svn_error_t *error = SVN_NO_ERROR;
      const char * directory = NULL;

      /* all processes and their I/O data */
      apr_proc_t process;
      const char * args[2];

      args[0] = proc;
      args[1] = NULL;
      svn_error_clear(adjust_proc_path(&args[0], &directory, pool));

      /* try to start the process */
      error = svn_io_start_cmd3(&process,
                                directory,  /* working directory */
                                args[0],
                                args,
                                NULL,       /* environment */
                                FALSE,      /* no handle inheritance */
                                FALSE,      /* no STDIN pipe */
                                NULL,
                                FALSE,      /* no STDOUT pipe */
                                NULL,
                                FALSE,      /* no STDERR pipe */
                                NULL,
                                pool);
      if (!error)
        error = svn_io_wait_for_cmd(&process, proc, NULL, NULL, pool);

      result = error ? svn_tristate_false : svn_tristate_true;
      svn_error_clear(error);
    }

  return result == svn_tristate_true;
}

/* Remove temporary files from disk.
 */
static apr_status_t
cleanup_test_shm(void *arg)
{
  apr_pool_t *pool = arg;

  svn_error_clear(svn_atomic_namespace__cleanup(name_namespace, pool));
  svn_error_clear(svn_atomic_namespace__cleanup(name_namespace1, pool));
  svn_error_clear(svn_atomic_namespace__cleanup(name_namespace2, pool));

  return 0;
}

/* Bring shared memory to a defined state. This is very useful in case of
 * lingering problems from previous tests or test runs.
 */
static svn_error_t *
init_test_shm(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic;
  apr_pool_t *scratch = svn_pool_create(pool);

  if (name_namespace == NULL)
    {
      apr_pool_t *global_pool = svn_pool_create(NULL);
      SVN_ERR(svn_io_open_unique_file3(NULL,
                                       &name_namespace,
                                       NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       global_pool,
                                       pool));
      SVN_ERR(svn_io_open_unique_file3(NULL,
                                       &name_namespace1,
                                       NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       global_pool,
                                       pool));
      SVN_ERR(svn_io_open_unique_file3(NULL,
                                       &name_namespace2,
                                       NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       global_pool,
                                       pool));
    }

  /* skip tests if the current user does not have the required privileges */
  if (!svn_named_atomic__is_supported())
    return svn_error_wrap_apr(SVN_ERR_TEST_SKIPPED,
                              "user has insufficient privileges");

  /* destroy temp files after usage */

  apr_pool_cleanup_register(pool, pool,
                            cleanup_test_shm, apr_pool_cleanup_null);

  /* get the two I/O atomics for this thread */
  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace, scratch));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "1", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "2", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));

  svn_pool_clear(scratch);

  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace1, scratch));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  svn_pool_clear(scratch);

  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace2, scratch));
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  svn_pool_clear(scratch);

  /* done */

  return SVN_NO_ERROR;
}

/* Prepare the shared memory for a run with COUNT workers.
 */
static svn_error_t *
init_concurrency_test_shm(apr_pool_t *pool, int count)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic;
  int i;

  /* get the two I/O atomics for this thread */
  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace, pool));

  /* reset the I/O atomics for all threads */
  for (i = 0; i < count; ++i)
    {
      SVN_ERR(svn_named_atomic__get(&atomic,
                                    ns,
                                    apr_pstrcat(pool,
                                                ATOMIC_NAME,
                                                apr_itoa(pool, i),
                                                NULL),
                                    TRUE));
      SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
    }

  SVN_ERR(svn_named_atomic__get(&atomic, ns, "counter", TRUE));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));

  return SVN_NO_ERROR;
}

#if APR_HAS_THREADS

/* our thread function type
 */
typedef svn_error_t *(*thread_func_t)(int, int, int, apr_pool_t *);

/* Per-thread input and output data.
 */
struct thread_baton
{
  int thread_count;
  int thread_no;
  int iterations;
  svn_error_t *result;
  thread_func_t func;
};

/* APR thread function implementation: A wrapper around baton->func that
 * handles the svn_error_t return value.
 */
static void *
APR_THREAD_FUNC test_thread(apr_thread_t *thread, void *baton)
{
  struct thread_baton *params = baton;
  apr_pool_t *pool = svn_pool_create_ex(NULL, NULL);

  params->result = (*params->func)(params->thread_no,
                                   params->thread_count,
                                   params->iterations,
                                   pool);
  svn_pool_destroy(pool);
  apr_thread_exit(thread, APR_SUCCESS);

  return NULL;
}

/* Runs FUNC in COUNT concurrent threads ITERATION times and combines the
 * results.
 */
static svn_error_t *
run_threads(apr_pool_t *pool, int count, int iterations, thread_func_t func)
{
  apr_status_t status;
  int i;
  svn_error_t *error = SVN_NO_ERROR;

  /* all threads and their I/O data */
  apr_thread_t **threads = apr_palloc(pool, count * sizeof(*threads));
  struct thread_baton *batons = apr_palloc(pool, count * sizeof(*batons));

  /* start threads */
  for (i = 0; i < count; ++i)
    {
      batons[i].thread_count = count;
      batons[i].thread_no = i;
      batons[i].iterations = iterations;
      batons[i].func = func;

      status = apr_thread_create(&threads[i],
                                 NULL,
                                 test_thread,
                                 &batons[i],
                                 pool);
      if (status != APR_SUCCESS)
        SVN_ERR(svn_error_wrap_apr(status, "could not create a thread"));
    }

  /* Wait for threads to finish and return result. */
  for (i = 0; i < count; ++i)
    {
      apr_status_t retval;
      status = apr_thread_join(&retval, threads[i]);
      if (status != APR_SUCCESS)
        SVN_ERR(svn_error_wrap_apr(status, "waiting for thread's end failed"));

      if (batons[i].result)
        error = svn_error_compose_create (error, svn_error_quick_wrap
           (batons[i].result, apr_psprintf(pool, "Thread %d failed", i)));
    }

  return error;
}
#endif

/* Runs PROC in COUNT concurrent worker processes and check the results.
 */
static svn_error_t *
run_procs(apr_pool_t *pool, const char *proc, int count, int iterations)
{
  int i, k;
  svn_error_t *error = SVN_NO_ERROR;
  const char * directory = NULL;

  /* all processes and their I/O data */
  apr_proc_t *process = apr_palloc(pool, count * sizeof(*process));
  apr_file_t *common_stdout = NULL;
  apr_file_open_stdout(&common_stdout, pool);

  SVN_ERR(adjust_proc_path(&proc, &directory, pool));

  /* start sub-processes */
  for (i = 0; i < count; ++i)
    {
      const char * args[6];

      args[0] = proc;
      args[1] = apr_itoa(pool, i);
      args[2] = apr_itoa(pool, count);
      args[3] = apr_itoa(pool, iterations);
      args[4] = name_namespace;
      args[5] = NULL;

      error = svn_io_start_cmd3(&process[i],
                                directory,  /* working directory */
                                args[0],
                                args,
                                NULL,       /* environment */
                                FALSE,      /* no handle inheritance */
                                FALSE,      /* no STDIN pipe */
                                NULL,
                                FALSE,      /* consolidate into 1 STDOUT */
                                common_stdout,
                                FALSE,      /* no STDERR pipe */
                                NULL,
                                pool);
      if (error)
        {
          /* dump program name and parameters */
          for (k = 0; k < sizeof(args) / sizeof(args[0]); ++k)
            if (args[k])
              printf(k == 0 ? "%s\n" : "    %s\n", args[k]);

          if (directory)
            printf("working folder %s:\n", directory);

          return error;
        }
    }

  /* Wait for sub-processes to finish and return result. */
  for (i = 0; i < count; ++i)
    {
      const char *cmd = apr_psprintf(pool,
                                     "named_atomic-test-proc %d %d %d",
                                     i, count, iterations);
      error = svn_error_compose_create(error,
                                       svn_io_wait_for_cmd(&process[i],
                                                           cmd, NULL, NULL,
                                                           pool));
    }

  return error;
}

/* Set SUGGESTED_ITERATIONS to a value that COUNT workers will take
 * about 1 second to execute.
 */
static svn_error_t *
calibrate_iterations(apr_pool_t *pool, int count)
{
  apr_time_t start;
  int calib_iterations;
  double taken = 0.0;

  /* increase iterations until we pass the 100ms mark */

  for (calib_iterations = 10; taken < 100000.0; calib_iterations *= 2)
    {
      apr_pool_t *scratch = svn_pool_create(pool);
      SVN_ERR(init_concurrency_test_shm(scratch, count));

      start = apr_time_now();
      SVN_ERR(run_procs(pool, TEST_PROC, count, calib_iterations));

      taken = (double)(apr_time_now() - start);
      svn_pool_destroy(scratch);
    }

  /* scale that to 1s */

  suggested_iterations = (int)(1000000.0 / taken * calib_iterations);

  return SVN_NO_ERROR;
}

/* Find out how far the system will scale, i.e. how many workers can be
 * run concurrently without experiencing significant slowdowns.
 * Sets HW_THREAD_COUNT to a value of 2 .. 32 (limit the system impact in
 * case our heuristics fail) and determines the number of iterations.
 * Can be called multiple times but will skip the calculations after the
 * first successful run.
 */
static svn_error_t *
calibrate_concurrency(apr_pool_t *pool)
{
  if (hw_thread_count == 0)
    {
      /* these parameters should be ok even on very slow machines */
      hw_thread_count = 2;
      suggested_iterations = 100;

      /* if we've got a proper machine and OS setup, let's prepare for
       * some real testing */
      if (svn_named_atomic__is_efficient() && proc_found(TEST_PROC, pool))
        {
          SVN_ERR(calibrate_iterations(pool, 2));
          for (; hw_thread_count < 32; hw_thread_count *= 2)
            {
              int saved_suggestion = suggested_iterations;

              /* run with an additional core to spare
               * (even low CPU usage might cause heavy context switching) */
              SVN_ERR(calibrate_iterations(pool, hw_thread_count * 2 + 1));
              if (suggested_iterations < 100000)
                {
                  /* Machines with only a small number of cores are prone
                   * to inconsistent performance due context switching.
                   * Reduce the number of iterations on those machines. */
                  suggested_iterations = hw_thread_count > 2
                                       ? saved_suggestion
                                       : saved_suggestion / 2;
                  break;
                }
            }
        }

      printf("using %d cores for %d iterations\n", hw_thread_count,
                                                   suggested_iterations);
  }

  return SVN_NO_ERROR;
}

/* The individual tests */

static svn_error_t *
test_basics(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic;
  apr_int64_t value;

  SVN_ERR(init_test_shm(pool));

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace, pool));

  /* Test a non-existing atomic */
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME "x", FALSE));
  SVN_TEST_ASSERT(atomic == NULL);

  /* Now, we auto-create it */
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_TEST_ASSERT(atomic != NULL);

  /* The default value should be 0 */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__read(&value, NULL),
                        SVN_ERR_BAD_ATOMIC);
  value = 1;
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 0);

  /* Write should return the previous value. */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__write(&value, 0, NULL),
                        SVN_ERR_BAD_ATOMIC);
  value = 1;
  SVN_ERR(svn_named_atomic__write(&value, 21, atomic));
  SVN_TEST_ASSERT(value == 0);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 21);

  SVN_ERR(svn_named_atomic__write(&value, 42, atomic));
  SVN_TEST_ASSERT(value == 21);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 42);

  SVN_ERR(svn_named_atomic__write(NULL, 17, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 17);

  /* Adding & subtracting values */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__add(&value, 0, NULL),
                        SVN_ERR_BAD_ATOMIC);
  SVN_ERR(svn_named_atomic__add(&value, 25, atomic));
  SVN_TEST_ASSERT(value == 42);
  SVN_ERR(svn_named_atomic__add(NULL, 47, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 89);

  SVN_ERR(svn_named_atomic__add(&value, -25, atomic));
  SVN_TEST_ASSERT(value == 64);
  SVN_ERR(svn_named_atomic__add(NULL, -22, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 42);

  /* Compare-and-exchange */
  SVN_TEST_ASSERT_ERROR(svn_named_atomic__cmpxchg(&value, 0, 0, NULL),
                        SVN_ERR_BAD_ATOMIC);
  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 99, 41, atomic));
  SVN_TEST_ASSERT(value == 42);

  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 98, 42, atomic));
  SVN_TEST_ASSERT(value == 42);
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 67, 98, atomic));
  SVN_TEST_ASSERT(value == 98);

  SVN_ERR(svn_named_atomic__cmpxchg(NULL, 42, 67, atomic));
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 42);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_bignums(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic;
  apr_int64_t value;

  SVN_ERR(init_test_shm(pool));

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace, pool));

  /* Auto-create our atomic variable */
  SVN_ERR(svn_named_atomic__get(&atomic, ns, ATOMIC_NAME, TRUE));
  SVN_TEST_ASSERT(atomic != NULL);

  /* Write should return the previous value. */

  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic));
  value = 1;
  SVN_ERR(svn_named_atomic__write(&value, 21 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 0 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__write(&value, 17 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);

  /* Adding & subtracting values */
  SVN_ERR(svn_named_atomic__add(&value, 25 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 42 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__add(&value, -25 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 17 * HUGE_VALUE);

  /* Compare-and-exchange */
  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 99 * HUGE_VALUE, 41 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 17 * HUGE_VALUE);

  value = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value, 98 * HUGE_VALUE, 17 * HUGE_VALUE, atomic));
  SVN_TEST_ASSERT(value == 17 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic));
  SVN_TEST_ASSERT(value == 98 * HUGE_VALUE);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_multiple_atomics(apr_pool_t *pool)
{
  svn_atomic_namespace__t *ns;
  svn_named_atomic__t *atomic1;
  svn_named_atomic__t *atomic2;
  svn_named_atomic__t *atomic1_alias;
  svn_named_atomic__t *atomic2_alias;
  apr_int64_t value1;
  apr_int64_t value2;

  SVN_ERR(init_test_shm(pool));

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&ns, name_namespace, pool));

  /* Create two atomics */
  SVN_ERR(svn_named_atomic__get(&atomic1, ns, ATOMIC_NAME "1", TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic2, ns, ATOMIC_NAME "2", TRUE));
  SVN_TEST_ASSERT(atomic1 != NULL);
  SVN_TEST_ASSERT(atomic2 != NULL);
  SVN_TEST_ASSERT(atomic1 != atomic2);

  /* Get aliases to those */
  SVN_ERR(svn_named_atomic__get(&atomic1_alias, ns, ATOMIC_NAME "1", TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic2_alias, ns, ATOMIC_NAME "2", TRUE));
  SVN_TEST_ASSERT(atomic1 == atomic1_alias);
  SVN_TEST_ASSERT(atomic2 == atomic2_alias);

  /* The atomics shall not overlap, i.e. changes to one do not affect the other */
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic1));
  SVN_ERR(svn_named_atomic__write(NULL, 0, atomic2));
  SVN_ERR(svn_named_atomic__write(&value1, 21 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__write(&value2, 42 * HUGE_VALUE, atomic2));
  SVN_TEST_ASSERT(value1 == 0);
  SVN_TEST_ASSERT(value2 == 0);

  SVN_ERR(svn_named_atomic__read(&value1, atomic1));
  SVN_ERR(svn_named_atomic__read(&value2, atomic2));
  SVN_TEST_ASSERT(value1 == 21 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 42 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__add(&value1, 25 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__add(&value2, -25 * HUGE_VALUE, atomic2));
  SVN_TEST_ASSERT(value1 == 46 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 17 * HUGE_VALUE);

  value1 = 1;
  value2 = 1;
  SVN_ERR(svn_named_atomic__cmpxchg(&value1, 4 * HUGE_VALUE, 46 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__cmpxchg(&value2, 98 * HUGE_VALUE, 17 * HUGE_VALUE, atomic2));
  SVN_TEST_ASSERT(value1 == 46 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 17 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__read(&value1, atomic1));
  SVN_ERR(svn_named_atomic__read(&value2, atomic2));
  SVN_TEST_ASSERT(value1 == 4 * HUGE_VALUE);
  SVN_TEST_ASSERT(value2 == 98 * HUGE_VALUE);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_namespaces(apr_pool_t *pool)
{
  svn_atomic_namespace__t *test_namespace1;
  svn_atomic_namespace__t *test_namespace1_alias;
  svn_atomic_namespace__t *test_namespace2;
  svn_atomic_namespace__t *test_namespace2_alias;
  svn_named_atomic__t *atomic1;
  svn_named_atomic__t *atomic2;
  svn_named_atomic__t *atomic1_alias;
  svn_named_atomic__t *atomic2_alias;
  apr_int64_t value;

  SVN_ERR(init_test_shm(pool));

  /* Use a separate namespace for our tests isolate them from production */
  SVN_ERR(svn_atomic_namespace__create(&test_namespace1, name_namespace1, pool));
  SVN_ERR(svn_atomic_namespace__create(&test_namespace1_alias, name_namespace1, pool));
  SVN_ERR(svn_atomic_namespace__create(&test_namespace2, name_namespace2, pool));
  SVN_ERR(svn_atomic_namespace__create(&test_namespace2_alias, name_namespace2, pool));

  /* Create two atomics with the same name in different namespaces */
  SVN_ERR(svn_named_atomic__get(&atomic1, test_namespace1, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic1_alias, test_namespace1_alias, ATOMIC_NAME, FALSE));
  SVN_ERR(svn_named_atomic__get(&atomic2, test_namespace2, ATOMIC_NAME, TRUE));
  SVN_ERR(svn_named_atomic__get(&atomic2_alias, test_namespace2_alias, ATOMIC_NAME, FALSE));
  SVN_TEST_ASSERT(atomic1 != atomic1_alias);
  SVN_TEST_ASSERT(atomic1_alias != NULL);
  SVN_TEST_ASSERT(atomic2 != atomic2_alias);
  SVN_TEST_ASSERT(atomic2_alias != NULL);

  /* Write data to our atomics */
  SVN_ERR(svn_named_atomic__write(NULL, 21 * HUGE_VALUE, atomic1));
  SVN_ERR(svn_named_atomic__write(NULL, 42 * HUGE_VALUE, atomic2));

  /* Now check who sees which value */
  SVN_ERR(svn_named_atomic__read(&value, atomic1));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic2));
  SVN_TEST_ASSERT(value == 42 * HUGE_VALUE);

  SVN_ERR(svn_named_atomic__read(&value, atomic1_alias));
  SVN_TEST_ASSERT(value == 21 * HUGE_VALUE);
  SVN_ERR(svn_named_atomic__read(&value, atomic2_alias));
  SVN_TEST_ASSERT(value == 42 * HUGE_VALUE);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_multithreaded(apr_pool_t *pool)
{
#if APR_HAS_THREADS
  SVN_ERR(init_test_shm(pool));

  SVN_ERR(calibrate_concurrency(pool));

  SVN_ERR(init_concurrency_test_shm(pool, hw_thread_count));
  SVN_ERR(run_threads(pool, hw_thread_count, suggested_iterations, test_pipeline));

  return SVN_NO_ERROR;
#else
  return svn_error_create(SVN_ERR_TEST_SKIPPED, NULL, NULL);
#endif
}

static svn_error_t *
test_multiprocess(apr_pool_t *pool)
{
  if (!proc_found(TEST_PROC, pool))
    return svn_error_wrap_apr(SVN_ERR_TEST_SKIPPED,
                              "executable '%s' not found", TEST_PROC);

  SVN_ERR(init_test_shm(pool));

  SVN_ERR(calibrate_concurrency(pool));

  SVN_ERR(init_concurrency_test_shm(pool, hw_thread_count));
  SVN_ERR(run_procs(pool, TEST_PROC, hw_thread_count, suggested_iterations));

  return SVN_NO_ERROR;
}

/*
   ====================================================================
   If you add a new test to this file, update this array.

   (These globals are required by our included main())
*/

/* An array of all test functions */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_basics,
                   "basic r/w access to a single atomic"),
    SVN_TEST_PASS2(test_bignums,
                   "atomics must be 64 bits"),
    SVN_TEST_PASS2(test_multiple_atomics,
                   "basic r/w access to multiple atomics"),
    SVN_TEST_PASS2(test_namespaces,
                   "use different namespaces"),
    SVN_TEST_PASS2(test_multithreaded,
                   "multithreaded access to atomics"),
    SVN_TEST_PASS2(test_multiprocess,
                   "multi-process access to atomics"),
    SVN_TEST_NULL
  };
