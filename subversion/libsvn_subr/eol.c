/*
 * eol.c :  generic eol/keyword routines
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



#define APR_WANT_STRFUNC

#include <apr_file_io.h>
#include "svn_io.h"
#include "private/svn_eol_private.h"

char *
svn_eol__find_eol_start(char *buf, apr_size_t len)
{
  for (; len > 0; ++buf, --len)
    {
      if (*buf == '\n' || *buf == '\r')
        return buf;
    }
  return NULL;
}

const char *
svn_eol__detect_eol(char *buf, char *endp)
{
  const char *eol;

  SVN_ERR_ASSERT_NO_RETURN(buf <= endp);
  eol = svn_eol__find_eol_start(buf, endp - buf);
  if (eol)
    {
      if (*eol == '\n')
        return "\n";

      /* We found a CR. */
      ++eol;
      if (eol == endp || *eol != '\n')
        return "\r";
      return "\r\n";
    }

  return NULL;
}

svn_error_t *
svn_eol__detect_file_eol(const char **eol, apr_file_t *file, apr_pool_t *pool)
{
  char buf[512];
  apr_size_t nbytes;
  svn_error_t *err;
  apr_off_t orig_pos;
  apr_off_t pos;

  /* Remember original file offset. */
  orig_pos = 0;
  SVN_ERR(svn_io_file_seek(file, APR_CUR, &orig_pos, pool));

  do
    {
      memset(buf, '\0', sizeof(buf));

      /* Read a chunk. */
      nbytes = sizeof(buf);
      err = svn_io_file_read(file, buf, &nbytes, pool);
      if (err)
        {
          /* An error occurred. We're going to return in any case,
           * so reset the file cursor right now. */
          pos = orig_pos;
          SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, pool));
          SVN_ERR_ASSERT(orig_pos == pos);

          /* If we reached the end of the file, the file has no
           * EOL markers at all... */
          if (APR_STATUS_IS_EOF(err->apr_err))
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            {
              /* Whatever happened, it's something we don't know how
               * to deal with. Just return the error. */
              return svn_error_return(err);
            }
        }

      /* Try to detect the EOL style of the file by searching the
       * current chunk. */
      SVN_ERR_ASSERT(nbytes <= sizeof(buf));
      *eol = svn_eol__detect_eol(buf, buf + nbytes);
    }
  while (*eol == NULL);

  /* We're done, reset the file cursor to the original offset. */
  pos = orig_pos;
  SVN_ERR(svn_io_file_seek(file, APR_SET, &pos, pool));
  SVN_ERR_ASSERT(orig_pos == pos);

  return SVN_NO_ERROR;
}
