/*
 * relocate.c: do wc repos relocation
 *
 * ====================================================================
 * Copyright (c) 2002-2006, 2009 CollabNet.  All rights reserved.
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
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc.h"
#include "entries.h"
#include "lock.h"
#include "props.h"

#include "svn_private_config.h"


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
  const char *old_repos_root;
  const char *old_url;
  const char *new_repos_root;

  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, &repos_relpath,
                               &old_repos_root,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath, scratch_pool,
                               scratch_pool));

  if (kind != svn_wc__db_kind_dir)
    return svn_error_create(SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
                            _("Cannot relocate a single file"));

  old_url = svn_uri_join(old_repos_root, repos_relpath, scratch_pool);
  if (strcmp(old_url, from) != 0)
    return svn_error_create(SVN_ERR_WC_INVALID_RELOCATION, NULL,
                            _("Given source URL invalid"));

  new_repos_root = uri_remove_components(to, repos_relpath, scratch_pool);
  if (!new_repos_root)
    return svn_error_createf(SVN_ERR_WC_INVALID_RELOCATION, NULL,
                             _("Given destination URL invalid: '%s'"), to);

  SVN_ERR(validator(validator_baton, NULL, to, new_repos_root, scratch_pool));

  return svn_error_return(svn_wc__db_global_relocate(wc_ctx->db, local_abspath,
                                                     new_repos_root, FALSE,
                                                     scratch_pool));
}

svn_error_t *
svn_wc_relocate3(const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const char *from,
                 const char *to,
                 svn_boolean_t recurse,
                 svn_wc_relocation_validator3_t validator,
                 void *validator_baton,
                 apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  if (recurse)
    return svn_error_return(svn_wc_relocate4(wc_ctx, local_abspath, from, to,
                                             validator, validator_baton,
                                             pool));
  else
    {
      /* This gets sticky.  We need to do the above relocation, and then
         relocate each of the children *back* to the original location.  Ugh.
       */

      /* ### TODO: Actually implement. */
      return svn_error__malfunction(TRUE, __FILE__, __LINE__,
                                    "Not implemented.");
    }
}
