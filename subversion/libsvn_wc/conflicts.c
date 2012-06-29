/*
 * conflicts.c: routines for managing conflict data.
 *            NOTE: this code doesn't know where the conflict is
 *            actually stored.
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



#include <string.h>

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include <apr_errno.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_diff.h"

#include "wc.h"
#include "wc_db.h"
#include "conflicts.h"
#include "workqueue.h"
#include "props.h"

#include "private/svn_wc_private.h"
#include "private/svn_skel.h"
#include "private/svn_string_private.h"

#include "svn_private_config.h"

/* --------------------------------------------------------------------
 * Conflict skel management
 */

svn_skel_t *
svn_wc__conflict_skel_create(apr_pool_t *result_pool)
{
  svn_skel_t *conflict_skel = svn_skel__make_empty_list(result_pool);

  /* Add empty CONFLICTS list */
  svn_skel__prepend(svn_skel__make_empty_list(result_pool), conflict_skel);

  /* Add empty WHY list */
  svn_skel__prepend(svn_skel__make_empty_list(result_pool), conflict_skel);

  return conflict_skel;
}

svn_error_t *
svn_wc__conflict_skel_is_complete(svn_boolean_t *complete,
                                  const svn_skel_t *conflict_skel)
{
  *complete = FALSE;

  if (svn_skel__list_length(conflict_skel) < 2)
    return svn_error_create(SVN_ERR_INCOMPLETE_DATA, NULL,
                            _("Not a conflict skel"));

  if (svn_skel__list_length(conflict_skel->children) < 2)
    return SVN_NO_ERROR; /* WHY is not set */

  if (svn_skel__list_length(conflict_skel->children->next) == 0)
    return SVN_NO_ERROR; /* No conflict set */

  *complete = TRUE;
  return SVN_NO_ERROR;
}

/* Serialize a svn_wc_conflict_version_t before the existing data in skel */
static svn_error_t *
conflict__prepend_location(svn_skel_t *skel,
                           const svn_wc_conflict_version_t *location,
                           svn_boolean_t allow_NULL,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_skel_t *loc;
  SVN_ERR_ASSERT(location || allow_NULL);

  if (!location)
    {
      svn_skel__prepend(svn_skel__make_empty_list(result_pool), skel);
      return SVN_NO_ERROR;
    }

  /* ("subversion" repos_root_url repos_uuid repos_relpath rev kind) */
  loc = svn_skel__make_empty_list(result_pool);

  svn_skel__prepend_str(svn_node_kind_to_word(location->node_kind),
                        loc, result_pool);

  svn_skel__prepend_int(location->peg_rev, loc, result_pool);

  svn_skel__prepend_str(apr_pstrdup(result_pool, location->path_in_repos), loc,
                        result_pool);

  if (!location->repos_uuid) /* Can theoretically be NULL */
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), loc);
  else
    svn_skel__prepend_str(location->repos_uuid, loc, result_pool);

  svn_skel__prepend_str(apr_pstrdup(result_pool, location->repos_url), loc,
                        result_pool);

  svn_skel__prepend_str(SVN_WC__CONFLICT_SRC_SUBVERSION, loc, result_pool);

  svn_skel__prepend(loc, skel);
  return SVN_NO_ERROR;
}

/* Get the operation part of CONFLICT_SKELL or NULL if no operation is set
   at this time */
static svn_error_t *
conflict__get_operation(const svn_skel_t **why,
                        const svn_skel_t *conflict_skel)
{
  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  *why = conflict_skel->children;

  if (!(*why)->children)
    *why = NULL; /* Operation is not set yet */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__conflict_skel_set_op_update(svn_skel_t *conflict_skel,
                                    const svn_wc_conflict_version_t *original,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *why;
  svn_skel_t *origins;

  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  SVN_ERR(conflict__get_operation(&why, conflict_skel));

  SVN_ERR_ASSERT(why == NULL); /* No operation set */

  why = conflict_skel->children;

  origins = svn_skel__make_empty_list(result_pool);

  SVN_ERR(conflict__prepend_location(origins, original, TRUE,
                                     result_pool, scratch_pool));

  svn_skel__prepend(origins, why);
  svn_skel__prepend_str(SVN_WC__CONFLICT_OP_UPDATE, why, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_set_op_switch(svn_skel_t *conflict_skel,
                                    const svn_wc_conflict_version_t *original,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_skel_t *why;
  svn_skel_t *origins;

  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  SVN_ERR(conflict__get_operation(&why, conflict_skel));

  SVN_ERR_ASSERT(why == NULL); /* No operation set */

  why = conflict_skel->children;

  origins = svn_skel__make_empty_list(result_pool);

  SVN_ERR(conflict__prepend_location(origins, original, TRUE,
                                     result_pool, scratch_pool));

  svn_skel__prepend(origins, why);
  svn_skel__prepend_str(SVN_WC__CONFLICT_OP_SWITCH, why, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_set_op_merge(svn_skel_t *conflict_skel,
                                   const svn_wc_conflict_version_t *left,
                                   const svn_wc_conflict_version_t *right,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_skel_t *why;
  svn_skel_t *origins;

  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  SVN_ERR(conflict__get_operation(&why, conflict_skel));

  SVN_ERR_ASSERT(why == NULL); /* No operation set */

  why = conflict_skel->children;

  origins = svn_skel__make_empty_list(result_pool);

  SVN_ERR(conflict__prepend_location(origins, right, TRUE,
                                     result_pool, scratch_pool));

  SVN_ERR(conflict__prepend_location(origins, left, TRUE,
                                     result_pool, scratch_pool));

  svn_skel__prepend(origins, why);
  svn_skel__prepend_str(SVN_WC__CONFLICT_OP_MERGE, why, result_pool);

  return SVN_NO_ERROR;
}

/* Gets the conflict data of the specified type CONFLICT_TYPE from
   CONFLICT_SKEL, or NULL if no such conflict is recorded */
static svn_error_t *
conflict__get_conflict(const svn_skel_t **conflict,
                       const svn_skel_t *conflict_skel,
                       const char *conflict_type)
{
  svn_skel_t *c;
  apr_size_t len;
  SVN_ERR_ASSERT(conflict_skel
                 && conflict_skel->children
                 && conflict_skel->children->next
                 && !conflict_skel->children->next->is_atom);

  len = strlen(conflict_type);

  for(c = conflict_skel->children->next->children;
      c;
      c = c->next)
    {
      if (svn_skel__matches_atom(c->children, conflict_type))
        {
          *conflict = c;
          return SVN_NO_ERROR;
        }
    }

  *conflict = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_add_text_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        const char *mine_abspath,
                                        const char *their_old_abspath,
                                        const char *their_abspath,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_skel_t *text_conflict;
  svn_skel_t *markers;

  SVN_ERR(conflict__get_conflict(&text_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TEXT));

  SVN_ERR_ASSERT(!text_conflict); /* ### Use proper error? */

  /* Current skel format
     ("text"
      (OLD MINE OLD-THEIRS THEIRS)) */

  text_conflict = svn_skel__make_empty_list(result_pool);
  markers = svn_skel__make_empty_list(result_pool);

if (their_abspath)
    {
      const char *their_relpath;

      SVN_ERR(svn_wc__db_to_relpath(&their_relpath,
                                    db, wri_abspath, their_abspath,
                                    result_pool, scratch_pool));
      svn_skel__prepend_str(their_relpath, markers, result_pool);
    }
  else
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);

  if (mine_abspath)
    {
      const char *mine_relpath;

      SVN_ERR(svn_wc__db_to_relpath(&mine_relpath,
                                    db, wri_abspath, mine_abspath,
                                    result_pool, scratch_pool));
      svn_skel__prepend_str(mine_relpath, markers, result_pool);
    }
  else
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);

  if (their_old_abspath)
    {
      const char *original_relpath;

      SVN_ERR(svn_wc__db_to_relpath(&original_relpath,
                                    db, wri_abspath, their_old_abspath,
                                    result_pool, scratch_pool));
      svn_skel__prepend_str(original_relpath, markers, result_pool);
    }
  else
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);

  svn_skel__prepend(markers, text_conflict);
  svn_skel__prepend_str(SVN_WC__CONFLICT_KIND_TEXT, text_conflict,
                        result_pool);

  /* And add it to the conflict skel */
  svn_skel__prepend(text_conflict, conflict_skel->children->next);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_skel_add_prop_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        const char *marker_abspath,
                                        apr_hash_t *mine_props,
                                        apr_hash_t *their_old_props,
                                        apr_hash_t *their_props,
                                        apr_hash_t *conflicted_prop_names,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_skel_t *prop_conflict;
  svn_skel_t *props;
  svn_skel_t *conflict_names;
  svn_skel_t *markers;
  apr_hash_index_t *hi;

  SVN_ERR(conflict__get_conflict(&prop_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_PROP));

  SVN_ERR_ASSERT(!prop_conflict); /* ### Use proper error? */

  /* This function currently implements:
  /* ("prop"
      ("marker_relpath")
      prop-conflicted_prop_names
      old-props
      mine-props
      their-props)
     NULL lists are recorded as "" */

  prop_conflict = svn_skel__make_empty_list(result_pool);

  if (their_props)
    {
      SVN_ERR(svn_skel__unparse_proplist(&props, their_props, result_pool));
      svn_skel__prepend(props, prop_conflict);
    }
  else
    svn_skel__prepend_str("", prop_conflict, result_pool); /* No their_props */

  if (mine_props)
    {
      SVN_ERR(svn_skel__unparse_proplist(&props, mine_props, result_pool));
      svn_skel__prepend(props, prop_conflict);
    }
  else
    svn_skel__prepend_str("", prop_conflict, result_pool); /* No mine_props */

  if (their_old_props)
    {
      SVN_ERR(svn_skel__unparse_proplist(&props, their_old_props,
                                         result_pool));
      svn_skel__prepend(props, prop_conflict);
    }
  else
    svn_skel__prepend_str("", prop_conflict, result_pool); /* No old_props */

  conflict_names = svn_skel__make_empty_list(result_pool);
  for (hi = apr_hash_first(scratch_pool, conflicted_prop_names);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_skel__prepend_str(apr_pstrdup(result_pool,
                                        svn__apr_hash_index_key(hi)),
                            conflict_names,
                            result_pool);
    }
  svn_skel__prepend(conflict_names, prop_conflict);

  markers = svn_skel__make_empty_list(result_pool);

  if (marker_abspath)
    {
      const char *marker_relpath;
      SVN_ERR(svn_wc__db_to_relpath(&marker_relpath, db, wri_abspath,
                                    marker_abspath,
                                    result_pool, scratch_pool));

      svn_skel__prepend_str(marker_relpath, markers, result_pool);
    }
/*else // ### set via svn_wc__conflict_create_markers
    svn_skel__prepend(svn_skel__make_empty_list(result_pool), markers);*/

  svn_skel__prepend(markers, prop_conflict);

  svn_skel__prepend_str(SVN_WC__CONFLICT_KIND_PROP, prop_conflict, result_pool);

  /* And add it to the conflict skel */
  svn_skel__prepend(prop_conflict, conflict_skel->children->next);

  return SVN_NO_ERROR;
}

/* A map for svn_wc_operation_t values. */
static const svn_token_map_t operation_map[] =
{
  { "",   svn_wc_operation_none },
  { SVN_WC__CONFLICT_OP_UPDATE, svn_wc_operation_update },
  { SVN_WC__CONFLICT_OP_SWITCH, svn_wc_operation_switch },
  { SVN_WC__CONFLICT_OP_MERGE,  svn_wc_operation_merge },
  { NULL }
};

svn_error_t *
svn_wc__conflict_read_info(svn_wc_operation_t *operation,
                           const apr_array_header_t **locations,
                           svn_wc__db_t *db,
                           const char *wri_abspath,
                           const svn_skel_t *conflict_skel,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_skel_t *op;

  SVN_ERR(conflict__get_operation(&op, conflict_skel));

  if (!op)
    return svn_error_create(SVN_ERR_INCOMPLETE_DATA, NULL,
                            _("Not a completed conflict skel"));

  if (operation)
    {
      svn_skel_t *what = op->children;

      int value = svn_token__from_mem(operation_map, what->data, what->len);

      if (value != SVN_TOKEN_UNKNOWN)
        *operation = value;
      else
        *operation = svn_wc_operation_none;
    }
  if (locations)
    *locations = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__conflict_read_text_conflict(const char **mine_abspath,
                                    const char **their_old_abspath,
                                    const char **their_abspath,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  const svn_skel_t *text_conflict;
  svn_skel_t *m;

  SVN_ERR(conflict__get_conflict(&text_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TEXT));

  if (!text_conflict)
    return svn_error_create(SVN_ERR_WC_MISSING, NULL, _("Conflict not set"));

  m = text_conflict->children->next->children;

  if (their_old_abspath)
    {
      if (m->is_atom)
        {
          const char *original_relpath;

          original_relpath = apr_pstrndup(scratch_pool, m->data, m->len);
          SVN_ERR(svn_wc__db_from_relpath(their_old_abspath,
                                          db, wri_abspath, original_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *their_old_abspath = NULL;
    }
  m = m->next;

  if (mine_abspath)
    {
      if (m->is_atom)
        {
          const char *mine_relpath;

          mine_relpath = apr_pstrndup(scratch_pool, m->data, m->len);
          SVN_ERR(svn_wc__db_from_relpath(mine_abspath,
                                          db, wri_abspath, mine_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *mine_abspath = NULL;
    }
  m = m->next;

  if (their_abspath)
    {
      if (m->is_atom)
        {
          const char *their_relpath;

          their_relpath = apr_pstrndup(scratch_pool, m->data, m->len);
          SVN_ERR(svn_wc__db_from_relpath(their_abspath,
                                          db, wri_abspath, their_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *their_abspath = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_read_prop_conflict(const char **marker_abspath,
                                    apr_hash_t **mine_props,
                                    apr_hash_t **their_old_props,
                                    apr_hash_t **their_props,
                                    apr_hash_t **conflicted_prop_names,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  const svn_skel_t *prop_conflict;
  svn_skel_t *c;

  SVN_ERR(conflict__get_conflict(&prop_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_PROP));

  if (!prop_conflict)
    return svn_error_create(SVN_ERR_WC_MISSING, NULL, _("Conflict not set"));

  c = prop_conflict->children;

  c = c->next; /* Skip "prop" */

  /* Get marker file */
  if (marker_abspath)
    {
      const char *marker_relpath;

      if (c->children && c->children->is_atom)
        {
          marker_relpath = apr_pstrndup(result_pool, c->children->data,
                                        c->children->len);

          SVN_ERR(svn_wc__db_from_relpath(marker_abspath, db, wri_abspath,
                                          marker_relpath,
                                          result_pool, scratch_pool));
        }
      else
        *marker_abspath = NULL;
    }
  c = c->next;

  /* Get conflicted properties */
  if (conflicted_prop_names)
    {
      svn_skel_t *name;
      *conflicted_prop_names = apr_hash_make(result_pool);

      for (name = c->children; name; name = name->next)
        {
          apr_hash_set(*conflicted_prop_names,
                       apr_pstrndup(result_pool, name->data, name->len),
                       APR_HASH_KEY_STRING,
                       "");
        }
    }
  c = c->next;

  /* Get original properties */
  if (their_old_props)
    {
      if (c->is_atom)
        *their_old_props = apr_hash_make(result_pool);
      else
        SVN_ERR(svn_skel__parse_proplist(their_old_props, c, result_pool));
    }
  c = c->next;

  /* Get mine properties */
  if (mine_props)
    {
      if (c->is_atom)
        *mine_props = apr_hash_make(result_pool);
      else
        SVN_ERR(svn_skel__parse_proplist(mine_props, c, result_pool));
    }
  c = c->next;

  /* Get their properties */
  if (their_props)
    {
      if (c->is_atom)
        *their_props = apr_hash_make(result_pool);
      else
        SVN_ERR(svn_skel__parse_proplist(their_props, c, result_pool));
    }
  c = c->next;

  return SVN_NO_ERROR;
}

/* --------------------------------------------------------------------
 */
/* Helper for svn_wc__conflict_create_markers */
static svn_skel_t *
prop_conflict_skel_new(apr_pool_t *result_pool)
{
  svn_skel_t *operation = svn_skel__make_empty_list(result_pool);
  svn_skel_t *result = svn_skel__make_empty_list(result_pool);

  svn_skel__prepend(operation, result);
  return result;
}


/* Helper for prop_conflict_skel_add */
static void
prepend_prop_value(const svn_string_t *value,
                   svn_skel_t *skel,
                   apr_pool_t *result_pool)
{
  svn_skel_t *value_skel = svn_skel__make_empty_list(result_pool);

  if (value != NULL)
    {
      const void *dup = apr_pmemdup(result_pool, value->data, value->len);

      svn_skel__prepend(svn_skel__mem_atom(dup, value->len, result_pool),
                        value_skel);
    }

  svn_skel__prepend(value_skel, skel);
}


/* Helper for svn_wc__conflict_create_markers */
static svn_error_t *
prop_conflict_skel_add(
  svn_skel_t *skel,
  const char *prop_name,
  const svn_string_t *original_value,
  const svn_string_t *mine_value,
  const svn_string_t *incoming_value,
  const svn_string_t *incoming_base_value,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  svn_skel_t *prop_skel = svn_skel__make_empty_list(result_pool);

  /* ### check that OPERATION has been filled in.  */

  /* See notes/wc-ng/conflict-storage  */
  prepend_prop_value(incoming_base_value, prop_skel, result_pool);
  prepend_prop_value(incoming_value, prop_skel, result_pool);
  prepend_prop_value(mine_value, prop_skel, result_pool);
  prepend_prop_value(original_value, prop_skel, result_pool);
  svn_skel__prepend_str(apr_pstrdup(result_pool, prop_name), prop_skel,
                        result_pool);
  svn_skel__prepend_str(SVN_WC__CONFLICT_KIND_PROP, prop_skel, result_pool);

  /* Now we append PROP_SKEL to the end of the provided conflict SKEL.  */
  svn_skel__append(skel, prop_skel);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflict_create_markers(svn_skel_t **work_items,
                                svn_wc__db_t *db,
                                const char *local_abspath,
                                svn_skel_t *conflict_skel,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_skel_t *prop_conflict;
  svn_skel_t *text_conflict;
  *work_items = NULL;

  SVN_ERR(conflict__get_conflict(&prop_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_PROP));

  if (prop_conflict)
    {
      /* Ok, currently we have to do a few things for property conflicts:
         - Create a marker file
         - Create a WQ item that sets the marker name
         - Create a WQ item that fills the marker with the expected data

         This can be simplified once we really store conflict_skel in wc.db */

      const char *marker_abspath;
      const char *marker_relpath;

      /* ### as the legacy code, check if we already have a prejfile.

         ### Triggered by merge_tests.py 90 on a double property merge.
         ### Needs further review as we will probably loose the original
         ### conflict by overwriting. (Legacy issue)  */
      SVN_ERR(svn_wc__get_prejfile_abspath(&marker_abspath, db, local_abspath,
                                           scratch_pool, scratch_pool));

      if (! marker_abspath)
        {
          svn_node_kind_t kind;
          const char *marker_dir;
          const char *marker_name;

          SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));

          if (kind == svn_node_dir)
            {
              marker_dir = local_abspath;
              marker_name = SVN_WC__THIS_DIR_PREJ;
            }
          else
            svn_dirent_split(&marker_dir, &marker_name, local_abspath,
                             scratch_pool);

          SVN_ERR(svn_io_open_uniquely_named(NULL, &marker_abspath,
                                             marker_dir,
                                             marker_name,
                                             SVN_WC__PROP_REJ_EXT,
                                             svn_io_file_del_none,
                                             scratch_pool, scratch_pool));
        }

      SVN_ERR(svn_wc__db_to_relpath(&marker_relpath, db, local_abspath,
                                    marker_abspath, result_pool, result_pool));

      /* And store the marker in the skel */
      svn_skel__prepend_str(marker_relpath, prop_conflict->children->next,
                            result_pool);

      /* Store the data in the WQ item in the same format used as 1.7.
         Once we store the data in DB it is easier to just read it back
         from the workqueue */
      {
        svn_skel_t *prop_data;
        apr_hash_index_t *hi;
        apr_hash_t *old_props;
        apr_hash_t *mine_props;
        apr_hash_t *their_original_props;
        apr_hash_t *their_props;
        apr_hash_t *conflicted_props;
        svn_wc_operation_t operation;

        SVN_ERR(svn_wc__conflict_read_prop_conflict(NULL,
                                                    &mine_props,
                                                    &their_original_props,
                                                    &their_props,
                                                    &conflicted_props,
                                                    db, local_abspath,
                                                    conflict_skel,
                                                    scratch_pool,
                                                    scratch_pool));

        SVN_ERR(svn_wc__conflict_read_info(&operation, NULL,
                                           db, local_abspath,
                                           conflict_skel,
                                           scratch_pool, scratch_pool));

        if (operation == svn_wc_operation_merge)
          SVN_ERR(svn_wc__db_read_pristine_props(&old_props, db, local_abspath,
                                                 scratch_pool, scratch_pool));
        else
          old_props = their_props;

        prop_data = prop_conflict_skel_new(result_pool);

        for (hi = apr_hash_first(scratch_pool, conflicted_props);
             hi;
             hi = apr_hash_next(hi))
          {
            const char *propname = svn__apr_hash_index_key(hi);

            prop_conflict_skel_add(
                            prop_data, propname,
                            old_props
                                    ? apr_hash_get(old_props, propname,
                                                   APR_HASH_KEY_STRING)
                                    : NULL,
                            mine_props
                                    ? apr_hash_get(mine_props, propname,
                                                   APR_HASH_KEY_STRING)
                                    : NULL,
                            their_props
                                    ? apr_hash_get(their_props, propname,
                                                   APR_HASH_KEY_STRING)
                                      : NULL,
                            their_original_props
                                    ? apr_hash_get(their_original_props, propname,
                                                   APR_HASH_KEY_STRING)
                                      : NULL,
                            result_pool, scratch_pool);
          }

        SVN_ERR(svn_wc__wq_build_prej_install(work_items,
                                              db, local_abspath,
                                              prop_data,
                                              scratch_pool, scratch_pool));
      }
    }

  SVN_ERR(conflict__get_conflict(&text_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TEXT));

  if (text_conflict)
    {
      const char *mine_abspath;
      const char *their_original_abspath;
      const char *their_abspath;

      SVN_ERR(svn_wc__conflict_read_text_conflict(&mine_abspath,
                                                  &their_original_abspath,
                                                  &their_abspath,
                                                  db, local_abspath,
                                                  conflict_skel,
                                                  scratch_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Helper function for the three apply_* functions below, used when
 * merging properties together.
 *
 * Given property PROPNAME on LOCAL_ABSPATH, and four possible property
 * values, generate four tmpfiles and pass them to CONFLICT_FUNC callback.
 * This gives the client an opportunity to interactively resolve the
 * property conflict.
 *
 * BASE_VAL/WORKING_VAL represent the current state of the working
 * copy, and INCOMING_OLD_VAL/INCOMING_NEW_VAL represents the incoming
 * propchange.  Any of these values might be NULL, indicating either
 * non-existence or intent-to-delete.
 *
 * If the callback isn't available, or if it responds with
 * 'choose_postpone', then set *CONFLICT_REMAINS to TRUE and return.
 *
 * If the callback responds with a choice of 'base', 'theirs', 'mine',
 * or 'merged', then install the proper value into ACTUAL_PROPS and
 * set *CONFLICT_REMAINS to FALSE.
 */
static svn_error_t *
generate_propconflict(svn_boolean_t *conflict_remains,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      const svn_wc_conflict_version_t *left_version,
                      const svn_wc_conflict_version_t *right_version,
                      const char *propname,
                      const svn_string_t *base_val,
                      const svn_string_t *working_val,
                      const svn_string_t *incoming_old_val,
                      const svn_string_t *incoming_new_val,
                      svn_wc_conflict_resolver_func2_t conflict_func,
                      void *conflict_baton,
                      apr_pool_t *scratch_pool)
{
  svn_wc_conflict_result_t *result = NULL;
  svn_wc_conflict_description2_t *cdesc;
  const char *dirpath = svn_dirent_dirname(local_abspath, scratch_pool);
  svn_kind_t kind;
  const svn_string_t *new_value = NULL;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, FALSE, FALSE,
                               scratch_pool));

  cdesc = svn_wc_conflict_description_create_prop2(
                local_abspath,
                (kind == svn_kind_dir) ? svn_node_dir : svn_node_file,
                propname, scratch_pool);

  cdesc->src_left_version = left_version;
  cdesc->src_right_version = right_version;

  /* Create a tmpfile for each of the string_t's we've got.  */
  if (working_val)
    {
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, working_val->data,
                                  working_val->len,
                                  svn_io_file_del_on_pool_cleanup,
                                  scratch_pool));
      cdesc->my_abspath = svn_dirent_join(dirpath, file_name, scratch_pool);
    }

  if (incoming_new_val)
    {
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, incoming_new_val->data,
                                  incoming_new_val->len,
                                  svn_io_file_del_on_pool_cleanup,
                                  scratch_pool));
      cdesc->their_abspath = svn_dirent_join(dirpath, file_name, scratch_pool);
    }

  if (!base_val && !incoming_old_val)
    {
      /* If base and old are both NULL, then that's fine, we just let
         base_file stay NULL as-is.  Both agents are attempting to add a
         new property.  */
    }

  else if ((base_val && !incoming_old_val)
           || (!base_val && incoming_old_val))
    {
      /* If only one of base and old are defined, then we've got a
         situation where one agent is attempting to add the property
         for the first time, and the other agent is changing a
         property it thinks already exists.  In this case, we return
         whichever older-value happens to be defined, so that the
         conflict-callback can still attempt a 3-way merge. */

      const svn_string_t *conflict_base_val = base_val ? base_val
                                                       : incoming_old_val;
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath,
                                  conflict_base_val->data,
                                  conflict_base_val->len,
                                  svn_io_file_del_on_pool_cleanup,
                                  scratch_pool));
      cdesc->base_abspath = svn_dirent_join(dirpath, file_name, scratch_pool);
    }

  else  /* base and old are both non-NULL */
    {
      const svn_string_t *conflict_base_val;
      const char *file_name;

      if (! svn_string_compare(base_val, incoming_old_val))
        {
          /* What happens if 'base' and 'old' don't match up?  In an
             ideal situation, they would.  But if they don't, this is
             a classic example of a patch 'hunk' failing to apply due
             to a lack of context.  For example: imagine that the user
             is busy changing the property from a value of "cat" to
             "dog", but the incoming propchange wants to change the
             same property value from "red" to "green".  Total context
             mismatch.

             HOWEVER: we can still pass one of the two base values as
             'base_file' to the callback anyway.  It's still useful to
             present the working and new values to the user to
             compare. */

          if (working_val && svn_string_compare(base_val, working_val))
            conflict_base_val = incoming_old_val;
          else
            conflict_base_val = base_val;
        }
      else
        {
          conflict_base_val = base_val;
        }

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, conflict_base_val->data,
                                  conflict_base_val->len,
                                  svn_io_file_del_on_pool_cleanup, scratch_pool));
      cdesc->base_abspath = svn_dirent_join(dirpath, file_name, scratch_pool);

      if (working_val && incoming_new_val)
        {
          svn_stream_t *mergestream;
          svn_diff_t *diff;
          svn_diff_file_options_t *options =
            svn_diff_file_options_create(scratch_pool);

          SVN_ERR(svn_stream_open_unique(&mergestream, &cdesc->merged_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));
          SVN_ERR(svn_diff_mem_string_diff3(&diff, conflict_base_val,
                                            working_val,
                                            incoming_new_val, options, scratch_pool));
          SVN_ERR(svn_diff_mem_string_output_merge2
                  (mergestream, diff, conflict_base_val, working_val,
                   incoming_new_val, NULL, NULL, NULL, NULL,
                   svn_diff_conflict_display_modified_latest, scratch_pool));
          SVN_ERR(svn_stream_close(mergestream));
        }
    }

  if (!incoming_old_val && incoming_new_val)
    cdesc->action = svn_wc_conflict_action_add;
  else if (incoming_old_val && !incoming_new_val)
    cdesc->action = svn_wc_conflict_action_delete;
  else
    cdesc->action = svn_wc_conflict_action_edit;

  if (base_val && !working_val)
    cdesc->reason = svn_wc_conflict_reason_deleted;
  else if (!base_val && working_val)
    cdesc->reason = svn_wc_conflict_reason_obstructed;
  else
    cdesc->reason = svn_wc_conflict_reason_edited;

  /* Invoke the interactive conflict callback. */
  {
    SVN_ERR(conflict_func(&result, cdesc, conflict_baton, scratch_pool,
                          scratch_pool));
  }
  if (result == NULL)
    {
      *conflict_remains = TRUE;
      return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                              NULL, _("Conflict callback violated API:"
                                      " returned no results."));
    }


  switch (result->choice)
    {
      default:
      case svn_wc_conflict_choose_postpone:
        {
          *conflict_remains = TRUE;
          break;
        }
      case svn_wc_conflict_choose_mine_full:
        {
          /* No need to change actual_props; it already contains working_val */
          *conflict_remains = FALSE;
          new_value = working_val;
          break;
        }
      /* I think _mine_full and _theirs_full are appropriate for prop
         behavior as well as the text behavior.  There should even be
         analogous behaviors for _mine and _theirs when those are
         ready, namely: fold in all non-conflicting prop changes, and
         then choose _mine side or _theirs side for conflicting ones. */
      case svn_wc_conflict_choose_theirs_full:
        {
          *conflict_remains = FALSE;
          new_value = incoming_new_val;
          break;
        }
      case svn_wc_conflict_choose_base:
        {
          *conflict_remains = FALSE;
          new_value = base_val;
          break;
        }
      case svn_wc_conflict_choose_merged:
        {
          if (!cdesc->merged_file && !result->merged_file)
            return svn_error_create
                (SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                 NULL, _("Conflict callback violated API:"
                         " returned no merged file."));
          else
            {
              svn_stringbuf_t *merged_stringbuf;
              svn_string_t *merged_string;

              SVN_ERR(svn_stringbuf_from_file2(&merged_stringbuf,
                                               result->merged_file ?
                                                    result->merged_file :
                                                    cdesc->merged_file,
                                               scratch_pool));
              merged_string = svn_stringbuf__morph_into_string(merged_stringbuf);
              *conflict_remains = FALSE;
              new_value = merged_string;
            }
          break;
        }
    }

  if (!*conflict_remains)
    {
      apr_hash_t *props;

      /* For now, just set the property values. This should really do some of the
         more advanced things from svn_wc_prop_set() */

      SVN_ERR(svn_wc__db_read_props(&props, db, local_abspath, scratch_pool,
                                    scratch_pool));

      apr_hash_set(props, propname, APR_HASH_KEY_STRING, new_value);

      SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, props,
                                      FALSE, NULL, NULL,
                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Deal with the result of the conflict resolution callback, as indicated by
 * CHOICE.
 *
 * Set *WORK_ITEMS to new work items that will ...
 * Set *MERGE_OUTCOME to the result of the 3-way merge.
 *
 * LEFT_ABSPATH, RIGHT_ABSPATH, and TARGET_ABSPATH are the input files to
 * the 3-way merge, and MERGED_FILE is the merged result as generated by the
 * internal or external merge or by the conflict resolution callback.
 *
 * DETRANSLATED_TARGET is the detranslated version of TARGET_ABSPATH
 * (see detranslate_wc_file() above).  DIFF3_OPTIONS are passed to the
 * diff3 implementation in case a 3-way merge has to be carried out. */
static svn_error_t*
eval_text_conflict_func_result(svn_skel_t **work_items,
                               enum svn_wc_merge_outcome_t *merge_outcome,
                               svn_wc_conflict_choice_t choice,
                               const apr_array_header_t *merge_options,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               const char *left_abspath,
                               const char *right_abspath,
                               const char *merged_file,
                               const char *detranslated_target,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  const char *install_from = NULL;
  svn_boolean_t remove_source = FALSE;

  *work_items = NULL;

  switch (choice)
    {
      /* If the callback wants to use one of the fulltexts
         to resolve the conflict, so be it.*/
      case svn_wc_conflict_choose_base:
        {
          install_from = left_abspath;
          *merge_outcome = svn_wc_merge_merged;
          break;
        }
      case svn_wc_conflict_choose_theirs_full:
        {
          install_from = right_abspath;
          *merge_outcome = svn_wc_merge_merged;
          break;
        }
      case svn_wc_conflict_choose_mine_full:
        {
          /* Do nothing to merge_target, let it live untouched! */
          *merge_outcome = svn_wc_merge_merged;
          return SVN_NO_ERROR;
        }
      case svn_wc_conflict_choose_theirs_conflict:
      case svn_wc_conflict_choose_mine_conflict:
        {
          const char *chosen_path;
          const char *temp_dir;
          svn_stream_t *chosen_stream;
          svn_diff_t *diff;
          svn_diff_conflict_display_style_t style;
          svn_diff_file_options_t *diff3_options;

          diff3_options = svn_diff_file_options_create(scratch_pool);

          if (merge_options)
             SVN_ERR(svn_diff_file_options_parse(diff3_options,
                                                 merge_options,
                                                 scratch_pool));

          style = choice == svn_wc_conflict_choose_theirs_conflict
                    ? svn_diff_conflict_display_latest
                    : svn_diff_conflict_display_modified;

          SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir, db,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_open_unique(&chosen_stream, &chosen_path,
                                         temp_dir, svn_io_file_del_none,
                                         scratch_pool, scratch_pool));

          SVN_ERR(svn_diff_file_diff3_2(&diff,
                                        left_abspath,
                                        detranslated_target, right_abspath,
                                        diff3_options, scratch_pool));
          SVN_ERR(svn_diff_file_output_merge2(chosen_stream, diff,
                                              left_abspath,
                                              detranslated_target,
                                              right_abspath,
                                              /* markers ignored */
                                              NULL, NULL,
                                              NULL, NULL,
                                              style,
                                              scratch_pool));
          SVN_ERR(svn_stream_close(chosen_stream));

          install_from = chosen_path;
          remove_source = TRUE;
          *merge_outcome = svn_wc_merge_merged;
          break;
        }

        /* For the case of 3-way file merging, we don't
           really distinguish between these return values;
           if the callback claims to have "generally
           resolved" the situation, we still interpret
           that as "OK, we'll assume the merged version is
           good to use". */
      case svn_wc_conflict_choose_merged:
        {
          install_from = merged_file;
          *merge_outcome = svn_wc_merge_merged;
          break;
        }
      case svn_wc_conflict_choose_postpone:
      default:
        {
#if 0
          /* ### what should this value be? no caller appears to initialize
             ### it, so we really SHOULD be setting a value here.  */
          *merge_outcome = svn_wc_merge_merged;
#endif

          /* Assume conflict remains. */
          return SVN_NO_ERROR;
        }
    }

  SVN_ERR_ASSERT(install_from != NULL);

  {
    svn_skel_t *work_item;

    SVN_ERR(svn_wc__wq_build_file_install(&work_item,
                                          db, local_abspath,
                                          install_from,
                                          FALSE /* use_commit_times */,
                                          FALSE /* record_fileinfo */,
                                          result_pool, scratch_pool));
    *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

    if (remove_source)
      {
        SVN_ERR(svn_wc__wq_build_file_remove(&work_item,
                                             db, install_from,
                                             result_pool, scratch_pool));
        *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);
      }
  }

  return SVN_NO_ERROR;
}

/* Helper for maybe_resolve_conflicts() below. */
static const svn_wc_conflict_description2_t *
setup_text_conflict_desc(const char *left_abspath,
                         const char *right_abspath,
                         const char *target_abspath,
                         const svn_wc_conflict_version_t *left_version,
                         const svn_wc_conflict_version_t *right_version,
                         const char *result_target,
                         const char *detranslated_target,
                         const char *mimeprop,
                         svn_boolean_t is_binary,
                         apr_pool_t *pool)
{
  svn_wc_conflict_description2_t *cdesc;

  cdesc = svn_wc_conflict_description_create_text2(target_abspath, pool);
  cdesc->is_binary = is_binary;
  cdesc->mime_type = mimeprop;
  cdesc->base_abspath = left_abspath;
  cdesc->their_abspath = right_abspath;
  cdesc->my_abspath = detranslated_target;
  cdesc->merged_file = result_target;

  cdesc->src_left_version = left_version;
  cdesc->src_right_version = right_version;

  return cdesc;
}

/* Create a new file in the same directory as VERSIONED_ABSPATH, with the
   same basename as VERSIONED_ABSPATH, with a ".edited" extension, and set
   *WORK_ITEM to a new work item that will copy and translate from the file
   SOURCE to that new file.  It will be translated from repository-normal
   form to working-copy form according to the versioned properties of
   VERSIONED_ABSPATH that are current when the work item is executed.

   DB should have a write lock for the directory containing SOURCE.

   Allocate *WORK_ITEM in RESULT_POOL. */
static svn_error_t*
save_merge_result(svn_skel_t **work_item,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  const char *source,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *edited_copy_abspath;
  const char *dir_abspath;
  const char *filename;

  svn_dirent_split(&dir_abspath, &filename, local_abspath, scratch_pool);

  /* ### Should use preserved-conflict-file-exts. */
  /* Create the .edited file within this file's DIR_ABSPATH  */
  SVN_ERR(svn_io_open_uniquely_named(NULL,
                                     &edited_copy_abspath,
                                     dir_abspath,
                                     filename,
                                     ".edited",
                                     svn_io_file_del_none,
                                     scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__wq_build_file_copy_translated(work_item,
                                                db, local_abspath,
                                                source, edited_copy_abspath,
                                                result_pool, scratch_pool));

  return SVN_NO_ERROR;
}


/* XXX Insane amount of parameters... */
/* RESULT_TARGET is the path to the merged file produced by the internal or
   external 3-way merge. */
static svn_error_t*
resolve_text_conflicts(svn_skel_t **work_items,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       const apr_array_header_t *merge_options,
                       const char *left_abspath,
                       const char *right_abspath,
                       enum svn_wc_merge_outcome_t *merge_outcome,
                       const svn_wc_conflict_version_t *left_version,
                       const svn_wc_conflict_version_t *right_version,
                       const char *result_target,
                       const char *detranslated_target,
                       svn_wc_conflict_resolver_func2_t conflict_func,
                       void *conflict_baton,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_wc_conflict_result_t *result;
  svn_skel_t *work_item;
  const svn_wc_conflict_description2_t *cdesc;
  apr_hash_t *props;

  *work_items = NULL;

  /* Give the conflict resolution callback a chance to clean
     up the conflicts before we mark the file 'conflicted' */

    SVN_ERR(svn_wc__db_read_props(&props, db, local_abspath,
                                scratch_pool, scratch_pool));

    cdesc = setup_text_conflict_desc(left_abspath,
                                    right_abspath,
                                    local_abspath,
                                    left_version,
                                    right_version,
                                    result_target,
                                    detranslated_target,
                                    svn_prop_get_value(props,
                                                        SVN_PROP_MIME_TYPE),
                                    FALSE,
                                    scratch_pool);

    SVN_ERR(conflict_func(&result, cdesc, conflict_baton, scratch_pool,
                        scratch_pool));
    if (result == NULL)
    return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                            NULL, _("Conflict callback violated API:"
                                    " returned no results"));

    if (result->save_merged)
    {
        SVN_ERR(save_merge_result(work_items,
                                  db, local_abspath,
                                  /* Look for callback's own
                                      merged-file first: */
                                  result->merged_file
                                      ? result->merged_file
                                      : result_target,
                                  result_pool, scratch_pool));
    }

  SVN_ERR(eval_text_conflict_func_result(&work_item,
                                         merge_outcome,
                                         result->choice,
                                         merge_options,
                                         db, local_abspath,
                                         left_abspath,
                                         right_abspath,
                                         result->merged_file
                                           ? result->merged_file
                                           : result_target,
                                         detranslated_target,
                                         result_pool, scratch_pool));
  *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

  if (result->choice != svn_wc_conflict_choose_postpone)
    /* The conflicts have been dealt with, nothing else
     * to do for us here. */
    return SVN_NO_ERROR;

  /* The conflicts have not been dealt with. */
  *merge_outcome = svn_wc_merge_conflict;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__conflict_invoke_resolver(svn_wc__db_t *db,
                                 const char *local_abspath,
                                 const svn_skel_t *conflict_skel,
                                 const apr_array_header_t *merge_options,
                                 svn_wc_conflict_resolver_func2_t resolver_func,
                                 void *resolver_baton,
                                 apr_pool_t *scratch_pool)
{
  svn_skel_t *prop_conflict;
  svn_skel_t *text_conflict;

  SVN_ERR(conflict__get_conflict(&prop_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_PROP));

  /* Quick and dirty compatibility wrapper. My guess would be that most resolvers
     would want to look at all properties at the same time.

     ### svn currently only invokes this from the merge code to collect the list of
     ### conflicted paths. Eventually this code will be the base for 'svn resolve'
     ### and at that time the test coverage will improve
     */
  if (prop_conflict)
    {
      apr_hash_t *old_props;
      apr_hash_t *mine_props;
      apr_hash_t *their_props;
      apr_hash_t *old_their_props;
      apr_hash_t *conflicted;
      apr_pool_t *iterpool;
      apr_hash_index_t *hi;
      svn_boolean_t mark_resolved = TRUE;
      svn_wc_operation_t operation;

      SVN_ERR(svn_wc__conflict_read_prop_conflict(NULL,
                                                  &mine_props,
                                                  &old_their_props,
                                                  &their_props,
                                                  &conflicted,
                                                  db, local_abspath,
                                                  conflict_skel,
                                                  scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__conflict_read_info(&operation, NULL,
                                         db, local_abspath,
                                         conflict_skel,
                                         scratch_pool, scratch_pool));

      if (operation == svn_wc_operation_merge)
        SVN_ERR(svn_wc__db_read_pristine_props(&old_props, db, local_abspath,
                                               scratch_pool, scratch_pool));
      else
        old_props = their_props;

      iterpool = svn_pool_create(scratch_pool);

      for (hi = apr_hash_first(scratch_pool, conflicted);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *propname = svn__apr_hash_index_key(hi);
          svn_boolean_t conflict_remains = TRUE;

          svn_pool_clear(iterpool);

          SVN_ERR(generate_propconflict(&conflict_remains,
                                        db, local_abspath,
                                        NULL, NULL, propname,
                                        old_props
                                          ? apr_hash_get(old_props, propname,
                                                         APR_HASH_KEY_STRING)
                                          : NULL,
                                        mine_props
                                          ? apr_hash_get(mine_props, propname,
                                                         APR_HASH_KEY_STRING)
                                          : NULL,
                                        old_their_props
                                          ? apr_hash_get(old_their_props, propname,
                                                         APR_HASH_KEY_STRING)
                                          : NULL,
                                        their_props
                                          ? apr_hash_get(their_props, propname,
                                                         APR_HASH_KEY_STRING)
                                          : NULL,
                                        resolver_func, resolver_baton,
                                        iterpool));

          if (conflict_remains)
            mark_resolved = FALSE;
        }

      if (mark_resolved)
        SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath, FALSE, TRUE,
                                            FALSE, NULL, scratch_pool));
    }

  SVN_ERR(conflict__get_conflict(&text_conflict, conflict_skel,
                                 SVN_WC__CONFLICT_KIND_TEXT));

  if (text_conflict)
    {
      const char *mine_abspath;
      const char *their_original_abspath;
      const char *their_abspath;
      svn_skel_t *work_item = NULL;
      svn_wc_merge_outcome_t merge_outcome = svn_wc_merge_conflict;

      SVN_ERR(svn_wc__conflict_read_text_conflict(&their_original_abspath,
                                                  &mine_abspath,
                                                  &their_abspath,
                                                  db, local_abspath,
                                                  conflict_skel,
                                                  scratch_pool, scratch_pool));

      SVN_ERR(resolve_text_conflicts(&work_item, db, local_abspath,
                                     merge_options,
                                     their_original_abspath, their_abspath,
                                     &merge_outcome,
                                     NULL /* left_version */,
                                     NULL /* right_version */,
                                     local_abspath,
                                     mine_abspath,
                                     resolver_func, resolver_baton,
                                     NULL, NULL,
                                     scratch_pool, scratch_pool));

      if (merge_outcome == svn_wc_merge_merged)
        {
          SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath, TRUE, FALSE,
                                              FALSE, work_item, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


/*** Resolving a conflict automatically ***/

/* Conflict resolution involves removing the conflict files, if they exist,
   and clearing the conflict filenames from the entry.  The latter needs to
   be done whether or not the conflict files exist.

   PATH is the path to the item to be resolved, BASE_NAME is the basename
   of PATH, and CONFLICT_DIR is the access baton for PATH.  ORIG_ENTRY is
   the entry prior to resolution. RESOLVE_TEXT and RESOLVE_PROPS are TRUE
   if text and property conflicts respectively are to be resolved.

   If this call marks any conflict as resolved, set *DID_RESOLVE to true,
   else do not change *DID_RESOLVE.

   See svn_wc_resolved_conflict5() for how CONFLICT_CHOICE behaves.
*/
static svn_error_t *
resolve_conflict_on_node(svn_boolean_t *did_resolve,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_boolean_t resolve_text,
                         svn_boolean_t resolve_props,
                         svn_boolean_t resolve_tree,
                         svn_wc_conflict_choice_t conflict_choice,
                         svn_cancel_func_t cancel_func_t,
                         void *cancel_baton,
                         apr_pool_t *pool)
{
  const char *conflict_old = NULL;
  const char *conflict_new = NULL;
  const char *conflict_working = NULL;
  const char *prop_reject_file = NULL;
  int i;
  const apr_array_header_t *conflicts;
  svn_skel_t *work_items = NULL;
  svn_skel_t *work_item;

  *did_resolve = FALSE;

  SVN_ERR(svn_wc__db_read_conflicts(&conflicts, db, local_abspath,
                                    pool, pool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *desc;

      desc = APR_ARRAY_IDX(conflicts, i,
                           const svn_wc_conflict_description2_t*);

      if (desc->kind == svn_wc_conflict_kind_text)
        {
          conflict_old = desc->base_abspath;
          conflict_new = desc->their_abspath;
          conflict_working = desc->my_abspath;
        }
      else if (desc->kind == svn_wc_conflict_kind_property)
        prop_reject_file = desc->their_abspath;
    }

  if (resolve_text)
    {
      const char *auto_resolve_src;

      /* Handle automatic conflict resolution before the temporary files are
       * deleted, if necessary. */
      switch (conflict_choice)
        {
        case svn_wc_conflict_choose_base:
          auto_resolve_src = conflict_old;
          break;
        case svn_wc_conflict_choose_mine_full:
          auto_resolve_src = conflict_working;
          break;
        case svn_wc_conflict_choose_theirs_full:
          auto_resolve_src = conflict_new;
          break;
        case svn_wc_conflict_choose_merged:
          auto_resolve_src = NULL;
          break;
        case svn_wc_conflict_choose_theirs_conflict:
        case svn_wc_conflict_choose_mine_conflict:
          {
            if (conflict_old && conflict_working && conflict_new)
              {
                const char *temp_dir;
                svn_stream_t *tmp_stream = NULL;
                svn_diff_t *diff;
                svn_diff_conflict_display_style_t style =
                  conflict_choice == svn_wc_conflict_choose_theirs_conflict
                  ? svn_diff_conflict_display_latest
                  : svn_diff_conflict_display_modified;

                SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir, db,
                                                       local_abspath,
                                                       pool, pool));
                SVN_ERR(svn_stream_open_unique(&tmp_stream,
                                               &auto_resolve_src,
                                               temp_dir,
                                               svn_io_file_del_on_pool_cleanup,
                                               pool, pool));

                SVN_ERR(svn_diff_file_diff3_2(&diff,
                                              conflict_old,
                                              conflict_working,
                                              conflict_new,
                                              svn_diff_file_options_create(pool),
                                              pool));
                SVN_ERR(svn_diff_file_output_merge2(tmp_stream, diff,
                                                    conflict_old,
                                                    conflict_working,
                                                    conflict_new,
                                                    /* markers ignored */
                                                    NULL, NULL, NULL, NULL,
                                                    style,
                                                    pool));
                SVN_ERR(svn_stream_close(tmp_stream));
              }
            else
              auto_resolve_src = NULL;
            break;
          }
        default:
          return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                  _("Invalid 'conflict_result' argument"));
        }

      if (auto_resolve_src)
        {
          SVN_ERR(svn_wc__wq_build_file_copy_translated(
                    &work_item, db, local_abspath,
                    auto_resolve_src, local_abspath, pool, pool));
          work_items = svn_wc__wq_merge(work_items, work_item, pool);
        }
    }

  if (resolve_text)
    {
      svn_node_kind_t node_kind;

      /* Legacy behavior: Only report text conflicts as resolved when at least
         one conflict marker file exists.

         If not the UI shows the conflict as already resolved
         (and in this case we just remove the in-db conflict) */

      if (conflict_old)
        {
          SVN_ERR(svn_io_check_path(conflict_old, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   conflict_old,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }

      if (conflict_new)
        {
          SVN_ERR(svn_io_check_path(conflict_new, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   conflict_new,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }

      if (conflict_working)
        {
          SVN_ERR(svn_io_check_path(conflict_working, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   conflict_working,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }
    }
  if (resolve_props)
    {
      svn_node_kind_t node_kind;

      /* Legacy behavior: Only report property conflicts as resolved when the
         property reject file exists

         If not the UI shows the conflict as already resolved
         (and in this case we just remove the in-db conflict) */

      if (prop_reject_file)
        {
          SVN_ERR(svn_io_check_path(prop_reject_file, &node_kind, pool));
          if (node_kind == svn_node_file)
            {
              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   prop_reject_file,
                                                   pool, pool));
              work_items = svn_wc__wq_merge(work_items, work_item, pool);
              *did_resolve = TRUE;
            }
        }
    }
  if (resolve_tree)
    *did_resolve = TRUE;

  if (resolve_text || resolve_props || resolve_tree)
    {
      SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath,
                                          resolve_text, resolve_props,
                                          resolve_tree, work_items, pool));

      /* Run the work queue to remove conflict marker files. */
      SVN_ERR(svn_wc__wq_run(db, local_abspath,
                             cancel_func_t, cancel_baton,
                             pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__resolve_text_conflict(svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool)
{
  svn_boolean_t ignored_result;

  return svn_error_trace(resolve_conflict_on_node(
                           &ignored_result,
                           db, local_abspath,
                           TRUE /* resolve_text */,
                           FALSE /* resolve_props */,
                           FALSE /* resolve_tree */,
                           svn_wc_conflict_choose_merged,
                           NULL, NULL, /* cancel_func */
                           scratch_pool));
}


/* Baton for conflict_status_walker */
struct conflict_status_walker_baton
{
  svn_wc__db_t *db;
  svn_boolean_t resolve_text;
  const char *resolve_prop;
  svn_boolean_t resolve_tree;
  svn_wc_conflict_choice_t conflict_choice;
  svn_wc_conflict_resolver_func2_t conflict_func;
  void *conflict_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

/* Implements svn_wc_status4_t to walk all conflicts to resolve */
static svn_error_t *
conflict_status_walker(void *baton,
                       const char *local_abspath,
                       const svn_wc_status3_t *status,
                       apr_pool_t *scratch_pool)
{
  struct conflict_status_walker_baton *cswb = baton;
  svn_wc__db_t *db = cswb->db;

  const apr_array_header_t *conflicts;
  apr_pool_t *iterpool;
  int i;
  svn_boolean_t resolved = FALSE;

  if (!status->conflicted)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_wc__db_read_conflicts(&conflicts, db, local_abspath,
                                    scratch_pool, iterpool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *cd;
      svn_boolean_t did_resolve;
      svn_wc_conflict_choice_t my_choice = cswb->conflict_choice;

      cd = APR_ARRAY_IDX(conflicts, i, const svn_wc_conflict_description2_t *);

      svn_pool_clear(iterpool);

      if (my_choice == svn_wc_conflict_choose_unspecified)
        {
          svn_wc_conflict_result_t *result;

          if (!cswb->conflict_func)
            return svn_error_create(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                    _("No conflict-callback and no "
                                      "pre-defined conflict-choice provided"));

          SVN_ERR(cswb->conflict_func(&result, cd, cswb->conflict_baton,
                                      iterpool, iterpool));

          my_choice = result->choice;
        }


      if (my_choice == svn_wc_conflict_choose_postpone)
        continue;

      switch (cd->kind)
        {
          case svn_wc_conflict_kind_tree:
            if (!cswb->resolve_tree)
              break;

            /* For now, we only clear tree conflict information and resolve
             * to the working state. There is no way to pick theirs-full
             * or mine-full, etc. Throw an error if the user expects us
             * to be smarter than we really are. */
            if (my_choice != svn_wc_conflict_choose_merged)
              {
                return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE,
                                         NULL,
                                         _("Tree conflicts can only be "
                                           "resolved to 'working' state; "
                                           "'%s' not resolved"),
                                         svn_dirent_local_style(local_abspath,
                                                                iterpool));
              }

            SVN_ERR(resolve_conflict_on_node(&did_resolve,
                                             db,
                                             local_abspath,
                                             FALSE /* resolve_text */,
                                             FALSE /* resolve_props */,
                                             TRUE /* resolve_tree */,
                                             my_choice,
                                             cswb->cancel_func,
                                             cswb->cancel_baton,
                                             iterpool));

            resolved = TRUE;
            break;

          case svn_wc_conflict_kind_text:
            if (!cswb->resolve_text)
              break;

            SVN_ERR(resolve_conflict_on_node(&did_resolve,
                                             db,
                                             local_abspath,
                                             TRUE /* resolve_text */,
                                             FALSE /* resolve_props */,
                                             FALSE /* resolve_tree */,
                                             my_choice,
                                             cswb->cancel_func,
                                             cswb->cancel_baton,
                                             iterpool));

            if (did_resolve)
              resolved = TRUE;
            break;

          case svn_wc_conflict_kind_property:
            if (!cswb->resolve_prop)
              break;

            /* ### this is bogus. resolve_conflict_on_node() does not handle
               ### individual property resolution.  */
            if (*cswb->resolve_prop != '\0' &&
                strcmp(cswb->resolve_prop, cd->property_name) != 0)
              {
                break; /* Skip this property conflict */
              }


            /* We don't have property name handling here yet :( */
            SVN_ERR(resolve_conflict_on_node(&did_resolve,
                                             db,
                                             local_abspath,
                                             FALSE /* resolve_text */,
                                             TRUE /* resolve_props */,
                                             FALSE /* resolve_tree */,
                                             my_choice,
                                             cswb->cancel_func,
                                             cswb->cancel_baton,
                                             iterpool));

            if (did_resolve)
              resolved = TRUE;
            break;

          default:
            /* We can't resolve other conflict types */
            break;
        }
    }

  /* Notify */
  if (cswb->notify_func && resolved)
    cswb->notify_func(cswb->notify_baton,
                      svn_wc_create_notify(local_abspath,
                                           svn_wc_notify_resolved,
                                           iterpool),
                      iterpool);

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__resolve_conflicts(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  svn_kind_t kind;
  svn_boolean_t conflicted;
  struct conflict_status_walker_baton cswb;

  /* ### the underlying code does NOT support resolving individual
     ### properties. bail out if the caller tries it.  */
  if (resolve_prop != NULL && *resolve_prop != '\0')
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            U_("Resolving a single property is not (yet) "
                               "supported."));

  /* ### Just a versioned check? */
  /* Conflicted is set to allow invoking on actual only nodes */
  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, &conflicted,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  /* When the implementation still used the entry walker, depth
     unknown was translated to infinity. */
  if (kind != svn_kind_dir)
    depth = svn_depth_empty;
  else if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  cswb.db = wc_ctx->db;
  cswb.resolve_text = resolve_text;
  cswb.resolve_prop = resolve_prop;
  cswb.resolve_tree = resolve_tree;
  cswb.conflict_choice = conflict_choice;

  cswb.conflict_func = conflict_func;
  cswb.conflict_baton = conflict_baton;

  cswb.cancel_func = cancel_func;
  cswb.cancel_baton = cancel_baton;

  cswb.notify_func = notify_func;
  cswb.notify_baton = notify_baton;

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_conflict_resolver_starting,
                                    scratch_pool),
                scratch_pool);

  SVN_ERR(svn_wc_walk_status(wc_ctx,
                             local_abspath,
                             depth,
                             FALSE /* get_all */,
                             FALSE /* no_ignore */,
                             TRUE /* ignore_text_mods */,
                             NULL /* ignore_patterns */,
                             conflict_status_walker, &cswb,
                             cancel_func, cancel_baton,
                             scratch_pool));

  if (notify_func)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_conflict_resolver_done,
                                    scratch_pool),
                scratch_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_resolved_conflict5(svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_depth_t depth,
                          svn_boolean_t resolve_text,
                          const char *resolve_prop,
                          svn_boolean_t resolve_tree,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__resolve_conflicts(wc_ctx, local_abspath,
                                                   depth, resolve_text,
                                                   resolve_prop, resolve_tree,
                                                   conflict_choice,
                                                   NULL, NULL,
                                                   cancel_func, cancel_baton,
                                                   notify_func, notify_baton,
                                                   scratch_pool));
}
