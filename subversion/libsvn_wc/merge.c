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

  /* reserve a unique file for the merged result */

  /* possibly make a contracted/LF-ified copy of merge_target */

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
