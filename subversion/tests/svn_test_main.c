/*
 * svn_test_main.c:  shared main() & friends for SVN test-suite programs
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



#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#ifdef WIN32
#include <crtdbg.h>
#endif

#include <apr_pools.h>
#include <apr_general.h>
#include <apr_signal.h>
#include <apr_env.h>

#include "svn_cmdline.h"
#include "svn_opt.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_test.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_ctype.h"
#include "svn_utf.h"
#include "svn_version.h"

#include "private/svn_cmdline_private.h"
#include "private/svn_atomic.h"
#include "private/svn_mutex.h"
#include "private/svn_sqlite.h"

#include "svn_private_config.h"

#if APR_HAS_THREADS
#  include <apr_thread_proc.h>
#endif

/* Some Subversion test programs may want to parse options in the
   argument list, so we remember it here. */
extern int test_argc;
extern const char **test_argv;
int test_argc;
const char **test_argv;

/* Many tests write to disk. Instead of writing to the current
   directory, they should use this path as the root of the test data
   area. */
static const char *data_path;

/* Test option: Print more output */
static svn_boolean_t verbose_mode = FALSE;

/* Test option: Print only unexpected results */
static svn_boolean_t quiet_mode = FALSE;

/* Test option: Remove test directories after success */
static svn_boolean_t cleanup_mode = FALSE;

/* Test option: Allow segfaults */
static svn_boolean_t allow_segfaults = FALSE;

/* Test option: Limit testing to a given mode (i.e. XFail, Skip,
   Pass, All). */
static enum svn_test_mode_t mode_filter = svn_test_all;

/* Test option: Allow concurrent execution of tests */
static svn_boolean_t parallel = FALSE;

/* Option parsing enums and structures */
enum test_options_e {
  help_opt = SVN_OPT_FIRST_LONGOPT_ID,
  cleanup_opt,
  fstype_opt,
  list_opt,
  verbose_opt,
  quiet_opt,
  config_opt,
  server_minor_version_opt,
  allow_segfault_opt,
  srcdir_opt,
  reposdir_opt,
  reposurl_opt,
  repostemplate_opt,
  memcached_server_opt,
  mode_filter_opt,
  sqlite_log_opt,
  parallel_opt,
  fsfs_version_opt
};

static const apr_getopt_option_t cl_options[] =
{
  {"help",          help_opt, 0,
                    N_("display this help")},
  {"cleanup",       cleanup_opt, 0,
                    N_("remove test directories after success")},
  {"config-file",   config_opt, 1,
                    N_("specify test config file ARG")},
  {"fs-type",       fstype_opt, 1,
                    N_("specify a filesystem backend type ARG")},
  {"fsfs-version",  fsfs_version_opt, 1,
                    N_("specify the FSFS version ARG")},
  {"list",          list_opt, 0,
                    N_("lists all the tests with their short description")},
  {"mode-filter",   mode_filter_opt, 1,
                    N_("only run/list tests with expected mode ARG = PASS, "
                       "XFAIL, SKIP, or ALL (default)")},
  {"verbose",       verbose_opt, 0,
                    N_("print extra information")},
  {"server-minor-version", server_minor_version_opt, 1,
                    N_("set the minor version for the server ('3', '4', "
                       "'5', or '6')")},
  {"quiet",         quiet_opt, 0,
                    N_("print only unexpected results")},
  {"allow-segfaults", allow_segfault_opt, 0,
                    N_("don't trap seg faults (useful for debugging)")},
  {"srcdir",        srcdir_opt, 1,
                    N_("directory which contains test's C source files")},
  {"repos-dir",     reposdir_opt, 1,
                    N_("directory to create repositories in")},
  {"repos-url",     reposurl_opt, 1,
                    N_("the url to access reposdir as")},
  {"repos-template",repostemplate_opt, 1,
                    N_("the repository to use as template")},
  {"memcached-server", memcached_server_opt, 1,
                    N_("the memcached server to use")},
  {"sqlite-logging", sqlite_log_opt, 0,
                    N_("enable SQLite logging")},
  {"parallel",      parallel_opt, 0,
                    N_("allow concurrent execution of tests")},
  {0,               0, 0, 0}
};


/* ================================================================= */
/* Stuff for cleanup processing */

/* When non-zero, don't remove test directories */
static svn_boolean_t skip_cleanup = FALSE;

/* All cleanup actions are registered as cleanups on the cleanup_pool,
 * which may be thread-specific. */
#if APR_HAS_THREADS
/* The thread-local data key for the cleanup pool. */
static apr_threadkey_t *cleanup_pool_key = NULL;

/* No-op destructor for apr_threadkey_private_create(). */
static void null_threadkey_dtor(void *stuff) {}

/* Set the thread-specific cleanup pool. */
static void set_cleanup_pool(apr_pool_t *pool)
{
  apr_status_t status = apr_threadkey_private_set(pool, cleanup_pool_key);
  if (status)
    {
      printf("apr_threadkey_private_set() failed with code %ld.\n",
             (long)status);
      exit(1);
    }
}

/* Get the thread-specific cleanup pool. */
static apr_pool_t *get_cleanup_pool(void)
{
  void *data;
  apr_status_t status = apr_threadkey_private_get(&data, cleanup_pool_key);
  if (status)
    {
      printf("apr_threadkey_private_get() failed with code %ld.\n",
             (long)status);
      exit(1);
    }
  return data;
}

#  define cleanup_pool (get_cleanup_pool())
#  define HAVE_PER_THREAD_CLEANUP
#else
static apr_pool_t *cleanup_pool = NULL;
#  define set_cleanup_pool(p) (cleanup_pool = (p))
#endif

/* Used by test_thread to serialize access to stdout. */
static svn_mutex__t *log_mutex = NULL;

static apr_status_t
cleanup_rmtree(void *data)
{
  if (!skip_cleanup)
    {
      apr_pool_t *pool = svn_pool_create(NULL);
      const char *path = data;

      /* Ignore errors here. */
      svn_error_t *err = svn_io_remove_dir2(path, FALSE, NULL, NULL, pool);
      svn_error_clear(err);
      if (verbose_mode)
        {
          if (err)
            printf("FAILED CLEANUP: %s\n", path);
          else
            printf("CLEANUP: %s\n", path);
        }
      svn_pool_destroy(pool);
    }
  return APR_SUCCESS;
}



void
svn_test_add_dir_cleanup(const char *path)
{
  if (cleanup_mode)
    {
      const char *abspath;
      svn_error_t *err;

      /* All cleanup functions use the *same* pool (not subpools of it).
         Thus, we need to synchronize. */
      err = svn_mutex__lock(log_mutex);
      if (err)
        {
          if (verbose_mode)
            printf("FAILED svn_mutex__lock in svn_test_add_dir_cleanup.\n");
          svn_error_clear(err);
          return;
        }

      err = svn_path_get_absolute(&abspath, path, cleanup_pool);
      svn_error_clear(err);
      if (!err)
        apr_pool_cleanup_register(cleanup_pool, abspath, cleanup_rmtree,
                                  apr_pool_cleanup_null);
      else if (verbose_mode)
        printf("FAILED ABSPATH: %s\n", path);

      err = svn_mutex__unlock(log_mutex, NULL);
      if (err)
        {
          if (verbose_mode)
            printf("FAILED svn_mutex__unlock in svn_test_add_dir_cleanup.\n");
          svn_error_clear(err);
        }
    }
}


/* ================================================================= */
/* Quite a few tests use random numbers. */

apr_uint32_t
svn_test_rand(apr_uint32_t *seed)
{
  *seed = (*seed * 1103515245UL + 12345UL) & 0xffffffffUL;
  return *seed;
}


/* ================================================================= */


/* Determine the array size of test_funcs[], the inelegant way.  :)  */
static int
get_array_size(struct svn_test_descriptor_t *test_funcs)
{
  int i;

  for (i = 1; test_funcs[i].func2 || test_funcs[i].func_opts; i++)
    {
    }

  return (i - 1);
}

/* Buffer used for setjmp/longjmp. */
static jmp_buf jump_buffer;

/* Our SIGSEGV handler, which jumps back into do_test_num(), which see for
   more information. */
static void
crash_handler(int signum)
{
  longjmp(jump_buffer, 1);
}

/* Write the result of test number TEST_NUM to stdout.  Pretty-print test
   name and dots according to our test-suite spec, and return TRUE if there
   has been a test failure.

   The parameters are basically the internal state of do_test_num() and
   test_thread(). */
/*  */
static svn_boolean_t
log_results(const char *progname,
            int test_num,
            svn_boolean_t msg_only,
            svn_boolean_t run_this_test,
            svn_boolean_t skip,
            svn_boolean_t xfail,
            svn_boolean_t wimp,
            svn_error_t *err,
            const char *msg,
            const struct svn_test_descriptor_t *desc)
{
  svn_boolean_t test_failed;

  if (err && err->apr_err == SVN_ERR_TEST_SKIPPED)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      skip = TRUE;
      xfail = FALSE; /* Or all XFail tests reporting SKIP would be failing */
    }

  /* Failure means unexpected results -- FAIL or XPASS. */
  test_failed = (!wimp && ((err != SVN_NO_ERROR) != (xfail != 0)));

  /* If we got an error, print it out.  */
  if (err)
    {
      svn_handle_error2(err, stdout, FALSE, "svn_tests: ");
      svn_error_clear(err);
    }

  if (msg_only)
    {
      const svn_boolean_t otoh = !!desc->predicate.description;

      if (run_this_test)
        printf(" %3d    %-5s  %s%s%s%s%s%s\n",
               test_num,
               (xfail ? "XFAIL" : (skip ? "SKIP" : "")),
               msg ? msg : "(test did not provide name)",
               (wimp && verbose_mode) ? " [[" : "",
               (wimp && verbose_mode) ? desc->wip : "",
               (wimp && verbose_mode) ? "]]" : "",
               (otoh ? " / " : ""),
               (otoh ? desc->predicate.description : ""));
    }
  else if (run_this_test && ((! quiet_mode) || test_failed))
    {
      printf("%s %s %d: %s%s%s%s\n",
             (err
              ? (xfail ? "XFAIL:" : "FAIL: ")
              : (xfail ? "XPASS:" : (skip ? "SKIP: " : "PASS: "))),
             progname,
             test_num,
             msg ? msg : "(test did not provide name)",
             wimp ? " [[WIMP: " : "",
             wimp ? desc->wip : "",
             wimp ? "]]" : "");
    }

  if (msg)
    {
      size_t len = strlen(msg);
      if (len > 50)
        printf("WARNING: Test docstring exceeds 50 characters\n");
      if (msg[len - 1] == '.')
        printf("WARNING: Test docstring ends in a period (.)\n");
      if (svn_ctype_isupper(msg[0]))
        printf("WARNING: Test docstring is capitalized\n");
    }
  if (desc->msg == NULL)
    printf("WARNING: New-style test descriptor is missing a docstring.\n");

  fflush(stdout);

  return test_failed;
}

/* Execute a test number TEST_NUM.  Pretty-print test name and dots
   according to our test-suite spec, and return the result code.
   If HEADER_MSG and *HEADER_MSG are not NULL, print *HEADER_MSG prior
   to pretty-printing the test information, then set *HEADER_MSG to NULL. */
static svn_boolean_t
do_test_num(const char *progname,
            int test_num,
            struct svn_test_descriptor_t *test_funcs,
            svn_boolean_t msg_only,
            svn_test_opts_t *opts,
            const char **header_msg,
            apr_pool_t *pool)
{
  svn_boolean_t skip, xfail, wimp;
  svn_error_t *err;
  const char *msg = NULL;  /* the message this individual test prints out */
  const struct svn_test_descriptor_t *desc;
  const int array_size = get_array_size(test_funcs);
  svn_boolean_t run_this_test; /* This test's mode matches DESC->MODE. */
  enum svn_test_mode_t test_mode;
  volatile int adjusted_num = test_num; /* volatile for setjmp */

  /* This allows './some-test -- -1' to run the last test. */
  if (adjusted_num < 0)
    adjusted_num += array_size + 1;

  /* Check our array bounds! */
  if ((adjusted_num > array_size) || (adjusted_num <= 0))
    {
      if (header_msg && *header_msg)
        printf("%s", *header_msg);
      printf("FAIL: %s: THERE IS NO TEST NUMBER %2d\n", progname, adjusted_num);
      skip_cleanup = TRUE;
      return TRUE;  /* BAIL, this test number doesn't exist. */
    }

  desc = &test_funcs[adjusted_num];
  /* Check the test predicate. */
  if (desc->predicate.func
      && desc->predicate.func(opts, desc->predicate.value, pool))
    test_mode = desc->predicate.alternate_mode;
  else
    test_mode = desc->mode;

  skip = test_mode == svn_test_skip;
  xfail = test_mode == svn_test_xfail;
  wimp = xfail && desc->wip;
  msg = desc->msg;
  run_this_test = mode_filter == svn_test_all || mode_filter == test_mode;

  if (run_this_test && header_msg && *header_msg)
    {
      printf("%s", *header_msg);
      *header_msg = NULL;
    }

  if (!allow_segfaults)
    {
      /* Catch a crashing test, so we don't interrupt the rest of 'em. */
      apr_signal(SIGSEGV, crash_handler);
    }

  /* We use setjmp/longjmp to recover from the crash.  setjmp() essentially
     establishes a rollback point, and longjmp() goes back to that point.
     When we invoke longjmp(), it instructs setjmp() to return non-zero,
     so we don't end up in an infinite loop.

     If we've got non-zero from setjmp(), we know we've crashed. */
  if (setjmp(jump_buffer) == 0)
    {
      /* Do test */
      if (msg_only || skip || !run_this_test)
        err = NULL; /* pass */
      else if (desc->func2)
        err = (*desc->func2)(pool);
      else
        err = (*desc->func_opts)(opts, pool);
    }
  else
    err = svn_error_create(SVN_ERR_TEST_FAILED, NULL,
                           "Test crashed "
                           "(run in debugger with '--allow-segfaults')");

  if (!allow_segfaults)
    {
      /* Now back to your regularly scheduled program... */
      apr_signal(SIGSEGV, SIG_DFL);
    }

  /* Failure means unexpected results -- FAIL or XPASS. */
  skip_cleanup = log_results(progname, adjusted_num, msg_only, run_this_test,
                             skip, xfail, wimp, err, msg, desc);

  return skip_cleanup;
}

#if APR_HAS_THREADS

/* Per-test parameters used by test_thread */
typedef struct test_params_t
{
  /* Name of the application */
  const char *progname;

  /* Total number of tests to execute */
  svn_atomic_t test_count;

  /* Global test options as provided by main() */
  svn_test_opts_t *opts;

  /* Reference to the global failure flag.  Set this if any test failed. */
  svn_atomic_t got_error;

  /* Test to execute next. */
  svn_atomic_t test_num;

  /* Test functions array. */
  struct svn_test_descriptor_t *test_funcs;
} test_params_t;

/* Thread function similar to do_test_num() but with fewer options.  We do
   catch segfaults.  All parameters are given as a test_params_t in DATA.
 */
static void * APR_THREAD_FUNC
test_thread(apr_thread_t *thread, void *data)
{
  svn_boolean_t skip, xfail, wimp;
  svn_error_t *err;
  const struct svn_test_descriptor_t *desc;
  svn_boolean_t run_this_test; /* This test's mode matches DESC->MODE. */
  enum svn_test_mode_t test_mode;
  test_params_t *params = data;
  svn_atomic_t test_num;
  apr_pool_t *pool;
  apr_pool_t *thread_root
    = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

#ifdef HAVE_PER_THREAD_CLEANUP
  set_cleanup_pool(svn_pool_create(thread_root));
#endif

  pool = svn_pool_create(thread_root);

  for (test_num = svn_atomic_inc(&params->test_num);
       test_num <= params->test_count;
       test_num = svn_atomic_inc(&params->test_num))
    {
      svn_pool_clear(pool);
#ifdef HAVE_PER_THREAD_CLEANUP
      svn_pool_clear(cleanup_pool); /* after clearing pool*/
#endif

      desc = &params->test_funcs[test_num];
      /* Check the test predicate. */
      if (desc->predicate.func
          && desc->predicate.func(params->opts, desc->predicate.value, pool))
        test_mode = desc->predicate.alternate_mode;
      else
        test_mode = desc->mode;

      skip = test_mode == svn_test_skip;
      xfail = test_mode == svn_test_xfail;
      wimp = xfail && desc->wip;
      run_this_test = mode_filter == svn_test_all
                   || mode_filter == test_mode;

      /* Do test */
      if (skip || !run_this_test)
        err = NULL; /* pass */
      else if (desc->func2)
        err = (*desc->func2)(pool);
      else
        err = (*desc->func_opts)(params->opts, pool);

      /* Write results to console */
      svn_error_clear(svn_mutex__lock(log_mutex));
      if (log_results(params->progname, test_num, FALSE, run_this_test,
                      skip, xfail, wimp, err, desc->msg, desc))
        svn_atomic_set(&params->got_error, TRUE);
      svn_error_clear(svn_mutex__unlock(log_mutex, NULL));
    }

  svn_pool_clear(pool); /* Make sure this is cleared before cleanup_pool*/

  /* Release all test memory. Possibly includes cleanup_pool */
  svn_pool_destroy(thread_root);

  /* End thread explicitly to prevent APR_INCOMPLETE return codes in
     apr_thread_join(). */
  apr_thread_exit(thread, 0);
  return NULL;
}

/* Log an error with message MSG if the APR status of EXPR is not 0.
 */
#define CHECK_STATUS(expr,msg) \
  do { \
    apr_status_t rv = (expr); \
    if (rv) \
      { \
        svn_error_t *svn_err__temp = svn_error_wrap_apr(rv, msg); \
        svn_handle_error2(svn_err__temp, stdout, FALSE, "svn_tests: "); \
        svn_error_clear(svn_err__temp); \
      } \
  } while (0);

/* Execute all ARRAY_SIZE tests concurrently using MAX_THREADS threads.
   Pass PROGNAME and OPTS to the individual tests.  Return TRUE if at least
   one of the tests failed.  Allocate all data in POOL.

   Note that cleanups are delayed until all tests have been completed.
 */
static svn_boolean_t
do_tests_concurrently(const char *progname,
                      struct svn_test_descriptor_t *test_funcs,
                      int array_size,
                      int max_threads,
                      svn_test_opts_t *opts,
                      apr_pool_t *pool)
{
  int i;
  apr_thread_t **threads;

  /* Prepare thread parameters. */
  test_params_t params;
  params.got_error = FALSE;
  params.opts = opts;
  params.progname = progname;
  params.test_num = 1;
  params.test_funcs = test_funcs;
  params.test_count = array_size;

  /* Start all threads. */
  threads = apr_pcalloc(pool, max_threads * sizeof(*threads));
  for (i = 0; i < max_threads; ++i)
    {
      CHECK_STATUS(apr_thread_create(&threads[i], NULL, test_thread, &params,
                                     pool),
                   "creating test thread failed.\n");
    }

  /* Wait for all tasks (tests) to complete. */
  for (i = 0; i < max_threads; ++i)
    {
      apr_status_t result = 0;
      CHECK_STATUS(apr_thread_join(&result, threads[i]),
                   "Waiting for test thread to finish failed.");
      CHECK_STATUS(result,
                   "Test thread returned an error.");
    }

  return params.got_error != FALSE;
}

#endif

static void help(const char *progname, apr_pool_t *pool)
{
  int i;

  svn_error_clear(svn_cmdline_fprintf(stdout, pool,
                                      _("usage: %s [options] [test-numbers]\n"
                                      "\n"
                                      "Valid options:\n"),
                                      progname));
  for (i = 0; cl_options[i].name && cl_options[i].optch; i++)
    {
      const char *optstr;

      svn_opt_format_option(&optstr, cl_options + i, TRUE, pool);
      svn_error_clear(svn_cmdline_fprintf(stdout, pool, "  %s\n", optstr));
    }
  svn_error_clear(svn_cmdline_fprintf(stdout, pool, "\n"));
}

static svn_error_t *init_test_data(const char *argv0, apr_pool_t *pool)
{
  const char *temp_path;
  const char *base_name;

  /* Convert the program path to an absolute path. */
  SVN_ERR(svn_utf_cstring_to_utf8(&temp_path, argv0, pool));
  temp_path = svn_dirent_internal_style(temp_path, pool);
  SVN_ERR(svn_dirent_get_absolute(&temp_path, temp_path, pool));
  SVN_ERR_ASSERT(!svn_dirent_is_root(temp_path, strlen(temp_path)));

  /* Extract the interesting bits of the path. */
  temp_path = svn_dirent_dirname(temp_path, pool);
  base_name = svn_dirent_basename(temp_path, pool);
  if (0 == strcmp(base_name, ".libs"))
    {
      /* This is a libtoolized binary, skip the .libs directory. */
      temp_path = svn_dirent_dirname(temp_path, pool);
      base_name = svn_dirent_basename(temp_path, pool);
    }
  temp_path = svn_dirent_dirname(temp_path, pool);

  /* temp_path should now point to the root of the test
     builddir. Construct the path to the transient dir.  Note that we
     put the path insinde the cmdline/svn-test-work area. This is
     because trying to get the cmdline tests to use a different work
     area is unprintable; so we put the C test transient dir in the
     cmdline tests area, as the lesser of evils ... */
  temp_path = svn_dirent_join_many(pool, temp_path,
                                   "cmdline", "svn-test-work",
                                   base_name, SVN_VA_NULL);

  /* Finally, create the transient directory. */
  SVN_ERR(svn_io_make_dir_recursively(temp_path, pool));

  data_path = temp_path;
  return SVN_NO_ERROR;
}

const char *
svn_test_data_path(const char *base_name, apr_pool_t *result_pool)
{
  return svn_dirent_join(data_path, base_name, result_pool);
}

svn_error_t *
svn_test_get_srcdir(const char **srcdir,
                    const svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  const char *cwd;

  if (opts->srcdir)
    {
      *srcdir = opts->srcdir;
      return SVN_NO_ERROR;
    }

  fprintf(stderr, "WARNING: missing '--srcdir' option");
  SVN_ERR(svn_dirent_get_absolute(&cwd, ".", pool));
  fprintf(stderr, ", assuming '%s'\n", cwd);
  *srcdir = cwd;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__init_auth_baton(svn_auth_baton_t **ab,
                          apr_pool_t *result_pool)
{
  svn_config_t *cfg_config;

  SVN_ERR(svn_config_create2(&cfg_config, FALSE, FALSE, result_pool));

  /* Disable the crypto backends that might not be entirely
     threadsafe and/or compatible with running headless.

     The windows system is just our own files, but then with user-key
     encrypted data inside. */
  svn_config_set(cfg_config,
                 SVN_CONFIG_SECTION_AUTH,
                 SVN_CONFIG_OPTION_PASSWORD_STORES,
                 "windows-cryptoapi");

  SVN_ERR(svn_cmdline_create_auth_baton2(ab,
                                         TRUE  /* non_interactive */,
                                         "jrandom", "rayjandom",
                                         NULL,
                                         TRUE  /* no_auth_cache */,
                                         TRUE /* trust_server_cert_unkown_ca */,
                                         FALSE, FALSE, FALSE, FALSE,
                                         cfg_config, NULL, NULL, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_test_make_sandbox_dir(const char **sb_dir_p,
                          const char *sb_name,
                          apr_pool_t *pool)
{
  const char *sb_dir;

  sb_dir = svn_test_data_path(sb_name, pool);
  SVN_ERR(svn_io_remove_dir2(sb_dir, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_io_make_dir_recursively(sb_dir, pool));
  svn_test_add_dir_cleanup(sb_dir);

  *sb_dir_p = sb_dir;

  return SVN_NO_ERROR;
}

/* Standard svn test program */
int
svn_test_main(int argc, const char *argv[], int max_threads,
              struct svn_test_descriptor_t *test_funcs)
{
  int i;
  svn_boolean_t got_error = FALSE;
  apr_pool_t *pool, *test_pool;
  svn_boolean_t ran_a_test = FALSE;
  svn_boolean_t list_mode = FALSE;
  int opt_id;
  apr_status_t apr_err;
  apr_getopt_t *os;
  svn_error_t *err;
  char errmsg[200];
  /* How many tests are there? */
  int array_size = get_array_size(test_funcs);

  svn_test_opts_t opts = { NULL };

  opts.fs_type = DEFAULT_FS_TYPE;

  /* Initialize APR (Apache pools) */
  if (apr_initialize() != APR_SUCCESS)
    {
      printf("apr_initialize() failed.\n");
      exit(1);
    }

  /* set up the global pool.  Use a separate allocator to limit memory
   * usage but make it thread-safe to allow for multi-threaded tests.
   */
  pool = apr_allocator_owner_get(svn_pool_create_allocator(TRUE));
  err = svn_mutex__init(&log_mutex, TRUE, pool);
  if (err)
    {
      svn_handle_error2(err, stderr, TRUE, "svn_tests: ");
      svn_error_clear(err);
    }

  /* Set up the thread-local storage key for the cleanup pool. */
#ifdef HAVE_PER_THREAD_CLEANUP
  apr_err = apr_threadkey_private_create(&cleanup_pool_key,
                                         null_threadkey_dtor,
                                         pool);
  if (apr_err)
    {
      printf("apr_threadkey_private_create() failed with code %ld.\n",
             (long)apr_err);
      exit(1);
    }
#endif /* HAVE_PER_THREAD_CLEANUP */

  /* Remember the command line */
  test_argc = argc;
  test_argv = argv;

  err = init_test_data(argv[0], pool);
  if (err)
    {
      svn_handle_error2(err, stderr, TRUE, "svn_tests: ");
      svn_error_clear(err);
    }

  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    {
      svn_handle_error2(err, stderr, TRUE, "svn_tests: ");
      svn_error_clear(err);
    }


  os->interleave = TRUE; /* Let options and arguments be interleaved */

  /* Strip off any leading path components from the program name.  */
  opts.prog_name = svn_dirent_internal_style(argv[0], pool);
  opts.prog_name = svn_dirent_basename(opts.prog_name, NULL);

#ifdef WIN32
  /* Abuse cast in strstr() to remove .exe extension.
     Value is allocated in pool by svn_dirent_internal_style() */
  {
    char *exe_ext = strstr(opts.prog_name, ".exe");

    if (exe_ext)
      *exe_ext = '\0';
  }

#if _MSC_VER >= 1400
  /* ### This should work for VC++ 2002 (=1300) and later */
  /* Show the abort message on STDERR instead of a dialog to allow
     scripts (e.g. our testsuite) to continue after an abort without
     user intervention. Allow overriding for easier debugging. */
  if (!getenv("SVN_CMDLINE_USE_DIALOG_FOR_ABORT"))
    {
      /* In release mode: Redirect abort() errors to stderr */
      _set_error_mode(_OUT_TO_STDERR);

      /* In _DEBUG mode: Redirect all debug output (E.g. assert() to stderr.
         (Ignored in releas builds) */
      _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR);
      _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
      _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
      _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    }
#endif /* _MSC_VER >= 1400 */
#endif

  if (err)
    return svn_cmdline_handle_exit_error(err, pool, opts.prog_name);

  /* For efficient UTF8 handling (e.g. used by our file I/O routines). */
  svn_utf_initialize2(FALSE, pool);

  while (1)
    {
      const char *opt_arg;

      /* Parse the next option. */
      apr_err = apr_getopt_long(os, cl_options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err && (apr_err != APR_BADCH))
        {
          /* Ignore invalid option error to allow passing arbitrary options */
          fprintf(stderr, "apr_getopt_long failed : [%d] %s\n",
                  apr_err, apr_strerror(apr_err, errmsg, sizeof(errmsg)));
          exit(1);
        }

      switch (opt_id) {
        case help_opt:
          help(opts.prog_name, pool);
          exit(0);
        case cleanup_opt:
          cleanup_mode = TRUE;
          break;
        case config_opt:
          opts.config_file = apr_pstrdup(pool, opt_arg);
          break;
        case fstype_opt:
          opts.fs_type = apr_pstrdup(pool, opt_arg);
          break;
        case srcdir_opt:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&opts.srcdir, opt_arg, pool));
          opts.srcdir = svn_dirent_internal_style(opts.srcdir, pool);
          break;
        case reposdir_opt:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&opts.repos_dir, opt_arg, pool));
          opts.repos_dir = svn_dirent_internal_style(opts.repos_dir, pool);
          break;
        case reposurl_opt:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&opts.repos_url, opt_arg, pool));
          opts.repos_url = svn_uri_canonicalize(opts.repos_url, pool);
          break;
        case repostemplate_opt:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&opts.repos_template, opt_arg,
                                              pool));
          opts.repos_template = svn_dirent_internal_style(opts.repos_template,
                                                          pool);
          break;
        case memcached_server_opt:
          SVN_INT_ERR(svn_utf_cstring_to_utf8(&opts.memcached_server, opt_arg,
                                              pool));
          break;
        case list_opt:
          list_mode = TRUE;
          break;
        case mode_filter_opt:
          if (svn_cstring_casecmp(opt_arg, "PASS") == 0)
            mode_filter = svn_test_pass;
          else if (svn_cstring_casecmp(opt_arg, "XFAIL") == 0)
            mode_filter = svn_test_xfail;
          else if (svn_cstring_casecmp(opt_arg, "SKIP") == 0)
            mode_filter = svn_test_skip;
          else if (svn_cstring_casecmp(opt_arg, "ALL") == 0)
            mode_filter = svn_test_all;
          else
            {
              fprintf(stderr, "FAIL: Invalid --mode-filter option.  Try ");
              fprintf(stderr, " PASS, XFAIL, SKIP or ALL.\n");
              exit(1);
            }
          break;
        case verbose_opt:
          verbose_mode = TRUE;
          break;
        case quiet_opt:
          quiet_mode = TRUE;
          break;
        case allow_segfault_opt:
          allow_segfaults = TRUE;
          break;
        case server_minor_version_opt:
          {
            char *end;
            opts.server_minor_version = (int) strtol(opt_arg, &end, 10);
            if (end == opt_arg || *end != '\0')
              {
                fprintf(stderr, "FAIL: Non-numeric minor version given\n");
                exit(1);
              }
            if ((opts.server_minor_version < 3)
                || (opts.server_minor_version > SVN_VER_MINOR))
              {
                fprintf(stderr, "FAIL: Invalid minor version given\n");
                exit(1);
              }
            break;
          }
        case sqlite_log_opt:
          svn_sqlite__dbg_enable_errorlog();
          break;
#if APR_HAS_THREADS
        case parallel_opt:
          parallel = TRUE;
          break;
#endif
      }
    }
  opts.verbose = verbose_mode;

  /* Disable sleeping for timestamps, to speed up the tests. */
  apr_env_set(
         "SVN_I_LOVE_CORRUPTED_WORKING_COPIES_SO_DISABLE_SLEEP_FOR_TIMESTAMPS",
         "yes", pool);

  /* You can't be both quiet and verbose. */
  if (quiet_mode && verbose_mode)
    {
      fprintf(stderr, "FAIL: --verbose and --quiet are mutually exclusive\n");
      exit(1);
    }

  /* Create an iteration pool for the tests */
  set_cleanup_pool(svn_pool_create(pool));
  test_pool = svn_pool_create(pool);

  if (!allow_segfaults)
    svn_error_set_malfunction_handler(svn_error_raise_on_malfunction);

  if (argc >= 2)  /* notice command-line arguments */
    {
      if (! strcmp(argv[1], "list") || list_mode)
        {
          const char *header_msg;
          ran_a_test = TRUE;

          /* run all tests with MSG_ONLY set to TRUE */
          header_msg = "Test #  Mode   Test Description\n"
                       "------  -----  ----------------\n";
          for (i = 1; i <= array_size; i++)
            {
              if (do_test_num(opts.prog_name, i, test_funcs,
                              TRUE, &opts, &header_msg, test_pool))
                got_error = TRUE;

              /* Clear the per-function pool */
              svn_pool_clear(test_pool);
              svn_pool_clear(cleanup_pool);
            }
        }
      else
        {
          for (i = 1; i < argc; i++)
            {
              if (svn_ctype_isdigit(argv[i][0]) || argv[i][0] == '-')
                {
                  int test_num = atoi(argv[i]);
                  if (test_num == 0)
                    /* A --option argument, most likely. */
                    continue;

                  ran_a_test = TRUE;
                  if (do_test_num(opts.prog_name, test_num, test_funcs,
                                  FALSE, &opts, NULL, test_pool))
                    got_error = TRUE;

                  /* Clear the per-function pool */
                  svn_pool_clear(test_pool);
                  svn_pool_clear(cleanup_pool);
                }
            }
        }
    }

  if (! ran_a_test)
    {
      /* just run all tests */
      if (max_threads < 1)
        max_threads = array_size;

      if (max_threads == 1 || !parallel)
        {
          for (i = 1; i <= array_size; i++)
            {
              if (do_test_num(opts.prog_name, i, test_funcs,
                              FALSE, &opts, NULL, test_pool))
                got_error = TRUE;

              /* Clear the per-function pool */
              svn_pool_clear(test_pool);
              svn_pool_clear(cleanup_pool);
            }
        }
#if APR_HAS_THREADS
      else
        {
          got_error = do_tests_concurrently(opts.prog_name, test_funcs,
                                            array_size, max_threads,
                                            &opts, test_pool);

          /* Execute all cleanups */
          svn_pool_clear(test_pool);
          svn_pool_clear(cleanup_pool);
        }
#endif
    }

  /* Clean up APR */
  svn_pool_destroy(pool);      /* takes test_pool with it */
  apr_terminate();

  return got_error;
}


svn_boolean_t
svn_test__fs_type_is(const svn_test_opts_t *opts,
                     const char *predicate_value,
                     apr_pool_t *pool)
{
  return (0 == strcmp(predicate_value, opts->fs_type));
}

svn_boolean_t
svn_test__fs_type_not(const svn_test_opts_t *opts,
                      const char *predicate_value,
                      apr_pool_t *pool)
{
  return (0 != strcmp(predicate_value, opts->fs_type));
}
