/*
 * ====================================================================
 * Copyright (c) 2000-2004, 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_TEST_H
#define SVN_TEST_H

#ifndef SVN_ENABLE_DEPRECATION_WARNINGS_IN_TESTS
#undef SVN_DEPRECATED
#define SVN_DEPRECATED
#endif /* ! SVN_ENABLE_DEPRECATION_WARNINGS_IN_TESTS */

#include <apr_pools.h>

#include "svn_delta.h"
#include "svn_path.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Handy macro to test a condition, returning SVN_ERR_TEST_FAILED if FALSE
 *
 * This macro should be used in place of SVN_ERR_ASSERT() since we don't
 * want to core-dump the test.
 */
#define SVN_TEST_ASSERT(expr)                                  \
  do {                                                         \
    if (!(expr))                                               \
      return svn_error_create(SVN_ERR_TEST_FAILED, 0, #expr);  \
  } while (0)


/* Baton for any arguments that need to be passed from main() to svn
 * test functions.
 */
typedef struct svn_test_opts_t
{
  /* Description of the fs backend that should be used for testing. */
  const char *fs_type;
  /* Config file. */
  const char *config_file;
  /* Minor version to use for servers and FS backends, or zero to use
     the current latest version. */
  int server_minor_version;
  /* Add future "arguments" here. */
} svn_test_opts_t;

/* Prototype for test driver functions. */
typedef svn_error_t* (*svn_test_driver2_t)(apr_pool_t *pool);

/* Prototype for test driver functions which need options. */
typedef svn_error_t* (*svn_test_driver_opts_t)(const svn_test_opts_t *opts,
                                               apr_pool_t *pool);

/* Test modes. */
enum svn_test_mode_t
  {
    svn_test_pass,
    svn_test_xfail,
    svn_test_skip
  };

/* Each test gets a test descriptor, holding the function and other
 * associated data.
 */
struct svn_test_descriptor_t
{
  /* Is the test marked XFAIL? */
  enum svn_test_mode_t mode;

  /* A pointer to the test driver function. */
  svn_test_driver2_t func2;

  /* A pointer to the test driver function. */
  svn_test_driver_opts_t func_opts;

  /* A descriptive message for this test. */
  const char *msg;

  /* An optional description of a work-in-progress test. */
  const char *wip;
};

/* All Subversion test programs include an array of svn_test_descriptor_t's
 * (all of our sub-tests) that begins and ends with a SVN_TEST_NULL entry.
 */
extern struct svn_test_descriptor_t test_funcs[];

/* A null initializer for the test descriptor. */
#define SVN_TEST_NULL  {0}

/* Initializer for PASS tests */
#define SVN_TEST_PASS2(func, msg)  {svn_test_pass, func, NULL, msg}

/* Initializer for XFAIL tests */
#define SVN_TEST_XFAIL2(func, msg) {svn_test_xfail, func, NULL, msg}

/* Initializer for conditional XFAIL tests */
#define SVN_TEST_XFAIL_COND2(func, p, msg) \
  {(p) ? svn_test_xfail : svn_test_pass, func, NULL, msg}

/* Initializer for SKIP tests */
#define SVN_TEST_SKIP2(func, p, msg) \
  {(p) ? svn_test_skip : svn_test_pass, func, NULL, msg}

/* Similar macros, but for tests needing options.  */
#define SVN_TEST_OPTS_PASS(func, msg)  {svn_test_pass, NULL, func, msg}
#define SVN_TEST_OPTS_XFAIL(func, msg) {svn_test_xfail, NULL, func, msg}
#define SVN_TEST_OPTS_XFAIL_COND(func, p, msg) \
  {(p) ? svn_test_xfail : svn_test_pass, NULL, func, msg}
#define SVN_TEST_OPTS_SKIP(func, p, msg) \
  {(p) ? svn_test_skip : svn_test_pass, NULL, func, msg}

/* Initializer for XFAIL tests for works-in-progress. */
#define SVN_TEST_WIMP(func, msg, wip) \
  {svn_test_xfail, func, NULL, msg, wip}
#define SVN_TEST_WIMP_COND(func, p, msg, wip) \
  {(p) ? svn_test_xfail : svn_test_pass, func, NULL, msg, wip}
#define SVN_TEST_OPTS_WIMP(func, msg, wip) \
  {svn_test_xfail, NULL, func, msg, wip}
#define SVN_TEST_OPTS_WIMP_COND(func, p, msg, wip) \
  {(p) ? svn_test_xfail : svn_test_pass, NULL, func, msg, wip}



/* Return a pseudo-random number based on SEED, and modify SEED.
 *
 * This is a "good" pseudo-random number generator, intended to replace
 * all those "bad" rand() implementations out there.
 */
apr_uint32_t svn_test_rand(apr_uint32_t *seed);


/* Add PATH to the test cleanup list.  */
void svn_test_add_dir_cleanup(const char *path);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TEST_H */
