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




/*** svn_wc_text_modified_p ***/

/* svn_wc_text_modified_p answers the question:

   "Are the contents of F different than the contents of SVN/text-base/F?"

   or

   "Are the contents of SVN/props/xxx different than SVN/prop-base/xxx?"

   In other words, we're looking to see if a user has made local
   modifications to a file since the last update or commit.

   Note: Assuming that F lives in a directory D at revision V, please
   notice that we are *NOT* answering the question, "are the contents
   of F different than revision V of F?"  While F may be at a different
   revision number than its parent directory, but we're only looking
   for local edits on F, not for consistent directory revisions.  

   TODO:  the logic of the routines on this page might change in the
   future, as they bear some relation to the user interface.  For
   example, if a file is removed -- without telling subversion about
   it -- how should subversion react?  Should it copy the file back
   out of text-base?  Should it ask whether one meant to officially
   mark it for removal?
*/

enum svn_wc__timestamp_kind
{
  svn_wc__text_time = 1,
  svn_wc__prop_time
};


/* Is FILENAME's timestamp the same as the one recorded in our
   `entries' file?  Return the answer in EQUAL_P.  TIMESTAMP_NAME
   should be one of */
static svn_error_t *
timestamps_equal_p (svn_boolean_t *equal_p,
                    svn_string_t *filename,
                    const enum svn_wc__timestamp_kind timestamp_kind,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_time_t wfile_time, entrytime;
  svn_string_t *dirpath, *entryname;
  apr_hash_t *entries = NULL;
  struct svn_wc_entry_t *entry;

  svn_path_split (filename, &dirpath, &entryname, svn_path_local_style, pool);

  /* Get the timestamp from the entries file */
  err = svn_wc__entries_read (&entries, dirpath, pool);
  if (err)
    return err;
  entry = apr_hash_get (entries, entryname->data, entryname->len);

  /* Get the timestamp from the working file and the entry */
  if (timestamp_kind == svn_wc__text_time)
    {
      err = svn_wc__file_affected_time (&wfile_time, filename, pool);
      if (err) return err;

      entrytime = entry->text_time;
    }
  
  else if (timestamp_kind == svn_wc__prop_time)
    {
      svn_string_t *prop_path = svn_wc__adm_path (dirpath,
                                                  0, /* not tmp */
                                                  pool,
                                                  SVN_WC__ADM_PROPS,
                                                  filename,
                                                  NULL);
      err = svn_wc__file_affected_time (&wfile_time, prop_path, pool);
      if (err) return err;      

      entrytime = entry->prop_time;
    }

  if (entry == NULL || (! entrytime))
    {
      /* TODO: If either timestamp is inaccessible, the test cannot
         return an answer.  Assume that the timestamps are
         different. */
      *equal_p = FALSE;
      return SVN_NO_ERROR;
    }

  {
    /* Put the disk timestamp through a string conversion, so it's
       at the same resolution as entry timestamps. */
    svn_string_t *tstr = svn_wc__time_to_string (wfile_time, pool);
    wfile_time = svn_wc__string_to_time (tstr);
  }
  
  if (wfile_time == entrytime)
    *equal_p = TRUE;
  else
    *equal_p = FALSE;

  return SVN_NO_ERROR;
}




/* Set *DIFFERENT_P to non-zero if FILENAME1 and FILENAME2 have
   different sizes, else set to zero.  If the size of one or both of
   the files cannot be determined, then the sizes are not "definitely"
   different, so *DIFFERENT_P will be set to 0. */
static svn_error_t *
filesizes_definitely_different_p (svn_boolean_t *different_p,
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
    {
      /* If we got an error stat'ing a file, it could be because the
         file was removed... or who knows.  Whatever the case, we
         don't know if the filesizes are definitely different, so
         assume that they're not. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }

  status = apr_stat (&finfo2, filename2->data, pool);
  if (status)
    {
      /* See previous comment. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }


  /* Examine file sizes */
  if (finfo1.size == finfo2.size)
    *different_p = FALSE;
  else
    *different_p = TRUE;

  return SVN_NO_ERROR;
}


/* Do a byte-for-byte comparison of FILE1 and FILE2. */
static svn_error_t *
contents_identical_p (svn_boolean_t *identical_p,
                      svn_string_t *file1,
                      svn_string_t *file2,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_size_t bytes_read1, bytes_read2;
  char buf1[BUFSIZ], buf2[BUFSIZ];
  apr_file_t *file1_h = NULL;
  apr_file_t *file2_h = NULL;

  status = apr_open (&file1_h, file1->data, APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_open failed on `%s'", file1->data);

  status = apr_open (&file2_h, file2->data, APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_open failed on `%s'", file2->data);

  *identical_p = TRUE;  /* assume TRUE, until disproved below */
  while (!APR_STATUS_IS_EOF(status))
    {
      status = apr_full_read (file1_h, buf1, BUFSIZ, &bytes_read1);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_full_read() failed on %s.", file1->data);

      status = apr_full_read (file2_h, buf2, BUFSIZ, &bytes_read2);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_full_read() failed on %s.", file2->data);
      
      if ((bytes_read1 != bytes_read2)
          || (memcmp (buf1, buf2, bytes_read1)))
        {
          *identical_p = FALSE;
          break;
        }
    }

  status = apr_close (file1_h);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                             "contents_identical_p: apr_close failed on %s.",
                              file1->data);

  status = apr_close (file2_h);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                             "contents_identical_p: apr_close failed on %s.",
                             file2->data);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__files_contents_same_p (svn_boolean_t *same,
                               svn_string_t *file1,
                               svn_string_t *file2,
                               apr_pool_t *pool)
{
  svn_error_t *err;
  svn_boolean_t q;

  err = filesizes_definitely_different_p (&q, file1, file2, pool);
  if (err)
    return err;

  if (q)
    {
      *same = 0;
      return SVN_NO_ERROR;
    }
  
  err = contents_identical_p (&q, file1, file2, pool);
  if (err)
    return err;

  if (q)
    *same = 1;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_text_modified_p (svn_boolean_t *modified_p,
                        svn_string_t *filename,
                        apr_pool_t *pool)
{
  svn_boolean_t identical_p;
  svn_error_t *err;
  svn_string_t *textbase_filename;
  svn_boolean_t different_filesizes, equal_timestamps;

  /* Sanity check:  if the path doesn't exist, return FALSE. */
  enum svn_node_kind kind;
  err = svn_io_check_path (filename, &kind, pool);
  if (err) return err;
  if (kind != svn_node_file)
    {
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }              

  /* Get the full path of the textbase revision of filename */
  textbase_filename = svn_wc__text_base_path (filename, 0, pool);

  /* Simple case:  if there's no text-base revision of the file, all we
     can do is look at timestamps.  */
  if (! textbase_filename)
    {
      err = timestamps_equal_p (&equal_timestamps, filename,
                                svn_wc__text_time, pool);
      if (err) return err;

      if (equal_timestamps)
        *modified_p = FALSE;
      else
        *modified_p = TRUE;

      return SVN_NO_ERROR;
    }
  
  /* Better case:  we have a text-base revision of the file, so there
     are at least three tests we can try in succession. */
  else
    {     
      /* Easy-answer attempt #1:  */
      
      /* Check if the the local and textbase file have *definitely*
         different filesizes. */
      err = filesizes_definitely_different_p (&different_filesizes,
                                              filename, textbase_filename,
                                              pool);
      if (err) return err;
      
      if (different_filesizes) 
        {
          *modified_p = TRUE;
          return SVN_NO_ERROR;
        }
      
      /* Easy-answer attempt #2:  */
      
      /* See if the local file's timestamp is the same as the one recorded
         in the administrative directory.  */
      err = timestamps_equal_p (&equal_timestamps, filename,
                                svn_wc__text_time, pool);
      if (err) return err;
      
      if (equal_timestamps)
        {
          *modified_p = FALSE;
          return SVN_NO_ERROR;
        }
      
      /* Last ditch attempt:  */

      /* If we get here, then we know that the filesizes are the same,
         but the timestamps are different.  That's still not enough
         evidence to make a correct decision.  So we just give up and
         get the answer the hard way -- a brute force, byte-for-byte
         comparison. */
      err = contents_identical_p (&identical_p,
                                  filename,
                                  textbase_filename,
                                  pool);
      if (err)
        return err;
      
      if (identical_p)
        *modified_p = FALSE;
      else
        *modified_p = TRUE;
      
      return SVN_NO_ERROR;
    }
}




svn_error_t *
svn_wc_props_modified_p (svn_boolean_t *modified_p,
                         svn_string_t *path,
                         apr_pool_t *pool)
{
  svn_boolean_t identical_p;
  enum svn_node_kind kind;
  svn_error_t *err;
  svn_string_t *prop_path;
  svn_string_t *prop_base_path;
  svn_string_t *working_path, *basename;
  svn_boolean_t different_filesizes, equal_timestamps;

  /* First, construct the prop_path from the original path */
  svn_path_split (path, &working_path, &basename,
                  svn_path_local_style, pool);
  
  prop_path = svn_wc__adm_path (working_path,
                                0, /* not tmp */
                                pool,
                                SVN_WC__ADM_PROPS,
                                basename,
                                NULL);

  /* Sanity check:  if the prop_path doesn't exist, return FALSE. */
  err = svn_io_check_path (prop_path, &kind, pool);
  if (err) return err;
  if (kind != svn_node_file)
    {
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }              

  /* Get the full path of the prop-base `pristine' file */
  prop_base_path = svn_wc__adm_path (working_path,
                                     0, /* not tmp */
                                     pool,
                                     SVN_WC__ADM_PROP_BASE,
                                     basename,
                                     NULL);

  
  /* There are at least three tests we can try in succession. */
  
  /* Easy-answer attempt #1:  */
  
  /* Check if the the local and prop-base file have *definitely*
     different filesizes. */
  err = filesizes_definitely_different_p (&different_filesizes,
                                          prop_path, prop_base_path,
                                          pool);
  if (err) return err;
  
  if (different_filesizes) 
    {
      *modified_p = TRUE;
      return SVN_NO_ERROR;
    }
  
  /* Easy-answer attempt #2:  */
      
  /* See if the local file's timestamp is the same as the one recorded
     in the administrative directory.  */
  err = timestamps_equal_p (&equal_timestamps, prop_path,
                            svn_wc__prop_time, pool);
  if (err) return err;
  
  if (equal_timestamps)
    {
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }
  
  /* Last ditch attempt:  */
  
  /* If we get here, then we know that the filesizes are the same,
     but the timestamps are different.  That's still not enough
     evidence to make a correct decision.  So we just give up and
     get the answer the hard way -- a brute force, byte-for-byte
     comparison. */
  err = contents_identical_p (&identical_p,
                              prop_path,
                              prop_base_path,
                              pool);
  if (err)
    return err;
  
  if (identical_p)
    *modified_p = FALSE;
  else
    *modified_p = TRUE;
  
  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
