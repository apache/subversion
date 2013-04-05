/*
 * conflicts.c: Tree conflicts.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include "cl-conflicts.h"
#include "svn_hash.h"
#include "svn_xml.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "private/svn_token.h"

#include "cl.h"

#include "svn_private_config.h"


/* A map for svn_wc_conflict_action_t values to human-readable strings */
static const svn_token_map_t map_conflict_action_human[] =
{
  { N_("edit"),         svn_wc_conflict_action_edit },
  { N_("delete"),       svn_wc_conflict_action_delete },
  { N_("add"),          svn_wc_conflict_action_add },
  { N_("replace"),      svn_wc_conflict_action_replace },
  { NULL,               0 }
};

/* A map for svn_wc_conflict_action_t values to XML strings */
static const svn_token_map_t map_conflict_action_xml[] =
{
  { "edit",             svn_wc_conflict_action_edit },
  { "delete",           svn_wc_conflict_action_delete },
  { "add",              svn_wc_conflict_action_add },
  { "replace",          svn_wc_conflict_action_replace },
  { NULL,               0 }
};

/* A map for svn_wc_conflict_reason_t values to human-readable strings */
static const svn_token_map_t map_conflict_reason_human[] =
{
  { N_("edit"),         svn_wc_conflict_reason_edited },
  { N_("delete"),       svn_wc_conflict_reason_deleted },
  { N_("missing"),      svn_wc_conflict_reason_missing },
  { N_("obstruction"),  svn_wc_conflict_reason_obstructed },
  { N_("add"),          svn_wc_conflict_reason_added },
  { N_("replace"),      svn_wc_conflict_reason_replaced },
  { N_("unversioned"),  svn_wc_conflict_reason_unversioned },
  { N_("moved away"),   svn_wc_conflict_reason_moved_away },
  { N_("moved here"),   svn_wc_conflict_reason_moved_here },
  { NULL,               0 }
};

/* A map for svn_wc_conflict_reason_t values to XML strings */
static const svn_token_map_t map_conflict_reason_xml[] =
{
  { "edit",             svn_wc_conflict_reason_edited },
  { "delete",           svn_wc_conflict_reason_deleted },
  { "missing",          svn_wc_conflict_reason_missing },
  { "obstruction",      svn_wc_conflict_reason_obstructed },
  { "add",              svn_wc_conflict_reason_added },
  { "replace",          svn_wc_conflict_reason_replaced },
  { "unversioned",      svn_wc_conflict_reason_unversioned },
  { "moved-away",       svn_wc_conflict_reason_moved_away },
  { "moved-here",       svn_wc_conflict_reason_moved_here },
  { NULL,               0 }
};

static const svn_token_map_t map_conflict_kind_xml[] =
{
  { "text",             svn_wc_conflict_kind_text },
  { "property",         svn_wc_conflict_kind_property },
  { "tree",             svn_wc_conflict_kind_tree },
  { NULL,               0 }
};

/* Return a localized string representation of CONFLICT->action. */
static const char *
action_str(const svn_wc_conflict_description2_t *conflict)
{
  return _(svn_token__to_word(map_conflict_action_human, conflict->action));
}

/* Return a localized string representation of CONFLICT->reason. */
static const char *
reason_str(const svn_wc_conflict_description2_t *conflict)
{
  return _(svn_token__to_word(map_conflict_reason_human, conflict->reason));
}

svn_error_t *
svn_cl__get_human_readable_tree_conflict_description(
  const char **desc,
  const svn_wc_conflict_description2_t *conflict,
  apr_pool_t *pool)
{
  const char *action, *reason, *operation;
  svn_node_kind_t incoming_kind;

  reason = reason_str(conflict);
  action = action_str(conflict);
  operation = svn_cl__operation_str_human_readable(conflict->operation, pool);

  /* Determine the node kind of the incoming change. */
  incoming_kind = svn_node_unknown;
  if (conflict->action == svn_wc_conflict_action_edit ||
      conflict->action == svn_wc_conflict_action_delete)
    {
      /* Change is acting on 'src_left' version of the node. */
      if (conflict->src_left_version)
        incoming_kind = conflict->src_left_version->node_kind;
    }
  else if (conflict->action == svn_wc_conflict_action_add ||
           conflict->action == svn_wc_conflict_action_replace)
    {
      /* Change is acting on 'src_right' version of the node.
       *
       * ### For 'replace', the node kind is ambiguous. However, src_left
       * ### is NULL for replace, so we must use src_right. */
      if (conflict->src_right_version)
        incoming_kind = conflict->src_right_version->node_kind;
    }

  SVN_ERR_ASSERT(action && reason);
  *desc = apr_psprintf(pool, _("local %s %s, incoming %s %s upon %s"),
                       svn_node_kind_to_word(conflict->node_kind),
                       reason,
                       svn_node_kind_to_word(incoming_kind),
                       action,
                       operation);
  return SVN_NO_ERROR;
}


/* Helper for svn_cl__append_tree_conflict_info_xml().
 * Appends the attributes of the given VERSION to ATT_HASH.
 * SIDE is the content of the version tag's side="..." attribute,
 * currently one of "source-left" or "source-right".*/
static svn_error_t *
add_conflict_version_xml(svn_stringbuf_t **pstr,
                         const char *side,
                         const svn_wc_conflict_version_t *version,
                         apr_pool_t *pool)
{
  apr_hash_t *att_hash = apr_hash_make(pool);


  svn_hash_sets(att_hash, "side", side);

  if (version->repos_url)
    svn_hash_sets(att_hash, "repos-url", version->repos_url);

  if (version->path_in_repos)
    svn_hash_sets(att_hash, "path-in-repos", version->path_in_repos);

  if (SVN_IS_VALID_REVNUM(version->peg_rev))
    svn_hash_sets(att_hash, "revision", apr_ltoa(pool, version->peg_rev));

  if (version->node_kind != svn_node_unknown)
    svn_hash_sets(att_hash, "kind",
                  svn_cl__node_kind_str_xml(version->node_kind));

  svn_xml_make_open_tag_hash(pstr, pool, svn_xml_self_closing,
                             "version", att_hash);
  return SVN_NO_ERROR;
}


static svn_error_t *
append_tree_conflict_info_xml(svn_stringbuf_t *str,
                              const svn_wc_conflict_description2_t *conflict,
                              apr_pool_t *pool)
{
  apr_hash_t *att_hash = apr_hash_make(pool);
  const char *tmp;

  svn_hash_sets(att_hash, "victim",
                svn_dirent_basename(conflict->local_abspath, pool));

  svn_hash_sets(att_hash, "kind",
                svn_cl__node_kind_str_xml(conflict->node_kind));

  svn_hash_sets(att_hash, "operation",
                svn_cl__operation_str_xml(conflict->operation, pool));

  tmp = svn_token__to_word(map_conflict_action_xml, conflict->action);
  svn_hash_sets(att_hash, "action", tmp);

  tmp = svn_token__to_word(map_conflict_reason_xml, conflict->reason);
  svn_hash_sets(att_hash, "reason", tmp);

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

svn_error_t *
svn_cl__append_conflict_info_xml(svn_stringbuf_t *str,
                                 const svn_wc_conflict_description2_t *conflict,
                                 apr_pool_t *scratch_pool)
{
  apr_hash_t *att_hash;
  const char *kind;
  if (conflict->kind == svn_wc_conflict_kind_tree)
    {
      /* Uses other element type */
      return svn_error_trace(
                append_tree_conflict_info_xml(str, conflict, scratch_pool));
    }

  att_hash = apr_hash_make(scratch_pool);

  svn_hash_sets(att_hash, "operation",
                svn_cl__operation_str_xml(conflict->operation, scratch_pool));


  kind = svn_token__to_word(map_conflict_kind_xml, conflict->kind);
  svn_hash_sets(att_hash, "type", kind);

  svn_hash_sets(att_hash, "operation",
                svn_cl__operation_str_xml(conflict->operation, scratch_pool));


  /* "<conflict>" */
  svn_xml_make_open_tag_hash(&str, scratch_pool,
                             svn_xml_normal, "conflict", att_hash);

  if (conflict->src_left_version)
    SVN_ERR(add_conflict_version_xml(&str,
                                     "source-left",
                                     conflict->src_left_version,
                                     scratch_pool));

  if (conflict->src_right_version)
    SVN_ERR(add_conflict_version_xml(&str,
                                     "source-right",
                                     conflict->src_right_version,
                                     scratch_pool));

  switch (conflict->kind)
    {
      case svn_wc_conflict_kind_text:
        /* "<prev-base-file> xx </prev-base-file>" */
        svn_cl__xml_tagged_cdata(&str, scratch_pool, "prev-base-file",
                                 conflict->base_abspath);

        /* "<prev-wc-file> xx </prev-wc-file>" */
        svn_cl__xml_tagged_cdata(&str, scratch_pool, "prev-wc-file",
                                 conflict->my_abspath);

        /* "<cur-base-file> xx </cur-base-file>" */
        svn_cl__xml_tagged_cdata(&str, scratch_pool, "cur-base-file",
                                 conflict->their_abspath);

        break;

      case svn_wc_conflict_kind_property:
        /* "<prop-file> xx </prop-file>" */
        svn_cl__xml_tagged_cdata(&str, scratch_pool, "prop-file",
                                 conflict->their_abspath);
        break;

      default:
      case svn_wc_conflict_kind_tree:
        SVN_ERR_MALFUNCTION(); /* Handled separately */
        break;
    }

  /* "</conflict>" */
  svn_xml_make_close_tag(&str, scratch_pool, "conflict");

  return SVN_NO_ERROR;
}
