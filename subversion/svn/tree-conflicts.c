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

svn_error_t *
svn_cl__get_human_readable_tree_conflict_description(
  svn_string_t **desc,
  const svn_wc_conflict_description_t *conflict,
  apr_pool_t *pool)
{
  const char *victim_name, *action, *reason;
  victim_name = svn_path_basename(conflict->path, pool);
  action = select_action(conflict);
  reason = select_reason(conflict);
  SVN_ERR_ASSERT(action && reason);
  *desc = svn_string_createf(pool, _("incoming %s, local %s"),
                             action, reason);
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
