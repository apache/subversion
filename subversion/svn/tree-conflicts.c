/*
 * tree-conflicts.c: Tree conflicts.
 *
 * ====================================================================
 * Copyright (c) 2007-2008 CollabNet.  All rights reserved.
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

#include "tree-conflicts.h"
#include "svn_xml.h"

#include "svn_private_config.h"


struct tree_conflict_phrases
{
  const char *update_deleted;
  const char *update_edited;
  const char *merge_deleted;
  const char *merge_edited;
  const char *merge_added;
  const char *we_deleted;

  /* Used during update. */
  const char *we_edited_update;
  const char *does_not_exist_update;

  /* Used during merge. */
  const char *we_edited_merge;
  const char *we_added_merge;
  const char *does_not_exist_merge;
  const char *obstructed;
};

/* Return a new (possibly localised)
 * tree_conflict_phrases object allocated in POOL. */
static struct tree_conflict_phrases *
new_tree_conflict_phrases(apr_pool_t *pool)
{
  struct tree_conflict_phrases *phrases =
    apr_pcalloc(pool, sizeof(struct tree_conflict_phrases));

  phrases->update_deleted = _("The update attempted to delete '%s'\n"
                              "(possibly as part of a rename operation).\n");

  phrases->update_edited = _("The update attempted to edit '%s'.\n");

  phrases->merge_deleted = _("The merge attempted to delete '%s'\n"
                             "(possibly as part of a rename operation).\n");

  phrases->merge_edited = _("The merge attempted to edit '%s'.\n");

  phrases->merge_added = _("The merge attempted to add '%s'.\n");

  phrases->we_deleted = _("You have deleted '%s' locally.\n"
                          "Maybe you renamed it?\n");

  phrases->we_edited_update = _("You have edited '%s' locally.\n");

  phrases->does_not_exist_update = _("'%s' does not exist locally.\n"
                                     "Maybe you renamed it?\n");

  /* This one only comes up together with merge_deleted, never with
   * merge_edited. Otherwise we would have a text conflict. So we can
   * provide a more detailed hint here as to what might have happened. */
  phrases->we_edited_merge = _("Either you have edited '%s' locally,\n"
                               "or it has been edited in the history of"
                               " the branch you are merging into,\n"
                               "but those edits are not present on the"
                               " branch you are merging from.\n");

  phrases->we_added_merge = _("Either you have added '%s' locally,\n"
                               "or it has been added in the history of"
                               " the branch you are merging into.\n");


  phrases->does_not_exist_merge = _("'%s' does not exist locally.\n"
                                    "Maybe you renamed it? Or has it been"
                                    " renamed in the history of the branch\n"
                                    "you are merging into?\n");

  phrases->obstructed = _("This action was obstructed by an item"
                           " in the working copy.\n");
  return phrases;
}

static const char *
select_their_phrase(const svn_wc_conflict_description_t *conflict,
                    struct tree_conflict_phrases *phrases)
{
  if (conflict->operation == svn_wc_operation_update
      || conflict->operation == svn_wc_operation_switch)
    {
      switch (conflict->action)
        {
          case svn_wc_conflict_action_delete:
            return phrases->update_deleted;
          case svn_wc_conflict_action_edit:
            return phrases->update_edited;
          default:
            return NULL; /* Should never happen! */
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
          case svn_wc_conflict_action_add:
            return phrases->merge_added;
          default:
            return NULL; /* Should never happen! */
        }
    }
  return NULL; /* Should never happen! */
}

static const char *
select_our_phrase(const svn_wc_conflict_description_t *conflict,
                  struct tree_conflict_phrases *phrases)
{
  switch (conflict->reason)
    {
      case svn_wc_conflict_reason_deleted:
        return phrases->we_deleted;

      case svn_wc_conflict_reason_edited:
        if (conflict->operation == svn_wc_operation_update
            || conflict->operation == svn_wc_operation_switch)
          {
            return phrases->we_edited_update;
          }
        else if (conflict->operation == svn_wc_operation_merge)
          {
            return phrases->we_edited_merge;
          }
        return NULL; /* Should never happen! */

      case svn_wc_conflict_reason_missing:
        if (conflict->operation == svn_wc_operation_update
            || conflict->operation == svn_wc_operation_switch)
          {
            return phrases->does_not_exist_update;
          }
        else if (conflict->operation == svn_wc_operation_merge)
          {
            return phrases->does_not_exist_merge;
          }
        return NULL; /* Should never happen! */

      case svn_wc_conflict_reason_obstructed:
        if (conflict->operation == svn_wc_operation_update
            || conflict->operation == svn_wc_operation_switch)
          {
            return NULL; /* Should never happen! */
          }
        else if (conflict->operation == svn_wc_operation_merge)
          {
            return phrases->obstructed;
          }
        return NULL; /* Should never happen! */

      case svn_wc_conflict_reason_added:
        if (conflict->operation == svn_wc_operation_update
            || conflict->operation == svn_wc_operation_switch)
          {
            return NULL; /* Should never happen! */
          }
        else if (conflict->operation == svn_wc_operation_merge)
          {
            return phrases->merge_added;
          }
        return NULL; /* Should never happen! */

     default:
        return NULL; /* Should never happen! */
    }
}

svn_error_t *
svn_cl__append_human_readable_tree_conflict_description(
  svn_stringbuf_t *descriptions,
  const svn_wc_conflict_description_t *conflict,
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


svn_error_t *
svn_cl__append_tree_conflict_info_xml(
  svn_stringbuf_t *str,
  const svn_wc_conflict_description_t *conflict,
  apr_pool_t *pool)
{
  apr_hash_t *att_hash = apr_hash_make(pool);
  const char *tmp;

  apr_hash_set(att_hash, "victim", APR_HASH_KEY_STRING,
               conflict->victim_path);

  switch (conflict->node_kind)
    {
      case svn_node_dir:
        tmp = "dir";
        break;
      case svn_node_file:
        tmp = "file";
        break;
      default:
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("Bad node_kind in tree conflict description"));
    }
  apr_hash_set(att_hash, "kind", APR_HASH_KEY_STRING, tmp);

  switch (conflict->operation)
    {
      case svn_wc_operation_update:
        tmp = "update";
        break;
      case svn_wc_operation_switch:
        tmp = "switch";
        break;
      case svn_wc_operation_merge:
        tmp = "merge";
      default:
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("Bad operation in tree conflict description"));
    }
  apr_hash_set(att_hash, "operation", APR_HASH_KEY_STRING, tmp);

  switch (conflict->action)
    {
      case svn_wc_conflict_action_edit:
        tmp = "edited";
        break;
      case svn_wc_conflict_action_delete:
        tmp = "deleted";
        break;
      default:
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("Bad action in tree conflict description"));
    }
  apr_hash_set(att_hash, "action", APR_HASH_KEY_STRING, tmp);

  switch (conflict->reason)
    {
      case svn_wc_conflict_reason_edited:
        tmp = "edited";
        break;
      case svn_wc_conflict_reason_deleted:
        tmp = "deleted";
        break;
      case svn_wc_conflict_reason_missing:
        tmp = "missing";
        break;
      case svn_wc_conflict_reason_obstructed:
        tmp = "obstructed";
        break;
      case svn_wc_conflict_reason_added:
        tmp = "added";
      default:
        return svn_error_create(SVN_ERR_WC_CORRUPT, NULL,
            _("Bad reason in tree conflict description"));
    }
  apr_hash_set(att_hash, "reason", APR_HASH_KEY_STRING, tmp);

  svn_xml_make_open_tag_hash(&str, pool, svn_xml_self_closing,
                             "tree-conflict", att_hash);

  return SVN_NO_ERROR;
}
