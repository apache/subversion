/*
 * mergeinfo.h : Client library-internal merge info APIs.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#ifndef SVN_LIBSVN_CLIENT_MERGEINFO_H
#define SVN_LIBSVN_CLIENT_MERGEINFO_H

/* Elide any svn:mergeinfo set on TARGET_PATH to its nearest working
   copy ancestor with equivalent mergeinfo.  If ELISION_LIMIT_PATH is NULL
   check up to the root of the working copy for elidable mergeinfo,
   otherwise check as far as ELISION_LIMIT_PATH.  TARGET_PATH and
   ELISION_LIMIT_PATH, if it exists, must both be absolute or relative to
   the working directory. */
svn_error_t *
svn_client__elide_mergeinfo(const char *target_wcpath,
                            const char *elision_limit_path,
                            const svn_wc_entry_t *entry,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

#endif /* SVN_LIBSVN_CLIENT_MERGEINFO_H */
