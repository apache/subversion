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

#include <apr_fnmatch.h>

#include "svn_pools.h"
#include "svn_iter.h"
#include "svn_hash.h"
#include "private/svn_subr_private.h"

#include "../../libsvn_repos/authz.h"

#include "../svn_test.h"

/* Used to terminate lines in large multi-line string literals. */
#define NL APR_EOL_STR

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
access_string(authz_access_t access)
{
  switch (access & authz_access_write)
    {
    case authz_access_none: return ""; break;
    case authz_access_read_flag: return "r"; break;
    case authz_access_write_flag: return "w"; break;
    default:
      return "rw";
    }
}

static svn_error_t *
print_repos_rights(void *baton,
                   const void *key, apr_ssize_t klen,
                   void *val,
                   apr_pool_t *pool)
{
  const char *repos = key;
  authz_rights_t *rights = val;
  printf("      %s = all:%s  some:%s\n", repos,
         access_string(rights->min_access),
         access_string(rights->max_access));
  return SVN_NO_ERROR;
}

static svn_error_t *
print_user_rights(void *baton, const void *key, apr_ssize_t klen,
                  void *val,
                  apr_pool_t *pool)
{
  authz_global_rights_t *gr = val;

  printf("   %s\n", gr->user);
  printf("      [all] = all:%s  some:%s\n",
         access_string(gr->all_repos_rights.min_access),
         access_string(gr->all_repos_rights.max_access));
  printf("      [any] = all:%s  some:%s\n",
         access_string(gr->any_repos_rights.min_access),
         access_string(gr->any_repos_rights.max_access));
  SVN_ERR(svn_iter_apr_hash(NULL, gr->per_repos_rights,
                            print_repos_rights, NULL, pool));
  return SVN_NO_ERROR;
}

static const char*
rule_string(authz_rule_t* rule, apr_pool_t *pool)
{
  svn_stringbuf_t *str;
  int i;

  if (rule->len == 0)
    return "/";

  str = svn_stringbuf_create_empty(pool);

  for (i = 0; i < rule->len; ++i)
    {
      authz_rule_segment_t *segment = &rule->path[i];

      switch(segment->kind)
        {
        case authz_rule_any_segment:
          svn_stringbuf_appendcstr(str, "/*");
          break;

        case authz_rule_any_recursive:
          svn_stringbuf_appendcstr(str, "/**");
          break;

        case authz_rule_prefix:
          svn_stringbuf_appendcstr(str, "/#");
          svn_stringbuf_appendcstr(str, segment->pattern.data);
          svn_stringbuf_appendbyte(str, '*');
          break;

        case authz_rule_suffix:
          svn_stringbuf_appendcstr(str, "/#*");
          svn_stringbuf_appendcstr(str, segment->pattern.data);
          svn_authz__reverse_string(
              str->data + str->len - segment->pattern.len,
              segment->pattern.len);
          break;

        case authz_rule_fnmatch:
          svn_stringbuf_appendcstr(str, "/%");
          svn_stringbuf_appendcstr(str, segment->pattern.data);
          break;

        default:                /* literal */
          svn_stringbuf_appendcstr(str, "//");
          svn_stringbuf_appendcstr(str, segment->pattern.data);
        }
    }
  return str->data;
}


static svn_boolean_t
has_glob(authz_rule_t* rule)
{
  int i;
  for (i = 0; i < rule->len; ++i)
    {
      authz_rule_segment_t *segment = &rule->path[i];
      if (segment->kind != authz_rule_literal)
        return TRUE;
    }
  return FALSE;
}


static svn_error_t *
test_authz_parse(const svn_test_opts_t *opts,
                 apr_pool_t *pool)
{
  const char *srcdir;
  const char *rules_path;
  apr_file_t *rules_file;
  svn_stream_t *rules;
  const char *groups_path;
  apr_file_t *groups_file;
  svn_stream_t *groups;
  authz_full_t *authz;
  apr_hash_t *groupdefs = svn_hash__make(pool);
  int i;

  const char *check_user = "wunga";
  const char *check_repo = "bloop";
  authz_rights_t global_rights;
  svn_boolean_t global_explicit;


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
  SVN_ERR(svn_authz__parse(&authz, rules, groups, pool, pool));

  printf("Access check for ('%s', '%s')\n", check_user, check_repo);

  global_explicit = svn_authz__get_global_rights(&global_rights, authz,
                                                 check_user, check_repo);
  printf("Global rights: min=%s, max=%s (%s)\n\n",
         access_string(global_rights.min_access),
         access_string(global_rights.max_access),
         (global_explicit ? "explicit" : "implicit"));

  printf("[rules]\n");
  for (i = 0; i < authz->acls->nelts; ++i)
    {
      authz_acl_t *acl = &APR_ARRAY_IDX(authz->acls, i, authz_acl_t);
      const authz_access_t all_access =
        (acl->anon_access & acl->authn_access);
      authz_access_t access;
      svn_boolean_t has_access =
        svn_authz__get_acl_access(&access, acl, check_user, check_repo);
      int j;

      printf("%s%s%s   Sequence:   %d\n"
             "   Repository: [%s]\n"
             "   Rule:  %s[%s]\n",
             (has_access ? "Match = " : ""),
             (has_access ? access_string(access) : ""),
             (has_access ? "\n" : ""),
             acl->sequence_number,
             acl->rule.repos,
             (has_glob(&acl->rule) ? "glob:" : "     "),
             rule_string(&acl->rule, pool));

      if (acl->has_anon_access && acl->has_authn_access)
        printf("       * = %s\n", access_string(all_access));

      if (acl->has_anon_access
          && (acl->anon_access & ~all_access) != svn_authz_none)
        printf("       $anonymous = %s\n",
               access_string(acl->anon_access));

      if (acl->has_authn_access
          && (acl->authn_access & ~all_access) != svn_authz_none)
        printf("       $authenticated = %s\n",
               access_string(acl->authn_access));

      for (j = 0; j < acl->user_access->nelts; ++j)
        {
          authz_ace_t *ace = &APR_ARRAY_IDX(acl->user_access, j, authz_ace_t);
          printf("      %c%s = %s\n",
                 (ace->inverted ? '~' : ' '),
                 ace->name, access_string(ace->access));
          if (ace->members)
            svn_hash_sets(groupdefs, ace->name, ace->members);
        }
      printf("\n\n");
    }

  printf("[groups]\n");
  SVN_ERR(svn_iter_apr_hash(NULL, groupdefs,
                            print_group, NULL, pool));
  printf("\n\n");

  printf("[users]\n");
  if (authz->has_anon_rights)
    print_user_rights(NULL, NULL, 0, &authz->anon_rights, pool);
  if (authz->has_authn_rights)
    print_user_rights(NULL, NULL, 0, &authz->authn_rights, pool);
  SVN_ERR(svn_iter_apr_hash(NULL, authz->user_rights,
                            print_user_rights, NULL, pool));
  printf("\n\n");

  return SVN_NO_ERROR;
}

typedef struct global_right_text_case_t
{
  const char *repos;
  const char *user;
  authz_rights_t rights;
  svn_boolean_t found;
} global_right_text_case_t;

static svn_error_t *
run_global_rights_tests(const char *contents,
                        const global_right_text_case_t *test_cases,
                        apr_pool_t *pool)
{
  svn_authz_t *authz;

  svn_stringbuf_t *buffer = svn_stringbuf_create(contents, pool);
  svn_stream_t *stream = svn_stream_from_stringbuf(buffer, pool);
  SVN_ERR(svn_repos_authz_parse(&authz, stream, NULL, pool));

  for (; test_cases->repos; ++test_cases)
    {
      authz_rights_t rights = { authz_access_write, authz_access_none };
      svn_boolean_t found = svn_authz__get_global_rights(&rights, authz->full,
                                                         test_cases->user,
                                                         test_cases->repos);

      printf("%s %s {%d %d} %d => {%d %d} %d\n",
        test_cases->repos, test_cases->user,
        test_cases->rights.min_access, test_cases->rights.max_access,
        test_cases->found, rights.min_access, rights.max_access, found);
      SVN_TEST_ASSERT(found == test_cases->found);
      SVN_TEST_ASSERT(rights.min_access == test_cases->rights.min_access);
      SVN_TEST_ASSERT(rights.max_access == test_cases->rights.max_access);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_global_rights(apr_pool_t *pool)
{
  const char* authz1 =
    "[/public]"                                                          NL
    "* = r"                                                              NL
    ""                                                                   NL
    "[greek:/A]"                                                         NL
    "userA = rw"                                                         NL
    ""                                                                   NL
    "[repo:/A]"                                                          NL
    "userA = r"                                                          NL
    ""                                                                   NL
    "[repo:/B]"                                                          NL
    "userA = rw"                                                         NL
    ""                                                                   NL
    "[greek:/B]"                                                         NL
    "userB = rw"                                                         NL;

  const global_right_text_case_t test_cases1[] =
    {
      /* Everyone may get read access b/c there might be a "/public" path. */
      {      "",      "", { authz_access_none, authz_access_read  },  TRUE },
      {      "", "userA", { authz_access_none, authz_access_read  },  TRUE },
      {      "", "userB", { authz_access_none, authz_access_read  },  TRUE },
      {      "", "userC", { authz_access_none, authz_access_read  },  TRUE },

      /* Two users do even get write access on some paths in "greek".
       * The root always defaults to n/a due to the default rule. */
      { "greek",      "", { authz_access_none, authz_access_read  }, FALSE },
      { "greek", "userA", { authz_access_none, authz_access_write },  TRUE },
      { "greek", "userB", { authz_access_none, authz_access_write },  TRUE },
      { "greek", "userC", { authz_access_none, authz_access_read  }, FALSE },

      /* One users has write access to some paths in "repo". */
      {  "repo",      "", { authz_access_none, authz_access_read  }, FALSE },
      {  "repo", "userA", { authz_access_none, authz_access_write },  TRUE },
      {  "repo", "userB", { authz_access_none, authz_access_read  }, FALSE },
      {  "repo", "userC", { authz_access_none, authz_access_read  }, FALSE },

      /* For unknown repos, we default to the global settings. */
      {     "X",      "", { authz_access_none, authz_access_read  }, FALSE },
      {     "X", "userA", { authz_access_none, authz_access_read  }, FALSE },
      {     "X", "userB", { authz_access_none, authz_access_read  }, FALSE },
      {     "X", "userC", { authz_access_none, authz_access_read  }, FALSE },

      { NULL }
    };

  const char* authz2 =
    "[/]"                                                                NL
    "userA = r"                                                          NL
    ""                                                                   NL
    "[/public]"                                                          NL
    "userB = rw"                                                         NL
    ""                                                                   NL
    "[repo:/]"                                                           NL
    "userA = rw"                                                         NL;

  const global_right_text_case_t test_cases2[] =
    {
      /* Everyone may get read access b/c there might be a "/public" path. */
      {      "",      "", { authz_access_none, authz_access_none  },  TRUE },
      {      "", "userA", { authz_access_none, authz_access_read  },  TRUE },
      {      "", "userB", { authz_access_none, authz_access_write },  TRUE },
      {      "", "userC", { authz_access_none, authz_access_none  },  TRUE },

      /* Two users do even get write access on some paths in "greek".
       * The root always defaults to n/a due to the default rule. */
      { "greek",      "", { authz_access_none, authz_access_none  }, FALSE },
      { "greek", "userA", { authz_access_none, authz_access_read  }, FALSE },
      { "greek", "userB", { authz_access_none, authz_access_write }, FALSE },
      { "greek", "userC", { authz_access_none, authz_access_none  }, FALSE },

      { NULL }
    };

  const char* authz3 =
    "[/]"                                                                NL
    "userA = r"                                                          NL
    ""                                                                   NL
    "[greek:/public]"                                                    NL
    "userB = rw"                                                         NL
    ""                                                                   NL
    "[repo:/users]"                                                      NL
    "$authenticated = rw"                                                NL;

  const global_right_text_case_t test_cases3[] =
    {
      /* Everyone may get read access b/c there might be a "/public" path. */
      {      "",      "", { authz_access_none, authz_access_none  },  TRUE },
      {      "", "userA", { authz_access_none, authz_access_read  },  TRUE },
      {      "", "userB", { authz_access_none, authz_access_none  },  TRUE },
      {      "", "userC", { authz_access_none, authz_access_none  },  TRUE },

      /* Two users do even get write access on some paths in "greek".
       * The root always defaults to n/a due to the default rule. */
      { "greek",      "", { authz_access_none, authz_access_none  }, FALSE },
      { "greek", "userA", { authz_access_none, authz_access_read  }, FALSE },
      { "greek", "userB", { authz_access_none, authz_access_write },  TRUE },
      { "greek", "userC", { authz_access_none, authz_access_none  }, FALSE },

      /* Two users do even get write access on some paths in "greek".
       * The root always defaults to n/a due to the default rule. */
      {  "repo",      "", { authz_access_none, authz_access_none  }, FALSE },
      {  "repo", "userA", { authz_access_none, authz_access_write },  TRUE },
      {  "repo", "userB", { authz_access_none, authz_access_write },  TRUE },
      {  "repo", "userC", { authz_access_none, authz_access_write },  TRUE },

      { NULL }
    };

  SVN_ERR(run_global_rights_tests(authz1, test_cases1, pool));
  SVN_ERR(run_global_rights_tests(authz2, test_cases2, pool));
  SVN_ERR(run_global_rights_tests(authz3, test_cases3, pool));

  return SVN_NO_ERROR;
}

static int max_threads = 4;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(test_authz_parse,
                       "test svn_authz__parse"),
    SVN_TEST_PASS2(test_global_rights,
                   "test svn_authz__get_global_rights"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
