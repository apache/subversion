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
#include "entries.h"
#include "translate.h"




svn_error_t *
svn_wc_merge (const char *left,
              const char *right,
              const char *merge_target,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              apr_pool_t *pool)
{
  const char *tmp_target, *result_target, *tmp_left, *tmp_right;
  const char *pt, *bn, *bn_left, *bn_right, *mt_pt, *mt_bn;
  apr_file_t *tmp_f, *result_f;
  svn_boolean_t is_binary, toggled;
  svn_wc_keywords_t *keywords;
  enum svn_wc__eol_style eol_style;
  const char *eol;
  apr_status_t apr_err;
  int exit_code;
  svn_wc_entry_t *entry;

  svn_path_split_nts (merge_target, &mt_pt, &mt_bn, pool);

  /* Sanity check:  the merge target must be under revision control. */
  SVN_ERR (svn_wc_entry (&entry, merge_target, FALSE, pool));
  if (! entry)
    return svn_error_createf
      (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
       "svn_wc_merge: `%s' not under revision control", merge_target);

  /* Decide if the merge target is a text or binary file. */
  SVN_ERR (svn_wc_has_binary_prop (&is_binary, merge_target, pool));
  
  if (! is_binary)              /* this is a text file */
    {
      /* Make sure a temporary copy of 'target' is available with keywords
         contracted and line endings in repository-normal (LF) form.
         This is the file that diff3 will read as the 'mine' file.  */
      SVN_ERR (svn_wc_translated_file (&tmp_target, merge_target, pool));
      if (tmp_target == merge_target)  /* contraction didn't happen */
        {
          /* The target is already in repository form, so we just need to
             make a verbatim copy of it. */
          SVN_ERR (svn_io_open_unique_file (&tmp_f, &tmp_target,
                                            merge_target,
                                            SVN_WC__TMP_EXT,
                                            FALSE, pool));
          apr_err = apr_file_close (tmp_f);
          if (apr_err)
            return svn_error_createf
              (apr_err, 0, NULL, pool,
               "svn_wc_merge: unable to close tmp file `%s'",
               tmp_target);
      
          SVN_ERR (svn_io_copy_file (merge_target,
                                     tmp_target, TRUE, pool));
        }

      /* Open a second temporary file for writing; this is where diff3
         will write the merged results. */
      SVN_ERR (svn_io_open_unique_file (&result_f, &result_target,
                                        merge_target, SVN_WC__TMP_EXT,
                                        FALSE, pool));

      /* LEFT and RIGHT might be in totally different directories than
         MERGE_TARGET, and our diff3 command wants them all to be in
         the same directory.  So make temporary copies of LEFT and
         RIGHT right next to the target. */
      SVN_ERR (svn_io_open_unique_file (&tmp_f, &tmp_left,
                                        merge_target,
                                        SVN_WC__TMP_EXT,
                                        FALSE, pool));
      apr_err = apr_file_close (tmp_f);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'",
           tmp_left);

      SVN_ERR (svn_io_open_unique_file (&tmp_f, &tmp_right,
                                        merge_target,
                                        SVN_WC__TMP_EXT,
                                        FALSE, pool));
      apr_err = apr_file_close (tmp_f);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", tmp_right);
    
      SVN_ERR (svn_io_copy_file (left, tmp_left, TRUE, pool));
      SVN_ERR (svn_io_copy_file (right, tmp_right, TRUE, pool));

      svn_path_split_nts (tmp_left, &pt, &bn_left, pool);
      svn_path_split_nts (tmp_right, &pt, &bn_right, pool);
      svn_path_split_nts (tmp_target, &pt, &bn, pool);
      
      /* sanity check */
      if (mt_pt[0] == '\0')
        mt_pt = ".";

      /* Do the Deed, using all four scratch files. */
      SVN_ERR (svn_io_run_diff3 (mt_pt,
                                 bn, bn_left, bn_right,
                                 target_label, left_label, right_label,
                                 result_f,
                                 &exit_code,
                                 pool));
  
      /* Close the output file */
      apr_err = apr_file_close (result_f);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", result_target);

      if (exit_code == 1)  /* got a conflict */
        {
          /* Preserve the three pre-merge files, and modify the
             entry (mark as conflicted, track the preserved files). */ 
          apr_file_t *lcopy_f, *rcopy_f, *tcopy_f;
          const char *left_copy, *right_copy, *target_copy;
          const char *parentt, *left_base, *right_base, *target_base;
      
          /* I miss Lisp. */

          SVN_ERR (svn_io_open_unique_file (&lcopy_f,
                                            &left_copy,
                                            merge_target,
                                            left_label,
                                            FALSE,
                                            pool));

          apr_err = apr_file_close (lcopy_f);
          if (apr_err)
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
          if (apr_err)
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
          if (apr_err)
            return svn_error_createf
              (apr_err, 0, NULL, pool,
               "svn_wc_merge: unable to close tmp file `%s'", target_copy);

          /* We preserve all the files with keywords expanded and line
             endings in local (working) form. */

          /* NOTE: Callers must ensure that the svn:eol-style and
             svn:keywords property values are correct in the currently
             installed props.  With 'svn merge', it's no big deal.  But
             when 'svn up' calls this routine, it needs to make sure that
             this routine is using the newest property values that may
             have been received *during* the update.  Since this routine
             will be run from within a log-command, svn_wc_install_file
             needs to make sure that a previous log-command to 'install
             latest props' has already executed first.  Ben and I just
             checked, and that is indeed the order in which the log items
             are written, so everything should be fine.  Really.  */

          /* Create LEFT and RIGHT backup files, in expanded form.
             We use merge_target's current properties to do the translation. */
          SVN_ERR (svn_wc__get_keywords (&keywords, merge_target,
                                         NULL, pool));
          SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol,
                                          merge_target,
                                          pool));
          SVN_ERR (svn_wc_copy_and_translate (left, 
                                              left_copy,
                                              eol, FALSE, keywords,
                                              TRUE, pool));
          SVN_ERR (svn_wc_copy_and_translate (right,
                                              right_copy,
                                              eol, FALSE, keywords,
                                              TRUE, pool));

          /* Back up MERGE_TARGET verbatim (it's already in expanded form.) */
          SVN_ERR (svn_io_copy_file (merge_target,
                                     target_copy, TRUE, pool));

          /* Derive the basenames of the the 3 backup files. */
          svn_path_split_nts (left_copy, &parentt, &left_base, pool);
          svn_path_split_nts (right_copy, &parentt, &right_base, pool);
          svn_path_split_nts (target_copy, &parentt, &target_base, pool);
          entry->conflict_old = left_base;
          entry->conflict_new = right_base;
          entry->conflict_wrk = target_base;

          /* Mark merge_target's entry as "Conflicted", and start tracking
             the backup files in the entry as well. */
          SVN_ERR (svn_wc__entry_modify 
                   (parentt, mt_bn, entry,
                    SVN_WC__ENTRY_MODIFY_CONFLICT_OLD
                    | SVN_WC__ENTRY_MODIFY_CONFLICT_NEW
                    | SVN_WC__ENTRY_MODIFY_CONFLICT_WRK,
                    pool));

        } /* end of conflict handling */

      /* Unconditionally replace MERGE_TARGET with the new merged file,
         expanding. */
      SVN_ERR (svn_wc__get_keywords (&keywords, merge_target,
                                     NULL, pool));
      SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol, merge_target,
                                      pool));
      SVN_ERR (svn_wc_copy_and_translate (result_target,
                                          merge_target,
                                          eol, FALSE, keywords, TRUE, pool));

      /* Don't forget to clean up tmp_target, result_target, tmp_left,
         tmp_right.  There are a lot of scratch files lying around. */
      apr_err = apr_file_remove (tmp_target, pool);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to delete tmp file `%s'", tmp_target);
      apr_err = apr_file_remove (result_target, pool);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to delete tmp file `%s'", result_target);
      apr_err = apr_file_remove (tmp_left, pool);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to delete tmp file `%s'", tmp_left);
      apr_err = apr_file_remove (tmp_right, pool);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to delete tmp file `%s'", tmp_right);

    } /* end of merging for text files */

  else  /* merging procedure for binary files */
    {
      /* ### when making the binary-file backups, should we be honoring
         keywords and eol stuff?   */

      apr_file_t *lcopy_f, *rcopy_f;
      const char *left_copy, *right_copy;
      const char *parentt, *left_base, *right_base;
      
      /* reserve names for backups of left and right fulltexts */
      SVN_ERR (svn_io_open_unique_file (&lcopy_f,
                                        &left_copy,
                                        merge_target,
                                        left_label,
                                        FALSE,
                                        pool));
      apr_err = apr_file_close (lcopy_f);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", left_copy);

      SVN_ERR (svn_io_open_unique_file (&rcopy_f,
                                        &right_copy,
                                        merge_target,
                                        right_label,
                                        FALSE,
                                        pool));
      apr_err = apr_file_close (rcopy_f);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", right_copy);

      /* create the backup files */
      SVN_ERR (svn_io_copy_file (left,
                                 left_copy, TRUE, pool));
      SVN_ERR (svn_io_copy_file (right,
                                 right_copy, TRUE, pool));
      
      /* Derive the basenames of the the backup files. */
      svn_path_split_nts (left_copy, &parentt, &left_base, pool);
      svn_path_split_nts (right_copy, &parentt, &right_base, pool);
      entry->conflict_old = left_base;
      entry->conflict_new = right_base;
      entry->conflict_wrk = NULL;

      /* Mark merge_target's entry as "Conflicted", and start tracking
         the backup files in the entry as well. */
      SVN_ERR (svn_wc__entry_modify 
               (parentt, mt_bn, entry,
                SVN_WC__ENTRY_MODIFY_CONFLICT_OLD
                | SVN_WC__ENTRY_MODIFY_CONFLICT_NEW
                | SVN_WC__ENTRY_MODIFY_CONFLICT_WRK,
                pool));

      exit_code = 1;  /* a conflict happened */

    } /* end of binary conflict handling */

  /* Merging is complete.  Regardless of text or binariness, we might
     need to tweak the executable bit on the new working file.  */
  SVN_ERR (svn_wc__maybe_toggle_working_executable_bit (&toggled,
                                                        merge_target, pool));

  /* The docstring promises we'll return a CONFLICT error if
     appropriate;  presumably callers will specifically look for this. */
  if (exit_code == 1)
    return svn_error_createf
      (SVN_ERR_WC_CONFLICT, 0, NULL, pool,
       "svn_wc_merge: `%s' had conflicts during merge", merge_target);

  else
    return SVN_NO_ERROR;
}



/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
