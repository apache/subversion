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



/* kff todo: make this compare repository too?  Or do so in parallel code. */
svn_error_t *
svn_wc_check_wc (const svn_stringbuf_t *path,
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
         "svn_wc_check_wc: %s does not exist", path->data);
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

enum svn_wc__timestamp_kind
{
  svn_wc__text_time = 1,
  svn_wc__prop_time
};


/* Is PATH's timestamp the same as the one recorded in our
   `entries' file?  Return the answer in EQUAL_P.  TIMESTAMP_KIND
   should be one of the enumerated type above. */
static svn_error_t *
timestamps_equal_p (svn_boolean_t *equal_p,
                    svn_stringbuf_t *path,
                    const enum svn_wc__timestamp_kind timestamp_kind,
                    apr_pool_t *pool)
{
  apr_time_t wfile_time, entrytime = 0;
  svn_stringbuf_t *dirpath, *entryname;
  apr_hash_t *entries = NULL;
  struct svn_wc_entry_t *entry;
  enum svn_node_kind kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    {
      dirpath = path;
      entryname = svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, pool);
    }
  else
    svn_path_split (path, &dirpath, &entryname, pool);

  /* Get the timestamp from the entries file */
  SVN_ERR (svn_wc_entries_read (&entries, dirpath, pool));
  entry = apr_hash_get (entries, entryname->data, entryname->len);

  /* Can't compare timestamps for an unversioned file. */
  if (entry == NULL)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
       "timestamps_equal_p: `%s' not under revision control", entryname->data);

  /* Get the timestamp from the working file and the entry */
  if (timestamp_kind == svn_wc__text_time)
    {
      SVN_ERR (svn_io_file_affected_time (&wfile_time, path, pool));
      entrytime = entry->text_time;
    }
  
  else if (timestamp_kind == svn_wc__prop_time)
    {
      svn_stringbuf_t *prop_path;

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




/* Set *DIFFERENT_P to non-zero if FILENAME1 and FILENAME2 have
   different sizes, else set to zero.  If the size of one or both of
   the files cannot be determined, then the sizes are not "definitely"
   different, so *DIFFERENT_P will be set to 0. */
static svn_error_t *
filesizes_definitely_different_p (svn_boolean_t *different_p,
                                  svn_stringbuf_t *filename1,
                                  svn_stringbuf_t *filename2,
                                  apr_pool_t *pool)
{
  apr_finfo_t finfo1;
  apr_finfo_t finfo2;
  apr_status_t status;

  /* Stat both files */
  status = apr_stat (&finfo1, filename1->data, APR_FINFO_MIN, pool);
  if (status)
    {
      /* If we got an error stat'ing a file, it could be because the
         file was removed... or who knows.  Whatever the case, we
         don't know if the filesizes are definitely different, so
         assume that they're not. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }

  status = apr_stat (&finfo2, filename2->data, APR_FINFO_MIN, pool);
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
                      svn_stringbuf_t *file1,
                      svn_stringbuf_t *file2,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_size_t bytes_read1, bytes_read2;
  char buf1[BUFSIZ], buf2[BUFSIZ];
  apr_file_t *file1_h = NULL;
  apr_file_t *file2_h = NULL;

  status = apr_file_open (&file1_h, file1->data, 
                          APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_open failed on `%s'", file1->data);

  status = apr_file_open (&file2_h, file2->data, APR_READ, 
                          APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_open failed on `%s'", file2->data);

  *identical_p = TRUE;  /* assume TRUE, until disproved below */
  while (!APR_STATUS_IS_EOF(status))
    {
      status = apr_file_read_full (file1_h, buf1, sizeof(buf1), &bytes_read1);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_file_read_full() failed on %s.", 
           file1->data);

      status = apr_file_read_full (file2_h, buf2, sizeof(buf2), &bytes_read2);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_file_read_full() failed on %s.", 
           file2->data);
      
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
       "contents_identical_p: apr_file_close failed on %s.", file1->data);

  status = apr_file_close (file2_h);
  if (status)
    return svn_error_createf 
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_close failed on %s.", file2->data);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__files_contents_same_p (svn_boolean_t *same,
                               svn_stringbuf_t *file1,
                               svn_stringbuf_t *file2,
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
  else
    *same = 0;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__versioned_file_modcheck (svn_boolean_t *modified_p,
                                 svn_stringbuf_t *versioned_file,
                                 svn_stringbuf_t *base_file,
                                 apr_pool_t *pool)
{
  svn_boolean_t same;
  svn_stringbuf_t *tmp_vfile;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR (svn_wc_translated_file (&tmp_vfile, versioned_file, pool));
  
  err = svn_wc__files_contents_same_p (&same, tmp_vfile, base_file, pool);
  *modified_p = (! same);
  
  if (tmp_vfile != versioned_file)
    SVN_ERR (svn_io_remove_file (tmp_vfile->data, pool));
  
  return err;
}


svn_error_t *
svn_wc_text_modified_p (svn_boolean_t *modified_p,
                        svn_stringbuf_t *filename,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *textbase_filename;
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
  SVN_ERR (timestamps_equal_p (&equal_timestamps, filename,
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



/* Helper to optimize svn_wc_props_modified_p().

   If PATH_TO_PROP_FILE is nonexistent, or is of size 4 bytes ("END"),
   then set EMPTY_P to true.   Otherwise set EMPTY_P to false, which
   means that the file must contain real properties.  */
static svn_error_t *
empty_props_p (svn_boolean_t *empty_p,
               svn_stringbuf_t *path_to_prop_file,
               apr_pool_t *pool)
{
  enum svn_node_kind kind;

  SVN_ERR (svn_io_check_path (path_to_prop_file, &kind, pool));

  if (kind == svn_node_none)
    *empty_p = TRUE;

  else 
    {
      apr_finfo_t finfo;
      apr_status_t status;

      status = apr_stat (&finfo, path_to_prop_file->data, APR_FINFO_MIN, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "couldn't stat '%s'...",
                                  path_to_prop_file->data);

      /* If we remove props from a propfile, eventually the file will
         contain nothing but "END\n" */
      if (finfo.size == 4)  
        *empty_p = TRUE;

      else
        *empty_p = FALSE;

      /* ### really, if the size is < 4, then something is corrupt.
         If the size is between 4 and 16, then something is corrupt,
         because 16 is the -smallest- the file can possibly be if it
         contained only one property.  someday we should check for
         this. */

    }

  return SVN_NO_ERROR;
}


/* Simple wrapper around previous helper func, and inversed. */
svn_error_t *
svn_wc__has_props (svn_boolean_t *has_props,
                   svn_stringbuf_t *path,
                   apr_pool_t *pool)
{
  svn_boolean_t is_empty;
  svn_stringbuf_t *prop_path;

  SVN_ERR (svn_wc__prop_path (&prop_path, path, 0, pool));
  SVN_ERR (empty_props_p (&is_empty, prop_path, pool));

  if (is_empty)
    *has_props = FALSE;
  else
    *has_props = TRUE;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc_props_modified_p (svn_boolean_t *modified_p,
                         svn_stringbuf_t *path,
                         apr_pool_t *pool)
{
  svn_boolean_t bempty, wempty;
  svn_stringbuf_t *prop_path;
  svn_stringbuf_t *prop_base_path;
  svn_boolean_t different_filesizes, equal_timestamps;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* First, get the paths of the working and 'base' prop files. */
  SVN_ERR (svn_wc__prop_path (&prop_path, path, 0, subpool));
  SVN_ERR (svn_wc__prop_base_path (&prop_base_path, path, 0, subpool));

  /* Decide if either path is "empty" of properties. */
  SVN_ERR (empty_props_p (&wempty, prop_path, subpool));
  SVN_ERR (empty_props_p (&bempty, prop_base_path, subpool));

  /* Easy out:  if the base file is empty, we know the answer
     immediately. */
  if (bempty)
    {
      if (! wempty)
        {
          /* base is empty, but working is not */
          *modified_p = TRUE;
          goto cleanup;
        }
      else
        {
          /* base and working are both empty */
          *modified_p = FALSE;
          goto cleanup;
        }
    }

  /* OK, so the base file is non-empty.  One more easy out: */
  if (wempty)
    {
      /* base exists, working is empty */
      *modified_p = TRUE;
      goto cleanup;
    }

  /* At this point, we know both files exists.  Therefore we have no
     choice but to start checking their contents. */
  
  /* There are at least three tests we can try in succession. */
  
  /* Easy-answer attempt #1:  */
  
  /* Check if the the local and prop-base file have *definitely*
     different filesizes. */
  SVN_ERR (filesizes_definitely_different_p (&different_filesizes,
                                             prop_path, prop_base_path,
                                             subpool));
  if (different_filesizes) 
    {
      *modified_p = TRUE;
      goto cleanup;
    }
  
  /* Easy-answer attempt #2:  */
      
  /* See if the local file's prop timestamp is the same as the one
     recorded in the administrative directory.  */
  SVN_ERR (timestamps_equal_p (&equal_timestamps, path,
                               svn_wc__prop_time, subpool));
  if (equal_timestamps)
    {
      *modified_p = FALSE;
      goto cleanup;
    }
  
  /* Last ditch attempt:  */
  
  /* If we get here, then we know that the filesizes are the same,
     but the timestamps are different.  That's still not enough
     evidence to make a correct decision;  we need to look at the
     files' contents directly.

     However, doing a byte-for-byte comparison won't work.  The two
     properties files may have the *exact* same name/value pairs, but
     arranged in a different order.  (Our hashdump format makes no
     guarantees about ordering.)

     Therefore, rather than use contents_identical_p(), we use
     svn_wc__get_local_propchanges(). */
  {
    apr_array_header_t *local_propchanges;
    apr_hash_t *localprops = apr_hash_make (subpool);
    apr_hash_t *baseprops = apr_hash_make (subpool);

    SVN_ERR (svn_wc__load_prop_file (prop_path->data, localprops, subpool));
    SVN_ERR (svn_wc__load_prop_file (prop_base_path->data,
                                     baseprops,
                                     subpool));
    SVN_ERR (svn_wc__get_local_propchanges (&local_propchanges,
                                            localprops,
                                            baseprops,
                                            subpool));
                                         
    if (local_propchanges->nelts > 0)
      *modified_p = TRUE;
    else
      *modified_p = FALSE;
  }
 
 cleanup:
  svn_pool_destroy (subpool);
  
  return SVN_NO_ERROR;
}






svn_error_t *
svn_wc_conflicted_p (svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     svn_stringbuf_t *dir_path,
                     svn_wc_entry_t *entry,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *rej_file, *prej_file;
  svn_stringbuf_t *rej_path, *prej_path;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Note:  it's assumed that ENTRY is a particular entry inside
     DIR_PATH's entries file. */
  
  if (entry->conflicted)
    {
      /* Get up to two reject files */
      rej_file = apr_hash_get (entry->attributes,
                               SVN_WC_ENTRY_ATTR_REJFILE,
                               APR_HASH_KEY_STRING);

      prej_file = apr_hash_get (entry->attributes,
                                SVN_WC_ENTRY_ATTR_PREJFILE,
                                APR_HASH_KEY_STRING);
      
      if ((! rej_file) && (! prej_file))
        {
          /* freaky, why is the entry marked as conflicted, but there
             are no reject files?  assume there's no more conflict.
             but maybe this should be an error someday.  :) */
          *text_conflicted_p = FALSE;
          *prop_conflicted_p = FALSE;
        }

      else
        {
          enum svn_node_kind kind;

          if (rej_file)
            {
              rej_path = svn_stringbuf_dup (dir_path, subpool);
              svn_path_add_component (rej_path, rej_file);

              SVN_ERR (svn_io_check_path (rej_path, &kind, subpool));
              if (kind == svn_node_file)
                /* The textual conflict file is still there. */
                *text_conflicted_p = TRUE;
              else
                /* The textual conflict file has been removed. */
                *text_conflicted_p = FALSE;  
            }
          else
            /* There's no mention of a .rej file at all */
            *text_conflicted_p = FALSE;

          if (prej_file)
            {
              prej_path = svn_stringbuf_dup (dir_path, subpool);
              svn_path_add_component (prej_path, prej_file);

              SVN_ERR (svn_io_check_path (prej_path, &kind, subpool));

              if (kind == svn_node_file)
                /* The property conflict file is still there. */
                *prop_conflicted_p = TRUE;
              else
                /* The property conflict file has been removed. */
                *prop_conflicted_p = FALSE;
            }
          else
            /* There's no mention of a .prej file at all. */
            *prop_conflicted_p = FALSE;
        }
    }
  else
    {
      /* The entry isn't marked with `conflict="true"' in the first
         place.  */
      *text_conflicted_p = FALSE;
      *prop_conflicted_p = FALSE;
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}





svn_error_t *
svn_wc_has_binary_prop (svn_boolean_t *has_binary_prop,
                        const svn_stringbuf_t *path,
                        apr_pool_t *pool)
{
  const svn_string_t *value;
  apr_pool_t *subpool = svn_pool_create (pool);

  /* The heuristic here is simple;  a file is of type `binary' iff it
     has the `svn:mime-type' property and its value does *not* start
     with `text/'. */

  SVN_ERR (svn_wc_prop_get (&value, SVN_PROP_MIME_TYPE, path->data, subpool));
 
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
