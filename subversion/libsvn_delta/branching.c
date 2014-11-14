/*
 * branching.c : Element-Based Branching and Move Tracking.
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

#include <stddef.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_props.h"

#include "private/svn_editor3.h"
#include "private/svn_sorts_private.h"
#include "svn_private_config.h"


svn_branch_repos_t *
svn_branch_repos_create(apr_pool_t *result_pool)
{
  svn_branch_repos_t *repos = apr_pcalloc(result_pool, sizeof(*repos));

  repos->rev_roots = apr_array_make(result_pool, 1, sizeof(void *));
  repos->families = apr_hash_make(result_pool);
  repos->pool = result_pool;
  return repos;
}

/* Find the existing family with id FID in REPOS.
 *
 * Return NULL if not found.
 *
 * Note: An FID is unique among all families.
 */
static svn_branch_family_t *
repos_get_family_by_id(svn_branch_repos_t *repos,
                       int fid)
{
  return apr_hash_get(repos->families, &fid, sizeof(fid));
}

/* Register FAMILY in REPOS.
 */
static void
repos_register_family(svn_branch_repos_t *repos,
                      svn_branch_family_t *family)
{
  int fid = family->fid;

  apr_hash_set(repos->families,
               apr_pmemdup(repos->pool, &fid, sizeof(fid)), sizeof(fid),
               family);
}

svn_branch_revision_root_t *
svn_branch_revision_root_create(svn_branch_repos_t *repos,
                                svn_revnum_t rev,
                                struct svn_branch_instance_t *root_branch,
                                apr_pool_t *result_pool)
{
  svn_branch_revision_root_t *rev_root
    = apr_pcalloc(result_pool, sizeof(*rev_root));

  rev_root->repos = repos;
  rev_root->rev = rev;
  rev_root->root_branch = root_branch;
  rev_root->branch_instances = apr_array_make(result_pool, 1, sizeof(void *));
  return rev_root;
}

svn_branch_family_t *
svn_branch_family_create(svn_branch_repos_t *repos,
                         int fid,
                         int first_bid,
                         int next_bid,
                         int first_eid,
                         int next_eid,
                         apr_pool_t *result_pool)
{
  svn_branch_family_t *f = apr_pcalloc(result_pool, sizeof(*f));

  f->fid = fid;
  f->repos = repos;
  f->branch_siblings = apr_array_make(result_pool, 1, sizeof(void *));
  f->sub_families = apr_array_make(result_pool, 1, sizeof(void *));
  f->first_bid = first_bid;
  f->next_bid = next_bid;
  f->first_eid = first_eid;
  f->next_eid = next_eid;
  f->pool = result_pool;
  return f;
}

int
svn_branch_family_add_new_element(svn_branch_family_t *family)
{
  int eid = family->next_eid++;

  return eid;
}

svn_branch_family_t *
svn_branch_family_add_new_subfamily(svn_branch_family_t *outer_family)
{
  svn_branch_repos_t *repos = outer_family->repos;
  int fid = repos->next_fid++;
  svn_branch_family_t *family
    = svn_branch_family_create(repos, fid,
                               fid * 10, fid * 10,
                               fid * 100, fid * 100,
                               outer_family->pool);

  /* Register the family */
  repos_register_family(repos, family);
  APR_ARRAY_PUSH(outer_family->sub_families, void *) = family;

  return family;
}

/* Create a new branch sibling in FAMILY, with branch id BID and
 * root element ROOT_EID, and register it as a member of the family.
 */
static svn_branch_sibling_t *
family_create_branch_sibling(svn_branch_family_t *family,
                             int bid,
                             int root_eid)
{
  svn_branch_sibling_t *branch_sibling
    = svn_branch_sibling_create(family, bid, root_eid, family->pool);

  /* The root EID must be an existing EID. */
  SVN_ERR_ASSERT_NO_RETURN(root_eid >= family->first_eid
                           /*&& root_eid < family->next_eid*/);
  /* ROOT_RRPATH must not be within another branch of the family. */

  /* Register the branch */
  APR_ARRAY_PUSH(family->branch_siblings, void *) = branch_sibling;

  return branch_sibling;
}

/* Return the branch sibling definition with branch id BID in FAMILY.
 *
 * Return NULL if not found.
 */
static svn_branch_sibling_t *
family_find_branch_sibling(svn_branch_family_t *family,
                           int bid)
{
  int i;

  for (i = 0; i < family->branch_siblings->nelts; i++)
    {
      svn_branch_sibling_t *this
        = APR_ARRAY_IDX(family->branch_siblings, i, void *);

      if (this->bid == bid)
        return this;
    }
  return NULL;
}

/* Return an existing (if found) or new (otherwise) branch sibling
 * definition object with id BID and root-eid ROOT_EID in FAMILY.
 */
static svn_branch_sibling_t *
family_find_or_create_branch_sibling(svn_branch_family_t *family,
                                     int bid,
                                     int root_eid)
{
  svn_branch_sibling_t *sibling = family_find_branch_sibling(family, bid);

  if (!sibling)
    {
      sibling = family_create_branch_sibling(family, bid, root_eid);
    }

  SVN_ERR_ASSERT_NO_RETURN(sibling->root_eid == root_eid);
  return sibling;
}

svn_branch_sibling_t *
svn_branch_family_add_new_branch_sibling(svn_branch_family_t *family,
                                         int root_eid)
{
  int bid = family->next_bid++;
  svn_branch_sibling_t *branch_sibling
    = family_create_branch_sibling(family, bid, root_eid);

  return branch_sibling;
}

apr_array_header_t *
svn_branch_family_get_children(svn_branch_family_t *family,
                               apr_pool_t *result_pool)
{
  return family->sub_families;
}

apr_array_header_t *
svn_branch_family_get_branch_instances(
                                svn_branch_revision_root_t *rev_root,
                                svn_branch_family_t *family,
                                apr_pool_t *result_pool)
{
  apr_array_header_t *rev_branch_instances = rev_root->branch_instances;
  apr_array_header_t *fam_branch_instances
    = apr_array_make(result_pool, 0, sizeof(void *));
  int i;

  for (i = 0; i < rev_branch_instances->nelts; i++)
    {
      svn_branch_instance_t *branch
        = APR_ARRAY_IDX(rev_branch_instances, i, svn_branch_instance_t *);

      if (branch->sibling_defn->family == family)
        APR_ARRAY_PUSH(fam_branch_instances, void *) = branch;
    }

  return fam_branch_instances;
}

svn_branch_sibling_t *
svn_branch_sibling_create(svn_branch_family_t *family,
                             int bid,
                             int root_eid,
                             apr_pool_t *result_pool)
{
  svn_branch_sibling_t *b = apr_pcalloc(result_pool, sizeof(*b));

  SVN_ERR_ASSERT_NO_RETURN(bid >= family->first_bid
                           && bid < family->next_bid);
  SVN_ERR_ASSERT_NO_RETURN(root_eid >= family->first_eid
                           && root_eid < family->next_eid);

  b->family = family;
  b->bid = bid;
  b->root_eid = root_eid;
  return b;
}

svn_branch_instance_t *
svn_branch_instance_create(svn_branch_sibling_t *branch_sibling,
                           svn_branch_revision_root_t *rev_root,
                           const char *branch_root_rrpath,
                           apr_pool_t *result_pool)
{
  svn_branch_instance_t *b = apr_pcalloc(result_pool, sizeof(*b));

  b->sibling_defn = branch_sibling;
  b->rev_root = rev_root;
  b->e_map = apr_hash_make(result_pool);
  b->branch_root_rrpath = apr_pstrdup(result_pool, branch_root_rrpath);
  return b;
}

svn_branch_el_rev_id_t *
svn_branch_el_rev_id_create(svn_branch_instance_t *branch,
                            int eid,
                            svn_revnum_t rev,
                            apr_pool_t *result_pool)
{
  svn_branch_el_rev_id_t *id = apr_palloc(result_pool, sizeof(*id));

  id->branch = branch;
  id->eid = eid;
  id->rev = rev;
  return id;
}

svn_branch_el_rev_content_t *
svn_branch_el_rev_content_create(svn_editor3_eid_t parent_eid,
                                 const char *name,
                                 const svn_editor3_node_content_t *node_content,
                                 apr_pool_t *result_pool)
{
  svn_branch_el_rev_content_t *content
     = apr_palloc(result_pool, sizeof(*content));

  content->parent_eid = parent_eid;
  content->name = apr_pstrdup(result_pool, name);
  content->content = svn_editor3_node_content_dup(node_content, result_pool);
  return content;
}

svn_branch_el_rev_content_t *
svn_branch_el_rev_content_dup(const svn_branch_el_rev_content_t *old,
                              apr_pool_t *result_pool)
{
  svn_branch_el_rev_content_t *content
     = apr_pmemdup(result_pool, old, sizeof(*content));

  content->name = apr_pstrdup(result_pool, old->name);
  content->content = svn_editor3_node_content_dup(old->content, result_pool);
  return content;
}

svn_boolean_t
svn_branch_el_rev_content_equal(const svn_branch_el_rev_content_t *content_left,
                                const svn_branch_el_rev_content_t *content_right,
                                apr_pool_t *scratch_pool)
{
  if (!content_left && !content_right)
    {
      return TRUE;
    }
  else if (!content_left || !content_right)
    {
      return FALSE;
    }

  if (content_left->parent_eid != content_right->parent_eid)
    {
      return FALSE;
    }
  if (strcmp(content_left->name, content_right->name) != 0)
    {
      return FALSE;
    }
  if (! svn_editor3_node_content_equal(content_left->content,
                                       content_right->content,
                                       scratch_pool))
    {
      return FALSE;
    }

  return TRUE;
}


/*
 * ========================================================================
 * Branch mappings
 * ========================================================================
 */

const char *
svn_branch_get_root_rrpath(const svn_branch_instance_t *branch)
{
  const char *root_rrpath = branch->branch_root_rrpath;

  SVN_ERR_ASSERT_NO_RETURN(root_rrpath);
  return root_rrpath;
}

/* Validate that NODE is suitable for a mapping of BRANCH:EID.
 * NODE->content may be null.
 */
static void
branch_map_node_validate(const svn_branch_instance_t *branch,
                         int eid,
                         const svn_branch_el_rev_content_t *node)
{
  SVN_ERR_ASSERT_NO_RETURN(node);

  /* Parent EID must be valid, or -1 iff EID is the branch root. */
  SVN_ERR_ASSERT_NO_RETURN(
    (eid == branch->sibling_defn->root_eid)
    ? (node->parent_eid == -1)
    : (node->parent_eid >= branch->sibling_defn->family->first_eid
       && node->parent_eid < branch->sibling_defn->family->next_eid));

  /* Node name must be given, and empty iff EID is the branch root. */
  SVN_ERR_ASSERT_NO_RETURN(
    node->name
    && (eid == branch->sibling_defn->root_eid) == (*node->name == '\0'));

  /* Content, if specified, must be in full or by reference. */
  if (node->content)
    SVN_ERR_ASSERT_NO_RETURN(node->content
                             && ((SVN_IS_VALID_REVNUM(node->content->ref.rev)
                                  && node->content->ref.relpath)
                                 || (node->content->kind != svn_node_unknown
                                     && node->content->kind != svn_node_none)));
}

svn_branch_el_rev_content_t *
svn_branch_map_get(const svn_branch_instance_t *branch,
                   int eid)
{
  svn_branch_el_rev_content_t *node;

  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->sibling_defn->family->first_eid
                           && eid < branch->sibling_defn->family->next_eid);

  node = apr_hash_get(branch->e_map, &eid, sizeof(eid));

  if (node)
    branch_map_node_validate(branch, eid, node);
  return node;
}

/* In BRANCH, set element EID's node (parent, name, content) to NODE.
 *
 * If NODE is null, delete element EID. Otherwise, NODE->content may be
 * null meaning it is unknown.
 *
 * Assume NODE is already allocated with sufficient lifetime.
 */
static void
branch_map_set(svn_branch_instance_t *branch,
               int eid,
               svn_branch_el_rev_content_t *node)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->e_map);
  int *eid_p = apr_pmemdup(map_pool, &eid, sizeof(eid));

  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->sibling_defn->family->first_eid
                           && eid < branch->sibling_defn->family->next_eid);
  if (node)
    branch_map_node_validate(branch, eid, node);

  apr_hash_set(branch->e_map, eid_p, sizeof(*eid_p), node);
}

void
svn_branch_map_delete(svn_branch_instance_t *branch,
                      int eid)
{
  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->sibling_defn->family->first_eid
                           && eid < branch->sibling_defn->family->next_eid);

  branch_map_set(branch, eid, NULL);
}

void
svn_branch_map_update(svn_branch_instance_t *branch,
                      int eid,
                      svn_editor3_eid_t new_parent_eid,
                      const char *new_name,
                      const svn_editor3_node_content_t *new_content)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->e_map);
  svn_branch_el_rev_content_t *node
    = svn_branch_el_rev_content_create(new_parent_eid, new_name, new_content,
                                       map_pool);

  /* EID must be a valid element id of the branch family */
  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->sibling_defn->family->first_eid
                           && eid < branch->sibling_defn->family->next_eid);
  /* NEW_CONTENT must be specified, either in full or by reference */
  SVN_ERR_ASSERT_NO_RETURN(new_content);

  /* We don't expect to be called more than once per eid. */
  /*SVN_ERR_ASSERT_NO_RETURN(branch_map_get(branch, eid) == NULL); ### hmm, no! */

  /* Insert the new version */
  branch_map_set(branch, eid, node);
}

/* Set or change the EID:element mapping for EID in BRANCH to reflect a
 * subbranch root node. This node has no content in this branch; the
 * corresponding element of the subbranch will define its content.
 *
 * Duplicate NEW_NAME into the branch mapping's pool.
 */
static void
branch_map_update_as_subbranch_root(svn_branch_instance_t *branch,
                                    int eid,
                                    svn_editor3_eid_t new_parent_eid,
                                    const char *new_name)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->e_map);
  svn_branch_el_rev_content_t *node
    = svn_branch_el_rev_content_create(new_parent_eid, new_name, NULL /*content*/,
                                       map_pool);

  /* EID must be a valid element id of the branch family */
  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->sibling_defn->family->first_eid
                           && eid < branch->sibling_defn->family->next_eid);
  branch_map_node_validate(branch, eid, node);

  /* We don't expect to be called more than once per eid. */
  /*SVN_ERR_ASSERT_NO_RETURN(branch_map_get(branch, eid) == NULL); ### hmm, no! */

  /* Insert the new version */
  branch_map_set(branch, eid, node);
}

void
svn_branch_map_purge_orphans(svn_branch_instance_t *branch,
                             apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_boolean_t changed;

  do
    {
      changed = FALSE;

      for (hi = apr_hash_first(scratch_pool, branch->e_map);
           hi; hi = apr_hash_next(hi))
        {
          int this_eid = *(const int *)apr_hash_this_key(hi);
          svn_branch_el_rev_content_t *this_node = apr_hash_this_val(hi);

          if (this_node->parent_eid != -1
              && ! svn_branch_map_get(branch, this_node->parent_eid))
            {
              SVN_DBG(("purge orphan: e%d", this_eid));
              svn_branch_map_delete(branch, this_eid);
              changed = TRUE;
            }
        }
    }
  while (changed);
}

const char *
svn_branch_get_path_by_eid(const svn_branch_instance_t *branch,
                           int eid,
                           apr_pool_t *result_pool)
{
  const char *path = "";
  svn_branch_el_rev_content_t *node;

  SVN_ERR_ASSERT_NO_RETURN(eid >= branch->sibling_defn->family->first_eid
                           && eid < branch->sibling_defn->family->next_eid);

  for (; eid != branch->sibling_defn->root_eid; eid = node->parent_eid)
    {
      node = svn_branch_map_get(branch, eid);
      if (! node)
        return NULL;
      path = svn_relpath_join(node->name, path, result_pool);
    }
  SVN_ERR_ASSERT_NO_RETURN(eid == branch->sibling_defn->root_eid);
  return path;
}

const char *
svn_branch_get_rrpath_by_eid(const svn_branch_instance_t *branch,
                             int eid,
                             apr_pool_t *result_pool)
{
  const char *path = svn_branch_get_path_by_eid(branch, eid, result_pool);
  const char *rrpath = NULL;

  if (path)
    {
      rrpath = svn_relpath_join(svn_branch_get_root_rrpath(branch),
                                path, result_pool);
    }
  return rrpath;
}

int
svn_branch_get_eid_by_path(const svn_branch_instance_t *branch,
                           const char *path,
                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  /* ### This is a crude, linear search */
  for (hi = apr_hash_first(scratch_pool, branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = *(const int *)apr_hash_this_key(hi);
      const char *this_path = svn_branch_get_path_by_eid(branch, eid,
                                                         scratch_pool);

      if (! this_path)
        {
          /* Mapping is not complete; this element is in effect not present. */
          continue;
        }
      if (strcmp(path, this_path) == 0)
        {
          return eid;
        }
    }

  return -1;
}

int
svn_branch_get_eid_by_rrpath(svn_branch_instance_t *branch,
                             const char *rrpath,
                             apr_pool_t *scratch_pool)
{
  const char *path = svn_relpath_skip_ancestor(svn_branch_get_root_rrpath(branch),
                                               rrpath);
  int eid = -1;

  if (path)
    {
      eid = svn_branch_get_eid_by_path(branch, path, scratch_pool);
    }
  return eid;
}

/* Get an element's content (props, text, ...) in full or by reference.
 */
static svn_error_t *
copy_content_from(svn_editor3_node_content_t **content_p,
                  svn_branch_instance_t *from_branch,
                  int from_eid,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_branch_el_rev_content_t *old_el = svn_branch_map_get(from_branch, from_eid);
  svn_editor3_node_content_t *content = old_el->content;

  /* If content is unknown, then presumably this is a committed rev and
     so we can provide a reference to the committed content. */
  if (! content)
    {
      svn_editor3_peg_path_t peg;

      SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(from_branch->rev_root->rev));
      peg.rev = from_branch->rev_root->rev;
      peg.relpath = svn_branch_get_rrpath_by_eid(from_branch, from_eid,
                                                 scratch_pool);
      content = svn_editor3_node_content_create_ref(peg, result_pool);
    }
  *content_p = content;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_map_delete_children(svn_branch_instance_t *branch,
                               int eid,
                               apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_eid = *(const int *)apr_hash_this_key(hi);
      svn_branch_el_rev_content_t *this_node = apr_hash_this_val(hi);

      if (this_node->parent_eid == eid)
        {
          /* Recurse. (We don't try to check whether it's a directory node,
             as we might not have the node kind in the map.) */
          SVN_ERR(svn_branch_map_delete_children(branch, this_eid,
                                             scratch_pool));

          /* Delete this immediate child. */
          svn_branch_map_delete(branch, this_eid);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_map_copy_children(svn_branch_instance_t *from_branch,
                             int from_parent_eid,
                             svn_branch_instance_t *to_branch,
                             int to_parent_eid,
                             apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  /* The 'from' and 'to' nodes must exist. */
  SVN_ERR_ASSERT(svn_branch_map_get(from_branch, from_parent_eid));
  SVN_ERR_ASSERT(svn_branch_map_get(to_branch, to_parent_eid));

  /* Process the immediate children of FROM_PARENT_EID. */
  for (hi = apr_hash_first(scratch_pool, from_branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_from_eid = *(const int *)apr_hash_this_key(hi);
      svn_branch_el_rev_content_t *from_node = apr_hash_this_val(hi);

      if (from_node->parent_eid == from_parent_eid)
        {
          int new_eid
            = svn_branch_family_add_new_element(to_branch->sibling_defn->family);

          svn_branch_map_update(to_branch, new_eid,
                            to_parent_eid, from_node->name,
                            from_node->content);

          /* Recurse. (We don't try to check whether it's a directory node,
             as we might not have the node kind in the map.) */
          SVN_ERR(svn_branch_map_copy_children(from_branch, this_from_eid,
                                               to_branch, new_eid,
                                               scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_map_branch_children(svn_branch_instance_t *from_branch,
                               int from_parent_eid,
                               svn_branch_instance_t *to_branch,
                               int to_parent_eid,
                               apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(from_branch->sibling_defn->family->fid
                 == to_branch->sibling_defn->family->fid);
  SVN_ERR_ASSERT(from_branch->sibling_defn->bid != to_branch->sibling_defn->bid);

  /* The 'from' and 'to' nodes must exist. */
  SVN_ERR_ASSERT(svn_branch_map_get(from_branch, from_parent_eid));
  SVN_ERR_ASSERT(svn_branch_map_get(to_branch, to_parent_eid));

  /* Process the immediate children of FROM_PARENT_EID. */
  for (hi = apr_hash_first(scratch_pool, from_branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_eid = *(const int *)apr_hash_this_key(hi);
      svn_branch_el_rev_content_t *from_node = svn_branch_map_get(from_branch,
                                                                  this_eid);

      if (from_node->parent_eid == from_parent_eid)
        {
          svn_editor3_node_content_t *this_content;

          SVN_ERR(copy_content_from(&this_content, from_branch, this_eid,
                                    scratch_pool, scratch_pool));
          svn_branch_map_update(to_branch, this_eid,
                                from_node->parent_eid, from_node->name,
                                this_content);

          /* Recurse. (We don't try to check whether it's a directory node,
             as we might not have the node kind in the map.) */
          SVN_ERR(svn_branch_map_branch_children(from_branch, this_eid,
                                                 to_branch, this_eid,
                                                 scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* Return true iff CHILD_FAMILY is an immediate child of PARENT_FAMILY. */
static svn_boolean_t
family_is_child(svn_branch_family_t *parent_family,
                svn_branch_family_t *child_family)
{
  int i;

  for (i = 0; i < parent_family->sub_families->nelts; i++)
    {
      svn_branch_family_t *sub_family
        = APR_ARRAY_IDX(parent_family->sub_families, i, void *);

      if (sub_family == child_family)
        return TRUE;
    }
  return FALSE;
}

/* Return an array of pointers to the branch instances that are immediate
 * sub-branches of BRANCH at or below EID.
 */
static apr_array_header_t *
branch_get_sub_branches(const svn_branch_instance_t *branch,
                        int eid,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_branch_family_t *family = branch->sibling_defn->family;
  const char *top_rrpath = svn_branch_get_rrpath_by_eid(branch, eid,
                                                        scratch_pool);
  apr_array_header_t *sub_branches = apr_array_make(result_pool, 0,
                                                    sizeof(void *));
  int i;

  for (i = 0; i < branch->rev_root->branch_instances->nelts; i++)
    {
      svn_branch_instance_t *sub_branch
        = APR_ARRAY_IDX(branch->rev_root->branch_instances, i, void *);
      svn_branch_family_t *sub_branch_family = sub_branch->sibling_defn->family;
      const char *sub_branch_root_rrpath
        = svn_branch_get_root_rrpath(sub_branch);

      /* Is it an immediate child at or below EID? */
      if (family_is_child(family, sub_branch_family)
          && svn_relpath_skip_ancestor(top_rrpath, sub_branch_root_rrpath))
        {
          APR_ARRAY_PUSH(sub_branches, void *) = sub_branch;
        }
    }
  return sub_branches;
}

apr_array_header_t *
svn_branch_get_all_sub_branches(const svn_branch_instance_t *branch,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  return branch_get_sub_branches(branch, branch->sibling_defn->root_eid,
                                 result_pool, scratch_pool);
}

/* Delete the branch instance BRANCH by removing the record of it from its
 * revision-root.
 */
static svn_error_t *
branch_instance_delete(svn_branch_instance_t *branch,
                       apr_pool_t *scratch_pool)
{
  apr_array_header_t *branch_instances = branch->rev_root->branch_instances;
  int i;

  for (i = 0; i < branch_instances->nelts; i++)
    {
      svn_branch_instance_t *b
        = APR_ARRAY_IDX(branch_instances, i, void *);

      if (b == branch)
        {
          svn_sort__array_delete(branch_instances, i, 1);
          break;
        }
    }

  return SVN_NO_ERROR;
}

/* Delete the branch instance object BRANCH and any nested branch instances,
 * recursively.
 */
static svn_error_t *
branch_instance_delete_r(svn_branch_instance_t *branch,
                         apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranches
    = svn_branch_get_all_sub_branches(branch, scratch_pool, scratch_pool);
  int i;

  /* Delete nested branch instances, recursively */
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *b = APR_ARRAY_IDX(subbranches, i, void *);

      branch_instance_delete_r(b, scratch_pool);
    }

  /* Remove the record of this branch instance */
  SVN_ERR(branch_instance_delete(branch, scratch_pool));

  return SVN_NO_ERROR;
}

svn_branch_instance_t *
svn_branch_add_new_branch_instance(svn_branch_instance_t *outer_branch,
                                   int outer_eid,
                                   svn_branch_sibling_t *branch_sibling,
                                   apr_pool_t *scratch_pool)
{
  svn_branch_family_t *family = branch_sibling->family;

  /* ### All this next bit is to get an RRPATH. Should ultimately go away. */
  const char *outer_root_rrpath = svn_branch_get_root_rrpath(outer_branch);
  const char *outer_eid_relpath
    = svn_branch_get_path_by_eid(outer_branch, outer_eid, scratch_pool);
  const char *new_root_rrpath
    = svn_relpath_join(outer_root_rrpath, outer_eid_relpath, scratch_pool);

  svn_branch_instance_t *branch_instance
    = svn_branch_instance_create(branch_sibling, outer_branch->rev_root,
                                 new_root_rrpath, family->pool);

  APR_ARRAY_PUSH(branch_instance->rev_root->branch_instances, void *)
    = branch_instance;

  return branch_instance;
}


/*
 * ========================================================================
 * Parsing and Serializing
 * ========================================================================
 */

/* Create a new branch *NEW_BRANCH that belongs to FAMILY, initialized
 * with info parsed from STREAM, allocated in RESULT_POOL.
 */
static svn_error_t *
svn_branch_instance_parse(svn_branch_instance_t **new_branch,
                          svn_branch_family_t *family,
                          svn_branch_revision_root_t *rev_root,
                          svn_stream_t *stream,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  int fid, bid, root_eid;
  svn_branch_sibling_t *branch_sibling;
  svn_branch_instance_t *branch_instance;
  char branch_root_path[100];
  const char *branch_root_rrpath;
  int eid;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(! eof);
  n = sscanf(line->data, "f%db%d: root-eid %d at %s\n",
             &fid, &bid, &root_eid, branch_root_path);
  SVN_ERR_ASSERT(n == 4);

  SVN_ERR_ASSERT(fid == family->fid);
  branch_root_rrpath
    = strcmp(branch_root_path, ".") == 0 ? "" : branch_root_path;
  branch_sibling = family_find_or_create_branch_sibling(family, bid, root_eid);
  branch_instance = svn_branch_instance_create(branch_sibling, rev_root,
                                               branch_root_rrpath, result_pool);

  for (eid = family->first_eid; eid < family->next_eid; eid++)
    {
      int this_fid, this_bid, this_eid, this_parent_eid;
      char this_name[20], this_path[100];

      SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
      SVN_ERR_ASSERT(! eof);
      n = sscanf(line->data, "f%db%de%d: %d %20s %100s\n",
                 &this_fid, &this_bid, &this_eid,
                 &this_parent_eid, this_name, this_path);
      SVN_ERR_ASSERT(n == 6);
      if (strcmp(this_path, "(null)") != 0)
        {
          const char *name = strcmp(this_name, ".") == 0 ? "" : this_name;
          const char *path = strcmp(this_path, ".") == 0 ? "" : this_path;
          const char *rrpath = svn_relpath_join(branch_root_rrpath, path,
                                                scratch_pool);
          svn_editor3_peg_path_t peg;
          svn_editor3_node_content_t *content;

          /* Specify the content by reference */
          peg.rev = rev_root->rev;
          peg.relpath = rrpath;
          content = svn_editor3_node_content_create_ref(peg, scratch_pool);

          svn_branch_map_update(branch_instance, this_eid,
                                this_parent_eid, name, content);
        }
    }

  *new_branch = branch_instance;
  return SVN_NO_ERROR;
}

/* Parse a branch family from STREAM.
 *
 * If the family is already found in REPOS, update it (assume it's from a
 * later revision), otherwise create a new one and register it in REPOS.
 *
 * Set *NEW_FAMILY to the branch family object, allocated in REPOS's pool.
 */
static svn_error_t *
svn_branch_family_parse(svn_branch_family_t **new_family,
                        int *parent_fid,
                        svn_branch_repos_t *repos,
                        svn_stream_t *stream,
                        apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  int fid, first_bid, next_bid, first_eid, next_eid;
  svn_branch_family_t *family;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);
  n = sscanf(line->data, "f%d: bids %d %d eids %d %d parent-fid %d\n",
             &fid,
             &first_bid, &next_bid, &first_eid, &next_eid,
             parent_fid);
  SVN_ERR_ASSERT(n == 6);

  family = repos_get_family_by_id(repos, fid);
  if (family)
    {
      SVN_ERR_ASSERT(first_bid == family->first_bid);
      SVN_ERR_ASSERT(next_bid >= family->next_bid);
      SVN_ERR_ASSERT(first_eid == family->first_eid);
      SVN_ERR_ASSERT(next_eid >= family->next_eid);
      family->next_bid = next_bid;
      family->next_eid = next_eid;
    }
  else
    {
      family = svn_branch_family_create(repos, fid,
                                        first_bid, next_bid,
                                        first_eid, next_eid,
                                        repos->pool);

      /* Register this family in the repos and in its parent family (if any) */
      repos_register_family(repos, family);
      if (*parent_fid >= 0)
        {
          svn_branch_family_t *parent_family
            = repos_get_family_by_id(repos, *parent_fid);

          SVN_ERR_ASSERT(parent_family);
          APR_ARRAY_PUSH(parent_family->sub_families, void *) = family;
        }
    }

  *new_family = family;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_revision_root_parse(svn_branch_revision_root_t **rev_root_p,
                               int *next_fid_p,
                               svn_branch_repos_t *repos,
                               svn_stream_t *stream,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_branch_revision_root_t *rev_root;
  svn_revnum_t rev;
  int root_fid;
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n, i;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(! eof);
  n = sscanf(line->data, "r%ld: fids %*d %d root-fid %d",
             &rev, /* 0, */ next_fid_p, &root_fid);
  SVN_ERR_ASSERT(n == 3);

  rev_root = svn_branch_revision_root_create(repos, rev, NULL /*root_branch*/,
                                             result_pool);

  /* parse the families */
  for (i = 0; i < *next_fid_p; i++)
    {
      svn_branch_family_t *family;
      int bid, parent_fid;

      SVN_ERR(svn_branch_family_parse(&family, &parent_fid, repos, stream,
                                      scratch_pool));

      /* parse the branches */
      for (bid = family->first_bid; bid < family->next_bid; ++bid)
        {
          svn_branch_instance_t *branch;

          SVN_ERR(svn_branch_instance_parse(&branch, family, rev_root, stream,
                                            family->pool, scratch_pool));
          APR_ARRAY_PUSH(branch->rev_root->branch_instances, void *) = branch;
          if (family->fid == root_fid)
            {
              branch->rev_root->root_branch = branch;
            }
        }
    }

  *rev_root_p = rev_root;
  return SVN_NO_ERROR;
}

/* Write to STREAM a parseable representation of BRANCH.
 */
static svn_error_t *
svn_branch_instance_serialize(svn_stream_t *stream,
                              svn_branch_instance_t *branch,
                              apr_pool_t *scratch_pool)
{
  svn_branch_family_t *family = branch->sibling_defn->family;
  const char *branch_root_rrpath = svn_branch_get_root_rrpath(branch);
  int eid;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "f%db%d: root-eid %d at %s\n",
                            family->fid, branch->sibling_defn->bid,
                            branch->sibling_defn->root_eid,
                            branch_root_rrpath[0] ? branch_root_rrpath : "."));

  svn_branch_map_purge_orphans(branch, scratch_pool);
  for (eid = family->first_eid; eid < family->next_eid; eid++)
    {
      svn_branch_el_rev_content_t *node = svn_branch_map_get(branch, eid);
      int parent_eid;
      const char *name;
      const char *path;

      if (node)
        {
          path = svn_branch_get_path_by_eid(branch, eid, scratch_pool);
          SVN_ERR_ASSERT(path);
          parent_eid = node->parent_eid;
          name = node->name[0] ? node->name : ".";
          path = path[0] ? path : ".";
        }
      else
        {
          /* ### TODO: instead, omit the line completely; but the
                 parser currently can't handle that. */
          parent_eid = -1;
          name = "(null)";
          path = "(null)";
        }
      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                "f%db%de%d: %d %s %s\n",
                                family->fid, branch->sibling_defn->bid, eid,
                                parent_eid, name, path));
    }
  return SVN_NO_ERROR;
}

/* Write to STREAM a parseable representation of FAMILY whose parent
 * family id is PARENT_FID. Recursively write all sub-families.
 */
static svn_error_t *
svn_branch_family_serialize(svn_stream_t *stream,
                            svn_branch_revision_root_t *rev_root,
                            svn_branch_family_t *family,
                            int parent_fid,
                            apr_pool_t *scratch_pool)
{
  int i;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "f%d: bids %d %d eids %d %d parent-fid %d\n",
                            family->fid,
                            family->first_bid, family->next_bid,
                            family->first_eid, family->next_eid,
                            parent_fid));

  for (i = 0; i < rev_root->branch_instances->nelts; i++)
    {
      svn_branch_instance_t *branch
        = APR_ARRAY_IDX(rev_root->branch_instances, i, void *);

      if (branch->sibling_defn->family == family)
        {
          SVN_ERR(svn_branch_instance_serialize(stream, branch, scratch_pool));
        }
    }

  for (i = 0; i < family->sub_families->nelts; i++)
    {
      svn_branch_family_t *f = APR_ARRAY_IDX(family->sub_families, i, void *);

      SVN_ERR(svn_branch_family_serialize(stream, rev_root, f, family->fid,
                                          scratch_pool));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_revision_root_serialize(svn_stream_t *stream,
                                   svn_branch_revision_root_t *rev_root,
                                   int next_fid,
                                   apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "r%ld: fids %d %d root-fid %d\n",
                            rev_root->rev,
                            0, next_fid,
                            rev_root->root_branch->sibling_defn->family->fid));

  SVN_ERR(svn_branch_family_serialize(
            stream, rev_root, rev_root->root_branch->sibling_defn->family,
            -1 /*parent_fid*/, scratch_pool));

  return SVN_NO_ERROR;
}


/*
 * ========================================================================
 */

void
svn_branch_find_nested_branch_element_by_rrpath(
                                svn_branch_instance_t **branch_p,
                                int *eid_p,
                                svn_branch_instance_t *root_branch,
                                const char *rrpath,
                                apr_pool_t *scratch_pool)
{
  const char *branch_root_path = svn_branch_get_root_rrpath(root_branch);
  apr_array_header_t *branch_instances;
  int i;

  if (! svn_relpath_skip_ancestor(branch_root_path, rrpath))
    {
      /* The path we're looking for is not (path-wise) in this branch. */
      *branch_p = NULL;
      if (eid_p)
        *eid_p = -1;
      return;
    }

  /* The path we're looking for is (path-wise) in this branch. See if it
     is also in a sub-branch (recursively). */
  branch_instances = svn_branch_get_all_sub_branches(root_branch,
                                                     scratch_pool, scratch_pool);
  for (i = 0; i < branch_instances->nelts; i++)
    {
      svn_branch_instance_t *this_branch
        = APR_ARRAY_IDX(branch_instances, i, void *);
      svn_branch_instance_t *sub_branch;
      int sub_branch_eid;

      svn_branch_find_nested_branch_element_by_rrpath(&sub_branch,
                                                      &sub_branch_eid,
                                                      this_branch, rrpath,
                                                      scratch_pool);
      if (sub_branch)
        {
           *branch_p = sub_branch;
           if (eid_p)
             *eid_p = sub_branch_eid;
           return;
         }
    }

  *branch_p = root_branch;
  if (eid_p)
    *eid_p = svn_branch_get_eid_by_rrpath(root_branch, rrpath, scratch_pool);
}

svn_error_t *
svn_branch_repos_find_el_rev_by_path_rev(svn_branch_el_rev_id_t **el_rev_p,
                                const char *rrpath,
                                svn_revnum_t revnum,
                                const svn_branch_repos_t *repos,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_branch_el_rev_id_t *el_rev = apr_palloc(result_pool, sizeof(*el_rev));
  const svn_branch_revision_root_t *rev_root;

  if (revnum < 0 || revnum >= repos->rev_roots->nelts)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("No such revision %ld"), revnum);

  rev_root = APR_ARRAY_IDX(repos->rev_roots, revnum, void *);
  el_rev->rev = revnum;
  svn_branch_find_nested_branch_element_by_rrpath(&el_rev->branch, &el_rev->eid,
                                                  rev_root->root_branch, rrpath,
                                                  scratch_pool);

  /* Any path must at least be within the repository root branch */
  SVN_ERR_ASSERT_NO_RETURN(el_rev->branch);
  *el_rev_p = el_rev;
  return SVN_NO_ERROR;
}


/*
 * ========================================================================
 */

svn_error_t *
svn_branch_delete_subtree_r(svn_branch_instance_t *branch,
                            int eid,
                            apr_pool_t *scratch_pool)
{
  apr_array_header_t *subbranches;
  int i;

  /* Delete any nested subbranches at or inside EID. */
  subbranches = branch_get_sub_branches(branch, eid,
                                        scratch_pool, scratch_pool);
  for (i = 0; i < subbranches->nelts; i++)
    {
      svn_branch_instance_t *subbranch
        = APR_ARRAY_IDX(subbranches, i, void *);

      /* Delete the whole subbranch (recursively) */
      SVN_ERR(branch_instance_delete_r(subbranch, scratch_pool));
    }

  /* update the element mapping in this branch */
  svn_branch_map_delete(branch, eid /* ### , since_rev? */);
  /* ### TODO: delete all elements under EID too. */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_branch_subtree_r(svn_branch_instance_t **new_branch_p,
                            svn_branch_instance_t *from_branch,
                            int from_eid,
                            svn_branch_instance_t *to_outer_branch,
                            svn_editor3_eid_t to_outer_parent_eid,
                            const char *new_name,
                            apr_pool_t *scratch_pool)
{
  int to_outer_eid;
  svn_branch_sibling_t *new_branch_def;
  svn_branch_instance_t *new_branch;

  /* FROM_BRANCH must be an immediate child branch of OUTER_BRANCH. */
  /* SVN_ERR_ASSERT(from_branch->sibling_defn->family->parent_family->fid
                 == to_outer_branch->sibling_defn->family->fid); */

  /* SVN_ERR_ASSERT(...); */

  /* assign new eid to root node (outer branch) */
  to_outer_eid
    = svn_branch_family_add_new_element(to_outer_branch->sibling_defn->family);
  branch_map_update_as_subbranch_root(to_outer_branch, to_outer_eid,
                                      to_outer_parent_eid, new_name);

  /* create new inner branch sibling & instance */
  /* ### On sub-branches, should not add new branch sibling, only instance. */
  new_branch_def
    = svn_branch_family_add_new_branch_sibling(from_branch->sibling_defn->family,
                                               from_eid);
  new_branch = svn_branch_add_new_branch_instance(to_outer_branch, to_outer_eid,
                                                  new_branch_def, scratch_pool);

  /* Initialize the new (inner) branch root element */
  {
    svn_editor3_node_content_t *old_content;

    SVN_ERR(copy_content_from(&old_content,
                              from_branch, from_eid,
                              scratch_pool, scratch_pool));
    svn_branch_map_update(new_branch, new_branch_def->root_eid,
                      -1, "", old_content);
  }

  /* Populate the rest of the new branch mapping */
  SVN_ERR(svn_branch_map_branch_children(from_branch, from_eid,
                                         new_branch, new_branch_def->root_eid,
                                         scratch_pool));

  /* branch any subbranches under FROM_BRANCH:FROM_EID */
#if 0 /* ### Later. */
  {
    apr_array_header_t *subbranches;
    int i;

    subbranches = branch_get_sub_branches(from_branch, from_eid,
                                          scratch_pool, scratch_pool);
    for (i = 0; i < subbranches->nelts; i++)
      {
        svn_branch_instance_t *subbranch
          = APR_ARRAY_IDX(subbranches, i, void *);
        int new_parent_eid /* = ### */;
        const char *new_name /* = ### */;

        /* branch this subbranch into NEW_BRANCH (recursing) */
        SVN_ERR(svn_branch_branch_subtree_r(NULL,
                                            subbranch,
                                            subbranch->sibling_defn->root_eid,
                                            new_branch, new_parent_eid, new_name,
                                            scratch_pool));
      }
  }
#endif

  if (new_branch_p)
    *new_branch_p = new_branch;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_copy_subtree_r(const svn_branch_el_rev_id_t *from_el_rev,
                          svn_branch_instance_t *to_branch,
                          svn_editor3_eid_t to_parent_eid,
                          const char *to_name,
                          apr_pool_t *scratch_pool)
{
  int to_eid;
  svn_branch_el_rev_content_t *old_content;

  /* Copy the root element */
  to_eid = svn_branch_family_add_new_element(to_branch->sibling_defn->family);
  old_content = svn_branch_map_get(from_el_rev->branch, from_el_rev->eid);

  /* ### If this element is a subbranch root, need to call
         branch_map_update_as_subbranch_root() instead. */
  svn_branch_map_update(to_branch, to_eid,
                        to_parent_eid, to_name, old_content->content);

  /* Copy the children within this branch */
  SVN_ERR(svn_branch_map_copy_children(from_el_rev->branch, from_el_rev->eid,
                                       to_branch, to_eid,
                                       scratch_pool));

  /* handle any subbranches under FROM_BRANCH:FROM_EID */
  /* ### Later. */

  SVN_DBG(("cp subtree from e%d (%d/%s) to e%d (%d/%s)",
           from_el_rev->eid, old_content->parent_eid, old_content->name,
           to_eid, to_parent_eid, to_name));
  return SVN_NO_ERROR;
}

/* Return the relative path to element EID within SUBTREE.
 *
 * Assumes the mapping is "complete" (has complete paths to SUBTREE and to EID).
 */
static const char *
element_relpath_in_subtree(const svn_branch_el_rev_id_t *subtree,
                           int eid,
                           apr_pool_t *scratch_pool)
{
  const char *subtree_path;
  const char *element_path;
  const char *relpath = NULL;

  SVN_ERR_ASSERT_NO_RETURN(
    subtree->eid >= subtree->branch->sibling_defn->family->first_eid
    && subtree->eid < subtree->branch->sibling_defn->family->next_eid);
  SVN_ERR_ASSERT_NO_RETURN(
    eid >= subtree->branch->sibling_defn->family->first_eid
    && eid < subtree->branch->sibling_defn->family->next_eid);

  subtree_path = svn_branch_get_path_by_eid(subtree->branch, subtree->eid,
                                            scratch_pool);
  element_path = svn_branch_get_path_by_eid(subtree->branch, eid,
                                            scratch_pool);

  SVN_ERR_ASSERT_NO_RETURN(subtree_path);

  if (element_path)
    relpath = svn_relpath_skip_ancestor(subtree_path, element_path);

  return relpath;
}

svn_error_t *
svn_branch_subtree_differences(apr_hash_t **diff_p,
                               svn_editor3_t *editor,
                               const svn_branch_el_rev_id_t *left,
                               const svn_branch_el_rev_id_t *right,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  apr_hash_t *diff = apr_hash_make(result_pool);
  int first_eid, next_eid;
  int e;

  /*SVN_DBG(("branch_element_differences(b%d r%ld, b%d r%ld, e%d)",
           left->branch->sibling->bid, left->rev,
           right->branch->sibling->bid, right->rev, right->eid));*/
  SVN_ERR_ASSERT(left->branch->sibling_defn->family->fid
                 == right->branch->sibling_defn->family->fid);
  SVN_ERR_ASSERT_NO_RETURN(
    left->eid >= left->branch->sibling_defn->family->first_eid
    && left->eid < left->branch->sibling_defn->family->next_eid);
  SVN_ERR_ASSERT_NO_RETURN(
    right->eid >= left->branch->sibling_defn->family->first_eid
    && right->eid < left->branch->sibling_defn->family->next_eid);

  first_eid = left->branch->sibling_defn->family->first_eid;
  next_eid = MAX(left->branch->sibling_defn->family->next_eid,
                 right->branch->sibling_defn->family->next_eid);

  for (e = first_eid; e < next_eid; e++)
    {
      svn_branch_el_rev_content_t *content_left = NULL;
      svn_branch_el_rev_content_t *content_right = NULL;

      if (e < left->branch->sibling_defn->family->next_eid
          && element_relpath_in_subtree(left, e, scratch_pool))
        {
          SVN_ERR(svn_editor3_el_rev_get(&content_left, editor,
                                         left->branch, e,
                                         result_pool, scratch_pool));
        }
      if (e < right->branch->sibling_defn->family->next_eid
          && element_relpath_in_subtree(right, e, scratch_pool))
        {
          SVN_ERR(svn_editor3_el_rev_get(&content_right, editor,
                                         right->branch, e,
                                         result_pool, scratch_pool));
        }

      if (! svn_branch_el_rev_content_equal(content_left, content_right,
                                            scratch_pool))
        {
          int *eid_stored = apr_pmemdup(result_pool, &e, sizeof(e));
          svn_branch_el_rev_content_t **contents
            = apr_palloc(result_pool, 2 * sizeof(void *));

          contents[0] = content_left;
          contents[1] = content_right;
          apr_hash_set(diff, eid_stored, sizeof(*eid_stored), contents);
        }
    }

  *diff_p = diff;
  return SVN_NO_ERROR;
}

