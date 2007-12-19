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

struct tree_conflict_phrases
{
  const char *update_deleted;
  const char *update_edited;
  const char *merge_deleted;
  const char *merge_edited;
  const char *we_deleted;
  const char *we_edited;
  const char *does_not_exist;
};

/* Return a new (possibly localised)
 * tree_conflict_phrases object allocated in POOL. */
static struct tree_conflict_phrases *
new_tree_conflict_phrases(apr_pool_t *pool)
{
  struct tree_conflict_phrases *phrases =
    apr_pcalloc(pool, sizeof(struct tree_conflict_phrases));

  phrases->update_deleted = _("The update wants to delete the file '%s'\n"
                              "(possibly as part of a rename operation).\n");

  phrases->update_edited = _("The update wants to edit the file '%s'.\n");

  phrases->merge_deleted = _("The merge wants to delete the file '%s'\n"
                             "(possibly as part of a rename operation).\n");

  phrases->merge_edited = _("The merge wants to edit the file '%s'.\n");

  phrases->we_deleted = _("You have deleted '%s' locally.\n"
                          "Maybe you renamed it?\n");

  phrases->we_edited = _("You have edited '%s' locally.\n");

  phrases->does_not_exist = _("The file '%s' does not exist locally\n"
                              "Maybe you renamed it?\n");
  return phrases;
}

static const char *
select_their_phrase(svn_wc_conflict_description_t *conflict,
                    struct tree_conflict_phrases *phrases)
{
  if (conflict->operation == svn_wc_operation_update)
    {
      switch (conflict->action)
        {
          case svn_wc_conflict_action_delete:
            return phrases->update_deleted;
          case svn_wc_conflict_action_edit:
            return phrases->update_edited;
          default:
            return NULL;
        }
    }
  else if (conflict->operation == svn_wc_operation_merge)
    {
      switch (conflict->action)
        {
          case svn_wc_conflict_action_delete:
            return phrases->merge_deleted;
          case svn_wc_conflict_action_edit:
            return phrases->merge_edited;
          default:
            return NULL;
        }
    }
  return NULL;
}

static const char *
select_our_phrase(svn_wc_conflict_description_t *conflict,
                  struct tree_conflict_phrases *phrases)
{
  switch (conflict->reason)
    {
      case svn_wc_conflict_reason_deleted:
        return phrases->we_deleted;
      case svn_wc_conflict_reason_edited:
        return phrases->we_edited;
      case svn_wc_conflict_reason_missing:
        return phrases->does_not_exist;
      default:
        return NULL;
    }
}

svn_error_t *
svn_wc_append_human_readable_tree_conflict_description(
                                       svn_stringbuf_t *descriptions,
                                       svn_wc_conflict_description_t *conflict,
                                       apr_pool_t *pool)
{
  const char *their_phrase, *our_phrase;
  svn_stringbuf_t *their_phrase_with_victim, *our_phrase_with_victim;
  struct tree_conflict_phrases *phrases = new_tree_conflict_phrases(pool);

  their_phrase = select_their_phrase(conflict, phrases);
  our_phrase = select_our_phrase(conflict, phrases);
  if (! our_phrase || ! their_phrase)
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("Invalid tree conflict data"));

  /* Substitute the '%s' format in the phrases with the victim path. */
  their_phrase_with_victim = svn_stringbuf_createf(pool, their_phrase,
                                                   conflict->victim_path);
  our_phrase_with_victim = svn_stringbuf_createf(pool, our_phrase,
                                                 conflict->victim_path);

  svn_stringbuf_appendstr(descriptions, their_phrase_with_victim);
  svn_stringbuf_appendstr(descriptions, our_phrase_with_victim);

  return SVN_NO_ERROR;
}

/* If **INPUT starts with *TOKEN, advance *INPUT by the length of *TOKEN. */
static svn_boolean_t
advance_on_match(char **input, const char *token)
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
                 char **start,
                 char *end,
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
            || **start != SVN_WC__TREE_CONFLICT_DESC_FIELD_SEPARATOR
            || **start != SVN_WC__TREE_CONFLICT_ESCAPE_CHAR)
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
               char **start,
               char *end)
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
               char **start,
               char *end)
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
            char **start,
            char *end)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  if (advance_on_match(start, SVN_WC__CONFLICT_ACTION_EDITED))
    conflict->action = svn_wc_conflict_action_edit;
  else if (advance_on_match(start, SVN_WC__CONFLICT_ACTION_DELETED))
    conflict->action = svn_wc_conflict_action_delete;
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
            char **start,
            char *end)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  if (advance_on_match(start, SVN_WC__CONFLICT_REASON_EDITED))
    conflict->reason = svn_wc_conflict_reason_edited;
  else if (advance_on_match(start, SVN_WC__CONFLICT_REASON_DELETED))
    conflict->reason = svn_wc_conflict_reason_deleted;
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
                       char **start,
                       char *end,
                       apr_pool_t *pool)
{
  if (*start >= end)
      return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
          _("Expected tree conflict data but got none"));

  *conflict = apr_pcalloc(pool, sizeof(svn_wc_conflict_description_t));

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
  char *start, *end;
  svn_wc_conflict_description_t *conflict = NULL;

  if (dir_entry->tree_conflict_data == NULL)
    {
      conflicts = NULL;
      return SVN_NO_ERROR;
    }

  assert(conflicts);

  start = (char*) dir_entry->tree_conflict_data;
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
                    const char c)
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
  char *path;
  int i, j, len;

  for (i = 0; i < conflicts->nelts; i++)
    {
      svn_wc_conflict_description_t *conflict =
          APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);

      path = (char *)conflict->victim_path;
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
          svn_stringbuf_appendbytes(buf, path, 1);
          path++;
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
  svn_wc_conflict_description_t *conflict;
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
svn_wc__add_tree_conflict_data(svn_stringbuf_t *log_accum,
                               svn_wc_conflict_description_t *conflict,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool)
{
  const char *dir_path;
  const svn_wc_entry_t *entry;
  apr_array_header_t *conflicts;
  svn_wc_entry_t tmp_entry;

  /* Retrieve the node path from adm_access. */
  dir_path = svn_wc_adm_access_path(adm_access);

  /* Make sure the node is a directory. */
  SVN_ERR(svn_wc_entry(&entry, dir_path, adm_access, TRUE, pool));
  assert(entry->kind == svn_node_dir);

  /* Get a list of existing tree conflicts. */
  conflicts = apr_array_make(pool, 0,
                             sizeof(svn_wc_conflict_description_t *));
  SVN_ERR(svn_wc_read_tree_conflicts_from_entry(conflicts, entry, pool));

  /* If CONFLICTS has a tree conflict with the same victim_path as the
   * new conflict, then the working copy has been corrupted.
   */
  if (svn_wc__tree_conflict_exists(conflicts, conflict->victim_path))
    return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
                            _("New tree conflict already exists"));

  /* Add the new tree conflict to CONFLICTS. */
  APR_ARRAY_PUSH(conflicts, svn_wc_conflict_description_t *) = conflict;

  /* Loggy write all the tree conflicts via a fresh temp entry. */
  SVN_ERR(svn_wc__write_tree_conflicts_to_entry(conflicts, &tmp_entry, pool));
  SVN_ERR(svn_wc__loggy_entry_modify(&log_accum,
                                     adm_access,
                                     dir_path,
                                     &tmp_entry,
                                     SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA,
                                     pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__is_tree_conflict_victim(svn_boolean_t *tree_conflict_victim,
                                const char *path,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool)
{
  /* Retrieve node path from adm_access. */

  /* Check whether node is a directory. If not, throw an error. */

  /* Get the entry for the directory. */

  /* If there is already a tree conflict victim with the same path
   * as the one we got as an argument, set tree_conflict_victim to
   * true, else set it to false */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__tree_conflict_resolved(const char* victim_path,
                               svn_wc_adm_access_t *adm_access,
                               apr_pool_t *pool)
{
  /* Make sure the node is a directory.
   * Otherwise we should not have been called. */

  /* In the dir entry, remove the tree conflict data for this victim. */

  return SVN_NO_ERROR;
}
