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

  /* Operate on a tmp file, with keywords and line endings contracted.
     If any contraction happens, we get a tmp file automatically;
     otherwise, we have to make the tmp file by hand.  */
  SVN_ERR (svn_wc_translated_file (&tmp_target, target, pool));
  if (tmp_target == target)  /* no contraction happened */
    {
      svn_path_split (target, &parent_dir, &basename, pool);
      SVN_ERR (svn_io_open_unique_file (&tmp_f,
                                        &tmp_target,
                                        target,
                                        SVN_WC__TMP_EXT,
                                        FALSE,
                                        pool));
      apr_err = apr_file_close (tmp_f);
      if (! APR_STATUS_IS_SUCCESS (apr_err))
        return svn_error_createf
          (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
           "svn_wc_merge: unable to close tmp file `%s'", tmp_target);
    }

  /* run diff3 using all 6 arguments. */

  /* if diff3 returned 1,

         reserve 3 unique files.
         copy-and-translate left -> xxx.left_label
         copy-and-translate right -> xxx.right_label
         copy merge_target -> xxx.target_label

         modify-entry:  conflicted
         modify-entry:  track 3 backup files.
 
     else if diff3 returned 0,
      
         do nothing;

     else

         return error;

     cp-and-translate merged-result merge_target
     rm merged-result (and merge_target.tmp)
  */

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
