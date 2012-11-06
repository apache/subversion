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
  const ssize_t result = utf8proc_decompose((const void*)str, len,
                                            buffer, buffer_length,
                                            UTF8PROC_DECOMPOSE
                                            | UTF8PROC_STABLE
                                            | (len ? 0 : UTF8PROC_NULLTERM));
  if (result < 0)
    return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                            gettext(utf8proc_errmsg(result)));

  *result_length = (apr_size_t)result;
  return SVN_NO_ERROR;
}


int svn_utf__ucs4cmp(const apr_int32_t *bufa, apr_size_t lena,
                     const apr_int32_t *bufb, apr_size_t lenb)
{
  const apr_size_t len = (lena < lenb ? lena : lenb);
  apr_size_t i;

  for (i = 0; i < len; ++i)
    {
      const int diff = bufb[i] - bufa[i];
      if (diff)
        return diff;
    }
  return (lena == lenb ? 0 : (lena < lenb ? 1 : -1));
}
