/* log.c --- retrieving log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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


#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"



svn_error_t *
svn_repos_get_logs (svn_fs_t *fs,
                    apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  svn_revnum_t this_rev;
  apr_pool_t *subpool = svn_pool_create (pool);

  if (start == SVN_INVALID_REVNUM)
    SVN_ERR (svn_fs_youngest_rev (&start, fs, pool));

  if (end == SVN_INVALID_REVNUM)
    SVN_ERR (svn_fs_youngest_rev (&end, fs, pool));

  /* ### todo: ignore `fs_path' and `paths' for
     now.  Probably want to convert them to a single hash containing
     absolute paths first.  */

  for (this_rev = start;
       ((start >= end) ? (this_rev >= end) : (this_rev <= end));
       ((start >= end) ? this_rev-- : this_rev++))
    {
      svn_stringbuf_t *author, *date, *message;
      svn_string_t date_prop = {SVN_PROP_REVISION_DATE,
                                strlen(SVN_PROP_REVISION_DATE)};
      svn_string_t author_prop = {SVN_PROP_REVISION_AUTHOR,
                                  strlen(SVN_PROP_REVISION_AUTHOR)};
      svn_string_t message_prop = {SVN_PROP_REVISION_LOG,
                                   strlen(SVN_PROP_REVISION_LOG)};

      SVN_ERR (svn_fs_revision_prop
               (&author, fs, this_rev, &author_prop, subpool));
      SVN_ERR (svn_fs_revision_prop
               (&date, fs, this_rev, &date_prop, subpool));
      SVN_ERR (svn_fs_revision_prop
               (&message, fs, this_rev, &message_prop, subpool));

      SVN_ERR ((*receiver) (receiver_baton,
                            NULL,
                            this_rev,
                            author ? author->data : "",
                            date ? date->data : "",
                            message ? message->data : "",
                            (this_rev == end)));
      
      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
