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

/* If **INPUT starts with *TOKEN, advance *INPUT by the length of *TOKEN
 * and return TRUE. Else, return FALSE and leave *INPUT alone. */
static svn_boolean_t
advance_on_match(const char **input, const char *token)
{
  int len = strlen(token);
  if ((strncmp(*input, token, len)) == 0)
    {
      *input += len;
      return TRUE;
    }
  else
    return FALSE;
}

/* Ensure the next character at position *START is a field separator, and
 * advance *START past it. */
static svn_error_t *
read_field_separator(const char **start,
                     const char *end)
{
  if (*start >= end || **start != field_separator)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Missing field delimiter in tree conflict description"));

  (*start)++;
  return SVN_NO_ERROR;
}

/* Parse a string field out of the data pointed to by *START. Set *STR to a
 * copy of the unescaped string, allocated in POOL. The string may be empty.
 * Stop reading at an unescaped field- or description delimiter, and never
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

/* Parse the 'victim path' field pointed to by *START. Modify the 'path'
 * field of *CONFLICT by appending the victim name to its existing value.
 * Stop reading at a field delimiter and never read past END.
 * After reading, make *START point to the character after the field.
 * Do all allocations in POOL.
 */
static svn_error_t *
read_victim_path(svn_wc_conflict_description_t *conflict,
                 const char **start,
                 const char *end,
                 apr_pool_t *pool)
{
  const char *victim_basename;

  SVN_ERR(read_string_field(&victim_basename, start, end, pool));

  if (victim_basename[0] == '\0')
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Empty 'victim' field in tree conflict "
                              "description"));

  conflict->path = svn_path_join(conflict->path, victim_basename, pool);

  return SVN_NO_ERROR;
}

/* Parse the 'node_kind' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_node_kind(svn_wc_conflict_description_t *conflict,
               const char **start,
               const char *end)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  if (advance_on_match(start, SVN_WC__NODE_FILE))
     conflict->node_kind = svn_node_file;
  else if (advance_on_match(start, SVN_WC__NODE_DIR))
     conflict->node_kind = svn_node_dir;
  else
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Invalid 'node_kind' field in tree conflict description"));

  return SVN_NO_ERROR;
}

/* Parse the 'operation' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_operation(svn_wc_conflict_description_t *conflict,
               const char **start,
               const char *end)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  if (advance_on_match(start, SVN_WC__OPERATION_UPDATE))
      conflict->operation = svn_wc_operation_update;
  else if (advance_on_match(start, SVN_WC__OPERATION_SWITCH))
      conflict->operation = svn_wc_operation_switch;
  else if (advance_on_match(start, SVN_WC__OPERATION_MERGE))
      conflict->operation = svn_wc_operation_merge;
  else
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Invalid 'operation' field in tree conflict description"));

  return SVN_NO_ERROR;
}

/* Parse the 'action' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_action(svn_wc_conflict_description_t *conflict,
            const char **start,
            const char *end)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  if (advance_on_match(start, SVN_WC__CONFLICT_ACTION_EDITED))
    conflict->action = svn_wc_conflict_action_edit;
  else if (advance_on_match(start, SVN_WC__CONFLICT_ACTION_DELETED))
    conflict->action = svn_wc_conflict_action_delete;
  else if (advance_on_match(start, SVN_WC__CONFLICT_ACTION_ADDED))
    conflict->action = svn_wc_conflict_action_add;
  else
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Invalid 'action' field in tree conflict description"));

  return SVN_NO_ERROR;
}

/* Parse the 'reason' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
 * After reading, make *START point to the character after the field.
 */
static svn_error_t *
read_reason(svn_wc_conflict_description_t *conflict,
            const char **start,
            const char *end)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  if (advance_on_match(start, SVN_WC__CONFLICT_REASON_EDITED))
    conflict->reason = svn_wc_conflict_reason_edited;
  else if (advance_on_match(start, SVN_WC__CONFLICT_REASON_DELETED))
    conflict->reason = svn_wc_conflict_reason_deleted;
  else if (advance_on_match(start, SVN_WC__CONFLICT_REASON_MISSING))
    conflict->reason = svn_wc_conflict_reason_missing;
  else if (advance_on_match(start, SVN_WC__CONFLICT_REASON_OBSTRUCTED))
    conflict->reason = svn_wc_conflict_reason_obstructed;
  else if (advance_on_match(start, SVN_WC__CONFLICT_REASON_ADDED))
    conflict->reason = svn_wc_conflict_reason_added;
  else
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Invalid 'reason' field in tree conflict description"));

  return SVN_NO_ERROR;
}

/* Parse a newly allocated svn_wc_conflict_description_t object from the
 * character string pointed to by *START. Return the result in *CONFLICT.
 * Don't read further than END. If a description separator is found in
 * the string after the conflict description that was read, set *START
 * to the first character of the next conflict description.
 * Otherwise, set *START to NULL.
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
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  *conflict = svn_wc_conflict_description_create_tree(
    dir_path, NULL, svn_node_none, 0, pool);

  /* Each of these modifies *START ! */
  SVN_ERR(read_victim_path(*conflict, start, end, pool));
  SVN_ERR(read_field_separator(start, end));
  SVN_ERR(read_node_kind(*conflict, start, end));
  SVN_ERR(read_field_separator(start, end));
  SVN_ERR(read_operation(*conflict, start, end));
  SVN_ERR(read_field_separator(start, end));
  SVN_ERR(read_action(*conflict, start, end));
  SVN_ERR(read_field_separator(start, end));
  SVN_ERR(read_reason(*conflict, start, end));

  /* *START should now point to a description separator
   * if there are any descriptions left. */
  if (**start == desc_separator)
    (*start)++;
  else
    {
      if (*start >= end)
        *start = NULL;
      else
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("No delimiter at end of tree conflict description, "
            "even though there is still data left to read"));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__read_tree_conflicts(apr_array_header_t **conflicts,
                            const char *conflict_data,
                            const char *dir_path,
                            apr_pool_t *pool)
{
  const char *start, *end;
  svn_wc_conflict_description_t *conflict = NULL;

  if (conflict_data == NULL)
    {
      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(*conflicts);

  start = conflict_data;
  end = start + strlen(start);

  while (start != NULL && start <= end) /* Yes, '<=', because 'start == end'
                                           is a special case that is dealt
                                           with further down the call chain. */
    {
      SVN_ERR(read_one_tree_conflict(&conflict, &start, end, dir_path, pool));
      if (conflict != NULL)
        APR_ARRAY_PUSH(*conflicts, svn_wc_conflict_description_t *) = conflict;
    }

  if (start != NULL)
    /* Not all conflicts have been read from the entry, but no error
     * has been thrown yet. We should not even be here! */
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
        _("Invalid tree conflict data in 'entries' file, "
          "but no idea what went wrong"));

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

/*
 * This function could be static, but we need to link to it
 * in a unit test in tests/libsvn_wc/, so it isn't.
 */
svn_error_t *
svn_wc__write_tree_conflicts(char **conflict_data,
                             apr_array_header_t *conflicts,
                             apr_pool_t *pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create("", pool);
  int i;

  for (i = 0; i < conflicts->nelts; i++)
    {
      const char *path;
      const svn_wc_conflict_description_t *conflict =
          APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);

      /* Escape separator chars while writing victim path. */
      path = svn_path_basename(conflict->path, pool);
      SVN_ERR_ASSERT(strlen(path) > 0);
      write_string_field(buf, path);

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      switch (conflict->node_kind)
        {
          case svn_node_dir:
            svn_stringbuf_appendcstr(buf, SVN_WC__NODE_DIR);
            break;
          case svn_node_file:
            svn_stringbuf_appendcstr(buf, SVN_WC__NODE_FILE);
            break;
          default:
            SVN_ERR_MALFUNCTION();
        }

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      switch (conflict->operation)
        {
          case svn_wc_operation_update:
            svn_stringbuf_appendcstr(buf, SVN_WC__OPERATION_UPDATE);
            break;
          case svn_wc_operation_switch:
            svn_stringbuf_appendcstr(buf, SVN_WC__OPERATION_SWITCH);
            break;
          case svn_wc_operation_merge:
            svn_stringbuf_appendcstr(buf, SVN_WC__OPERATION_MERGE);
            break;
          default:
            SVN_ERR_MALFUNCTION();
        }

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      switch (conflict->action)
        {
          case svn_wc_conflict_action_edit:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_ACTION_EDITED);
            break;
         case svn_wc_conflict_action_delete:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_ACTION_DELETED);
            break;
         case svn_wc_conflict_action_add:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_ACTION_ADDED);
            break;
          default:
            SVN_ERR_MALFUNCTION();
        }

      svn_stringbuf_appendbytes(buf, &field_separator, 1);

      switch (conflict->reason)
        {
          case svn_wc_conflict_reason_edited:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_REASON_EDITED);
            break;
          case svn_wc_conflict_reason_deleted:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_REASON_DELETED);
            break;
          case svn_wc_conflict_reason_added:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_REASON_ADDED);
            break;
          case svn_wc_conflict_reason_missing:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_REASON_MISSING);
            break;
          case svn_wc_conflict_reason_obstructed:
            svn_stringbuf_appendcstr(buf, SVN_WC__CONFLICT_REASON_OBSTRUCTED);
            break;
          default:
            SVN_ERR_MALFUNCTION();
        }

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

  conflicts = apr_array_make(pool, 0,
                             sizeof(svn_wc_conflict_description_t *));
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

  conflicts = apr_array_make(pool, 0,
                             sizeof(svn_wc_conflict_description_t *));
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

