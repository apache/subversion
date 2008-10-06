/*
 * questions.c:  routines for asking questions about working copies
 *
 * ====================================================================
 * Copyright (c) 2000-2004, 2006, 2008 CollabNet.  All rights reserved.
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
#include <apr_file_info.h>
#include <apr_time.h>
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_io.h"
#include "svn_props.h"

#include "wc.h"
#include "adm_files.h"
#include "questions.h"
#include "entries.h"
#include "props.h"
#include "translate.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/* ### todo: make this compare repository too?  Or do so in parallel
   code.  See also adm_files.c:check_adm_exists(), which should
   probably be merged with this.  */
svn_error_t *
svn_wc_check_wc(const char *path,
                int *wc_format,
                apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  const char *format_file_path
    = svn_wc__adm_path(path, FALSE, pool, SVN_WC__ADM_ENTRIES, NULL);

  /* First try to read the format number from the entries file. */
  err = svn_io_read_version_file(wc_format, format_file_path, pool);

  /* If that didn't work and the first line of the entries file contains
     something other than a number, then it is probably in XML format. */
  if (err && err->apr_err == SVN_ERR_BAD_VERSION_FILE_FORMAT)
    {
      svn_error_clear(err);
      /* Fall back on reading the format file instead.
         Note that the format file might not exist in newer working copies
         (format 7 and higher), but in that case, the entries file should
         have contained the format number. */
      format_file_path
        = svn_wc__adm_path(path, FALSE, pool, SVN_WC__ADM_FORMAT, NULL);

      err = svn_io_read_version_file(wc_format, format_file_path, pool);
    }

  if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
              || APR_STATUS_IS_ENOTDIR(err->apr_err)))
    {
      svn_node_kind_t kind;

      svn_error_clear(err);

      /* Check path itself exists. */
      SVN_ERR(svn_io_check_path(path, &kind, pool));

      if (kind == svn_node_none)
        {
          return svn_error_createf
            (APR_ENOENT, NULL, _("'%s' does not exist"),
            svn_path_local_style(path, pool));
        }

      /* If the format file does not exist or path not directory, then for
         our purposes this is not a working copy, so return 0. */
      *wc_format = 0;
    }
  else if (err)
    return err;
  else
    {
      /* If we managed to read the format file we assume that we
          are dealing with a real wc so we can return a nice
          error. */
      SVN_ERR(svn_wc__check_format(*wc_format, path, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__check_format(int wc_format, const char *path, apr_pool_t *pool)
{
  if (wc_format < 2)
    {
      return svn_error_createf
        (SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
         _("Working copy format of '%s' is too old (%d); "
           "please check out your working copy again"),
         svn_path_local_style(path, pool), wc_format);
    }
  else if (wc_format > SVN_WC__VERSION)
    {
      /* This won't do us much good for the 1.4<->1.5 crossgrade,
         since 1.4.x clients don't refer to this FAQ entry, but at
         least post-1.5 crossgrades will be somewhat less painful. */
      return svn_error_createf
        (SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
         _("This client is too old to work with working copy '%s'.  You need\n"
           "to get a newer Subversion client, or to downgrade this working "
           "copy.\n"
           "See "
           "http://subversion.tigris.org/faq.html#working-copy-format-change\n"
           "for details."
           ),
         svn_path_local_style(path, pool));
    }

  return SVN_NO_ERROR;
}



/*** svn_wc_text_modified_p ***/

/* svn_wc_text_modified_p answers the question:

   "Are the contents of F different than the contents of
   .svn/text-base/F.svn-base or .svn/tmp/text-base/F.svn-base?"

   In the first case, we're looking to see if a user has made local
   modifications to a file since the last update or commit.  In the
   second, the file may not be versioned yet (it doesn't exist in
   entries).  Support for the latter case came about to facilitate
   forced checkouts, updates, and switches, where an unversioned file
   may obstruct a file about to be added.

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
svn_wc__timestamps_equal_p(svn_boolean_t *equal_p,
                           const char *path,
                           svn_wc_adm_access_t *adm_access,
                           enum svn_wc__timestamp_kind timestamp_kind,
                           apr_pool_t *pool)
{
  apr_time_t wfile_time, entrytime = 0;
  const svn_wc_entry_t *entry;

  /* Get the timestamp from the entries file */
  SVN_ERR(svn_wc__entry_versioned(&entry, path, adm_access, FALSE, pool));

  /* Get the timestamp from the working file and the entry */
  if (timestamp_kind == svn_wc__text_time)
    {
      SVN_ERR(svn_io_file_affected_time(&wfile_time, path, pool));
      entrytime = entry->text_time;
    }

  else if (timestamp_kind == svn_wc__prop_time)
    {
      SVN_ERR(svn_wc__props_last_modified(&wfile_time,
                                          path, svn_wc__props_working,
                                          adm_access, pool));
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


/* Set *MODIFIED_P to TRUE if (after translation) VERSIONED_FILE
 * differs from BASE_FILE, else to FALSE if not.  Also verify that
 * BASE_FILE matches the entry checksum for VERSIONED_FILE, if
 * verify_checksum is TRUE. If checksum does not match, return the error
 * SVN_ERR_WC_CORRUPT_TEXT_BASE.
 *
 * ADM_ACCESS is an access baton for VERSIONED_FILE.  Use POOL for
 * temporary allocation.
 */
static svn_error_t *
compare_and_verify(svn_boolean_t *modified_p,
                   const char *versioned_file,
                   svn_wc_adm_access_t *adm_access,
                   const char *base_file,
                   svn_boolean_t compare_textbases,
                   svn_boolean_t verify_checksum,
                   apr_pool_t *pool)
{
  svn_boolean_t same;
  svn_subst_eol_style_t eol_style;
  const char *eol_str;
  apr_hash_t *keywords;
  svn_boolean_t special;
  svn_boolean_t need_translation;


  SVN_ERR(svn_wc__get_eol_style(&eol_style, &eol_str, versioned_file,
                                adm_access, pool));
  SVN_ERR(svn_wc__get_keywords(&keywords, versioned_file,
                              adm_access, NULL, pool));
  SVN_ERR(svn_wc__get_special(&special, versioned_file, adm_access, pool));


  need_translation = svn_subst_translation_required(eol_style, eol_str,
                                                    keywords, special, TRUE);
  /* Special files can only be compared through their text bases:
     they have no working copy representation
     for example: symlinks aren't guaranteed to be valid, nor does
                  it make sense to compare with the linked file-or-directory. */
  compare_textbases |= special;
  if (verify_checksum || need_translation)
    {
      /* Reading files is necessary. */
      svn_checksum_t *checksum;
      /* "v_" means versioned_file, "b_" means base_file. */
      apr_file_t *v_file_h, *b_file_h;
      svn_stream_t *v_stream, *b_stream;
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_io_file_open(&b_file_h, base_file, APR_READ,
                               APR_OS_DEFAULT, pool));

      b_stream = svn_stream_from_aprfile2(b_file_h, FALSE, pool);

      if (verify_checksum)
        {
          /* Need checksum verification, so read checksum from entries file
           * and setup checksummed stream for base file. */
          SVN_ERR(svn_wc__entry_versioned(&entry, versioned_file, adm_access,
                                         TRUE, pool));

          if (entry->checksum)
            b_stream = svn_stream_checksummed2(b_stream, &checksum,
                                               svn_checksum_md5, NULL,
                                               svn_checksum_md5, TRUE, pool);
        }

      if (compare_textbases && need_translation)
        {
          /* Create stream for detranslate versioned file to normal form. */
          SVN_ERR(svn_subst_stream_detranslated(&v_stream,
                                                versioned_file,
                                                eol_style,
                                                eol_str, TRUE,
                                                keywords, special,
                                                pool));
        }
      else
        {
          SVN_ERR(svn_io_file_open(&v_file_h, versioned_file, APR_READ,
                              APR_OS_DEFAULT, pool));
          v_stream = svn_stream_from_aprfile2(v_file_h, FALSE, pool);

          if (need_translation)
            {
              /* Translate text-base to working copy form. */
              b_stream = svn_subst_stream_translated(b_stream, eol_str,
                                                     FALSE, keywords, TRUE,
                                                     pool);
            }
        }

      SVN_ERR(svn_stream_contents_same(&same, b_stream, v_stream, pool));

      SVN_ERR(svn_stream_close(v_stream));
      SVN_ERR(svn_stream_close(b_stream));

      if (verify_checksum && entry->checksum)
        {
          const char *digest;
          digest = svn_checksum_to_cstring_display(checksum, pool);
          if (strcmp(digest, entry->checksum) != 0)
            {
              return svn_error_createf
                (SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
                  _("Checksum mismatch indicates corrupt text base: '%s'\n"
                    "   expected:  %s\n"
                    "     actual:  %s\n"),
                  svn_path_local_style(base_file, pool),
                  entry->checksum,
                  digest);
            }
        }
    }
  else
    {
      /* Translation would be a no-op, so compare the original file. */
      SVN_ERR(svn_io_files_contents_same_p(&same, base_file, versioned_file,
                                           pool));
    }



  *modified_p = (! same);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__versioned_file_modcheck(svn_boolean_t *modified_p,
                                const char *versioned_file,
                                svn_wc_adm_access_t *adm_access,
                                const char *base_file,
                                svn_boolean_t compare_textbases,
                                apr_pool_t *pool)
{
  return compare_and_verify(modified_p, versioned_file, adm_access,
                            base_file, compare_textbases, FALSE, pool);
}

svn_error_t *
svn_wc__text_modified_internal_p(svn_boolean_t *modified_p,
                                 const char *filename,
                                 svn_boolean_t force_comparison,
                                 svn_wc_adm_access_t *adm_access,
                                 svn_boolean_t compare_textbases,
                                 apr_pool_t *pool)
{
  const char *textbase_filename;
  svn_node_kind_t kind;
  svn_error_t *err;
  apr_finfo_t finfo;


  /* No matter which way you look at it, the file needs to exist. */
  err = svn_io_stat(&finfo, filename,
                    APR_FINFO_SIZE | APR_FINFO_MTIME | APR_FINFO_TYPE
                    | APR_FINFO_LINK, pool);
  if ((err && APR_STATUS_IS_ENOENT(err->apr_err))
      || (!err && !(finfo.filetype == APR_REG ||
                    finfo.filetype == APR_LNK)))
    {
      /* There is no entity, or, the entity is not a regular file or link.
         So, it can't be modified. */
      svn_error_clear(err);
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  if (! force_comparison)
    {
      const svn_wc_entry_t *entry;

      /* We're allowed to use a heuristic to determine whether files may
         have changed.  The heuristic has these steps:


         1. Compare the working file's size
            with the size cached in the entries file
         2. If they differ, do a full file compare
         3. Compare the working file's timestamp
            with the timestamp cached in the entries file
         4. If they differ, do a full file compare
         5. Otherwise, return indicating an unchanged file.

         There are 2 problematic situations which may occur:

         1. The cached working size is missing
         --> In this case, we forget we ever tried to compare
             and skip to the timestamp comparison.  This is
             because old working copies do not contain cached sizes

         2. The cached timestamp is missing
         --> In this case, we forget we ever tried to compare
             and skip to full file comparison.  This is because
             the timestamp will be removed when the library
             updates a locally changed file.  (ie, this only happens
             when the file was locally modified.)

      */


      /* Get the entry */
      err = svn_wc_entry(&entry, filename, adm_access, FALSE, pool);
      if (err)
        {
          svn_error_clear(err);
          goto compare_them;
        }

      if (! entry)
        goto compare_them;

      /* Compare the sizes, if applicable */
      if (entry->working_size != SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN
          && finfo.size != entry->working_size)
        goto compare_them;


      /* Compare the timestamps

         Note: text_time == 0 means absent from entries,
               which also means the timestamps won't be equal,
               so there's no need to explicitly check the 'absent' value. */
      if (entry->text_time != finfo.mtime)
        goto compare_them;


      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }

 compare_them:
 /* If there's no text-base file, we have to assume the working file
     is modified.  For example, a file scheduled for addition but not
     yet committed. */
  /* We used to stat for the working base here, but we just give
     compare_and_verify a try; we'll check for errors afterwards */
  textbase_filename = svn_wc__text_base_path(filename, FALSE, pool);

  /* Check all bytes, and verify checksum if requested. */
  {
    apr_pool_t *subpool = svn_pool_create(pool);

    err = compare_and_verify(modified_p,
                             filename,
                             adm_access,
                             textbase_filename,
                             compare_textbases,
                             force_comparison,
                             subpool);
    if (err)
      {
        svn_error_t *err2;

        err2 = svn_io_check_path(textbase_filename, &kind, pool);
        if (! err2 && kind != svn_node_file)
          {
            svn_error_clear(err);
            *modified_p = TRUE;
            return SVN_NO_ERROR;
          }

        svn_error_clear(err);
        return err2;
      }

    svn_pool_destroy(subpool);
  }

  /* It is quite legitimate for modifications to the working copy to
     produce a timestamp variation with no text variation. If it turns out
     that there are no differences then we might be able to "repair" the
     text-time in the entries file and so avoid the expensive file contents
     comparison in the future.
     Though less likely, the same may be true for the size
     of the working file. */
  if (! *modified_p && svn_wc_adm_locked(adm_access))
    {
      svn_wc_entry_t tmp;

      tmp.working_size = finfo.size;
      tmp.text_time = finfo.mtime;
      SVN_ERR(svn_wc__entry_modify(adm_access,
                                   svn_path_basename(filename, pool),
                                   &tmp,
                                   SVN_WC__ENTRY_MODIFY_TEXT_TIME
                                   | SVN_WC__ENTRY_MODIFY_WORKING_SIZE,
                                   TRUE, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_text_modified_p(svn_boolean_t *modified_p,
                        const char *filename,
                        svn_boolean_t force_comparison,
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  return svn_wc__text_modified_internal_p(modified_p, filename,
                                          force_comparison, adm_access,
                                          TRUE, pool);
}



svn_error_t *
svn_wc_conflicted_p2(svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     svn_boolean_t *has_tree_conflicted_children,
                     const char *dir_path,
                     const svn_wc_entry_t *entry,
                     apr_pool_t *pool)
{
  const char *path;
  svn_node_kind_t kind;
  apr_pool_t *subpool = svn_pool_create(pool);  /* ### Why? */

  *text_conflicted_p = FALSE;
  *prop_conflicted_p = FALSE;
  *has_tree_conflicted_children = FALSE;

  /* Look for any text conflict, exercising only as much effort as
     necessary to obtain a definitive answer.  This only applies to
     files, but we don't have to explicitly check that entry is a
     file, since these attributes would never be set on a directory
     anyway.  A conflict file entry notation only counts if the
     conflict file still exists on disk.  */
  if (entry->conflict_old)
    {
      path = svn_path_join(dir_path, entry->conflict_old, subpool);
      SVN_ERR(svn_io_check_path(path, &kind, subpool));
      if (kind == svn_node_file)
        *text_conflicted_p = TRUE;
    }

  if ((! *text_conflicted_p) && (entry->conflict_new))
    {
      path = svn_path_join(dir_path, entry->conflict_new, subpool);
      SVN_ERR(svn_io_check_path(path, &kind, subpool));
      if (kind == svn_node_file)
        *text_conflicted_p = TRUE;
    }

  if ((! *text_conflicted_p) && (entry->conflict_wrk))
    {
      path = svn_path_join(dir_path, entry->conflict_wrk, subpool);
      SVN_ERR(svn_io_check_path(path, &kind, subpool));
      if (kind == svn_node_file)
        *text_conflicted_p = TRUE;
    }

  /* What about prop conflicts? */
  if (entry->prejfile)
    {
      path = svn_path_join(dir_path, entry->prejfile, subpool);
      SVN_ERR(svn_io_check_path(path, &kind, subpool));
      if (kind == svn_node_file)
        *prop_conflicted_p = TRUE;
    }

  /* Check for tree conflicts (only "this-dir" entries have tree conflicts). */
  if ((strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
      && entry->tree_conflict_data)
    {
      *has_tree_conflicted_children = TRUE;
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_conflicted_p(svn_boolean_t *text_conflicted_p,
                    svn_boolean_t *prop_conflicted_p,
                    const char *dir_path,
                    const svn_wc_entry_t *entry,
                    apr_pool_t *pool)
{
  svn_boolean_t has_tree_conflicted_children;
  return svn_wc_conflicted_p2(text_conflicted_p, prop_conflicted_p,
                              &has_tree_conflicted_children, dir_path, entry,
                              pool);
}



svn_error_t *
svn_wc_has_binary_prop(svn_boolean_t *has_binary_prop,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       apr_pool_t *pool)
{
  const svn_string_t *value;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_wc_prop_get(&value, SVN_PROP_MIME_TYPE, path, adm_access,
                          subpool));

  if (value && (svn_mime_type_is_binary(value->data)))
    *has_binary_prop = TRUE;
  else
    *has_binary_prop = FALSE;

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}
