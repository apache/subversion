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



/* Is FILENAME's timestamp the same as the one recorded in our
   `entries' file?  Return the answer in EQUAL_P.  */
static svn_error_t *
timestamps_equal_p (svn_boolean_t *equal_p,
                    svn_string_t *filename,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_time_t wfile_time, entry_time;
  svn_string_t *dirpath, *entryname;
  apr_hash_t *entries = NULL;
  struct svn_wc__entry_t *entry;

  svn_path_split (filename, &dirpath, &entryname, svn_path_local_style, pool);

  err = svn_wc__entries_read (&entries, dirpath, pool);
  if (err)
    return err;
  entry = apr_hash_get (entries, entryname->data, entryname->len);

  if (! entry->timestamp)
    {
      /* This entry has no timestamp, so the only the safe thing to do
         is return FALSE, i.e. "different" timestamps. */
      *equal_p = FALSE;
      return SVN_NO_ERROR;
    }

  /* Get the timestamp from the working file */
  err = svn_wc__file_affected_time (&wfile_time, filename, pool);
  if (err)
    return err;

  {
    /* Put the disk timestamp through a string conversion, so it's
       at the same resolution as entry timestamps. */
    svn_string_t *tstr = svn_wc__time_to_string (wfile_time, pool);
    wfile_time = svn_wc__string_to_time (tstr);
  }

  if (wfile_time == entry_time)
    *equal_p = TRUE;
  else
    *equal_p = FALSE;

  return SVN_NO_ERROR;
}




/* Given FILENAME1 and FILENAME2, are their filesizes the same?
   Return answer in EQUAL_P. */
static svn_error_t *
filesizes_equal_p (svn_boolean_t *equal_p,
                   svn_string_t *filename1,
                   svn_string_t *filename2,
                   apr_pool_t *pool)
{
  apr_finfo_t finfo1;
  apr_finfo_t finfo2;
  apr_status_t status;

  /* Stat both files */
  status = apr_stat (&finfo1, filename1->data, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "filesizes_equal_p: apr_stat failed on `%s'", filename1->data);

  status = apr_stat (&finfo2, filename2->data, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "filesizes_equal_p: apr_stat failed on `%s'", filename2->data);

  /* Examine file sizes */
  if (finfo1.size == finfo2.size)
    *equal_p = TRUE;
  else
    *equal_p = FALSE;

  return SVN_NO_ERROR;
}


/* Do a byte-for-byte comparison of the local version and text-base
   version of FILENAME.  These two files are assumed to be the *same*
   size already (i.e. have already passed the filesizes_equal_p()
   test). */
static svn_error_t *
contents_identical_p (svn_boolean_t *identical_p,
                      svn_string_t *filename,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t status;
  apr_size_t bytes_read1, bytes_read2;
  char buf1[BUFSIZ], buf2[BUFSIZ];
  apr_file_t *local_file = NULL;
  apr_file_t *textbase_file = NULL;

  /* Get filehandles for both the original and text-base versions of
     FILENAME */
  status = apr_open (&local_file, filename->data,
                     APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_open failed on `%s'", filename->data);

  err = svn_wc__open_text_base (&textbase_file, filename, APR_READ, pool);
  if (err)
    {
      char *msg =
        apr_psprintf
        (pool,
         "contents_identical_p: failed to open text-base copy of `%s'",
         filename->data);
      return svn_error_quick_wrap (err, msg);
    }

 
  /* Strategy: repeatedly read BUFSIZ bytes from each file and
     memcmp() the bytestrings.  */

  *identical_p = TRUE;  /* assume TRUE, until disproved below */

  while (status != APR_EOF)
    {
      status = apr_full_read (local_file, buf1, BUFSIZ, &bytes_read1);
      if (status && (status != APR_EOF))
        return svn_error_create
          (status, 0, NULL, pool, "apr_full_read() failed.");

      status = apr_full_read (textbase_file, buf2, BUFSIZ, &bytes_read2);
      if (status && (status != APR_EOF))
        return svn_error_create
          (status, 0, NULL, pool, "apr_full_read() failed.");
      
      if (memcmp (buf1, buf2, bytes_read1)) 
        {
          *identical_p = FALSE;
          break;
        }
    }

  /* Close filehandles. */
  err = svn_wc__close_text_base (textbase_file, filename, 0, pool);
  if (err)
    return err;

  status = apr_close (local_file);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "contents_identical_p: apr_close failed.");

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
  svn_boolean_t identical_p;
  svn_error_t *err;
  svn_string_t *textbase_filename;
  svn_boolean_t equal_filesizes, equal_timestamps;
                     
  /* Get the full path of the textbase version of filename */
  textbase_filename = svn_wc__text_base_path (filename, 0, pool);

  /* Easy-answer attempt #1:  */
  if (textbase_filename)
    {
      /* See if the the local and textbase file are the same size. */
      err = filesizes_equal_p (&equal_filesizes,
                               filename, textbase_filename,
                               pool);
      if (err) return err;

      if (! equal_filesizes) 
        {
          *modified_p = TRUE;
          return SVN_NO_ERROR;
        }
    }

  /* Easy-answer attempt #2:  */

  /* See if the local file's timestamp is the same as the one recorded
     in the administrative directory.  */
  err = timestamps_equal_p (&equal_timestamps, filename, pool);
  if (err) return err;

  if (equal_timestamps)
    {
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }
  
  else 
    {
      /* If we get here, then we know that the filesizes are the same,
         but the timestamps are different.  That's still not enough
         evidence to make a correct decision.  So we just give up and
         get the answer the hard way -- a brute force, byte-for-byte
         comparison. */
      err = contents_identical_p (&identical_p, filename, pool);
      if (err)
        return err;
      
      if (identical_p)
        *modified_p = FALSE;
      else
        *modified_p = TRUE;

      return SVN_NO_ERROR;
    }
}






/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
