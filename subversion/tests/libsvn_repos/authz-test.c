/* authz-test.c --- tests for authorization system
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

#include "svn_pools.h"
#include "svn_iter.h"

#include "../../libsvn_repos/authz.h"

#include "../svn_test.h"


static svn_error_t *
print_group_member(void *baton,
                   const void *key, apr_ssize_t klen, void *val,
                   apr_pool_t *pool)
{
  svn_boolean_t *first = baton;
  const char *member = key;
  printf("%s%s", (*first ? "" : ", "), member);
  *first = FALSE;
  return SVN_NO_ERROR;
}


static svn_error_t *
print_group(void *baton,
            const void *key, apr_ssize_t klen, void *val,
            apr_pool_t *pool)
{
  const char *group = key;
  apr_hash_t *members = val;
  svn_boolean_t first = TRUE;
  svn_error_t *err;

  printf("   %s = ", group);
  err = svn_iter_apr_hash(NULL, members,
                          print_group_member, &first, pool);
  printf("\n");

  return err;
}


static const char *
access_string(svn_repos_authz_access_t access)
{
  switch (access & (svn_authz_read | svn_authz_write))
    {
    case svn_authz_none: return ""; break;
    case svn_authz_read: return "r"; break;
    case svn_authz_write: return "w"; break;
    default:
      return "rw";
    }
}


static svn_error_t *
test_authz_parse_tng(const svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  const char *srcdir;
  const char *rules_path;
  apr_file_t *rules_file;
  svn_stream_t *rules;
  const char *groups_path;
  apr_file_t *groups_file;
  svn_stream_t *groups;
  svn_authz_tng_t *authz;
  int i;

  const char *check_user = "wunga";
  const char *check_repo = "bloop";


  SVN_ERR(svn_test_get_srcdir(&srcdir, opts, pool));
  rules_path = svn_dirent_join(srcdir, "authz.rules", pool);
  groups_path = svn_dirent_join(srcdir, "authz.groups", pool);

  SVN_ERR(svn_io_file_open(&rules_file, rules_path,
                           APR_READ, APR_OS_DEFAULT,
                           pool));
  rules = svn_stream_from_aprfile2(rules_file, FALSE, pool);
  SVN_ERR(svn_io_file_open(&groups_file, groups_path,
                           APR_READ, APR_OS_DEFAULT,
                           pool));
  groups = svn_stream_from_aprfile2(groups_file, FALSE, pool);
  SVN_ERR(svn_authz__tng_parse(&authz, rules, groups, pool, pool));

  printf("Access check for ('%s', '%s')\n\n", check_user, check_repo);

  printf("[rules]\n");
  for (i = 0; i < authz->acls->nelts; ++i)
    {
      authz_acl_t *acl = &APR_ARRAY_IDX(authz->acls, i, authz_acl_t);
      const svn_repos_authz_access_t all_access =
        (acl->anon_access & acl->authn_access);
      svn_repos_authz_access_t access;
      svn_boolean_t has_access =
        svn_authz__acl_get_access(&access, acl, check_user, check_repo);
      int j;

      printf("%s%s%s   Sequence:   %d\n"
             "   Repository: [%s]\n"
             "   Rule:  %s[/%s]\n",
             (has_access ? "Match = " : ""),
             (has_access ? access_string(access) : ""),
             (has_access ? "\n" : ""),
             acl->sequence_number,
             acl->repos,
             (acl->glob ? "glob:" : "     "),
             acl->rule);

      if (acl->has_anon_access && acl->has_authn_access
          && all_access != svn_authz_none)
        printf("       * = %s\n", access_string(all_access));

      if (acl->has_anon_access
          && (acl->anon_access & ~all_access) != svn_authz_none)
        printf("       $anonymous = %s\n",
               access_string(acl->anon_access & ~all_access));

      if (acl->has_authn_access
          && (acl->authn_access & ~all_access) != svn_authz_none)
        printf("       $authenticated = %s\n",
               access_string(acl->authn_access & ~all_access));

      for (j = 0; j < acl->user_access->nelts; ++j)
        {
          authz_ace_t *ace = &APR_ARRAY_IDX(acl->user_access, j, authz_ace_t);
          printf("      %c%s = %s\n",
                 (ace->inverted ? '~' : ' '),
                 ace->name, access_string(ace->access));
        }
      printf("\n\n");
    }

  printf("[groups]\n");
  SVN_ERR(svn_iter_apr_hash(NULL, authz->groups,
                            print_group, NULL, pool));
  printf("\n\n");

  return SVN_NO_ERROR;
}


static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_authz_parse_tng,
                       "test svn_authz__tng_parse"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
