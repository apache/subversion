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
#include "utf8proc/utf8proc.c.inline"

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
 * characters and *RESULT_LENGTH contains the length of the array.
 *
 * A returned error may indicate that STRING contains invalid UTF-8 or
 * invalid Unicode codepoints. Any error message comes from utf8proc.
 */
static svn_error_t *
decompose_normalized(apr_size_t *result_length,
                     const char *string, apr_size_t length,
                     svn_membuf_t *buffer)
{
  const int nullterm = (length == SVN_UTF__UNKNOWN_LENGTH
                        ? UTF8PROC_NULLTERM : 0);

  for (;;)
    {
      apr_int32_t *const ucs4buf = buffer->data;
      const ssize_t ucs4len = buffer->size / sizeof(*ucs4buf);
      const ssize_t result =
        utf8proc_decompose((const void*) string, length, ucs4buf, ucs4len,
                           UTF8PROC_DECOMPOSE | UTF8PROC_STABLE | nullterm);

      if (result < 0)
        return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                                gettext(utf8proc_errmsg(result)));

      if (result <= ucs4len)
        {
          *result_length = result;
          return SVN_NO_ERROR;
        }

      /* Increase the decomposition buffer size and retry */
      svn_membuf__ensure(buffer, result * sizeof(*ucs4buf));
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
svn_utf__normcmp(int *result,
                 const char *str1, apr_size_t len1,
                 const char *str2, apr_size_t len2,
                 svn_membuf_t *buf1, svn_membuf_t *buf2)
{
  apr_size_t buflen1;
  apr_size_t buflen2;

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

  SVN_ERR(decompose_normalized(&buflen1, str1, len1, buf1));
  SVN_ERR(decompose_normalized(&buflen2, str2, len2, buf2));
  *result = ucs4cmp(buf1->data, buflen1, buf2->data, buflen2);
  return SVN_NO_ERROR;
}


/* Decode a single UCS-4 code point to UTF-8, appending the result to BUFFER.
 * Assume BUFFER is already filled to *LENGTH and return the new size there.
 * This function does *not* nul-terminate the stringbuf!
 *
 * A returned error indicates that the codepoint is invalud.
 */
static svn_error_t *
encode_ucs4(svn_membuf_t *buffer, apr_int32_t ucs4chr, apr_size_t *length)
{
  apr_size_t utf8len;

  if (buffer->size - *length < 4)
    svn_membuf__resize(buffer, buffer->size + 4);

  utf8len = utf8proc_encode_char(ucs4chr, ((uint8_t*)buffer->data + *length));
  if (!utf8len)
    return svn_error_createf(SVN_ERR_UTF8PROC_ERROR, NULL,
                             _("Invalid Unicode character U+%04lX"),
                             (long)ucs4chr);
  *length += utf8len;
  return SVN_NO_ERROR;
}

/* Decode an UCS-4 string to UTF-8, placing the result into BUFFER.
 * While utf8proc does have a similar function, it does more checking
 * and processing than we want here. Return the lenght of the result
 * (excluding the NUL terminator) in *result_length.
 *
 * A returned error indicates that the codepoint is invalud.
 */
static svn_error_t *
encode_ucs4_string(svn_membuf_t *buffer,
                   apr_int32_t *ucs4str, apr_size_t len,
                   apr_size_t *result_length)
{
  *result_length = 0;
  while (len-- > 0)
    SVN_ERR(encode_ucs4(buffer, *ucs4str++, result_length));
  svn_membuf__resize(buffer, *result_length + 1);
  ((char*)buffer->data)[*result_length] = '\0';
  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf__glob(svn_boolean_t *match,
              const char *pattern, apr_size_t pattern_len,
              const char *string, apr_size_t string_len,
              const char *escape, apr_size_t escape_len,
              svn_boolean_t sql_like,
              svn_membuf_t *pattern_buf,
              svn_membuf_t *string_buf,
              svn_membuf_t *temp_buf)
{
  apr_size_t patternbuf_len;
  apr_size_t tempbuf_len;

  /* If we're in GLOB mode, we don't do custom escape chars. */
  if (escape && !sql_like)
    return svn_error_create(SVN_ERR_UTF8_GLOB, NULL,
                            _("Cannot use a custom escape token"
                              " in glob matching mode"));

  /* Convert the patern to NFD UTF-8. We can't use the UCS-4 result
     because apr_fnmatch can't handle it.*/
  SVN_ERR(decompose_normalized(&tempbuf_len, pattern, pattern_len, temp_buf));
  if (!sql_like)
    SVN_ERR(encode_ucs4_string(pattern_buf, temp_buf->data, tempbuf_len,
                               &patternbuf_len));
  else
    {
      /* Convert a LIKE pattern to a GLOB pattern that apr_fnmatch can use. */
      const apr_int32_t *like = temp_buf->data;
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
          if (result == 0 || result > 1)
            return svn_error_create(SVN_ERR_UTF8_GLOB, NULL,
                                    _("Escape token must be one character"));
          if ((ucs4esc & 0xFF) != ucs4esc)
            return svn_error_createf(SVN_ERR_UTF8_GLOB, NULL,
                                     _("Invalid escape character U+%04lX"),
                                     (long)ucs4esc);
        }

      patternbuf_len = 0;
      svn_membuf__ensure(pattern_buf, tempbuf_len + 1);
      for (i = 0, escaped = FALSE; i < tempbuf_len; ++i, ++like)
        {
          if (*like == ucs4esc && !escaped)
            {
              svn_membuf__resize(pattern_buf, patternbuf_len + 1);
              ((char*)pattern_buf->data)[patternbuf_len++] = '\\';
              escaped = TRUE;
            }
          else if (escaped)
            {
              SVN_ERR(encode_ucs4(pattern_buf, *like, &patternbuf_len));
              escaped = FALSE;
            }
          else
            {
              if ((*like == '[' || *like == '\\') && !escaped)
                {
                  /* Escape brackets and backslashes which are always
                     literals in LIKE patterns. */
                  svn_membuf__resize(pattern_buf, patternbuf_len + 1);
                  ((char*)pattern_buf->data)[patternbuf_len++] = '\\';
                  escaped = TRUE;
                  --i; --like;
                  continue;
                }

              /* Replace LIKE wildcards with their GLOB equivalents. */
              if (*like == '%' || *like == '_')
                {
                  const char wildcard = (*like == '%' ? '*' : '?');
                  svn_membuf__resize(pattern_buf, patternbuf_len + 1);
                  ((char*)pattern_buf->data)[patternbuf_len++] = wildcard;
                }
              else
                SVN_ERR(encode_ucs4(pattern_buf, *like, &patternbuf_len));
            }
        }
      svn_membuf__resize(pattern_buf, patternbuf_len + 1);
      ((char*)pattern_buf->data)[patternbuf_len] = '\0';
    }

  /* Now normalize the string */
  SVN_ERR(decompose_normalized(&tempbuf_len, string, string_len, temp_buf));
  SVN_ERR(encode_ucs4_string(string_buf, temp_buf->data,
                             tempbuf_len, &tempbuf_len));

  *match = !apr_fnmatch(pattern_buf->data, string_buf->data, 0);
  return SVN_NO_ERROR;
}
