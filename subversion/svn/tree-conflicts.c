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

static const char *
select_action(const svn_wc_conflict_description_t *conflict)
{
  switch (conflict->action)
    {
      /* Order of cases follows definition of svn_wc_conflict_action_t. */
      case svn_wc_conflict_action_edit:
        return _("edit");
      case svn_wc_conflict_action_add:
        return _("add");
      case svn_wc_conflict_action_delete:
        return _("delete");
    }
  return NULL;
}

static const char *
select_reason(const svn_wc_conflict_description_t *conflict)
{
  switch (conflict->reason)
    {
      /* Order of cases follows definition of svn_wc_conflict_reason_t. */
      case svn_wc_conflict_reason_edited:
        return _("edit");
      case svn_wc_conflict_reason_obstructed:
        return _("obstruction");
      case svn_wc_conflict_reason_deleted:
        return _("delete");
      case svn_wc_conflict_reason_added:
        return _("add");
      case svn_wc_conflict_reason_missing:
        return _("missing");
      case svn_wc_conflict_reason_unversioned:
        return _("unversioned");
    }
  return NULL;
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
svn_cl__get_human_readable_tree_conflict_description(
  const char **desc,
  const svn_wc_conflict_description_t *conflict,
  apr_pool_t *pool)
{
<<<<<<< .working
  const char *victim_name, *their_phrase, *our_phrase;
  svn_stringbuf_t *their_phrase_with_victim, *our_phrase_with_victim;
  struct tree_conflict_phrases *phrases = new_tree_conflict_phrases(pool);
  const char *str;

=======
  const char *victim_name, *action, *reason;
>>>>>>> .merge-right.r34324
  victim_name = svn_path_basename(conflict->path, pool);
<<<<<<< .working
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

=======
  action = select_action(conflict);
  reason = select_reason(conflict);
  SVN_ERR_ASSERT(action && reason);
  *desc = apr_psprintf(pool, _("incoming %s, local %s"), action, reason);
>>>>>>> .merge-right.r34324
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
        tmp = "edit";
        break;
      case svn_wc_conflict_action_add:
        tmp = "add";
        break;
      case svn_wc_conflict_action_delete:
        tmp = "delete";
        break;
      default:
        SVN_ERR_MALFUNCTION();
    }
  apr_hash_set(att_hash, "action", APR_HASH_KEY_STRING, tmp);

  switch (conflict->reason)
    {
      /* Order of cases follows definition of svn_wc_conflict_reason_t. */
      case svn_wc_conflict_reason_edited:
        tmp = "edit";
        break;
      case svn_wc_conflict_reason_obstructed:
        tmp = "obstruction";
        break;
      case svn_wc_conflict_reason_deleted:
        tmp = "delete";
        break;
      case svn_wc_conflict_reason_added:
        tmp = "add";
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
