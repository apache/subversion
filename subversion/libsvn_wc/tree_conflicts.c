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

#include "tree_conflicts.h"
#include "log.h"
#include "entries.h"
#include "svn_path.h"
#include "svn_types.h"

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


static const char field_separator = SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR;
static const char desc_separator = SVN_WC__TREE_CONFLICT_DESC_SEPARATOR;
static const char escape_char = SVN_WC__TREE_CONFLICT_ESCAPE_CHAR;

/* Ensure the next character at position *START is a field separator, and
 * advance *START past it. */
static svn_error_t *
read_field_separator(const char **start,
                     const char *end)
{
  if (*start >= end || **start != field_separator)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Missing field separator in tree conflict description"));

  (*start)++;
  return SVN_NO_ERROR;
}

/* Ensure the next character at position *START is a description separator,
 * and advance *START past it. */
static svn_error_t *
read_desc_separator(const char **start,
                    const char *end)
{
  if (*start >= end || **start != desc_separator)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("No separator at end of tree conflict description, "
               "even though there is still data left to read"));

  (*start)++;
  return SVN_NO_ERROR;
}

/* Parse a string field out of the data pointed to by *START. Set *STR to a
 * copy of the unescaped string, allocated in POOL. The string may be empty.
 * Stop reading at an unescaped field- or description separator, and never
 * read past END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_string_field(const char **str,
                  const char **start,
                  const char *end,
                  apr_pool_t *pool)
{
  svn_stringbuf_t *new_str = svn_stringbuf_create("", pool);

  while (*start < end)
    {
      /* The field or description separators may occur inside the
       * string if they are escaped. */
      if (**start == escape_char)
        {
          (*start)++;

          if (! (*start < end))
            return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
              _("Unfinished escape sequence in tree conflict description"));

          if (**start != desc_separator
              && **start != field_separator
              && **start != escape_char)
            return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
              _("Illegal escaped character in tree conflict description"));
        }
      else
        {
          if (**start == field_separator
              || **start == desc_separator)
            break;
        }

      svn_stringbuf_appendbytes(new_str, *start, 1);
      (*start)++;
    }

  *str = new_str->data;
  return SVN_NO_ERROR;
}

/* A mapping between a string STR and an enumeration value VAL. */
typedef struct enum_mapping_t
{
  const char *str;
  int val;
} enum_mapping_t;

/* A map for svn_node_kind_t values. */
static const enum_mapping_t node_kind_map[] =
{
  { SVN_WC__NODE_NONE, svn_node_none },
  { SVN_WC__NODE_FILE, svn_node_file },
  { SVN_WC__NODE_DIR,  svn_node_dir },
  { "",                svn_node_unknown },
  { NULL,              0 }
};

/* A map for svn_wc_operation_t values. */
static const enum_mapping_t operation_map[] =
{
  { SVN_WC__OPERATION_UPDATE, svn_wc_operation_update },
  { SVN_WC__OPERATION_SWITCH, svn_wc_operation_switch },
  { SVN_WC__OPERATION_MERGE,  svn_wc_operation_merge },
  { NULL,                     0 }
};

/* A map for svn_wc_conflict_action_t values. */
static const enum_mapping_t action_map[] =
{
  { SVN_WC__CONFLICT_ACTION_EDITED,  svn_wc_conflict_action_edit },
  { SVN_WC__CONFLICT_ACTION_DELETED, svn_wc_conflict_action_delete },
  { SVN_WC__CONFLICT_ACTION_ADDED,   svn_wc_conflict_action_add },
  { NULL,                            0 }
};

/* A map for svn_wc_conflict_reason_t values. */
static const enum_mapping_t reason_map[] =
{
  { SVN_WC__CONFLICT_REASON_EDITED,     svn_wc_conflict_reason_edited },
  { SVN_WC__CONFLICT_REASON_DELETED,    svn_wc_conflict_reason_deleted },
  { SVN_WC__CONFLICT_REASON_MISSING,    svn_wc_conflict_reason_missing },
  { SVN_WC__CONFLICT_REASON_OBSTRUCTED, svn_wc_conflict_reason_obstructed },
  { SVN_WC__CONFLICT_REASON_ADDED,      svn_wc_conflict_reason_added },
  { NULL,                               0 }
};

/* Parse the enumeration field pointed to by *START into *RESULT as a plain
 * 'int', using MAP to convert from strings to enumeration values.
 * In MAP, a null STR field marks the end of the map.
 * Don't read further than END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_enum_field(int *result,
                const enum_mapping_t *map,
                const char **start,
                const char *end,
                apr_pool_t *pool)
{
  const char *str;
  int i;

  SVN_ERR(read_string_field(&str, start, end, pool));

  /* Find STR in MAP; error if not found. */
  for (i = 0; ; i++)
    {
      if (map[i].str == NULL)
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                                _("Unknown enumeration value in tree conflict "
                                  "description"));
      if (strcmp(str, map[i].str) == 0)
        break;
    }

  *result = map[i].val;
  return SVN_NO_ERROR;
}

/* Parse the conflict info fields pointed to by *START into *VERSION_INFO.
 * Don't read further than END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_node_version_info(svn_wc_conflict_version_t *version_info,
                       const char **start,
                       const char *end,
                       apr_pool_t *pool)
{
  const char *str;
  int n;

  /* repos_url */
  SVN_ERR(read_string_field(&str, start, end, pool));
  version_info->repos_url = (str[0] == '\0') ? NULL : str;
  SVN_ERR(read_field_separator(start, end));

  /* peg_rev */
  SVN_ERR(read_string_field(&str, start, end, pool));
  version_info->peg_rev = (str[0] == '\0') ? SVN_INVALID_REVNUM
    : SVN_STR_TO_REV(str);
  SVN_ERR(read_field_separator(start, end));

  /* path_in_repos */
  SVN_ERR(read_string_field(&str, start, end, pool));
  version_info->path_in_repos = (str[0] == '\0') ? NULL : str;
  SVN_ERR(read_field_separator(start, end));

  /* node_kind */
  SVN_ERR(read_enum_field(&n, node_kind_map, start, end, pool));
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
                       const char **start,
                       const char *end,
                       const char *dir_path,
                       apr_pool_t *pool)
{
  const char *victim_basename;
  svn_node_kind_t node_kind;
  svn_wc_operation_t operation;
  svn_wc_conflict_version_t *src_left_version;
  svn_wc_conflict_version_t *src_right_version;
  int n;

  SVN_ERR_ASSERT(*start < end);

  /* Each read_...() call modifies *START ! */

  /* victim basename */
  SVN_ERR(read_string_field(&victim_basename, start, end, pool));
  if (victim_basename[0] == '\0')
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Empty 'victim' field in tree conflict "
                              "description"));
  SVN_ERR(read_field_separator(start, end));

  /* node_kind */
  SVN_ERR(read_enum_field(&n, node_kind_map, start, end, pool));
  node_kind = (svn_node_kind_t)n;
  if (node_kind != svn_node_file && node_kind != svn_node_dir)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Invalid 'node_kind' field in tree conflict description"));
  SVN_ERR(read_field_separator(start, end));

  /* operation */
  SVN_ERR(read_enum_field(&n, operation_map, start, end, pool));
  operation = (svn_wc_operation_t)n;
  SVN_ERR(read_field_separator(start, end));

  /* Construct the description object */
  src_left_version = svn_wc_conflict_version_create(NULL, NULL, 
                                                    SVN_INVALID_REVNUM,
                                                    svn_node_none, pool);
  src_right_version = svn_wc_conflict_version_create(NULL, NULL, 
                                                     SVN_INVALID_REVNUM,
                                                     svn_node_none, pool);
  *conflict = svn_wc_conflict_description_create_tree(
    svn_path_join(dir_path, victim_basename, pool),
    NULL, node_kind, operation, src_left_version, src_right_version, pool);

  /* action */
  SVN_ERR(read_enum_field(&n, action_map, start, end, pool));
  (*conflict)->action = (svn_wc_conflict_action_t)n;
  SVN_ERR(read_field_separator(start, end));

  /* reason */
  SVN_ERR(read_enum_field(&n, reason_map, start, end, pool));
  (*conflict)->reason = (svn_wc_conflict_reason_t)n;
  SVN_ERR(read_field_separator(start, end));

  /* src_left_version */
  SVN_ERR(read_node_version_info((*conflict)->src_left_version, start, end,
                                 pool));
  SVN_ERR(read_field_separator(start, end));

  /* src_right_version */
  SVN_ERR(read_node_version_info((*conflict)->src_right_version, start, end,
                                 pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__read_tree_conflicts(apr_array_header_t **conflicts,
                            const char *conflict_data,
                            const char *dir_path,
                            apr_pool_t *pool)
{
  const char *start, *end;

  *conflicts = apr_array_make(pool, 0,
                              sizeof(svn_wc_conflict_description_t *));

  if (conflict_data == NULL)
    return SVN_NO_ERROR;

  start = conflict_data;
  end = start + strlen(start);

  while (start < end)
    {
      svn_wc_conflict_description_t *conflict;

      SVN_ERR(read_one_tree_conflict(&conflict, &start, end, dir_path, pool));
      if (conflict != NULL)
        APR_ARRAY_PUSH(*conflicts, svn_wc_conflict_description_t *) = conflict;

      /* *START should now point to a description separator
       * if there are any descriptions left. */
      if (start < end)
        SVN_ERR(read_desc_separator(&start, end));
    }

  return SVN_NO_ERROR;
}

/* Append to BUF the string STR, escaping it as necessary. */
static void
write_string_field(svn_stringbuf_t *buf,
                   const char *str)
{
  int len = strlen(str);
  int i;

  /* Escape separator chars. */
  for (i = 0; i < len; i++)
    {
      if ((str[i] == field_separator)
          || (str[i] == desc_separator)
          || (str[i] == escape_char))
        {
          svn_stringbuf_appendbytes(buf, &escape_char, 1);
        }
      svn_stringbuf_appendbytes(buf, &str[i], 1);
    }
}

/* Append to BUF the string corresponding to enumeration value N, as found
 * in MAP. */
static svn_error_t *
write_enum_field(svn_stringbuf_t *buf,
                 const enum_mapping_t *map,
                 int n)
{
  int i;

  for (i = 0; ; i++)
    {
      SVN_ERR_ASSERT(map[i].str != NULL);
      if (map[i].val == n)
        break;
    }
  svn_stringbuf_appendcstr(buf, map[i].str);
  return SVN_NO_ERROR;
}

/* Append to BUF the denary form of the number N. */
static void
write_integer_field(svn_stringbuf_t *buf,
                    int n,
                    apr_pool_t *pool)
{
  const char *str = apr_psprintf(pool, "%d", n);

  svn_stringbuf_appendcstr(buf, str);
}

/* Append to BUF the several fields that represent VERSION_INFO, */
static svn_error_t *
write_node_version_info(svn_stringbuf_t *buf,
                         const svn_wc_conflict_version_t *version_info,
                         apr_pool_t *pool)
{
  if (version_info->repos_url)
    write_string_field(buf, version_info->repos_url);
  svn_stringbuf_appendbytes(buf, &field_separator, 1);

  if (SVN_IS_VALID_REVNUM(version_info->peg_rev))
    write_integer_field(buf, version_info->peg_rev, pool);
  svn_stringbuf_appendbytes(buf, &field_separator, 1);

  if (version_info->path_in_repos)
    write_string_field(buf, version_info->path_in_repos);
  svn_stringbuf_appendbytes(buf, &field_separator, 1);

  SVN_ERR(write_enum_field(buf, node_kind_map, version_info->node_kind));
  return SVN_NO_ERROR;
}

/*
 * This function could be static, but we need to link to it
 * in a unit test in tests/libsvn_wc/, so it isn't.
 */
svn_error_t *
svn_wc__write_tree_conflicts(char **conflict_data,
                             apr_array_header_t *conflicts,
                             apr_pool_t *pool)
{
  /* A conflict version struct with all fields null/invalid. */
  static const svn_wc_conflict_version_t null_version = {
    NULL, SVN_INVALID_REVNUM, NULL, svn_node_unknown };
  svn_stringbuf_t *buf = svn_stringbuf_create("", pool);
  int i;

  for (i = 0; i < conflicts->nelts; i++)
    {
      const char *path;
      const svn_wc_conflict_description_t *conflict =
          APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);

      /* Victim path (escaping separator chars). */
      path = svn_path_basename(conflict->path, pool);
      SVN_ERR_ASSERT(strlen(path) > 0);
      write_string_field(buf, path);

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      /* node_kind */
      SVN_ERR_ASSERT(conflict->node_kind == svn_node_dir
                     || conflict->node_kind == svn_node_file);
      SVN_ERR(write_enum_field(buf, node_kind_map, conflict->node_kind));

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      /* operation */
      SVN_ERR(write_enum_field(buf, operation_map, conflict->operation));

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      /* action */
      SVN_ERR(write_enum_field(buf, action_map, conflict->action));

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      /* reason */
      SVN_ERR(write_enum_field(buf, reason_map, conflict->reason));

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      /* src_left_version */
      if (conflict->src_left_version)
        SVN_ERR(write_node_version_info(buf, conflict->src_left_version,
                                        pool));
      else
        SVN_ERR(write_node_version_info(buf, &null_version, pool));

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      /* src_right_version */
      if (conflict->src_right_version)
        SVN_ERR(write_node_version_info(buf, conflict->src_right_version,
                                        pool));
      else
        SVN_ERR(write_node_version_info(buf, &null_version, pool));

      if (i < (conflicts->nelts - 1))
        svn_stringbuf_appendbytes(buf, &desc_separator, 1);
    }

  *conflict_data = apr_pstrdup(pool, buf->data);

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
  svn_stringbuf_t *log_accum = NULL;

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
  char *conflict_data;
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
      SVN_ERR(svn_wc__write_tree_conflicts(&conflict_data, conflicts, pool));
      tmp_entry.tree_conflict_data = apr_pstrdup(pool, conflict_data);

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

