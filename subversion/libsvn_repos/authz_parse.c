/* authz_parse.c : Parser for path-based access control
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

#include <apr_tables.h>

#include "svn_ctype.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_iter.h"
#include "svn_pools.h"
#include "svn_repos.h"

#include "private/svn_fspath.h"
#include "private/svn_config_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"

#include "svn_private_config.h"

#include "authz.h"


/* Temporary ACL constructed by the parser. */
typedef struct parsed_acl_t
{
  /* The global ACL.
     ACL.repos and ACL>rule are allocated from the result pool.
     ACL.user_access is null during the parsing stage. */
  authz_acl_t acl;

  /* The set of access control entries. In the second pass, aliases in
     these entries will be expanded and equivalent entries will be
     merged. The entries are allocated from the parser pool. */
  apr_hash_t *aces;

  /* The set of access control entries that use aliases. In the second
     pass, aliases in these entries will be expanded and merged into ACES.
     The entries are allocated from the parser pool. */
  apr_hash_t *alias_aces;
} parsed_acl_t;


/* Temporary group definition constructed by the authz/group parser.
   Once all groups and aliases are defined, a second pass over these
   data will recursively expand group memberships. */
typedef struct parsed_group_t
{
  svn_boolean_t local_group;
  apr_array_header_t *members;
} parsed_group_t;


/* Baton for the parser constructor. */
typedef struct ctor_baton_t
{
  /* The final output of the parser. */
  svn_authz_tng_t *authz;

  /* Interned-string set, allocated in AUTHZ->pool.
     Stores singleton instances of user, group and repository names,
     which are used by members of the AUTHZ structure. By reusing the
     same immutable string multiple times, we reduce the size of the
     authz representation in the result pool.

     N.B.: Whilst the strings are allocated from teh result pool, the
     hash table itself is not. */
  apr_hash_t *strings;

  /* A set of all the sections that were seen in the authz or global
     groups file. Rules, aliases and groups may each only be defined
     once in the authz file. The global groups file may only contain a
     [groups] section. */
  apr_hash_t *sections;

  /* The name of the section we're currently parsing. */
  const char *section;

  /* TRUE iff we're parsing the global groups file. */
  svn_boolean_t parsing_groups;

  /* TRUE iff we're parsing a [groups] section. */
  svn_boolean_t in_groups;

  /* TRUE iff we're parsing an [aliases] section. */
  svn_boolean_t in_aliases;

  /* Temporary parsed-groups definitions. */
  apr_hash_t *parsed_groups;

  /* Temporary alias mappings. */
  apr_hash_t *parsed_aliases;

  /* Temporary parsed-acl definitions. */
  apr_array_header_t *parsed_acls;

  /* The temporary ACL we're currently constructing. */
  parsed_acl_t *current_acl;

  /* The parser's scratch pool. This may not be the same pool as
     passed to the constructor callbacks, that is supposed to be an
     iteration pool maintained by the generic parser.

     N.B.: The result pool is AUTHZ->pool. */
  apr_pool_t *parser_pool;
} ctor_baton_t;


/* The name of the aliases section. */
static const char aliases_section[] = "aliases";

/* The name of the groups section. */
static const char groups_section[] = "groups";

/* The token indicating that an authz rule contains wildcards. */
static const char glob_rule_token[] = "glob";

/* The anonymous access token. */
static const char anon_access_token[] = "$anonymous";

/* The authenticated access token. */
static const char authn_access_token[] = "$authenticated";


/* Initialize a rights structure.
   The minimum rights start with all available access and are later
   bitwise-and'ed with actual access rights. The maximum rights begin
   empty and are later bitwise-and'ed with actual rights. */
static void init_rights(authz_rights_t *rights)
{
  rights->min_access = svn_authz_read | svn_authz_write;
  rights->max_access = svn_authz_none;
 }

/* Initialize a global rights structure.
   The USER string must be interned or statically initialized. */
static void
init_global_rights(authz_global_rights_t *gr, const char *user,
                   apr_pool_t *result_pool)
{
  gr->user = user;
  init_rights(&gr->all_repos_rights);
  init_rights(&gr->any_repos_rights);
  gr->per_repos_rights = apr_hash_make(result_pool);
}


/* Initialize a constuctor baton. */
static ctor_baton_t *
create_ctor_baton(apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_pool_t *const parser_pool = svn_pool_create(scratch_pool);
  ctor_baton_t *const cb = apr_pcalloc(parser_pool, sizeof(*cb));

  svn_authz_tng_t *const authz = apr_pcalloc(result_pool, sizeof(*authz));
  init_global_rights(&authz->anon_rights, anon_access_token, result_pool);
  init_global_rights(&authz->authn_rights, authn_access_token, result_pool);
  authz->user_rights = svn_hash__make(result_pool);
  authz->pool = result_pool;

  cb->authz = authz;
  cb->strings = svn_hash__make(parser_pool);

  cb->sections = svn_hash__make(parser_pool);
  cb->section = NULL;
  cb->parsing_groups = FALSE;
  cb->in_groups = FALSE;
  cb->in_aliases = FALSE;

  cb->parsed_groups = svn_hash__make(parser_pool);
  cb->parsed_aliases = svn_hash__make(parser_pool);
  cb->parsed_acls = apr_array_make(parser_pool, 64, sizeof(parsed_acl_t));
  cb->current_acl = NULL;

  cb->parser_pool = parser_pool;

  return cb;
}


/* Create and store per-user global rights.
   The USER string must be interned or statically initialized. */
static void
prepare_global_rights(ctor_baton_t *cb, const char *user)
{
  authz_global_rights_t *gr = svn_hash_gets(cb->authz->user_rights, user);
  if (!gr)
    {
      gr = apr_palloc(cb->authz->pool, sizeof(*gr));
      init_global_rights(gr, user, cb->authz->pool);
      svn_hash_sets(cb->authz->user_rights, gr->user, gr);
    }
}


/* Internalize a string that will be referenced by the parsed svn_authz_t.
   If LEN is (apr_size_t)-1, assume the string is NUL-terminated. */
static const char *
intern_string(ctor_baton_t *cb, const char *str, apr_size_t len)
{
  const char *interned;

  if (len == (apr_size_t)-1)
    len = strlen(str);

  interned = apr_hash_get(cb->strings, str, len);
  if (!interned)
    {
      interned = apr_pstrmemdup(cb->authz->pool, str, len);
      apr_hash_set(cb->strings, interned, len, interned);
    }
  return interned;
}


/* Helper for rules_open_section and groups_open_section. */
static svn_error_t *
check_open_section(ctor_baton_t *cb, svn_stringbuf_t *section)
{
  SVN_ERR_ASSERT(!cb->current_acl && !cb->section);
  if (apr_hash_get(cb->sections, section->data, section->len))
    {
      if (cb->parsing_groups)
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Section appears more than once"
              " in the global groups file: [%s]"),
            section->data);
      else
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Section appears more than once"
              " in the authz file: [%s]"),
            section->data);
    }

  cb->section = apr_pstrmemdup(cb->parser_pool, section->data, section->len);
  svn_hash_sets(cb->sections,  cb->section, "");
  return SVN_NO_ERROR;
}


/* Constructor callback: Begins the [groups] section. */
static svn_error_t *
groups_open_section(void *baton, svn_stringbuf_t *section)
{
  ctor_baton_t *const cb = baton;

  if (cb->parsing_groups)
    SVN_ERR(check_open_section(cb, section));

  if (0 == strcmp(section->data, groups_section))
    {
      cb->in_groups = TRUE;
      return SVN_NO_ERROR;
    }

  return svn_error_createf(
      SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
      (cb->parsing_groups
       ? _("Section is not valid in the global group file: [%s]")
       : _("Section is not valid in the authz file: [%s]")),
      section->data);
}


/* Constructor callback: Parses a group declaration. */
static svn_error_t *
groups_add_value(void *baton, svn_stringbuf_t *section,
                 svn_stringbuf_t *option, svn_stringbuf_t *value)
{
  ctor_baton_t *const cb = baton;
  const char *group;
  apr_size_t group_len;

  SVN_ERR_ASSERT(cb->in_groups);

  if (strchr("@$&*~", *option->data))
    {
      if (cb->parsing_groups)
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Global group name '%s' may not begin with '%c'"),
            option->data, *option->data);
      else
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Group name '%s' may not begin with '%c'"),
            option->data, *option->data);
    }

  /* Decorate the name to make lookups consistent. */
  group = apr_pstrcat(cb->parser_pool, "@", option->data, SVN_VA_NULL);
  group_len = option->len + 1;
  if (apr_hash_get(cb->parsed_groups, group, group_len))
    {
      if (cb->parsing_groups)
        return svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                 _("Can't override definition"
                                   " of global group '%s'"),
                                 group);
      else
        return svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                 _("Can't override definition"
                                   " of group '%s'"),
                                 group);
    }

  /* We store the whole group definition, so that we can use the
     temporary groups in the baton hash later to fully expand group
     memberships.
     At this point, we can finally internalize the group name. */
  apr_hash_set(cb->parsed_groups,
               intern_string(cb, group, group_len), group_len,
               svn_cstring_split(value->data, ",", TRUE, cb->parser_pool));

  return SVN_NO_ERROR;
}


/* Constructor callback: Starts a rule or [aliases] section. */
static svn_error_t *
rules_open_section(void *baton, svn_stringbuf_t *section)
{
  ctor_baton_t *const cb = baton;
  const char *rule = section->data;
  apr_size_t rule_len = section->len;
  const char *endp;
  parsed_acl_t acl;

  SVN_ERR(check_open_section(cb, section));

  /* Parse rule property tokens. */
  if (*rule != ':')
    acl.acl.glob = FALSE;
  else
    {
      /* This must be a wildcard rule. */
      apr_size_t token_len;

      ++rule; --rule_len;
      endp = memchr(rule, ':', rule_len);
      if (!endp)
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Empty repository name in authz rule [%s]"),
            section->data);

      /* Note: the size of glob_rule_token includes the NUL terminator. */
      token_len = endp - rule;
      if (token_len != sizeof(glob_rule_token) - 1
          || memcmp(rule, glob_rule_token, token_len))
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Invalid type token '%s' in authz rule [%s]"),
            apr_pstrmemdup(cb->parser_pool, rule, token_len),
            section->data);

      acl.acl.glob = TRUE;
      rule = endp + 1;
    }

  /* Parse the repository name. */
  endp = (*rule == '/' ? NULL : memchr(rule, ':', rule_len));
  if (!endp)
    acl.acl.repos = AUTHZ_ANY_REPOSITORY;
  else
    {
      const apr_size_t repos_len = endp - rule;

      /* The rule contains a repository name. */
      if (0 == repos_len)
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Empty repository name in authz rule [%s]"),
            section->data);

      if (memchr(rule, '/', repos_len))
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Invalid repository name '%s' in authz rule [%s]"),
            apr_pstrmemdup(cb->parser_pool, rule, repos_len),
            section->data);

      acl.acl.repos = intern_string(cb, rule, repos_len);
      rule = endp + 1;
    }

  /* Parse the actual rule. */
  if (*rule == '/')
    {
      ++rule; --rule_len;
      if (svn_fspath__is_canonical(rule))
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Non-canonical path '%s' in in authz rule [%s]"),
            rule, section->data);

      /* FIXME: Normalize any wildcard escape chars here, and turn off
         the glob flag if the rule does not contain any wildcards. */
      acl.acl.rule = intern_string(cb, rule, rule_len);
    }
  else if (0 == strcmp(section->data, aliases_section))
    {
      cb->in_aliases = TRUE;
      return SVN_NO_ERROR;
    }
  else
    {
      /* This must be the [groups] section. */
      return groups_open_section(cb, section);
    }

  acl.acl.sequence_number = cb->parsed_acls->nelts;
  acl.acl.anon_access = svn_authz_none;
  acl.acl.has_anon_access = FALSE;
  acl.acl.authn_access = svn_authz_none;
  acl.acl.has_authn_access = FALSE;
  acl.acl.user_access = NULL;

  acl.aces = svn_hash__make(cb->parser_pool);
  acl.alias_aces = svn_hash__make(cb->parser_pool);

  cb->current_acl = &APR_ARRAY_PUSH(cb->parsed_acls, parsed_acl_t);
  *cb->current_acl = acl;
  return SVN_NO_ERROR;
}


/* Parses an alias declaration. The definition (username) of the
   alias will always be interned. */
static svn_error_t *
add_alias_definition(ctor_baton_t *cb,
                     svn_stringbuf_t *option, svn_stringbuf_t *value)
{
  const char *alias;
  apr_size_t alias_len;
  const char *user;

  if (strchr("@$&*~", *option->data))
    return svn_error_createf(
        SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
        _("Alias name '%s' may not begin with '%c'"),
        option->data, *option->data);

  /* Decorate the name to make lookups consistent. */
  alias = apr_pstrcat(cb->parser_pool, "&", option->data, SVN_VA_NULL);
  alias_len = option->len + 1;
  if (apr_hash_get(cb->parsed_aliases, alias, alias_len))
    return svn_error_createf(
        SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
        _("Can't override definition of alias '%s'"),
        alias);

  user = intern_string(cb, value->data, value->len);
  apr_hash_set(cb->parsed_aliases, alias, alias_len, user);

  /* Prepare the global rights struct for this user. */
  prepare_global_rights(cb, user);
  return SVN_NO_ERROR;
}

/* Parses an access entry. Groups and users in access entry names will
   always be interned, aliases will never be. */
static svn_error_t *
add_access_entry(ctor_baton_t *cb, svn_stringbuf_t *section,
                 svn_stringbuf_t *option, svn_stringbuf_t *value)
{
  parsed_acl_t *const acl = cb->current_acl;
  const char *name = option->data;
  apr_size_t name_len = option->len;
  const svn_boolean_t inverted = (*name == '~');
  svn_boolean_t anonymous = FALSE;
  svn_boolean_t authenticated = FALSE;
  svn_repos_authz_access_t access = svn_authz_none;
  authz_ace_t *ace;
  int i;

  SVN_ERR_ASSERT(acl != NULL);

  if (inverted)
    {
      ++name;
      --name_len;
    }

  /* Determine the access entry type. */
  switch (*name)
    {
    case '~':
      return svn_error_createf(
          SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
          _("Access entry '%s' has more than one inversion;"
            " double negatives are not permitted"),
          option->data);
      break;

    case '*':
      if (name_len != 1)
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Access entry '%s' is not valid;"
              " it must be a single '*'"),
            option->data);

      if (inverted)
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Access entry '~*' will never match"));

      anonymous = TRUE;
      authenticated = TRUE;
      break;

    case '$':
      if (0 == strcmp(name, anon_access_token))
        {
          if (inverted)
            authenticated = TRUE;
          else
            anonymous = TRUE;
        }
      else if (0 == strcmp(name, authn_access_token))
        {
          if (inverted)
            anonymous = TRUE;
          else
            authenticated = TRUE;
        }
      else
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Access entry token '%s' is not valid;"
              " should be '%s' or '%s'"),
            option->data, anon_access_token, authn_access_token);
      break;

    default:
      /* A username, group name or alias. */;
    }

  /* Parse the access rights. */
  for (i = 0; i < value->len; ++i)
    {
      const char access_code = value->data[i];
      switch (access_code)
        {
        case 'r':
          access |= svn_authz_read;
          break;

        case 'w':
          /* FIXME: Idiocy. Write access should imply read access. */
          access |= svn_authz_write;
          break;

        default:
          if (!svn_ctype_isspace(access_code))
            return svn_error_createf(
                SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                _("The access mode '%c' in access entry '%s'"
                  "of rule [%s] is not valid"),
                access_code, option->data, section->data);
      }
    }

  /* Update the parsed ACL with this access entry. */
  if (anonymous || authenticated)
    {
      if (anonymous)
        {
          acl->acl.has_anon_access = TRUE;
          acl->acl.anon_access |= access;
        }
      if (authenticated)
        {
          acl->acl.has_authn_access = TRUE;
          acl->acl.authn_access |= access;
        }
    }
  else
    {
      /* The inversion tag must be part of the key in the hash
         table, otherwise we can't tell regular and inverted
         entries appart. */
      const char *key = (inverted ? name - 1 : name);
      const apr_size_t key_len = (inverted ? name_len + 1 : name_len);
      const svn_boolean_t aliased = (*name == '&');
      apr_hash_t *aces = (aliased ? acl->alias_aces : acl->aces);

      ace = apr_hash_get(aces, key, key_len);
      if (ace)
        ace->access |= access;
      else
        {
          ace = apr_palloc(cb->parser_pool, sizeof(*ace));
          ace->name = (aliased
                       ? apr_pstrmemdup(cb->parser_pool, name, name_len)
                       : intern_string(cb, name, name_len));
          ace->members = NULL;
          ace->inverted = inverted;
          ace->access = access;

          key = (inverted
                 ? apr_pstrmemdup(cb->parser_pool, key, key_len)
                 : ace->name);
          apr_hash_set(aces, key, key_len, ace);

          /* Prepare the global rights struct for this user. */
          if (!aliased && *ace->name != '@')
            prepare_global_rights(cb, ace->name);
        }
    }

  return SVN_NO_ERROR;
}

/* Constructor callback: Parse a rule, alias or group delcaration. */
static svn_error_t *
rules_add_value(void *baton, svn_stringbuf_t *section,
                svn_stringbuf_t *option, svn_stringbuf_t *value)
{
  ctor_baton_t *const cb = baton;

  if (cb->in_groups)
    return groups_add_value(baton, section, option, value);

  if (cb->in_aliases)
    return add_alias_definition(cb, option, value);

  return add_access_entry(cb, section, option, value);
}


/* Constructor callback: Close a section. */
static svn_error_t *
close_section(void *baton, svn_stringbuf_t *section)
{
  ctor_baton_t *const cb = baton;

  SVN_ERR_ASSERT(0 == strcmp(cb->section, section->data));
  cb->section = NULL;
  cb->current_acl = NULL;
  cb->in_groups = FALSE;
  cb->in_aliases = FALSE;
  return SVN_NO_ERROR;
}


/* Add a user to GROUP.
   GROUP is never internalized, but USER always is. */
static void
add_to_group(ctor_baton_t *cb, const char *group, const char *user)
{
  apr_hash_t *members = svn_hash_gets(cb->authz->groups, group);
  if (!members)
    {
      group = intern_string(cb, group, -1);
      members = svn_hash__make(cb->authz->pool);
      svn_hash_sets(cb->authz->groups, group, members);
    }
  svn_hash_sets(members, user, "");
}


/* Hash iterator for expanding group definitions.
   WARNING: This function is recursive! */
static svn_error_t *
expand_group_callback(void *baton,
                      const void *key,
                      apr_ssize_t klen,
                      void *value,
                      apr_pool_t *scratch_pool)
{
  ctor_baton_t *const cb = baton;
  const char *const group = key;
  apr_array_header_t *members = value;

  int i;
  for (i = 0; i < members->nelts; ++i)
    {
      const char *member = APR_ARRAY_IDX(members, i, const char*);
      if (0 == strcmp(member, group))
            return svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     _("Recursive definition of group '%s'"),
                                     group);

      if (*member == '&')
        {
          /* Add expanded alias to the group.
             N.B.: the user name is already internalized. */
          const char *user = svn_hash_gets(cb->parsed_aliases, member);
          if (!user)
            return svn_error_createf(
                SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                _("Alias '%s' was never defined"),
                member);

          add_to_group(cb, group, user);
        }
      else if (*member != '@')
        {
          /* Add the member to the group. */
          const char *user = intern_string(cb, member, -1);
          add_to_group(cb, group, user);

          /* Prepare the global rights struct for this user. */
          prepare_global_rights(cb, user);
        }
      else
        {
          /* Recursively expand the group membership */
          members = svn_hash_gets(cb->parsed_groups, member);
          if (!members)
            return svn_error_createf(
                SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                _("Undefined group '%s'"),
                member);
          SVN_ERR(expand_group_callback(cb, key, klen,
                                        members, scratch_pool));
        }
    }
  return SVN_NO_ERROR;
}


/* Hash iteration baton for merge_alias_ace. */
typedef struct merge_alias_baton_t
{
  apr_hash_t *aces;
  ctor_baton_t *cb;
} merge_alias_baton_t;

/* Hash iterator for expanding and mergina alias-based ACEs
   into the user/group-based ACEs. */
static svn_error_t *
merge_alias_ace(void *baton,
                const void *key,
                apr_ssize_t klen,
                void *value,
                apr_pool_t *scratch_pool)
{
  merge_alias_baton_t *const mab = baton;
  authz_ace_t *aliased_ace = value;
  const char *alias = aliased_ace->name;
  const char *unaliased_key;
  const char *user;
  authz_ace_t *ace;

  user = svn_hash_gets(mab->cb->parsed_aliases, alias);
  if (!user)
    return svn_error_createf(
        SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
        _("Alias '%s' was never defined"),
        alias);

  /* N.B.: The user name is always internalized,
     but the inverted key may not be. */
  if (!aliased_ace->inverted)
    unaliased_key = user;
  else
    {
      unaliased_key = apr_pstrcat(mab->cb->parser_pool,
                                  "~", user, SVN_VA_NULL);
      unaliased_key = intern_string(mab->cb, unaliased_key, -1);
    }

  ace = svn_hash_gets(mab->aces, unaliased_key);
  if (!ace)
    {
      aliased_ace->name = user;
      svn_hash_sets(mab->aces, unaliased_key, aliased_ace);
    }
  else
    {
      SVN_ERR_ASSERT(!ace->inverted == !aliased_ace->inverted);
      ace->access |= aliased_ace->access;
    }

  return SVN_NO_ERROR;
}


/* Hash iteration baton for array_insert_ace. */
typedef struct insert_ace_baton_t
{
  apr_array_header_t *ace_array;
  ctor_baton_t *cb;
} insert_ace_baton_t;

/* Hash iterator, inserts an ACE into the ACLs array. */
static svn_error_t *
array_insert_ace(void *baton,
                 const void *key,
                 apr_ssize_t klen,
                 void *value,
                 apr_pool_t *scratch_pool)
{
  insert_ace_baton_t *iab = baton;
  authz_ace_t *ace = value;

  /* Add group membership info to the ACE. */
  if (*ace->name == '@')
    {
      SVN_ERR_ASSERT(ace->members == NULL);
      ace->members = svn_hash_gets(iab->cb->authz->groups, ace->name);
      if (!ace->members)
        return svn_error_createf(
            SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
            _("Access entry refers to undefined group '%s'"),
            ace->name);
    }

  APR_ARRAY_PUSH(iab->ace_array, authz_ace_t) = *ace;
  return SVN_NO_ERROR;
}


/* Update accumulated RIGHTS from ACCESS. */
static void
update_rights(authz_rights_t *rights,
              svn_repos_authz_access_t access)
{
  rights->min_access &= access;
  rights->max_access |= access;
}


/* Update a global RIGHTS based on REPOS and ACCESS. */
static void
update_global_rights(authz_global_rights_t *gr,
                     const char *repos,
                     svn_repos_authz_access_t access)
{
  update_rights(&gr->all_repos_rights, access);
  if (0 == strcmp(repos, AUTHZ_ANY_REPOSITORY))
    update_rights(&gr->any_repos_rights, access);
  else
    {
      authz_rights_t *rights = svn_hash_gets(gr->per_repos_rights, repos);
      if (rights)
        update_rights(rights, access);
      else
        {
          rights = apr_palloc(apr_hash_pool_get(gr->per_repos_rights),
                              sizeof(*rights));
          init_rights(rights);
          update_rights(rights, access);
          svn_hash_sets(gr->per_repos_rights, repos, rights);
        }
    }
}


/* Hash iterator to update global per-user rights from an ACL. */
static svn_error_t *
update_user_rights(void *baton,
                   const void *key,
                   apr_ssize_t klen,
                   void *value,
                   apr_pool_t *scratch_pool)
{
  const authz_acl_t *const acl = baton;
  const char *const user = key;
  authz_global_rights_t *const gr = value;
  svn_repos_authz_access_t access;
  svn_boolean_t has_access =
    svn_authz__acl_get_access(&access, acl, user, acl->repos);

  if (has_access)
    update_global_rights(gr, acl->repos, access);
  return SVN_NO_ERROR;
}


/* List iterator, expands/merges a parsed ACL into its final form and
   appends it to the authz info's ACL array. */
static svn_error_t *
expand_acl_callback(void *baton,
                    void *item,
                    apr_pool_t *scratch_pool)
{
  ctor_baton_t *const cb = baton;
  parsed_acl_t *const pacl = item;
  authz_acl_t *const acl = &pacl->acl;

  /* Expand and merge the aliased ACEs. */
  if (apr_hash_count(pacl->alias_aces))
    {
      merge_alias_baton_t mab;
      mab.aces = pacl->aces;
      mab.cb = cb;
      SVN_ERR(svn_iter_apr_hash(NULL, pacl->alias_aces,
                                merge_alias_ace, &mab, scratch_pool));
    }

  /* Make an array from the merged hashes. */
  acl->user_access =
    apr_array_make(cb->authz->pool, apr_hash_count(pacl->aces),
                   sizeof(authz_ace_t));
  {
    insert_ace_baton_t iab;
    iab.ace_array = acl->user_access;
    iab.cb = cb;
    SVN_ERR(svn_iter_apr_hash(NULL, pacl->aces,
                              array_insert_ace, &iab, scratch_pool));
  }

  /* Store the completed ACL into authz. */
  APR_ARRAY_PUSH(cb->authz->acls, authz_acl_t) = *acl;

  /* Update global access rights for this ACL. */
  if (acl->has_anon_access)
    {
      cb->authz->has_anon_rights = TRUE;
      update_global_rights(&cb->authz->anon_rights,
                           acl->repos, acl->anon_access);
    }
  if (acl->has_authn_access)
    {
      cb->authz->has_authn_rights = TRUE;
      update_global_rights(&cb->authz->authn_rights,
                           acl->repos, acl->authn_access);
    }
  SVN_ERR(svn_iter_apr_hash(NULL, cb->authz->user_rights,
                            update_user_rights, acl, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_authz__tng_parse(svn_authz_tng_t **authz,
                     svn_stream_t *rules,
                     svn_stream_t *groups,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  ctor_baton_t *const cb = create_ctor_baton(result_pool, scratch_pool);

  /* Pass 1: Parse the authz file. */
  SVN_ERR(svn_config__parse_stream(rules,
                                   svn_config__constructor_create(
                                       rules_open_section,
                                       close_section,
                                       rules_add_value,
                                       cb->parser_pool),
                                   cb, cb->parser_pool));

  /* Pass 1.6487: Parse the global groups file. */
  if (groups)
    {
      /* Check that the authz file did not contain any groups. */
      if (0 != apr_hash_count(cb->parsed_groups))
          return svn_error_create(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                  ("Authz file cannot contain any groups"
                                   " when global groups are being used."));

      apr_hash_clear(cb->sections);
      cb->parsing_groups = TRUE;
      SVN_ERR(svn_config__parse_stream(groups,
                                       svn_config__constructor_create(
                                           groups_open_section,
                                           close_section,
                                           groups_add_value,
                                           cb->parser_pool),
                                       cb, cb->parser_pool));
    }

  /* Pass 2: Expand groups and construct the final svn_authz_t. */
  cb->authz->groups = svn_hash__make(cb->authz->pool);
  SVN_ERR(svn_iter_apr_hash(NULL, cb->parsed_groups,
                            expand_group_callback, cb, cb->parser_pool));

  cb->authz->acls = apr_array_make(cb->authz->pool, cb->parsed_acls->nelts,
                                   sizeof(authz_acl_t));
  SVN_ERR(svn_iter_apr_array(NULL, cb->parsed_acls,
                             expand_acl_callback, cb, cb->parser_pool));

  *authz = cb->authz;
  apr_pool_destroy(cb->parser_pool);
  return SVN_NO_ERROR;
}
