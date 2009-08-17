/*
 * node.c:  routines for getting information about nodes in the working copy.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

/* A note about these functions:

   We aren't really sure yet which bits of data libsvn_client needs about
   nodes.  In wc-1, we just grab the entry, and then use whatever we want
   from it.  Such a pattern is Bad.

   This file is intended to hold functions which retrieve specific bits of
   information about a node, and will hopefully give us a better idea about
   what data libsvn_client needs, and how to best provide that data in 1.7
   final.  As such, these functions should only be called from outside
   libsvn_wc; any internal callers are encouraged to use the appropriate
   information fetching function, such as svn_wc__db_read_info().
*/

#include <apr_pools.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_types.h"

#include "wc.h"
#include "lock.h"
#include "props.h"
#include "log.h"
#include "entries.h"
#include "wc_db.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_debug.h"


svn_error_t *
svn_wc__node_get_children(const apr_array_header_t **children,
                          svn_wc_context_t *wc_ctx,
                          const char *dir_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const apr_array_header_t *rel_children;
  apr_array_header_t *childs;
  int i;

  SVN_ERR(svn_wc__db_read_children(&rel_children, wc_ctx->db, dir_abspath,
                                   scratch_pool, scratch_pool));

  childs = apr_array_make(result_pool, rel_children->nelts,
                             sizeof(const char *));
  for (i = 0; i < rel_children->nelts; i++)
    {
      APR_ARRAY_PUSH(childs, const char *) =
                svn_dirent_join(dir_abspath, APR_ARRAY_IDX(rel_children, i,
                                                           const char *),
                      result_pool);
    }

  *children = childs;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__node_get_repos_root(const char **repos_root_url,
                            svn_wc_context_t *wc_ctx,
                            const char * local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  
  err = svn_wc__db_read_info(NULL, NULL, NULL, NULL,
                             repos_root_url,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             wc_ctx->db, local_abspath, result_pool,
                             scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
      *repos_root_url = NULL;
    }

  return err;
}

