/*
 * utf8proc.c:  Wrappers for the utf8proc library
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



#define UTF8PROC_INLINE
#include "utf8proc/utf8proc.c"

#include "private/svn_string_private.h"
#include "private/svn_utf_private.h"
#include "svn_private_config.h"
#define UNUSED(x) ((void)(x))


const char *svn_utf__utf8proc_version(void)
{
  /* Unused static function warning removal hack. */
  UNUSED(utf8proc_codepoint_valid);
  UNUSED(utf8proc_NFD);
  UNUSED(utf8proc_NFC);
  UNUSED(utf8proc_NFKD);
  UNUSED(utf8proc_NFKC);

  return utf8proc_version();
}


svn_error_t *
svn_utf__decompose_normalized(const char *str, apr_size_t len,
                              apr_int32_t *buffer, apr_size_t buffer_length,
                              apr_size_t *result_length)
{
  const int nullterm = (len == SVN_UTF__UNKNOWN_LENGTH ? UTF8PROC_NULLTERM : 0);
  const ssize_t result = utf8proc_decompose((const void*)str, len,
                                            buffer, buffer_length,
                                            UTF8PROC_DECOMPOSE
                                            | UTF8PROC_STABLE | nullterm);
  if (result < 0)
    return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                            gettext(utf8proc_errmsg(result)));

  *result_length = (apr_size_t)result;
  return SVN_NO_ERROR;
}


int
svn_utf__ucs4cmp(const apr_int32_t *bufa, apr_size_t lena,
                 const apr_int32_t *bufb, apr_size_t lenb)
{
  const apr_size_t len = (lena < lenb ? lena : lenb);
  apr_size_t i;

  for (i = 0; i < len; ++i)
    {
      const int diff = bufa[i] - bufb[i];
      if (diff)
        return diff;
    }
  return (lena == lenb ? 0 : (lena < lenb ? -1 : 1));
}


svn_error_t *
svn_utf__encode_ucs4_to_stringbuf(apr_int32_t ucs4, svn_stringbuf_t *buf)
{
  char utf8buf[8];     /* The longest UTF-8 sequence has 4 bytes */
  const apr_size_t utf8len = utf8proc_encode_char(ucs4, (void *)utf8buf);

  if (utf8len)
    {
      svn_stringbuf_appendbytes(buf, utf8buf, utf8len);
      return SVN_NO_ERROR;
    }

  return svn_error_createf(SVN_ERR_UTF8PROC_ERROR, NULL,
                           "Invalid Unicode character U+%04lX",
                           (long)ucs4);
}


/* Decompose the given UTF-8 KEY of length KEYLEN.  This function
   really horribly abuses stringbufs, because the result does not
   conform to published stringbuf semantics. However, these results
   should never be used outside the very carefully closed world of
   SQLite extensions.
 */
static svn_error_t *
decompose_normcmp_arg(const void *arg, apr_size_t arglen,
                      svn_stringbuf_t *buf)
{
  for (;;)
    {
      apr_int32_t *const ucsbuf = (void *)buf->data;
      const apr_size_t ucslen = buf->blocksize / sizeof(*ucsbuf);
      SVN_ERR(svn_utf__decompose_normalized(arg, arglen, ucsbuf, ucslen,
                                            &buf->len));
      if (buf->len <= ucslen)
        return SVN_NO_ERROR;

      /* Increase the decomposition buffer size and retry */
      svn_stringbuf__reserve(buf, buf->len * sizeof(*ucsbuf));
    }
}

svn_error_t *
svn_utf__normcmp(const void *str1, apr_size_t len1,
                 const void *str2, apr_size_t len2,
                 svn_stringbuf_t *buf1, svn_stringbuf_t *buf2,
                 int *result)
{
  /* Shortcut-circuit the decision if at least one of the strings is empty. */
  const svn_boolean_t empty1 = (0 == len1
                                || (len1 == SVN_UTF__UNKNOWN_LENGTH
                                    && !*(const char*)str1));
  const svn_boolean_t empty2 = (0 == len2
                                || (len2 == SVN_UTF__UNKNOWN_LENGTH
                                    && !*(const char*)str2));
  if (empty1 || empty2)
    {
      *result = (empty1 == empty2 ? 0 : (empty1 ? -1 : 1));
      return SVN_NO_ERROR;
    }

  SVN_ERR(decompose_normcmp_arg(str1, len1, buf1));
  SVN_ERR(decompose_normcmp_arg(str2, len2, buf2));
  *result = svn_utf__ucs4cmp((void *)buf1->data, buf1->len,
                             (void *)buf2->data, buf2->len);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf__glob(const void *pattern, apr_size_t pattern_len,
              const void *string, apr_size_t string_len,
              const void *escape, apr_size_t escape_len,
              svn_stringbuf_t *buf1, svn_stringbuf_t *buf2,
              svn_boolean_t sql_like, svn_boolean_t *match)
{
  return SVN_NO_ERROR;
}
