/*
 * questions.c:  routines for asking questions about working copies
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <string.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_wc.h"

#include "wc.h"
#include "adm_files.h"
#include "questions.h"



/* kff todo: make this compare repository too?  Or do so in parallel code. */
svn_error_t *
svn_wc_check_wc (const char *path,
                 svn_boolean_t *is_wc,
                 apr_pool_t *pool)
{
  /* Nothing fancy, just check for an administrative subdir and a
     `README' file. */ 

  apr_file_t *f = NULL;
  svn_error_t *err = NULL;
  enum svn_node_kind kind;

  err = svn_io_check_path (path, &kind, pool);
  if (err)
    return err;
  
  if (kind == svn_node_none)
    {
      return svn_error_createf
        (APR_ENOENT, 0, NULL, pool,
         "svn_wc_check_wc: %s does not exist", path);
    }
  else if (kind != svn_node_dir)
    *is_wc = FALSE;
  else
    {
      err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_README,
                                   APR_READ, pool);
      
      /* It really doesn't matter what kind of error it is; if there
         was an error at all, then for our purposes this is not a
         working copy. */
      if (err)
        {
          svn_error_clear_all (err);
          *is_wc = FALSE;
        }
      else
        {
          *is_wc = TRUE;
          err = svn_wc__close_adm_file (f, path, SVN_WC__ADM_README, 0, pool);
          if (err)
            return err;
        }
    }

  return SVN_NO_ERROR;
}




/*** svn_wc_text_modified_p ***/

/* svn_wc_text_modified_p answers the question:

   "Are the contents of F different than the contents of
   .svn/text-base/F.svn-base?"

   or

   "Are the contents of .svn/props/xxx different than
   .svn/prop-base/xxx.svn-base?"

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

/* Is PATH's timestamp the same as the one recorded in our
   `entries' file?  Return the answer in EQUAL_P.  TIMESTAMP_KIND
   should be one of the enumerated type above. */
svn_error_t *
svn_wc__timestamps_equal_p (svn_boolean_t *equal_p,
                            const char *path,
                            const enum svn_wc__timestamp_kind timestamp_kind,
                            apr_pool_t *pool)
{
  apr_time_t wfile_time, entrytime = 0;
  const char *dirpath, *entryname;
  apr_hash_t *entries = NULL;
  struct svn_wc_entry_t *entry;
  enum svn_node_kind kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    {
      dirpath = path;
      entryname = SVN_WC_ENTRY_THIS_DIR;
    }
  else
    svn_path_split_nts (path, &dirpath, &entryname, pool);

  /* Get the timestamp from the entries file */
  SVN_ERR (svn_wc_entries_read (&entries, dirpath, FALSE, pool));
  entry = apr_hash_get (entries, entryname, APR_HASH_KEY_STRING);

  /* Can't compare timestamps for an unversioned file. */
  if (entry == NULL)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
       "timestamps_equal_p: `%s' not under revision control", entryname);

  /* Get the timestamp from the working file and the entry */
  if (timestamp_kind == svn_wc__text_time)
    {
      SVN_ERR (svn_io_file_affected_time (&wfile_time, path, pool));
      entrytime = entry->text_time;
    }
  
  else if (timestamp_kind == svn_wc__prop_time)
    {
      const char *prop_path;

      SVN_ERR (svn_wc__prop_path (&prop_path, path, 0, pool));
      SVN_ERR (svn_io_file_affected_time (&wfile_time, prop_path, pool));
      entrytime = entry->prop_time;
    }

  if (! entrytime)
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
    const char *tstr = svn_time_to_nts (wfile_time, pool);
    wfile_time = svn_time_from_nts (tstr);
  }
  
  if (wfile_time == entrytime)
    *equal_p = TRUE;
  else
    *equal_p = FALSE;

  return SVN_NO_ERROR;
}


/* Do a byte-for-byte comparison of FILE1 and FILE2. */
static svn_error_t *
contents_identical_p (svn_boolean_t *identical_p,
                      const char *file1,
                      const char *file2,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_size_t bytes_read1, bytes_read2;
  char buf1[BUFSIZ], buf2[BUFSIZ];
  apr_file_t *file1_h = NULL;
  apr_file_t *file2_h = NULL;

  status = apr_file_open (&file1_h, file1, 
                          APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_open failed on `%s'", file1);

  status = apr_file_open (&file2_h, file2, APR_READ, 
                          APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_open failed on `%s'", file2);

  *identical_p = TRUE;  /* assume TRUE, until disproved below */
  while (!APR_STATUS_IS_EOF(status))
    {
      status = apr_file_read_full (file1_h, buf1, sizeof(buf1), &bytes_read1);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_file_read_full() failed on %s.", 
           file1);

      status = apr_file_read_full (file2_h, buf2, sizeof(buf2), &bytes_read2);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_file_read_full() failed on %s.", 
           file2);
      
      if ((bytes_read1 != bytes_read2)
          || (memcmp (buf1, buf2, bytes_read1)))
        {
          *identical_p = FALSE;
          break;
        }
    }

  status = apr_file_close (file1_h);
  if (status)
    return svn_error_createf 
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_close failed on %s.", file1);

  status = apr_file_close (file2_h);
  if (status)
    return svn_error_createf 
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_close failed on %s.", file2);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__files_contents_same_p (svn_boolean_t *same,
                               const char *file1,
                               const char *file2,
                               apr_pool_t *pool)
{
  svn_error_t *err;
  svn_boolean_t q;

  err = svn_io_filesizes_different_p (&q, file1, file2, pool);
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
  else
    *same = 0;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__versioned_file_modcheck (svn_boolean_t *modified_p,
                                 const char *versioned_file,
                                 const char *base_file,
                                 apr_pool_t *pool)
{
  svn_boolean_t same;
  const char *tmp_vfile;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR (svn_wc_translated_file (&tmp_vfile, versioned_file, pool));
  
  err = svn_wc__files_contents_same_p (&same, tmp_vfile, base_file, pool);
  *modified_p = (! same);
  
  if (tmp_vfile != versioned_file)
    SVN_ERR (svn_io_remove_file (tmp_vfile, pool));
  
  return err;
}


svn_error_t *
svn_wc_text_modified_p (svn_boolean_t *modified_p,
                        const char *filename,
                        apr_pool_t *pool)
{
  const char *textbase_filename;
  svn_boolean_t equal_timestamps;
  apr_pool_t *subpool = svn_pool_create (pool);
  enum svn_node_kind kind;

  /* Sanity check:  if the path doesn't exist, return FALSE. */
  SVN_ERR (svn_io_check_path (filename, &kind, subpool));
  if (kind != svn_node_file)
    {
      *modified_p = FALSE;
      goto cleanup;
    }

  /* See if the local file's timestamp is the same as the one recorded
     in the administrative directory.  This could, theoretically, be
     wrong in certain rare cases, but with the addition of a forced
     delay after commits (see revision 419 and issue #542) it's highly
     unlikely to be a problem. */
  SVN_ERR (svn_wc__timestamps_equal_p (&equal_timestamps, filename,
                                       svn_wc__text_time, subpool));
  if (equal_timestamps)
    {
      *modified_p = FALSE;
      goto cleanup;
    }
      
  /* If there's no text-base file, we have to assume the working file
     is modified.  For example, a file scheduled for addition but not
     yet committed. */
  textbase_filename = svn_wc__text_base_path (filename, 0, subpool);
  SVN_ERR (svn_io_check_path (textbase_filename, &kind, subpool));
  if (kind != svn_node_file)
    {
      *modified_p = TRUE;
      goto cleanup;
    }
  
  /* Otherwise, fall back on the standard mod detector. */
  SVN_ERR (svn_wc__versioned_file_modcheck (modified_p,
                                            filename,
                                            textbase_filename,
                                            subpool));

 cleanup:
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc_conflicted_p (svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     const char *dir_path,
                     svn_wc_entry_t *entry,
                     apr_pool_t *pool)
{
  const char *path;
  enum svn_node_kind kind;
  apr_pool_t *subpool = svn_pool_create (pool);  /* ### Why? */

  *text_conflicted_p = FALSE;
  *prop_conflicted_p = FALSE;

  /* Look for any text conflict, exercising only as much effort as
     necessary to obtain a definitive answer.  This only applies to
     files, but we don't have to explicitly check that entry is a
     file, since these attributes would never be set on a directory
     anyway.  A conflict file entry notation only counts if the
     conflict file still exists on disk.  */
  if (entry->conflict_old)
    {
      path = svn_path_join (dir_path, entry->conflict_old, subpool);
      SVN_ERR (svn_io_check_path (path, &kind, subpool));
      if (kind == svn_node_file)
        *text_conflicted_p = TRUE;
    }

  if ((! *text_conflicted_p) && (entry->conflict_new))
    {
      path = svn_path_join (dir_path, entry->conflict_new, subpool);
      SVN_ERR (svn_io_check_path (path, &kind, subpool));
      if (kind == svn_node_file)
        *text_conflicted_p = TRUE;
    }

  if ((! *text_conflicted_p) && (entry->conflict_wrk))
    {
      path = svn_path_join (dir_path, entry->conflict_wrk, subpool);
      SVN_ERR (svn_io_check_path (path, &kind, subpool));
      if (kind == svn_node_file)
        *text_conflicted_p = TRUE;
    }

  /* What about prop conflicts? */
  if (entry->prejfile)
    {
      path = svn_path_join (dir_path, entry->prejfile, subpool);
      SVN_ERR (svn_io_check_path (path, &kind, subpool));
      if (kind == svn_node_file)
        *prop_conflicted_p = TRUE;
    }
  
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}





svn_error_t *
svn_wc_has_binary_prop (svn_boolean_t *has_binary_prop,
                        const char *path,
                        apr_pool_t *pool)
{
  const svn_string_t *value;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* The heuristic here is simple;  a file is of type `binary' iff it
     has the `svn:mime-type' property and its value does *not* start
     with `text/'. */

  SVN_ERR (svn_wc_prop_get (&value, SVN_PROP_MIME_TYPE, path, subpool));
 
  if (value
      && (value->len > 5) 
      && (strncmp (value->data, "text/", 5)))
    *has_binary_prop = TRUE;
  else
    *has_binary_prop = FALSE;
  
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}






/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
