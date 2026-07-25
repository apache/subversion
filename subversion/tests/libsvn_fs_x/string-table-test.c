/* string-table-test.c --- tests for string tables
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

#include "../svn_test.h"
#include "../../libsvn_fs_x/string_table.h"
#include "svn_pools.h"
#include "svn_sorts.h"

/* Some tests use this list of strings as is.  They are all "short strings"
 * in the terminology of string tables.  We use them also as an input to
 * generate strings of arbitrary length.
 */
enum { STRING_COUNT = 12 };
static const char *basic_strings[STRING_COUNT] =
  {
     "some string",
     "this is another string",
     "this is a duplicate",
     "some longer string",
     "this is a very long string",
     "and here is another",
     "this is a duplicate",
     "/some/path/to/a/dir",
     "/some/path/to/a/file",
     "/some/other/dir",
     "/some/other/file",
     ""
  };

/* Generate a string of exactly LEN chars (plus terminating NUL).  KEY is
 * an arbitrary integer that will be transformed into a character sequence
 * using entries of BASIC_STRINGS.  The result will be allocated in POOL.
 */
static svn_stringbuf_t *
generate_string(apr_uint64_t key, apr_size_t len, apr_pool_t *pool)
{
  svn_stringbuf_t *result = svn_stringbuf_create_ensure(len, pool);
  apr_uint64_t temp = key;
  apr_uint64_t run = 0;

  while (len)
    {
      apr_size_t idx;
      apr_size_t add_len;

      if (temp == 0)
        {
          temp = key;
          run++;
        }

      idx = (temp + run) % STRING_COUNT;
      temp /= STRING_COUNT;

      add_len = strlen(basic_strings[idx]);
      add_len = MIN(len, add_len);

      svn_stringbuf_appendbytes(result, basic_strings[idx], add_len);
      len -= add_len;
    }

  return result;
}

static svn_error_t *
store_and_load_table(string_table_t **table, apr_pool_t *pool)
{
  svn_stringbuf_t *stream_buffer = svn_stringbuf_create_empty(pool);
  svn_stream_t *stream;

  stream = svn_stream_from_stringbuf(stream_buffer, pool);
  SVN_ERR(svn_fs_x__write_string_table(stream, *table, pool));
  SVN_ERR(svn_stream_close(stream));

  *table = NULL;

  stream = svn_stream_from_stringbuf(stream_buffer, pool);
  SVN_ERR(svn_fs_x__read_string_table(table, stream, pool, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}

static svn_error_t *
create_empty_table_body(svn_boolean_t do_load_store,
                        apr_pool_t *pool)
{
  string_table_builder_t *builder
    = svn_fs_x__string_table_builder_create(pool);
  string_table_t *table
    = svn_fs_x__string_table_create(builder, pool);

  SVN_TEST_STRING_ASSERT(svn_fs_x__string_table_get(table, 0, NULL, pool), "");

  if (do_load_store)
    SVN_ERR(store_and_load_table(&table, pool));

  SVN_TEST_STRING_ASSERT(svn_fs_x__string_table_get(table, 0, NULL, pool), "");

  return SVN_NO_ERROR;
}

static svn_error_t *
short_string_table_body(svn_boolean_t do_load_store,
                        apr_pool_t *pool)
{
  apr_size_t indexes[STRING_COUNT] = { 0 };

  string_table_builder_t *builder;
  string_table_t *table;
  int i;

  builder = svn_fs_x__string_table_builder_create(pool);
  for (i = 0; i < STRING_COUNT; ++i)
    indexes[i] = svn_fs_x__string_table_builder_add(builder, basic_strings[i], 0);

  table = svn_fs_x__string_table_create(builder, pool);
  if (do_load_store)
    SVN_ERR(store_and_load_table(&table, pool));

  SVN_TEST_ASSERT(indexes[2] == indexes[6]);
  for (i = 0; i < STRING_COUNT; ++i)
    {
      apr_size_t len;
      const char *string
        = svn_fs_x__string_table_get(table, indexes[i], &len, pool);

      SVN_TEST_STRING_ASSERT(string, basic_strings[i]);
      SVN_TEST_ASSERT(len == strlen(string));
      SVN_TEST_ASSERT(len == strlen(basic_strings[i]));
    }

  SVN_TEST_STRING_ASSERT(svn_fs_x__string_table_get(table, STRING_COUNT,
                                                    NULL, pool), "");

  return SVN_NO_ERROR;
}

static svn_error_t *
large_string_table_body(svn_boolean_t do_load_store,
                        apr_pool_t *pool)
{
  enum { COUNT = 10 };

  svn_stringbuf_t *strings[COUNT] = { 0 };
  apr_size_t indexes[COUNT] = { 0 };

  string_table_builder_t *builder;
  string_table_t *table;
  int i;

  builder = svn_fs_x__string_table_builder_create(pool);
  for (i = 0; i < COUNT; ++i)
    {
      strings[i] = generate_string(APR_UINT64_C(0x1234567876543210) * (i + 1),
                                   73000 + 1000 * i,  pool);
      indexes[i] = svn_fs_x__string_table_builder_add(builder,
                                                      strings[i]->data,
                                                      strings[i]->len);
    }

  table = svn_fs_x__string_table_create(builder, pool);
  if (do_load_store)
    SVN_ERR(store_and_load_table(&table, pool));

  for (i = 0; i < COUNT; ++i)
    {
      apr_size_t len;
      const char *string
        = svn_fs_x__string_table_get(table, indexes[i], &len, pool);

      SVN_TEST_STRING_ASSERT(string, strings[i]->data);
      SVN_TEST_ASSERT(len == strlen(string));
      SVN_TEST_ASSERT(len == strings[i]->len);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
many_strings_table_body(svn_boolean_t do_load_store,
                        apr_pool_t *pool)
{
  /* cause multiple sub-tables (6 to be exact) to be created */
  enum { COUNT = 100 };

  svn_stringbuf_t *strings[COUNT] = { 0 };
  apr_size_t indexes[COUNT] = { 0 };

  string_table_builder_t *builder;
  string_table_t *table;
  int i;

  builder = svn_fs_x__string_table_builder_create(pool);
  for (i = 0; i < COUNT; ++i)
    {
      strings[i] = generate_string(APR_UINT64_C(0x1234567876543210) * (i + 1),
                                   (i * i) % 23000,  pool);
      indexes[i] = svn_fs_x__string_table_builder_add(builder,
                                                      strings[i]->data,
                                                      strings[i]->len);
    }

  table = svn_fs_x__string_table_create(builder, pool);
  if (do_load_store)
    SVN_ERR(store_and_load_table(&table, pool));

  for (i = 0; i < COUNT; ++i)
    {
      apr_size_t len;
      const char *string
        = svn_fs_x__string_table_get(table, indexes[i], &len, pool);

      SVN_TEST_STRING_ASSERT(string, strings[i]->data);
      SVN_TEST_ASSERT(len == strlen(string));
      SVN_TEST_ASSERT(len == strings[i]->len);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
create_empty_table(apr_pool_t *pool)
{
  return svn_error_trace(create_empty_table_body(FALSE, pool));
}

static svn_error_t *
short_string_table(apr_pool_t *pool)
{
  return svn_error_trace(short_string_table_body(FALSE, pool));
}

static svn_error_t *
large_string_table(apr_pool_t *pool)
{
  return svn_error_trace(large_string_table_body(FALSE, pool));
}

static svn_error_t *
many_strings_table(apr_pool_t *pool)
{
  return svn_error_trace(many_strings_table_body(FALSE, pool));
}

static svn_error_t *
store_load_short_string_table(apr_pool_t *pool)
{
  return svn_error_trace(short_string_table_body(TRUE, pool));
}

static svn_error_t *
store_load_large_string_table(apr_pool_t *pool)
{
  return svn_error_trace(large_string_table_body(TRUE, pool));
}

static svn_error_t *
store_load_empty_table(apr_pool_t *pool)
{
  return svn_error_trace(create_empty_table_body(TRUE, pool));
}

static svn_error_t *
store_load_many_strings_table(apr_pool_t *pool)
{
  return svn_error_trace(many_strings_table_body(TRUE, pool));
}


/* ------------------------------------------------------------------------ */

/* The test table.  */

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(create_empty_table,
                   "create an empty string table"),
    SVN_TEST_PASS2(short_string_table,
                   "string table with short strings only"),
    SVN_TEST_PASS2(large_string_table,
                   "string table with large strings only"),
    SVN_TEST_PASS2(many_strings_table,
                   "string table with many strings"),
    SVN_TEST_PASS2(store_load_empty_table,
                   "store and load an empty string table"),
    SVN_TEST_PASS2(store_load_short_string_table,
                   "store and load table with short strings only"),
    SVN_TEST_PASS2(store_load_large_string_table,
                   "store and load table with large strings only"),
    SVN_TEST_PASS2(store_load_many_strings_table,
                   "store and load string table with many strings"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
