/*
 * questions.c:  routines for asking questions about working copies
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
#include "svn_io.h"

#include "wc.h"
#include "adm_files.h"
#include "questions.h"
#include "entries.h"

#include "svn_md5.h"
#include <apr_md5.h>

#include "svn_private_config.h"


/* ### todo: make this compare repository too?  Or do so in parallel
   code.  See also adm_files.c:check_adm_exists(), which should
   probably be merged with this.  */
svn_error_t *
svn_wc_check_wc (const char *path,
                 int *wc_format,
                 apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_node_kind_t kind;

  SVN_ERR (svn_io_check_path (path, &kind, pool));
  
  if (kind == svn_node_none)
    {
      return svn_error_createf
        (APR_ENOENT, NULL, _("'%s' does not exist"), path);
    }
  else if (kind != svn_node_dir)
    *wc_format = 0;
  else  /* okay, it's a directory, but is it a working copy? */
    {
      const char *format_file_path
        = svn_wc__adm_path (path, FALSE, pool, SVN_WC__ADM_FORMAT, NULL);

      err = svn_io_read_version_file (wc_format, format_file_path, pool);

      if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
                  || APR_STATUS_IS_ENOTDIR(err->apr_err)))
        {
          /* If the format file does not exist, then for our purposes
             this is not a working copy, so return 0. */
          svn_error_clear (err);
          *wc_format = 0;
        }
      else if (err)
        return err;
      else
        {
          /* If we managed to read the format file we assume that we
             are dealing with a real wc so we can return a nice
             error. */
          SVN_ERR (svn_wc__check_format (*wc_format, path, pool));
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__check_format (int wc_format, const char *path, apr_pool_t *pool)
{
  if (wc_format < 2)
    {
      return svn_error_createf
        (SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
         _("Working copy format of '%s' is too old (%d); "
           "please check out your working copy again"),
         path, wc_format);
    }
  else if (wc_format > SVN_WC__VERSION)
    {
      return svn_error_createf
        (SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
         _("This client is too old to work with working copy '%s'; "
           "please get a newer Subversion client"),
         path);
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
                            svn_wc_adm_access_t *adm_access,
                            enum svn_wc__timestamp_kind timestamp_kind,
                            apr_pool_t *pool)
{
  apr_time_t wfile_time, entrytime = 0;
  const svn_wc_entry_t *entry;

  /* Get the timestamp from the entries file */
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, FALSE, pool));

  /* Can't compare timestamps for an unversioned file. */
  if (entry == NULL)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, NULL,
       _("'%s' is not under version control"), path);

  /* Get the timestamp from the working file and the entry */
  if (timestamp_kind == svn_wc__text_time)
    {
      SVN_ERR (svn_io_file_affected_time (&wfile_time, path, pool));
      entrytime = entry->text_time;
    }
  
  else if (timestamp_kind == svn_wc__prop_time)
    {
      const char *prop_path;

      SVN_ERR (svn_wc__prop_path (&prop_path, path, adm_access, FALSE, pool));
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
    /* This string conversion here may be goodness, but it does
       nothing currently _and_ it is somewhat expensive _and_ it eats
       memory _and_ it is tested for in the regression tests. But I
       will only comment it out because I do not possess the guts to
       remove it altogether. */
    /*
    const char *tstr = svn_time_to_cstring (wfile_time, pool);
    SVN_ERR (svn_time_from_cstring (&wfile_time, tstr, pool));
    */
  }
  
  if (wfile_time == entrytime)
    *equal_p = TRUE;
  else
    *equal_p = FALSE;

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc__versioned_file_modcheck (svn_boolean_t *modified_p,
                                 const char *versioned_file,
                                 svn_wc_adm_access_t *adm_access,
                                 const char *base_file,
                                 apr_pool_t *pool)
{
  svn_boolean_t same;
  const char *tmp_vfile;
  svn_error_t *err = SVN_NO_ERROR, *err2 = SVN_NO_ERROR;

  SVN_ERR (svn_wc_translated_file (&tmp_vfile, versioned_file, adm_access,
                                   TRUE, pool));
  
  err = svn_io_files_contents_same_p (&same, tmp_vfile, base_file, pool);
  *modified_p = (! same);
  
  if (tmp_vfile != versioned_file)
    err2 = svn_io_remove_file (tmp_vfile, pool);

  if (err)
    {
      if (err2)
        svn_error_compose (err, err2);
      return err;
    }

  return err2;
}


/* Set *MODIFIED_P to true if (after translation) VERSIONED_FILE
 * differs from BASE_FILE, else to false if not.  Also, verify that
 * BASE_FILE matches the entry checksum for VERSIONED_FILE; if it
 * does not match, return the error SVN_ERR_WC_CORRUPT_TEXT_BASE.
 *
 * ADM_ACCESS is an access baton for VERSIONED_FILE.  Use POOL for
 * temporary allocation.
 */
static svn_error_t *
compare_and_verify (svn_boolean_t *modified_p,
                    const char *versioned_file,
                    svn_wc_adm_access_t *adm_access,
                    const char *base_file,
                    apr_pool_t *pool)

{
  const char *tmp_vfile;
  svn_error_t *err = SVN_NO_ERROR, *err2 = SVN_NO_ERROR;
  const svn_wc_entry_t *entry;

  SVN_ERR (svn_wc_entry (&entry, versioned_file, adm_access, TRUE, pool));


  SVN_ERR (svn_wc_translated_file (&tmp_vfile, versioned_file, adm_access,
                                   TRUE, pool));

  /* Compare the files, while calculating the base file's checksum. */
  {
    /* "v_" means versioned_file, "b_" means base_file. */
    svn_error_t *v_err = SVN_NO_ERROR;
    svn_error_t *b_err = SVN_NO_ERROR;
    apr_size_t v_file_bytes_read, b_file_bytes_read;
    char v_buf[BUFSIZ], b_buf[BUFSIZ];
    apr_file_t *v_file_h = NULL;
    apr_file_t *b_file_h = NULL;
    apr_pool_t *loop_pool;

    int identical = TRUE;
    unsigned char digest[APR_MD5_DIGESTSIZE];
    apr_md5_ctx_t context;
    const char *checksum;

    SVN_ERR (svn_io_file_open (&v_file_h, tmp_vfile,
                               APR_READ, APR_OS_DEFAULT, pool));
    SVN_ERR (svn_io_file_open (&b_file_h, base_file, APR_READ, APR_OS_DEFAULT,
                               pool));
    apr_md5_init (&context);

    loop_pool = svn_pool_create (pool);
    do
      {
        svn_pool_clear (loop_pool);

        /* The only way v_err can be true here is if we hit EOF. */
        if (! v_err)
          {
            v_err = svn_io_file_read_full (v_file_h, v_buf, sizeof(v_buf),
                                           &v_file_bytes_read, loop_pool);
            if (v_err && !APR_STATUS_IS_EOF(v_err->apr_err))
              return v_err;
          }
        
        b_err = svn_io_file_read_full (b_file_h, b_buf, sizeof(b_buf),
                                       &b_file_bytes_read, loop_pool);
        if (b_err && !APR_STATUS_IS_EOF(b_err->apr_err))
          return b_err;
        
        apr_md5_update (&context, b_buf, b_file_bytes_read);

        if ((v_err && (! b_err))
            || (v_file_bytes_read != b_file_bytes_read)
            || (memcmp (v_buf, b_buf, v_file_bytes_read)))
          {
            identical = FALSE;
          }
      } while (! b_err);
    
    svn_pool_destroy (loop_pool);

    /* Clear any errors, but don't set the error variables to null, as
       we still depend on them for conditionals. */
    svn_error_clear (v_err);
    svn_error_clear (b_err);
    
    SVN_ERR (svn_io_file_close (v_file_h, pool));
    SVN_ERR (svn_io_file_close (b_file_h, pool));

    apr_md5_final (digest, &context);

    checksum = svn_md5_digest_to_cstring (digest, pool);
    if (entry->checksum && strcmp (checksum, entry->checksum) != 0)
      {
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
           _("Checksum mismatch indicates corrupt text base: '%s'\n"
             "   expected:  %s\n"
             "     actual:  %s\n"),
           base_file, entry->checksum, checksum);
      }

    *modified_p = ! identical;
  }
  
  if (tmp_vfile != versioned_file)
    err2 = svn_io_remove_file (tmp_vfile, pool);

  if (err)
    {
      if (err2)
        svn_error_compose (err, err2);
      return err;
    }

  return err2;
}


svn_error_t *
svn_wc_text_modified_p (svn_boolean_t *modified_p,
                        const char *filename,
                        svn_boolean_t force_comparison,
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  const char *textbase_filename;
  svn_boolean_t equal_timestamps;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_node_kind_t kind;

  /* Sanity check:  if the path doesn't exist, return FALSE. */
  SVN_ERR (svn_io_check_path (filename, &kind, subpool));
  if (kind != svn_node_file)
    {
      *modified_p = FALSE;
      goto cleanup;
    }

  if (! force_comparison)
    {
      /* See if the local file's timestamp is the same as the one
         recorded in the administrative directory.  This could,
         theoretically, be wrong in certain rare cases, but with the
         addition of a forced delay after commits (see revision 419
         and issue #542) it's highly unlikely to be a problem. */
      SVN_ERR (svn_wc__timestamps_equal_p (&equal_timestamps,
                                           filename, adm_access,
                                           svn_wc__text_time, subpool));
      if (equal_timestamps)
        {
          *modified_p = FALSE;
          goto cleanup;
        }
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
  
  
  if (force_comparison)  /* Check all bytes, and verify checksum. */
    {
      SVN_ERR (compare_and_verify (modified_p,
                                   filename,
                                   adm_access,
                                   textbase_filename,
                                   subpool));
    }
  else  /* Else, fall back on the standard mod detector. */
    {
      SVN_ERR (svn_wc__versioned_file_modcheck (modified_p,
                                                filename,
                                                adm_access,
                                                textbase_filename,
                                                subpool));
    }

  /* It is quite legitimate for modifications to the working copy to
     produce a timestamp variation with no text variation. If it turns out
     that there are no differences then we might be able to "repair" the
     text-time in the entries file and so avoid the expensive file contents
     comparison in the future. */
  if (! *modified_p && svn_wc_adm_locked (adm_access))
    {
      svn_wc_entry_t tmp;
      SVN_ERR (svn_io_file_affected_time (&tmp.text_time, filename, pool));
      SVN_ERR (svn_wc__entry_modify (adm_access,
                                     svn_path_basename (filename, pool),
                                     &tmp, SVN_WC__ENTRY_MODIFY_TEXT_TIME, TRUE,
                                     pool));
    }

 cleanup:
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc_conflicted_p (svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     const char *dir_path,
                     const svn_wc_entry_t *entry,
                     apr_pool_t *pool)
{
  const char *path;
  svn_node_kind_t kind;
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
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  const svn_string_t *value;
  apr_pool_t *subpool = svn_pool_create (pool);

  SVN_ERR (svn_wc_prop_get (&value, SVN_PROP_MIME_TYPE, path, adm_access,
                            subpool));
 
  if (value && (svn_mime_type_is_binary (value->data)))
    *has_binary_prop = TRUE;
  else
    *has_binary_prop = FALSE;
  
  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}
