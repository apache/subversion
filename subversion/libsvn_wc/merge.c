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
svn_wc_merge (const char *parent,
              const char *left,
              const char *right,
              const char *merge_target,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              apr_pool_t *pool)
{
  svn_stringbuf_t *full_tgt_path, *full_left_path, *full_right_path;
  svn_stringbuf_t *tmp_target, *result_target;
  svn_stringbuf_t *pt, *bn;
  apr_file_t *tmp_f, *result_f;
  svn_wc_keywords_t *keywords;
  enum svn_wc__eol_style eol_style;
  const char *eol;
  apr_status_t apr_err;
  int exit_code;

  /* We need temporary fullpaths of our three basenames, so that we
     pass them as arguments to svn_io_copy_* and
     svn_io_open_unique_file... */
  full_tgt_path = svn_stringbuf_create (parent, pool);
  svn_path_add_component_nts (full_tgt_path, merge_target);
  full_left_path = svn_stringbuf_create (parent, pool);
  svn_path_add_component_nts (full_left_path, left);
  full_right_path = svn_stringbuf_create (parent, pool);
  svn_path_add_component_nts (full_right_path, right);

  /* Sanity check:  the merge target must be under revision control. */
  {
    svn_wc_entry_t *ignored_ent;
    
    SVN_ERR (svn_wc_entry (&ignored_ent, full_tgt_path, pool));
    if (ignored_ent == NULL)
      return svn_error_createf
        (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
         "svn_wc_merge: `%s' not under revision control", full_tgt_path->data);
  }

  /* Make sure a temporary copy of 'target' is available with keywords
     contracted and line endings in repository-normal (LF) form.
     This is the file that diff3 will read as the 'mine' file.  */
  SVN_ERR (svn_wc_translated_file (&tmp_target, full_tgt_path, pool));
  if (tmp_target == full_tgt_path)  /* contraction didn't happen */
    {
      /* The target is already in repository form, so we just need to
         make a verbatim copy of it. */
      SVN_ERR (svn_io_open_unique_file (&tmp_f, &tmp_target,
                                        full_tgt_path->data, SVN_WC__TMP_EXT,
                                        FALSE, pool));
      apr_err = apr_file_close (tmp_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", tmp_target->data);
      
      SVN_ERR (svn_io_copy_file (full_tgt_path->data,
                                 tmp_target->data, TRUE, pool));
    }

  /* Open a second temporary file for writing; this is where diff3
     will write the merged results. */
  SVN_ERR (svn_io_open_unique_file (&result_f, &result_target,
                                    full_tgt_path->data, SVN_WC__TMP_EXT,
                                    FALSE, pool));

  /* Run diff3 on the basenames {left, right, basename(tmp_target)}. */
  svn_path_split (tmp_target, &pt, &bn, pool);
  SVN_ERR (svn_io_run_diff3 (parent,
                             bn->data, left, right,
                             target_label, left_label, right_label,
                             result_f,
                             &exit_code,
                             pool));
  
  apr_err = apr_file_close (result_f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "svn_wc_merge: unable to close tmp file `%s'", result_target->data);

  if (exit_code == 1)  /* got a conflict */
    {
      /* Preserve the three pre-merge files, and modify the
         entry (mark as conflicted, track the preserved files). */ 
      apr_file_t *lcopy_f, *rcopy_f, *tcopy_f;
      svn_stringbuf_t *left_copy, *right_copy, *target_copy;
      svn_stringbuf_t *parentt, *left_base, *right_base, *target_base;
      apr_hash_t *atthash = apr_hash_make (pool);
      
      /* I miss Lisp. */

      SVN_ERR (svn_io_open_unique_file (&lcopy_f,
                                        &left_copy,
                                        full_tgt_path->data,
                                        left_label,
                                        FALSE,
                                        pool));

      apr_err = apr_file_close (lcopy_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", left_copy->data);

      /* Have I mentioned how much I miss Lisp? */

      SVN_ERR (svn_io_open_unique_file (&rcopy_f,
                                        &right_copy,
                                        full_tgt_path->data,
                                        right_label,
                                        FALSE,
                                        pool));

      apr_err = apr_file_close (rcopy_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", right_copy->data);

      /* Why, how much more pleasant to be forced to unroll my loops.
         If I'd been writing in Lisp, I might have mapped an inline
         lambda form over a list, or something equally disgusting.
         Thank goodness C was here to protect me! */

      SVN_ERR (svn_io_open_unique_file (&tcopy_f,
                                        &target_copy,
                                        full_tgt_path->data,
                                        target_label,
                                        FALSE,
                                        pool));

      apr_err = apr_file_close (tcopy_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", target_copy->data);

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
      SVN_ERR (svn_wc__get_keywords (&keywords, full_tgt_path->data,
                                     NULL, pool));
      SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol, full_tgt_path->data,
                                      pool));
      SVN_ERR (svn_wc_copy_and_translate (full_left_path->data, 
                                          left_copy->data,
                                          eol, FALSE, keywords, TRUE, pool));
      SVN_ERR (svn_wc_copy_and_translate (full_right_path->data,
                                          right_copy->data,
                                          eol, FALSE, keywords, TRUE, pool));

      /* Back up MERGE_TARGET verbatim (it's already in expanded form.) */
      SVN_ERR (svn_io_copy_file (full_tgt_path->data,
                                 target_copy->data, TRUE, pool));

      /* Derive the basenames of the the 3 backup files. */
      svn_path_split (left_copy, &parentt, &left_base, pool);
      svn_path_split (right_copy, &parentt, &right_base, pool);
      svn_path_split (target_copy, &parentt, &target_base, pool);
      apr_hash_set (atthash, SVN_WC_ENTRY_ATTR_CONFLICT_OLD,
                    APR_HASH_KEY_STRING, left_base);
      apr_hash_set (atthash, SVN_WC_ENTRY_ATTR_CONFLICT_NEW,
                    APR_HASH_KEY_STRING, right_base);
      apr_hash_set (atthash, SVN_WC_ENTRY_ATTR_CONFLICT_WRK,
                    APR_HASH_KEY_STRING, target_base);

      /* Mark merge_target's entry as "Conflicted", and start tracking
         the 3 backup files in the entry as well. */
      SVN_ERR (svn_wc__entry_modify (parentt, 
                                     svn_stringbuf_create (merge_target, pool),
                                     (SVN_WC__ENTRY_MODIFY_CONFLICTED
                                      | SVN_WC__ENTRY_MODIFY_ATTRIBUTES),
                                     -1, svn_node_none, svn_wc_schedule_normal,
                                     TRUE, /* Conflicted */
                                     FALSE, 0, 0, NULL,
                                     atthash, /* has the 3 backup files */
                                     pool, NULL));
    }

  /* Unconditionally replace MERGE_TARGET with the new merged file,
     expanding. */
  SVN_ERR (svn_wc__get_keywords (&keywords, full_tgt_path->data, NULL, pool));
  SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol, full_tgt_path->data,
                                  pool));
  SVN_ERR (svn_wc_copy_and_translate (result_target->data, full_tgt_path->data,
                                      eol, FALSE, keywords, TRUE, pool));

  /* Don't forget to clean up tmp_target and result_target. */
  apr_err = apr_file_remove (tmp_target->data, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "svn_wc_merge: unable to delete tmp file `%s'",
       tmp_target->data);
  apr_err = apr_file_remove (result_target->data, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "svn_wc_merge: unable to delete tmp file `%s'",
       result_target->data);

  /* The docstring promises we'll return a CONFLICT error if
     appropriate;  presumably callers will specifically look for this. */
  if (exit_code == 1)
    return svn_error_createf
      (SVN_ERR_WC_CONFLICT, 0, NULL, pool,
       "svn_wc_merge: `%s' had conflicts during merge", full_tgt_path->data);

  else
    return SVN_NO_ERROR;
}



/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
