/*
 * obliterate.c: removing nodes (or changes) from history
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#include "private/svn_client_private.h"
#include "private/svn_ra_private.h"
#include "svn_client.h"



svn_error_t *
svn_client__obliterate_path_rev(const char *url,
                                svn_revnum_t rev,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *path;

  /* Open a simple RA session for the URL (not connected to a WC). */
  SVN_ERR(svn_client_open_ra_session(&ra_session, url, ctx, pool));

  path = "";  /* relative to URL of session */

  SVN_ERR(svn_ra__obliterate_path_rev(ra_session, rev, path, pool));

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify_url(url, svn_wc_notify_delete, pool);
      /* ### Should be svn_wc_notify_obliterate not svn_wc_notify_delete. */
      notify->revision = rev;

      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  return SVN_NO_ERROR;
}
