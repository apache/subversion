/* authz.h : authz parsing and searching, private to libsvn_repos
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

#ifndef SVN_REPOS_AUTHZ_H
#define SVN_REPOS_AUTHZ_H

#include <apr_hash.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_config.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_repos.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*
 *   Authz and global group file parsing
 */

/* Number of (user, respoitory) combinations per authz for which we can
 * cache the corresponding filtered path rule trees.
 *
 * Since authz instance are per connection and there is usually only one
 * repository per connection, 2 (user + anonymous) would be sufficient in
 * most cases.  Having 4 adds plenty of headroom and we expect high locality
 * in any case.
 *
 * ### This number will be far too low if/when the parsed authz info
 *     becomes shared between multiple sessions.
 */
#define AUTHZ_FILTERED_CACHE_SIZE 4


/* A dictionary of rules that are specific to a particular
   (user, repository) combination. */
typedef struct authz_user_rules_t authz_user_rules_t;


/* Accumulated global rights for (user, repository). */
typedef struct authz_global_rights_t
{
  /* Interned key of this instance in svn_authz_tng_t::global_rights. */
  const char *key;

  /* The lowest level of access that the user has to every
     path in the repository. */
  svn_repos_authz_access_t min_access;

  /* The highest level of access that the user has to
     any path in the repository. */
  svn_repos_authz_access_t max_access;
} authz_global_rights_t;


/* Immutable authorization info */
typedef struct svn_authz_tng_t
{
  /* All ACLs from the authz file, in the order of definition. */
  apr_array_header_t *acls;

  /* Globally accumulated rights, for all concrete users mentioned
     in the authz file, indexed by (user, repository). */
  apr_hash_t *global_rights;

  /* Fully recursively expanded group definitions, indexed by group name. */
  apr_hash_t *groups;

  /* A cache of rules filtered for a particular user.
     These will be generated on-demand. */
  authz_user_rules_t *user_rules[AUTHZ_FILTERED_CACHE_SIZE];

  /* The pool from which all the parsed authz data is allocated.
     This is hte RESULT_POOL passed to svn_authz__tng_parse.

     It's a good idea to dedicate a pool for the authz structure, so
     that the whole authz representation can be deallocated by
     destroying the pool. */
  apr_pool_t *pool;
} svn_authz_tng_t;


/* An access control list defined by access rules. */
typedef struct authz_acl_t
{
  /* The sequence number of the ACL stores the order in which access
     rules were defined in the authz file. The authz lookup code
     selects the highest-numbered ACL from amongst a set of equivalent
     matches. */
  apr_int64_t sequence_number;

  /* The reposttory name from the rule. This will be empty string if a
     the rule did not name a repository. */
  const char *repos;

  /* The path (or pattern) part of the rule, including the leading /. */
  const char *rule;

  /* TRUE if RULE contains wildcards. */
  svn_boolean_t glob;

  /* Access rigts for anonymous users */
  svn_boolean_t has_anon_access;
  svn_repos_authz_access_t anon_access;

  /* Access rigts for authenticated users */
  svn_boolean_t has_authn_access;
  svn_repos_authz_access_t authn_access;

  /* All other user- or group-specific access rights.
     Aliases are replaced with their definitions, rules for the same
     user or group are merged. */
  apr_array_header_t *user_access;
} authz_acl_t;


/* An access control entry in authz_acl_t::user_access. */
typedef struct authz_ace_t
{
  /* The name of the alias, user or group that this ACE applies to. */
  const char *name;

  /* True if this is an inverse-match rule. */
  svn_boolean_t inverted;

  /* The access rights defined by this ACE. */
  svn_repos_authz_access_t access;
} authz_ace_t;


/* Parse authz definitions from RULES and optional global group
 * definitions from GROUPS, returning an immutable, in-memory
 * representation of all the rules, groups and aliases.
 *
 * **AUTHZ and its contents will be allocated from RESULT_POOL.
 * The function uses SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_authz__tng_parse(svn_authz_tng_t **authz,
                     svn_stream_t *rules,
                     svn_stream_t *groups,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool);


/*
 *   Authorization lookup
 */

/* The "anonymous" user for authz queries. */
#define AUTHZ_ANONYMOUS_USER ((const char*)0)



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_REPOS_AUTHZ_H */
