/*
 * subst_translate-test.c -- test the svn_subst_translate* functions
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

#include <stddef.h>
#include <string.h>

#include <apr.h>
#include <apr_errno.h>
#include <apr_general.h>

#include "../svn_test.h"

#include "svn_types.h"
#include "svn_error_codes.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_subst.h"

static char s_buf[2053];

/**
 * Converts the string STR to C code for a string literal that represents it.
 *
 * Not thread safe. The returned pointer is to a static buffer, so it does
 * not need to be freed.
 */
static char *
strtocsrc(const char *str)
{
  char *p = s_buf;
  size_t i;

  *p++ = '"';

  for (i = 0; str[i] != '\0' && i < 512; ++i) {
    sprintf(p, "\\x%02x", (int) str[i]);
    p += 4;
  }

  if (i < 512) {
    *p++ = '"';
  } else {
    *p++ = '.';
    *p++ = '.';
    *p++ = '.';
    /* no terminating double quote character */
  }

  *p = '\0';

  return s_buf;
}

static svn_error_t *
test_svn_subst_translate_string2(apr_pool_t *pool)
{
  svn_string_t *new_value;
  svn_boolean_t translated_to_utf8, translated_line_endings;

  {
  /* No reencoding, no translation of line endings */
  const char data0[] = "abcdefz";
  svn_string_t *string0 = svn_string_create(data0, pool);
  new_value = NULL;
  translated_line_endings = TRUE;
  SVN_ERR(svn_subst_translate_string2(&new_value,
                                      NULL, &translated_line_endings,
                                      string0, "UTF-8", pool, pool));
  if (strcmp(new_value->data, data0) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING0 should "
                             "yield \"abcdefz\".");
  if (translated_line_endings)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING0 should "
                             "reset TRANSLATED_LINE_ENDINGS to FALSE because "
                             "the string does not contain a new line to "
                             "translate.");
  new_value = NULL;
  translated_to_utf8 = TRUE;
  translated_line_endings = TRUE;
  SVN_ERR(svn_subst_translate_string2(&new_value, &translated_to_utf8,
                                      &translated_line_endings,
                                      string0, "ISO-8859-1", pool, pool));
  if (strcmp(new_value->data, data0) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING0 "
                             "should yield \"abcdefz\".");
  if (translated_to_utf8)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING0 should "
                             "reset TRANSLATED_TO_UTF8 to FALSE because "
                             "the string should not be changed as a result of "
                             "reencoding to UTF-8.");
  if (translated_line_endings)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING0 "
                             "should reset TRANSLATED_LINE_ENDINGS to FALSE "
                             "because the string does not contain a new line "
                             "to translate.");
  }

  {
  /* No reencoding, translation of line endings */
  const char data1[] = "     \r\n\r\n      \r\n        \r\n";
  svn_string_t *string1 = svn_string_create(data1, pool);
  const char expected_result1[] = "     \n\n      \n        \n";
  new_value = NULL;
  translated_line_endings = FALSE;
  SVN_ERR(svn_subst_translate_string2(&new_value,
                                      NULL, &translated_line_endings,
                                      string1, "UTF-8", pool, pool));
  if (strcmp(new_value->data, expected_result1) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING1 should "
                             "yield \"     \\n\\n      \\n        \\n\".");
  if (! translated_line_endings)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING1 should "
                             "set TRANSLATED_LINE_ENDINGS to TRUE because the "
                             "string has Windows-style line endings that "
                             "were translated.");
  new_value = NULL;
  translated_to_utf8 = TRUE;
  translated_line_endings = FALSE;
  SVN_ERR(svn_subst_translate_string2(&new_value, &translated_to_utf8,
                                      &translated_line_endings,
                                      string1, "ISO-8859-1", pool, pool));
  if (strcmp(new_value->data, expected_result1) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING1 "
                             "should yield \"     \\n\\n      \\n        \\n\""
                             ".");
  if (translated_to_utf8)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING1 should "
                             "reset TRANSLATED_TO_UTF8 to FALSE because "
                             "the string should not be changed as a result of "
                             "reencoding to UTF-8.");
  if (! translated_line_endings)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING1 "
                             "should set TRANSLATED_LINE_ENDINGS to TRUE "
                             "because the string has Windows-style line "
                             "endings that were translated.");
  }

  {
  /* Reencoding, no translation of line endings */
  const char data2[] = "\xc7\xa9\xf4\xdf";
  svn_string_t *string2 = svn_string_create(data2, pool);
  const char expected_result2[] = "\xc3\x87\xc2\xa9\xc3\xb4\xc3\x9f";
  new_value = NULL;
  translated_to_utf8 = FALSE;
  SVN_ERR(svn_subst_translate_string2(&new_value,
                                      &translated_to_utf8, NULL,
                                      string2, "ISO-8859-1", pool, pool));
  if (strcmp(new_value->data, expected_result2) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING2 should "
                             "yield \"\\xc3\\x87\\xc2\\xa9\\xc3\\xb4\\xc3\\x9f"
                             "\". Instead, got %s.",
                             strtocsrc(new_value->data));
  if (! translated_to_utf8)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING2 should "
                             "set TRANSLATED_TO_UTF8 to TRUE.");
  new_value = NULL;
  translated_to_utf8 = FALSE;
  translated_line_endings = TRUE;
  SVN_ERR(svn_subst_translate_string2(&new_value, &translated_to_utf8,
                                      &translated_line_endings,
                                      string2, "ISO-8859-1", pool, pool));
  if (strcmp(new_value->data, expected_result2) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING2 "
                             "should yield \"\\xc3\\x87\\xc2\\xa9\\xc3\\xb4"
                             "\\xc3\\x9f\". Instead, got %s.",
                             strtocsrc(new_value->data));
  if (! translated_to_utf8)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING2 "
                             "should set TRANSLATED_TO_UTF8 to TRUE.");
  if (translated_line_endings)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING2 should "
                             "reset TRANSLATED_LINE_ENDINGS to FALSE.");
  }

  {
  /* Reencoding, translation of line endings */
  const char data3[] = "\xc7\xa9\xf4\xdf\r\n";
  svn_string_t *string3 = svn_string_create(data3, pool);
  const char expected_result3[] = "\xc3\x87\xc2\xa9\xc3\xb4\xc3\x9f\n";
  new_value = NULL;
  translated_to_utf8 = FALSE;
  SVN_ERR(svn_subst_translate_string2(&new_value,
                                      &translated_to_utf8, NULL,
                                      string3, "ISO-8859-1", pool, pool));
  if (strcmp(new_value->data, expected_result3) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING3 should "
                             "yield \"\\xc3\\x87\\xc2\\xa9\\xc3\\xb4\\xc3\\x9f"
                             "\\x0a\". Instead, got %s.",
                             strtocsrc(new_value->data));
  if (! translated_to_utf8)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING3 should "
                             "set TRANSLATED_TO_UTF8 to TRUE.");
  new_value = NULL;
  translated_to_utf8 = FALSE;
  translated_line_endings = FALSE;
  SVN_ERR(svn_subst_translate_string2(&new_value, &translated_to_utf8,
                                      &translated_line_endings,
                                      string3, "ISO-8859-1", pool, pool));
  if (strcmp(new_value->data, expected_result3) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING3 "
                             "should yield \"\\xc3\\x87\\xc2\\xa9\\xc3\\xb4"
                             "\\xc3\\x9f\\x0a\". Instead, got %s.",
                             strtocsrc(new_value->data));
  if (! translated_to_utf8)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "(2) svn_subst_translate_string2() on STRING3 "
                             "should set TRANSLATED_TO_UTF8 to TRUE.");
  if (! translated_line_endings)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_string2() on STRING3 should "
                             "set TRANSLATED_LINE_ENDINGS to TRUE.");
  }

  return SVN_NO_ERROR;
}

/*static svn_error_t *
test_svn_subst_copy_and_translate4(apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
test_svn_subst_stream_translated(apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}*/

static svn_error_t *
test_svn_subst_translate_cstring2(apr_pool_t *pool)
{
  {
  /* Test the unusual case where EOL_STR is an empty string. */
  const char src0[] = "   \r   \n\r\n     \n\n\n";
  const char *dest0 = NULL;
  const char expected_result0[] = "           ";
  SVN_ERR(svn_subst_translate_cstring2(src0, &dest0, "", TRUE, NULL, FALSE,
                                       pool));
  if (strcmp(dest0, expected_result0) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_cstring2() on SRC0 should "
                             "yield \"           \".");
  }

  {
  /* Test the unusual case where EOL_STR is not a standard EOL string. */
  const char src1[] = "   \r   \n\r\n     \n\n\n";
  const char *dest1 = NULL;
  const char expected_result1[] = "   z   zz     zzz";
  SVN_ERR(svn_subst_translate_cstring2(src1, &dest1, "z", TRUE, NULL, FALSE,
                                       pool));
  if (strcmp(dest1, expected_result1) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_cstring2() on SRC1 should "
                             "yield \"   z   zz     zzz\".");
  }
  {
  const char src2[] = "    \n    \n ";
  const char *dest2 = NULL;
  const char expected_result2[] = "    buzz    buzz ";
  SVN_ERR(svn_subst_translate_cstring2(src2, &dest2, "buzz", FALSE, NULL, FALSE,
                                       pool));
  if (strcmp(dest2, expected_result2) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_cstring2() on SRC2 should "
                             "yield \"    buzz    buzz \".");
  }
  {
  const char src3[] = "    \r\n    \n";
  const char *dest3 = NULL;
  const char expected_result3[] = "    buzz    buzz";
  SVN_ERR(svn_subst_translate_cstring2(src3, &dest3, "buzz", TRUE, NULL, FALSE,
                                       pool));
  if (strcmp(dest3, expected_result3) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "svn_subst_translate_cstring2() on SRC3 should "
                             "yield \"    buzz    buzz\".");
  }

  return SVN_NO_ERROR;
}

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_svn_subst_translate_string2,
                   "test svn_subst_translate_string2()"),
    SVN_TEST_PASS2(test_svn_subst_translate_cstring2,
                   "test svn_subst_translate_cstring2()"),
    SVN_TEST_NULL
  };
