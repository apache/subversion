/*
 * merge3.c: 3-way merging
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

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <apr_lib.h>

#include "svn_hash.h"
#include "svn_iter.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"

#include "private/svn_subr_private.h"
#include "private/svn_branch_repos.h"
#include "private/svn_branch_nested.h"
#include "private/svn_branch_compat.h"
#include "private/svn_sorts_private.h"
#include "private/svn_client_private.h"

#include "svnmover.h"

#include "svn_private_config.h"


/* ====================================================================== */

#define is_branch_root_element(branch, eid) \
  (svn_branch__root_eid(branch) == (eid))

/* Return a string suitable for appending to a displayed element name or
 * element id to indicate that it is a subbranch root element for SUBBRANCH.
 * Return "" if SUBBRANCH is null.
 */
static const char *
branch_str(svn_branch__state_t *subbranch,
           apr_pool_t *result_pool)
{
  if (subbranch)
    return apr_psprintf(result_pool,
                      " (branch %s)",
                      svn_branch__get_id(subbranch, result_pool));
  return "";
}

/* Return a string suitable for appending to a displayed element name or
 * element id to indicate that BRANCH:EID is a subbranch root element.
 * Return "" if the element is not a subbranch root element.
 */
static const char *
subbranch_str(svn_branch__state_t *branch,
              int eid,
              apr_pool_t *result_pool)
{
  svn_branch__state_t *subbranch;

  svn_error_clear(svn_branch__get_subbranch_at_eid(branch, &subbranch, eid,
                                                   result_pool));
  return branch_str(subbranch, result_pool);
}

/* Return the longest known relative path leading to element EID in ELEMENTS.
 *
 * Set *BASE_EID_P to -1 if this path is rooted at the branch root;
 * otherwise, set *BASE_EID_P to the EID from which the path is relative,
 * In the latter case, element *BASE_EID_P is not found in ELEMENTS.
 *
 * If CYCLE_CONFLICTS is non-null, it maps each EID involved in a cycle to
 * [something]. If null, assume there are no cycles.
 *
 * If there is a cycle, set *BASE_EID_P to the EID of the nearest element
 * that is part of a cycle and return the path relative to that element.
 */
static const char *
partial_relpath(int *base_eid_p,
                svn_element__tree_t *elements,
                apr_hash_t *cycle_conflicts,
                int eid,
                apr_pool_t *result_pool)
{
  const char *s = "";
  int this_eid = eid;
  svn_element__content_t *e;

  while ((e = svn_element__tree_get(elements, this_eid))
         && (e->parent_eid != -1))
    {
      s = svn_relpath_join(e->name, s, result_pool);

      this_eid = e->parent_eid;

      /* Detect cycles */
      if (cycle_conflicts && svn_eid__hash_get(cycle_conflicts, this_eid))
        {
          /* Cycle detected */
          e = NULL;
          break;
        }
    }

  if (base_eid_p)
    {
      if (e)
        {
          /* We reached the root element */
          *base_eid_p = -1;
        }
      else
        {
          /* We came to this nonexistent or cyclic parent element */
          *base_eid_p = this_eid;
        }
    }
  return s;
}

/*  */
static svn_error_t *
display_relpath(const char **s_p,
                svn_branch__state_t *branch,
                apr_hash_t *cycle_conflicts,
                int eid,
                apr_pool_t *result_pool)
{
  svn_element__tree_t *elements;
  int base_eid;
  const char *s;

  SVN_ERR(svn_branch__state_get_elements(branch, &elements, result_pool));
  s = partial_relpath(&base_eid, elements, cycle_conflicts, eid, result_pool);

  if (base_eid == -1)
    s = apr_psprintf(result_pool, "/%s", s);
  else if (base_eid == eid)
    s = "<nil>";
  else if (the_ui_mode == UI_MODE_PATHS)
    s = svn_relpath_join("...", s, result_pool);
  else
    {
      const char *eid_str = apr_psprintf(result_pool, "<e%d>", base_eid);
      s = svn_relpath_join(eid_str, s, result_pool);
    }
  *s_p = s;
  return SVN_NO_ERROR;
}

/* Set *S_P to a string describing the identity of element EID. */
static svn_error_t *
merged_element_id_str(const char **s_p,
                      conflict_storage_t *conflict_storage,
                      int eid,
                      apr_pool_t *result_pool)
{
  const char *s_yca, *s_src, *s_tgt, *s_merged;

  SVN_ERR(display_relpath(&s_yca, conflict_storage->yca_branch, NULL,
                          eid, result_pool));
  SVN_ERR(display_relpath(&s_src, conflict_storage->src_branch, NULL,
                          eid, result_pool));
  SVN_ERR(display_relpath(&s_tgt, conflict_storage->tgt_branch, conflict_storage->cycle_conflicts,
                          eid, result_pool));
  SVN_ERR(display_relpath(&s_merged, conflict_storage->merged_branch,
                          conflict_storage->cycle_conflicts,
                          eid, result_pool));
  *s_p = apr_psprintf(result_pool,
                      "yca=%s, side1=%s, side2=%s, merged=%s",
                      s_yca, s_src, s_tgt, s_merged);
  if (the_ui_mode == UI_MODE_EIDS)
    {
      *s_p = apr_psprintf(result_pool,
                          "e%d (%s)", eid, *s_p);
    }
  return SVN_NO_ERROR;
}

/* Options to control how strict the merge is about detecting conflicts.
 *
 * The options affect cases that, depending on the user's preference, could
 * either be considered a conflict or be merged to a deterministic result.
 *
 * The set of options is flexible and may be extended in future.
 */
typedef struct merge_conflict_policy_t
{
  /* Whether to merge delete-vs-delete */
  svn_boolean_t merge_double_delete;
  /* Whether to merge add-vs-add (with same parent/name/payload) */
  svn_boolean_t merge_double_add;
  /* Whether to merge reparent-vs-reparent (with same parent) */
  svn_boolean_t merge_double_reparent;
  /* Whether to merge rename-vs-rename (with same name) */
  svn_boolean_t merge_double_rename;
  /* Whether to merge modify-vs-modify (with same payload) */
  svn_boolean_t merge_double_modify;
  /* Possible additional controls: */
  /* merge (parent, name, props, text) independently or as a group */
  /* merge (parent, name) independently or as a group */
  /* merge (props, text) independently or as a group */
} merge_conflict_policy_t;

/* An element-merge conflict description.
 */
typedef struct element_merge3_conflict_t
{
  svn_element__content_t *yca;
  svn_element__content_t *side1;
  svn_element__content_t *side2;
} element_merge3_conflict_t;

static element_merge3_conflict_t *
element_merge3_conflict_create(svn_element__content_t *yca,
                               svn_element__content_t *side1,
                               svn_element__content_t *side2,
                               apr_pool_t *result_pool)
{
  element_merge3_conflict_t *c = apr_pcalloc(result_pool, sizeof(*c));

  c->yca = yca ? svn_element__content_dup(yca, result_pool) : NULL;
  c->side1 = side1 ? svn_element__content_dup(side1, result_pool) : NULL;
  c->side2 = side2 ? svn_element__content_dup(side2, result_pool) : NULL;
  return c;
}

static element_merge3_conflict_t *
element_merge3_conflict_dup(element_merge3_conflict_t *old_conflict,
                            apr_pool_t *result_pool)
{
  return element_merge3_conflict_create(old_conflict->yca,
                                        old_conflict->side1,
                                        old_conflict->side2, result_pool);
}

static svn_error_t *
element_merge3_conflict_str(const char **s_p,
                            conflict_storage_t *conflict_storage,
                            element_merge3_conflict_t *c,
                            int eid,
                            apr_pool_t *result_pool)
{
  const char *id_str;

  SVN_ERR(merged_element_id_str(&id_str, conflict_storage,
                                eid, result_pool));
  *s_p = apr_psprintf(result_pool,
                      "element-merge conflict:\n"
                      "    %s",
                      id_str);
  return SVN_NO_ERROR;
}

/* A name-clash conflict description.
 */
typedef struct name_clash_conflict_t
{
  int parent_eid;
  const char *name;
  /* All EIDs that conflict with each other: hash of (eid -> irrelevant). */
  apr_hash_t *elements;
} name_clash_conflict_t;

static name_clash_conflict_t *
name_clash_conflict_create(int parent_eid,
                           const char *name,
                           apr_pool_t *result_pool)
{
  name_clash_conflict_t *c = apr_pcalloc(result_pool, sizeof(*c));

  c->parent_eid = parent_eid;
  c->name = apr_pstrdup(result_pool, name);
  c->elements = apr_hash_make(result_pool);
  return c;
}

static svn_error_t *
name_clash_conflict_str(const char **s_p,
                        conflict_storage_t *conflict_storage,
                        name_clash_conflict_t *c,
                        apr_pool_t *result_pool)
{
  apr_hash_index_t *hi2;
  const char *s = "name-clash conflict: elements";

  for (hi2 = apr_hash_first(result_pool, c->elements);
       hi2; hi2 = apr_hash_next(hi2))
    {
      int eid = svn_eid__hash_this_key(hi2);
      const char *id_str;

      SVN_ERR(merged_element_id_str(&id_str, conflict_storage,
                                    eid, result_pool));
      s = apr_psprintf(result_pool,
                       "%s\n"
                       "    %s",
                       s, id_str);
    }
  *s_p = s;
  return SVN_NO_ERROR;
}

/* A cycle conflict description.
 */
typedef struct cycle_conflict_t
{
  /* All EIDs that conflict with each other: hash of (eid -> irrelevant). */
  apr_hash_t *elements;
} cycle_conflict_t;

static cycle_conflict_t *
cycle_conflict_create(apr_pool_t *result_pool)
{
  cycle_conflict_t *c = apr_pcalloc(result_pool, sizeof(*c));

  c->elements = apr_hash_make(result_pool);
  return c;
}

static svn_error_t *
cycle_conflict_str(const char **s_p,
                   conflict_storage_t *conflict_storage,
                   cycle_conflict_t *c,
                   int eid,
                   apr_pool_t *result_pool)
{
  svn_element__content_t *element = svn_eid__hash_get(c->elements, eid);
  const char *s
    = apr_psprintf(result_pool, "element '%s' has cyclic parentage",
                   element->name);
  int this_eid = eid;

  do
    {
      const char *id_str;

      SVN_ERR(merged_element_id_str(&id_str, conflict_storage,
                                    this_eid, result_pool));
      s = apr_psprintf(result_pool,
                       "%s\n"
                       "    %s",
                       s, id_str);
      element = svn_eid__hash_get(c->elements, this_eid);
      this_eid = element->parent_eid;
    }
  while (this_eid != eid);

  *s_p = s;
  return SVN_NO_ERROR;
}

/* An orphan conflict description.
 */
typedef struct orphan_conflict_t
{
  svn_element__content_t *element;
} orphan_conflict_t;

static orphan_conflict_t *
orphan_conflict_create(svn_element__content_t *element,
                       apr_pool_t *result_pool)
{
  orphan_conflict_t *c = apr_pcalloc(result_pool, sizeof(*c));

  c->element = svn_element__content_dup(element, result_pool);
  return c;
}

static svn_error_t *
orphan_conflict_str(const char **s_p,
                    conflict_storage_t *conflict_storage,
                    orphan_conflict_t *c,
                    int eid,
                    apr_pool_t *result_pool)
{
  const char *id_str;
  const char *parent_id_str;

  SVN_ERR(merged_element_id_str(&id_str, conflict_storage,
                                eid, result_pool));
  SVN_ERR(merged_element_id_str(&parent_id_str, conflict_storage,
                                c->element->parent_eid, result_pool));
  *s_p = apr_psprintf(result_pool,
                      "orphan conflict: parent (%s) does not exist "
                      "for the following child:\n"
                      "    %s",
                      parent_id_str, id_str);
  return SVN_NO_ERROR;
}

/*  */
static conflict_storage_t *
conflict_storage_create(apr_pool_t *result_pool)
{
  conflict_storage_t *c = apr_pcalloc(result_pool, sizeof(*c));

  return c;
}

svn_error_t *
svnmover_display_conflicts(conflict_storage_t *conflict_storage,
                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  svnmover_notify(_("Conflicts:"));

  for (hi = apr_hash_first(scratch_pool,
                           conflict_storage->element_merge_conflicts);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      element_merge3_conflict_t *c = apr_hash_this_val(hi);
      const char *id_string = apr_psprintf(scratch_pool, "e%d", eid);
      const char *c_str;

      SVN_ERR(element_merge3_conflict_str(&c_str, conflict_storage,
                                          c, eid, scratch_pool));
      if (the_ui_mode == UI_MODE_EIDS)
        {
          svnmover_notify("  %s: %s", id_string, c_str);
        }
      else
        {
          svnmover_notify("  %s", c_str);
        }
    }
  for (hi = apr_hash_first(scratch_pool,
                           conflict_storage->name_clash_conflicts);
       hi; hi = apr_hash_next(hi))
    {
      const char *id_string = apr_hash_this_key(hi);
      name_clash_conflict_t *c = apr_hash_this_val(hi);
      const char *c_str;

      SVN_ERR(name_clash_conflict_str(&c_str, conflict_storage,
                                      c, scratch_pool));
      if (the_ui_mode == UI_MODE_EIDS)
        {
          svnmover_notify("  %s: %s", id_string, c_str);
        }
      else
        {
          svnmover_notify("  %s", c_str);
        }
    }
  for (hi = apr_hash_first(scratch_pool,
                           conflict_storage->cycle_conflicts);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      cycle_conflict_t *c = apr_hash_this_val(hi);
      const char *id_string = apr_psprintf(scratch_pool, "e%d", eid);
      const char *c_str;

      SVN_ERR(cycle_conflict_str(&c_str, conflict_storage,
                                 c, eid, scratch_pool));
      if (the_ui_mode == UI_MODE_EIDS)
        {
          svnmover_notify("  %s: %s", id_string, c_str);
        }
      else
        {
          svnmover_notify("  %s", c_str);
        }
    }
  for (hi = apr_hash_first(scratch_pool,
                           conflict_storage->orphan_conflicts);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      orphan_conflict_t *c = apr_hash_this_val(hi);
      const char *id_string = apr_psprintf(scratch_pool, "e%d", eid);
      const char *c_str;

      SVN_ERR(orphan_conflict_str(&c_str, conflict_storage,
                                  c, eid, scratch_pool));
      if (the_ui_mode == UI_MODE_EIDS)
        {
          svnmover_notify("  %s: %s", id_string, c_str);
        }
      else
        {
          svnmover_notify("  %s", c_str);
        }
    }

  svnmover_notify(_("Summary of conflicts:\n"
                    "  %d element-merge conflicts\n"
                    "  %d name-clash conflicts\n"
                    "  %d cycle conflicts\n"
                    "  %d orphan conflicts\n"),
                  apr_hash_count(conflict_storage->element_merge_conflicts),
                  apr_hash_count(conflict_storage->name_clash_conflicts),
                  apr_hash_count(conflict_storage->cycle_conflicts),
                  apr_hash_count(conflict_storage->orphan_conflicts));
  return SVN_NO_ERROR;
}

enum conflict_kind_t { conflict_kind_element_merge,
                       conflict_kind_clash,
                       conflict_kind_cycle,
                       conflict_kind_orphan };

/*  */
typedef struct conflict_object_t
{
  enum conflict_kind_t conflict_kind;
  apr_hash_t *conflicts;
  const void *key;
} conflict_object_t;

/*  */
static conflict_object_t *
conflict_object_create(enum conflict_kind_t conflict_kind,
                       apr_hash_t *conflicts,
                       const void *key,
                       apr_pool_t *result_pool)
{
  conflict_object_t *c = apr_pcalloc(result_pool, sizeof(*c));

  c->conflict_kind = conflict_kind;
  c->conflicts = conflicts;
  c->key = (conflict_kind == conflict_kind_clash)
             ? apr_pstrdup(result_pool, key)
             : apr_pmemdup(result_pool, key, sizeof(int));
  return c;
}

static svn_error_t *
find_conflict(conflict_object_t **conflict_p,
              conflict_storage_t *conflicts,
              const char *id_string,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  *conflict_p = NULL;

  if (id_string[0] == 'e')
    {
      int which_eid = atoi(id_string + 1);

      if (svn_eid__hash_get(conflicts->element_merge_conflicts, which_eid))
        {
          *conflict_p
            = conflict_object_create(conflict_kind_element_merge,
                                     conflicts->element_merge_conflicts,
                                     &which_eid, result_pool);
        }
      if (svn_eid__hash_get(conflicts->cycle_conflicts, which_eid))
        {
          *conflict_p
            = conflict_object_create(conflict_kind_cycle,
                                     conflicts->cycle_conflicts,
                                     &which_eid, result_pool);
        }
      if (svn_eid__hash_get(conflicts->orphan_conflicts, which_eid))
        {
          *conflict_p
            = conflict_object_create(conflict_kind_orphan,
                                     conflicts->orphan_conflicts,
                                     &which_eid, result_pool);
        }
    }
  else
    {
      if (svn_hash_gets(conflicts->name_clash_conflicts, id_string))
        {
          *conflict_p
            = conflict_object_create(conflict_kind_clash,
                                     conflicts->name_clash_conflicts,
                                     id_string, result_pool);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svnmover_conflict_resolved(conflict_storage_t *conflicts,
                           const char *id_string,
                           apr_pool_t *scratch_pool)
{
  conflict_object_t *conflict;

  SVN_ERR(find_conflict(&conflict, conflicts, id_string,
                        scratch_pool, scratch_pool));
  if (! conflict)
    {
      return svn_error_createf(SVN_BRANCH__ERR, NULL,
                               _("Conflict '%s' not found"), id_string);
    }

  if (conflict->conflict_kind == conflict_kind_clash)
    {
      svn_hash_sets(conflict->conflicts, conflict->key, NULL);
    }
  else
    {
      apr_hash_set(conflict->conflicts, conflict->key, sizeof (int), NULL);
    }
  svnmover_notify("Marked conflict '%s' as resolved", id_string);
  return SVN_NO_ERROR;
}

svn_boolean_t
svnmover_any_conflicts(const conflict_storage_t *conflicts)
{
  return conflicts
    && (apr_hash_count(conflicts->element_merge_conflicts)
        || apr_hash_count(conflicts->name_clash_conflicts)
        || apr_hash_count(conflicts->cycle_conflicts)
        || apr_hash_count(conflicts->orphan_conflicts));
}

/* Merge the payload for one element.
 *
 * If there is no conflict, set *CONFLICT_P to FALSE and *RESULT_P to the
 * merged element; otherwise set *CONFLICT_P to TRUE and *RESULT_P to NULL.
 * Note that *RESULT_P can be null, indicating a deletion.
 *
 * This handles any case where at least one of (SIDE1, SIDE2, YCA) exists.
 *
 * Allocate the result in RESULT_POOL and/or as pointers to the inputs.
 */
static void
payload_merge(svn_element__payload_t **result_p,
              svn_boolean_t *conflict_p,
              int eid,
              svn_element__payload_t *side1,
              svn_element__payload_t *side2,
              svn_element__payload_t *yca,
              const merge_conflict_policy_t *policy,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_boolean_t conflict = FALSE;
  svn_element__payload_t *result = NULL;

  if (yca && side1 && side2)
    {
      if (svn_element__payload_equal(side1, yca, scratch_pool))
        {
          result = side2;
        }
      else if (svn_element__payload_equal(side2, yca, scratch_pool))
        {
          result = side1;
        }
      else if (policy->merge_double_modify
               && svn_element__payload_equal(side1, side2, scratch_pool))
        {
          /*SVN_DBG(("e%d double modify: ... -> { ... | ... }",
                   eid));*/
          result = side1;
        }
      else
        {
          /* ### Need not conflict if can merge props and text separately. */

          /*SVN_DBG(("e%d conflict: payload: ... -> { ... | ... }",
                   eid));*/
          conflict = TRUE;
        }
    }

  *result_p = result;
  *conflict_p = conflict;
}

/* Merge the content for one element.
 *
 * If there is no conflict, set *CONFLICT_P to FALSE and *RESULT_P to the
 * merged element; otherwise set *CONFLICT_P to TRUE and *RESULT_P to NULL.
 * Note that *RESULT_P can be null, indicating a deletion.
 *
 * This handles any case where at least one of (SIDE1, SIDE2, YCA) exists.
 *
 * Allocate the result in RESULT_POOL and/or as pointers to the inputs.
 */
static void
element_merge(svn_element__content_t **result_p,
              element_merge3_conflict_t **conflict_p,
              int eid,
              svn_element__content_t *side1,
              svn_element__content_t *side2,
              svn_element__content_t *yca,
              const merge_conflict_policy_t *policy,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_boolean_t same1 = svn_element__content_equal(yca, side1, scratch_pool);
  svn_boolean_t same2 = svn_element__content_equal(yca, side2, scratch_pool);
  svn_boolean_t conflict = FALSE;
  svn_element__content_t *result = NULL;

  if (same1)
    {
      result = side2;
    }
  else if (same2)
    {
      result = side1;
    }
  else if (yca && side1 && side2)
    {
      /* All three sides are different, and all exist */
      result = apr_pmemdup(result_pool, yca, sizeof(*result));

      /* merge the parent-eid */
      if (side1->parent_eid == yca->parent_eid)
        {
          result->parent_eid = side2->parent_eid;
        }
      else if (side2->parent_eid == yca->parent_eid)
        {
          result->parent_eid = side1->parent_eid;
        }
      else if (policy->merge_double_reparent
               && side1->parent_eid == side2->parent_eid)
        {
          /*SVN_DBG(("e%d double reparent: e%d -> { e%d | e%d }",
                   eid, yca->parent_eid, side1->parent_eid, side2->parent_eid));*/
          result->parent_eid = side1->parent_eid;
        }
      else
        {
          /*SVN_DBG(("e%d conflict: parent: e%d -> { e%d | e%d }",
                   eid, yca->parent_eid, side1->parent_eid, side2->parent_eid));*/
          conflict = TRUE;
        }

      /* merge the name */
      if (strcmp(side1->name, yca->name) == 0)
        {
          result->name = side2->name;
        }
      else if (strcmp(side2->name, yca->name) == 0)
        {
          result->name = side1->name;
        }
      else if (policy->merge_double_rename
               && strcmp(side1->name, side2->name) == 0)
        {
          /*SVN_DBG(("e%d double rename: %s -> { %s | %s }",
                   eid, yca->name, side1->name, side2->name));*/
          result->name = side1->name;
        }
      else
        {
          /*SVN_DBG(("e%d conflict: name: %s -> { %s | %s }",
                   eid, yca->name, side1->name, side2->name));*/
          conflict = TRUE;
        }

      /* merge the payload */
      {
        svn_boolean_t payload_conflict;

        payload_merge(&result->payload, &payload_conflict,
                      eid, side1->payload, side2->payload, yca->payload,
                      policy, result_pool, scratch_pool);
        if (payload_conflict)
          conflict = TRUE;
      }
    }
  else if (! side1 && ! side2)
    {
      /* Double delete (as we assume at least one of YCA/SIDE1/SIDE2 exists) */
      if (policy->merge_double_delete)
        {
          /*SVN_DBG(("e%d double delete",
                   eid));*/
          result = side1;
        }
      else
        {
          /*SVN_DBG(("e%d conflict: delete vs. delete",
                   eid));*/
          conflict = TRUE;
        }
    }
  else if (side1 && side2)
    {
      /* Double add (as we already handled the case where YCA also exists) */
      /* May be allowed for equal content of a normal element (not subbranch) */
      if (policy->merge_double_add
          && !side1->payload->is_subbranch_root
          && !side2->payload->is_subbranch_root
          && svn_element__content_equal(side1, side2, scratch_pool))
        {
          /*SVN_DBG(("e%d double add",
                   eid));*/
          result = side1;
        }
      else
        {
          /*SVN_DBG(("e%d conflict: add vs. add (%s)",
                   eid,
                   svn_element_content_equal(side1, side2, scratch_pool)
                     ? "same content" : "different content"));*/
          conflict = TRUE;
        }
    }
  else
    {
      /* The remaining cases must be delete vs. modify */
      /*SVN_DBG(("e%d conflict: delete vs. modify: %d -> { %d | %d }",
               eid, !!yca, !!side1, !!side2));*/
      conflict = TRUE;
    }

  *result_p = result;
  *conflict_p
    = conflict ? element_merge3_conflict_create(yca, side1, side2,
                                                result_pool) : NULL;
}

static svn_error_t *
branch_merge_subtree_r(svn_branch__txn_t *edit_txn,
                       svn_branch__state_t *edit_branch,
                       conflict_storage_t **conflict_storage_p,
                       const svn_branch__el_rev_id_t *src,
                       const svn_branch__el_rev_id_t *tgt,
                       const svn_branch__el_rev_id_t *yca,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/* Merge the subbranch of {SRC, TGT, YCA} found at EID.
 */
static svn_error_t *
merge_subbranch(svn_branch__txn_t *edit_txn,
                svn_branch__state_t *edit_branch,
                const svn_branch__el_rev_id_t *src,
                const svn_branch__el_rev_id_t *tgt,
                const svn_branch__el_rev_id_t *yca,
                int eid,
                apr_pool_t *scratch_pool)
{
  svn_branch__state_t *src_subbranch;
  svn_branch__state_t *tgt_subbranch;
  svn_branch__state_t *yca_subbranch;
  svn_branch__el_rev_id_t *subbr_src = NULL;
  svn_branch__el_rev_id_t *subbr_tgt = NULL;
  svn_branch__el_rev_id_t *subbr_yca = NULL;

  SVN_ERR(svn_branch__get_subbranch_at_eid(src->branch, &src_subbranch,
                                           eid, scratch_pool));
  SVN_ERR(svn_branch__get_subbranch_at_eid(tgt->branch, &tgt_subbranch,
                                           eid, scratch_pool));
  SVN_ERR(svn_branch__get_subbranch_at_eid(yca->branch, &yca_subbranch,
                                           eid, scratch_pool));
  if (src_subbranch)
    subbr_src = svn_branch__el_rev_id_create(
                  src_subbranch, svn_branch__root_eid(src_subbranch),
                  src->rev, scratch_pool);
  if (tgt_subbranch)
    subbr_tgt = svn_branch__el_rev_id_create(
                  tgt_subbranch, svn_branch__root_eid(tgt_subbranch),
                  tgt->rev, scratch_pool);
  if (yca_subbranch)
    subbr_yca = svn_branch__el_rev_id_create(
                  yca_subbranch, svn_branch__root_eid(yca_subbranch),
                  yca->rev, scratch_pool);

  if (subbr_src && subbr_tgt && subbr_yca)  /* ?edit vs. ?edit */
    {
      conflict_storage_t *conflict_storage;
      const char *new_branch_id
        = svn_branch__id_nest(svn_branch__get_id(edit_branch, scratch_pool),
                              eid, scratch_pool);
      svn_branch__rev_bid_eid_t *from
        = svn_branch__rev_bid_eid_create(tgt_subbranch->txn->rev,
                                         svn_branch__get_id(tgt_subbranch,
                                                            scratch_pool),
                                         svn_branch__root_eid(tgt_subbranch),
                                         scratch_pool);
      svn_branch__state_t *edit_subbranch;

      SVN_ERR(svn_branch__txn_open_branch(edit_txn, &edit_subbranch,
                                          new_branch_id, from->eid, from,
                                          scratch_pool, scratch_pool));

      /* subbranch possibly changed in source => merge */
      SVN_ERR(branch_merge_subtree_r(edit_txn, edit_subbranch,
                                     &conflict_storage,
                                     subbr_src, subbr_tgt, subbr_yca,
                                     scratch_pool, scratch_pool));
      /* ### store this branch's conflict_storage somewhere ... */
    }
  else if (subbr_src && subbr_yca)  /* ?edit vs. delete */
    {
      /* ### possible conflict (edit vs. delete) */
    }
  else if (subbr_tgt && subbr_yca)  /* delete vs. ?edit */
    {
      /* ### possible conflict (delete vs. edit) */
    }
  else if (subbr_src && subbr_tgt)  /* double add */
    {
      /* ### conflict */
    }
  else if (subbr_src)  /* added on source branch */
    {
      const char *new_branch_id
        = svn_branch__id_nest(svn_branch__get_id(edit_branch, scratch_pool),
                              eid, scratch_pool);
      svn_branch__rev_bid_eid_t *from
        = svn_branch__rev_bid_eid_create(src_subbranch->txn->rev,
                                         svn_branch__get_id(src_subbranch,
                                                            scratch_pool),
                                         svn_branch__root_eid(src_subbranch),
                                         scratch_pool);

      SVN_ERR(svn_branch__txn_open_branch(edit_txn, NULL /*new_branch_p*/,
                                          new_branch_id, from->eid, from,
                                          scratch_pool, scratch_pool));
    }
  else if (subbr_tgt)  /* added on target branch */
    {
      const char *new_branch_id
        = svn_branch__id_nest(svn_branch__get_id(edit_branch, scratch_pool),
                              eid, scratch_pool);
      svn_branch__rev_bid_eid_t *from
        = svn_branch__rev_bid_eid_create(tgt_subbranch->txn->rev,
                                         svn_branch__get_id(tgt_subbranch,
                                                            scratch_pool),
                                         svn_branch__root_eid(tgt_subbranch),
                                         scratch_pool);

      SVN_ERR(svn_branch__txn_open_branch(edit_txn, NULL /*new_branch_p*/,
                                          new_branch_id, from->eid, from,
                                          scratch_pool, scratch_pool));
    }
  else if (subbr_yca)  /* double delete */
    {
      /* ### conflict? policy option? */
    }

  return SVN_NO_ERROR;
}

/*  */
static int
sort_compare_items_by_peid_and_name(const svn_sort__item_t *a,
                                    const svn_sort__item_t *b)
{
  svn_element__content_t *element_a = a->value;
  svn_element__content_t *element_b = b->value;

  if (element_a->parent_eid != element_b->parent_eid)
    return element_a->parent_eid - element_b->parent_eid;
  return strcmp(element_a->name, element_b->name);
}

/* Return all (key -> name_clash_conflict_t) name clash conflicts in BRANCH.
 */
static svn_error_t *
detect_clashes(apr_hash_t **clashes_p,
               svn_branch__state_t *branch,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  apr_hash_t *clashes = apr_hash_make(result_pool);
  svn_element__tree_t *elements;
  svn_eid__hash_iter_t *ei;
  int prev_eid = -1;
  svn_element__content_t *prev_element = NULL;

  SVN_ERR(svn_branch__state_get_elements(branch, &elements, scratch_pool));
  for (SVN_EID__HASH_ITER_SORTED(ei, elements->e_map,
                                 sort_compare_items_by_peid_and_name,
                                 scratch_pool))
    {
      int eid = ei->eid;
      svn_element__content_t *element = ei->val;

      if (prev_element
          && element->parent_eid == prev_element->parent_eid
          && strcmp(element->name, prev_element->name) == 0)
        {
          const char *key = apr_psprintf(result_pool, "%d/%s",
                                         element->parent_eid, element->name);
          name_clash_conflict_t *c;

          c = svn_hash_gets(clashes, key);
          if (!c)
            {
              c = name_clash_conflict_create(
                    element->parent_eid, element->name,
                    result_pool);
              svn_hash_sets(clashes, key, c);
            }
          svn_eid__hash_set(c->elements, eid, &c);
          svn_eid__hash_set(c->elements, prev_eid, &c);
        }
      prev_eid = eid;
      prev_element = element;
    }

  *clashes_p = clashes;
  return SVN_NO_ERROR;
}

/* For each element in the cycle starting at ONE_EID in EIDS_VISITED,
 * record an entry in CYCLES[this_eid] mapping to a cycle_conflict_t.
 * Each such new entry will point to the same cycle_conflict_t. This
 * cycle_conflict_t will contain the list of elements in the cycle.
 *
 * ONE_EID should identify a member of a simple cycle, not an element
 * that merely has a parent or ancestor in a simple cycle.
 */
static svn_error_t *
record_cycle(apr_hash_t *cycles,
             apr_hash_t *eids_visited,
             int one_eid,
             apr_pool_t *result_pool)
{
  cycle_conflict_t *c = cycle_conflict_create(result_pool);
  int this_eid = one_eid;

  do
    {
      svn_element__content_t *element
        = svn_eid__hash_get(eids_visited, this_eid);

      svn_eid__hash_set(cycles, this_eid, c);
      svn_eid__hash_set(c->elements, this_eid, element);

      this_eid = element->parent_eid;
      assert(this_eid != -1);
    }
  while (this_eid != one_eid);

  return SVN_NO_ERROR;
}

/* Return all (eid -> cycle_conflict_t) cycle conflicts in BRANCH.

 * ### This implementation is crude: it finds all cycles, but doesn't
 * report them minimally. It reports each element that leads to a cycle,
 * without isolating the minimal cycles nor eliminating duplicates.
 */
static svn_error_t *
detect_cycles(apr_hash_t **cycles_p,
              svn_branch__state_t *branch,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  apr_hash_t *cycles = apr_hash_make(result_pool);
  apr_hash_index_t *hi;
  svn_element__tree_t *elements;

  SVN_ERR(svn_branch__state_get_elements(branch, &elements, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, elements->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      svn_element__content_t *element = apr_hash_this_val(hi);
      apr_hash_t *eids_visited;
      int this_eid;

      /* If the element EID is already recorded as a member of a cycle,
         there's nothing more to do for it. */
      if (svn_eid__hash_get(cycles, eid))
        {
          continue;
        }

      eids_visited = apr_hash_make(scratch_pool);

      /* See if we can trace the parentage of EID back to the branch root
         without finding a cycle. If we find a cycle, store a conflict. */
      for (this_eid = eid;
           element = svn_eid__hash_get(elements->e_map, this_eid),
             element && element->parent_eid != -1;
           this_eid = element->parent_eid)
        {
          svn_eid__hash_set(eids_visited, this_eid, element);

          /* If the element EID is attached to an element of a previously
             detected cycle, then it's not interesting in itself. */
          if (svn_eid__hash_get(cycles, element->parent_eid))
            {
              break;
            }
          /* If this element's parent-EID is already in the path of EIDs
             visited from EID to the root, then we have found a cycle. */
          if (svn_eid__hash_get(eids_visited, element->parent_eid))
            {
              SVN_ERR(record_cycle(cycles, eids_visited, this_eid,
                                   result_pool));
              break;
            }
        }
    }

  *cycles_p = cycles;
  return SVN_NO_ERROR;
}

/* Return all (eid -> orphan_conflict_t) orphan conflicts in BRANCH.
 */
static svn_error_t *
detect_orphans(apr_hash_t **orphans_p,
               svn_branch__state_t *branch,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  apr_hash_t *orphans = apr_hash_make(result_pool);
  apr_hash_index_t *hi;
  svn_element__tree_t *elements;

  SVN_ERR(svn_branch__state_get_elements(branch, &elements, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, elements->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_eid__hash_this_key(hi);
      svn_element__content_t *element = apr_hash_this_val(hi);

      if (eid != elements->root_eid
          && ! svn_element__tree_get(elements, element->parent_eid))
        {
          orphan_conflict_t *c;

          c = orphan_conflict_create(element, result_pool);
          svn_eid__hash_set(orphans, eid, c);
        }
    }

  *orphans_p = orphans;
  return SVN_NO_ERROR;
}

/* Merge ...
 *
 * The elements to merge are the union of the elements in the three input
 * subtrees (SRC, TGT, YCA).
 *
 * Merge any sub-branches in the same way, recursively.
 *
 * ### TODO: Store the merge result separately, without overwriting the
 *     target input state, so that the three input states are all available
 *     for reference while resolving conflicts.
 */
static svn_error_t *
branch_merge_subtree_r(svn_branch__txn_t *edit_txn,
                       svn_branch__state_t *edit_branch,
                       conflict_storage_t **conflict_storage_p,
                       const svn_branch__el_rev_id_t *src,
                       const svn_branch__el_rev_id_t *tgt,
                       const svn_branch__el_rev_id_t *yca,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_branch__subtree_t *s_src, *s_tgt, *s_yca;
  apr_hash_t *diff_yca_src, *diff_yca_tgt;
  apr_hash_t *e_conflicts = apr_hash_make(result_pool);
  conflict_storage_t *conflict_storage = conflict_storage_create(result_pool);
  svn_element__tree_t *src_elements, *tgt_elements, *yca_elements;
  apr_hash_t *all_elements;
  svn_eid__hash_iter_t *ei;
  const merge_conflict_policy_t policy = { TRUE, TRUE, TRUE, TRUE, TRUE };
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /*SVN_DBG(("merge src: r%2ld %s e%3d",
           src->rev,
           svn_branch__get_id(src->branch, scratch_pool), src->eid));*/
  /*SVN_DBG(("merge tgt: r%2ld %s e%3d",
           tgt->rev,
           svn_branch__get_id(tgt->branch, scratch_pool), tgt->eid));*/
  /*SVN_DBG(("merge yca: r%2ld %s e%3d",
           yca->rev,
           svn_branch__get_id(yca->branch, scratch_pool), yca->eid));*/

  svnmover_notify_v("merging into branch %s",
                    edit_branch->bid);
  /*
      for (eid, diff1) in element_differences(YCA, FROM):
        diff2 = element_diff(eid, YCA, TO)
        if diff1 and diff2:
          result := element_merge(diff1, diff2)
        elif diff1:
          result := diff1.right
        # else no change
   */
  SVN_ERR(svn_branch__get_subtree(src->branch, &s_src, src->eid, scratch_pool));
  SVN_ERR(svn_branch__get_subtree(tgt->branch, &s_tgt, tgt->eid, scratch_pool));
  SVN_ERR(svn_branch__get_subtree(yca->branch, &s_yca, yca->eid, scratch_pool));

  /* ALL_ELEMENTS enumerates the elements in union of subtrees YCA,SRC,TGT. */
  all_elements = hash_overlay(s_src->tree->e_map,
                              s_tgt->tree->e_map);
  all_elements = hash_overlay(s_yca->tree->e_map,
                              all_elements);

  SVN_ERR(svn_branch__state_get_elements(src->branch, &src_elements,
                                         scratch_pool));
  SVN_ERR(svn_branch__state_get_elements(tgt->branch, &tgt_elements,
                                         scratch_pool));
  SVN_ERR(svn_branch__state_get_elements(yca->branch, &yca_elements,
                                         scratch_pool));

  /* Find the two changes for each element that is in any of the subtrees,
     even for an element that is (for example) not in YCA or SRC but has
     been moved into TGT. */
  SVN_ERR(svnmover_element_differences(&diff_yca_src,
                                       yca_elements, src_elements,
                                       all_elements,
                                       scratch_pool, scratch_pool));
  /* ### We only need to know about YCA:TGT differences for elements that
         differ in YCA:SRC, but right now we ask for all differences. */
  SVN_ERR(svnmover_element_differences(&diff_yca_tgt,
                                       yca_elements, tgt_elements,
                                       all_elements,
                                       scratch_pool, scratch_pool));

  for (SVN_EID__HASH_ITER_SORTED_BY_EID(ei, all_elements, scratch_pool))
    {
      int eid = ei->eid;
      svn_element__content_t **e_yca_src
        = svn_eid__hash_get(diff_yca_src, eid);
      svn_element__content_t **e_yca_tgt
        = svn_eid__hash_get(diff_yca_tgt, eid);
      svn_element__content_t *e_yca;
      svn_element__content_t *e_src;
      svn_element__content_t *e_tgt;
      svn_element__content_t *result;
      element_merge3_conflict_t *conflict;

      svn_pool_clear(iterpool);

      /* If an element hasn't changed in the source branch, there is
         no need to do anything with it in the target branch. We could
         use element_merge() for any case where at least one of (SRC,
         TGT, YCA) exists, but we choose to skip it when SRC == YCA. */
      if (! e_yca_src)
        {
          /* Still need to merge any subbranch linked to this element.
             There were no changes to the link element but that doesn't
             mean there were no changes to the linked branch. */
          SVN_ERR(merge_subbranch(edit_txn, edit_branch,
                                  src, tgt, yca, eid, iterpool));

          continue;
        }

      e_yca = e_yca_src[0];
      e_src = e_yca_src[1];
      e_tgt = e_yca_tgt ? e_yca_tgt[1] : e_yca_src[0];

      /* If some but not all of the three subtree-root elements are branch
         roots, then we will see the parentage of this element changing to
         or from 'no parent' in one or both sides of the merge. We want to
         ignore this part of the difference, as parentage of a subtree root
         element is by definition not part of a 'subtree', so blank it out.
         (If we merged it, it could break the single-rooted-tree invariant
         of the target branch.)
       */
      if (is_branch_root_element(src->branch, eid)
          || is_branch_root_element(tgt->branch, eid)
          || is_branch_root_element(yca->branch, eid))
        {
          e_src = svn_element__content_create(
                    e_tgt->parent_eid, e_tgt->name, e_src->payload, iterpool);
          e_yca = svn_element__content_create(
                    e_tgt->parent_eid, e_tgt->name, e_yca->payload, iterpool);
        }

      element_merge(&result, &conflict,
                    eid, e_src, e_tgt, e_yca,
                    &policy,
                    scratch_pool, scratch_pool);

      if (conflict)
        {
          svnmover_notify_v("!    e%d <conflict>", eid);
          svn_eid__hash_set(e_conflicts, eid,
                            element_merge3_conflict_dup(conflict, result_pool));
        }
      else
        {
          if (e_tgt && result)
            {
              svnmover_notify_v("M/V  e%d %s%s",
                                eid, result->name,
                                subbranch_str(tgt->branch, eid, iterpool));
            }
          else if (e_tgt)
            {
              svnmover_notify_v("D    e%d %s%s",
                                eid, e_yca->name,
                                subbranch_str(yca->branch, eid, iterpool));

              /* ### If this is a subbranch-root element being deleted, shouldn't
                 we see if there were any changes to be merged in the subbranch,
                 and raise a delete-vs-edit conflict if so? */
            }
          else if (result)
            {
              svnmover_notify_v("A    e%d %s%s",
                                eid, result->name,
                                subbranch_str(src->branch, eid, iterpool));
            }

          SVN_ERR(svn_branch__state_set_element(tgt->branch, eid,
                                                result, iterpool));

          if (result)
            {
              SVN_ERR(merge_subbranch(edit_txn, edit_branch,
                                      src, tgt, yca, eid, iterpool));
            }
        }
    }
  svn_pool_destroy(iterpool);

  conflict_storage->yca_branch = yca->branch;
  conflict_storage->src_branch = src->branch;
  conflict_storage->tgt_branch = tgt->branch;
  conflict_storage->merged_branch = tgt->branch; /* ### should be != tgt */
  conflict_storage->element_merge_conflicts = e_conflicts;
  SVN_ERR(detect_clashes(&conflict_storage->name_clash_conflicts,
                         tgt->branch,
                         result_pool, scratch_pool));
  SVN_ERR(detect_cycles(&conflict_storage->cycle_conflicts,
                        tgt->branch,
                        result_pool, scratch_pool));
  SVN_ERR(detect_orphans(&conflict_storage->orphan_conflicts,
                         tgt->branch,
                         result_pool, scratch_pool));

  svnmover_notify_v("merging into branch %s -- finished",
                    svn_branch__get_id(tgt->branch, scratch_pool));

  *conflict_storage_p = conflict_storage;
  return SVN_NO_ERROR;
}

svn_error_t *
svnmover_branch_merge(svn_branch__txn_t *edit_txn,
                      svn_branch__state_t *edit_branch,
                      conflict_storage_t **conflict_storage_p,
                      svn_branch__el_rev_id_t *src,
                      svn_branch__el_rev_id_t *tgt,
                      svn_branch__el_rev_id_t *yca,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  conflict_storage_t *conflicts;

  /*SVN_ERR(verify_exists_in_branch(from, scratch_pool));*/
  /*SVN_ERR(verify_exists_in_branch(to, scratch_pool));*/
  /*SVN_ERR(verify_exists_in_branch(yca, scratch_pool));*/
  /*SVN_ERR(verify_not_subbranch_root(from, scratch_pool));*/
  /*SVN_ERR(verify_not_subbranch_root(to, scratch_pool));*/
  /*SVN_ERR(verify_not_subbranch_root(yca, scratch_pool));*/

  SVN_ERR(branch_merge_subtree_r(edit_txn, edit_branch,
                                 &conflicts,
                                 src, tgt, yca,
                                 result_pool, scratch_pool));

  if (conflict_storage_p)
    {
      if (svnmover_any_conflicts(conflicts))
        {
          *conflict_storage_p = conflicts;
        }
      else
        {
          *conflict_storage_p = NULL;
        }
    }
  return SVN_NO_ERROR;
}

