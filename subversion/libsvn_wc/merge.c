/*
 * merge.c:  merging changes into a working file
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



#include "svn_wc.h"
#include "wc.h"




svn_error_t *
svn_wc_merge (const char *left,
              const char *right,
              const char *merge_target,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              apr_pool_t *pool)
{
  svn_stringbuf_t *target = svn_stringbuf_create (merge_target, pool);
  svn_stringbuf_t *parent_dir, *basename;
  svn_stringbuf_t *tmp_target;
  apr_file_t *tmp_f;
  apr_status_t apr_err;
  int exit_code;

  abort ();  /* this is not ready yet, callers should blow up */

  /* The merge target must be under revision control. */
  {
    svn_wc_entry_t *ignored_ent;
    
    SVN_ERR (svn_wc_entry (&ignored_ent, target, pool));
    if (ignored_ent == NULL)
      return svn_error_createf
        (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
         "svn_wc_merge: `%s' not under revision control", merge_target);
  }

  /* Open a tmp file, with keywords and line endings contracted.  If
     any contraction happens, we get the tmp file automatically;
     otherwise, we have to make it by hand.  */
  SVN_ERR (svn_wc_translated_file (&tmp_target, target, pool));
  if (tmp_target != target)  /* contraction occurred */
    {
      apr_err = apr_file_open (&tmp_f, tmp_target->data,
                               APR_WRITE, APR_OS_DEFAULT, pool);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to open tmp file `%s'", tmp_target);
    }
  else
    {
      SVN_ERR (svn_io_open_unique_file (&tmp_f,
                                        &tmp_target,
                                        merge_target,
                                        SVN_WC__TMP_EXT,
                                        FALSE,
                                        pool));
    }

  svn_path_split (target, &parent_dir, &basename, pool);
  SVN_ERR (svn_io_run_diff3 (parent_dir->data,
                             tmp_target->data, left, right,
                             target_label, left_label, right_label,
                             tmp_f,
                             &exit_code,
                             pool));
  
  apr_err = apr_file_close (tmp_f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "svn_wc_merge: unable to close tmp file `%s'", tmp_target);

  if (exit_code == 1)  /* got a conflict */
    {
      /* Preserve the three files pre-merge files, and modify the
         entry (mark as conflicted, track the preserved files). */ 

      svn_stringbuf_t *left_copy, *right_copy, *target_copy;
      apr_file_t *lcopy_f, *rcopy_f, *tcopy_f;

      /* I miss Lisp. */

      SVN_ERR (svn_io_open_unique_file (&lcopy_f,
                                        &left_copy,
                                        merge_target,
                                        left_label,
                                        FALSE,
                                        pool));

      apr_err = apr_file_close (lcopy_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", left_copy);

      /* Have I mentioned how much I miss Lisp? */

      SVN_ERR (svn_io_open_unique_file (&rcopy_f,
                                        &right_copy,
                                        merge_target,
                                        right_label,
                                        FALSE,
                                        pool));

      apr_err = apr_file_close (rcopy_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", right_copy);

      /* Why, how much more pleasant to be forced to unroll my loops.
         If I'd been writing in Lisp, I might have mapped an inline
         lambda form over a list, or something equally disgusting.
         Thank goodness C was here to protect me! */

      SVN_ERR (svn_io_open_unique_file (&tcopy_f,
                                        &target_copy,
                                        merge_target,
                                        target_label,
                                        FALSE,
                                        pool));

      apr_err = apr_file_close (tcopy_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", target_copy);

      /* What about translation??  Ben said:
       *
       *   cp-and-translate merged-result merge_target
       *   rm merged-result (and merge_target.tmp)
       */

    }

  /* ### PROBLEM: Callers need to be careful about making sure the
     values of svn:eol-style and svn:keywords are correct in the
     currently installed props.  With 'svn merge', it's no big deal.
     But when 'svn up' calls this routine, it needs to make sure that
     this routine is using the newest property values that may have
     been received *during* the update.  Since this routine will be
     run from within a log-command, svn_wc_install_file needs to make
     sure that a previous log-command to 'install latest props' has
     already executed first.  */

  return SVN_NO_ERROR;
}



/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
