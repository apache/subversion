/* authz_info.c : Information derived from authz settings.
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

#include <apr_hash.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_hash.h"

#include "svn_private_config.h"

#include "authz.h"


svn_boolean_t
svn_authz__acl_get_access(svn_repos_authz_access_t *access_p,
                          const authz_acl_t *acl,
                          const char *user, const char *repos)
{
  svn_repos_authz_access_t access;
  svn_boolean_t has_access;
  int i;

  /* The repository name must match the one in the rule, iff the rule
     was defined for a specific repository. */
  if (strcmp(acl->repos, AUTHZ_ANY_REPOSITORY) && strcmp(repos, acl->repos))
    return FALSE;

  /* Check anonymous access first. */
  if (!user || 0 == strcmp(user, AUTHZ_ANONYMOUS_USER))
    {
      if (!acl->has_anon_access)
        return FALSE;

      if (access_p)
        *access_p = acl->anon_access;
      return TRUE;
    }

  /* Get the access rights for all authenticated users. */
  has_access = acl->has_authn_access;
  access = (has_access ? acl->authn_access : svn_authz_none);

  /* Scan the ACEs in the ACL and merge the access rights. */
  for (i = 0; i < acl->user_access->nelts; ++i)
    {
      const authz_ace_t *const ace =
        &APR_ARRAY_IDX(acl->user_access, i, authz_ace_t);
      const svn_boolean_t match =
        ((ace->members && svn_hash_gets(ace->members, user))
         || (!ace->members && 0 == strcmp(user, ace->name)));

      if (!match != !ace->inverted) /* match XNOR ace->inverted */
        {
          access |= ace->access;
          has_access = TRUE;
        }
    }

  if (access_p)
    *access_p = access;
  return has_access;
}
