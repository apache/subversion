/*
 * string-test.c:  a collection of libsvn_string tests
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
#include <apr_file_io.h>

#include "../svn_test.h"

#include "svn_io.h"
#include "svn_error.h"
#include "svn_sorts.h"    /* MIN / MAX */
#include "svn_string.h"   /* This includes <apr_*.h> */
#include "private/svn_string_private.h"

/* A quick way to create error messages.  */
static svn_error_t *
fail(apr_pool_t *pool, const char *fmt, ...)
{
  va_list ap;
  char *msg;

  va_start(ap, fmt);
  msg = apr_pvsprintf(pool, fmt, ap);
  va_end(ap);

  return svn_error_create(SVN_ERR_TEST_FAILED, 0, msg);
}


/* Some of our own global variables, for simplicity.  Yes,
   simplicity. */
static const char *phrase_1 = "hello, ";
static const char *phrase_2 = "a longish phrase of sorts, longer than 16 anyway";




static svn_error_t *
test1(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create(phrase_1, pool);

  /* Test that length, data, and null-termination are correct. */
  if ((a->len == strlen(phrase_1)) && ((strcmp(a->data, phrase_1)) == 0))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test2(apr_pool_t *pool)
{
  svn_stringbuf_t *b = svn_stringbuf_ncreate(phrase_2, 16, pool);

  /* Test that length, data, and null-termination are correct. */
  if ((b->len == 16) && ((strncmp(b->data, phrase_2, 16)) == 0))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test3(apr_pool_t *pool)
{
  char *tmp;
  size_t old_len;

  svn_stringbuf_t *a = svn_stringbuf_create(phrase_1, pool);
  svn_stringbuf_t *b = svn_stringbuf_ncreate(phrase_2, 16, pool);

  tmp = apr_palloc(pool, (a->len + b->len + 1));
  strcpy(tmp, a->data);
  strcat(tmp, b->data);
  old_len = a->len;
  svn_stringbuf_appendstr(a, b);

  /* Test that length, data, and null-termination are correct. */
  if ((a->len == (old_len + b->len)) && ((strcmp(a->data, tmp)) == 0))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test4(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create(phrase_1, pool);
  svn_stringbuf_appendcstr(a, "new bytes to append");

  /* Test that length, data, and null-termination are correct. */
  if (svn_stringbuf_compare
      (a, svn_stringbuf_create("hello, new bytes to append", pool)))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test5(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create(phrase_1, pool);
  svn_stringbuf_appendbytes(a, "new bytes to append", 9);

  /* Test that length, data, and null-termination are correct. */
  if (svn_stringbuf_compare
      (a, svn_stringbuf_create("hello, new bytes", pool)))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test6(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create(phrase_1, pool);
  svn_stringbuf_t *b = svn_stringbuf_create(phrase_2, pool);
  svn_stringbuf_t *c = svn_stringbuf_dup(a, pool);

  /* Test that length, data, and null-termination are correct. */
  if ((svn_stringbuf_compare(a, c)) && (! svn_stringbuf_compare(b, c)))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test7(apr_pool_t *pool)
{
  char *tmp;
  size_t tmp_len;

  svn_stringbuf_t *c = svn_stringbuf_create(phrase_2, pool);

  tmp_len = c->len;
  tmp = apr_palloc(pool, c->len + 1);
  strcpy(tmp, c->data);

  svn_stringbuf_chop(c, 11);

  if ((c->len == (tmp_len - 11))
      && (strncmp(tmp, c->data, c->len) == 0)
      && (c->data[c->len] == '\0'))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test8(apr_pool_t *pool)
{
  svn_stringbuf_t *c = svn_stringbuf_create(phrase_2, pool);

  svn_stringbuf_setempty(c);

  if ((c->len == 0) && (c->data[0] == '\0'))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test9(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create(phrase_1, pool);

  svn_stringbuf_fillchar(a, '#');

  if ((strcmp(a->data, "#######") == 0)
      && ((strncmp(a->data, "############", a->len - 1)) == 0)
      && (a->data[(a->len - 1)] == '#')
      && (a->data[(a->len)] == '\0'))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}



static svn_error_t *
test10(apr_pool_t *pool)
{
  svn_stringbuf_t *s, *t;
  size_t len_1 = 0;
  size_t block_len_1 = 0;
  size_t block_len_2 = 0;

  s = svn_stringbuf_create("a small string", pool);
  len_1       = (s->len);
  block_len_1 = (s->blocksize);

  t = svn_stringbuf_create(", plus a string more than twice as long", pool);
  svn_stringbuf_appendstr(s, t);
  block_len_2 = (s->blocksize);

  /* Test that:
   *   - The initial block was at least the right fit.
   *   - The initial block was not excessively large.
   *   - The block more than doubled (because second string so long).
   */
  if ((len_1 <= (block_len_1 - 1))
      && ((block_len_1 - len_1) <= APR_ALIGN_DEFAULT(1))
        && ((block_len_2 / block_len_1) > 2))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}


static svn_error_t *
test11(apr_pool_t *pool)
{
  svn_stringbuf_t *s;

  s = svn_stringbuf_createf(pool,
                            "This %s is used in test %d.",
                            "string",
                            12);

  if (strcmp(s->data, "This string is used in test 12.") == 0)
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}

static svn_error_t *
check_string_contents(svn_stringbuf_t *string,
                      const char *ftext,
                      apr_size_t ftext_len,
                      int repeat,
                      apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  int i;

  data = string->data;
  len = string->len;
  for (i = 0; i < repeat; ++i)
    {
      if (len < ftext_len || memcmp(ftext, data, ftext_len))
        return fail(pool, "comparing failed");
      data += ftext_len;
      len -= ftext_len;
    }
  if (len < 1 || memcmp(data, "\0", 1))
    return fail(pool, "comparing failed");
  data += 1;
  len -= 1;
  for (i = 0; i < repeat; ++i)
    {
      if (len < ftext_len || memcmp(ftext, data, ftext_len))
        return fail(pool, "comparing failed");
      data += ftext_len;
      len -= ftext_len;
    }

  if (len)
    return fail(pool, "comparing failed");

  return SVN_NO_ERROR;
}


static svn_error_t *
test12(apr_pool_t *pool)
{
  svn_stringbuf_t *s;
  const char fname[] = "string-test.tmp";
  apr_file_t *file;
  apr_status_t status;
  apr_size_t len;
  int i, repeat;
  const char ftext[] =
    "Just some boring text. Avoiding newlines 'cos I don't know"
    "if any of the Subversion platfoms will mangle them! There's no"
    "need to test newline handling here anyway, it's not relevant.";

  status = apr_file_open(&file, fname, APR_WRITE | APR_TRUNCATE | APR_CREATE,
                         APR_OS_DEFAULT, pool);
  if (status)
    return fail(pool, "opening file");

  repeat = 100;

  /* Some text */
  for (i = 0; i < repeat; ++i)
    {
      status = apr_file_write_full(file, ftext, sizeof(ftext) - 1, &len);
      if (status)
        return fail(pool, "writing file");
    }

  /* A null byte, I don't *think* any of our platforms mangle these */
  status = apr_file_write_full(file, "\0", 1, &len);
  if (status)
    return fail(pool, "writing file");

  /* Some more text */
  for (i = 0; i < repeat; ++i)
    {
      status = apr_file_write_full(file, ftext, sizeof(ftext) - 1, &len);
      if (status)
        return fail(pool, "writing file");
    }

  status = apr_file_close(file);
  if (status)
    return fail(pool, "closing file");

  SVN_ERR(svn_stringbuf_from_file(&s, fname, pool));
  SVN_ERR(check_string_contents(s, ftext, sizeof(ftext) - 1, repeat, pool));

  /* Reset to avoid false positives */
  s = NULL;

  status = apr_file_open(&file, fname, APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return fail(pool, "opening file");

  SVN_ERR(svn_stringbuf_from_aprfile(&s, file, pool));
  SVN_ERR(check_string_contents(s, ftext, sizeof(ftext) - 1, repeat, pool));

  status = apr_file_close(file);
  if (status)
    return fail(pool, "closing file");

  status = apr_file_remove(fname, pool);
  if (status)
    return fail(pool, "removing file");

  return SVN_NO_ERROR;
}

/* Helper function for checking correctness of find_char_backward */
static svn_error_t *
test_find_char_backward(const char* data,
                        apr_size_t len,
                        char ch,
                        apr_size_t pos,
                        apr_pool_t *pool)
{
  apr_size_t i;

  svn_stringbuf_t *a = svn_stringbuf_create(data, pool);
  i = svn_stringbuf_find_char_backward(a, ch);

  if (i == pos)
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}

static svn_error_t *
test13(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create("test, test", pool);

  return test_find_char_backward(a->data, a->len, ',', 4, pool);
}

static svn_error_t *
test14(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create(",test test", pool);

  return test_find_char_backward(a->data, a->len, ',', 0, pool);
}

static svn_error_t *
test15(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create("testing,", pool);

  return test_find_char_backward(a->data,
                                 a->len,
                                 ',',
                                 a->len - 1,
                                 pool);
}

static svn_error_t *
test16(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create_empty(pool);

  return test_find_char_backward(a->data, a->len, ',', 0, pool);
}

static svn_error_t *
test17(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create("test test test", pool);

  return test_find_char_backward(a->data,
                                 a->len,
                                 ',',
                                 a->len,
                                 pool);
}

static svn_error_t *
test_first_non_whitespace(const char *str,
                          const apr_size_t pos,
                          apr_pool_t *pool)
{
  apr_size_t i;

  svn_stringbuf_t *a = svn_stringbuf_create(str, pool);

  i = svn_stringbuf_first_non_whitespace(a);

  if (i == pos)
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}

static svn_error_t *
test18(apr_pool_t *pool)
{
  return test_first_non_whitespace("   \ttest", 4, pool);
}

static svn_error_t *
test19(apr_pool_t *pool)
{
  return test_first_non_whitespace("test", 0, pool);
}

static svn_error_t *
test20(apr_pool_t *pool)
{
  return test_first_non_whitespace("   ", 3, pool);
}

static svn_error_t *
test21(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create("    \ttest\t\t  \t  ", pool);
  svn_stringbuf_t *b = svn_stringbuf_create("test", pool);

  svn_stringbuf_strip_whitespace(a);

  if (svn_stringbuf_compare(a, b))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}

static svn_error_t *
test_stringbuf_unequal(const char* str1,
                       const char* str2,
                       apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create(str1, pool);
  svn_stringbuf_t *b = svn_stringbuf_create(str2, pool);

  if (svn_stringbuf_compare(a, b))
    return fail(pool, "test failed");
  else
    return SVN_NO_ERROR;
}

static svn_error_t *
test22(apr_pool_t *pool)
{
  return test_stringbuf_unequal("abc", "abcd", pool);
}

static svn_error_t *
test23(apr_pool_t *pool)
{
  return test_stringbuf_unequal("abc", "abb", pool);
}

static svn_error_t *
test24(apr_pool_t *pool)
{
  char buffer[SVN_INT64_BUFFER_SIZE];
  apr_size_t length;

  length = svn__i64toa(buffer, 0);
  SVN_TEST_ASSERT(length == 1);
  SVN_TEST_STRING_ASSERT(buffer, "0");

  length = svn__i64toa(buffer, APR_INT64_MIN);
  SVN_TEST_ASSERT(length == 20);
  SVN_TEST_STRING_ASSERT(buffer, "-9223372036854775808");

  length = svn__i64toa(buffer, APR_INT64_MAX);
  SVN_TEST_ASSERT(length == 19);
  SVN_TEST_STRING_ASSERT(buffer, "9223372036854775807");

  length = svn__ui64toa(buffer, 0u);
  SVN_TEST_ASSERT(length == 1);
  SVN_TEST_STRING_ASSERT(buffer, "0");

  length = svn__ui64toa(buffer, APR_UINT64_MAX);
  SVN_TEST_ASSERT(length == 20);
  SVN_TEST_STRING_ASSERT(buffer, "18446744073709551615");

  return SVN_NO_ERROR;
}

static svn_error_t *
sub_test_base36(apr_uint64_t value, const char *base36)
{
  char buffer[SVN_INT64_BUFFER_SIZE];
  apr_size_t length;
  apr_size_t expected_length = strlen(base36);
  const char *end = buffer;
  apr_uint64_t result;

  length = svn__ui64tobase36(buffer, value);
  SVN_TEST_ASSERT(length == expected_length);
  SVN_TEST_STRING_ASSERT(buffer, base36);

  result = svn__base36toui64(&end, buffer);
  SVN_TEST_ASSERT(end - buffer == length);
  SVN_TEST_ASSERT(result == value);

  result = svn__base36toui64(NULL, buffer);
  SVN_TEST_ASSERT(result == value);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_base36(apr_pool_t *pool)
{
  SVN_ERR(sub_test_base36(0, "0"));
  SVN_ERR(sub_test_base36(APR_UINT64_C(1234567890), "kf12oi"));
  SVN_ERR(sub_test_base36(APR_UINT64_C(0x7fffffffffffffff), "1y2p0ij32e8e7"));
  SVN_ERR(sub_test_base36(APR_UINT64_C(0x8000000000000000), "1y2p0ij32e8e8"));
  SVN_ERR(sub_test_base36(APR_UINT64_MAX, "3w5e11264sgsf"));

  return SVN_NO_ERROR;
}

static svn_error_t *
expect_stringbuf_equal(const svn_stringbuf_t* str1,
                       const char* str2,
                       apr_pool_t *pool)
{
  if (svn_stringbuf_compare(str1, svn_stringbuf_create(str2, pool)))
    return SVN_NO_ERROR;
  else
    return fail(pool, "test failed");
}

static svn_error_t *
test_stringbuf_insert(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create("st , ", pool);

  svn_stringbuf_insert(a, 0, "teflon", 2);
  SVN_TEST_STRING_ASSERT(a->data, "test , ");

  svn_stringbuf_insert(a, 5, "hllo", 4);
  SVN_TEST_STRING_ASSERT(a->data, "test hllo, ");

  svn_stringbuf_insert(a, 6, a->data + 1, 1);
  SVN_TEST_STRING_ASSERT(a->data, "test hello, ");

  svn_stringbuf_insert(a, 12, "world class", 5);
  SVN_TEST_STRING_ASSERT(a->data, "test hello, world");

  svn_stringbuf_insert(a, 1200, "!", 1);
  SVN_TEST_STRING_ASSERT(a->data, "test hello, world!");

  svn_stringbuf_insert(a, 4, "\0-\0", 3);
  SVN_TEST_ASSERT(svn_stringbuf_compare(a,
                    svn_stringbuf_ncreate("test\0-\0 hello, world!",
                                          21, pool)));

  svn_stringbuf_insert(a, 14, a->data + 4, 3);
  SVN_TEST_ASSERT(svn_stringbuf_compare(a,
                    svn_stringbuf_ncreate("test\0-\0 hello,\0-\0 world!",
                                          24, pool)));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_stringbuf_remove(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create("test hello, world!", pool);

  svn_stringbuf_remove(a, 0, 2);
  SVN_TEST_STRING_ASSERT(a->data, "st hello, world!");

  svn_stringbuf_remove(a, 2, 2);
  SVN_TEST_STRING_ASSERT(a->data, "stello, world!");

  svn_stringbuf_remove(a, 5, 200);
  SVN_TEST_STRING_ASSERT(a->data, "stell");

  svn_stringbuf_remove(a, 1200, 393);
  SVN_ERR(expect_stringbuf_equal(a, "stell", pool));

  svn_stringbuf_remove(a, APR_SIZE_MAX, 2);
  SVN_ERR(expect_stringbuf_equal(a, "stell", pool));

  svn_stringbuf_remove(a, 1, APR_SIZE_MAX);
  SVN_ERR(expect_stringbuf_equal(a, "s", pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_stringbuf_replace(apr_pool_t *pool)
{
  svn_stringbuf_t *a = svn_stringbuf_create("odd with some world?", pool);

  svn_stringbuf_replace(a, 0, 3, "tester", 4);
  SVN_TEST_STRING_ASSERT(a->data, "test with some world?");

  svn_stringbuf_replace(a, 5, 10, "hllo, coder", 6);
  SVN_TEST_STRING_ASSERT(a->data, "test hllo, world?");

  svn_stringbuf_replace(a, 6, 0, a->data + 1, 1);
  SVN_TEST_STRING_ASSERT(a->data, "test hello, world?");

  svn_stringbuf_replace(a, 17, 10, "!", 1);
  SVN_TEST_STRING_ASSERT(a->data, "test hello, world!");

  svn_stringbuf_replace(a, 1200, 199, "!!", 2);
  SVN_TEST_STRING_ASSERT(a->data, "test hello, world!!!");

  svn_stringbuf_replace(a, 10, 2, "\0-\0", 3);
  SVN_TEST_ASSERT(svn_stringbuf_compare(a,
                    svn_stringbuf_ncreate("test hello\0-\0world!!!",
                                          21, pool)));

  svn_stringbuf_replace(a, 10, 3, a->data + 10, 3);
  SVN_TEST_ASSERT(svn_stringbuf_compare(a,
                    svn_stringbuf_ncreate("test hello\0-\0world!!!",
                                          21, pool)));

  svn_stringbuf_replace(a, 19, 1, a->data + 10, 3);
  SVN_TEST_ASSERT(svn_stringbuf_compare(a,
                    svn_stringbuf_ncreate("test hello\0-\0world!\0-\0!",
                                          23, pool)));

  svn_stringbuf_replace(a, 1, APR_SIZE_MAX, "x", 1);
  SVN_ERR(expect_stringbuf_equal(a, "tx", pool));

  svn_stringbuf_replace(a, APR_SIZE_MAX, APR_SIZE_MAX, "y", 1);
  SVN_ERR(expect_stringbuf_equal(a, "txy", pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_string_similarity(apr_pool_t *pool)
{
  const struct sim_score_test_t
  {
    const char *stra;
    const char *strb;
    apr_size_t lcs;
    unsigned int score;
  } tests[] =
      {
#define SCORE(lcs, len) \
   ((2 * SVN_STRING__SIM_RANGE_MAX * (lcs) + (len)/2) / (len))

        /* Equality */
        {"",       "",          0, SVN_STRING__SIM_RANGE_MAX},
        {"quoth",  "quoth",     5, SCORE(5, 5+5)},

        /* Deletion at start */
        {"quoth",  "uoth",      4, SCORE(4, 5+4)},
        {"uoth",   "quoth",     4, SCORE(4, 4+5)},

        /* Deletion at end */
        {"quoth",  "quot",      4, SCORE(4, 5+4)},
        {"quot",   "quoth",     4, SCORE(4, 4+5)},

        /* Insertion at start */
        {"quoth",  "Xquoth",    5, SCORE(5, 5+6)},
        {"Xquoth", "quoth",     5, SCORE(5, 6+5)},

        /* Insertion at end */
        {"quoth",  "quothX",    5, SCORE(5, 5+6)},
        {"quothX", "quoth",     5, SCORE(5, 6+5)},

        /* Insertion in middle */
        {"quoth",  "quoXth",    5, SCORE(5, 5+6)},
        {"quoXth", "quoth",     5, SCORE(5, 6+5)},

        /* Transposition at start */
        {"quoth",  "uqoth",     4, SCORE(4, 5+5)},
        {"uqoth",  "quoth",     4, SCORE(4, 5+5)},

        /* Transposition at end */
        {"quoth",  "quoht",     4, SCORE(4, 5+5)},
        {"quoht",  "quoth",     4, SCORE(4, 5+5)},

        /* Transposition in middle */
        {"quoth",  "qutoh",     4, SCORE(4, 5+5)},
        {"qutoh",  "quoth",     4, SCORE(4, 5+5)},

        /* Difference */
        {"quoth",  "raven",     0, SCORE(0, 5+5)},
        {"raven",  "quoth",     0, SCORE(0, 5+5)},
        {"x",      "",          0, SCORE(0, 1+0)},
        {"",       "x",         0, SCORE(0, 0+1)},
        {"",       "quoth",     0, SCORE(0, 0+5)},
        {"quoth",  "",          0, SCORE(0, 5+0)},
        {"quoth",  "the raven", 2, SCORE(2, 5+9)},
        {"the raven",  "quoth", 2, SCORE(2, 5+9)},
        {NULL, NULL}
      };

  const struct sim_score_test_t *t;
  svn_membuf_t buffer;

  svn_membuf__create(&buffer, 0, pool);
  for (t = tests; t->stra; ++t)
    {
      apr_size_t lcs;
      const apr_size_t score =
        svn_cstring__similarity(t->stra, t->strb, &buffer, &lcs);
      /*
      fprintf(stderr,
              "lcs %s ~ %s score %.6f (%"APR_SIZE_T_FMT
              ") expected %.6f (%"APR_SIZE_T_FMT"))\n",
              t->stra, t->strb, score/1.0/SVN_STRING__SIM_RANGE_MAX,
              lcs, t->score/1.0/SVN_STRING__SIM_RANGE_MAX, t->lcs);
      */
      if (score != t->score)
        return fail(pool, "%s ~ %s score %.6f <> expected %.6f",
                    t->stra, t->strb,
                    score/1.0/SVN_STRING__SIM_RANGE_MAX,
                    t->score/1.0/SVN_STRING__SIM_RANGE_MAX);

      if (lcs != t->lcs)
        return fail(pool,
                    "%s ~ %s lcs %"APR_SIZE_T_FMT
                    " <> expected %"APR_SIZE_T_FMT,
                    t->stra, t->strb, lcs, t->lcs);
    }

  /* Test partial similarity */
  {
    const svn_string_t foo = {"svn:foo", 4};
    const svn_string_t bar = {"svn:bar", 4};
    if (SVN_STRING__SIM_RANGE_MAX
        != svn_string__similarity(&foo, &bar, &buffer, NULL))
      return fail(pool, "'%s'[:4] ~ '%s'[:4] found different",
                  foo.data, bar.data);
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_string_matching(apr_pool_t *pool)
{
  const struct test_data_t
    {
      const char *a;
      const char *b;
      apr_size_t match_len;
      apr_size_t rmatch_len;
    }
  tests[] =
    {
      /* edge cases */
      {"", "", 0, 0},
      {"", "x", 0, 0},
      {"x", "", 0, 0},
      {"x", "x", 1, 1},
      {"", "1234567890abcdef", 0, 0},
      {"1234567890abcdef", "", 0, 0},
      {"1234567890abcdef", "1234567890abcdef", 16, 16},

      /* left-side matches */
      {"x", "y", 0, 0},
      {"ax", "ay", 1, 0},
      {"ax", "a", 1, 0},
      {"a", "ay", 1, 0},
      {"1234567890abcdef", "1234567890abcdeg", 15, 0},
      {"1234567890abcdef_", "1234567890abcdefg", 16, 0},
      {"12345678_0abcdef", "1234567890abcdeg", 8, 0},
      {"1234567890abcdef", "12345678", 8, 0},
      {"12345678", "1234567890abcdef", 8, 0},
      {"12345678_0ab", "1234567890abcdef", 8, 0},

      /* right-side matches */
      {"xa", "ya", 0, 1},
      {"xa", "a", 0, 1},
      {"a", "ya", 0, 1},
      {"_234567890abcdef", "1234567890abcdef", 0, 15},
      {"_1234567890abcdef", "x1234567890abcdef", 0, 16},
      {"1234567_90abcdef", "_1234567890abcdef", 0, 8},
      {"1234567890abcdef", "90abcdef", 0, 8},
      {"90abcdef", "1234567890abcdef", 0, 8},
      {"8_0abcdef", "7890abcdef", 0, 7},

      /* two-side matches */
      {"bxa", "bya", 1, 1},
      {"bxa", "ba", 1, 1},
      {"ba", "bya", 1, 1},
      {"1234567_90abcdef", "1234567890abcdef", 7, 8},
      {"12345678_90abcdef", "1234567890abcdef", 8, 8},
      {"12345678_0abcdef", "1234567890abcdef", 8, 7},
      {"123456_abcdef", "1234sdffdssdf567890abcdef", 4, 6},
      {"1234567890abcdef", "12345678ef", 8, 2},
      {"x_234567890abcdef", "x1234567890abcdef", 1, 15},
      {"1234567890abcdefx", "1234567890abcdex", 15, 1},

      /* list terminator */
      {NULL}
    };

  const struct test_data_t *test;
  for (test = tests; test->a != NULL; ++test)
    {
      apr_size_t a_len = strlen(test->a);
      apr_size_t b_len = strlen(test->b);
      apr_size_t max_match = MIN(a_len, b_len);
      apr_size_t match_len
        = svn_cstring__match_length(test->a, test->b, max_match);
      apr_size_t rmatch_len
        = svn_cstring__reverse_match_length(test->a + a_len, test->b + b_len,
                                            max_match);

      SVN_TEST_ASSERT(match_len == test->match_len);
      SVN_TEST_ASSERT(rmatch_len == test->rmatch_len);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_cstring_skip_prefix(apr_pool_t *pool)
{
  SVN_TEST_STRING_ASSERT(svn_cstring_skip_prefix("12345", "12345"),
                         "");
  SVN_TEST_STRING_ASSERT(svn_cstring_skip_prefix("12345", "123"),
                         "45");
  SVN_TEST_STRING_ASSERT(svn_cstring_skip_prefix("12345", ""),
                         "12345");
  SVN_TEST_STRING_ASSERT(svn_cstring_skip_prefix("12345", "23"),
                         NULL);
  SVN_TEST_STRING_ASSERT(svn_cstring_skip_prefix("1", "12"),
                         NULL);
  SVN_TEST_STRING_ASSERT(svn_cstring_skip_prefix("", ""),
                         "");
  SVN_TEST_STRING_ASSERT(svn_cstring_skip_prefix("", "12"),
                         NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_stringbuf_replace_all(apr_pool_t *pool)
{
  svn_stringbuf_t *s = svn_stringbuf_create("abccabcdabc", pool);

  /* no replacement */
  SVN_TEST_ASSERT(0 == svn_stringbuf_replace_all(s, "xyz", "k"));
  SVN_TEST_STRING_ASSERT(s->data, "abccabcdabc");
  SVN_TEST_ASSERT(s->len == 11);

  /* replace at string head: grow */
  SVN_TEST_ASSERT(1 == svn_stringbuf_replace_all(s, "abcc", "xyabcz"));
  SVN_TEST_STRING_ASSERT(s->data, "xyabczabcdabc");
  SVN_TEST_ASSERT(s->len == 13);

  /* replace at string head: shrink */
  SVN_TEST_ASSERT(1 == svn_stringbuf_replace_all(s, "xyabcz", "abcc"));
  SVN_TEST_STRING_ASSERT(s->data, "abccabcdabc");
  SVN_TEST_ASSERT(s->len == 11);

  /* replace at string tail: grow */
  SVN_TEST_ASSERT(1 == svn_stringbuf_replace_all(s, "dabc", "xyabcz"));
  SVN_TEST_STRING_ASSERT(s->data, "abccabcxyabcz");
  SVN_TEST_ASSERT(s->len == 13);

  /* replace at string tail: shrink */
  SVN_TEST_ASSERT(1 == svn_stringbuf_replace_all(s, "xyabcz", "dabc"));
  SVN_TEST_STRING_ASSERT(s->data, "abccabcdabc");
  SVN_TEST_ASSERT(s->len == 11);

  /* replace at multiple locations: grow */
  SVN_TEST_ASSERT(3 == svn_stringbuf_replace_all(s, "ab", "xyabz"));
  SVN_TEST_STRING_ASSERT(s->data, "xyabzccxyabzcdxyabzc");
  SVN_TEST_ASSERT(s->len == 20);

  /* replace at multiple locations: shrink */
  SVN_TEST_ASSERT(3 == svn_stringbuf_replace_all(s, "xyabz", "ab"));
  SVN_TEST_STRING_ASSERT(s->data, "abccabcdabc");
  SVN_TEST_ASSERT(s->len == 11);

  /* replace at multiple locations: same length */
  SVN_TEST_ASSERT(3 == svn_stringbuf_replace_all(s, "abc", "xyz"));
  SVN_TEST_STRING_ASSERT(s->data, "xyzcxyzdxyz");
  SVN_TEST_ASSERT(s->len == 11);

  /* replace at multiple locations: overlapping */
  s = svn_stringbuf_create("aaaaaaaaaaa", pool);
  SVN_TEST_ASSERT(5 == svn_stringbuf_replace_all(s, "aa", "aaa"));
  SVN_TEST_STRING_ASSERT(s->data, "aaaaaaaaaaaaaaaa");
  SVN_TEST_ASSERT(s->len == 16);

  SVN_TEST_ASSERT(5 == svn_stringbuf_replace_all(s, "aaa", "aa"));
  SVN_TEST_STRING_ASSERT(s->data, "aaaaaaaaaaa");
  SVN_TEST_ASSERT(s->len == 11);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_stringbuf_leftchop(apr_pool_t *pool)
{
  svn_stringbuf_t *s;

  s = svn_stringbuf_create("abcd", pool);
  svn_stringbuf_leftchop(s, 0);
  SVN_TEST_ASSERT(s->len == 4);
  SVN_TEST_STRING_ASSERT(s->data, "abcd");

  svn_stringbuf_leftchop(s, 2);
  SVN_TEST_ASSERT(s->len == 2);
  SVN_TEST_STRING_ASSERT(s->data, "cd");

  svn_stringbuf_leftchop(s, 4);
  SVN_TEST_ASSERT(s->len == 0);
  SVN_TEST_STRING_ASSERT(s->data, "");

  s = svn_stringbuf_create("abcd", pool);
  svn_stringbuf_leftchop(s, 4);
  SVN_TEST_ASSERT(s->len == 0);
  SVN_TEST_STRING_ASSERT(s->data, "");

  s = svn_stringbuf_create_empty(pool);
  svn_stringbuf_leftchop(s, 0);
  SVN_TEST_ASSERT(s->len == 0);
  SVN_TEST_STRING_ASSERT(s->data, "");

  svn_stringbuf_leftchop(s, 2);
  SVN_TEST_ASSERT(s->len == 0);
  SVN_TEST_STRING_ASSERT(s->data, "");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_stringbuf_set(apr_pool_t *pool)
{
  svn_stringbuf_t *str = svn_stringbuf_create_empty(pool);

  SVN_TEST_STRING_ASSERT(str->data, "");
  SVN_TEST_INT_ASSERT(str->len, 0);

  svn_stringbuf_set(str, "0123456789");
  SVN_TEST_STRING_ASSERT(str->data, "0123456789");
  SVN_TEST_INT_ASSERT(str->len, 10);

  svn_stringbuf_set(str, "");
  SVN_TEST_STRING_ASSERT(str->data, "");
  SVN_TEST_INT_ASSERT(str->len, 0);

  svn_stringbuf_set(str, "0123456789abcdef");
  SVN_TEST_STRING_ASSERT(str->data, "0123456789abcdef");
  SVN_TEST_INT_ASSERT(str->len, 16);

  svn_stringbuf_set(str, "t");
  SVN_TEST_STRING_ASSERT(str->data, "t");
  SVN_TEST_INT_ASSERT(str->len, 1);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_cstring_join(apr_pool_t *pool)
{
  apr_array_header_t *arr;

  {
    arr = apr_array_make(pool, 0, sizeof(const char *));

    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", FALSE, pool), "");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", TRUE, pool), "");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", FALSE, pool), "");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", TRUE, pool), "");
  }

  {
    arr = apr_array_make(pool, 0, sizeof(const char *));
    APR_ARRAY_PUSH(arr, const char *) = "";

    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", FALSE, pool), "");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", TRUE, pool), "");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", FALSE, pool), "");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", TRUE, pool), ";");
  }

  {
    arr = apr_array_make(pool, 0, sizeof(const char *));
    APR_ARRAY_PUSH(arr, const char *) = "ab";
    APR_ARRAY_PUSH(arr, const char *) = "cd";

    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", FALSE, pool), "abcd");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", TRUE, pool), "abcd");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", FALSE, pool), "ab;cd");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", TRUE, pool), "ab;cd;");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "//", FALSE, pool), "ab//cd");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "//", TRUE, pool), "ab//cd//");
  }

  {
    arr = apr_array_make(pool, 0, sizeof(const char *));
    APR_ARRAY_PUSH(arr, const char *) = "";
    APR_ARRAY_PUSH(arr, const char *) = "ab";
    APR_ARRAY_PUSH(arr, const char *) = "";

    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", FALSE, pool), "ab");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "", TRUE, pool), "ab");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", FALSE, pool), ";ab;");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, ";", TRUE, pool), ";ab;;");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "//", FALSE, pool), "//ab//");
    SVN_TEST_STRING_ASSERT(svn_cstring_join2(arr, "//", TRUE, pool), "//ab////");
  }

  return SVN_NO_ERROR;
}

/*
   ====================================================================
   If you add a new test to this file, update this array.

   (These globals are required by our included main())
*/

/* An array of all test functions */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test1,
                   "make svn_stringbuf_t from cstring"),
    SVN_TEST_PASS2(test2,
                   "make svn_stringbuf_t from substring of cstring"),
    SVN_TEST_PASS2(test3,
                   "append svn_stringbuf_t to svn_stringbuf_t"),
    SVN_TEST_PASS2(test4,
                   "append C string to svn_stringbuf_t"),
    SVN_TEST_PASS2(test5,
                   "append bytes, then compare two strings"),
    SVN_TEST_PASS2(test6,
                   "dup two strings, then compare"),
    SVN_TEST_PASS2(test7,
                   "chopping a string"),
    SVN_TEST_PASS2(test8,
                   "emptying a string"),
    SVN_TEST_PASS2(test9,
                   "fill string with hashmarks"),
    SVN_TEST_PASS2(test10,
                   "block initialization and growth"),
    SVN_TEST_PASS2(test11,
                   "formatting strings from varargs"),
    SVN_TEST_PASS2(test12,
                   "create string from file"),
    SVN_TEST_PASS2(test13,
                   "find_char_backward; middle case"),
    SVN_TEST_PASS2(test14,
                   "find_char_backward; 0 case"),
    SVN_TEST_PASS2(test15,
                   "find_char_backward; strlen - 1 case"),
    SVN_TEST_PASS2(test16,
                   "find_char_backward; len = 0 case"),
    SVN_TEST_PASS2(test17,
                   "find_char_backward; no occurrence case"),
    SVN_TEST_PASS2(test18,
                   "check whitespace removal; common case"),
    SVN_TEST_PASS2(test19,
                   "check whitespace removal; no whitespace case"),
    SVN_TEST_PASS2(test20,
                   "check whitespace removal; all whitespace case"),
    SVN_TEST_PASS2(test21,
                   "check that whitespace will be stripped correctly"),
    SVN_TEST_PASS2(test22,
                   "compare stringbufs; different lengths"),
    SVN_TEST_PASS2(test23,
                   "compare stringbufs; same length, different content"),
    SVN_TEST_PASS2(test24,
                   "verify i64toa"),
    SVN_TEST_PASS2(test_base36,
                   "verify base36 conversion"),
    SVN_TEST_PASS2(test_stringbuf_insert,
                   "check inserting into svn_stringbuf_t"),
    SVN_TEST_PASS2(test_stringbuf_remove,
                   "check deletion from svn_stringbuf_t"),
    SVN_TEST_PASS2(test_stringbuf_replace,
                   "check replacement in svn_stringbuf_t"),
    SVN_TEST_PASS2(test_string_similarity,
                   "test string similarity scores"),
    SVN_TEST_PASS2(test_string_matching,
                   "test string matching"),
    SVN_TEST_PASS2(test_cstring_skip_prefix,
                   "test svn_cstring_skip_prefix()"),
    SVN_TEST_PASS2(test_stringbuf_replace_all,
                   "test svn_stringbuf_replace_all"),
    SVN_TEST_PASS2(test_stringbuf_leftchop,
                   "test svn_stringbuf_leftchop"),
    SVN_TEST_PASS2(test_stringbuf_set,
                   "test svn_stringbuf_set()"),
    SVN_TEST_PASS2(test_cstring_join,
                   "test svn_cstring_join2()"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
