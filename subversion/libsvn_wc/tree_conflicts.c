/*
 * tree_conflicts.c: Storage of tree conflict descriptions in the WC.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include "svn_path.h"
#include "svn_types.h"
#include "svn_pools.h"

#include "tree_conflicts.h"
#include "log.h"
#include "entries.h"

#include "private/svn_skel.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"


/* OVERVIEW
 *
 * This file handles the storage and retrieval of tree conflict descriptions
 * (svn_wc_conflict_description_t) in the WC.
 *
 * Data Format
 *
 * All tree conflicts descriptions for the current tree conflict victims in
 * one parent directory are stored in a single "tree_conflict_data" text
 * field in that parent's THIS_DIR entry.
 *
 *   tree_conflict_data: zero or more conflicts (one per victim path),
 *     separated by the SVN_WC__TREE_CONFLICT_DESC_SEPARATOR character.
 *
 *   a description entry: a fixed sequence of text fields, some of which
 *     may be empty, corresponding to the pertinent fields of
 *     svn_wc_conflict_description_t, separated by
 *     SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR.
 *
 *   a field: a string within which any separator or escape characters are
 *     escaped with the escape character SVN_WC__TREE_CONFLICT_ESCAPE_CHAR.
 *
 * Error Handling
 *
 * On reading from the WC entry, errors of malformed data are handled by
 * raising an svn_error_t, as these can occur from WC corruption. On
 * writing, errors in the internal data consistency before it is written are
 * handled more severely because any such errors must be due to a bug.
 */


/* A mapping between a string STR and an enumeration value VAL. */
typedef struct enum_mapping_t
{
  const char *str;
  int val;
} enum_mapping_t;

/* A map for svn_node_kind_t values. */
static const enum_mapping_t node_kind_map[] =
{
  { "none", svn_node_none },
  { "file", svn_node_file },
  { "dir",  svn_node_dir },
  { "",     svn_node_unknown },
  { NULL,   0 }
};

/* A map for svn_wc_operation_t values. */
static const enum_mapping_t operation_map[] =
{
  { "none",   svn_wc_operation_none },
  { "update", svn_wc_operation_update },
  { "switch", svn_wc_operation_switch },
  { "merge",  svn_wc_operation_merge },
  { NULL,     0 }
};

/* A map for svn_wc_conflict_action_t values. */
static const enum_mapping_t action_map[] =
{
  { "edited",  svn_wc_conflict_action_edit },
  { "deleted", svn_wc_conflict_action_delete },
  { "added",   svn_wc_conflict_action_add },
  { NULL,      0 }
};

/* A map for svn_wc_conflict_reason_t values. */
static const enum_mapping_t reason_map[] =
{
  { "edited",     svn_wc_conflict_reason_edited },
  { "deleted",    svn_wc_conflict_reason_deleted },
  { "missing",    svn_wc_conflict_reason_missing },
  { "obstructed", svn_wc_conflict_reason_obstructed },
  { "added",      svn_wc_conflict_reason_added },
  { NULL,         0 }
};


static svn_boolean_t
is_valid_version_info_skel(const svn_skel_t *skel)
{
  return (svn_skel__list_length(skel) == 5
          && svn_skel__matches_atom(skel->children, "version")
          && skel->children->next->is_atom
          && skel->children->next->next->is_atom
          && skel->children->next->next->next->is_atom
          && skel->children->next->next->next->next->is_atom);
}


static svn_boolean_t
is_valid_conflict_skel(const svn_skel_t *skel)
{
  int i;

  if (svn_skel__list_length(skel) != 8
      || !svn_skel__matches_atom(skel->children, "conflict"))
    return FALSE;

  /* 5 atoms ... */
  skel = skel->children->next;
  for (i = 5; i--; skel = skel->next)
    if (!skel->is_atom)
      return FALSE;

  /* ... and 2 version info skels. */
  return (is_valid_version_info_skel(skel)
          && is_valid_version_info_skel(skel->next));
}

/* Parse the enumeration value in VALUE into a plain
 * 'int', using MAP to convert from strings to enumeration values.
 * In MAP, a null .str field marks the end of the map.
 */
static svn_error_t *
read_enum_field(int *result,
                const enum_mapping_t *map,
                const svn_skel_t *skel)
{
  int i;

  /* Find STR in MAP; error if not found. */
  for (i = 0; ; i++)
    {
      if (map[i].str == NULL)
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                                _("Unknown enumeration value in tree conflict "
                                  "description"));
      /* ### note: theoretically, a corrupt skel could have a long value
         ### whose prefix is one of our enumerated values, and so we'll
         ### choose that enum. fine. we'll accept these "corrupt" values. */
      if (strncmp(skel->data, map[i].str, skel->len) == 0)
        break;
    }

  *result = map[i].val;
  return SVN_NO_ERROR;
}

/* Parse the conflict info fields from SKEL into *VERSION_INFO. */
static svn_error_t *
read_node_version_info(svn_wc_conflict_version_t *version_info,
                       const svn_skel_t *skel,
                       apr_pool_t *scratch_pool,
                       apr_pool_t *result_pool)
{
  int n;

  if (!is_valid_version_info_skel(skel))
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid version info in tree conflict "
                              "description"));

  version_info->repos_url = apr_pstrmemdup(result_pool,
                                           skel->children->next->data,
                                           skel->children->next->len);
  if (*version_info->repos_url == '\0')
    version_info->repos_url = NULL;

  version_info->peg_rev =
    SVN_STR_TO_REV(apr_pstrmemdup(scratch_pool,
                                  skel->children->next->next->data,
                                  skel->children->next->next->len));

  version_info->path_in_repos =
    apr_pstrmemdup(result_pool,
                   skel->children->next->next->next->data,
                   skel->children->next->next->next->len);
  if (*version_info->path_in_repos == '\0')
    version_info->path_in_repos = NULL;

  SVN_ERR(read_enum_field(&n, node_kind_map,
                          skel->children->next->next->next->next));
  version_info->node_kind = (svn_node_kind_t)n;

  return SVN_NO_ERROR;
}

/* Parse a newly allocated svn_wc_conflict_description_t object from the
 * character string pointed to by *START. Return the result in *CONFLICT.
 * Don't read further than END. Set *START to point to the next character
 * after the description that was read.
 * DIR_PATH is the path to the WC directory whose conflicts are being read.
 * Do all allocations in pool.
 */
static svn_error_t *
read_one_tree_conflict(svn_wc_conflict_description_t **conflict,
                       const svn_skel_t *skel,
                       const char *dir_path,
                       apr_pool_t *scratch_pool,
                       apr_pool_t *result_pool)
{
  const char *victim_basename;
  svn_node_kind_t node_kind;
  svn_wc_operation_t operation;
  svn_wc_conflict_version_t *src_left_version;
  svn_wc_conflict_version_t *src_right_version;
  int n;

  if (!is_valid_conflict_skel(skel))
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid conflict info in tree conflict "
                              "description"));

  /* victim basename */
  victim_basename = apr_pstrmemdup(scratch_pool,
                                   skel->children->next->data,
                                   skel->children->next->len);
  if (victim_basename[0] == '\0')
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Empty 'victim' field in tree conflict "
                              "description"));

  /* node_kind */
  SVN_ERR(read_enum_field(&n, node_kind_map, skel->children->next->next));
  node_kind = (svn_node_kind_t)n;
  if (node_kind != svn_node_file && node_kind != svn_node_dir)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Invalid 'node_kind' field in tree conflict description"));

  /* operation */
  SVN_ERR(read_enum_field(&n, operation_map,
                          skel->children->next->next->next));
  operation = (svn_wc_operation_t)n;

  /* Construct the description object */
  src_left_version = svn_wc_conflict_version_create(NULL, NULL,
                                                    SVN_INVALID_REVNUM,
                                                    svn_node_none,
                                                    result_pool);
  src_right_version = svn_wc_conflict_version_create(NULL, NULL,
                                                     SVN_INVALID_REVNUM,
                                                     svn_node_none,
                                                     result_pool);
  *conflict = svn_wc_conflict_description_create_tree(
    svn_path_join(dir_path, victim_basename, result_pool),
    NULL, node_kind, operation, src_left_version, src_right_version,
    result_pool);

  /* action */
  SVN_ERR(read_enum_field(&n, action_map,
                          skel->children->next->next->next->next));
  (*conflict)->action = (svn_wc_conflict_action_t)n;

  /* reason */
  SVN_ERR(read_enum_field(&n, reason_map,
                          skel->children->next->next->next->next->next));
  (*conflict)->reason = (svn_wc_conflict_reason_t)n;

  /* Let's just make it a bit easier on ourself here... */
  skel = skel->children->next->next->next->next->next->next;

  /* src_left_version */
  SVN_ERR(read_node_version_info((*conflict)->src_left_version, skel,
                                 scratch_pool, result_pool));

  /* src_right_version */
  SVN_ERR(read_node_version_info((*conflict)->src_right_version, skel->next,
                                 scratch_pool, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__read_tree_conflicts(apr_array_header_t **conflicts,
                            const char *conflict_data,
                            const char *dir_path,
                            apr_pool_t *pool)
{
  const svn_skel_t *skel;
  apr_pool_t *iterpool;

  *conflicts = apr_array_make(pool, 0,
                              sizeof(svn_wc_conflict_description_t *));

  if (conflict_data == NULL)
    return SVN_NO_ERROR;

  skel = svn_skel__parse(conflict_data, strlen(conflict_data), pool);
  if (skel == NULL)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Error parsing tree conflict skel"));

  iterpool = svn_pool_create(pool);
  for (skel = skel->children; skel != NULL; skel = skel->next)
    {
      svn_wc_conflict_description_t *conflict;

      svn_pool_clear(iterpool);
      SVN_ERR(read_one_tree_conflict(&conflict, skel, dir_path, iterpool,
                                     pool));
      if (conflict != NULL)
        APR_ARRAY_PUSH(*conflicts, svn_wc_conflict_description_t *) = conflict;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Prepend to SKEL the string corresponding to enumeration value N, as found
 * in MAP. */
static svn_error_t *
skel_prepend_enum(svn_skel_t *skel,
                  const enum_mapping_t *map,
                  int n,
                  apr_pool_t *result_pool)
{
  int i;

  for (i = 0; ; i++)
    {
      SVN_ERR_ASSERT(map[i].str != NULL);
      if (map[i].val == n)
        break;
    }

  svn_skel__prepend(svn_skel__str_atom(map[i].str, result_pool), skel);
  return SVN_NO_ERROR;
}

/* Prepend to PARENT_SKEL the several fields that represent VERSION_INFO, */
static svn_error_t *
prepend_version_info_skel(svn_skel_t *parent_skel,
                          const svn_wc_conflict_version_t *version_info,
                          apr_pool_t *pool)
{
  svn_skel_t *skel = svn_skel__make_empty_list(pool);

  /* node_kind */
  SVN_ERR(skel_prepend_enum(skel, node_kind_map, version_info->node_kind,
                            pool));

  /* path_in_repos */
  svn_skel__prepend(svn_skel__str_atom(version_info->path_in_repos
                                       ? version_info->path_in_repos
                                       : "", pool), skel);

  /* peg_rev */
  svn_skel__prepend(svn_skel__str_atom(apr_psprintf(pool, "%ld",
                                                    version_info->peg_rev),
                                       pool), skel);

  /* repos_url */
  svn_skel__prepend(svn_skel__str_atom(version_info->repos_url
                                       ? version_info->repos_url
                                       : "", pool), skel);

  svn_skel__prepend(svn_skel__str_atom("version", pool), skel);

  SVN_ERR_ASSERT(is_valid_version_info_skel(skel));

  svn_skel__prepend(skel, parent_skel);

  return SVN_NO_ERROR;
}

/*
 * This function could be static, but we need to link to it
 * in a unit test in tests/libsvn_wc/, so it isn't.
 */
svn_error_t *
svn_wc__write_tree_conflicts(const char **conflict_data,
                             apr_array_header_t *conflicts,
                             apr_pool_t *pool)
{
  /* A conflict version struct with all fields null/invalid. */
  static const svn_wc_conflict_version_t null_version = {
    NULL, SVN_INVALID_REVNUM, NULL, svn_node_unknown };
  int i;
  svn_skel_t *skel = svn_skel__make_empty_list(pool);

  /* Iterate backwards so that the list-prepend will build the skel in
     proper order. */
  for (i = conflicts->nelts; --i >= 0; )
    {
      const char *path;
      const svn_wc_conflict_description_t *conflict =
          APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);
      svn_skel_t *c_skel = svn_skel__make_empty_list(pool);

      /* src_right_version */
      if (conflict->src_right_version)
        SVN_ERR(prepend_version_info_skel(c_skel, conflict->src_right_version,
                                          pool));
      else
        SVN_ERR(prepend_version_info_skel(c_skel, &null_version, pool));

      /* src_left_version */
      if (conflict->src_left_version)
        SVN_ERR(prepend_version_info_skel(c_skel, conflict->src_left_version,
                                          pool));
      else
        SVN_ERR(prepend_version_info_skel(c_skel, &null_version, pool));

      /* reason */
      SVN_ERR(skel_prepend_enum(c_skel, reason_map, conflict->reason, pool));

      /* action */
      SVN_ERR(skel_prepend_enum(c_skel, action_map, conflict->action, pool));

      /* operation */
      SVN_ERR(skel_prepend_enum(c_skel, operation_map, conflict->operation,
                                pool));

      /* node_kind */
      SVN_ERR_ASSERT(conflict->node_kind == svn_node_dir
                     || conflict->node_kind == svn_node_file);
      SVN_ERR(skel_prepend_enum(c_skel, node_kind_map, conflict->node_kind,
                                pool));

      /* Victim path (escaping separator chars). */
      path = svn_path_basename(conflict->path, pool);
      SVN_ERR_ASSERT(strlen(path) > 0);
      svn_skel__prepend(svn_skel__str_atom(path, pool), c_skel);

      svn_skel__prepend(svn_skel__str_atom("conflict", pool), c_skel);

      SVN_ERR_ASSERT(is_valid_conflict_skel(c_skel));

      svn_skel__prepend(c_skel, skel);
    }

  *conflict_data = svn_skel__unparse(skel, pool)->data;

  return SVN_NO_ERROR;
}

/*
 * This function could be static, but we need to link to it
 * in a unit test in tests/libsvn_wc/, so it isn't.
 */
svn_boolean_t
svn_wc__tree_conflict_exists(apr_array_header_t *conflicts,
                             const char *victim_basename,
                             apr_pool_t *pool)
{
  const svn_wc_conflict_description_t *conflict;
  int i;

  for (i = 0; i < conflicts->nelts; i++)
    {
      conflict = APR_ARRAY_IDX(conflicts, i,
                               svn_wc_conflict_description_t *);
      if (strcmp(svn_path_basename(conflict->path, pool), victim_basename) == 0)
        return TRUE;
    }

  return FALSE;
}

svn_error_t *
svn_wc__del_tree_conflict(const char *victim_path,
                          svn_wc_adm_access_t *adm_access,
                          apr_pool_t *pool)
{
  svn_stringbuf_t *log_accum = NULL;

  SVN_ERR(svn_wc__loggy_del_tree_conflict(&log_accum, victim_path, adm_access,
                                          pool));

  if (log_accum != NULL)
    {
      SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));
      SVN_ERR(svn_wc__run_log(adm_access, NULL, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__add_tree_conflict(const svn_wc_conflict_description_t *conflict,
                          svn_wc_adm_access_t *adm_access,
                          apr_pool_t *pool)
{
  svn_wc_conflict_description_t *existing_conflict;
  svn_stringbuf_t *log_accum = NULL;

  /* Re-adding an existing tree conflict victim is an error. */
  SVN_ERR(svn_wc__get_tree_conflict(&existing_conflict, conflict->path,
                                    adm_access, pool));
  if (existing_conflict != NULL)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                         _("Attempt to add tree conflict that already exists"));

  SVN_ERR(svn_wc__loggy_add_tree_conflict(&log_accum, conflict, adm_access,
                                          pool));

  SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));
  SVN_ERR(svn_wc__run_log(adm_access, NULL, pool));

  return SVN_NO_ERROR;
}

/* Remove, from the array ARRAY, the element at index REMOVE_INDEX, possibly
 * changing the order of the remaining elements.
 */
static void
array_remove_unordered(apr_array_header_t *array, int remove_index)
{
  /* Get the address of the last element, and mark it as removed. Rely on
   * that element's memory being preserved intact for the moment. (This
   * guarantee is implied as it is how 'pop' returns the value.) */
  void *last_element = apr_array_pop(array);

  /* If the element to remove is not the last, overwrite it with the old
   * last element. (We have just decremented the array size, so check that
   * the index is still inside the array.) */
  if (remove_index < array->nelts)
    memcpy(array->elts + remove_index * array->elt_size, last_element,
           array->elt_size);

  /* The memory at LAST_ELEMENT need no longer be preserved. */
}

svn_error_t *
svn_wc__loggy_del_tree_conflict(svn_stringbuf_t **log_accum,
                                const char *victim_path,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool)
{
  const char *dir_path;
  const svn_wc_entry_t *entry;
  apr_array_header_t *conflicts;
  svn_wc_entry_t tmp_entry;
  const char *victim_basename = svn_path_basename(victim_path, pool);

  /* Make sure the node is a directory.
   * Otherwise we should not have been called. */
  dir_path = svn_wc_adm_access_path(adm_access);
  SVN_ERR(svn_wc_entry(&entry, dir_path, adm_access, TRUE, pool));
  SVN_ERR_ASSERT((entry != NULL) && (entry->kind == svn_node_dir));

  /* Make sure that VICTIM_PATH is a child node of DIR_PATH.
   * Anything else is a bug. */
  SVN_ERR_ASSERT(strcmp(dir_path, svn_path_dirname(victim_path, pool)) == 0);

  SVN_ERR(svn_wc__read_tree_conflicts(&conflicts, entry->tree_conflict_data,
                                      dir_path, pool));

  /* If CONFLICTS has a tree conflict with the same victim path as the
   * new conflict, then remove it. */
  if (svn_wc__tree_conflict_exists(conflicts, victim_basename, pool))
    {
      int i;

      /* Delete the element that matches VICTIM_BASENAME */
      for (i = 0; i < conflicts->nelts; i++)
        {
          const svn_wc_conflict_description_t *conflict
            = APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);

          if (strcmp(svn_path_basename(conflict->path, pool), victim_basename)
              == 0)
            {
              array_remove_unordered(conflicts, i);

              break;
            }
        }

      /* Rewrite the entry. */
      SVN_ERR(svn_wc__write_tree_conflicts(&tmp_entry.tree_conflict_data,
                                           conflicts,
                                           pool));

      SVN_ERR(svn_wc__loggy_entry_modify(log_accum, adm_access, dir_path,
                                         &tmp_entry,
                                         SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA,
                                         pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__get_tree_conflict(svn_wc_conflict_description_t **tree_conflict,
                          const char *victim_path,
                          svn_wc_adm_access_t *adm_access,
                          apr_pool_t *pool)
{
  const char *parent_path = svn_path_dirname(victim_path, pool);
  svn_wc_adm_access_t *parent_adm_access;
  svn_boolean_t parent_adm_access_is_temporary = FALSE;
  svn_error_t *err;
  apr_array_header_t *conflicts;
  const svn_wc_entry_t *entry;
  int i;

  /* Try to get the parent's admin access baton from the baton set. */
  err = svn_wc_adm_retrieve(&parent_adm_access, adm_access, parent_path,
                            pool);
  if (err && (err->apr_err == SVN_ERR_WC_NOT_LOCKED))
    {
      svn_error_clear(err);

      /* Try to access the parent dir independently. We can't add
         a parent's access baton to the existing access baton set
         of its child, because the lifetimes would be wrong
         according to doc string of svn_wc_adm_open3(), so we get
         open it temporarily and close it after use. */
      err = svn_wc_adm_open3(&parent_adm_access, NULL, parent_path,
                             FALSE, 0, NULL, NULL, pool);
      parent_adm_access_is_temporary = TRUE;

      /* If the parent isn't a WC dir, the child can't be
         tree-conflicted. */
      if (err && (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY))
        {
          svn_error_clear(err);
          *tree_conflict = NULL;
          return SVN_NO_ERROR;
        }
    }
  SVN_ERR(err);

  SVN_ERR(svn_wc_entry(&entry, parent_path, parent_adm_access, TRUE, pool));
  SVN_ERR(svn_wc__read_tree_conflicts(&conflicts, entry->tree_conflict_data,
                                      parent_path, pool));

  *tree_conflict = NULL;
  for (i = 0; i < conflicts->nelts; i++)
    {
      svn_wc_conflict_description_t *conflict;

      conflict = APR_ARRAY_IDX(conflicts, i,
                               svn_wc_conflict_description_t *);
      if (strcmp(svn_path_basename(conflict->path, pool),
                 svn_path_basename(victim_path, pool)) == 0)
        {
          *tree_conflict = conflict;
          break;
        }
    }

  /* If we opened a temporary admin access baton, close it. */
  if (parent_adm_access_is_temporary)
    SVN_ERR(svn_wc_adm_close2(parent_adm_access, pool));

  return SVN_NO_ERROR;
}
