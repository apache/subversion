/*
 * tree_conflicts.c: Handling of known problematic tree conflict use cases.
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
#include "svn_types.h"
#include "svn_private_config.h"

#include <assert.h>

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

/* Parse the 'victim_path' field pointed to by *START into the
 * tree conflict descriptor pointed to by CONFLICT.
 * Stop reading at a field delimiter and never read past END.
 * After reading, make *START point to the character after
 * the field delimiter.
 * Do all allocations in POOL.
 */
static svn_error_t *
read_victim_path(svn_wc_conflict_description_t *conflict,
                 const char **start,
                 const char *end,
                 apr_pool_t *pool)
{
  svn_stringbuf_t *victim_path;
  svn_boolean_t escape = FALSE;

  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  victim_path = svn_stringbuf_create("", pool);

  while (*start < end)
  {
    /* The field or description separators may occur inside the
     * victim_path if they are escaped. */
    if (! escape && **start == SVN_WC__TREE_CONFLICT_ESCAPE_CHAR)
      {
        escape = TRUE;
        (*start)++;

        if (! (*start < end))
          return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("Unexpected end of tree conflict description, within escape "
              "sequence in 'victim_path'"));

        if (**start != SVN_WC__TREE_CONFLICT_DESC_SEPARATOR
            && **start != SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR
            && **start != SVN_WC__TREE_CONFLICT_ESCAPE_CHAR)
          return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("Illegal escaped character in 'victim_path' of tree "
              "conflict description"));
      }

    if (! escape)
      {
        if (**start == SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR)
          break;
        else if (**start == SVN_WC__TREE_CONFLICT_DESC_SEPARATOR)
          return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Unescaped description delimiter inside 'victim_path' "
               "in tree conflict description"));
      }

    svn_stringbuf_appendbytes(victim_path, *start, 1);
    escape = FALSE;
    (*start)++;
  }

  if (victim_path->len == 0)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("Empty 'victim_path' in tree conflict description"));

  if (**start == SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR)
    (*start)++;
  else
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
        _("No delimiter after 'victim_path' in tree conflict description"));

  conflict->victim_path = victim_path->data;

  return SVN_NO_ERROR;
}

/* Parse the 'node_kind' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
 * After reading, make *START point to the character after
 * the field delimiter.
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

  if (*start >= end || **start != SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("No delimiter after 'node_kind' in tree conflict description"));

  (*start)++;
  return SVN_NO_ERROR;
}

/* Parse the 'operation' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
 * After reading, make *START point to the character after
 * the field delimiter.
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

  if (*start >= end || **start != SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("No delimiter after 'operation' in tree conflict description"));

  (*start)++;
  return SVN_NO_ERROR;
}

/* Parse the 'action' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
 * After reading, make *START point to the character after
 * the field delimiter.
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

  if (*start >= end || **start != SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
             _("No delimiter after 'action' in tree conflict description"));

  (*start)++;
  return SVN_NO_ERROR;
}

/* Parse the 'reason' field pointed to by *START into the
 * the tree conflict descriptor pointed to by DESC.
 * Don't read further than END.
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

  /* This was the last field to parse, leave *START alone.
   * It should already point to an SVN_WC__TREE_CONFLICT_DESC_SEPARATOR.
   */
  return SVN_NO_ERROR;
}

/* Parse a newly allocated svn_wc_conflict_description_t object from the
 * character string pointed to by *START. Return the result in *CONFLICT.
 * Don't read further than END. If a description separator is found in
 * the string after the conflict description that was read, set *START
 * to the first character of the next conflict description.
 * Otherwise, set *START to NULL.
 * Do all allocations in pool.
 */
static svn_error_t *
read_one_tree_conflict(svn_wc_conflict_description_t **conflict,
                       const char **start,
                       const char *end,
                       apr_pool_t *pool)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  *conflict = svn_wc_conflict_description_create_tree(
    NULL, NULL, svn_node_none, 0, pool);

  /* Each of these modifies *START ! */
  SVN_ERR(read_victim_path(*conflict, start, end, pool));
  SVN_ERR(read_node_kind(*conflict, start, end));
  SVN_ERR(read_operation(*conflict, start, end));
  SVN_ERR(read_action(*conflict, start, end));
  SVN_ERR(read_reason(*conflict, start, end));

  /* *START should now point to an SVN_WC__TREE_CONFLICT_DESC_SEPARATOR
   * if there are any descriptions left. */
  if (**start == SVN_WC__TREE_CONFLICT_DESC_SEPARATOR)
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
svn_wc_read_tree_conflicts_from_entry(apr_array_header_t *conflicts,
                                       const svn_wc_entry_t *dir_entry,
                                       apr_pool_t *pool)
{
  const char *start, *end;
  svn_wc_conflict_description_t *conflict = NULL;

  if (dir_entry->tree_conflict_data == NULL)
    {
      conflicts = NULL;
      return SVN_NO_ERROR;
    }

  assert(conflicts);

  start = dir_entry->tree_conflict_data;
  end = start + strlen(start);

  while (start != NULL && start <= end) /* Yes, '<=', because 'start == end'
                                           is a special case that is dealt
                                           with further down the call chain. */
    {
      SVN_ERR(read_one_tree_conflict(&conflict, &start, end, pool));
      if (conflict != NULL)
        APR_ARRAY_PUSH(conflicts, svn_wc_conflict_description_t *) = conflict;
    }

  if (start != NULL)
    /* Not all conflicts have been read from the entry, but no error
     * has been thrown yet. We should not even be here! */
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
        _("Invalid tree conflict data in entries file, "
          "but no idea what went wrong"));

  return SVN_NO_ERROR;
}

/* Like svn_stringbuf_appendcstr(), but appends a single char. */
static void
stringbuf_appendchar(svn_stringbuf_t *targetstr,
                     char c)
{
  char s[2];
  
  s[0] = c;
  s[1] = '\0';
  svn_stringbuf_appendbytes(targetstr, s, 1);
}

/*
 * This function could be static, but we need to link to it
 * in a unit test in tests/libsvn_wc/, so it isn't.
 */
svn_error_t *
svn_wc__write_tree_conflicts_to_entry(apr_array_header_t *conflicts,
                                      svn_wc_entry_t *dir_entry,
                                      apr_pool_t *pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create("", pool);
  const char *path;
  int i, j, len;

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description_t *conflict =
          APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);

      path = conflict->victim_path;
      len = strlen(conflict->victim_path);
      if (len == 0)
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                        _("Empty victim_path in tree conflict description"));

      /* Escape separator chars while writing victim_path. */
      for (j = 0; j < len; j++)
        {
          if ((path[j] == SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR) ||
              (path[j] == SVN_WC__TREE_CONFLICT_DESC_SEPARATOR) ||
              (path[j] == SVN_WC__TREE_CONFLICT_ESCAPE_CHAR))
            {
              stringbuf_appendchar(buf, SVN_WC__TREE_CONFLICT_ESCAPE_CHAR);
            }
          svn_stringbuf_appendbytes(buf, &(path[j]), 1);
        }

      stringbuf_appendchar(buf, SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR);

      switch (conflict->node_kind)
        {
          case svn_node_dir:
            svn_stringbuf_appendcstr(buf, SVN_WC__NODE_DIR);
            break;
          case svn_node_file:
            svn_stringbuf_appendcstr(buf, SVN_WC__NODE_FILE);
            break;
          default:
            return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                _("Bad node_kind in tree conflict description"));
        }

      stringbuf_appendchar(buf, SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR);

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
            return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                _("Bad operation in tree conflict description"));
        }

      stringbuf_appendchar(buf, SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR);

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
            return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                _("Bad action in tree conflict description"));
        }

      stringbuf_appendchar(buf, SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR);

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
            return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                _("Bad reason in tree conflict description"));
        }

      if (i < (conflicts->nelts - 1))
        stringbuf_appendchar(buf, SVN_WC__TREE_CONFLICT_DESC_SEPARATOR);
    }

  dir_entry->tree_conflict_data = apr_pstrdup(pool, buf->data);

  return SVN_NO_ERROR;
}

/*
 * This function could be static, but we need to link to it
 * in a unit test in tests/libsvn_wc/, so it isn't.
 */
svn_boolean_t
svn_wc__tree_conflict_exists(apr_array_header_t *conflicts,
                             const char *victim_path)
{
  const svn_wc_conflict_description_t *conflict;
  int i;

  for (i = 0; i < conflicts->nelts; i++)
    {
      conflict = APR_ARRAY_IDX(conflicts, i, 
                               svn_wc_conflict_description_t *);
      if (strcmp(conflict->victim_path, victim_path) == 0)
        return TRUE;
    }

  return FALSE;
}

svn_error_t *
svn_wc_add_tree_conflict_data(const svn_wc_conflict_description_t *conflict,
                              svn_wc_adm_access_t *adm_access,
                              apr_pool_t *pool)
{
  svn_stringbuf_t *log_accum = svn_stringbuf_create("", pool);

  SVN_ERR(svn_wc__loggy_add_tree_conflict_data(log_accum,
                                               conflict,
                                               adm_access,
                                               pool));

  SVN_ERR(svn_wc__write_log(adm_access, 0, log_accum, pool));
  SVN_ERR(svn_wc__run_log(adm_access, NULL, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_add_tree_conflict_data(
  svn_stringbuf_t *log_accum,
  const svn_wc_conflict_description_t *conflict,
  svn_wc_adm_access_t *adm_access,
  apr_pool_t *pool)
{
  const char *dir_path;
  const svn_wc_entry_t *entry;
  apr_array_header_t *conflicts;
  svn_wc_entry_t tmp_entry;

  /* Make sure the node is a directory.
   * Otherwise we should not have been called. */
  dir_path = svn_wc_adm_access_path(adm_access);
  SVN_ERR(svn_wc_entry(&entry, dir_path, adm_access, TRUE, pool));
  assert(entry->kind == svn_node_dir);

  conflicts = apr_array_make(pool, 0,
                             sizeof(svn_wc_conflict_description_t *));
  SVN_ERR(svn_wc_read_tree_conflicts_from_entry(conflicts, entry, pool));

  /* If CONFLICTS has a tree conflict with the same victim_path as the
   * new conflict, then the working copy has been corrupted. */
  if (svn_wc__tree_conflict_exists(conflicts, conflict->victim_path))
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
        _("Attempt to add tree conflict that already exists"));

  APR_ARRAY_PUSH(conflicts, const svn_wc_conflict_description_t *) = conflict;

  SVN_ERR(svn_wc__write_tree_conflicts_to_entry(conflicts, &tmp_entry, pool));
  SVN_ERR(svn_wc__loggy_entry_modify(&log_accum,
                                     adm_access,
                                     dir_path,
                                     &tmp_entry,
                                     SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA,
                                     pool));

  return SVN_NO_ERROR;
}

