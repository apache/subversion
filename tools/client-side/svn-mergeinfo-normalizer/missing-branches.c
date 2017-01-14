/*
 * missing-branches.c -- Efficiently scan for missing branches.
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

/* ==================================================================== */



/*** Includes. ***/

#include <assert.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"

#include "mergeinfo-normalizer.h"


/*** Code. ***/

struct svn_min__branch_lookup_t
{
  /* Connection to the repository where we are looking for paths.
     If this is NULL, then only local lookups may be performed. */
  svn_ra_session_t *session;

  /* Keyed by const char * FS paths that are known not to exist.
     It is implied that sub-paths won't and can't exist either. */
  apr_hash_t *deleted;

  /* Keyed by const char * FS paths that are known to exist. */
  apr_hash_t *existing;
};

/* Return the location of the last '/' in PATH before LEN.
   Return 0 for root and empty paths.  PATH must be a canonical FS path. */
static apr_size_t
parent_segment(const char *path,
               apr_size_t len)
{
  assert(path[0] == '/');

  if (len <= 1)
    return 0;

  --len;
  while (path[len] != '/')
    --len;

  return len;
}

/* Look for BRANCH in LOOKUP without connecting to the server.  Return
 * svn_tristate_true, if it is known to exist, svn_tristate_false if it is
 * known to not exist.  Otherwise return svn_tristate_unknown. */
static svn_tristate_t
local_lookup(const svn_min__branch_lookup_t *lookup,
             const char *branch)
{
  apr_size_t len;

  /* Non-canonical paths are bad but we let the remote lookup take care of
   * them.  Our hashes simply have no info on them. */
  if (branch[0] != '/')
    return svn_tristate_unknown;

  /* Hard-coded: "/" always exists. */
  if (branch[1] == '\0')
    return svn_tristate_true;

  /* For every existing path that we encountered, there is an entry in the
     EXISITING hash.  So, we can just use that. */
  len = strlen(branch);
  if (apr_hash_get(lookup->existing, branch, len))
    return svn_tristate_true;

  /* Not known to exist and might be known to not exist.  We only record
     the top level deleted directory for DELETED branches, so we need to
     walk up the path until we either find that deletion or an existing
     path.  In the latter case, we don't know what happened to the levels
     below that, including BRANCH. */
  while (len > 0)
    {
      /* Known deleted?  Note that we checked BRANCH for existence but not
         for deletion, yet. */
      if (apr_hash_get(lookup->deleted, branch, len))
        return svn_tristate_false;

      /* Parent known to exist?
         Then, we don't know what happened to the BRANCH. */
      len = parent_segment(branch, len);

      if (apr_hash_get(lookup->existing, branch, len))
        return svn_tristate_unknown;
    }

  /* We don't know. */
  return svn_tristate_unknown;
}

/* Set *DELETED to TRUE, if PATH can't be found at HEAD in SESSION.
   Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
path_deleted(svn_boolean_t *deleted,
            svn_ra_session_t *session,
            const char *path,
            apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(*path == '/');
  SVN_ERR(svn_ra_check_path(session, path + 1, SVN_INVALID_REVNUM, &kind,
                            scratch_pool));
  *deleted = kind == svn_node_none;

  return SVN_NO_ERROR;
}

/* Chop the last segment off PATH.  PATH must be a canonical FS path.
   No-op for the root path. */
static void
to_parent(svn_stringbuf_t *path)
{
  path->len = parent_segment(path->data, path->len);
  if (path->len == 0)
    path->len = 1;

  path->data[path->len] = '\0';
}

/* Contact the repository used by LOOKUP and set *DELETED to TRUE, if path
   BRANCH does not exist at HEAD.  Cache the lookup results in LOOKUP and
   use SCRATCH_POOL for temporary allocations.  Call this only if
   local_lookup returned svn_tristate_unknown. */
static svn_error_t *
remote_lookup(svn_boolean_t *deleted,
              const svn_min__branch_lookup_t *lookup,
              const char *branch,
              apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *path = svn_stringbuf_create(branch, scratch_pool);

  /* We shall call this function only after the local lookup failed. */
  assert(local_lookup(lookup, branch) == svn_tristate_unknown);

  /* Actual repository lookup. */
  SVN_ERR(path_deleted(deleted, lookup->session, branch, scratch_pool));

  /* If the path did not exist, store the furthest non-existent parent. */
  if (*deleted)
    {
      apr_pool_t *iterpool;
      svn_boolean_t is_deleted;
      const char *deleted_path;
      apr_size_t len;

      /* Find the closest parent that exists.  Often, that is something like
         "branches" and the next level already does not exist.  So, use that
         as a heuristics to minimize the number of lookups. */

      /* Set LEN to the length of the last unknown to exist sub-path. */
      svn_stringbuf_t *temp = svn_stringbuf_dup(path, scratch_pool);
      do
        {
          len = temp->len;
          to_parent(temp);
        }
      while (local_lookup(lookup, temp->data) != svn_tristate_true);

      /* Check whether that path actually does not exist. */
      if (len == path->len)
        {
          /* We already know that the full PATH does not exist.
             We get here if the immediate parent of PATH is known to exist. */
          is_deleted = TRUE;
        }
      else
        {
          temp = svn_stringbuf_ncreate(branch, len, scratch_pool);
          SVN_ERR(path_deleted(&is_deleted, lookup->session, temp->data,
                               scratch_pool));
        }

      /* Whether or not that path does not exist, we know now and should
         store that in LOOKUP. */
      if (is_deleted)
        {
          /* We are almost done here. The existing parent is already in
             LOOKUP and we only need to add the deleted path. */
          deleted_path = apr_pstrmemdup(apr_hash_pool_get(lookup->deleted),
                                        branch, len);
          apr_hash_set(lookup->deleted, deleted_path, len, deleted_path);

          return SVN_NO_ERROR;
        }
      else
        {
          /* We just learned that TEMP does exist. Remember this fact and
             later continue the search for the deletion boundary. */
          const char *hash_path
            = apr_pstrmemdup(apr_hash_pool_get(lookup->existing), temp->data,
                             temp->len);

          /* Only add HASH_PATH.  Its parents are already in that hash. */
          apr_hash_set(lookup->existing, hash_path, temp->len, hash_path);
        }

      /* Find the closest parent that does exist.
        "/" exists, hence, this will terminate. */
      iterpool = svn_pool_create(scratch_pool);
      do
        {
          svn_pool_clear(iterpool);

          len = path->len;
          to_parent(path);

          /* We often know that "/branches" etc. exist.  So, we can skip
             the final lookup in that case. */
          if (local_lookup(lookup, path->data) == svn_tristate_true)
            break;

          /* Get the info from the repository. */
          SVN_ERR(path_deleted(&is_deleted, lookup->session, path->data,
                              iterpool));
        }
      while (is_deleted);
      svn_pool_destroy(iterpool);

      /* PATH exists, it's sub-path of length LEN does not. */
      deleted_path = apr_pstrmemdup(apr_hash_pool_get(lookup->deleted),
                                    branch, len);
      apr_hash_set(lookup->deleted, deleted_path, len, deleted_path);
    }

  /* PATH and all its parents exist. Add them to the EXISITING hash.
     Make sure to allocate only the longest path and then reference
     sub-sequences of it to keep memory usage in check. */
  if (!apr_hash_get(lookup->existing, path->data, path->len))
    {
      const char *hash_path
        = apr_pstrmemdup(apr_hash_pool_get(lookup->existing), path->data,
                         path->len);

      /* Note that we don't need to check for exiting entries here because
         the APR hash will reuse existing nodes and we are not allocating
         anything else here.  So, this does not allocate duplicate nodes. */
      for (; path->len > 1; to_parent(path))
        apr_hash_set(lookup->existing, hash_path, path->len, hash_path);
    }

  return SVN_NO_ERROR;
}

svn_min__branch_lookup_t *
svn_min__branch_lookup_create(svn_ra_session_t *session,
                              apr_pool_t *result_pool)
{
  svn_min__branch_lookup_t *result = apr_pcalloc(result_pool,
                                                 sizeof(*result));
  result->session = session;
  result->deleted = svn_hash__make(result_pool);
  result->existing = svn_hash__make(result_pool);

  return result;
}

svn_min__branch_lookup_t *
svn_min__branch_lookup_from_paths(apr_array_header_t *paths,
                                  apr_pool_t *result_pool)
{
  svn_min__branch_lookup_t *result
    = svn_min__branch_lookup_create(NULL, result_pool);

  int i;
  for (i = 0; i < paths->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      if (strlen(path) > 0)
        {
          path = apr_pstrdup(result_pool, path);
          svn_hash_sets(result->deleted, path, path);
        }
    }

  return result;
}

svn_error_t *
svn_min__branch_lookup(svn_boolean_t *deleted,
                       svn_min__branch_lookup_t *lookup,
                       const char *branch,
                       svn_boolean_t local_only,
                       apr_pool_t *scratch_pool)
{
  switch (local_lookup(lookup, branch))
    {
      case svn_tristate_false:
        *deleted = TRUE;
        return SVN_NO_ERROR;

      case svn_tristate_true:
        *deleted = FALSE;
        return SVN_NO_ERROR;

      default:
        /* If the state is unknown and we are only allowed to do a local
           lookup, default to a possible false negative.  Note that not
           having the session available implies local-only lookup. */
        if (local_only || !lookup->session)
          {
            *deleted = FALSE;
            return SVN_NO_ERROR;
          }
    }

  return svn_error_trace(remote_lookup(deleted, lookup, branch,
                                       scratch_pool));
}

apr_array_header_t *
svn_min__branch_deleted_list(svn_min__branch_lookup_t *lookup,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  apr_array_header_t *result = apr_array_make(result_pool,
                                              apr_hash_count(lookup->deleted),
                                              sizeof(const char *));
  apr_hash_index_t *hi;
  for (hi = apr_hash_first(scratch_pool, lookup->deleted);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      apr_size_t len = apr_hash_this_key_len(hi);

      APR_ARRAY_PUSH(result, const char *) = apr_pstrmemdup(result_pool,
                                                            path, len);
    }

  svn_sort__array(result, svn_sort_compare_paths);

  return result;
}
