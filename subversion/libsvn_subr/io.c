/*
 * io.c:   shared file reading, writing, and probing code.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals and marine fronds on behalf of CollabNet.
 */



#include <apr_pools.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"



svn_error_t *
svn_io_check_path (svn_string_t *path,
                   enum svn_node_kind *kind,
                   apr_pool_t *pool)
{
  apr_finfo_t finfo;
  apr_status_t apr_err;

  apr_err = apr_stat (&finfo, path->data, pool);

  if (apr_err && (apr_err != APR_ENOENT))
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "svn_io_check_path: "
                              "problem checking path %s",
                              path->data);
  else if (apr_err == APR_ENOENT)
    *kind = svn_node_none;
  else if (finfo.filetype == APR_NOFILE)
    *kind = svn_node_unknown;
  else if (finfo.filetype == APR_REG)
    *kind = svn_node_file;
  else if (finfo.filetype == APR_DIR)
    *kind = svn_node_dir;
#if 0
  else if (finfo.filetype == APR_LINK)
    *kind = svn_node_symlink;  /* we support symlinks someday, but not yet */
#endif /* 0 */
  else
    *kind = svn_node_unknown;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_io_tmp_file (apr_file_t **f,
                 svn_string_t **tmp_name,
                 const svn_string_t *path,
                 const char *suffix,
                 apr_pool_t *pool)
{
  char number_buf[6];
  int i;
  apr_size_t iterating_portion_idx;

  /* The random portion doesn't have to be very random; it's just to
     avoid a series of collisions where someone has filename NAME and
     also NAME.00001.tmp, NAME.00002.tmp, etc, under version control
     already, which might conceivably happen.  The random portion is a
     last-ditch safeguard against that case.  It's okay, and even
     preferable, for tmp files to collide with each other, though, so
     that the iterating portion changes instead.  Taking the pointer
     as an unsigned short int has more or less this effect. */
  int random_portion_width;
  char *random_portion = apr_psprintf (pool, "%hu%n",
                                       tmp_name,
                                       &random_portion_width);

  *tmp_name = svn_string_dup (path, pool);

  /* Not sure of a portable PATH_MAX constant to use here, so just
     guessing at 255. */
  if ((*tmp_name)->len >= 255)
    {
      int chop_amt = ((*tmp_name)->len - 255)
                      + random_portion_width
                      + 3  /* 2 dots */
                      + 5  /* 5 digits of iteration portion */
                      + strlen (suffix);
      svn_string_chop (*tmp_name, chop_amt);
    }

  iterating_portion_idx = (*tmp_name)->len + random_portion_width + 2;
  svn_string_appendcstr (*tmp_name,
                         apr_psprintf (pool, ".%s.00000%s",
                                       random_portion, suffix));

  for (i = 1; i <= 99999; i++)
    {
      apr_status_t apr_err;

      /* Tweak last attempted name to get the next one. */
      sprintf (number_buf, "%05d", i);
      (*tmp_name)->data[iterating_portion_idx + 0] = number_buf[0];
      (*tmp_name)->data[iterating_portion_idx + 1] = number_buf[1];
      (*tmp_name)->data[iterating_portion_idx + 2] = number_buf[2];
      (*tmp_name)->data[iterating_portion_idx + 3] = number_buf[3];
      (*tmp_name)->data[iterating_portion_idx + 4] = number_buf[4];

      apr_err = apr_open (f, (*tmp_name)->data,
                          (APR_WRITE | APR_CREATE | APR_EXCL),
                          APR_OS_DEFAULT, pool);

      if (apr_err == APR_EEXIST)
        continue;
      else if (apr_err)
        {
          *f = NULL;
          *tmp_name = NULL;
          return svn_error_createf (apr_err,
                                    0,
                                    NULL,
                                    pool,
                                    "svn_io_tmp_name: "
                                    "error attempting %s", (*tmp_name)->data);
        }
      else
        return SVN_NO_ERROR;
    }

  *f = NULL;
  *tmp_name = NULL;
  return svn_error_createf (SVN_ERR_IO_TMP_NAMES_EXHAUSTED,
                            0,
                            NULL,
                            pool,
                            "svn_io_tmp_name: unable to make a tmp name for "
                            "%s", path->data);
}


svn_error_t *
svn_io_file_reader (void *filehandle,
                    char *buffer,
                    apr_size_t *len,
                    apr_pool_t *pool)
{
  apr_status_t stat;

  /* Recover our filehandle */
  apr_file_t *the_file = (apr_file_t *) filehandle;

  if (the_file == NULL)
    *len = 0;
  else
    {
      stat = apr_full_read (the_file, buffer,
                            (apr_size_t) *len,
                            (apr_size_t *) len);
      
      if (stat && (stat != APR_EOF)) 
        return
          svn_error_create (stat, 0, NULL, pool,
                            "adm_crawler.c (posix_file_reader): "
                            "file read error");
    }

  return SVN_NO_ERROR;  
}


svn_error_t *
svn_io_file_writer (void *filehandle,
                    const char *buffer,
                    apr_size_t *len,
                    apr_pool_t *pool)
{
  apr_file_t *dst = (apr_file_t *) filehandle;
  apr_status_t stat;
  
  stat = apr_full_write (dst, buffer, (apr_size_t) *len, (apr_size_t *) len);
  
  if (stat && (stat != APR_EOF))
    return
      svn_error_create (stat, 0, NULL, pool,
                        "error writing xml delta");
  else 
    return 0;  
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
