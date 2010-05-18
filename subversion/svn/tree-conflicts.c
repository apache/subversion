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
action_str(const svn_wc_conflict_description_t *conflict)
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
reason_str(const svn_wc_conflict_description_t *conflict)
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
  const char **desc,
  const svn_wc_conflict_description_t *conflict,
  apr_pool_t *pool)
{
  const char *victim_name, *action, *reason, *operation;
  victim_name = svn_path_basename(conflict->path, pool);
  reason = reason_str(conflict);
  action = action_str(conflict);
  operation = svn_cl__operation_str_human_readable(conflict->operation, pool);
  SVN_ERR_ASSERT(action && reason);
  *desc = apr_psprintf(pool, _("local %s, incoming %s upon %s"),
                       reason, action, operation);
  return SVN_NO_ERROR;
}


/* Helper for svn_cl__append_tree_conflict_info_xml().
 * Appends the attributes of the given VERSION to ATT_HASH.
 * SIDE is the content of the version tag's side="..." attribute,
 * currently one of "source-left" or "source-right".*/
static svn_error_t *
add_conflict_version_xml(svn_stringbuf_t **pstr,
                         const char *side,
                         svn_wc_conflict_version_t *version,
                         apr_pool_t *pool)
{
  apr_hash_t *att_hash = apr_hash_make(pool);


  apr_hash_set(att_hash, "side", APR_HASH_KEY_STRING, side);

  if (version->repos_url)
    apr_hash_set(att_hash, "repos-url", APR_HASH_KEY_STRING,
                 version->repos_url);

  if (version->path_in_repos)
    apr_hash_set(att_hash, "path-in-repos", APR_HASH_KEY_STRING,
                 version->path_in_repos);

  if (SVN_IS_VALID_REVNUM(version->peg_rev))
    apr_hash_set(att_hash, "revision", APR_HASH_KEY_STRING,
                 apr_itoa(pool, version->peg_rev));

  if (version->node_kind != svn_node_unknown)
    apr_hash_set(att_hash, "kind", APR_HASH_KEY_STRING,
                 svn_cl__node_kind_str_xml(version->node_kind));

  svn_xml_make_open_tag_hash(pstr, pool, svn_xml_self_closing,
                             "version", att_hash);
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

  apr_hash_set(att_hash, "kind", APR_HASH_KEY_STRING,
               svn_cl__node_kind_str_xml(conflict->node_kind));

  apr_hash_set(att_hash, "operation", APR_HASH_KEY_STRING,
               svn_cl__operation_str_xml(conflict->operation, pool));

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

  /* Open the tree-conflict tag. */
  svn_xml_make_open_tag_hash(&str, pool, svn_xml_normal,
                             "tree-conflict", att_hash);

  /* Add child tags for OLDER_VERSION and THEIR_VERSION. */

  if (conflict->src_left_version)
    SVN_ERR(add_conflict_version_xml(&str,
                                     "source-left",
                                     conflict->src_left_version,
                                     pool));

  if (conflict->src_right_version)
    SVN_ERR(add_conflict_version_xml(&str,
                                     "source-right",
                                     conflict->src_right_version,
                                     pool));

  svn_xml_make_close_tag(&str, pool, "tree-conflict");

  return SVN_NO_ERROR;
}

