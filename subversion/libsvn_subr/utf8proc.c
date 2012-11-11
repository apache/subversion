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

#include <apr_fnmatch.h>

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



/* Fill the given BUFFER with an NFD UCS-4 representation of the UTF-8
 * STRING. If LENGTH is SVN_UTF__UNKNOWN_LENGTH, assume STRING is
 * NUL-terminated; otherwise look only at the first LENGTH bytes in
 * STRING. Upon return, BUFFER->data points at an array of UCS-4
 * characters and BUFFER->len contains the length of the array.
 *
 * This function really horribly abuses stringbufs, because the result
 * does not conform to published stringbuf semantics. However, these
 * results are never used outside the utf8proc wrappers.
 *
 * A returned error may indicate that STRING contains invalid UTF-8 or
 * invalid Unicode codepoints. Any error message comes from utf8proc.
 */
static svn_error_t *
decompose_normalized(const char *string, apr_size_t length,
                     svn_stringbuf_t *buffer)
{
  const int nullterm = (length == SVN_UTF__UNKNOWN_LENGTH
                        ? UTF8PROC_NULLTERM : 0);

  for (;;)
    {
      apr_int32_t *const ucs4buf = (void *)buffer->data;
      const ssize_t ucs4len = buffer->blocksize / sizeof(*ucs4buf);
      const ssize_t result =
        utf8proc_decompose((const void*) string, length, ucs4buf, ucs4len,
                           UTF8PROC_DECOMPOSE | UTF8PROC_STABLE | nullterm);

      if (result < 0)
        return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                                gettext(utf8proc_errmsg(result)));

      if (result <= ucs4len)
        {
          buffer->len = result;
          return SVN_NO_ERROR;
        }

      /* Increase the decomposition buffer size and retry */
      svn_stringbuf__reserve(buffer, result * sizeof(*ucs4buf));
    }
}


/* Compare two arrays of UCS-4 codes, BUFA of length LENA and BUFB of
 * length LENB. Return 0 if they're equal, a negative value if BUFA is
 * less than BUFB, otherwise a positive value.
 *
 * Yes, this is strcmp for known-length UCS-4 strings.
 */
static int
ucs4cmp(const apr_int32_t *bufa, apr_size_t lena,
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
svn_utf__normcmp(const char *str1, apr_size_t len1,
                 const char *str2, apr_size_t len2,
                 svn_stringbuf_t *buf1, svn_stringbuf_t *buf2,
                 int *result)
{
  /* Shortcut-circuit the decision if at least one of the strings is empty. */
  const svn_boolean_t empty1 =
    (0 == len1 || (len1 == SVN_UTF__UNKNOWN_LENGTH && !*str1));
  const svn_boolean_t empty2 =
    (0 == len2 || (len2 == SVN_UTF__UNKNOWN_LENGTH && !*str2));
  if (empty1 || empty2)
    {
      *result = (empty1 == empty2 ? 0 : (empty1 ? -1 : 1));
      return SVN_NO_ERROR;
    }

  SVN_ERR(decompose_normalized(str1, len1, buf1));
  SVN_ERR(decompose_normalized(str2, len2, buf2));
  *result = ucs4cmp((void *)buf1->data, buf1->len,
                    (void *)buf2->data, buf2->len);
  return SVN_NO_ERROR;
}


/* Decode a single UCS-4 code point to UTF-8, appending the result to BUFFER.
 * This function does *not* nul-terminate the stringbuf!
 *
 * A returned error indicates that the codepoint is invalud.
 */
static svn_error_t *
encode_ucs4(svn_stringbuf_t *buffer, apr_int32_t ucs4chr)
{
  apr_size_t utf8len;

  if (buffer->blocksize - buffer->len < 4)
    svn_stringbuf_ensure(buffer, 2 * buffer->blocksize - 1);

  utf8len = utf8proc_encode_char(ucs4chr,
                                 (void *)(buffer->data + buffer->len));
  if (!utf8len)
    return svn_error_createf(SVN_ERR_UTF8PROC_ERROR, NULL,
                             "Invalid Unicode character U+%04lX",
                             (long)ucs4chr);
  buffer->len += utf8len;
  return SVN_NO_ERROR;
}

/* Decode an UCS-4 string to UTF-8, placing the result into BUFFER.
 * While utf8proc does have a similar function, it does more checking
 * and processing than we want here.
 *
 * A returned error indicates that the codepoint is invalud.
 */
static svn_error_t *
encode_ucs4_string(svn_stringbuf_t *buffer,
                   apr_int32_t *ucs4str, apr_size_t len)
{
  svn_stringbuf_setempty(buffer);
  while (len-- > 0)
    SVN_ERR(encode_ucs4(buffer, *ucs4str++));
  buffer->data[buffer->len] = '\0';
  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf__glob(const char *pattern, apr_size_t pattern_len,
              const char *string, apr_size_t string_len,
              const char *escape, apr_size_t escape_len,
              svn_boolean_t sql_like,
              svn_stringbuf_t *pattern_buf,
              svn_stringbuf_t *string_buf,
              svn_stringbuf_t *temp_buf,
              svn_boolean_t *match)
{
  /* If we're in LIKE mode, we don't do custom escape chars. */
  if (escape && !sql_like)
    return svn_error_create(SVN_ERR_UTF8_GLOB, NULL,
                            "The GLOB operator does not allow"
                            " a custom escape character");

  /* Convert the patern to NFD UTF-8. We can't use the UCS-4 result
     because apr_fnmatch can't handle it.*/
  SVN_ERR(decompose_normalized(pattern, pattern_len, temp_buf));
  if (!sql_like)
    SVN_ERR(encode_ucs4_string(pattern_buf,
                               (void *)temp_buf->data,
                               temp_buf->len));
  else
    {
      /* Convert a LIKE pattern to a GLOB pattern that apr_fnmatch can use. */
      const apr_int32_t *like = (void *)temp_buf->data;
      apr_int32_t ucs4esc;
      svn_boolean_t escaped;
      apr_size_t i;

      if (!escape)
        ucs4esc = -1;           /* Definitely an invalid UCS-4 character. */
      else
        {
          const int nullterm = (escape_len == SVN_UTF__UNKNOWN_LENGTH
                                ? UTF8PROC_NULLTERM : 0);
          ssize_t result =
            utf8proc_decompose((const void*) escape, escape_len, &ucs4esc, 1,
                               UTF8PROC_DECOMPOSE | UTF8PROC_STABLE | nullterm);
          if (result < 0)
            return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                                    gettext(utf8proc_errmsg(result)));
          if (result > 1)
            return svn_error_create(SVN_ERR_UTF8_GLOB, NULL,
                                    "The ESCAPE parameter is too long");
          if ((ucs4esc & 0xFF) != ucs4esc)
            return svn_error_createf(SVN_ERR_UTF8_GLOB, NULL,
                                     "Invalid ESCAPE character U+%04lX",
                                     (long)ucs4esc);
        }

      svn_stringbuf_setempty(pattern_buf);
      for (i = 0, escaped = FALSE; i < temp_buf->len; ++i, ++like)
        {
          if (*like == ucs4esc && !escaped)
            {
              svn_stringbuf_appendbyte(pattern_buf, '\\');
              escaped = TRUE;
            }
          else if (escaped)
            {
              SVN_ERR(encode_ucs4(pattern_buf, *like));
              escaped = FALSE;
            }
          else
            {
              if ((*like == '[' || *like == '\\') && !escaped)
                {
                  /* Escape brackets and backslashes which are always
                     literals in LIKE patterns. */
                  svn_stringbuf_appendbyte(pattern_buf, '\\');
                  escaped = TRUE;
                  --i; --like;
                  continue;
                }

              /* Replace LIKE wildcards with their GLOB equivalents. */
              if (*like == '%')
                  svn_stringbuf_appendbyte(pattern_buf, '*');
              else if (*like == '_')
                  svn_stringbuf_appendbyte(pattern_buf, '?');
              else
                SVN_ERR(encode_ucs4(pattern_buf, *like));
            }
        }
      pattern_buf->data[pattern_buf->len] = '\0';
    }

  /* Now normalize the string */
  SVN_ERR(decompose_normalized(string, string_len, temp_buf));
    SVN_ERR(encode_ucs4_string(string_buf,
                               (void *)temp_buf->data,
                               temp_buf->len));

  *match = !apr_fnmatch(pattern_buf->data, string_buf->data, 0);
  return SVN_NO_ERROR;
}
