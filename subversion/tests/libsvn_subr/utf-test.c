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

#include "private/svn_string_private.h"
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
      {"ascii text\n", "ascii text\n", "unexistent-page"},
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
      {"ascii text\n", "ascii text\n", "unexistent-page"},
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

  static const struct utfcmp_test_t {
    const char *stra;
    char op;
    const char *strb;
    const char *taga;
    const char *tagb;
  } utfcmp_tests[] = {
    /* Empty key */
    {"",  '=', "",  "empty",    "empty"},
    {"",  '<', "a", "empty",    "nonempty"},
    {"a", '>', "",  "nonempty", "empty"},

    /* Deterministic ordering */
    {"a", '<', "b", "a", "b"},
    {"b", '<', "c", "b", "c"},
    {"a", '<', "c", "a", "c"},

    /* Normalized equality */
    {nfc,   '=', nfd,    "nfc",   "nfd"},
    {nfd,   '=', nfc,    "nfd",   "nfc"},
    {nfc,   '=', mixup,  "nfc",   "mixup"},
    {nfd,   '=', mixup,  "nfd",   "mixup"},
    {mixup, '=', nfd,    "mixup", "nfd"},
    {mixup, '=', nfc,    "mixup", "nfc"},

    /* Key length */
    {nfc,     '<', longer,    "nfc",     "longer"},
    {longer,  '>', nfc,       "longer",  "nfc"},
    {nfd,     '>', shorter,   "nfd",     "shorter"},
    {shorter, '<', nfd,       "shorter", "nfd"},
    {mixup,   '<', lowcase,   "mixup",   "lowcase"},
    {lowcase, '>', mixup,     "lowcase",  "mixup"},

    {NULL, 0, NULL, NULL, NULL}
  };


  const struct utfcmp_test_t *ut;
  svn_membuf_t bufa, bufb;
  svn_membuf__create(&bufa, 0, pool);
  svn_membuf__create(&bufb, 0, pool);

  srand(111);
  for (ut = utfcmp_tests; ut->stra; ++ut)
    {
      const svn_boolean_t implicit_size = (rand() % 17) & 1;
      const apr_size_t lena = (implicit_size
                               ? SVN_UTF__UNKNOWN_LENGTH : strlen(ut->stra));
      const apr_size_t lenb = (implicit_size
                               ? SVN_UTF__UNKNOWN_LENGTH : strlen(ut->strb));
      int result;

      SVN_ERR(svn_utf__normcmp(&result,
                               ut->stra, lena, ut->strb, lenb,
                               &bufa, &bufb));

      /* UCS-4 debugging dump of the decomposed strings
      {
        const apr_int32_t *const ucsbufa = bufa.data;
        const apr_int32_t *const ucsbufb = bufb.data;
        apr_size_t i;

        printf("(%c)%7s %c %s\n", ut->op,
               ut->taga, (!result ? '=' : (result < 0 ? '<' : '>')), ut->tagb);

        for (i = 0; i < bufa.size || i < bufb.size; ++i)
        {
          if (i < bufa.size && i < bufb.size)
            printf("    U+%04X   U+%04X\n", ucsbufa[i], ucsbufb[i]);
          else if (i < bufa.size)
            printf("    U+%04X\n", ucsbufa[i]);
          else
            printf("             U+%04X\n", ucsbufb[i]);
        }
      }
      */

      if (('=' == ut->op && 0 != result)
          || ('<' == ut->op && 0 <= result)
          || ('>' == ut->op && 0 >= result))
        {
          return svn_error_createf
            (SVN_ERR_TEST_FAILED, NULL,
             "Ut->Op '%s' %c '%s' but '%s' %c '%s'",
             ut->taga, ut->op, ut->tagb,
             ut->taga, (!result ? '=' : (result < 0 ? '<' : '>')), ut->tagb);
        }
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
test_utf_pattern_match(apr_pool_t *pool)
{
  static const struct glob_test_t {
    svn_boolean_t sql_like;
    svn_boolean_t matches;
    const char *pattern;
    const char *string;
    const char *escape;
  } glob_tests[] = {
#define LIKE_MATCH TRUE, TRUE
#define LIKE_FAIL  TRUE, FALSE
#define GLOB_MATCH FALSE, TRUE
#define GLOB_FAIL  FALSE, FALSE

    {LIKE_FAIL,  "",     "test", NULL},
    {GLOB_FAIL,  "",     "test", NULL},
    {LIKE_FAIL,  "",     "%",    NULL},
    {GLOB_FAIL,  "",     "*",    NULL},
    {LIKE_FAIL,  "test", "%",    NULL},
    {GLOB_FAIL,  "test", "*",    NULL},
    {LIKE_MATCH, "test", "test", NULL},
    {GLOB_MATCH, "test", "test", NULL},
    {LIKE_MATCH, "t\xe1\xb8\x9dst", "te\xcc\xa7\xcc\x86st", NULL},
    {GLOB_MATCH, "te\xcc\xa7\xcc\x86st", "t\xe1\xb8\x9dst", NULL},

    {LIKE_FAIL,  "test", "test", "\xe1\xb8\x9d"}, /* escape char not ascii */
    {LIKE_FAIL,  "test", "test", ""},             /* empty escape string */

    {LIKE_MATCH, "te#st",    "test",   "#"},
    {LIKE_FAIL,  "te#st",    "test",   NULL},
    {GLOB_MATCH, "te\\st",   "test",   NULL},
    {LIKE_MATCH, "te##st",   "te#st",  "#"},
    {LIKE_FAIL,  "te##st",   "te#st",  NULL},
    {GLOB_MATCH, "te\\\\st", "te\\st", NULL},
    {GLOB_FAIL,  "te\\\\st", "te\\st", "\\"}, /* escape char with glob */
    {LIKE_FAIL,  "te#%t",    "te%t",   NULL},
    {LIKE_MATCH, "te#%t",    "te%t",   "#"},
    {GLOB_MATCH, "te\\*t",   "te*t",   NULL},
    {LIKE_FAIL,  "te#%t",    "test",   NULL},
    {GLOB_FAIL,  "te\\*t",   "test",   NULL},
    {LIKE_FAIL,  "te#_t",    "te_t",   NULL},
    {LIKE_MATCH, "te#_t",    "te_t",   "#"},
    {GLOB_MATCH, "te\\?t",   "te?t",   NULL},
    {LIKE_FAIL,  "te#_t",    "test",   NULL},
    {LIKE_FAIL,  "te#_t",    "test",   "#"},
    {GLOB_FAIL,  "te\\?t",   "test",   NULL},

    {LIKE_MATCH, "_est",     "test",   NULL},
    {GLOB_MATCH, "?est",     "test",   NULL},
    {LIKE_MATCH, "te_t",     "test",   NULL},
    {GLOB_MATCH, "te?t",     "test",   NULL},
    {LIKE_MATCH, "tes_",     "test",   NULL},
    {GLOB_MATCH, "tes?",     "test",   NULL},
    {LIKE_FAIL,  "test_",    "test",   NULL},
    {GLOB_FAIL,  "test?",    "test",   NULL},

    {LIKE_MATCH, "[s%n]",   "[subversion]", NULL},
    {GLOB_FAIL,  "[s*n]",   "[subversion]", NULL},
    {LIKE_MATCH, "#[s%n]",  "[subversion]", "#"},
    {GLOB_MATCH, "\\[s*n]", "[subversion]", NULL},

    {GLOB_MATCH, ".[\\-\\t]", ".t",           NULL},
    {GLOB_MATCH, "test*?*[a-z]*", "testgoop", NULL},
    {GLOB_MATCH, "te[^x]t", "test",           NULL},
    {GLOB_MATCH, "te[^abc]t", "test",         NULL},
    {GLOB_MATCH, "te[^x]t", "test",           NULL},
    {GLOB_MATCH, "te[!x]t", "test",           NULL},
    {GLOB_FAIL,  "te[^x]t", "text",           NULL},
    {GLOB_FAIL,  "te[^\\x]t", "text",         NULL},
    {GLOB_FAIL,  "te[^x\\", "text",           NULL},
    {GLOB_FAIL,  "te[/]t", "text",            NULL},
    {GLOB_MATCH, "te[r-t]t", "test",          NULL},
    {GLOB_MATCH, "te[r-Tz]t", "tezt",         NULL},
    {GLOB_FAIL,  "te[R-T]t", "tent",          NULL},
/*  {GLOB_MATCH, "tes[]t]", "test",           NULL}, */
    {GLOB_MATCH, "tes[t-]", "test",           NULL},
    {GLOB_MATCH, "tes[t-]]", "test]",         NULL},
    {GLOB_FAIL,  "tes[t-]]", "test",          NULL},
    {GLOB_FAIL,  "tes[u-]", "test",           NULL},
    {GLOB_FAIL,  "tes[t-]", "tes[t-]",        NULL},
    {GLOB_MATCH, "test[/-/]", "test/",        NULL},
    {GLOB_MATCH, "test[\\/-/]", "test/",      NULL},
    {GLOB_MATCH, "test[/-\\/]", "test/",      NULL},

#undef LIKE_MATCH
#undef LIKE_FAIL
#undef GLOB_MATCH
#undef GLOB_FAIL

    {FALSE, FALSE, NULL, NULL, NULL}
  };

  const struct glob_test_t *gt;
  svn_membuf_t bufa, bufb, bufc;
  svn_membuf__create(&bufa, 0, pool);
  svn_membuf__create(&bufb, 0, pool);
  svn_membuf__create(&bufc, 0, pool);

  srand(79);
  for (gt = glob_tests; gt->pattern; ++gt)
    {
      const svn_boolean_t implicit_size = (rand() % 13) & 1;
      const apr_size_t lenptn = (implicit_size
                                 ? SVN_UTF__UNKNOWN_LENGTH
                                 : strlen(gt->pattern));
      const apr_size_t lenstr = (implicit_size
                                 ? SVN_UTF__UNKNOWN_LENGTH
                                 : strlen(gt->string));
      const apr_size_t lenesc = (implicit_size
                                 ? SVN_UTF__UNKNOWN_LENGTH
                                 : (gt->escape ? strlen(gt->escape) : 0));
      svn_boolean_t match;
      svn_error_t *err;


      err = svn_utf__glob(&match,
                          gt->pattern, lenptn,
                          gt->string, lenstr,
                          gt->escape, lenesc,
                          gt->sql_like, &bufa, &bufb, &bufc);

      if (!gt->sql_like && gt->escape && !err)
        return svn_error_create
          (SVN_ERR_TEST_FAILED, err, "Failed to detect GLOB ESCAPE");

      if ((err && gt->matches)
          || (!err && !match != !gt->matches))
        {
          if (gt->sql_like)
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, err,
               "Wrong result: %s'%s' LIKE '%s'%s%s%s%s",
               (gt->matches ? "NOT " : ""), gt->string, gt->pattern,
               (gt->escape ? " ESCAPE " : ""), (gt->escape ? "'" : ""),
               (gt->escape ? gt->escape : ""), (gt->escape ? "'" : ""));
          else
            return svn_error_createf
              (SVN_ERR_TEST_FAILED, err, "Wrong result: %s%s GLOB %s",
               (gt->matches ? "NOT " : ""), gt->string, gt->pattern);
        }

      if (err)
        svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
test_utf_fuzzy_escape(apr_pool_t *pool)
{

  /* Accented latin, mixed normalization */
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

  /* As above, but latin lowercase 'o' replaced with Greek 'omicron' */
  static const char greekish[] =
    "S\xcc\x87\xcc\xa3"         /* S with dot above and below */
    "\xc5\xaf"                  /* u with ring */
    "b\xcc\xb1"                 /* b with macron below */
    "\xe1\xb9\xbd"              /* v with tilde */
    "e\xcc\xa7\xcc\x86"         /* e with breve and cedilla */
    "\xc8\x91"                  /* r with double grave */
    "s\xcc\x8c"                 /* s with caron */
    "\xe1\xb8\xaf"              /* i with diaeresis and acute */
    "\xce\xbf\xcc\x80\xcc\x9b"  /* omicron with grave and hook */
    "\xe1\xb9\x8b";             /* n with circumflex below */

  /* More interesting invalid characters. */
  static const char invalid[] =
    "Not Unicode: \xef\xb7\x91;"      /* U+FDD1 */
    "Out of range: \xf4\x90\x80\x81;" /* U+110001 */
    "Not UTF-8: \xe6;"
    "Null byte: \0;";

  const char *fuzzy;

  fuzzy = svn_utf__fuzzy_escape(mixup, strlen(mixup), pool);
  SVN_TEST_ASSERT(0 == strcmp(fuzzy, "Subversion"));

  fuzzy = svn_utf__fuzzy_escape(greekish, strlen(greekish), pool);
  SVN_TEST_ASSERT(0 == strcmp(fuzzy, "Subversi{U+03BF}n"));

  fuzzy = svn_utf__fuzzy_escape(invalid, sizeof(invalid) - 1, pool);
  /*fprintf(stderr, "%s\n", fuzzy);*/
  SVN_TEST_ASSERT(0 == strcmp(fuzzy,
                              "Not Unicode: {U?FDD1};"
                              "Out of range: ?\\F4?\\90?\\80?\\81;"
                              "Not UTF-8: ?\\E6;"
                              "Null byte: \\0;"));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_utf_is_normalized(apr_pool_t *pool)
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

  /* Invalid UTF-8 */
  static const char invalid[] =
    "\xe1\xb9\xa8"              /* S with dot above and below */
    "\xc5\xaf"                  /* u with ring */
    "\xe1\xb8\x87"              /* b with macron below */
    "\xe1\xb9\xbd"              /* v with tilde */
    "\xe1\xb8\x9d"              /* e with breve and cedilla */
    "\xc8\x91"                  /* r with double grave */
    "\xc5\xa1"                  /* s with caron */
    "\xe1\xb8\xaf"              /* i with diaeresis and acute */
    "\xe6"                      /* Invalid byte */
    "\xe1\xb9\x8b";             /* n with circumflex below */

  SVN_ERR_ASSERT(svn_utf__is_normalized(nfc, pool));
  SVN_ERR_ASSERT(!svn_utf__is_normalized(nfd, pool));
  SVN_ERR_ASSERT(!svn_utf__is_normalized(mixup, pool));
  SVN_ERR_ASSERT(!svn_utf__is_normalized(invalid, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_utf_conversions(apr_pool_t *pool)
{
  static const struct cvt_test_t
  {
    svn_boolean_t sixteenbit;
    svn_boolean_t bigendian;
    const char *source;
    const char *result;
  } tests[] = {

#define UTF_32_LE FALSE, FALSE
#define UTF_32_BE FALSE, TRUE
#define UTF_16_LE TRUE, FALSE
#define UTF_16_BE TRUE, TRUE

    /* Normal character conversion */
    { UTF_32_LE, "t\0\0\0" "e\0\0\0" "s\0\0\0" "t\0\0\0" "\0\0\0\0", "test" },
    { UTF_32_BE, "\0\0\0t" "\0\0\0e" "\0\0\0s" "\0\0\0t" "\0\0\0\0", "test" },
    { UTF_16_LE, "t\0" "e\0" "s\0" "t\0" "\0\0", "test" },
    { UTF_16_BE, "\0t" "\0e" "\0s" "\0t" "\0\0", "test" },

    /* Valid surrogate pairs */
    { UTF_16_LE, "\x00\xD8" "\x00\xDC" "\0\0", "\xf0\x90\x80\x80" }, /* U+010000 */
    { UTF_16_LE, "\x34\xD8" "\x1E\xDD" "\0\0", "\xf0\x9d\x84\x9e" }, /* U+01D11E */
    { UTF_16_LE, "\xFF\xDB" "\xFD\xDF" "\0\0", "\xf4\x8f\xbf\xbd" }, /* U+10FFFD */

    { UTF_16_BE, "\xD8\x00" "\xDC\x00" "\0\0", "\xf0\x90\x80\x80" }, /* U+010000 */
    { UTF_16_BE, "\xD8\x34" "\xDD\x1E" "\0\0", "\xf0\x9d\x84\x9e" }, /* U+01D11E */
    { UTF_16_BE, "\xDB\xFF" "\xDF\xFD" "\0\0", "\xf4\x8f\xbf\xbd" }, /* U+10FFFD */

    /* Swapped, single and trailing surrogate pairs */
    { UTF_16_LE, "*\0" "\x00\xDC" "\x00\xD8" "*\0\0\0", "*\xed\xb0\x80" "\xed\xa0\x80*" },
    { UTF_16_LE, "*\0" "\x1E\xDD" "*\0\0\0", "*\xed\xb4\x9e*" },
    { UTF_16_LE, "*\0" "\xFF\xDB" "*\0\0\0", "*\xed\xaf\xbf*" },
    { UTF_16_LE, "\x1E\xDD" "\0\0", "\xed\xb4\x9e" },
    { UTF_16_LE, "\xFF\xDB" "\0\0", "\xed\xaf\xbf" },

    { UTF_16_BE, "\0*" "\xDC\x00" "\xD8\x00" "\0*\0\0", "*\xed\xb0\x80" "\xed\xa0\x80*" },
    { UTF_16_BE, "\0*" "\xDD\x1E" "\0*\0\0", "*\xed\xb4\x9e*" },
    { UTF_16_BE, "\0*" "\xDB\xFF" "\0*\0\0", "*\xed\xaf\xbf*" },
    { UTF_16_BE, "\xDD\x1E" "\0\0", "\xed\xb4\x9e" },
    { UTF_16_BE, "\xDB\xFF" "\0\0", "\xed\xaf\xbf" },

#undef UTF_32_LE
#undef UTF_32_BE
#undef UTF_16_LE
#undef UTF_16_BE

    { 0 }
  };

  const struct cvt_test_t *tc;
  const svn_string_t *result;
  int i;

  for (i = 1, tc = tests; tc->source; ++tc, ++i)
    {
      if (tc->sixteenbit)
        SVN_ERR(svn_utf__utf16_to_utf8(&result, (const void*)tc->source,
                                       SVN_UTF__UNKNOWN_LENGTH,
                                       tc->bigendian, pool, pool));
      else
        SVN_ERR(svn_utf__utf32_to_utf8(&result, (const void*)tc->source,
                                       SVN_UTF__UNKNOWN_LENGTH,
                                       tc->bigendian, pool, pool));
      SVN_ERR_ASSERT(0 == strcmp(result->data, tc->result));
    }

  /* Test counted strings with NUL characters */
  SVN_ERR(svn_utf__utf16_to_utf8(
              &result, (void*)("x\0" "\0\0" "y\0" "*\0"), 3,
              FALSE, pool, pool));
  SVN_ERR_ASSERT(0 == memcmp(result->data, "x\0y", 3));

  SVN_ERR(svn_utf__utf32_to_utf8(
              &result,
              (void*)("\0\0\0x" "\0\0\0\0" "\0\0\0y" "\0\0\0*"), 3,
              TRUE, pool, pool));
  SVN_ERR_ASSERT(0 == memcmp(result->data, "x\0y", 3));

  return SVN_NO_ERROR;
}



/* The test table.  */

static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
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
    SVN_TEST_PASS2(test_utf_pattern_match,
                   "test svn_utf__glob"),
    SVN_TEST_PASS2(test_utf_fuzzy_escape,
                   "test svn_utf__fuzzy_escape"),
    SVN_TEST_PASS2(test_utf_is_normalized,
                   "test svn_utf__is_normalized"),
    SVN_TEST_PASS2(test_utf_conversions,
                   "test svn_utf__utf{16,32}_to_utf8"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
