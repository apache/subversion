/*
 * questions.c:  routines for asking questions about working copies
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
 * individuals on behalf of CollabNet.
 */



#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"



svn_error_t *
svn_wc__check_wc (svn_string_t *path, apr_pool_t *pool)
{
  /* Nothing fancy, just check for an administrative subdir and a
     `README' file. */ 
  apr_file_t *f = NULL;
  svn_error_t *err = NULL;

  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_README,
                               APR_READ, pool);

  /* It really doesn't matter what kind of error it is; for our
     purposes, this is not a working copy. */
  return err;

  /* Else. */
  err = svn_wc__close_adm_file (f, path, SVN_WC__ADM_README, 0, pool);
  return err;
}




/*** svn_wc__file_modified_p ***/

/* svn_wc__file_modified_p answers the question:

   "Are the contents of F different than the contents of SVN/text-base/F?"

   In other words, we're looking to see if a user has made local
   modifications to a file since the last update or commit.

   Note: Assuming that F lives in a directory D at version V, please
   notice that we are *NOT* answering the question, "are the contents
   of F different than version V of F?"  While F may be at a different
   version number than its parent directory, but we're only looking
   for local edits on F, not for consistent directory versions.  */



/* Determine if two file-stat structures contain "the same timestamp".

   There are three kinds of POSIX timestamps: 

    - when the file was last read     ("access time" or atime)
    - when the file was last written  ("modification time" or mtime)
    - when the inode last changed,
      *either* from a file write or an
      ownership/permission change     ("status time" or ctime)

   Since Subversion cares about things like ownership and permission
   bits, we need to check the ctime field.  When receiving an update,
   libsvn_wc should manually set the mtime (and thus ctime too) on
   both the original file and the "textbase" version of it.

   NOTE: if we decide *not* to set timestamps manually, we'll have to
   go with the CVS strategy of recording timestamps in a file... in
   which case, the routine below will need to be rewritten and will
   take different args.  */
static svn_boolean_t
timestamps_equal_p (apr_finfo_t *finfo1, apr_finfo_t *finfo2)
{
  if (finfo1->ctime == finfo2->ctime)
    return TRUE;

  else
    return FALSE;
}


/* Are the filesizes of two files the same? */
static svn_boolean_t
filesizes_equal_p (apr_finfo_t *finfo1, apr_finfo_t *finfo2)
{
  if (finfo1->size == finfo2->size)
    return TRUE;
  else
    return FALSE;
}


/* Do a byte-for-byte comparison of two previously-opened files, FILE1
   and FILE2.  FILE1 and FILE2 are _assumed_ to be identical in size.  */
static svn_error_t *
contents_identical_p (svn_boolean_t *identical_p,
                      apr_file_t *file1,
                      apr_file_t *file2,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_size_t bytes_read1, bytes_read2;
  char buf1[BUFSIZ], buf2[BUFSIZ];
 
  /* Strategy: repeatedly read BUFSIZ bytes from each file and
     memcmp() the bytestrings.  */

  *identical_p = TRUE;  /* assume TRUE, until disproved below */

  while (status != APR_EOF)
    {
      status = apr_full_read (file1, buf1, BUFSIZ, &bytes_read1);
      if (status)
        return svn_create_error
          (status, 0, NULL, pool, "apr_full_read() failed.");

      status = apr_full_read (file2, buf2, BUFSIZ, &bytes_read2);
      if (status)
        return svn_create_error
          (status, 0, NULL, pool, "apr_full_read() failed.");
      
      if (memcmp (buf1, buf2, bytes_read1)) 
        {
          *identical_p = FALSE;
          break;
        }
    }

  return SVN_NO_ERROR;
}


/* The public interface: has FILENAME been edited since the last
   update/commit?  Return answer in MODIFIED_P.   

   FILENAME is assumed to be a complete path, ending in the file's
   name.  */
svn_error_t *
svn_wc__file_modified_p (svn_boolean_t *modified_p,
                         svn_string_t *filename,
                         apr_pool_t *pool)
{
  apr_status_t status;
  svn_boolean_t identical_p;
  svn_error_t *err;
  apr_finfo_t current_stat;
  apr_finfo_t textbase_stat;
  apr_file_t *current_file = NULL;
  apr_file_t *textbase_file = NULL;

  /* Get filehandles for both the original and text-base versions of
     FILENAME */
  status = apr_open (&current_file, filename->data,
                     APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_create_error
      (status, 0, NULL, pool, "svn_wc__file_modified_p: apr_open failed.");

  err = svn_wc__open_text_base (&textbase_file, filename, APR_READ, pool);
  if (err)
    {
      char *msg =
        apr_psprintf
        (pool,
         "svn_wc__file_modified_p:  failed to open text-base copy of `%s'",
         filename->data);
      return svn_quick_wrap_error (err, msg);
    }
                     
  /* Get stat info on both files */
  status = apr_getfileinfo (&current_stat, current_file);
  if (status)
    return svn_create_error
      (status, 0, NULL, pool,
       "svn_wc__file_modified_p: apr_get_fileinfo failed.");

  status = apr_getfileinfo (&textbase_stat, textbase_file);
  if (status)
    return svn_create_error
      (status, 0, NULL, pool,
       "svn_wc__file_modified_p: apr_get_fileinfo failed.");


  /* Easy-answer attempt #1:  */
  if (timestamps_equal_p (&current_stat, &textbase_stat)
      && filesizes_equal_p (&current_stat, &textbase_stat))
    *modified_p = TRUE;

  /* Easy-answer attempt #2:  */
  else if (! filesizes_equal_p (&current_stat, &textbase_stat))
    *modified_p = FALSE;
  
  else {
    /* Give up and get the answer the hard way -- brute force! */
    err = contents_identical_p (&identical_p, 
                                current_file,
                                textbase_file,
                                pool);
    if (err)
      return err;

    if (identical_p)
      *modified_p = FALSE;
    else
      *modified_p = TRUE;
  }

  /* Close filehandles. */
  err = svn_wc__close_text_base (textbase_file, filename, 0, pool);
  if (err)
    return err;

  status = apr_close (current_file);
  if (status)
    return svn_create_error (status, 0, NULL, pool,
                             "svn_wc__file_modified_p: apr_close failed.");


  return SVN_NO_ERROR;
}






/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
