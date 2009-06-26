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
                 svn_boolean_t recurse,
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

  SVN_ERR(svn_wc__db_global_relocate(wc_ctx->db, local_abspath, new_repos_root,
                                     FALSE, scratch_pool));

  if (!recurse)
    {
      /* This gets sticky.  We need to do the above relocation, and then
         relocate each of the children *back* to the original location.  Ugh.
       */
      const apr_array_header_t *children;
      apr_pool_t *iterpool;
      int i;

      SVN_ERR(svn_wc__db_read_children(&children, wc_ctx->db, local_abspath,
                                       scratch_pool, scratch_pool));
      iterpool = svn_pool_create(scratch_pool);
      for (i = 0; i < children->nelts; i++)
        {
          const char *child = APR_ARRAY_IDX(children, i, const char *);
          const char *child_abspath;
          const char *child_from;
          const char *child_to;

          svn_pool_clear(iterpool);
          child_abspath = svn_dirent_join(local_abspath, child, iterpool);

          /* We invert the "from" and "to" because we're switching the
             children back to the original location. */
          child_from = svn_uri_join(to, child, iterpool);
          child_to = svn_uri_join(from, child, iterpool);

          SVN_ERR(svn_wc_relocate4(wc_ctx, child_abspath, child_from,
                                   child_to, TRUE, validator, validator_baton,
                                   iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}
