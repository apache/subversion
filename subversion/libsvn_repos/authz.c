/* authz.c : path-based access control
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


/*** Includes. ***/

#include <assert.h>
#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "svn_config.h"
#include "svn_ctype.h"
#include "private/svn_fspath.h"
#include "private/svn_repos_private.h"
#include "private/svn_subr_private.h"
#include "repos.h"


/*** Utilities. ***/

/* We are using hashes to represent sets.  This glorified macro adds the
 * string KEY to SET. */
static void
set_add_string(apr_hash_t *set,
               const char *key)
{
  apr_hash_set(set, key, strlen(key), "");
}


/*** Users, aliases and groups. ***/

/* Callback baton used with add_alias(). */
typedef struct add_alias_baton_t
{
  /* Store the alias names here (including the '&' prefix). */
  apr_hash_t *aliases;

  /* The user we are currently looking up. */
  const char *user;
} add_alias_baton_t;

/* Implements svn_config_enumerator2_t processing all entries in the
 * [aliases] section. Collect all the aliases for the user specified
 * in VOID_BATON. */
static svn_boolean_t
add_alias(const char *name,
          const char *value,
          void *void_baton,
          apr_pool_t *scrath_pool)
{
  add_alias_baton_t *baton = void_baton;

  /* Is this an alias for the current user? */
  if (strcmp(baton->user, value) == 0)
    {
      /* Add it to our results.  However, decorate it such that it will
         match directly against all occurrences of that alias. */
      apr_pool_t *result_pool = apr_hash_pool_get(baton->aliases);
      const char *decorated_name = apr_pstrcat(result_pool, "&", name,
                                               SVN_VA_NULL);
      set_add_string(baton->aliases, decorated_name);
    }

  /* Keep going. */
  return TRUE;
}

/* Wrapper around svn_config_enumerate2.  Return a hash set containing the
 * USER and all its aliases as defined in CONFIG. */
static apr_hash_t *
get_aliases(svn_config_t *config,
            const char *user,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  apr_hash_t *result = svn_hash__make(result_pool);

  add_alias_baton_t baton;
  baton.aliases = result;
  baton.user = user;

  set_add_string(result, user);
  svn_config_enumerate2(config, "aliases", add_alias, &baton, scratch_pool);

  return result;
}

/* Callback baton to be used with add_group(). */
typedef struct add_group_baton_t
{
  /* Output: Maps C string name (user, decorated alias, decorated group) to
   *         an array of C string decorated group names that the key name is
   *         a direct member of.  I.e. reversal of the group declaration. */
  apr_hash_t *memberships;

  /* Name and aliases of the current user. */
  apr_hash_t *aliases;
} add_group_baton_t;

/* Implements svn_config_enumerator2_t processing all entries in the
 * [groups] section. Collect all groups that contain other groups or
 * directly one of the aliases provided in VOID_BATON. */
static svn_boolean_t
add_group(const char *name,
          const char *value,
          void *void_baton,
          apr_pool_t *scrath_pool)
{
  int i;
  apr_array_header_t *list;
  add_group_baton_t *baton = void_baton;
  apr_pool_t *result_pool = apr_hash_pool_get(baton->memberships);

  /* Decorated group NAME (i.e. '@' added).  Lazily initialized since many
   * groups may not be relevant. */
  const char *decorated_name = NULL;

  /* Store the reversed membership  All group members. */
  list = svn_cstring_split(value, ",", TRUE, scrath_pool);
  for (i = 0; i < list->nelts; i++)
    {
      /* We are only interested in other groups as well as the user(s) given
         through all their aliases. */
      const char *member = APR_ARRAY_IDX(list, i, const char *);
      if (member[0] == '@' || svn_hash_gets(baton->aliases, member))
        {
          /* Ensure there is a map entry for MEMBER. */
          apr_array_header_t *groups = svn_hash_gets(baton->memberships,
                                                     member);
          if (groups == NULL)
            {
              groups = apr_array_make(result_pool, 4, sizeof(const char *));
              member = apr_pstrdup(result_pool, member);
              svn_hash_sets(baton->memberships, member, groups);
            }

          /* Lazily initialize DECORATED_NAME. */
          if (decorated_name == NULL)
            decorated_name = apr_pstrcat(result_pool, "@", name, SVN_VA_NULL);

          /* Finally, add the group to the list of memberships. */
          APR_ARRAY_PUSH(groups, const char *) = decorated_name;
        }
    }

  /* Keep going. */
  return TRUE;
}

/* Wrapper around svn_config_enumerate2.  Find all groups that ALIASES are
 * members of and all groups that other groups are member of. */
static apr_hash_t *
get_group_memberships(svn_config_t *config,
                      apr_hash_t *aliases,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_hash_t *result = svn_hash__make(result_pool);

  add_group_baton_t baton;
  baton.aliases = aliases;
  baton.memberships = result;
  svn_config_enumerate2(config, SVN_CONFIG_SECTION_GROUPS, add_group,
                        &baton, scratch_pool);

  return result;
}

/* Return a hash set of all name keys (plain user name, decorated aliases
 * and decorated group names) that refer to USER in the authz CONFIG.
 * This include indirect group memberships. */
static apr_hash_t *
get_memberships(svn_config_t *config,
                const char *user,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *to_follow;
  apr_hash_index_t *hi;
  apr_hash_t *result, *memberships;
  int i, k;

  /* special case: anonymous user */
  if (user == NULL)
    {
      result = svn_hash__make(result_pool);
      set_add_string(result, "*");
      set_add_string(result, "$anonymous");

      return result;
    }

  /* The USER and all its aliases. */
  result = get_aliases(config, user, result_pool, scratch_pool);

  /* For each potentially relevant decorated user / group / alias name,
   * find the immediate group memberships. */
  memberships = get_group_memberships(config, result, scratch_pool,
                                      scratch_pool);

  /* Now, flatten everything and construct the full result.
   * We start at the user / decorated alias names. */
  to_follow = apr_array_make(scratch_pool, 16, sizeof(const char *));
  for (hi = apr_hash_first(scratch_pool, result); hi; hi = apr_hash_next(hi))
    APR_ARRAY_PUSH(to_follow, const char *) = svn__apr_hash_index_key(hi);

  /* Iteratively add group memberships. */
  for (i = 0; i < to_follow->nelts; ++i)
    {
      /* Is NAME member of any groups? */
      const char *name = APR_ARRAY_IDX(to_follow, i, const char *);
      apr_array_header_t *next = svn_hash_gets(memberships, name);
      if (next)
        {
          /* Add all groups to the result, if not included already
           * (multiple subgroups may belong to the same super group). */
          for (k = 0; k < next->nelts; ++k)
            {
              const char *group = APR_ARRAY_IDX(next, k, const char *);
              if (!svn_hash_gets(result, group))
                {
                  /* New group. Add to result and look for parents later. */
                  set_add_string(result, group);
                  APR_ARRAY_PUSH(to_follow, const char *) = group;
                }
            }
        }
    }

  /* standard memberships */
  set_add_string(result, "*");
  set_add_string(result, "$authenticated");

  return result;
}


/*** Constructing the prefix tree. ***/

/* This structure describes the access rights given to a specific user by
 * a path rule (actually the rule set specified for a path).  I.e. there is
 * one instance of this per path rule.
 * Later commits will add more fields.
 */
typedef struct access_t
{
  svn_repos_authz_access_t rights;
} access_t;

/* The pattern tree.  All relevant path rules are being folded into this
 * prefix tree, with a single, whole segment stored at each node.  The whole
 * tree applies to a single user only.
 */
typedef struct node_t
{
  /* The segment as specified in the path rule.  During the lookup tree walk,
   * this will compared to the respective segment of the path to check. */
  svn_string_t segment;

  /* Access granted to the current user.  If this is NULL, there has been
   * no specific path rule for this PATH but only for some sub-path(s).
   * Never NULL at the root node. */
  access_t *access;

  /* Map of sub-segment(const char *) to respective node (node_t) for all
   * sub-segments that have rules on themselves or their respective subtrees.
   * NULL, if there are no rules for sub-paths relevant to the user. */
  apr_hash_t *sub_nodes;
} node_t;

/* Callback baton to be used with process_rule(). */
typedef struct process_rule_baton_t
{
  /* The user name, their aliases and names of all groups that the user is
     a member of. */
  apr_hash_t *memberships;

  /* TRUE, if we found at least one rule that applies the user. */
  svn_boolean_t found;

  /* Aggregated rights granted to the user. */
  svn_repos_authz_access_t access;
} process_rule_baton_t;

/* Implements svn_config_enumerator2_t processing all entries (rules) in a
 * rule set (path rule).  Aggregate all access rights in VOID_BATON.
 * Note that, within a rule set, are always accumulated, never subtracted. */
static svn_boolean_t
process_rule(const char *name,
             const char *value,
             void *void_baton,
             apr_pool_t *scrath_pool)
{
  process_rule_baton_t *baton = void_baton;
  svn_boolean_t inverted = FALSE;

  /* Is this an inverted rule? */
  if (name[0] == '~')
    {
      inverted = TRUE;
      ++name;
    }

  /* Inversion simply means inverted membership / relevance check. */
  if (inverted == !svn_hash_gets(baton->memberships, name))
    {
      /* The rule applies. Accumulate the rights that the user is given. */
      baton->found = TRUE;
      baton->access |= (strchr(value, 'r') ? svn_authz_read : svn_authz_none)
                    | (strchr(value, 'w') ? svn_authz_write : svn_authz_none);
    }

  return TRUE;
}

/* Return whether the path rule SECTION in authz CONFIG applies to any of
 * the user's MEMBERSHIPS.  If it does, return the specified access rights
 * in *ACCESS.
 */
static svn_boolean_t
has_matching_rule(svn_repos_authz_access_t *access,
                  svn_config_t *config,
                  const char *section,
                  apr_hash_t *memberships,
                  apr_pool_t *scratch_pool)
{
  /* Fully initialize the baton. */
  process_rule_baton_t baton;
  baton.memberships = memberships;
  baton.found = FALSE;
  baton.access = svn_authz_none;

  /* Scan the whole rule set in SECTION and collect the access rights. */
  svn_config_enumerate2(config, section, process_rule, &baton, scratch_pool);

  /* Return the results. */
  if (baton.found)
    *access = baton.access;

  return baton.found;
}

/* Constructor utility: Create a new tree node for SEGMENT.
 */
static node_t *
create_node(const char *segment,
            apr_pool_t *result_pool)
{
  apr_size_t len = strlen(segment);

  node_t *result = apr_pcalloc(result_pool, sizeof(*result));
  result->segment.data = apr_pstrmemdup(result_pool, segment, len);
  result->segment.len = len;

  return result;
}

/* Constructor utility:  Below NODE, recursively insert sub-nodes for the
 * path given as *SEGMENTS.  The end of the path is indicated by a NULL
 * SEGMENT.  If matching nodes already exist, use those instead of creating
 * new ones.  Set the leave node's access rights spec to ACCESS.
 */
static void
insert_path(node_t *node,
            const char **segments,
            access_t *access,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  const char *segment = *segments;
  node_t *sub_node;

  /* End of path? */
  if (segment == NULL)
    {
      /* Set access rights.  Since we call this function once per authz
       * config file section, there cannot be multiple paths having the
       * same leave node.  Hence, access gets never overwritten.
       */
      assert(node->access == NULL);
      node->access = access;
      return;
    }

  /* There will be sub-nodes.  Ensure the container is there as well. */
  if (!node->sub_nodes)
    node->sub_nodes = svn_hash__make(result_pool);

  /* Auto-insert a sub-node for the current segment. */
  sub_node = svn_hash_gets(node->sub_nodes, segment);
  if (!sub_node)
    {
      sub_node = create_node(segment, result_pool);
      apr_hash_set(node->sub_nodes, sub_node->segment.data,
                   sub_node->segment.len, sub_node);
    }

  /* Continue at the sub-node with the next segment. */
  insert_path(sub_node, segments + 1, access, result_pool, scratch_pool);
}

/* Callback baton to be used with  process_path_rule().
 */
typedef struct process_path_rule_baton_t
{
  /* The authz config structure from which we extract the user and repo-
   * relevant information. */
  svn_config_t *config;

  /* Ignore path rules that don't apply to this repository. */
  svn_string_t repository;

  /* The user name, their aliases and names of all groups that the user is
     a member of. */
  apr_hash_t *memberships;

  /* Root node of the result tree. Never NULL. */
  node_t *root;

  /* Allocate all nodes and their data from this pool. */
  apr_pool_t *pool;
} process_path_rule_baton_t;

/* Implements svn_config_section_enumerator2_t taking a
 * process_path_rule_baton_t in *VOID_BATON and inserting rules into the
 * result tree, if they are relevant to the given user and repository.
 */
static svn_boolean_t
process_path_rule(const char *name,
                  void *void_baton,
                  apr_pool_t *scratch_pool)
{
  process_path_rule_baton_t *baton = void_baton;
  const char *colon_pos;
  const char *path;
  access_t *access;
  svn_repos_authz_access_t rights;
  apr_array_header_t *segments;

  /* Is this section is relevant to the selected repository? */
  colon_pos = strchr(name, ':');
  if (colon_pos)
      if (   (colon_pos - name != baton->repository.len)
          || memcmp(name, baton->repository.data, baton->repository.len))
        return TRUE;

  /* Ignore sections that are not path rules */
  path = colon_pos ? colon_pos + 1 : name;
  if (path[0] != '/')
    return TRUE;

  /* Skip sections that don't say anything about the current user. */
  if (!has_matching_rule(&rights, baton->config, name,
                         baton->memberships, scratch_pool))
    return TRUE;

  /* Process the path */
  segments = svn_cstring_split(path, "/", FALSE, scratch_pool);
  APR_ARRAY_PUSH(segments, const char *) = NULL;

  /* Access rights to assign. */
  access = apr_pcalloc(baton->pool, sizeof(*access));
  access->rights = rights;

  /* Insert the path rule into the filtered tree. */
  insert_path(baton->root, (void *)segments->elts, access, baton->pool,
              scratch_pool);

  return TRUE;
}

/* From the authz CONFIG, extract the parts relevant to USER and REPOSITORY.
 * Return the filtered rule tree.
 */
static node_t *
create_user_authz(svn_config_t *config,
                  const char *repository,
                  const char *user,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  /* Use a separate sub-pool to keep memory usage tight. */
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  /* Initialize the simple BATON members. */
  process_path_rule_baton_t baton;
  baton.config = config;
  baton.repository.data = repository;
  baton.repository.len = strlen(repository);
  baton.pool = result_pool;

  /* Determine the user's aliases, group memberships etc. */
  baton.memberships = get_memberships(config, user, scratch_pool, subpool);
  svn_pool_clear(subpool);

  /* Filtering and tree construction. */
  baton.root = create_node("", result_pool);
  svn_config_enumerate_sections2(config, process_path_rule, &baton, subpool);

  /* If there is no relevant rule at the root node, the "no access" default
   * applies. */
  if (!baton.root->access)
    {
      baton.root->access = apr_pcalloc(result_pool, sizeof(access_t));
      baton.root->access->rights = svn_authz_none;
    }

  /* Done. */
  svn_pool_destroy(subpool);
  return baton.root;
}


/*** Lookup. ***/

/* Extract the next segment from PATH and copy it into SEGMENT, whose current
 * contents get overwritten.  Empty paths ("") are supported and leading '/'
 * segment separators will be interpreted as an empty segment ("").  Non-
 * normalizes parts, i.e. sequences of '/', will be treated as a single '/'.
 *
 * Return the start of the next segment within PATH, skipping the '/'
 * separator(s).  Return NULL, if there are no further segments.
 *
 * The caller (only called by lookup(), ATM) must ensure that SEGMENT has
 * enough room to store all of PATH.
 */
static const char *
next_segment(svn_stringbuf_t *segment,
             const char *path)
{
  apr_size_t len;
  char c;

  /* Read and scan PATH for NUL and '/' -- whichever comes first. */
  for (len = 0, c = *path; c; c = path[++len])
    if (c == '/')
      {
        /* End of segment. */
        segment->data[len] = 0;
        segment->len = len;

        /* If PATH is not normalized, this is where we skip whole sequences
         * of separators. */
        while (path[++len] == '/')
          ;

        /* Continue behind the last separator in the sequence.  We will
         * treat trailing '/' as indicating an empty trailing segment.
         * Therefore, we never have to return NULL here. */
        return path + len;
      }
    else
      {
        /* Copy segment contents directly into the result buffer.
         * On many architectures, this is almost or entirely for free. */
        segment->data[len] = c;
      }

  /* No separator found, so all of PATH has been the last segment. */
  segment->data[len] = 0;
  segment->len = len;

  /* Tell the caller that this has been the last segment. */
  return NULL;
}

/* Starting at the respective user's authz ROOT node, follow PATH and return
 * TRUE, iff the REQUIRED access has been granted to that user for this PATH.
 * REQUIRED must not contain svn_authz_recursive.  PATH does not need to be
 * normalized, may be empty but must not be NULL.
 */
static svn_boolean_t
lookup(node_t *root,
       const char *path,
       svn_repos_authz_access_t required,
       apr_pool_t *scratch_pool)
{
  /* Create a scratch pad large enough to hold any of PATH's segments. */
  apr_size_t path_len = strlen(path);
  svn_stringbuf_t *scratch_pad = svn_stringbuf_create_ensure(path_len,
                                                             scratch_pool);

  /* Our current position in the path rule tree. */
  node_t *current = root;

  /* Last access rights description that we encountered along the path.
   * By construction, there is always a rule at the root node.  Thus,
   * ACCESS can never be NULL. */
  access_t *access = root->access;

  /* Normalize start and end of PATH.  Most paths will be fully normalized,
   * so keep the overhead as low as possible. */
  if (path_len && path[path_len-1] == '/')
    {
      do
      {
        --path_len;
      }
      while (path_len && path[path_len-1] == '/');
      path = apr_pstrmemdup(scratch_pool, path, path_len);
    }

  while (path[0] == '/')
    ++path;     /* Don't update PATH_LEN as we won't need it anymore. */

  /* Actually walk the path rule tree following PATH until we run out of
   * either tree or PATH. */
  while (current && path)
    {
      /* Extract the next segment. */
      svn_stringbuf_t *segment = scratch_pad;
      path = next_segment(segment, path);

      /* Reached the bottom of the tree? */
      if (current->sub_nodes)
        {
          /* Maybe. Attempt to walk one level down. */
          current = apr_hash_get(current->sub_nodes, segment->data,
                                 segment->len);

          /* If there are path rules for _exactly_ this SEGMENT, then these
           * will be the new authoritative ones for PATH. */
          if (current && current->access)
            access = current->access;
        }
      else
        {
          /* Yes, done. */
          current = NULL;
        }
    }

  /* Return whether the access rights on PATH fully include REQUIRED. */
  return (access->rights & required) == required;
}

/*** Structures. ***/

/* Information for the config enumerators called during authz
   lookup. */
struct authz_lookup_baton {
  /* The authz configuration. */
  svn_config_t *config;

  /* The user name, their aliases and names of all groups that the user is
     a member of. */
  apr_hash_t *memberships;

  /* Explicitly granted rights. */
  svn_repos_authz_access_t allow;
  /* Explicitly denied rights. */
  svn_repos_authz_access_t deny;

  /* The rights required by the caller of the lookup. */
  svn_repos_authz_access_t required_access;

  /* The following are used exclusively in recursive lookups. */

  /* The path in the repository (an fspath) to authorize. */
  const char *repos_path;
  /* repos_path prefixed by the repository name and a colon. */
  const char *qualified_repos_path;

  /* Whether, at the end of a recursive lookup, access is granted. */
  svn_boolean_t access;
};

/* Information for the config enumeration functions called during the
   validation process. */
struct authz_validate_baton {
  svn_config_t *config; /* The configuration file being validated. */
  svn_error_t *err;     /* The error being thrown out of the
                           enumerator, if any. */
};


/*** Checking access. ***/

/* Determine whether the REQUIRED access is granted given what authz
 * to ALLOW or DENY.  Return TRUE if the REQUIRED access is
 * granted.
 *
 * Access is granted either when no required access is explicitly
 * denied (implicit grant), or when the required access is explicitly
 * granted, overriding any denials.
 */
static svn_boolean_t
authz_access_is_granted(svn_repos_authz_access_t allow,
                        svn_repos_authz_access_t deny,
                        svn_repos_authz_access_t required)
{
  svn_repos_authz_access_t stripped_req =
    required & (svn_authz_read | svn_authz_write);

  if ((deny & required) == svn_authz_none)
    return TRUE;
  else if ((allow & required) == stripped_req)
    return TRUE;
  else
    return FALSE;
}


/* Decide whether the REQUIRED access has been conclusively
 * determined.  Return TRUE if the given ALLOW/DENY authz are
 * conclusive regarding the REQUIRED authz.
 *
 * Conclusive determination occurs when any of the REQUIRED authz are
 * granted or denied by ALLOW/DENY.
 */
static svn_boolean_t
authz_access_is_determined(svn_repos_authz_access_t allow,
                           svn_repos_authz_access_t deny,
                           svn_repos_authz_access_t required)
{
  if ((deny & required) || (allow & required))
    return TRUE;
  else
    return FALSE;
}

/* Determines whether an authz rule applies to the current
 * user, given the name part of the rule's name-value pair
 * in RULE_MATCH_STRING and the authz_lookup_baton object
 * B with the username in question.
 */
static svn_boolean_t
authz_line_applies_to_user(const char *rule_match_string,
                           struct authz_lookup_baton *b,
                           apr_pool_t *pool)
{
  if (rule_match_string[0] == '~')
    return svn_hash_gets(b->memberships, rule_match_string + 1) == NULL;

  return svn_hash_gets(b->memberships, rule_match_string) != NULL;
}

/* Callback to parse one line of an authz file and update the
 * authz_baton accordingly.
 */
static svn_boolean_t
authz_parse_line(const char *name, const char *value,
                 void *baton, apr_pool_t *pool)
{
  struct authz_lookup_baton *b = baton;

  /* Stop if the rule doesn't apply to this user. */
  if (!authz_line_applies_to_user(name, b, pool))
    return TRUE;

  /* Set the access grants for the rule. */
  if (strchr(value, 'r'))
    b->allow |= svn_authz_read;
  else
    b->deny |= svn_authz_read;

  if (strchr(value, 'w'))
    b->allow |= svn_authz_write;
  else
    b->deny |= svn_authz_write;

  return TRUE;
}


/* Return TRUE iff the access rules in SECTION_NAME apply to PATH_SPEC
 * (which is a repository name, colon, and repository fspath, such as
 * "myrepos:/trunk/foo").
 */
static svn_boolean_t
is_applicable_section(const char *path_spec,
                      const char *section_name)
{
  apr_size_t path_spec_len = strlen(path_spec);

  return ((strncmp(path_spec, section_name, path_spec_len) == 0)
          && (path_spec[path_spec_len - 1] == '/'
              || section_name[path_spec_len] == '/'
              || section_name[path_spec_len] == '\0'));
}


/* Callback to parse a section and update the authz_baton if the
 * section denies access to the subtree the baton describes.
 */
static svn_boolean_t
authz_parse_section(const char *section_name, void *baton, apr_pool_t *pool)
{
  struct authz_lookup_baton *b = baton;
  svn_boolean_t conclusive;

  /* Does the section apply to us? */
  if (!is_applicable_section(b->qualified_repos_path, section_name)
      && !is_applicable_section(b->repos_path, section_name))
    return TRUE;

  /* Work out what this section grants. */
  b->allow = b->deny = 0;
  svn_config_enumerate2(b->config, section_name,
                        authz_parse_line, b, pool);

  /* Has the section explicitly determined an access? */
  conclusive = authz_access_is_determined(b->allow, b->deny,
                                          b->required_access);

  /* Is access granted OR inconclusive? */
  b->access = authz_access_is_granted(b->allow, b->deny,
                                      b->required_access)
    || !conclusive;

  /* As long as access isn't conclusively denied, carry on. */
  return b->access;
}


/* Validate access to the given user for the subtree starting at the
 * given path.  This function walks the whole authz file in search of
 * rules applying to paths in the requested subtree which deny the
 * requested access.
 *
 * As soon as one is found, or else when the whole ACL file has been
 * searched, return the updated authorization status.
 */
static svn_boolean_t
authz_get_tree_access(svn_config_t *cfg, const char *repos_name,
                      const char *path, const char *user,
                      svn_repos_authz_access_t required_access,
                      apr_pool_t *pool)
{
  struct authz_lookup_baton baton = { 0 };

  baton.config = cfg;
  baton.memberships = get_memberships(cfg, user, pool, pool);
  baton.required_access = required_access;
  baton.repos_path = path;
  baton.qualified_repos_path = apr_pstrcat(pool, repos_name,
                                           ":", path, SVN_VA_NULL);
  /* Default to access granted if no rules say otherwise. */
  baton.access = TRUE;

  svn_config_enumerate_sections2(cfg, authz_parse_section,
                                 &baton, pool);

  return baton.access;
}


/* Callback to parse sections of the configuration file, looking for
   any kind of granted access.  Implements the
   svn_config_section_enumerator2_t interface. */
static svn_boolean_t
authz_get_any_access_parser_cb(const char *section_name, void *baton,
                               apr_pool_t *pool)
{
  struct authz_lookup_baton *b = baton;

  /* Does the section apply to the query? */
  if (section_name[0] == '/'
      || strncmp(section_name, b->qualified_repos_path,
                 strlen(b->qualified_repos_path)) == 0)
    {
      b->allow = b->deny = svn_authz_none;

      svn_config_enumerate2(b->config, section_name,
                            authz_parse_line, baton, pool);
      b->access = authz_access_is_granted(b->allow, b->deny,
                                          b->required_access);

      /* Continue as long as we don't find a determined, granted access. */
      return !(b->access
               && authz_access_is_determined(b->allow, b->deny,
                                             b->required_access));
    }

  return TRUE;
}


/* Walk through the authz CFG to check if USER has the REQUIRED_ACCESS
 * to any path within the REPOSITORY.  Return TRUE if so.  Use POOL
 * for temporary allocations. */
static svn_boolean_t
authz_get_any_access(svn_config_t *cfg, const char *repos_name,
                     const char *user,
                     svn_repos_authz_access_t required_access,
                     apr_pool_t *pool)
{
  struct authz_lookup_baton baton = { 0 };

  baton.config = cfg;
  baton.memberships = get_memberships(cfg, user, pool, pool);
  baton.required_access = required_access;
  baton.access = FALSE; /* Deny access by default. */
  baton.repos_path = "/";
  baton.qualified_repos_path = apr_pstrcat(pool, repos_name,
                                           ":/", SVN_VA_NULL);

  /* We could have used svn_config_enumerate2 for "repos_name:/".
   * However, this requires access for root explicitly (which the user
   * may not always have). So we end up enumerating the sections in
   * the authz CFG and stop on the first match with some access for
   * this user. */
  svn_config_enumerate_sections2(cfg, authz_get_any_access_parser_cb,
                                 &baton, pool);

  /* If walking the configuration was inconclusive, deny access. */
  if (!authz_access_is_determined(baton.allow,
                                  baton.deny, baton.required_access))
    return FALSE;

  return baton.access;
}



/*** Validating the authz file. ***/

/* Check for errors in GROUP's definition of CFG.  The errors
 * detected are references to non-existent groups and circular
 * dependencies between groups.  If an error is found, return
 * SVN_ERR_AUTHZ_INVALID_CONFIG.  Use POOL for temporary
 * allocations only.
 *
 * CHECKED_GROUPS should be an empty (it is used for recursive calls).
 */
static svn_error_t *
authz_group_walk(svn_config_t *cfg,
                 const char *group,
                 apr_hash_t *checked_groups,
                 apr_pool_t *pool)
{
  const char *value;
  apr_array_header_t *list;
  int i;

  svn_config_get(cfg, &value, "groups", group, NULL);
  /* Having a non-existent group in the ACL configuration might be the
     sign of a typo.  Refuse to perform authz on uncertain rules. */
  if (!value)
    return svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                             "An authz rule refers to group '%s', "
                             "which is undefined",
                             group);

  list = svn_cstring_split(value, ",", TRUE, pool);

  for (i = 0; i < list->nelts; i++)
    {
      const char *group_user = APR_ARRAY_IDX(list, i, char *);

      /* If the 'user' is a subgroup, recurse into it. */
      if (*group_user == '@')
        {
          /* A circular dependency between groups is a Bad Thing.  We
             don't do authz with invalid ACL files. */
          if (svn_hash_gets(checked_groups, &group_user[1]))
            return svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG,
                                     NULL,
                                     "Circular dependency between "
                                     "groups '%s' and '%s'",
                                     &group_user[1], group);

          /* Add group to hash of checked groups. */
          svn_hash_sets(checked_groups, &group_user[1], "");

          /* Recurse on that group. */
          SVN_ERR(authz_group_walk(cfg, &group_user[1],
                                   checked_groups, pool));

          /* Remove group from hash of checked groups, so that we don't
             incorrectly report an error if we see it again as part of
             another group. */
          svn_hash_sets(checked_groups, &group_user[1], NULL);
        }
      else if (*group_user == '&')
        {
          const char *alias;

          svn_config_get(cfg, &alias, "aliases", &group_user[1], NULL);
          /* Having a non-existent alias in the ACL configuration might be the
             sign of a typo.  Refuse to perform authz on uncertain rules. */
          if (!alias)
            return svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     "An authz rule refers to alias '%s', "
                                     "which is undefined",
                                     &group_user[1]);
        }
    }

  return SVN_NO_ERROR;
}


/* Callback to perform some simple sanity checks on an authz rule.
 *
 * - If RULE_MATCH_STRING references a group or an alias, verify that
 *   the group or alias definition exists.
 * - If RULE_MATCH_STRING specifies a token (starts with $), verify
 *   that the token name is valid.
 * - If RULE_MATCH_STRING is using inversion, verify that it isn't
 *   doing it more than once within the one rule, and that it isn't
 *   "~*", as that would never match.
 * - Check that VALUE part of the rule specifies only allowed rule
 *   flag characters ('r' and 'w').
 *
 * Return TRUE if the rule has no errors. Use BATON for context and
 * error reporting.
 */
static svn_boolean_t authz_validate_rule(const char *rule_match_string,
                                         const char *value,
                                         void *baton,
                                         apr_pool_t *pool)
{
  const char *val;
  const char *match = rule_match_string;
  struct authz_validate_baton *b = baton;

  /* Make sure the user isn't using double-negatives. */
  if (match[0] == '~')
    {
      /* Bump the pointer past the inversion for the other checks. */
      match++;

      /* Another inversion is a double negative; we can't not stop. */
      if (match[0] == '~')
        {
          b->err = svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     "Rule '%s' has more than one "
                                     "inversion; double negatives are "
                                     "not permitted.",
                                     rule_match_string);
          return FALSE;
        }

      /* Make sure that the rule isn't "~*", which won't ever match. */
      if (strcmp(match, "*") == 0)
        {
          b->err = svn_error_create(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                    "Authz rules with match string '~*' "
                                    "are not allowed, because they never "
                                    "match anyone.");
          return FALSE;
        }
    }

  /* If the rule applies to a group, check its existence. */
  if (match[0] == '@')
    {
      const char *group = &match[1];

      svn_config_get(b->config, &val, "groups", group, NULL);

      /* Having a non-existent group in the ACL configuration might be
         the sign of a typo.  Refuse to perform authz on uncertain
         rules. */
      if (!val)
        {
          b->err = svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     "An authz rule refers to group "
                                     "'%s', which is undefined",
                                     rule_match_string);
          return FALSE;
        }
    }

  /* If the rule applies to an alias, check its existence. */
  if (match[0] == '&')
    {
      const char *alias = &match[1];

      svn_config_get(b->config, &val, "aliases", alias, NULL);

      if (!val)
        {
          b->err = svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     "An authz rule refers to alias "
                                     "'%s', which is undefined",
                                     rule_match_string);
          return FALSE;
        }
     }

  /* If the rule specifies a token, check its validity. */
  if (match[0] == '$')
    {
      const char *token_name = &match[1];

      if ((strcmp(token_name, "anonymous") != 0)
       && (strcmp(token_name, "authenticated") != 0))
        {
          b->err = svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     "Unrecognized authz token '%s'.",
                                     rule_match_string);
          return FALSE;
        }
    }

  val = value;

  while (*val)
    {
      if (*val != 'r' && *val != 'w' && ! svn_ctype_isspace(*val))
        {
          b->err = svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     "The character '%c' in rule '%s' is not "
                                     "allowed in authz rules", *val,
                                     rule_match_string);
          return FALSE;
        }

      ++val;
    }

  return TRUE;
}

/* Callback to check ALIAS's definition for validity.  Use
   BATON for context and error reporting. */
static svn_boolean_t authz_validate_alias(const char *alias,
                                          const char *value,
                                          void *baton,
                                          apr_pool_t *pool)
{
  /* No checking at the moment, every alias is valid */
  return TRUE;
}


/* Callback to check GROUP's definition for cyclic dependancies.  Use
   BATON for context and error reporting. */
static svn_boolean_t authz_validate_group(const char *group,
                                          const char *value,
                                          void *baton,
                                          apr_pool_t *pool)
{
  struct authz_validate_baton *b = baton;

  b->err = authz_group_walk(b->config, group, apr_hash_make(pool), pool);
  if (b->err)
    return FALSE;

  return TRUE;
}


/* Callback to check the contents of the configuration section given
   by NAME.  Use BATON for context and error reporting. */
static svn_boolean_t authz_validate_section(const char *name,
                                            void *baton,
                                            apr_pool_t *pool)
{
  struct authz_validate_baton *b = baton;

  /* Use the group checking callback for the "groups" section... */
  if (strcmp(name, "groups") == 0)
    svn_config_enumerate2(b->config, name, authz_validate_group,
                          baton, pool);
  /* ...and the alias checking callback for "aliases"... */
  else if (strcmp(name, "aliases") == 0)
    svn_config_enumerate2(b->config, name, authz_validate_alias,
                          baton, pool);
  /* ...but for everything else use the rule checking callback. */
  else
    {
      /* Validate the section's name. Skip the optional REPOS_NAME. */
      const char *fspath = strchr(name, ':');
      if (fspath)
        fspath++;
      else
        fspath = name;
      if (! svn_fspath__is_canonical(fspath))
        {
          b->err = svn_error_createf(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                     "Section name '%s' contains non-canonical "
                                     "fspath '%s'",
                                     name, fspath);
          return FALSE;
        }

      svn_config_enumerate2(b->config, name, authz_validate_rule,
                            baton, pool);
    }

  if (b->err)
    return FALSE;

  return TRUE;
}


svn_error_t *
svn_repos__authz_validate(svn_authz_t *authz, apr_pool_t *pool)
{
  struct authz_validate_baton baton = { 0 };

  baton.err = SVN_NO_ERROR;
  baton.config = authz->cfg;

  /* Step through the entire rule file stopping on error. */
  svn_config_enumerate_sections2(authz->cfg, authz_validate_section,
                                 &baton, pool);
  SVN_ERR(baton.err);

  return SVN_NO_ERROR;
}


/* Retrieve the file at DIRENT (contained in a repo) then parse it as a config
 * file placing the result into CFG_P allocated in POOL.
 *
 * If DIRENT cannot be parsed as a config file then an error is returned.  The
 * contents of CFG_P is then undefined.  If MUST_EXIST is TRUE, a missing
 * authz file is also an error.  The CASE_SENSITIVE controls the lookup
 * behavior for section and option names alike.
 *
 * SCRATCH_POOL will be used for temporary allocations. */
static svn_error_t *
authz_retrieve_config_repo(svn_config_t **cfg_p,
                           const char *dirent,
                           svn_boolean_t must_exist,
                           svn_boolean_t case_sensitive,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_repos_t *repos;
  const char *repos_root_dirent;
  const char *fs_path;
  svn_fs_t *fs;
  svn_fs_root_t *root;
  svn_revnum_t youngest_rev;
  svn_node_kind_t node_kind;
  svn_stream_t *contents;

  /* Search for a repository in the full path. */
  repos_root_dirent = svn_repos_find_root_path(dirent, scratch_pool);
  if (!repos_root_dirent)
    return svn_error_createf(SVN_ERR_RA_LOCAL_REPOS_NOT_FOUND, NULL,
                             "Unable to find repository at '%s'", dirent);

  /* Attempt to open a repository at repos_root_dirent. */
  SVN_ERR(svn_repos_open3(&repos, repos_root_dirent, NULL, scratch_pool,
                          scratch_pool));

  fs_path = &dirent[strlen(repos_root_dirent)];

  /* Root path is always a directory so no reason to go any further */
  if (*fs_path == '\0')
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             "'/' is not a file in repo '%s'",
                             repos_root_dirent);

  /* We skip some things that are non-important for how we're going to use
   * this repo connection.  We do not set any capabilities since none of
   * the current ones are important for what we're doing.  We also do not
   * setup the environment that repos hooks would run under since we won't
   * be triggering any. */

  /* Get the filesystem. */
  fs = svn_repos_fs(repos);

  /* Find HEAD and the revision root */
  SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, scratch_pool));
  SVN_ERR(svn_fs_revision_root(&root, fs, youngest_rev, scratch_pool));

  SVN_ERR(svn_fs_check_path(&node_kind, root, fs_path, scratch_pool));
  if (node_kind == svn_node_none)
    {
      if (!must_exist)
        {
          SVN_ERR(svn_config_create2(cfg_p, case_sensitive, case_sensitive,
                                     result_pool));
          return SVN_NO_ERROR;
        }
      else
        {
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   "'%s' path not found in repo '%s'", fs_path,
                                   repos_root_dirent);
        }
    }
  else if (node_kind != svn_node_file)
    {
      return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                               "'%s' is not a file in repo '%s'", fs_path,
                               repos_root_dirent);
    }

  SVN_ERR(svn_fs_file_contents(&contents, root, fs_path, scratch_pool));
  err = svn_config_parse(cfg_p, contents, case_sensitive, case_sensitive,
                         result_pool);

  /* Add the URL to the error stack since the parser doesn't have it. */
  if (err != SVN_NO_ERROR)
    return svn_error_createf(err->apr_err, err,
                             "Error while parsing config file: '%s' in repo '%s':",
                             fs_path, repos_root_dirent);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__retrieve_config(svn_config_t **cfg_p,
                           const char *path,
                           svn_boolean_t must_exist,
                           svn_boolean_t case_sensitive,
                           apr_pool_t *pool)
{
  if (svn_path_is_url(path))
    {
      const char *dirent;
      svn_error_t *err;
      apr_pool_t *scratch_pool = svn_pool_create(pool);

      err = svn_uri_get_dirent_from_file_url(&dirent, path, scratch_pool);

      if (err == SVN_NO_ERROR)
        err = authz_retrieve_config_repo(cfg_p, dirent, must_exist,
                                         case_sensitive, pool, scratch_pool);

      /* Close the repos and streams we opened. */
      svn_pool_destroy(scratch_pool);

      return err;
    }
  else
    {
      /* Outside of repo file or Windows registry*/
      SVN_ERR(svn_config_read3(cfg_p, path, must_exist, case_sensitive,
                               case_sensitive, pool));
    }

  return SVN_NO_ERROR;
}


/* Callback to copy (name, value) group into the "groups" section
   of another configuration. */
static svn_boolean_t
authz_copy_group(const char *name, const char *value,
                 void *baton, apr_pool_t *pool)
{
  svn_config_t *authz_cfg = baton;

  svn_config_set(authz_cfg, SVN_CONFIG_SECTION_GROUPS, name, value);

  return TRUE;
}

/* Copy group definitions from GROUPS_CFG to the resulting AUTHZ.
 * If AUTHZ already contains any group definition, report an error.
 * Use POOL for temporary allocations. */
static svn_error_t *
authz_copy_groups(svn_authz_t *authz, svn_config_t *groups_cfg,
                  apr_pool_t *pool)
{
  /* Easy out: we prohibit local groups in the authz file when global
     groups are being used. */
  if (svn_config_has_section(authz->cfg, SVN_CONFIG_SECTION_GROUPS))
    {
      return svn_error_create(SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                              "Authz file cannot contain any groups "
                              "when global groups are being used.");
    }

  svn_config_enumerate2(groups_cfg, SVN_CONFIG_SECTION_GROUPS,
                        authz_copy_group, authz->cfg, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__authz_read(svn_authz_t **authz_p, const char *path,
                      const char *groups_path, svn_boolean_t must_exist,
                      svn_boolean_t accept_urls, apr_pool_t *pool)
{
  svn_authz_t *authz = apr_palloc(pool, sizeof(*authz));

  /* Load the authz file */
  if (accept_urls)
    SVN_ERR(svn_repos__retrieve_config(&authz->cfg, path, must_exist, TRUE,
                                       pool));
  else
    SVN_ERR(svn_config_read3(&authz->cfg, path, must_exist, TRUE, TRUE,
                             pool));

  if (groups_path)
    {
      svn_config_t *groups_cfg;
      svn_error_t *err;

      /* Load the groups file */
      if (accept_urls)
        SVN_ERR(svn_repos__retrieve_config(&groups_cfg, groups_path,
                                           must_exist, TRUE, pool));
      else
        SVN_ERR(svn_config_read3(&groups_cfg, groups_path, must_exist,
                                 TRUE, TRUE, pool));

      /* Copy the groups from groups_cfg into authz. */
      err = authz_copy_groups(authz, groups_cfg, pool);

      /* Add the paths to the error stack since the authz_copy_groups
         routine knows nothing about them. */
      if (err != SVN_NO_ERROR)
        return svn_error_createf(err->apr_err, err,
                                 "Error reading authz file '%s' with "
                                 "groups file '%s':", path, groups_path);
    }

  /* Make sure there are no errors in the configuration. */
  SVN_ERR(svn_repos__authz_validate(authz, pool));

  *authz_p = authz;
  return SVN_NO_ERROR;
}



/*** Public functions. ***/

svn_error_t *
svn_repos_authz_read2(svn_authz_t **authz_p, const char *path,
                      const char *groups_path, svn_boolean_t must_exist,
                      apr_pool_t *pool)
{
  return svn_repos__authz_read(authz_p, path, groups_path, must_exist,
                               TRUE, pool);
}


svn_error_t *
svn_repos_authz_parse(svn_authz_t **authz_p, svn_stream_t *stream,
                      svn_stream_t *groups_stream, apr_pool_t *pool)
{
  svn_authz_t *authz = apr_palloc(pool, sizeof(*authz));

  /* Parse the authz stream */
  SVN_ERR(svn_config_parse(&authz->cfg, stream, TRUE, TRUE, pool));

  if (groups_stream)
    {
      svn_config_t *groups_cfg;

      /* Parse the groups stream */
      SVN_ERR(svn_config_parse(&groups_cfg, groups_stream, TRUE, TRUE, pool));

      SVN_ERR(authz_copy_groups(authz, groups_cfg, pool));
    }

  /* Make sure there are no errors in the configuration. */
  SVN_ERR(svn_repos__authz_validate(authz, pool));

  *authz_p = authz;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_authz_check_access(svn_authz_t *authz, const char *repos_name,
                             const char *path, const char *user,
                             svn_repos_authz_access_t required_access,
                             svn_boolean_t *access_granted,
                             apr_pool_t *pool)
{
  node_t *root;

  if (!repos_name)
    repos_name = "";

  /* If PATH is NULL, check if the user has *any* access. */
  if (!path)
    {
      *access_granted = authz_get_any_access(authz->cfg, repos_name,
                                             user, required_access, pool);
      return SVN_NO_ERROR;
    }

  /* Sanity check. */
  SVN_ERR_ASSERT(path[0] == '/');

  /* Determine the granted access for the requested path.
   * PATH does not need to be normalized for lockup(). */
  root = create_user_authz(authz->cfg, repos_name, user, pool, pool);
  *access_granted = lookup(root, path + 1,
                           required_access & ~svn_authz_recursive, pool);

  /* If the caller requested recursive access, we need to walk through
     the entire authz config to see whether any child paths are denied
     to the requested user. */
  if (*access_granted && (required_access & svn_authz_recursive))
    {
      path = svn_fspath__canonicalize(path, pool);
      *access_granted = authz_get_tree_access(authz->cfg, repos_name, path,
                                              user, required_access, pool);
    }

  return SVN_NO_ERROR;
}
