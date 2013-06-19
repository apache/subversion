/* io-test.c --- tests for some i/o functions
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

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <apr.h>

#include "svn_pools.h"
#include "svn_string.h"
#include "private/svn_skel.h"

#include "../svn_test.h"
#include "../svn_test_fs.h"


/* Helpers to create the test data directory. */

#define TEST_DIR "io-test-temp"

/* The definition for the test data files. */
struct test_file_definition_t
  {
    /* The name of the test data file. */
    const char* const name;

    /* The string needs to contain up to 5 bytes, they
     * are interpreded as:
     * - first byte
     * - filler between first and medium byte
     * - medium byte (the byte in the middle of the file)
     * - filler between medium and last byte
     * - last byte.
     * If the string is shorter than the file length,
     * the test will fail. */
    const char* const data;

    /* The size of the file actually to create. */
    const apr_off_t size;

    /* The created path of the file. Will be filled in
     * by create_test_file() */
    char* created_path;
  };

struct test_file_definition_t test_file_definitions[] =
  {
    {"empty",                 "",      0},
    {"single_a",              "a",     1},
    {"single_b",              "b",     1},
    {"hundred_a",             "aaaaa", 100},
    {"hundred_b",             "bbbbb", 100},
    {"hundred_b1",            "baaaa", 100},
    {"hundred_b2",            "abaaa", 100},
    {"hundred_b3",            "aabaa", 100},
    {"hundred_b4",            "aaaba", 100},
    {"hundred_b5",            "aaaab", 100},
    {"chunk_minus_one_a",     "aaaaa", SVN__STREAM_CHUNK_SIZE - 1},
    {"chunk_minus_one_b1",    "baaaa", SVN__STREAM_CHUNK_SIZE - 1},
    {"chunk_minus_one_b2",    "abaaa", SVN__STREAM_CHUNK_SIZE - 1},
    {"chunk_minus_one_b3",    "aabaa", SVN__STREAM_CHUNK_SIZE - 1},
    {"chunk_minus_one_b4",    "aaaba", SVN__STREAM_CHUNK_SIZE - 1},
    {"chunk_minus_one_b5",    "aaaab", SVN__STREAM_CHUNK_SIZE - 1},
    {"chunk_a",               "aaaaa", SVN__STREAM_CHUNK_SIZE},
    {"chunk_b1",              "baaaa", SVN__STREAM_CHUNK_SIZE},
    {"chunk_b2",              "abaaa", SVN__STREAM_CHUNK_SIZE},
    {"chunk_b3",              "aabaa", SVN__STREAM_CHUNK_SIZE},
    {"chunk_b4",              "aaaba", SVN__STREAM_CHUNK_SIZE},
    {"chunk_b5",              "aaaab", SVN__STREAM_CHUNK_SIZE},
    {"chunk_plus_one_a",      "aaaaa", SVN__STREAM_CHUNK_SIZE + 1},
    {"chunk_plus_one_b1",     "baaaa", SVN__STREAM_CHUNK_SIZE + 1},
    {"chunk_plus_one_b2",     "abaaa", SVN__STREAM_CHUNK_SIZE + 1},
    {"chunk_plus_one_b3",     "aabaa", SVN__STREAM_CHUNK_SIZE + 1},
    {"chunk_plus_one_b4",     "aaaba", SVN__STREAM_CHUNK_SIZE + 1},
    {"chunk_plus_one_b5",     "aaaab", SVN__STREAM_CHUNK_SIZE + 1},
    {"twochunk_minus_one_a",  "aaaaa", SVN__STREAM_CHUNK_SIZE*2 - 1},
    {"twochunk_minus_one_b1", "baaaa", SVN__STREAM_CHUNK_SIZE*2 - 1},
    {"twochunk_minus_one_b2", "abaaa", SVN__STREAM_CHUNK_SIZE*2 - 1},
    {"twochunk_minus_one_b3", "aabaa", SVN__STREAM_CHUNK_SIZE*2 - 1},
    {"twochunk_minus_one_b4", "aaaba", SVN__STREAM_CHUNK_SIZE*2 - 1},
    {"twochunk_minus_one_b5", "aaaab", SVN__STREAM_CHUNK_SIZE*2 - 1},
    {"twochunk_a",            "aaaaa", SVN__STREAM_CHUNK_SIZE*2},
    {"twochunk_b1",           "baaaa", SVN__STREAM_CHUNK_SIZE*2},
    {"twochunk_b2",           "abaaa", SVN__STREAM_CHUNK_SIZE*2},
    {"twochunk_b3",           "aabaa", SVN__STREAM_CHUNK_SIZE*2},
    {"twochunk_b4",           "aaaba", SVN__STREAM_CHUNK_SIZE*2},
    {"twochunk_b5",           "aaaab", SVN__STREAM_CHUNK_SIZE*2},
    {"twochunk_plus_one_a",   "aaaaa", SVN__STREAM_CHUNK_SIZE*2 + 1},
    {"twochunk_plus_one_b1",  "baaaa", SVN__STREAM_CHUNK_SIZE*2 + 1},
    {"twochunk_plus_one_b2",  "abaaa", SVN__STREAM_CHUNK_SIZE*2 + 1},
    {"twochunk_plus_one_b3",  "aabaa", SVN__STREAM_CHUNK_SIZE*2 + 1},
    {"twochunk_plus_one_b4",  "aaaba", SVN__STREAM_CHUNK_SIZE*2 + 1},
    {"twochunk_plus_one_b5",  "aaaab", SVN__STREAM_CHUNK_SIZE*2 + 1},
    {0},
  };

/* Function to prepare a single test file */

static svn_error_t *
create_test_file(struct test_file_definition_t* definition,
                 apr_pool_t *pool,
                 apr_pool_t *scratch_pool)
{
  apr_status_t status = 0;
  apr_file_t *file_h;
  apr_off_t midpos = definition->size / 2;
  svn_error_t *err = NULL;
  int i;

  if (definition->size < 5)
    SVN_ERR_ASSERT(strlen(definition->data) >= (apr_size_t)definition->size);
  else
    SVN_ERR_ASSERT(strlen(definition->data) >= 5);


  definition->created_path = svn_dirent_join(TEST_DIR,
                                  definition->name,
                                  pool);

  SVN_ERR(svn_io_file_open(&file_h,
                           definition->created_path,
                           (APR_WRITE | APR_CREATE | APR_EXCL | APR_BUFFERED),
                           APR_OS_DEFAULT,
                           scratch_pool));

  for (i=1; i <= definition->size; i += 1)
    {
      char c;
      if (i == 1)
        c = definition->data[0];
      else if (i < midpos)
        c = definition->data[1];
      else if (i == midpos)
        c = definition->data[2];
      else if (i < definition->size)
        c = definition->data[3];
      else
        c = definition->data[4];

      status = apr_file_putc(c, file_h);

      if (status)
        break;
    }

  if (status)
    err = svn_error_wrap_apr(status, "Can't write to file '%s'",
                              definition->name);

  return svn_error_compose_create(err,
                        svn_io_file_close(file_h, scratch_pool));
}

/* Function to prepare the whole set of on-disk files to be compared. */
static svn_error_t *
create_comparison_candidates(apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  struct test_file_definition_t *candidate;
  svn_error_t *err = SVN_NO_ERROR;

  /* If there's already a directory named io-test-temp, delete it.
     Doing things this way means that repositories stick around after
     a failure for postmortem analysis, but also that tests can be
     re-run without cleaning out the repositories created by prior
     runs.  */
  SVN_ERR(svn_io_check_path(TEST_DIR, &kind, scratch_pool));

  if (kind == svn_node_dir)
    SVN_ERR(svn_io_remove_dir2(TEST_DIR, TRUE, NULL, NULL, scratch_pool));
  else if (kind != svn_node_none)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "There is already a file named '%s'",
                             TEST_DIR);

  SVN_ERR(svn_io_dir_make(TEST_DIR, APR_OS_DEFAULT, scratch_pool));

  svn_test_add_dir_cleanup(TEST_DIR);

  for (candidate = test_file_definitions;
       candidate->name != NULL;
       candidate += 1)
    {
      svn_pool_clear(iterpool);
      err = create_test_file(candidate, scratch_pool, iterpool);
      if (err)
        break;
    }

  svn_pool_destroy(iterpool);

  return err;
}


/* Functions to check the 2-way and 3-way file comparison functions.  */

/* Test 2-way file size checking */
static svn_error_t *
test_two_file_size_comparison(apr_pool_t *scratch_pool)
{
  struct test_file_definition_t *inner, *outer;
  svn_boolean_t actual;
  svn_boolean_t expected;
  svn_error_t *err = SVN_NO_ERROR;
  svn_error_t *cmp_err;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(create_comparison_candidates(scratch_pool));

  for (outer = test_file_definitions; outer->name != NULL; outer += 1)
    {
#ifdef SVN_IO_TEST_ALL_PERMUTATIONS
      inner = test_file_definitions;
#else
      inner = outer;
#endif
      for (; inner->name != NULL; inner += 1)
        {
          svn_pool_clear(iterpool);

          expected = inner->size != outer->size;

          cmp_err = svn_io_filesizes_different_p(&actual,
                                                 inner->created_path,
                                                 outer->created_path,
                                                 iterpool);

          if (cmp_err)
            {
              err = svn_error_compose_create(err, cmp_err);
            }
          else if (expected != actual)
            {
              err = svn_error_compose_create(err,
                  svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                   "size comparison problem: '%s' and '%s'",
                                   inner->created_path,
                                   outer->created_path));
            }
        }
    }

  svn_pool_destroy(iterpool);
  return err;
}


/* Test 2-way file content checking */
static svn_error_t *
test_two_file_content_comparison(apr_pool_t *scratch_pool)
{
  struct test_file_definition_t *inner, *outer;
  svn_boolean_t actual;
  svn_boolean_t expected;
  svn_error_t *err = SVN_NO_ERROR;
  svn_error_t *cmp_err;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(create_comparison_candidates(scratch_pool));

  for (outer = test_file_definitions; outer->name != NULL; outer += 1)
    {
#ifdef SVN_IO_TEST_ALL_PERMUTATIONS
      inner = test_file_definitions;
#else
      inner = outer;
#endif
      for (; inner->name != NULL; inner += 1)
        {
          svn_pool_clear(iterpool);

          expected = inner->size == outer->size
            && strcmp(inner->data, outer->data) == 0;

          cmp_err = svn_io_files_contents_same_p(&actual,
                                                 inner->created_path,
                                                 outer->created_path,
                                                 iterpool);

          if (cmp_err)
            {
              err = svn_error_compose_create(err, cmp_err);
            }
          else
            {
              if (expected != actual)
                  err = svn_error_compose_create(err,
                      svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                 "content comparison problem: '%s' and '%s'",
                                 inner->created_path,
                                 outer->created_path));
            }
        }
    }

  svn_pool_destroy(iterpool);
  return err;
}


/* Test 3-way file size checking */
static svn_error_t *
test_three_file_size_comparison(apr_pool_t *scratch_pool)
{
  struct test_file_definition_t *inner, *middle, *outer;
  svn_boolean_t actual12, actual23, actual13;
  svn_boolean_t expected12, expected23, expected13;
  svn_error_t *err = SVN_NO_ERROR;
  svn_error_t *cmp_err;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(create_comparison_candidates(scratch_pool));

  for (outer = test_file_definitions; outer->name != NULL; outer += 1)
    {
#ifdef SVN_IO_TEST_ALL_PERMUTATIONS
      middle = test_file_definitions;
#else
      middle = outer;
#endif
      for (; middle->name != NULL; middle += 1)
        {
#ifdef SVN_IO_TEST_ALL_PERMUTATIONS
          inner = test_file_definitions;
#else
          inner = middle;
#endif
          for (; inner->name != NULL; inner += 1)
            {
              svn_pool_clear(iterpool);

              expected12 = inner->size != middle->size;
              expected23 = middle->size != outer->size;
              expected13 = inner->size != outer->size;

              cmp_err = svn_io_filesizes_three_different_p(&actual12,
                                &actual23,
                                &actual13,
                                inner->created_path,
                                middle->created_path,
                                outer->created_path,
                                iterpool);

              if (cmp_err)
                {
                  err = svn_error_compose_create(err, cmp_err);
                }
              else
                {
                  if (expected12 != actual12)
                      err = svn_error_compose_create(err,
                          svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "size comparison problem: '%s' and '%s'",
                                     inner->created_path,
                                     middle->created_path));

                  if (expected23 != actual23)
                      err = svn_error_compose_create(err,
                          svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "size comparison problem: '%s' and '%s'",
                                     middle->created_path,
                                     outer->created_path));

                  if (expected13 != actual13)
                      err = svn_error_compose_create(err,
                          svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "size comparison problem: '%s' and '%s'",
                                     inner->created_path,
                                     outer->created_path));
                }
            }
        }
    }

  svn_pool_destroy(iterpool);

  return err;
}


/* Test 3-way file content checking */
static svn_error_t *
test_three_file_content_comparison(apr_pool_t *scratch_pool)
{
  struct test_file_definition_t *inner, *middle, *outer;
  svn_boolean_t actual12, actual23, actual13;
  svn_boolean_t expected12, expected23, expected13;
  svn_error_t *err = SVN_NO_ERROR;
  svn_error_t *cmp_err;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(create_comparison_candidates(scratch_pool));

  for (outer = test_file_definitions; outer->name != NULL; outer += 1)
    {
#ifdef SVN_IO_TEST_ALL_PERMUTATIONS
      middle = test_file_definitions;
#else
      middle = outer;
#endif
      for (; middle->name != NULL; middle += 1)
        {
#ifdef SVN_IO_TEST_ALL_PERMUTATIONS
          inner = test_file_definitions;
#else
          inner = middle;
#endif
          for (; inner->name != NULL; inner += 1)
            {
              svn_pool_clear(iterpool);

              expected12 = outer->size == middle->size
                && strcmp(outer->data, middle->data) == 0;
              expected23 = middle->size == inner->size
                && strcmp(middle->data, inner->data) == 0;
              expected13 = outer->size == inner->size
                && strcmp(outer->data, inner->data) == 0;

              cmp_err = svn_io_files_contents_three_same_p(&actual12,
                                &actual23,
                                &actual13,
                                outer->created_path,
                                middle->created_path,
                                inner->created_path,
                                iterpool);

              if (cmp_err)
                {
                  err = svn_error_compose_create(err, cmp_err);
                }
              else
                {
                  if (expected12 != actual12)
                      err = svn_error_compose_create(err,
                          svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "size comparison problem: '%s' and '%s'",
                                     inner->created_path,
                                     middle->created_path));

                  if (expected23 != actual23)
                      err = svn_error_compose_create(err,
                          svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "size comparison problem: '%s' and '%s'",
                                     middle->created_path,
                                     outer->created_path));

                  if (expected13 != actual13)
                      err = svn_error_compose_create(err,
                          svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                                     "size comparison problem: '%s' and '%s'",
                                     inner->created_path,
                                     outer->created_path));
                }
            }
        }
    }

  return err;
}

static svn_error_t *
read_length_line_shouldnt_loop(apr_pool_t *pool)
{
  const char *tmp_dir;
  const char *tmp_file;
  char buffer[4];
  apr_size_t buffer_limit = sizeof(buffer);
  apr_file_t *f;

  SVN_ERR(svn_dirent_get_absolute(&tmp_dir, "read_length_tmp", pool));
  SVN_ERR(svn_io_remove_dir2(tmp_dir, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_io_make_dir_recursively(tmp_dir, pool));
  svn_test_add_dir_cleanup(tmp_dir);

  SVN_ERR(svn_io_write_unique(&tmp_file, tmp_dir, "1234\r\n", 6,
                              svn_io_file_del_on_pool_cleanup, pool));

  SVN_ERR(svn_io_file_open(&f, tmp_file, APR_READ, APR_OS_DEFAULT, pool));

  SVN_TEST_ASSERT_ERROR(svn_io_read_length_line(f, buffer, &buffer_limit,
                                                pool), SVN_ERR_MALFORMED_FILE);
  SVN_TEST_ASSERT(buffer_limit == 4);

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_two_file_size_comparison,
                   "two file size comparison"),
    SVN_TEST_PASS2(test_two_file_content_comparison,
                   "two file content comparison"),
    SVN_TEST_PASS2(test_three_file_size_comparison,
                   "three file size comparison"),
    SVN_TEST_PASS2(test_three_file_content_comparison,
                   "three file content comparison"),
    SVN_TEST_PASS2(read_length_line_shouldnt_loop,
                   "svn_io_read_length_line() shouldn't loop"),
    SVN_TEST_NULL
  };
