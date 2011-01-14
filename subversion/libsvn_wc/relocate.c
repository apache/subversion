/*
 * relocate.c: do wc repos relocation
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



#include "svn_wc.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc.h"
#include "props.h"

#include "svn_private_config.h"


/* */
static const char *
uri_remove_components(const char *uri,
                      const char *component,
                      apr_pool_t *result_pool)
{
  char *result = apr_pstrdup(result_pool, uri);
  char *result_end;
  const char *component_end;

  SVN_ERR_ASSERT_NO_RETURN(svn_uri_is_absolute(uri));
  SVN_ERR_ASSERT_NO_RETURN(!svn_uri_is_absolute(component));

  if (component[0] == 0)
    return result;

  result_end = result + strlen(result) - 1;
  component_end = component + strlen(component) - 1;

  while (component_end >= component)
    {
      if (*result_end != *component_end)
        return NULL;

      component_end--;
      result_end--;
    }

  if (*result_end != '/')
    return NULL;

  *result_end = 0;

  return result;
}

svn_error_t *
svn_wc_relocate4(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *from,
                 const char *to,
                 svn_wc_relocation_validator3_t validator,
                 void *validator_baton,
                 apr_pool_t *scratch_pool)
{
  svn_wc__db_kind_t kind;
  const char *repos_relpath;
  const char *old_repos_root, *old_url;
  const char *new_repos_root, *new_url;
  int from_len, old_url_len;
  const char *uuid;
  svn_boolean_t is_wc_root;

  SVN_ERR(svn_wc__strictly_is_wc_root(&is_wc_root, wc_ctx, local_abspath,
                                      scratch_pool));
  if (! is_wc_root)
    {
      const char *wcroot_abspath;
      svn_error_t *err;

      err = svn_wc__db_get_wcroot(&wcroot_abspath, wc_ctx->db,
                                  local_abspath, scratch_pool, scratch_pool);
      if (err)
        {
          svn_error_clear(err);
          return svn_error_createf(
            SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
            _("Cannot relocate '%s' as it is not the root of a working copy"),
            svn_dirent_local_style(local_abspath, scratch_pool));
        }
      else
        {
          return svn_error_createf(
            SVN_ERR_WC_INVALID_OP_ON_CWD, NULL,
            _("Cannot relocate '%s' as it is not the root of a working copy; "
              "try relocating '%s' instead"),
            svn_dirent_local_style(local_abspath, scratch_pool),
            svn_dirent_local_style(wcroot_abspath, scratch_pool));
        }
    }

  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, &repos_relpath,
                               &old_repos_root, &uuid,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath, scratch_pool,
                               scratch_pool));

  if (kind != svn_wc__db_kind_dir)
    return svn_error_create(SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
                            _("Cannot relocate a single file"));

  old_url = svn_path_url_add_component2(old_repos_root, repos_relpath,
                                        scratch_pool);
  old_url_len = strlen(old_url);
  from_len = strlen(from);
  if ((from_len > old_url_len) || (strncmp(old_url, from, strlen(from)) != 0))
    return svn_error_createf(SVN_ERR_WC_INVALID_RELOCATION, NULL,
                             _("Invalid source URL prefix: '%s' (does not "
                               "overlap target's URL '%s')"),
                             from, old_url);

  if (old_url_len == from_len)
    new_url = to;
  else
    new_url = apr_pstrcat(scratch_pool, to, old_url + from_len, NULL);
  if (! svn_path_is_url(new_url))
    return svn_error_createf(SVN_ERR_WC_INVALID_RELOCATION, NULL,
                             _("Invalid destination URL: '%s'"), new_url);

  new_repos_root = uri_remove_components(new_url, repos_relpath, scratch_pool);
  if (!new_repos_root)
    return svn_error_createf(SVN_ERR_WC_INVALID_RELOCATION, NULL,
                             _("Invalid destination URL: '%s'"), new_url);

  SVN_ERR(validator(validator_baton, uuid, new_url, new_repos_root, scratch_pool));

  return svn_error_return(svn_wc__db_global_relocate(wc_ctx->db, local_abspath,
                                                     new_repos_root,
                                                     scratch_pool));
}
