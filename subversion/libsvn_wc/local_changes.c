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




struct svn_wc__diff_holder
{
  apr_pool_t *pool;
};


svn_error_t *
svn_wc__generic_differ (void *user_data,
                        void **result,
                        svn_string_t *src,
                        svn_string_t *target)
{
  struct svn_wc__diff_holder *holder;
  apr_pool_t *pool = (apr_pool_t *) user_data;

  /* kff todo: someday, do "diff -c SVN/text-base/foo ./foo" and store
     the result in *RESULT. */
  
  holder = apr_pcalloc (pool, sizeof (*holder));
  holder->pool = pool;
  *result = holder;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__generic_patcher (void *user_data,
                         svn_string_t *src,
                         svn_string_t *target)
{
  struct svn_wc__diff_holder *holder 
    = (struct svn_wc__diff_holder *) user_data;
  apr_pool_t *pool = holder->pool;
  apr_status_t apr_err;

  /* kff todo: someday, take CHANGES, which are the result of "diff -c
     SVN/text-base/foo ./foo", and re-apply them to the 
     file.  If any hunks fail, that's a conflict, do what CVS does. */

  /* kff todo: "Patch?  We don't need no stinkin' patch."  Just
     overwrite local mods for now. */

  apr_err = apr_copy_file (src->data, target->data, pool);
  if (apr_err)
    {
      /* kff todo: write svn_io_copy_file ? */
      char *msg = apr_psprintf (pool, "copying %s to %s",
                                src->data, target->data);
      return svn_error_create (apr_err, 0, NULL, pool, msg);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_local_changes (svn_wc_diff_fn_t *diff_fn,
                           void **result,
                           svn_string_t *path,
                           apr_pool_t *pool)
{
  return (*diff_fn) (pool, result, path, svn_wc__text_base_path (path, pool));
}


svn_error_t *
svn_wc__merge_local_changes (svn_wc_patch_fn_t *patch_fn,
                             void *changes,
                             svn_string_t *path,
                             apr_pool_t *pool)
{
  /* kff todo: this will be reworked.  for now, just reverse source
     and dest to achieve desired effect. */
  return (*patch_fn) (changes, svn_wc__text_base_path (path, pool), path);
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
