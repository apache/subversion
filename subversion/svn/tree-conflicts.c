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
#include "svn_path.h"

#include "cl.h"

#include "svn_private_config.h"


struct tree_conflict_phrases
{
  const char *update_deleted;
  const char *update_edited;
  const char *update_added;
  const char *switch_deleted;
  const char *switch_edited;
  const char *switch_added;
  const char *merge_deleted;
  const char *merge_edited;
  const char *merge_added;
  const char *we_deleted;
  const char *we_added;
  const char *we_edited_update;
  const char *missing_update;
  const char *we_edited_merge;
  const char *we_added_merge;
  const char *missing_merge;
  const char *obstructed;
  const char *unversioned;
};

/* Return a new (possibly localised)
 * tree_conflict_phrases object allocated in POOL. */
static struct tree_conflict_phrases *
new_tree_conflict_phrases(apr_pool_t *pool)
{
  struct tree_conflict_phrases *phrases =
    apr_pcalloc(pool, sizeof(struct tree_conflict_phrases));

  phrases->update_deleted = _(
    "  The update attempted to delete '%s',\n"
    "  or attempted to rename it.\n");

  phrases->update_edited = _(
    "  The update attempted to edit '%s'.\n");

  phrases->update_added = _(
    "  The update attempted to add '%s'.\n");

  phrases->switch_deleted = _(
    "  The switch attempted to delete '%s',\n"
    "  or attempted to rename it.\n");

  phrases->switch_edited = _(
    "  The switch attempted to edit '%s'.\n");

  phrases->switch_added = _(
    "  The switch attempted to add '%s'.\n");

  phrases->merge_deleted = _(
    "  The merge attempted to delete '%s',\n"
    "  or attempted to rename it.\n");

  phrases->merge_edited = _(
    "  The merge attempted to edit '%s'.\n");

  phrases->merge_added = _(
    "  The merge attempted to add '%s'.\n");

  phrases->we_deleted = _(
    "  You have deleted '%s' locally.\n"
    "  Maybe you renamed it?\n");

  phrases->we_added = _(
    "  You have added '%s' locally.\n");

  phrases->we_edited_update = _(
    "  You have edited '%s' locally.\n");

  phrases->missing_update = _(
    "  '%s' does not exist locally.\n"
    "  Maybe you renamed it?\n");

  /* This one only comes up together with merge_deleted, never with
   * merge_edited. Otherwise we would have a text conflict. So we can
   * provide a more detailed hint here as to what might have happened. */
  phrases->we_edited_merge = _(
    "Either you have edited '%s' locally, or it has been edited in the\n"
    "history of the branch you are merging into, but those edits are not\n"
    "present on the branch you are merging from.\n");

  phrases->we_added_merge = _(
    "Either you have added '%s' locally, or it has been added in the\n"
    "history of the branch you are merging into.\n");

  phrases->missing_merge = _(
    "'%s' does not exist locally. Maybe you renamed it? Or has it been\n"
    "renamed in the history of the branch you are merging into?\n");

  phrases->obstructed = _(
    "This action was obstructed by an item in the working copy.\n");

  phrases->unversioned = _(
    "'%s' is unversioned.\n");

  return phrases;
}

static const char *
select_their_phrase(const svn_wc_conflict_description_t *conflict,
                    struct tree_conflict_phrases *phrases)
{
  if (conflict->operation == svn_wc_operation_update)
    {
      switch (conflict->action)
        {
          /* Order of cases follows definition of svn_wc_conflict_action_t. */
          case svn_wc_conflict_action_edit:
            return phrases->update_edited;
          case svn_wc_conflict_action_add:
            return phrases->update_added;
          case svn_wc_conflict_action_delete:
            return phrases->update_deleted;
        }
    }
  else if (conflict->operation == svn_wc_operation_switch)
    {
      switch (conflict->action)
        {
          /* Order of cases follows definition of svn_wc_conflict_action_t. */
          case svn_wc_conflict_action_edit:
            return phrases->switch_edited;
          case svn_wc_conflict_action_add:
            return phrases->switch_added;
          case svn_wc_conflict_action_delete:
            return phrases->switch_deleted;
        }
    }
  else if (conflict->operation == svn_wc_operation_merge)
    {
      switch (conflict->action)
        {
          /* Order of cases follows definition of svn_wc_conflict_action_t. */
          case svn_wc_conflict_action_edit:
            return phrases->merge_edited;
          case svn_wc_conflict_action_add:
            return phrases->merge_added;
          case svn_wc_conflict_action_delete:
            return phrases->merge_deleted;
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
      /* Order of cases follows definition of svn_wc_conflict_reason_t. */
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
        break;

      case svn_wc_conflict_reason_obstructed:
        return phrases->obstructed;
        break;

      case svn_wc_conflict_reason_deleted:
        return phrases->we_deleted;

      case svn_wc_conflict_reason_added:
        if (conflict->operation == svn_wc_operation_update
            || conflict->operation == svn_wc_operation_switch)
          {
            return phrases->we_added;
          }
        else if (conflict->operation == svn_wc_operation_merge)
          {
            return phrases->we_added_merge;
          }
        break;

      case svn_wc_conflict_reason_missing:
        if (conflict->operation == svn_wc_operation_update
            || conflict->operation == svn_wc_operation_switch)
          {
            return phrases->missing_update;
          }
        else if (conflict->operation == svn_wc_operation_merge)
          {
            return phrases->missing_merge;
          }
        break;

      case svn_wc_conflict_reason_unversioned:
        return phrases->unversioned;
    }
  return NULL; /* Should never happen! */
}

/* Return a string showing NODE's kind, URL and revision, to the extent that
 * that information is available in NODE. */
static const char *
node_description(const svn_wc_conflict_version_t *node,
                 apr_pool_t *pool)
{
  const char *url_str;

  /* Construct the whole URL if we can, else use whatever we have. */
  if (node->repos_url && node->path_in_repos)
    url_str = svn_path_url_add_component(node->repos_url,
                                         node->path_in_repos, pool);
  else if (node->repos_url)
    url_str = svn_path_url_add_component(node->repos_url, "...", pool);
  else if (node->path_in_repos)
    url_str = node->path_in_repos;
  else
    url_str = "...";

  return apr_psprintf(pool, "(%s) %s@%ld",
                      svn_cl__node_kind_str(node->node_kind),
                      url_str, node->peg_rev);
}

svn_error_t *
svn_cl__append_human_readable_tree_conflict_description(
  svn_stringbuf_t *descriptions,
  const svn_wc_conflict_description_t *conflict,
  apr_pool_t *pool)
{
  const char *victim_name, *their_phrase, *our_phrase;
  svn_stringbuf_t *their_phrase_with_victim, *our_phrase_with_victim;
  struct tree_conflict_phrases *phrases = new_tree_conflict_phrases(pool);
  const char *str;

  victim_name = svn_path_basename(conflict->path, pool);
  their_phrase = select_their_phrase(conflict, phrases);
  our_phrase = select_our_phrase(conflict, phrases);
  SVN_ERR_ASSERT(our_phrase && their_phrase);

  /* Substitute the '%s' format in the phrases with the victim path. */
  their_phrase_with_victim = svn_stringbuf_createf(pool, their_phrase,
                                                  victim_name);
  our_phrase_with_victim = svn_stringbuf_createf(pool, our_phrase,
                                                victim_name);

  svn_stringbuf_appendstr(descriptions, their_phrase_with_victim);
  svn_stringbuf_appendstr(descriptions, our_phrase_with_victim);

  str = apr_psprintf(pool, _("  Older version: %s\n"),
                     node_description(&conflict->older_version, pool));
  svn_stringbuf_appendcstr(descriptions, str);

  str = apr_psprintf(pool, _("  Their version: %s\n"),
                     node_description(&conflict->their_version, pool));
  svn_stringbuf_appendcstr(descriptions, str);

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
               svn_path_basename(conflict->path, pool));

  switch (conflict->node_kind)
    {
      case svn_node_dir:
        tmp = "dir";
        break;
      case svn_node_file:
        tmp = "file";
        break;
      default:
        SVN_ERR_MALFUNCTION();
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
        break;
      default:
        SVN_ERR_MALFUNCTION();
    }
  apr_hash_set(att_hash, "operation", APR_HASH_KEY_STRING, tmp);

  switch (conflict->action)
    {
      /* Order of cases follows definition of svn_wc_conflict_action_t. */
      case svn_wc_conflict_action_edit:
        tmp = "edited";
        break;
      case svn_wc_conflict_action_add:
        tmp = "added";
        break;
      case svn_wc_conflict_action_delete:
        tmp = "deleted";
        break;
      default:
        SVN_ERR_MALFUNCTION();
    }
  apr_hash_set(att_hash, "action", APR_HASH_KEY_STRING, tmp);

  switch (conflict->reason)
    {
      /* Order of cases follows definition of svn_wc_conflict_reason_t. */
      case svn_wc_conflict_reason_edited:
        tmp = "edited";
        break;
      case svn_wc_conflict_reason_obstructed:
        tmp = "obstructed";
        break;
      case svn_wc_conflict_reason_deleted:
        tmp = "deleted";
        break;
      case svn_wc_conflict_reason_added:
        tmp = "added";
        break;
      case svn_wc_conflict_reason_missing:
        tmp = "missing";
        break;
      case svn_wc_conflict_reason_unversioned:
        tmp = "unversioned";
        break;
      default:
        SVN_ERR_MALFUNCTION();
    }
  apr_hash_set(att_hash, "reason", APR_HASH_KEY_STRING, tmp);

  svn_xml_make_open_tag_hash(&str, pool, svn_xml_self_closing,
                             "tree-conflict", att_hash);

  return SVN_NO_ERROR;
}
