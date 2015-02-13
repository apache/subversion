/* dag_cache.c : DAG walker and node cache.
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


/* The job of this layer is to take a filesystem with lots of node
   sharing going on --- the real DAG filesystem as it appears in the
   database --- and make it look and act like an ordinary tree
   filesystem, with no sharing.

   We do just-in-time cloning: you can walk from some unfinished
   transaction's root down into directories and files shared with
   committed revisions; as soon as you try to change something, the
   appropriate nodes get cloned (and parent directory entries updated)
   invisibly, behind your back.  Any other references you have to
   nodes that have been cloned by other changes, even made by other
   processes, are automatically updated to point to the right clones.  */


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_mergeinfo.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "svn_sorts.h"

#include "fs.h"
#include "dag.h"
#include "dag_cache.h"
#include "lock.h"
#include "tree.h"
#include "fs_x.h"
#include "fs_id.h"
#include "temp_serializer.h"
#include "cached_data.h"
#include "transaction.h"
#include "pack.h"
#include "util.h"

#include "private/svn_mergeinfo_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "../libsvn_fs/fs-loader.h"




/* Return the change set to a given ROOT. */
static svn_fs_x__change_set_t
root_change_set(svn_fs_root_t *root)
{
  if (root->is_txn_root)
    return svn_fs_x__change_set_by_txn(svn_fs_x__root_txn_id(root));

  return svn_fs_x__change_set_by_rev(root->rev);
}



/*** Path handling ***/

/* DAG caching uses "normalized" paths - which are a relaxed form of
   canonical relpaths.  They omit the leading '/' of the abspath and trim
   any trailing '/'.  Any sequences of '//' will be kept as the path walker
   simply skips over them.

   Non-canonical sections of the path will therefore only impact efficiency
   (extra walker iterations and possibly duplicated entries in the cache)
   but not correctness.

   Another optimization is that we don't NUL-terminate the path but strictly
   use its length info.  That way, it can be traversed easily without
   chopping it up and patching it together again.  ultimately, however,
   the path string is NUL-terminated because we wrapped a NUL-terminated
   C string.
 */

/* Set *RESULT to a normalized version of PATH without actually copying any
   string contents.

   For convenience, return the RESULT pointer as the function value too. */
static svn_string_t *
normalize_path(svn_string_t *result,
               const char *path)
{
  apr_size_t len;

  if (path[0] == '/')
    ++path;

  len = strlen(path);
  while (len && path[len-1] == '/')
    --len;

  result->data = path;
  result->len = len;

  return result;
}

/* Extend PATH, i.e. increase its LEN, to cover the next segment.  Skip
   sequences of '/'.  Store the segment in ENTRY and return a pointer to
   it C string representation.  If no segment has been found (end of path),
   return NULL. */
static const char *
next_entry_name(svn_string_t *path,
                svn_stringbuf_t *entry)
{
  const char *segment_start;
  const char *segment_end;

  /* Moving to the next segment, skip separators
     (normalized does not imply canonical). */
  segment_start = path->data + path->len;
  while (*segment_start == '/')
    ++segment_start;

  /* End of path? */
  if (*segment_start == '\0')
    return NULL;

  /* Find the end of this segment.  Note that strchr would not give us the
     length of the last segment. */
  segment_end = segment_start;
  while (*segment_end != '/' && *segment_end != '\0')
    ++segment_end;

  /* Copy the segment into the result buffer. */
  svn_stringbuf_setempty(entry);
  svn_stringbuf_appendbytes(entry, segment_start,
                            segment_end - segment_start);

  /* Extend the "visible" part of the path to the end of that segment. */
  path->len = segment_end - path->data;

  /* Indicate that we found something. */
  return entry->data;
}

/* Split the normalized PATH into its last segment the corresponding parent
   directory.  Store them in ENTRY and DIRECTORY, respectively.

   If PATH is empty, return FALSE and produce no output.
   Otherwise, return TRUE.
 */
static svn_boolean_t
extract_last_segment(const svn_string_t *path,
                     svn_string_t *directory,
                     svn_stringbuf_t *entry)
{
  const char *segment_start;
  const char *parent_end;

  /* Edge case.  We can't navigate in empty paths. */
  if (path->len == 0)
    return FALSE;

  /* Find the start of the last segment.  Note that normalized paths never
     start nor end with a '/'. */
  segment_start = path->data + path->len - 1;
  while (*segment_start != '/' && segment_start != path->data)
    --segment_start;

  /* At root level already, i.e. no parent? */
  if (segment_start == path->data)
    {
      /* Construct result. */
      directory->data = "";
      directory->len = 0;

      svn_stringbuf_setempty(entry);
      svn_stringbuf_appendbytes(entry, path->data, path->len);

      return TRUE;
    }

  /* Find the end of the parent directory. */
  parent_end = segment_start;
  while (parent_end[-1] == '/')
    --parent_end;

  /* Construct result. */
  directory->data = path->data;
  directory->len = parent_end - path->data;

  ++segment_start; /* previously pointed to the last '/'. */
  svn_stringbuf_setempty(entry);
  svn_stringbuf_appendbytes(entry, segment_start,
                            path->len - (segment_start - path->data));

  return TRUE;
}


/*** Node Caching ***/

/* 1st level cache */

/* An entry in the first-level cache.  REVISION and PATH form the key that
   will ultimately be matched.
 */
typedef struct cache_entry_t
{
  /* hash value derived from PATH, REVISION.
     Used to short-circuit failed lookups. */
  apr_uint32_t hash_value;

  /* change set to which the NODE belongs */
  svn_fs_x__change_set_t change_set;

  /* path of the NODE */
  char *path;

  /* cached value of strlen(PATH). */
  apr_size_t path_len;

  /* the node allocated in the cache's pool. NULL for empty entries. */
  dag_node_t *node;
} cache_entry_t;

/* Number of entries in the cache.  Keep this low to keep pressure on the
   CPU caches low as well.  A binary value is most efficient.  If we walk
   a directory tree, we want enough entries to store nodes for all files
   without overwriting the nodes for the parent folder.  That way, there
   will be no unnecessary misses (except for a few random ones caused by
   hash collision).

   The actual number of instances may be higher but entries that got
   overwritten are no longer visible.
 */
enum { BUCKET_COUNT = 256 };

/* The actual cache structure.  All nodes will be allocated in POOL.
   When the number of INSERTIONS (i.e. objects created form that pool)
   exceeds a certain threshold, the pool will be cleared and the cache
   with it.
 */
struct svn_fs_x__dag_cache_t
{
  /* fixed number of (possibly empty) cache entries */
  cache_entry_t buckets[BUCKET_COUNT];

  /* pool used for all node allocation */
  apr_pool_t *pool;

  /* number of entries created from POOL since the last cleanup */
  apr_size_t insertions;

  /* Property lookups etc. have a very high locality (75% re-hit).
     Thus, remember the last hit location for optimistic lookup. */
  apr_size_t last_hit;

  /* Position of the last bucket hit that actually had a DAG node in it.
     LAST_HIT may refer to a bucket that matches path@rev but has not
     its NODE element set, yet.
     This value is a mere hint for optimistic lookup and any value is
     valid (as long as it is < BUCKET_COUNT). */
  apr_size_t last_non_empty;
};

svn_fs_x__dag_cache_t*
svn_fs_x__create_dag_cache(apr_pool_t *result_pool)
{
  svn_fs_x__dag_cache_t *result = apr_pcalloc(result_pool, sizeof(*result));
  result->pool = svn_pool_create(result_pool);

  return result;
}

/* Clears the CACHE at regular intervals (destroying all cached nodes)
 */
static void
auto_clear_dag_cache(svn_fs_x__dag_cache_t* cache)
{
  if (cache->insertions > BUCKET_COUNT)
    {
      svn_pool_clear(cache->pool);

      memset(cache->buckets, 0, sizeof(cache->buckets));
      cache->insertions = 0;
    }
}

/* For the given CHANGE_SET and PATH, return the respective entry in CACHE.
   If the entry is empty, its NODE member will be NULL and the caller
   may then set it to the corresponding DAG node allocated in CACHE->POOL.
 */
static cache_entry_t *
cache_lookup(svn_fs_x__dag_cache_t *cache,
             svn_fs_x__change_set_t change_set,
             const svn_string_t *path)
{
  apr_size_t i, bucket_index;
  apr_size_t path_len = path->len;
  apr_uint32_t hash_value = (apr_uint32_t)(apr_uint64_t)change_set;

#if SVN_UNALIGNED_ACCESS_IS_OK
  /* "randomizing" / distributing factor used in our hash function */
  const apr_uint32_t factor = 0xd1f3da69;
#endif

  /* optimistic lookup: hit the same bucket again? */
  cache_entry_t *result = &cache->buckets[cache->last_hit];
  if (   (result->change_set == change_set)
      && (result->path_len == path_len)
      && !memcmp(result->path, path->data, path_len))
    {
      /* Remember the position of the last node we found in this cache. */
      if (result->node)
        cache->last_non_empty = cache->last_hit;

      return result;
    }

  /* need to do a full lookup.  Calculate the hash value
     (HASH_VALUE has been initialized to REVISION). */
  i = 0;
#if SVN_UNALIGNED_ACCESS_IS_OK
  /* We relax the dependency chain between iterations by processing
     two chunks from the input per hash_value self-multiplication.
     The HASH_VALUE update latency is now 1 MUL latency + 1 ADD latency
     per 2 chunks instead of 1 chunk.
   */
  for (; i + 8 <= path_len; i += 8)
    hash_value = hash_value * factor * factor
               + (  *(const apr_uint32_t*)(path->data + i) * factor
                  + *(const apr_uint32_t*)(path->data + i + 4));
#endif

  for (; i < path_len; ++i)
    /* Help GCC to minimize the HASH_VALUE update latency by splitting the
       MUL 33 of the naive implementation: h = h * 33 + path[i].  This
       shortens the dependency chain from 1 shift + 2 ADDs to 1 shift + 1 ADD.
     */
    hash_value = hash_value * 32 + (hash_value + (apr_byte_t)path->data[i]);

  bucket_index = hash_value + (hash_value >> 16);
  bucket_index = (bucket_index + (bucket_index >> 8)) % BUCKET_COUNT;

  /* access the corresponding bucket and remember its location */
  result = &cache->buckets[bucket_index];
  cache->last_hit = bucket_index;

  /* if it is *NOT* a match,  clear the bucket, expect the caller to fill
     in the node and count it as an insertion */
  if (   (result->hash_value != hash_value)
      || (result->change_set != change_set)
      || (result->path_len != path_len)
      || memcmp(result->path, path->data, path_len))
    {
      result->hash_value = hash_value;
      result->change_set = change_set;

      if (result->path_len < path_len || result->path_len == 0)
        result->path = apr_palloc(cache->pool, path_len + 1);
      result->path_len = path_len;

      memcpy(result->path, path->data, path_len);
      result->path[path_len] = 0;

      result->node = NULL;

      cache->insertions++;
    }
  else if (result->node)
    {
      /* This bucket is valid & has a suitable DAG node in it.
         Remember its location. */
      cache->last_non_empty = bucket_index;
    }

  return result;
}

/* Optimistic lookup using the last seen non-empty location in CACHE.
   Return the node of that entry, if it is still in use and matches PATH.
   Return NULL otherwise. */
static dag_node_t *
cache_lookup_last_path(svn_fs_x__dag_cache_t *cache,
                       const svn_string_t *path)
{
  cache_entry_t *result = &cache->buckets[cache->last_non_empty];

  if (   result->node
      && (result->path_len == path->len)
      && !memcmp(result->path, path->data, path->len))
    {
      return result->node;
    }

  return NULL;
}

/* Return the cached DAG node for PATH from ROOT's node cache, or NULL if
   the node isn't cached.
 */
static dag_node_t *
dag_node_cache_get(svn_fs_root_t *root,
                   const svn_string_t *path)
{
  svn_fs_x__data_t *ffd = root->fs->fsap_data;
  svn_fs_x__change_set_t change_set = root_change_set(root);

  auto_clear_dag_cache(ffd->dag_node_cache);
  return cache_lookup(ffd->dag_node_cache, change_set, path)->node;
}


/* Add the NODE for PATH to ROOT's node cache. */
void
svn_fs_x__set_dag_node(svn_fs_root_t *root,
                       const char *path,
                       dag_node_t *node)
{
  svn_fs_x__data_t *ffd = root->fs->fsap_data;
  cache_entry_t *bucket;
  svn_fs_x__dag_cache_t *cache = ffd->dag_node_cache;
  svn_string_t normalized;

  auto_clear_dag_cache(cache);
  bucket = cache_lookup(cache, root_change_set(root),
                        normalize_path(&normalized, path));
  bucket->node = svn_fs_x__dag_copy_into_pool(node, cache->pool);
}

void
svn_fs_x__invalidate_dag_cache(svn_fs_root_t *root,
                               const char *path)
{
  svn_fs_x__data_t *ffd = root->fs->fsap_data;
  svn_fs_x__dag_cache_t *cache = ffd->dag_node_cache;
  svn_fs_x__change_set_t change_set = root_change_set(root);

  apr_size_t i;
  for (i = 0; i < BUCKET_COUNT; ++i)
    {
      cache_entry_t *bucket = &cache->buckets[i];
      if (bucket->change_set == change_set && bucket->node)
        {
          /* The call to svn_relpath_skip_ancestor() will require both
             parameters to be canonical.  Since we allow for non-canonical
             paths in our cache (unlikely to actually happen), we drop all
             such entries.
           */
          if (!svn_relpath_is_canonical(bucket->path)
              || svn_relpath_skip_ancestor(path + 1, bucket->path))
            bucket->node = NULL;
        }
    }
}


/* Getting dag nodes for roots.  */

/* Set *NODE_P to a mutable root directory for ROOT, cloning if
   necessary, allocating in RESULT_POOL.  ROOT must be a transaction root.
   Use ERROR_PATH in error messages.  Use SCRATCH_POOL for temporaries.*/
static svn_error_t *
mutable_root_node(dag_node_t **node_p,
                  svn_fs_root_t *root,
                  const char *error_path,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  if (root->is_txn_root)
    {
      /* It's a transaction root.  Open a fresh copy.  */
      return svn_fs_x__dag_txn_root(node_p, root->fs,
                                    svn_fs_x__root_txn_id(root),
                                    result_pool, scratch_pool);
    }
  else
    /* If it's not a transaction root, we can't change its contents.  */
    return SVN_FS__ERR_NOT_MUTABLE(root->fs, root->rev, error_path);
}



/* Traversing directory paths.  */

/* Return a text string describing the absolute path of parent path
   DAG_PATH.  It will be allocated in POOL. */
static const char *
parent_path_path(svn_fs_x__dag_path_t *dag_path,
                 apr_pool_t *pool)
{
  const char *path_so_far = "/";
  if (dag_path->parent)
    path_so_far = parent_path_path(dag_path->parent, pool);
  return dag_path->entry
    ? svn_fspath__join(path_so_far, dag_path->entry, pool)
    : path_so_far;
}


/* Choose a copy ID inheritance method *INHERIT_P to be used in the
   event that immutable node CHILD in FS needs to be made mutable.  If
   the inheritance method is copy_id_inherit_new, also return a
   *COPY_SRC_PATH on which to base the new copy ID (else return NULL
   for that path).  CHILD must have a parent (it cannot be the root
   node).  Allocations are taken from POOL. */
static svn_error_t *
get_copy_inheritance(svn_fs_x__copy_id_inherit_t *inherit_p,
                     const char **copy_src_path,
                     svn_fs_t *fs,
                     svn_fs_x__dag_path_t *child,
                     apr_pool_t *pool)
{
  svn_fs_x__id_t child_copy_id, parent_copy_id;
  svn_boolean_t related;
  const char *id_path = NULL;
  svn_fs_root_t *copyroot_root;
  dag_node_t *copyroot_node;
  svn_revnum_t copyroot_rev;
  const char *copyroot_path;

  SVN_ERR_ASSERT(child && child->parent);

  /* Initialize some convenience variables. */
  SVN_ERR(svn_fs_x__dag_get_copy_id(&child_copy_id, child->node));
  SVN_ERR(svn_fs_x__dag_get_copy_id(&parent_copy_id, child->parent->node));

  /* If this child is already mutable, we have nothing to do. */
  if (svn_fs_x__dag_check_mutable(child->node))
    {
      *inherit_p = svn_fs_x__copy_id_inherit_self;
      *copy_src_path = NULL;
      return SVN_NO_ERROR;
    }

  /* From this point on, we'll assume that the child will just take
     its copy ID from its parent. */
  *inherit_p = svn_fs_x__copy_id_inherit_parent;
  *copy_src_path = NULL;

  /* Special case: if the child's copy ID is '0', use the parent's
     copy ID. */
  if (svn_fs_x__id_is_root(&child_copy_id))
    return SVN_NO_ERROR;

  /* Compare the copy IDs of the child and its parent.  If they are
     the same, then the child is already on the same branch as the
     parent, and should use the same mutability copy ID that the
     parent will use. */
  if (svn_fs_x__id_eq(&child_copy_id, &parent_copy_id))
    return SVN_NO_ERROR;

  /* If the child is on the same branch that the parent is on, the
     child should just use the same copy ID that the parent would use.
     Else, the child needs to generate a new copy ID to use should it
     need to be made mutable.  We will claim that child is on the same
     branch as its parent if the child itself is not a branch point,
     or if it is a branch point that we are accessing via its original
     copy destination path. */
  SVN_ERR(svn_fs_x__dag_get_copyroot(&copyroot_rev, &copyroot_path,
                                     child->node));
  SVN_ERR(svn_fs_x__revision_root(&copyroot_root, fs, copyroot_rev, pool));
  SVN_ERR(svn_fs_x__get_dag_node(&copyroot_node, copyroot_root,
                                 copyroot_path, pool));

  SVN_ERR(svn_fs_x__dag_related_node(&related, copyroot_node, child->node));
  if (!related)
    return SVN_NO_ERROR;

  /* Determine if we are looking at the child via its original path or
     as a subtree item of a copied tree. */
  id_path = svn_fs_x__dag_get_created_path(child->node);
  if (strcmp(id_path, parent_path_path(child, pool)) == 0)
    {
      *inherit_p = svn_fs_x__copy_id_inherit_self;
      return SVN_NO_ERROR;
    }

  /* We are pretty sure that the child node is an unedited nested
     branched node.  When it needs to be made mutable, it should claim
     a new copy ID. */
  *inherit_p = svn_fs_x__copy_id_inherit_new;
  *copy_src_path = id_path;
  return SVN_NO_ERROR;
}

/* Allocate a new svn_fs_x__dag_path_t node from RESULT_POOL, referring to
   NODE, ENTRY, PARENT, and COPY_ID.  */
static svn_fs_x__dag_path_t *
make_parent_path(dag_node_t *node,
                 char *entry,
                 svn_fs_x__dag_path_t *parent,
                 apr_pool_t *result_pool)
{
  svn_fs_x__dag_path_t *dag_path
    = apr_pcalloc(result_pool, sizeof(*dag_path));
  if (node)
    dag_path->node = svn_fs_x__dag_copy_into_pool(node, result_pool);
  dag_path->entry = entry;
  dag_path->parent = parent;
  dag_path->copy_inherit = svn_fs_x__copy_id_inherit_unknown;
  dag_path->copy_src_path = NULL;
  return dag_path;
}

/* Try a short-cut for the open_path() function using the last node accessed.
 * If that ROOT is that nodes's "created rev" and PATH matches its "created-
 * path", return the node in *NODE_P.  Set it to NULL otherwise.
 *
 * This function is used to support ra_serf-style access patterns where we
 * are first asked for path@rev and then for path@c_rev of the same node.
 * The shortcut works by ignoring the "rev" part of the cache key and then
 * checking whether we got lucky.  Lookup and verification are both quick
 * plus there are many early outs for common types of mismatch.
 */
static svn_error_t *
try_match_last_node(dag_node_t **node_p,
                    svn_fs_root_t *root,
                    const svn_string_t *path)
{
  svn_fs_x__data_t *ffd = root->fs->fsap_data;

  /* Optimistic lookup: if the last node returned from the cache applied to
     the same PATH, return it in NODE. */
  dag_node_t *node = cache_lookup_last_path(ffd->dag_node_cache, path);

  /* Did we get a bucket with a committed node? */
  if (node && !svn_fs_x__dag_check_mutable(node))
    {
      /* Get the path&rev pair at which this node was created.
         This is repository location for which this node is _known_ to be
         the right lookup result irrespective of how we found it. */
      const char *created_path
        = svn_fs_x__dag_get_created_path(node) + 1;
      svn_revnum_t revision = svn_fs_x__dag_get_revision(node);

      /* Is it an exact match? */
      if (   revision == root->rev
          && strlen(created_path) == path->len
          && memcmp(created_path, path->data, path->len) == 0)
        {
          /* Cache it under its full path@rev access path. */
          svn_fs_x__set_dag_node(root, created_path, node);

          *node_p = node;
          return SVN_NO_ERROR;
        }
    }

  *node_p = NULL;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__get_dag_path(svn_fs_x__dag_path_t **dag_path_p,
                       svn_fs_root_t *root,
                       const char *path,
                       int flags,
                       svn_boolean_t is_txn_path,
                       apr_pool_t *pool)
{
  svn_fs_t *fs = root->fs;
  dag_node_t *here = NULL; /* The directory we're currently looking at.  */
  svn_fs_x__dag_path_t *dag_path; /* The path from HERE up to the root. */
  const char *rest = NULL; /* The portion of PATH we haven't traversed yet. */
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* path to the currently processed entry without trailing '/'.
     We will reuse this across iterations by simply putting a NUL terminator
     at the respective position and replacing that with a '/' in the next
     iteration.  This is correct as we assert() PATH to be canonical. */
  svn_stringbuf_t *path_so_far = svn_stringbuf_create(path, pool);

  /* Callers often traverse the DAG in some path-based order or along the
     history segments.  That allows us to try a few guesses about where to
     find the next item.  This is only useful if the caller didn't request
     the full parent chain. */
  assert(svn_fs__is_canonical_abspath(path));
  path_so_far->len = 0; /* "" */

  /* Make a parent_path item for the root node, using its own current
     copy id.  */
  SVN_ERR(svn_fs_x__root_node(&here, root, pool, iterpool));
  rest = path + 1; /* skip the leading '/', it saves in iteration */

  path_so_far->data[path_so_far->len] = '\0';
  dag_path = make_parent_path(here, 0, 0, pool);
  dag_path->copy_inherit = svn_fs_x__copy_id_inherit_self;

  /* Whenever we are at the top of this loop:
     - HERE is our current directory,
     - ID is the node revision ID of HERE,
     - REST is the path we're going to find in HERE, and
     - PARENT_PATH includes HERE and all its parents.  */
  for (;;)
    {
      const char *next;
      char *entry;
      dag_node_t *child;

      svn_pool_clear(iterpool);

      /* The NODE in PARENT_PATH always lives in POOL, i.e. it will
       * survive the cleanup of ITERPOOL and the DAG cache.*/
      here = dag_path->node;

      /* Parse out the next entry from the path.  */
      entry = svn_fs__next_entry_name(&next, rest, pool);

      /* Update the path traversed thus far. */
      path_so_far->data[path_so_far->len] = '/';
      path_so_far->len += strlen(entry) + 1;
      path_so_far->data[path_so_far->len] = '\0';

      /* Given the behavior of svn_fs__next_entry_name(), ENTRY may be an
         empty string when the path either starts or ends with a slash.
         In either case, we stay put: the current directory stays the
         same, and we add nothing to the parent path.  We only need to
         process non-empty path segments. */
      if (*entry != '\0')
        {
          svn_fs_x__copy_id_inherit_t inherit;
          const char *copy_path = NULL;
          dag_node_t *cached_node = NULL;
          svn_string_t normalized;

          /* If we found a directory entry, follow it.  First, we
             check our node cache, and, failing that, we hit the DAG
             layer.  Don't bother to contact the cache for the last
             element if we already know the lookup to fail for the
             complete path. */
          cached_node = dag_node_cache_get(root,
                                           normalize_path(&normalized,
                                                          path_so_far->data));
          if (cached_node)
            child = cached_node;
          else
            SVN_ERR(svn_fs_x__dag_open(&child, here, entry, pool, iterpool));

          /* "file not found" requires special handling.  */
          if (child == NULL)
            {
              /* If this was the last path component, and the caller
                 said it was optional, then don't return an error;
                 just put a NULL node pointer in the path.  */

              if ((flags & svn_fs_x__dag_path_last_optional)
                  && (! next || *next == '\0'))
                {
                  dag_path = make_parent_path(NULL, entry, dag_path, pool);
                  break;
                }
              else if (flags & svn_fs_x__dag_path_allow_null)
                {
                  dag_path = NULL;
                  break;
                }
              else
                {
                  /* Build a better error message than svn_fs_x__dag_open
                     can provide, giving the root and full path name.  */
                  return SVN_FS__NOT_FOUND(root, path);
                }
            }

          /* Now, make a parent_path item for CHILD. */
          dag_path = make_parent_path(child, entry, dag_path, pool);
          if (is_txn_path)
            {
              SVN_ERR(get_copy_inheritance(&inherit, &copy_path, fs,
                                           dag_path, iterpool));
              dag_path->copy_inherit = inherit;
              dag_path->copy_src_path = apr_pstrdup(pool, copy_path);
            }

          /* Cache the node we found (if it wasn't already cached). */
          if (! cached_node)
            svn_fs_x__set_dag_node(root, path_so_far->data, child);
        }

      /* Are we finished traversing the path?  */
      if (! next)
        break;

      /* The path isn't finished yet; we'd better be in a directory.  */
      if (svn_fs_x__dag_node_kind(child) != svn_node_dir)
        SVN_ERR_W(SVN_FS__ERR_NOT_DIRECTORY(fs, path_so_far->data),
                  apr_psprintf(iterpool, _("Failure opening '%s'"), path));

      rest = next;
    }

  svn_pool_destroy(iterpool);
  *dag_path_p = dag_path;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__make_path_mutable(svn_fs_root_t *root,
                            svn_fs_x__dag_path_t *parent_path,
                            const char *error_path,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  dag_node_t *clone;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__root_txn_id(root);

  /* Is the node mutable already?  */
  if (svn_fs_x__dag_check_mutable(parent_path->node))
    return SVN_NO_ERROR;

  /* Are we trying to clone the root, or somebody's child node?  */
  if (parent_path->parent)
    {
      svn_fs_x__id_t copy_id = { SVN_INVALID_REVNUM, 0 };
      svn_fs_x__id_t *copy_id_ptr = &copy_id;
      svn_fs_x__copy_id_inherit_t inherit = parent_path->copy_inherit;
      const char *clone_path, *copyroot_path;
      svn_revnum_t copyroot_rev;
      svn_boolean_t is_parent_copyroot = FALSE;
      svn_fs_root_t *copyroot_root;
      dag_node_t *copyroot_node;
      svn_boolean_t related;
      apr_pool_t *subpool;

      /* We're trying to clone somebody's child.  Make sure our parent
         is mutable.  */
      SVN_ERR(svn_fs_x__make_path_mutable(root, parent_path->parent,
                                          error_path, result_pool,
                                          scratch_pool));

      /* Allocate all temporaries in a sub-pool that we control locally.
         That way, we keep only the data of one level of recursion around
         at any time. */
      subpool = svn_pool_create(scratch_pool);
      switch (inherit)
        {
        case svn_fs_x__copy_id_inherit_parent:
          SVN_ERR(svn_fs_x__dag_get_copy_id(&copy_id,
                                            parent_path->parent->node));
          break;

        case svn_fs_x__copy_id_inherit_new:
          SVN_ERR(svn_fs_x__reserve_copy_id(&copy_id, root->fs, txn_id,
                                            subpool));
          break;

        case svn_fs_x__copy_id_inherit_self:
          copy_id_ptr = NULL;
          break;

        case svn_fs_x__copy_id_inherit_unknown:
        default:
          SVN_ERR_MALFUNCTION(); /* uh-oh -- somebody didn't calculate copy-ID
                      inheritance data. */
        }

      /* Determine what copyroot our new child node should use. */
      SVN_ERR(svn_fs_x__dag_get_copyroot(&copyroot_rev, &copyroot_path,
                                          parent_path->node));
      SVN_ERR(svn_fs_x__revision_root(&copyroot_root, root->fs,
                                      copyroot_rev, subpool));
      SVN_ERR(svn_fs_x__get_dag_node(&copyroot_node, copyroot_root,
                                     copyroot_path, result_pool));

      SVN_ERR(svn_fs_x__dag_related_node(&related, copyroot_node,
                                         parent_path->node));
      if (!related)
        is_parent_copyroot = TRUE;

      /* Now make this node mutable.  */
      clone_path = parent_path_path(parent_path->parent, subpool);
      SVN_ERR(svn_fs_x__dag_clone_child(&clone,
                                        parent_path->parent->node,
                                        clone_path,
                                        parent_path->entry,
                                        copy_id_ptr, txn_id,
                                        is_parent_copyroot,
                                        result_pool,
                                        subpool));

      /* Update the path cache. */
      svn_fs_x__set_dag_node(root, parent_path_path(parent_path, subpool),
                             clone);
      svn_pool_destroy(subpool);
    }
  else
    {
      /* We're trying to clone the root directory.  */
      SVN_ERR(mutable_root_node(&clone, root, error_path, result_pool,
                                scratch_pool));
    }

  /* Update the PARENT_PATH link to refer to the clone.  */
  parent_path->node = clone;

  return SVN_NO_ERROR;
}


/* From directory node PARENT, under ROOT, go one step down to the entry
   NAME and return a reference to it in *CHILD_P.

   PATH is the combination of PARENT's path and NAME and is provided by
   the caller such that we don't have to construct it here ourselves.
   Similarly, CHANGE_SET is redundant with ROOT.

   NOTE: *NODE_P will live within the DAG cache and we merely return a
   reference to it.  Hence, it will invalid upon the next cache insertion.
   Callers must create a copy if they want a non-temporary object.
*/
static svn_error_t *
dag_step(dag_node_t **child_p,
         svn_fs_root_t *root,
         dag_node_t *parent,
         const char *name,
         const svn_string_t *path,
         svn_fs_x__change_set_t change_set,
         apr_pool_t *scratch_pool)
{
  svn_fs_t *fs = svn_fs_x__dag_get_fs(parent);
  svn_fs_x__data_t *ffd = fs->fsap_data;
  cache_entry_t *bucket;
  svn_fs_x__id_t node_id;

  /* Get the ID of the node we are looking for.  The function call checks
     for various error conditions such like PARENT not being a directory. */
  SVN_ERR(svn_fs_x__dir_entry_id(&node_id, parent, name, scratch_pool));
  if (! svn_fs_x__id_used(&node_id))
    {
      const char *dir = apr_pstrmemdup(scratch_pool, path->data, path->len);
      dir = svn_fs__canonicalize_abspath(dir, scratch_pool);

      return SVN_FS__NOT_FOUND(root, dir);
    }

  /* Auto-insert the node in the cache. */
  auto_clear_dag_cache(ffd->dag_node_cache);
  bucket = cache_lookup(ffd->dag_node_cache, change_set, path);

  /* If it is not already cached, construct the DAG node object for NODE_ID.
     Let it live in the cache.  Sadly, we often can't reuse txn DAG nodes. */
  if (bucket->node == NULL || root->is_txn_root)
    SVN_ERR(svn_fs_x__dag_get_node(&bucket->node, fs, &node_id,
                                  ffd->dag_node_cache->pool,
                                  scratch_pool));

  /* Return a reference to the cached object. */
  *child_p = bucket->node;
  return SVN_NO_ERROR;
}

/* Walk the DAG starting at ROOT, following PATH and return a reference to
   the target node in *NODE_P.   Use SCRATCH_POOL for temporary allocations.

   NOTE: *NODE_P will live within the DAG cache and we merely return a
   reference to it.  Hence, it will invalid upon the next cache insertion.
   Callers must create a copy if they want a non-temporary object.
*/
static svn_error_t *
walk_dag_path(dag_node_t **node_p,
              svn_fs_root_t *root,
              svn_string_t *path,
              apr_pool_t *scratch_pool)
{
  dag_node_t *here = NULL; /* The directory we're currently looking at.  */
  apr_pool_t *iterpool;
  svn_fs_x__change_set_t change_set = root_change_set(root);
  const char *entry;
  svn_string_t directory;
  svn_stringbuf_t *entry_buffer;

  /* Special case: root directory.
     We will later assume that all paths have at least one parent level,
     so we must check here for those that don't. */
  if (path->len == 0)
    return svn_error_trace(svn_fs_x__root_node(node_p, root, scratch_pool,
                                               scratch_pool));

  /* Callers often traverse the DAG in some path-based order or along the
     history segments.  That allows us to try a few guesses about where to
     find the next item.  This is only useful if the caller didn't request
     the full parent chain. */

  /* First attempt: Assume that we access the DAG for the same path as
     in the last lookup but for a different revision that happens to be
     the last revision that touched the respective node.  This is a
     common pattern when e.g. checking out over ra_serf.  Note that this
     will only work for committed data as the revision info for nodes in
     txns is bogus.

     This shortcut is quick and will exit this function upon success.
     So, try it first. */
  if (!root->is_txn_root)
    {
      SVN_ERR(try_match_last_node(node_p, root, path));

      /* Did the shortcut work? */
      if (*node_p)
          return SVN_NO_ERROR;
    }

  /* Second attempt: Try starting the lookup immediately at the parent
     node.  We will often have recently accessed either a sibling or
     said parent directory itself for the same revision.  ENTRY will
     point to the last '/' in PATH. */
  entry_buffer = svn_stringbuf_create_ensure(64, scratch_pool);
  if (extract_last_segment(path, &directory, entry_buffer))
    {
      here = dag_node_cache_get(root, &directory);

      /* Did the shortcut work? */
      if (here)
        return svn_error_trace(dag_step(node_p, root, here,
                                        entry_buffer->data, path,
                                        change_set, scratch_pool));
    }

  /* Now there is something to iterate over. Thus, create the ITERPOOL. */
  iterpool = svn_pool_create(scratch_pool);

  /* Make a parent_path item for the root node, using its own current
     copy id.  */
  SVN_ERR(svn_fs_x__root_node(&here, root, scratch_pool, iterpool));
  path->len = 0;

  /* Whenever we are at the top of this loop:
     - HERE is our current directory,
     - REST is the path we're going to find in HERE. */
  for (entry = next_entry_name(path, entry_buffer);
       entry;
       entry = next_entry_name(path, entry_buffer))
    {
      svn_pool_clear(iterpool);

      /* Note that HERE is allocated from the DAG node cache and will
         therefore survive the ITERPOOL cleanup. */
      SVN_ERR(dag_step(&here, root, here, entry, path, change_set, iterpool));
    }

  svn_pool_destroy(iterpool);
  *node_p = here;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_dag_node(dag_node_t **dag_node_p,
                       svn_fs_root_t *root,
                       const char *path,
                       apr_pool_t *pool)
{
  dag_node_t *node = NULL;
  svn_string_t normalized;

  /* First we look for the DAG in our cache. */
  node = dag_node_cache_get(root, normalize_path(&normalized, path));

  /* If it is not there, walk the DAG and fill the cache. */
  if (! node)
    SVN_ERR(walk_dag_path(&node, root, &normalized, pool));

  /* We want the returned node to live in POOL. */
  *dag_node_p = svn_fs_x__dag_copy_into_pool(node, pool);

  return SVN_NO_ERROR;
}
