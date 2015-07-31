/*
 * resolved.c:  wrapper around wc resolved functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_types.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "client.h"
#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"

#define ARRAY_LEN(ary) ((sizeof (ary)) / (sizeof ((ary)[0])))


/*** Code. ***/

svn_error_t *
svn_client__resolve_conflicts(svn_boolean_t *conflicts_remain,
                              apr_hash_t *conflicted_paths,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *array;
  int i;

  if (conflicts_remain)
    *conflicts_remain = FALSE;

  SVN_ERR(svn_hash_keys(&array, conflicted_paths, scratch_pool));
  svn_sort__array(array, svn_sort_compare_paths);

  for (i = 0; i < array->nelts; i++)
    {
      const char *local_abspath = APR_ARRAY_IDX(array, i, const char *);

      svn_pool_clear(iterpool);
      SVN_ERR(svn_wc__resolve_conflicts(ctx->wc_ctx, local_abspath,
                                        svn_depth_empty,
                                        TRUE /* resolve_text */,
                                        "" /* resolve_prop (ALL props) */,
                                        TRUE /* resolve_tree */,
                                        svn_wc_conflict_choose_unspecified,
                                        ctx->conflict_func2,
                                        ctx->conflict_baton2,
                                        ctx->cancel_func, ctx->cancel_baton,
                                        ctx->notify_func2, ctx->notify_baton2,
                                        iterpool));

      if (conflicts_remain && !*conflicts_remain)
        {
          svn_error_t *err;
          svn_boolean_t text_c, prop_c, tree_c;

          err = svn_wc_conflicted_p3(&text_c, &prop_c, &tree_c,
                                     ctx->wc_ctx, local_abspath,
                                     iterpool);
          if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              text_c = prop_c = tree_c = FALSE;
            }
          else
            {
              SVN_ERR(err);
            }
          if (text_c || prop_c || tree_c)
            *conflicts_remain = TRUE;
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_resolve(const char *path,
                   svn_depth_t depth,
                   svn_wc_conflict_choice_t conflict_choice,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const char *local_abspath;
  svn_error_t *err;
  const char *lock_abspath;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* Similar to SVN_WC__CALL_WITH_WRITE_LOCK but using a custom
     locking function. */

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath, pool, pool));
  err = svn_wc__resolve_conflicts(ctx->wc_ctx, local_abspath,
                                  depth,
                                  TRUE /* resolve_text */,
                                  "" /* resolve_prop (ALL props) */,
                                  TRUE /* resolve_tree */,
                                  conflict_choice,
                                  ctx->conflict_func2,
                                  ctx->conflict_baton2,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  ctx->notify_func2, ctx->notify_baton2,
                                  pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 pool));
  svn_io_sleep_for_timestamps(path, pool);

  return svn_error_trace(err);
}


/*** Dealing with conflicts. ***/

struct svn_client_conflict_t
{
  const char *local_abspath;
  svn_client_ctx_t *ctx;
  apr_hash_t *prop_conflicts;

  /* Indicate which options were chosen to resolve a text or tree conflict
   * on the conflited node. */
  svn_client_conflict_option_id_t resolution_text;
  svn_client_conflict_option_id_t resolution_tree;

  /* A mapping from const char* property name to pointers to
   * svn_client_conflict_option_t for all properties which had their
   * conflicts resolved. Indicates which options were chosen to resolve
   * the property conflicts. */
  apr_hash_t *resolved_props;

  /* For backwards compat. */
  const svn_wc_conflict_description2_t *legacy_text_conflict;
  const svn_wc_conflict_description2_t *legacy_prop_conflict;
  const svn_wc_conflict_description2_t *legacy_tree_conflict;
};

/*
 * Return a legacy conflict choice corresponding to OPTION_ID.
 * Return svn_wc_conflict_choose_undefined if no corresponding
 * legacy conflict choice exists.
 */
static svn_wc_conflict_choice_t
conflict_option_id_to_wc_conflict_choice(
  svn_client_conflict_option_id_t option_id)
{

  switch (option_id)
    {
      case svn_client_conflict_option_undefined:
        return svn_wc_conflict_choose_undefined;

      case svn_client_conflict_option_postpone:
        return svn_wc_conflict_choose_postpone;

      case svn_client_conflict_option_base_text:
        return svn_wc_conflict_choose_base;

      case svn_client_conflict_option_incoming_new_text:
        return svn_wc_conflict_choose_theirs_full;

      case svn_client_conflict_option_working_text:
        return svn_wc_conflict_choose_mine_full;

      case svn_client_conflict_option_incoming_new_text_for_conflicted_hunks_only:
        return svn_wc_conflict_choose_theirs_conflict;

      case svn_client_conflict_option_working_text_for_conflicted_hunks_only:
        return svn_wc_conflict_choose_mine_conflict;

      case svn_client_conflict_option_merged_text:
        return svn_wc_conflict_choose_merged;

      case svn_client_conflict_option_unspecified:
        return svn_wc_conflict_choose_unspecified;

      default:
        break;
    }

  return svn_wc_conflict_choose_undefined;
}

static void
add_legacy_desc_to_conflict(const svn_wc_conflict_description2_t *desc,
                            svn_client_conflict_t *conflict,
                            apr_pool_t *result_pool)
{
  switch (desc->kind)
    {
      case svn_wc_conflict_kind_text:
        conflict->legacy_text_conflict = desc;
        break;

      case svn_wc_conflict_kind_property:
        conflict->legacy_prop_conflict = desc;
        break;

      case svn_wc_conflict_kind_tree:
        conflict->legacy_tree_conflict = desc;
        break;

      default:
        SVN_ERR_ASSERT_NO_RETURN(FALSE); /* unknown kind of conflict */
    }
}

/* Set up a conflict object. If legacy conflict descriptor DESC is not NULL,
 * set up the conflict object for backwards compatibility. */
static svn_error_t *
conflict_get_internal(svn_client_conflict_t **conflict,
                      const char *local_abspath,
                      const svn_wc_conflict_description2_t *desc,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  const apr_array_header_t *descs;
  int i;

  *conflict = apr_pcalloc(result_pool, sizeof(**conflict));

  if (desc)
    {
      /* Add a single legacy conflict descriptor. */
      (*conflict)->local_abspath = desc->local_abspath;
      (*conflict)->resolution_text = svn_client_conflict_option_undefined;
      (*conflict)->resolution_tree = svn_client_conflict_option_undefined;
      (*conflict)->resolved_props = apr_hash_make(result_pool);
      add_legacy_desc_to_conflict(desc, *conflict, result_pool);

      return SVN_NO_ERROR;
    }

  (*conflict)->local_abspath = apr_pstrdup(result_pool, local_abspath);
  (*conflict)->resolution_text = svn_client_conflict_option_undefined;
  (*conflict)->resolution_tree = svn_client_conflict_option_undefined;
  (*conflict)->resolved_props = apr_hash_make(result_pool);
  (*conflict)->ctx = ctx;

  /* Add all legacy conflict descriptors we can find. Eventually, this code
   * path should stop relying on svn_wc_conflict_description2_t entirely. */
  SVN_ERR(svn_wc__read_conflict_descriptions2_t(&descs, ctx->wc_ctx,
                                                local_abspath,
                                                result_pool, scratch_pool));
  for (i = 0; i < descs->nelts; i++)
    {
      desc = APR_ARRAY_IDX(descs, i, const svn_wc_conflict_description2_t *);
      if (desc->kind == svn_wc_conflict_kind_property)
        {
          if ((*conflict)->prop_conflicts == NULL)
            (*conflict)->prop_conflicts = apr_hash_make(result_pool);
          svn_hash_sets((*conflict)->prop_conflicts, desc->property_name, desc);
        }
      else
        add_legacy_desc_to_conflict(desc, *conflict, result_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get(svn_client_conflict_t **conflict,
                        const char *local_abspath,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  return svn_error_trace(conflict_get_internal(conflict, local_abspath, NULL,
                                               ctx, result_pool, scratch_pool));
}

svn_error_t *
svn_client_conflict_from_wc_description2_t(
  svn_client_conflict_t **conflict,
  const svn_wc_conflict_description2_t *desc,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  return svn_error_trace(conflict_get_internal(conflict, NULL, desc, NULL,
                                               result_pool, scratch_pool));
}

/* Baton type for conflict_resolver_func(). */
struct conflict_resolver_baton_t {

  svn_client_conflict_walk_func_t *conflict_walk_func;
  void *conflict_walk_func_baton;
  svn_client_ctx_t *ctx;
  
} conflict_walk_baton_t;

/* Implements svn_wc_conflict_resolver_func2_t for now because
 * libsvn_wc does not support our new conflict type yet. */
static svn_error_t *
conflict_resolver_func(svn_wc_conflict_result_t **result,
                       const svn_wc_conflict_description2_t *description,
                       void *baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  struct conflict_resolver_baton_t *b = baton;
  svn_client_conflict_t *conflict;
  const char *local_abspath;
  svn_client_conflict_option_id_t resolution;
  svn_wc_conflict_choice_t conflict_choice;

  local_abspath = description->local_abspath;
  SVN_ERR(svn_client_conflict_get(&conflict, local_abspath, b->ctx,
                                  scratch_pool, scratch_pool));

  SVN_ERR(b->conflict_walk_func(b->conflict_walk_func_baton,
                                conflict, scratch_pool));

  /* Evaluate the conflict callback result based on which kind
   * of conflict libsvn_wc has given us. */
  resolution = svn_client_conflict_option_undefined;
  if (description->kind == svn_wc_conflict_kind_text)
    resolution = conflict->resolution_text;
  else if (description->kind == svn_wc_conflict_kind_tree)
    resolution = conflict->resolution_tree;
  else if (description->kind == svn_wc_conflict_kind_property)
    {
      svn_client_conflict_option_t *option;

      option = svn_hash_gets(conflict->resolved_props,
                             description->property_name);
      if (option == NULL)
        return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                                 _("No resolution provided for conflicted "
                                   "property '%s' on path '%s'"),
                                 description->property_name,
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));

      resolution = svn_client_conflict_option_get_id(option);
    }
  else
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("Unknown legacy conflict kind '%d'"),
                             description->kind);

  if (resolution == svn_client_conflict_option_undefined)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("No resolution for conflicted path '%s'"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
    
  conflict_choice = conflict_option_id_to_wc_conflict_choice(resolution);
  *result = svn_wc_create_conflict_result(conflict_choice, NULL, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_walk(const char *local_abspath,
                         svn_depth_t depth,
                         svn_client_conflict_walk_func_t conflict_walk_func,
                         void *conflict_walk_func_baton,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  struct conflict_resolver_baton_t b;
  const char *lock_abspath;
  svn_error_t *err;

  b.conflict_walk_func = conflict_walk_func;
  b.conflict_walk_func_baton = conflict_walk_func_baton;
  b.ctx = ctx;

  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  /* ### TODO: svn_wc__resolve_conflicts() should be changed to support
   * ### iteration without relying on svn_wc_conflict_resolver_func2_t */
  err = svn_wc__resolve_conflicts(ctx->wc_ctx, local_abspath,
                                  depth,
                                  TRUE /* resolve_text */,
                                  "" /* resolve_prop (ALL props) */,
                                  TRUE /* resolve_tree */,
                                  svn_wc_conflict_choose_unspecified,
                                  conflict_resolver_func, &b,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  ctx->notify_func2, ctx->notify_baton2,
                                  scratch_pool);

  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);

  

  return SVN_NO_ERROR;
}

/* Resolves conflict to OPTION and sets CONFLICT->RESOLUTION accordingly. */
typedef svn_error_t *(*conflict_option_resolve_func_t)(
  svn_client_conflict_option_t *option,
  svn_client_conflict_t *conflict,
  apr_pool_t *scratch_pool);

struct svn_client_conflict_option_t
{
  svn_client_conflict_option_id_t id;
  const char *description;

  svn_client_conflict_t *conflict;
  conflict_option_resolve_func_t do_resolve_func;

  /* Data which is specific to particular conflicts and options. */
  union {
    struct {
      /* Indicates the property to resolve in case of a property conflict.
       * If set to "", all properties are resolved to this option. */
      const char *propname;
    } prop;
  } type_data;

};

/* 
 * Resolve the conflict at LOCAL_ABSPATH. Currently only supports
 * an OPTION_ID which can be mapped to svn_wc_conflict_choice_t and
 * maps a single option_id to text, prop, and/or tree conflicts.
 */
static svn_error_t *
resolve_conflict(svn_client_conflict_option_id_t option_id,
                 const char *local_abspath,
                 svn_boolean_t resolve_text,
                 const char * resolve_prop,
                 svn_boolean_t resolve_tree,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  svn_wc_conflict_choice_t conflict_choice;
  const char *lock_abspath;
  svn_error_t *err;

  conflict_choice = conflict_option_id_to_wc_conflict_choice(option_id);
  SVN_ERR(svn_wc__acquire_write_lock_for_resolve(&lock_abspath, ctx->wc_ctx,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
  err = svn_wc__resolve_conflicts(ctx->wc_ctx, local_abspath,
                                  svn_depth_empty,
                                  resolve_text, resolve_prop, resolve_tree,
                                  conflict_choice,
                                  NULL, NULL, /* legacy conflict_func/baton */
                                  ctx->cancel_func,
                                  ctx->cancel_baton,
                                  ctx->notify_func2,
                                  ctx->notify_baton2,
                                  scratch_pool);
  err = svn_error_compose_create(err, svn_wc__release_write_lock(ctx->wc_ctx,
                                                                 lock_abspath,
                                                                 scratch_pool));
  svn_io_sleep_for_timestamps(local_abspath, scratch_pool);

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_text_conflict(svn_client_conflict_option_t *option,
                      svn_client_conflict_t *conflict,
                      apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;

  option_id = svn_client_conflict_option_get_id(option);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  SVN_ERR(resolve_conflict(option_id, local_abspath, TRUE, NULL, FALSE,
                           conflict->ctx, scratch_pool));
  conflict->resolution_text = option_id;

  return SVN_NO_ERROR;
}

/* Implements conflict_option_resolve_func_t. */
static svn_error_t *
resolve_prop_conflict(svn_client_conflict_option_t *option,
                      svn_client_conflict_t *conflict,
                      apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;
  const char *propname = option->type_data.prop.propname;

  option_id = svn_client_conflict_option_get_id(option);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  SVN_ERR(resolve_conflict(option_id, local_abspath,
                           FALSE, propname, FALSE,
                           conflict->ctx, scratch_pool));

  if (propname[0] == '\0')
    {
      apr_hash_index_t *hi;

      /* All properties have been resolved to the same option. */
      for (hi = apr_hash_first(scratch_pool, conflict->prop_conflicts);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *this_propname = apr_hash_this_key(hi);

          svn_hash_sets(conflict->resolved_props,
                        apr_pstrdup(apr_hash_pool_get(conflict->resolved_props),
                                    this_propname),
                        option);
          svn_hash_sets(conflict->prop_conflicts, this_propname, NULL);
        }
    }
  else
    {
      svn_hash_sets(conflict->resolved_props,
                    apr_pstrdup(apr_hash_pool_get(conflict->resolved_props),
                                propname),
                   option);
      svn_hash_sets(conflict->prop_conflicts, propname, NULL);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
/* Implements conflict_option_resolve_func_t. */
resolve_tree_conflict(svn_client_conflict_option_t *option,
                      svn_client_conflict_t *conflict,
                      apr_pool_t *scratch_pool)
{
  svn_client_conflict_option_id_t option_id;
  const char *local_abspath;

  option_id = svn_client_conflict_option_get_id(option);
  local_abspath = svn_client_conflict_get_local_abspath(conflict);
  SVN_ERR(resolve_conflict(option_id, local_abspath, FALSE, NULL, TRUE,
                           conflict->ctx, scratch_pool));
  conflict->resolution_tree = option_id;

  return SVN_NO_ERROR;
}

/* Resolver options for a text conflict */
static const svn_client_conflict_option_t text_conflict_options[] =
{
  {
    svn_client_conflict_option_postpone,
    N_("mark the conflict to be resolved later"),
    NULL,
    resolve_text_conflict
  },

  {
    svn_client_conflict_option_incoming_new_text,
    N_("accept incoming version of entire file"),
    NULL,
    resolve_text_conflict
  },

  {
    svn_client_conflict_option_working_text,
    N_("accept working copy version of entire file"),
    NULL,
    resolve_text_conflict
  },

  {
    svn_client_conflict_option_incoming_new_text_for_conflicted_hunks_only,
    N_("accept incoming version of all text conflicts in file"),
    NULL,
    resolve_text_conflict
  },

  {
    svn_client_conflict_option_working_text_for_conflicted_hunks_only,
    N_("accept working copy version of all text conflicts in file"),
    NULL,
    resolve_text_conflict
  },

};

/* Resolver options for a binary file conflict */
static const svn_client_conflict_option_t binary_conflict_options[] =
{
  {
    svn_client_conflict_option_postpone,
    N_("mark the conflict to be resolved later"),
    NULL,
    resolve_text_conflict,
  },

  {
    svn_client_conflict_option_incoming_new_text,
    N_("accept incoming version of binary file"),
    NULL,
    resolve_text_conflict
  },

  {
    svn_client_conflict_option_working_text,
    N_("accept working copy version of binary file"),
    NULL,
    resolve_text_conflict
  },

};

/* Resolver options for a property conflict */
static const svn_client_conflict_option_t prop_conflict_options[] =
{
  {
    svn_client_conflict_option_postpone,
    N_("mark the conflict to be resolved later"),
    NULL,
    resolve_prop_conflict
  },

  {
    svn_client_conflict_option_incoming_new_text,
    N_("accept incoming version of entire property value"),
    NULL,
    resolve_prop_conflict
  },

  {
    svn_client_conflict_option_working_text,
    N_("accept working copy version of entire property value"),
    NULL,
    resolve_prop_conflict
  },

};

/* Resolver options for a tree conflict */
static const svn_client_conflict_option_t tree_conflict_options[] =
{
  {
    svn_client_conflict_option_postpone,
    N_("mark the conflict to be resolved later"),
    NULL,
    resolve_tree_conflict
  },

  {
    /* ### Use 'working text' for now since libsvn_wc does not know another
     * ### choice to resolve to working yet. */
    svn_client_conflict_option_working_text,
    N_("accept current working copy state"),
    NULL,
    resolve_tree_conflict
  },

};

static svn_error_t *
assert_text_conflict(svn_client_conflict_t *conflict, apr_pool_t *scratch_pool)
{
  svn_boolean_t text_conflicted;

  SVN_ERR(svn_client_conflict_get_conflicted(&text_conflicted, NULL, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));

  SVN_ERR_ASSERT(text_conflicted); /* ### return proper error? */

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_prop_conflict(svn_client_conflict_t *conflict, apr_pool_t *scratch_pool)
{
  apr_array_header_t *props_conflicted;

  SVN_ERR(svn_client_conflict_get_conflicted(NULL, &props_conflicted, NULL,
                                             conflict, scratch_pool,
                                             scratch_pool));

  /* ### return proper error? */
  SVN_ERR_ASSERT(props_conflicted && props_conflicted->nelts > 0);

  return SVN_NO_ERROR;
}

static svn_error_t *
assert_tree_conflict(svn_client_conflict_t *conflict, apr_pool_t *scratch_pool)
{
  svn_boolean_t tree_conflicted;

  SVN_ERR(svn_client_conflict_get_conflicted(NULL, NULL, &tree_conflicted,
                                             conflict, scratch_pool,
                                             scratch_pool));

  SVN_ERR_ASSERT(tree_conflicted); /* ### return proper error? */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_text_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool)
{
  const char *mime_type;
  int i;

  SVN_ERR(assert_text_conflict(conflict, scratch_pool));

  *options = apr_array_make(result_pool, ARRAY_LEN(text_conflict_options),
                            sizeof(svn_client_conflict_option_t *));

  mime_type = svn_client_conflict_text_get_mime_type(conflict);
  if (mime_type && svn_mime_type_is_binary(mime_type))
    {
      for (i = 0; i < ARRAY_LEN(binary_conflict_options); i++)
        {
          APR_ARRAY_PUSH((*options), const svn_client_conflict_option_t *) =
            &binary_conflict_options[i];
        }
    }
  else
    {
      for (i = 0; i < ARRAY_LEN(text_conflict_options); i++)
        {
          APR_ARRAY_PUSH((*options), const svn_client_conflict_option_t *) =
            &text_conflict_options[i];
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_prop_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool)
{
  int i;

  SVN_ERR(assert_prop_conflict(conflict, scratch_pool));

  *options = apr_array_make(result_pool, ARRAY_LEN(prop_conflict_options),
                            sizeof(svn_client_conflict_option_t *));
  for (i = 0; i < ARRAY_LEN(prop_conflict_options); i++)
    {
      APR_ARRAY_PUSH((*options), const svn_client_conflict_option_t *) =
        &prop_conflict_options[i];
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_tree_get_resolution_options(apr_array_header_t **options,
                                                svn_client_conflict_t *conflict,
                                                apr_pool_t *result_pool,
                                                apr_pool_t *scratch_pool)
{
  int i;

  SVN_ERR(assert_tree_conflict(conflict, scratch_pool));

  *options = apr_array_make(result_pool, ARRAY_LEN(tree_conflict_options),
                            sizeof(svn_client_conflict_option_t *));
  for (i = 0; i < ARRAY_LEN(tree_conflict_options); i++)
    {
      APR_ARRAY_PUSH((*options), const svn_client_conflict_option_t *) =
        &tree_conflict_options[i];
    }

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_option_get_id(svn_client_conflict_option_t *option)
{
  return option->id;
}

svn_error_t *
svn_client_conflict_option_describe(const char **description,
                                    svn_client_conflict_option_t *option,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  *description = apr_pstrdup(result_pool, option->description);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_text_resolve(svn_client_conflict_t *conflict,
                                 svn_client_conflict_option_t *option,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_text_conflict(conflict, scratch_pool));
  SVN_ERR(option->do_resolve_func(option, conflict, scratch_pool));

  return SVN_NO_ERROR;
}

/* Find an *OPTION in RESOLUTION_OPTIONS which matches the desired
 * RESOLUTION option ID. If no matching option exists, raise an
 * error using LOCAL_ABSPATH as part of the error message. */
static svn_error_t *
match_resolution_option(svn_client_conflict_option_t **option,
                        apr_array_header_t *resolution_options,
                        svn_client_conflict_option_id_t resolution,
                        const char *local_abspath,
                        apr_pool_t *scratch_pool)
{
  int i;

  *option = NULL;
  for (i = 0; i < resolution_options->nelts; i++)
    {
      svn_client_conflict_option_t *this_option;
      svn_client_conflict_option_id_t this_option_id;
      
      this_option = APR_ARRAY_IDX(resolution_options, i,
                                  svn_client_conflict_option_t *);
      this_option_id = svn_client_conflict_option_get_id(this_option);

      if (this_option_id == resolution)
        *option = this_option;
        break;
    }

  if (*option == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                               _("Inapplicable conflict resolution option "
                                 "ID '%d' given for conflicted path '%s'"),
                               resolution,
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_text_resolve_by_id(
  svn_client_conflict_t *conflict,
  svn_client_conflict_option_id_t option_id,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *resolution_options;
  svn_client_conflict_option_t *option;

  SVN_ERR(svn_client_conflict_text_get_resolution_options(
            &resolution_options, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(match_resolution_option(&option, resolution_options,
                                  option_id, conflict->local_abspath,
                                  scratch_pool));
  SVN_ERR(svn_client_conflict_text_resolve(conflict, option, scratch_pool));

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_text_get_resolution(const svn_client_conflict_t *conflict)
{
  return conflict->resolution_text;
}

svn_error_t *
svn_client_conflict_prop_resolve(svn_client_conflict_t *conflict,
                                 const char *propname,
                                 svn_client_conflict_option_t *option,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_prop_conflict(conflict, scratch_pool));
  option->type_data.prop.propname = propname;
  SVN_ERR(option->do_resolve_func(option, conflict, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_prop_resolve_by_id(
  svn_client_conflict_t *conflict,
  const char *propname,
  svn_client_conflict_option_id_t option_id,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *resolution_options;
  svn_client_conflict_option_t *option;

  SVN_ERR(svn_client_conflict_prop_get_resolution_options(
            &resolution_options, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(match_resolution_option(&option, resolution_options,
                                  option_id, conflict->local_abspath,
                                  scratch_pool));
  SVN_ERR(svn_client_conflict_prop_resolve(conflict, propname, option,
                                           scratch_pool));

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_prop_get_resolution(const svn_client_conflict_t *conflict,
                                        const char *propname)
{
  svn_client_conflict_option_t *option;

  option = svn_hash_gets(conflict->resolved_props, propname);
  if (option == NULL)
    return svn_client_conflict_option_undefined;

  return svn_client_conflict_option_get_id(option);
}

svn_error_t *
svn_client_conflict_tree_resolve(svn_client_conflict_t *conflict,
                                 svn_client_conflict_option_t *option,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(assert_tree_conflict(conflict, scratch_pool));
  SVN_ERR(option->do_resolve_func(option, conflict, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_tree_resolve_by_id(
  svn_client_conflict_t *conflict,
  svn_client_conflict_option_id_t option_id,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *resolution_options;
  svn_client_conflict_option_t *option;

  SVN_ERR(svn_client_conflict_tree_get_resolution_options(
            &resolution_options, conflict,
            scratch_pool, scratch_pool));
  SVN_ERR(match_resolution_option(&option, resolution_options,
                                  option_id, conflict->local_abspath,
                                  scratch_pool));
  SVN_ERR(svn_client_conflict_tree_resolve(conflict, option, scratch_pool));

  return SVN_NO_ERROR;
}

svn_client_conflict_option_id_t
svn_client_conflict_tree_get_resolution(const svn_client_conflict_t *conflict)
{
  return conflict->resolution_tree;
}

/* Return the legacy conflict descriptor which is wrapped by CONFLICT. */
static const svn_wc_conflict_description2_t *
get_conflict_desc2_t(const svn_client_conflict_t *conflict)
{
  if (conflict->legacy_text_conflict)
    return conflict->legacy_text_conflict;

  if (conflict->legacy_tree_conflict)
    return conflict->legacy_tree_conflict;

  if (conflict->legacy_prop_conflict)
    return conflict->legacy_prop_conflict;

  return NULL;
}

svn_wc_conflict_kind_t
svn_client_conflict_get_kind(const svn_client_conflict_t *conflict)
{
  return get_conflict_desc2_t(conflict)->kind;
}

svn_error_t *
svn_client_conflict_get_conflicted(svn_boolean_t *text_conflicted,
                                   apr_array_header_t **props_conflicted,
                                   svn_boolean_t *tree_conflicted,
                                   svn_client_conflict_t *conflict,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  if (text_conflicted)
    *text_conflicted = (conflict->legacy_text_conflict != NULL);

  if (props_conflicted)
    {
      if (conflict->legacy_prop_conflict)
        {
          *props_conflicted = apr_array_make(result_pool, 1,
                                             sizeof(const char*));
          APR_ARRAY_PUSH((*props_conflicted), const char *) =
            conflict->legacy_prop_conflict->property_name;
        }
      else
        SVN_ERR(svn_hash_keys(props_conflicted, conflict->prop_conflicts,
                              result_pool));
    }

  if (tree_conflicted)
    *tree_conflicted = (conflict->legacy_tree_conflict != NULL);

  return SVN_NO_ERROR;
}

const char *
svn_client_conflict_get_local_abspath(const svn_client_conflict_t *conflict)
{
  return conflict->local_abspath;
}

svn_wc_operation_t
svn_client_conflict_get_operation(const svn_client_conflict_t *conflict)
{
  return get_conflict_desc2_t(conflict)->operation;
}

svn_wc_conflict_action_t
svn_client_conflict_get_incoming_change(const svn_client_conflict_t *conflict)
{
  return get_conflict_desc2_t(conflict)->action;
}

svn_wc_conflict_reason_t
svn_client_conflict_get_local_change(const svn_client_conflict_t *conflict)
{
  return get_conflict_desc2_t(conflict)->reason;
}

svn_error_t *
svn_client_conflict_get_repos_info(const char **repos_root_url,
                                   const char **repos_uuid,
                                   const svn_client_conflict_t *conflict,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  if (repos_root_url)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *repos_root_url =
          get_conflict_desc2_t(conflict)->src_left_version->repos_url;
      else if (get_conflict_desc2_t(conflict)->src_right_version)
        *repos_root_url =
          get_conflict_desc2_t(conflict)->src_right_version->repos_url;
      else
        *repos_root_url = NULL;
    }

  if (repos_uuid)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *repos_uuid =
          get_conflict_desc2_t(conflict)->src_left_version->repos_uuid;
      else if (get_conflict_desc2_t(conflict)->src_right_version)
        *repos_uuid =
          get_conflict_desc2_t(conflict)->src_right_version->repos_uuid;
      else
        *repos_uuid = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get_incoming_old_repos_location(
  const char **incoming_old_repos_relpath,
  svn_revnum_t *incoming_old_pegrev,
  svn_node_kind_t *incoming_old_node_kind,
  const svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (incoming_old_repos_relpath)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *incoming_old_repos_relpath =
          get_conflict_desc2_t(conflict)->src_left_version->path_in_repos;
      else
        *incoming_old_repos_relpath = NULL;
    }

  if (incoming_old_pegrev)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *incoming_old_pegrev =
          get_conflict_desc2_t(conflict)->src_left_version->peg_rev;
      else
        *incoming_old_pegrev = SVN_INVALID_REVNUM;
    }

  if (incoming_old_node_kind)
    {
      if (get_conflict_desc2_t(conflict)->src_left_version)
        *incoming_old_node_kind =
          get_conflict_desc2_t(conflict)->src_left_version->node_kind;
      else
        *incoming_old_node_kind = svn_node_none;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_conflict_get_incoming_new_repos_location(
  const char **incoming_new_repos_relpath,
  svn_revnum_t *incoming_new_pegrev,
  svn_node_kind_t *incoming_new_node_kind,
  const svn_client_conflict_t *conflict,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  if (incoming_new_repos_relpath)
    {
      if (get_conflict_desc2_t(conflict)->src_right_version)
        *incoming_new_repos_relpath =
          get_conflict_desc2_t(conflict)->src_right_version->path_in_repos;
      else
        *incoming_new_repos_relpath = NULL;
    }

  if (incoming_new_pegrev)
    {
      if (get_conflict_desc2_t(conflict)->src_right_version)
        *incoming_new_pegrev =
          get_conflict_desc2_t(conflict)->src_right_version->peg_rev;
      else
        *incoming_new_pegrev = SVN_INVALID_REVNUM;
    }

  if (incoming_new_node_kind)
    {
      if (get_conflict_desc2_t(conflict)->src_right_version)
        *incoming_new_node_kind =
          get_conflict_desc2_t(conflict)->src_right_version->node_kind;
      else
        *incoming_new_node_kind = svn_node_none;
    }

  return SVN_NO_ERROR;
}

svn_node_kind_t
svn_client_conflict_tree_get_victim_node_kind(
  const svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_tree);

  return get_conflict_desc2_t(conflict)->node_kind;
}

const char *
svn_client_conflict_prop_get_propname(const svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_property);

  return get_conflict_desc2_t(conflict)->property_name;
}

svn_error_t *
svn_client_conflict_prop_get_propvals(const svn_string_t **base_propval,
                                      const svn_string_t **working_propval,
                                      const svn_string_t **incoming_old_propval,
                                      const svn_string_t **incoming_new_propval,
                                      const svn_client_conflict_t *conflict,
  apr_pool_t *result_pool)
{
  SVN_ERR_ASSERT(svn_client_conflict_get_kind(conflict) ==
                 svn_wc_conflict_kind_property);

  if (base_propval)
    *base_propval =
      svn_string_dup(get_conflict_desc2_t(conflict)->prop_value_base,
                     result_pool);

  if (working_propval)
    *working_propval =
      svn_string_dup(get_conflict_desc2_t(conflict)->prop_value_working,
                     result_pool);

  if (incoming_old_propval)
    *incoming_old_propval =
      svn_string_dup(get_conflict_desc2_t(conflict)->prop_value_incoming_old,
                     result_pool);

  if (incoming_new_propval)
    *incoming_new_propval =
      svn_string_dup(get_conflict_desc2_t(conflict)->prop_value_incoming_new,
                     result_pool);

  return SVN_NO_ERROR;
}

const char *
svn_client_conflict_prop_get_reject_abspath(
  const svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_property);

  /* svn_wc_conflict_description2_t stores this path in 'their_abspath' */
  return get_conflict_desc2_t(conflict)->their_abspath;
}

const char *
svn_client_conflict_text_get_mime_type(const svn_client_conflict_t *conflict)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_text);

  return get_conflict_desc2_t(conflict)->mime_type;
}

svn_error_t *
svn_client_conflict_text_get_contents(const char **base_abspath,
                                      const char **working_abspath,
                                      const char **incoming_old_abspath,
                                      const char **incoming_new_abspath,
                                      const svn_client_conflict_t *conflict,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_client_conflict_get_kind(conflict)
      == svn_wc_conflict_kind_text);

  if (base_abspath)
    {
      if (svn_client_conflict_get_operation(conflict) ==
          svn_wc_operation_merge)
        *base_abspath = NULL; /* ### WC base contents not available yet */
      else /* update/switch */
        *base_abspath = get_conflict_desc2_t(conflict)->base_abspath;
    }

  if (working_abspath)
    *working_abspath = get_conflict_desc2_t(conflict)->my_abspath;

  if (incoming_old_abspath)
    *incoming_old_abspath = get_conflict_desc2_t(conflict)->base_abspath;

  if (incoming_new_abspath)
    *incoming_new_abspath = get_conflict_desc2_t(conflict)->their_abspath;

  return SVN_NO_ERROR;
}
