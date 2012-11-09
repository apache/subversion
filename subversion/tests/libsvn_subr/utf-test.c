/*
 * utf-test.c -- test the utf functions
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
#include "svn_utf.h"
#include "svn_pools.h"

#include "private/svn_utf_private.h"

/* Random number seed.  Yes, it's global, just pretend you can't see it. */
static apr_uint32_t diff_diff3_seed;

/* Return the value of the current random number seed, initializing it if
   necessary */
static apr_uint32_t
seed_val(void)
{
  static svn_boolean_t first = TRUE;

  if (first)
    {
      diff_diff3_seed = (apr_uint32_t) apr_time_now();
      first = FALSE;
    }

  return diff_diff3_seed;
}

/* Return a random number N such that MIN_VAL <= N <= MAX_VAL */
static apr_uint32_t
range_rand(apr_uint32_t min_val,
           apr_uint32_t max_val)
{
  apr_uint64_t diff = max_val - min_val;
  apr_uint64_t val = diff * svn_test_rand(&diff_diff3_seed);
  val /= 0xffffffff;
  return min_val + (apr_uint32_t) val;
}

/* Explicit tests of various valid/invalid sequences */
static svn_error_t *
utf_validate(apr_pool_t *pool)
{
  struct data {
    svn_boolean_t valid;
    char string[20];
  } tests[] = {
    {TRUE,  {'a', 'b', '\0'}},
    {FALSE, {'a', 'b', '\x80', '\0'}},

    {FALSE, {'a', 'b', '\xC0',                                   '\0'}},
    {FALSE, {'a', 'b', '\xC0', '\x81',                 'x', 'y', '\0'}},

    {TRUE,  {'a', 'b', '\xC5', '\x81',                 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xC5', '\xC0',                 'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xE0',                                   '\0'}},
    {FALSE, {'a', 'b', '\xE0',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xE0', '\xA0',                           '\0'}},
    {FALSE, {'a', 'b', '\xE0', '\xA0',                 'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xE0', '\xA0', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xE0', '\x9F', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xE0', '\xCF', '\x81',         'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xE5',                                   '\0'}},
    {FALSE, {'a', 'b', '\xE5',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xE5', '\x81',                           '\0'}},
    {FALSE, {'a', 'b', '\xE5', '\x81',                 'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xE5', '\x81', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xE5', '\xE1', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xE5', '\x81', '\xE1',         'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xED',                                   '\0'}},
    {FALSE, {'a', 'b', '\xED',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xED', '\x81',                           '\0'}},
    {FALSE, {'a', 'b', '\xED', '\x81',                 'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xED', '\x81', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xED', '\xA0', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xED', '\x81', '\xC1',         'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xEE',                                   '\0'}},
    {FALSE, {'a', 'b', '\xEE',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xEE', '\x81',                           '\0'}},
    {FALSE, {'a', 'b', '\xEE', '\x81',                 'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xEE', '\x81', '\x81',         'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xEE', '\xA0', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xEE', '\xC0', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xEE', '\x81', '\xC1',         'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xF0',                                   '\0'}},
    {FALSE, {'a', 'b', '\xF0',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\x91',                           '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\x91',                 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\x91', '\x81',                   '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\x91', '\x81',         'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xF0', '\x91', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\x81', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\xC1', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\x91', '\xC1', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF0', '\x91', '\x81', '\xC1', 'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xF2',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF2', '\x91',                 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF2', '\x91', '\x81',         'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xF2', '\x91', '\x81', '\x81', 'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xF2', '\x81', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF2', '\xC1', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF2', '\x91', '\xC1', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF2', '\x91', '\x81', '\xC1', 'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xF4',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\x91',                 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\x91', '\x81',         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\x91', '\x81', '\x81', 'x', 'y', '\0'}},
    {TRUE,  {'a', 'b', '\xF4', '\x81', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\xC1', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\x91', '\xC1', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\x91', '\x81', '\xC1', 'x', 'y', '\0'}},

    {FALSE, {'a', 'b', '\xF5',                         'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF5', '\x81',                 'x', 'y', '\0'}},

    {TRUE,  {'a', 'b', '\xF4', '\x81', '\x81', '\x81', 'x', 'y',
             'a', 'b', '\xF2', '\x91', '\x81', '\x81', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\x81', '\x81', '\x81', 'x', 'y',
             'a', 'b', '\xF2', '\x91', '\x81', '\xC1', 'x', 'y', '\0'}},
    {FALSE, {'a', 'b', '\xF4', '\x81', '\x81', '\x81', 'x', 'y',
             'a', 'b', '\xF2', '\x91', '\x81',         'x', 'y', '\0'}},

    {-1},
  };
  int i = 0;

  while (tests[i].valid != -1)
    {
      const char *last = svn_utf__last_valid(tests[i].string,
                                             strlen(tests[i].string));
      apr_size_t len = strlen(tests[i].string);

      if ((svn_utf__cstring_is_valid(tests[i].string) != tests[i].valid)
          ||
          (svn_utf__is_valid(tests[i].string, len) != tests[i].valid))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL, "is_valid test %d failed", i);

      if (!svn_utf__is_valid(tests[i].string, last - tests[i].string)
          ||
          (tests[i].valid && *last))
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL, "last_valid test %d failed", i);

      ++i;
    }

  return SVN_NO_ERROR;
}

/* Compare the two different implementations using random data. */
static svn_error_t *
utf_validate2(apr_pool_t *pool)
{
  int i;

  seed_val();

  /* We want enough iterations so that most runs get both valid and invalid
     strings.  We also want enough iterations such that a deliberate error
     in one of the implementations will trigger a failure.  By experiment
     the second requirement requires a much larger number of iterations
     that the first. */
  for (i = 0; i < 100000; ++i)
    {
      unsigned int j;
      char str[64];
      apr_size_t len;

      /* A random string; experiment shows that it's occasionally (less
         than 1%) valid but usually invalid. */
      for (j = 0; j < sizeof(str) - 1; ++j)
        str[j] = (char)range_rand(0, 255);
      str[sizeof(str) - 1] = 0;
      len = strlen(str);

      if (svn_utf__last_valid(str, len) != svn_utf__last_valid2(str, len))
        {
          /* Duplicate calls for easy debugging */
          svn_utf__last_valid(str, len);
          svn_utf__last_valid2(str, len);
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL, "is_valid2 test %d failed", i);
        }
    }

  return SVN_NO_ERROR;
}

/* Test conversion from different codepages to utf8. */
static svn_error_t *
test_utf_cstring_to_utf8_ex2(apr_pool_t *pool)
{
  apr_size_t i;
  apr_pool_t *subpool = svn_pool_create(pool);

  struct data {
      const char *string;
      const char *expected_result;
      const char *from_page;
  } tests[] = {
      {"ascii text\n", "ascii text\n", "unexistant-page"},
      {"Edelwei\xdf", "Edelwei\xc3\x9f", "ISO-8859-1"}
  };

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      const char *dest;

      svn_pool_clear(subpool);

      SVN_ERR(svn_utf_cstring_to_utf8_ex2(&dest, tests[i].string,
                                          tests[i].from_page, pool));

      if (strcmp(dest, tests[i].expected_result))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "svn_utf_cstring_to_utf8_ex2 ('%s', '%s') returned ('%s') "
             "instead of ('%s')",
             tests[i].string, tests[i].from_page,
             dest,
             tests[i].expected_result);
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Test conversion to different codepages from utf8. */
static svn_error_t *
test_utf_cstring_from_utf8_ex2(apr_pool_t *pool)
{
  apr_size_t i;
  apr_pool_t *subpool = svn_pool_create(pool);

  struct data {
      const char *string;
      const char *expected_result;
      const char *to_page;
  } tests[] = {
      {"ascii text\n", "ascii text\n", "unexistant-page"},
      {"Edelwei\xc3\x9f", "Edelwei\xdf", "ISO-8859-1"}
  };

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
      const char *dest;

      svn_pool_clear(subpool);

      SVN_ERR(svn_utf_cstring_from_utf8_ex2(&dest, tests[i].string,
                                            tests[i].to_page, pool));

      if (strcmp(dest, tests[i].expected_result))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "svn_utf_cstring_from_utf8_ex2 ('%s', '%s') returned ('%s') "
             "instead of ('%s')",
             tests[i].string, tests[i].to_page,
             dest,
             tests[i].expected_result);
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Test normalization-independent UTF-8 string comparison */
static svn_error_t *
normalized_compare(const char *stra, int expected, const char *strb,
                   svn_boolean_t implicit_size,
                   const char *stratag, const char *strbtag,
                   svn_stringbuf_t *bufa, svn_stringbuf_t *bufb)
{
  const apr_size_t lena = (implicit_size
                           ? SVN_UTF__UNKNOWN_LENGTH : strlen(stra));
  const apr_size_t lenb = (implicit_size
                           ? SVN_UTF__UNKNOWN_LENGTH : strlen(strb));
  int result;

  SVN_ERR(svn_utf__normcmp(stra, lena, strb, lenb,
                           bufa, bufb, &result));

  /* UCS-4 debugging dump of the decomposed strings
  {
    const apr_int32_t *const ucsbufa = (void*)bufa->data;
    const apr_int32_t *const ucsbufb = (void*)bufb->data;
    apr_size_t i;

    printf("(%c)%7s %c %s\n", expected,
           stratag, (!result ? '=' : (result < 0 ? '<' : '>')), strbtag);

    for (i = 0; i < bufa->len || i < bufb->len; ++i)
    {
      if (i < bufa->len && i < bufb->len)
        printf("    U+%04X   U+%04X\n", ucsbufa[i], ucsbufb[i]);
      else if (i < bufa->len)
        printf("    U+%04X\n", ucsbufa[i]);
      else
        printf("             U+%04X\n", ucsbufb[i]);
    }
  }
  */

  if (('=' == expected && 0 != result)
      || ('<' == expected && 0 <= result)
      || ('>' == expected && 0 >= result))
    {
      return svn_error_createf
        (SVN_ERR_TEST_FAILED, NULL,
         "Expected '%s' %c '%s' but '%s' %c '%s'",
         stratag, expected, strbtag,
         stratag, (!result ? '=' : (result < 0 ? '<' : '>')), strbtag);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_utf_collated_compare(apr_pool_t *pool)
{
  /* Normalized: NFC */
  static const char nfc[] =
    "\xe1\xb9\xa8"              /* S with dot above and below */
    "\xc5\xaf"                  /* u with ring */
    "\xe1\xb8\x87"              /* b with macron below */
    "\xe1\xb9\xbd"              /* v with tilde */
    "\xe1\xb8\x9d"              /* e with breve and cedilla */
    "\xc8\x91"                  /* r with double grave */
    "\xc5\xa1"                  /* s with caron */
    "\xe1\xb8\xaf"              /* i with diaeresis and acute */
    "\xe1\xbb\x9d"              /* o with grave and hook */
    "\xe1\xb9\x8b";             /* n with circumflex below */

  /* Normalized: NFD */
  static const char nfd[] =
    "S\xcc\xa3\xcc\x87"         /* S with dot above and below */
    "u\xcc\x8a"                 /* u with ring */
    "b\xcc\xb1"                 /* b with macron below */
    "v\xcc\x83"                 /* v with tilde */
    "e\xcc\xa7\xcc\x86"         /* e with breve and cedilla */
    "r\xcc\x8f"                 /* r with double grave */
    "s\xcc\x8c"                 /* s with caron */
    "i\xcc\x88\xcc\x81"         /* i with diaeresis and acute */
    "o\xcc\x9b\xcc\x80"         /* o with grave and hook */
    "n\xcc\xad";                /* n with circumflex below */

  /* Mixed, denormalized */
  static const char mixup[] =
    "S\xcc\x87\xcc\xa3"         /* S with dot above and below */
    "\xc5\xaf"                  /* u with ring */
    "b\xcc\xb1"                 /* b with macron below */
    "\xe1\xb9\xbd"              /* v with tilde */
    "e\xcc\xa7\xcc\x86"         /* e with breve and cedilla */
    "\xc8\x91"                  /* r with double grave */
    "s\xcc\x8c"                 /* s with caron */
    "\xe1\xb8\xaf"              /* i with diaeresis and acute */
    "o\xcc\x80\xcc\x9b"         /* o with grave and hook */
    "\xe1\xb9\x8b";             /* n with circumflex below */

  static const char longer[] =
    "\xe1\xb9\xa8"              /* S with dot above and below */
    "\xc5\xaf"                  /* u with ring */
    "\xe1\xb8\x87"              /* b with macron below */
    "\xe1\xb9\xbd"              /* v with tilde */
    "\xe1\xb8\x9d"              /* e with breve and cedilla */
    "\xc8\x91"                  /* r with double grave */
    "\xc5\xa1"                  /* s with caron */
    "\xe1\xb8\xaf"              /* i with diaeresis and acute */
    "\xe1\xbb\x9d"              /* o with grave and hook */
    "\xe1\xb9\x8b"              /* n with circumflex below */
    "X";

  static const char shorter[] =
    "\xe1\xb9\xa8"              /* S with dot above and below */
    "\xc5\xaf"                  /* u with ring */
    "\xe1\xb8\x87"              /* b with macron below */
    "\xe1\xb9\xbd"              /* v with tilde */
    "\xe1\xb8\x9d"              /* e with breve and cedilla */
    "\xc8\x91"                  /* r with double grave */
    "\xc5\xa1"                  /* s with caron */
    "\xe1\xb8\xaf"              /* i with diaeresis and acute */
    "\xe1\xbb\x9d";             /* o with grave and hook */

  static const char lowcase[] =
    "s\xcc\x87\xcc\xa3"         /* s with dot above and below */
    "\xc5\xaf"                  /* u with ring */
    "b\xcc\xb1"                 /* b with macron below */
    "\xe1\xb9\xbd"              /* v with tilde */
    "e\xcc\xa7\xcc\x86"         /* e with breve and cedilla */
    "\xc8\x91"                  /* r with double grave */
    "s\xcc\x8c"                 /* s with caron */
    "\xe1\xb8\xaf"              /* i with diaeresis and acute */
    "o\xcc\x80\xcc\x9b"         /* o with grave and hook */
    "\xe1\xb9\x8b";             /* n with circumflex below */

  svn_stringbuf_t *bufa = svn_stringbuf_create_empty(pool);
  svn_stringbuf_t *bufb = svn_stringbuf_create_empty(pool);

  /* Empty key */
  SVN_ERR(normalized_compare("", '=', "", TRUE, "empty", "empty",
                             bufa, bufb));
  SVN_ERR(normalized_compare("", '<', "a", TRUE, "empty", "nonempty",
                             bufa, bufb));
  SVN_ERR(normalized_compare("a", '>', "", TRUE, "nonempty", "empty",
                             bufa, bufb));

  /* Deterministic ordering */
  SVN_ERR(normalized_compare("a", '<', "b", TRUE, "a", "b",
                             bufa, bufb));
  SVN_ERR(normalized_compare("b", '<', "c", TRUE, "b", "c",
                             bufa, bufb));
  SVN_ERR(normalized_compare("a", '<', "c", TRUE, "a", "c",
                             bufa, bufb));

  SVN_ERR(normalized_compare("b", '>', "a", FALSE, "b", "a",
                             bufa, bufb));
  SVN_ERR(normalized_compare("c", '>', "b", FALSE, "c", "b",
                             bufa, bufb));
  SVN_ERR(normalized_compare("c", '>', "a", FALSE, "c", "a",
                             bufa, bufb));

  /* Normalized equality */
  SVN_ERR(normalized_compare(nfc, '=', nfd, TRUE, "nfc", "nfd",
                             bufa, bufb));
  SVN_ERR(normalized_compare(nfd, '=', nfc, TRUE, "nfd", "nfc",
                             bufa, bufb));
  SVN_ERR(normalized_compare(nfc, '=', mixup, TRUE, "nfc", "mixup",
                             bufa, bufb));
  SVN_ERR(normalized_compare(nfd, '=', mixup, TRUE, "nfd", "mixup",
                             bufa, bufb));
  SVN_ERR(normalized_compare(mixup, '=', nfd, FALSE, "mixup", "nfd",
                             bufa, bufb));
  SVN_ERR(normalized_compare(mixup, '=', nfc, FALSE, "mixup", "nfc",
                             bufa, bufb));

  /* Key length */
  SVN_ERR(normalized_compare(nfc, '<', longer, FALSE, "nfc", "longer",
                             bufa, bufb));
  SVN_ERR(normalized_compare(longer, '>', nfc, FALSE, "longer","nfc",
                             bufa, bufb));
  SVN_ERR(normalized_compare(nfd, '>', shorter, TRUE, "nfd", "shorter",
                             bufa, bufb));
  SVN_ERR(normalized_compare(shorter, '<', nfd, TRUE, "shorter", "nfd",
                             bufa, bufb));
  SVN_ERR(normalized_compare(mixup, '<', lowcase, FALSE, "mixup", "lowcase",
                             bufa, bufb));
  SVN_ERR(normalized_compare(lowcase, '>', mixup, FALSE, "lowcase", "mixup",
                             bufa, bufb));

  return SVN_NO_ERROR;
}



/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(utf_validate,
                   "test is_valid/last_valid"),
    SVN_TEST_PASS2(utf_validate2,
                   "test last_valid/last_valid2"),
    SVN_TEST_PASS2(test_utf_cstring_to_utf8_ex2,
                   "test svn_utf_cstring_to_utf8_ex2"),
    SVN_TEST_PASS2(test_utf_cstring_from_utf8_ex2,
                   "test svn_utf_cstring_from_utf8_ex2"),
    SVN_TEST_PASS2(test_utf_collated_compare,
                   "test svn_utf__normcmp"),
    SVN_TEST_NULL
  };
