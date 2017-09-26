/*
 * packed-data-test.c:  a collection of svn_packed__* tests
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
#include "svn_string.h"   /* This includes <apr_*.h> */
#include "private/svn_packed_data.h"

/* Take the WRITE_ROOT, serialize its contents, parse it again into a new
 * data root and return it in *READ_ROOT.  Allocate it in POOL.
 */
static svn_error_t*
get_read_root(svn_packed__data_root_t **read_root,
              svn_packed__data_root_t *write_root,
              apr_pool_t *pool)
{
  svn_stringbuf_t *stream_buffer = svn_stringbuf_create_empty(pool);
  svn_stream_t *stream;

  stream = svn_stream_from_stringbuf(stream_buffer, pool);
  SVN_ERR(svn_packed__data_write(stream, write_root, pool));
  SVN_ERR(svn_stream_close(stream));

  stream = svn_stream_from_stringbuf(stream_buffer, pool);
  SVN_ERR(svn_packed__data_read(read_root, stream, pool, pool));
  SVN_ERR(svn_stream_close(stream));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_empty_container(apr_pool_t *pool)
{
  /* create an empty, readable container */
  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);
  SVN_ERR(get_read_root(&root, root, pool));

  /* there should be no sub-streams */
  SVN_TEST_ASSERT(svn_packed__first_int_stream(root) == NULL);
  SVN_TEST_ASSERT(svn_packed__first_byte_stream(root) == NULL);

  return SVN_NO_ERROR;
}

/* Check that COUNT numbers from VALUES can be written as uints to a
 * packed data stream and can be read from that stream again.  Deltify
 * data in the stream if DIFF is set.  Use POOL for allocations.
 */
static svn_error_t *
verify_uint_stream(const apr_uint64_t *values,
                   apr_size_t count,
                   svn_boolean_t diff,
                   apr_pool_t *pool)
{
  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);
  svn_packed__int_stream_t *stream
    = svn_packed__create_int_stream(root, diff, FALSE);

  apr_size_t i;
  for (i = 0; i < count; ++i)
    svn_packed__add_uint(stream, values[i]);

  SVN_ERR(get_read_root(&root, root, pool));

  /* the container should contain exactly one int stream */
  stream = svn_packed__first_int_stream(root);
  SVN_TEST_ASSERT(stream);
  SVN_TEST_ASSERT(!svn_packed__next_int_stream(stream));
  SVN_TEST_ASSERT(!svn_packed__first_byte_stream(root));

  /* the stream shall contain exactly the items we put into it */
  SVN_TEST_ASSERT(svn_packed__int_count(stream) == count);
  for (i = 0; i < count; ++i)
    SVN_TEST_ASSERT(svn_packed__get_uint(stream) == values[i]);

  /* reading beyond eos should return 0 values */
  SVN_TEST_ASSERT(svn_packed__get_uint(stream) == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_uint_stream(apr_pool_t *pool)
{
  enum { COUNT = 8 };
  const apr_uint64_t values[COUNT] =
  {
    APR_UINT64_MAX,
    0,
    APR_UINT64_MAX,
    APR_UINT64_C(0x8000000000000000),
    0,
    APR_UINT64_C(0x7fffffffffffffff),
    APR_UINT64_C(0x1234567890abcdef),
    APR_UINT64_C(0x0fedcba987654321),
  };

  SVN_ERR(verify_uint_stream(values, COUNT, FALSE, pool));
  SVN_ERR(verify_uint_stream(values, COUNT, TRUE, pool));

  return SVN_NO_ERROR;
}

/* Check that COUNT numbers from VALUES can be written as signed ints to a
 * packed data stream and can be read from that stream again.  Deltify
 * data in the stream if DIFF is set.  Use POOL for allocations.
 */
static svn_error_t *
verify_int_stream(const apr_int64_t *values,
                  apr_size_t count,
                  svn_boolean_t diff,
                  apr_pool_t *pool)
{
  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);
  svn_packed__int_stream_t *stream
    = svn_packed__create_int_stream(root, diff, TRUE);

  apr_size_t i;
  for (i = 0; i < count; ++i)
    svn_packed__add_int(stream, values[i]);

  SVN_ERR(get_read_root(&root, root, pool));

  /* the container should contain exactly one int stream */
  stream = svn_packed__first_int_stream(root);
  SVN_TEST_ASSERT(stream);
  SVN_TEST_ASSERT(!svn_packed__next_int_stream(stream));
  SVN_TEST_ASSERT(!svn_packed__first_byte_stream(root));

  /* the stream shall contain exactly the items we put into it */
  SVN_TEST_ASSERT(svn_packed__int_count(stream) == count);
  for (i = 0; i < count; ++i)
    SVN_TEST_ASSERT(svn_packed__get_int(stream) == values[i]);

  /* reading beyond eos should return 0 values */
  SVN_TEST_ASSERT(svn_packed__get_int(stream) == 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_int_stream(apr_pool_t *pool)
{
  enum { COUNT = 7 };
  const apr_int64_t values[COUNT] =
  {
     APR_INT64_MAX, /* extreme value */
     APR_INT64_MIN, /* other extreme, creating maximum delta to predecessor */
     0,             /* delta to predecessor > APR_INT64_MAX */
     APR_INT64_MAX, /* max value, again */
    -APR_INT64_MAX, /* _almost_ min value, almost max delta */
     APR_INT64_C(0x1234567890abcdef),  /* some arbitrary value */
    -APR_INT64_C(0x0fedcba987654321),  /* arbitrary value, different sign */
  };

  SVN_ERR(verify_int_stream(values, COUNT, FALSE, pool));
  SVN_ERR(verify_int_stream(values, COUNT, TRUE, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_byte_stream(apr_pool_t *pool)
{
  enum { COUNT = 6 };
  const svn_string_t values[COUNT] =
  {
    { "", 0 },
    { "\0", 1 },
    { "\0", 1 },
    { "some text", 9 },
    { "", 0 },
    { "some more", 9 }
  };

  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);
  svn_packed__byte_stream_t *stream
    = svn_packed__create_bytes_stream(root);

  apr_size_t i;
  for (i = 0; i < COUNT; ++i)
    svn_packed__add_bytes(stream, values[i].data, values[i].len);

  SVN_ERR(get_read_root(&root, root, pool));

  /* the container should contain exactly one byte stream */
  stream = svn_packed__first_byte_stream(root);
  SVN_TEST_ASSERT(stream);
  SVN_TEST_ASSERT(!svn_packed__next_byte_stream(stream));

  /* the stream shall contain exactly the items we put into it */
  SVN_TEST_ASSERT(svn_packed__byte_count(stream) == 20);
  SVN_TEST_ASSERT(svn_packed__byte_block_count(stream) == COUNT);
  for (i = 0; i < COUNT; ++i)
    {
      svn_string_t string;
      string.data = svn_packed__get_bytes(stream, &string.len);

      SVN_TEST_ASSERT(string.len == values[i].len);
      SVN_TEST_ASSERT(!memcmp(string.data, values[i].data, string.len));
    }

  /* reading beyond eos should return 0 values */
  SVN_TEST_ASSERT(svn_packed__byte_count(stream) == 0);

  return SVN_NO_ERROR;
}

/* Some simple structure that we use as sub-structure to BASE_RECORD_T.
 * Have it contain numbers and strings.
 */
typedef struct sub_record_t
{
  int sub_counter;
  svn_string_t text;
} sub_record_t;

/* signed / unsigned, 64 bits and shorter, diff-able and not, multiple
 * strings, multiple sub-records. */
typedef struct base_record_t
{
  int counter;
  svn_string_t description;
  apr_uint64_t large_unsigned1;
  apr_uint64_t large_unsigned2;
  const sub_record_t *left_subs;
  apr_int64_t large_signed1;
  apr_int64_t large_signed2;
  unsigned prime;
  const sub_record_t *right_subs;
  svn_string_t binary;
} base_record_t;

/* our test data */
enum {SUB_RECORD_COUNT = 7};
enum {BASE_RECORD_COUNT = 4};

static const sub_record_t sub_records[SUB_RECORD_COUNT] =
{
  { 6, { "this is quite a longish piece of text", 37} },
  { 5, { "x", 1} },
  { 4, { "not empty", 9} },
  { 3, { "another bit of text", 19} },
  { 2, { "", 0} },
  { 1, { "first sub-record", 16} },
  { 0 }
};

static const base_record_t test_data[BASE_RECORD_COUNT] =
{
  { 1, { "maximum", 7},
    APR_UINT64_MAX, APR_UINT64_MAX, sub_records,
    APR_INT64_MAX,  APR_INT64_MAX, 9967, sub_records + 1,
    { "\0\1\2\3\4\5\6\7\x8\x9\xa", 11} },

  { 2, { "minimum", 7},
    0, 0, sub_records + 6,
    APR_INT64_MIN, APR_INT64_MIN, 6029, sub_records + 5,
    { "X\0\0Y", 4} },

  { 3, { "mean", 4},
    APR_UINT64_C(0x8000000000000000), APR_UINT64_C(0x8000000000000000),
                                      sub_records + 2,
    0, 0, 653, sub_records + 3,
    { "\xff\0\1\2\3\4\5\6\7\x8\x9\xa", 12} },

  { 4, { "random", 6},
    APR_UINT64_C(0x1234567890abcdef), APR_UINT64_C(0xfedcba987654321),
                                      sub_records + 4,
    APR_INT64_C(0x1234567890abcd), APR_INT64_C(-0xedcba987654321), 7309,
                                   sub_records + 1,
    { "\x80\x7f\0\1\6", 5} }
};

/* Serialize RECORDS into INT_STREAM and TEXT_STREAM.  Stop when the
 * current record's SUB_COUNTER is 0.
 */
static unsigned
pack_subs(svn_packed__int_stream_t *int_stream,
          svn_packed__byte_stream_t *text_stream,
          const sub_record_t *records)
{
  unsigned count;
  for (count = 0; records[count].sub_counter; ++count)
    {
      svn_packed__add_int(int_stream, records[count].sub_counter);
      svn_packed__add_bytes(text_stream,
                            records[count].text.data,
                            records[count].text.len);
    }

  return count;
}

/* Serialize COUNT records starting from DATA into a packed data container
 * allocated in POOL and return the container root.
 */
static svn_packed__data_root_t *
pack(const base_record_t *data,
     apr_size_t count,
     apr_pool_t *pool)
{
  apr_size_t i;
  svn_packed__data_root_t *root = svn_packed__data_create_root(pool);
  svn_packed__int_stream_t *base_stream
    = svn_packed__create_int_stream(root, FALSE, FALSE);
  svn_packed__int_stream_t *sub_count_stream
    = svn_packed__create_int_stream(root, TRUE, FALSE);

  svn_packed__int_stream_t *left_sub_stream
    = svn_packed__create_int_stream(root, FALSE, TRUE);
  svn_packed__int_stream_t *right_sub_stream
    = svn_packed__create_int_stream(root, FALSE, TRUE);

  svn_packed__byte_stream_t *base_description_stream
    = svn_packed__create_bytes_stream(root);
  svn_packed__byte_stream_t *base_binary_stream
    = svn_packed__create_bytes_stream(root);
  svn_packed__byte_stream_t *sub_text_stream
    = svn_packed__create_bytes_stream(root);

  svn_packed__create_int_substream(base_stream, TRUE, TRUE);   /* counter */
  svn_packed__create_int_substream(base_stream, TRUE, FALSE);  /* large_unsigned1 */
  svn_packed__create_int_substream(base_stream, FALSE, FALSE); /* large_unsigned2 */
  svn_packed__create_int_substream(base_stream, TRUE, TRUE);   /* large_signed1 */
  svn_packed__create_int_substream(base_stream, FALSE, TRUE);  /* large_signed2 */
  svn_packed__create_int_substream(base_stream, TRUE, FALSE);  /* prime */

  for (i = 0; i < count; ++i)
    {
      svn_packed__add_int(base_stream, data[i].counter);
      svn_packed__add_bytes(base_description_stream,
                            data[i].description.data,
                            data[i].description.len);
      svn_packed__add_uint(base_stream, data[i].large_unsigned1);
      svn_packed__add_uint(base_stream, data[i].large_unsigned2);
      svn_packed__add_uint(sub_count_stream,
                           pack_subs(left_sub_stream, sub_text_stream,
                                     data[i].left_subs));

      svn_packed__add_int(base_stream, data[i].large_signed1);
      svn_packed__add_int(base_stream, data[i].large_signed2);
      svn_packed__add_uint(base_stream, data[i].prime);
      svn_packed__add_uint(sub_count_stream,
                           pack_subs(right_sub_stream, sub_text_stream,
                                     data[i].right_subs));

      svn_packed__add_bytes(base_binary_stream,
                            data[i].binary.data,
                            data[i].binary.len);
    }

  return root;
}

/* Deserialize COUNT records from INT_STREAM and TEXT_STREAM and return
 * the result allocated in POOL.
 */
static sub_record_t *
unpack_subs(svn_packed__int_stream_t *int_stream,
            svn_packed__byte_stream_t *text_stream,
            apr_size_t count,
            apr_pool_t *pool)
{
  sub_record_t *records = apr_pcalloc(pool, (count + 1) * sizeof(*records));

  apr_size_t i;
  for (i = 0; i < count; ++i)
    {
      records[i].sub_counter = (int) svn_packed__get_int(int_stream);
      records[i].text.data = svn_packed__get_bytes(text_stream,
                                                   &records[i].text.len);
    }

  return records;
}

/* Deserialize all records from the packed data container ROOT, allocate
 * them in POOL and return them.  Set *COUNT to the number of records read.
 */
static base_record_t *
unpack(apr_size_t *count,
       svn_packed__data_root_t *root,
       apr_pool_t *pool)
{
  svn_packed__int_stream_t *base_stream
    = svn_packed__first_int_stream(root);
  svn_packed__int_stream_t *sub_count_stream
    = svn_packed__next_int_stream(base_stream);
  svn_packed__byte_stream_t *base_description_stream
    = svn_packed__first_byte_stream(root);
  svn_packed__byte_stream_t *base_binary_stream
    = svn_packed__next_byte_stream(base_description_stream);
  svn_packed__byte_stream_t *sub_text_stream
    = svn_packed__next_byte_stream(base_binary_stream);

  svn_packed__int_stream_t *left_sub_stream
    = svn_packed__next_int_stream(sub_count_stream);
  svn_packed__int_stream_t *right_sub_stream
    = svn_packed__next_int_stream(left_sub_stream);

  apr_size_t i;
  base_record_t *data;
  *count = svn_packed__int_count(sub_count_stream) / 2;
  data = apr_pcalloc(pool, *count * sizeof(*data));

  for (i = 0; i < *count; ++i)
    {
      data[i].counter = (int) svn_packed__get_int(base_stream);
      data[i].description.data
        = svn_packed__get_bytes(base_description_stream,
                                &data[i].description.len);
      data[i].large_unsigned1 = svn_packed__get_uint(base_stream);
      data[i].large_unsigned2 = svn_packed__get_uint(base_stream);
      data[i].left_subs = unpack_subs(left_sub_stream, sub_text_stream,
                      (apr_size_t)svn_packed__get_uint(sub_count_stream),
                      pool);

      data[i].large_signed1 = svn_packed__get_int(base_stream);
      data[i].large_signed2 = svn_packed__get_int(base_stream);
      data[i].prime = (unsigned) svn_packed__get_uint(base_stream);
      data[i].right_subs = unpack_subs(right_sub_stream, sub_text_stream,
                      (apr_size_t)svn_packed__get_uint(sub_count_stream),
                      pool);

      data[i].binary.data
        = svn_packed__get_bytes(base_binary_stream,
                                &data[i].binary.len);
    }

  return data;
}

/* Assert that LHS and RHS contain the same binary data (i.e. don't test
 * for a terminating NUL).
 */
static svn_error_t *
compare_binary(const svn_string_t *lhs,
               const svn_string_t *rhs)
{
  SVN_TEST_ASSERT(lhs->len == rhs->len);
  SVN_TEST_ASSERT(!memcmp(lhs->data, rhs->data, rhs->len));

  return SVN_NO_ERROR;
}

/* Assert that LHS and RHS contain the same number of records with the
 * same contents.
 */
static svn_error_t *
compare_subs(const sub_record_t *lhs,
             const sub_record_t *rhs)
{
  for (; lhs->sub_counter; ++lhs, ++rhs)
    {
      SVN_TEST_ASSERT(lhs->sub_counter == rhs->sub_counter);
      SVN_ERR(compare_binary(&lhs->text, &rhs->text));
    }

  SVN_TEST_ASSERT(lhs->sub_counter == rhs->sub_counter);
  return SVN_NO_ERROR;
}

/* Assert that the first COUNT records in LHS and RHS have the same contents.
 */
static svn_error_t *
compare(const base_record_t *lhs,
        const base_record_t *rhs,
        apr_size_t count)
{
  apr_size_t i;
  for (i = 0; i < count; ++i)
    {
      SVN_TEST_ASSERT(lhs[i].counter == rhs[i].counter);
      SVN_ERR(compare_binary(&lhs[i].description, &rhs[i].description));
      SVN_TEST_ASSERT(lhs[i].large_unsigned1 == rhs[i].large_unsigned1);
      SVN_TEST_ASSERT(lhs[i].large_unsigned2 == rhs[i].large_unsigned2);
      SVN_ERR(compare_subs(lhs[i].left_subs, rhs[i].left_subs));
      SVN_TEST_ASSERT(lhs[i].counter == rhs[i].counter);
      SVN_TEST_ASSERT(lhs[i].large_signed1 == rhs[i].large_signed1);
      SVN_TEST_ASSERT(lhs[i].large_signed2 == rhs[i].large_signed2);
      SVN_TEST_ASSERT(lhs[i].prime == rhs[i].prime);
      SVN_ERR(compare_subs(lhs[i].right_subs, rhs[i].right_subs));
      SVN_ERR(compare_binary(&lhs[i].binary, &rhs[i].binary));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_empty_structure(apr_pool_t *pool)
{
  base_record_t *unpacked;
  apr_size_t count;

  /* create an empty, readable container */
  svn_packed__data_root_t *root = pack(test_data, 0, pool);

  SVN_ERR(get_read_root(&root, root, pool));
  unpacked = unpack(&count, root, pool);
  SVN_TEST_ASSERT(count == 0);
  SVN_ERR(compare(unpacked, test_data, count));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_full_structure(apr_pool_t *pool)
{
  base_record_t *unpacked;
  apr_size_t count;

  /* create an empty, readable container */
  svn_packed__data_root_t *root = pack(test_data, BASE_RECORD_COUNT, pool);

  SVN_ERR(get_read_root(&root, root, pool));
  unpacked = unpack(&count, root, pool);
  SVN_TEST_ASSERT(count == BASE_RECORD_COUNT);
  SVN_ERR(compare(unpacked, test_data, count));

  return SVN_NO_ERROR;
}

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_empty_container,
                   "test empty container"),
    SVN_TEST_PASS2(test_uint_stream,
                   "test a single uint stream"),
    SVN_TEST_PASS2(test_int_stream,
                   "test a single int stream"),
    SVN_TEST_PASS2(test_byte_stream,
                   "test a single bytes stream"),
    SVN_TEST_PASS2(test_empty_structure,
                   "test empty, nested structure"),
    SVN_TEST_PASS2(test_full_structure,
                   "test nested structure"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
