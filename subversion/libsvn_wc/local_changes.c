/*
 * local_changes.c:  preserving local mods across updates.
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
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_wc.h"
#include "wc.h"




/*** Timestamp generation and comparision. ***/

svn_error_t *
svn_wc__file_affected_time (apr_time_t *apr_time,
                            svn_string_t *path,
                            apr_pool_t *pool)
{
  apr_file_t *f = NULL;
  apr_finfo_t finfo;
  apr_status_t apr_err;

  apr_err = apr_open (&f, path->data, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err)
    goto error;

  apr_err = apr_getfileinfo (&finfo, f);
  if (apr_err)
    goto error;

  if (finfo.mtime > finfo.ctime)
    *apr_time = finfo.mtime;
  else
    *apr_time = finfo.ctime;

  apr_err = apr_close (f);
  if (apr_err)
    goto error;

  return SVN_NO_ERROR;

 error:
  return svn_error_createf (apr_err, 0, NULL, pool,
                            "svn_wc__file_affected_time: %s", path->data);
}




/*** Storing the diff between calls. ***/

struct svn_wc__diff_holder
{
  svn_string_t *patchfile;  /* Where to find the result of diff -c */
};







svn_error_t *
svn_wc__gnudiff_differ (void **result,
                        svn_string_t *src,
                        svn_string_t *target,
                        apr_pool_t *pool)
{
  struct svn_wc__diff_holder *dh = apr_pcalloc (pool, sizeof (*dh));

  /* kff todo: someday, do "diff -c SVN/text-base/foo ./foo" and store
     the result in a file, store the filename in dh->patchfile, and
     store dh in *RESULT. */
  
  *result = dh;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__gnudiff_patcher (void *user_data,
                         svn_string_t *src,
                         svn_string_t *target,
                         apr_pool_t *pool)
{
#if 0
  struct svn_wc__diff_holder *dh = (struct svn_wc__diff_holder *) user_data;
#endif
  apr_status_t apr_err;

  /* kff todo: someday, take CHANGES, which are the result of "diff -c
     SVN/text-base/foo ./foo", and re-apply them to the 
     file.  If any hunks fail, that's a conflict, do what CVS does. */

  /* kff todo: "Patch?  We don't need no stinkin' patch."  Just
     overwrite local mods for now, like the barbarians we are. */

  apr_err = apr_copy_file (src->data, target->data, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "copying %s to %s", src->data, target->data);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_local_changes (svn_wc_diff_fn_t *diff_fn,
                           void **result,
                           svn_string_t *path,
                           apr_pool_t *pool)
{
  return (*diff_fn) (result,
                     path,
                     svn_wc__text_base_path (path, 0, pool),
                     pool);
}


svn_error_t *
svn_wc__merge_local_changes (svn_wc_patch_fn_t *patch_fn,
                             void *diff,
                             svn_string_t *path,
                             apr_pool_t *pool)
{
  /* kff todo: the real recipe here is something like:

        1. apply the diff to ./SVN/tmp/text-base/newfile...
        2. ... and store the result in ./newfile
        
     That's right -- we don't want to update SVN/text-base/newfile
     until after the merge, because once the true text-base is
     updated, the ability to merge is lost, as we don't have the old
     ancestor locally anymore.

     But for now, we just copy the tmp text-base over to the real
     file.
  */
  svn_boolean_t exists;
  svn_error_t *err;
  svn_string_t *tmp_text_base = svn_wc__text_base_path (path, 1, pool);
  err = svn_wc__file_exists_p (&exists, tmp_text_base, pool);
  if (err)
    return err;

  if (! exists)
    return SVN_NO_ERROR;   /* tolerate mop-up calls gracefully */
  else
    return (*patch_fn) (diff, tmp_text_base, path, pool);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
