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
#include "../../libsvn_fs_fs/string_table.h"
#include "svn_pools.h"
#include "svn_sorts.h"

/* Some tests use this list of strings as is.  They are all "short strings"
 * in the terminology of string tables.  We use them also as an input to
 * generate strings of arbitrary length.
 */
enum { STRING_COUNT = 12 };
const char *basic_strings[STRING_COUNT] =
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
create_empty_table(apr_pool_t *pool)
{
  string_table_builder_t *builder
    = svn_fs_fs__string_table_builder_create(pool);
  string_table_t *table
    = svn_fs_fs__string_table_create(builder, pool);

  SVN_TEST_STRING_ASSERT(svn_fs_fs__string_table_get(table, 0, pool), "");
  SVN_TEST_ASSERT(svn_fs_fs__string_table_copy_string(NULL, 0, table, 0) == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
short_string_table(apr_pool_t *pool)
{
  apr_size_t indexes[STRING_COUNT] = { 0 };
    
  string_table_builder_t *builder;
  string_table_t *table;
  int i;
  
  builder = svn_fs_fs__string_table_builder_create(pool);
  for (i = 0; i < STRING_COUNT; ++i)
    indexes[i] = svn_fs_fs__string_table_builder_add(builder, basic_strings[i], 0);
  
  table = svn_fs_fs__string_table_create(builder, pool);
  
  SVN_TEST_ASSERT(indexes[2] == indexes[6]);
  for (i = 0; i < STRING_COUNT; ++i)
    {
      char long_buffer[100] = { 0 };
      char short_buffer[10] = { 0 };
      const char *string
        = svn_fs_fs__string_table_get(table, indexes[i], pool);
      apr_size_t len
        = svn_fs_fs__string_table_copy_string(NULL, 0, table, indexes[i]);
      apr_size_t long_len
        = svn_fs_fs__string_table_copy_string(long_buffer,
                                              sizeof(long_buffer),
                                              table, indexes[i]);
      apr_size_t short_len
        = svn_fs_fs__string_table_copy_string(short_buffer,
                                              sizeof(short_buffer),
                                              table, indexes[i]);

      SVN_TEST_STRING_ASSERT(string, basic_strings[i]);
      SVN_TEST_ASSERT(len == strlen(basic_strings[i]));
      SVN_TEST_ASSERT(long_len == strlen(basic_strings[i]));
      SVN_TEST_ASSERT(short_len == strlen(basic_strings[i]));

      SVN_TEST_STRING_ASSERT(long_buffer, basic_strings[i]);
      SVN_TEST_STRING_ASSERT(short_buffer, "");
    }

  SVN_TEST_STRING_ASSERT(svn_fs_fs__string_table_get(table, STRING_COUNT, pool), "");
  SVN_TEST_ASSERT(svn_fs_fs__string_table_copy_string(NULL, 0, table, STRING_COUNT) == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
large_string_table(apr_pool_t *pool)
{
  enum { COUNT = 10 };

  svn_stringbuf_t *strings[COUNT] = { 0 };
  apr_size_t indexes[COUNT] = { 0 };

  string_table_builder_t *builder;
  string_table_t *table;
  int i;

  builder = svn_fs_fs__string_table_builder_create(pool);
  for (i = 0; i < COUNT; ++i)
    {
      strings[i] = generate_string(0x1234567876543210ull * (i + 1), 73000 + 1000 * i,  pool);
      indexes[i] = svn_fs_fs__string_table_builder_add(builder, strings[i]->data, strings[i]->len);
    }

  table = svn_fs_fs__string_table_create(builder, pool);
  for (i = 0; i < COUNT; ++i)
    {
      char long_buffer[73000 + 1000 * COUNT] = { 0 };
      char short_buffer[100] = { 0 };
      const char *string
        = svn_fs_fs__string_table_get(table, indexes[i], pool);
      apr_size_t len
        = svn_fs_fs__string_table_copy_string(NULL, 0, table, indexes[i]);
      apr_size_t long_len
        = svn_fs_fs__string_table_copy_string(long_buffer,
                                              sizeof(long_buffer),
                                              table, indexes[i]);
      apr_size_t short_len
        = svn_fs_fs__string_table_copy_string(short_buffer,
                                              sizeof(short_buffer),
                                              table, indexes[i]);

      SVN_TEST_STRING_ASSERT(string, strings[i]->data);
      SVN_TEST_ASSERT(len == strings[i]->len);
      SVN_TEST_ASSERT(long_len == strings[i]->len);
      SVN_TEST_ASSERT(short_len == strings[i]->len);

      SVN_TEST_STRING_ASSERT(long_buffer, strings[i]->data);
      SVN_TEST_STRING_ASSERT(short_buffer, "");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
many_strings_table(apr_pool_t *pool)
{
  /* cause multiple sub-tables to be created */
  enum { COUNT = 1000 };

  svn_stringbuf_t *strings[COUNT] = { 0 };
  apr_size_t indexes[COUNT] = { 0 };

  string_table_builder_t *builder;
  string_table_t *table;
  int i;

  builder = svn_fs_fs__string_table_builder_create(pool);
  for (i = 0; i < COUNT; ++i)
    {
      strings[i] = generate_string(0x1234567876543210ull * (i + 1), (i * i) % 23000,  pool);
      indexes[i] = svn_fs_fs__string_table_builder_add(builder, strings[i]->data, strings[i]->len);
    }

  table = svn_fs_fs__string_table_create(builder, pool);
  for (i = 0; i < COUNT; ++i)
    {
      char long_buffer[23000] = { 0 };
      char short_buffer[100] = { 0 };
      const char *string
        = svn_fs_fs__string_table_get(table, indexes[i], pool);
      apr_size_t len
        = svn_fs_fs__string_table_copy_string(NULL, 0, table, indexes[i]);
      apr_size_t long_len
        = svn_fs_fs__string_table_copy_string(long_buffer,
                                              sizeof(long_buffer),
                                              table, indexes[i]);
      apr_size_t short_len
        = svn_fs_fs__string_table_copy_string(short_buffer,
                                              sizeof(short_buffer),
                                              table, indexes[i]);

      SVN_TEST_STRING_ASSERT(string, strings[i]->data);
      SVN_TEST_ASSERT(len == strings[i]->len);
      SVN_TEST_ASSERT(long_len == strings[i]->len);
      SVN_TEST_ASSERT(short_len == strings[i]->len);

      SVN_TEST_STRING_ASSERT(long_buffer, strings[i]->data);
      if (len > sizeof(short_buffer))
        SVN_TEST_STRING_ASSERT(short_buffer, "");
    }

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
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
    SVN_TEST_NULL
  };
