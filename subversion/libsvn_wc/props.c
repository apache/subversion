/*
 * props.c :  routines dealing with properties in the working copy
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



#include <stdlib.h>
#include <string.h>

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_mergeinfo.h"
#include "svn_wc.h"
#include "svn_utf.h"
#include "svn_diff.h"

#include "private/svn_wc_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_skel.h"

#include "wc.h"
#include "props.h"
#include "translate.h"
#include "workqueue.h"
#include "conflicts.h"

#include "svn_private_config.h"

/* #define TEST_DB_PROPS */


/* Forward declaration.  */
static const svn_string_t *
message_from_skel(const svn_skel_t *skel,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool);

/* Given a *SINGLE* property conflict in PROP_SKEL, generate a message
   for it, and write it to STREAM, along with a trailing EOL sequence.

   See message_from_skel() for details on PROP_SKEL.  */
static svn_error_t *
append_prop_conflict(svn_stream_t *stream,
                     const svn_skel_t *prop_skel,
                     apr_pool_t *pool)
{
  /* TODO:  someday, perhaps prefix each conflict_description with a
     timestamp or something? */
  const svn_string_t *message = message_from_skel(prop_skel, pool, pool);
  apr_size_t len;
  const char *native_text =
    svn_utf_cstring_from_utf8_fuzzy(message->data, pool);

  len = strlen(native_text);
  SVN_ERR(svn_stream_write(stream, native_text, &len));

  native_text = svn_utf_cstring_from_utf8_fuzzy(APR_EOL_STR, pool);
  len = strlen(native_text);
  return svn_stream_write(stream, native_text, &len);
}


/* Get the property reject file for LOCAL_ABSPATH in DB.  Set
   *PREJFILE_ABSPATH to the name of that file, or to NULL if no such
   file is named.  The file may, or may not, exist on disk. */
svn_error_t *
svn_wc__get_prejfile_abspath(const char **prejfile_abspath,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const apr_array_header_t *conflicts;
  int i;

  SVN_ERR(svn_wc__db_read_conflicts(&conflicts, db, local_abspath,
                                    scratch_pool, scratch_pool));

  for (i = 0; i < conflicts->nelts; i++)
    {
      const svn_wc_conflict_description2_t *cd;
      cd = APR_ARRAY_IDX(conflicts, i, const svn_wc_conflict_description2_t *);

      if (cd->kind == svn_wc_conflict_kind_property)
        {
          *prejfile_abspath = apr_pstrdup(result_pool, cd->their_abspath);
          return SVN_NO_ERROR;
        }
    }

  *prejfile_abspath = NULL;
  return SVN_NO_ERROR;
}

/*---------------------------------------------------------------------*/

/*** Merging propchanges into the working copy ***/


/* Parse FROM_PROP_VAL and TO_PROP_VAL into mergeinfo hashes, and
   calculate the deltas between them. */
static svn_error_t *
diff_mergeinfo_props(svn_mergeinfo_t *deleted, svn_mergeinfo_t *added,
                     const svn_string_t *from_prop_val,
                     const svn_string_t *to_prop_val, apr_pool_t *pool)
{
  if (svn_string_compare(from_prop_val, to_prop_val))
    {
      /* Don't bothering parsing identical mergeinfo. */
      *deleted = apr_hash_make(pool);
      *added = apr_hash_make(pool);
    }
  else
    {
      svn_mergeinfo_t from, to;
      SVN_ERR(svn_mergeinfo_parse(&from, from_prop_val->data, pool));
      SVN_ERR(svn_mergeinfo_parse(&to, to_prop_val->data, pool));
      SVN_ERR(svn_mergeinfo_diff(deleted, added, from, to,
                                 TRUE, pool));
    }
  return SVN_NO_ERROR;
}

/* Parse the mergeinfo from PROP_VAL1 and PROP_VAL2, combine it, then
   reconstitute it into *OUTPUT.  Call when the WC's mergeinfo has
   been modified to combine it with incoming mergeinfo from the
   repos. */
static svn_error_t *
combine_mergeinfo_props(const svn_string_t **output,
                        const svn_string_t *prop_val1,
                        const svn_string_t *prop_val2,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t mergeinfo1, mergeinfo2;
  svn_string_t *mergeinfo_string;

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo1, prop_val1->data, scratch_pool));
  SVN_ERR(svn_mergeinfo_parse(&mergeinfo2, prop_val2->data, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge(mergeinfo1, mergeinfo2, scratch_pool));
  SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string, mergeinfo1, result_pool));
  *output = mergeinfo_string;
  return SVN_NO_ERROR;
}

/* Perform a 3-way merge operation on mergeinfo.  FROM_PROP_VAL is
   the "base" property value, WORKING_PROP_VAL is the current value,
   and TO_PROP_VAL is the new value. */
static svn_error_t *
combine_forked_mergeinfo_props(const svn_string_t **output,
                               const svn_string_t *from_prop_val,
                               const svn_string_t *working_prop_val,
                               const svn_string_t *to_prop_val,
                               apr_pool_t *pool)
{
  svn_mergeinfo_t from_mergeinfo, l_deleted, l_added, r_deleted, r_added;
  svn_string_t *mergeinfo_string;

  /* ### OPTIMIZE: Use from_mergeinfo when diff'ing. */
  SVN_ERR(diff_mergeinfo_props(&l_deleted, &l_added, from_prop_val,
                               working_prop_val, pool));
  SVN_ERR(diff_mergeinfo_props(&r_deleted, &r_added, from_prop_val,
                               to_prop_val, pool));
  SVN_ERR(svn_mergeinfo_merge(l_deleted, r_deleted, pool));
  SVN_ERR(svn_mergeinfo_merge(l_added, r_added, pool));

  /* Apply the combined deltas to the base. */
  SVN_ERR(svn_mergeinfo_parse(&from_mergeinfo, from_prop_val->data, pool));
  SVN_ERR(svn_mergeinfo_merge(from_mergeinfo, l_added, pool));

  SVN_ERR(svn_mergeinfo_remove2(&from_mergeinfo, l_deleted,
                                from_mergeinfo, TRUE, pool, pool));

  SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string, from_mergeinfo, pool));
  *output = mergeinfo_string;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__perform_props_merge(svn_wc_notify_state_t *state,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            const svn_wc_conflict_version_t *left_version,
                            const svn_wc_conflict_version_t *right_version,
                            apr_hash_t *baseprops,
                            const apr_array_header_t *propchanges,
                            svn_boolean_t base_merge,
                            svn_boolean_t dry_run,
                            svn_wc_conflict_resolver_func_t conflict_func,
                            void *conflict_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  int i;
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  apr_hash_t *pristine_props = NULL;
  apr_hash_t *actual_props = NULL;
  apr_hash_t *new_pristine_props;
  apr_hash_t *new_actual_props;
  svn_boolean_t had_props, props_mod;
  svn_boolean_t have_base;
  svn_skel_t *work_items;

  /* IMPORTANT: svn_wc_merge_prop_diffs relies on the fact that baseprops
     may be NULL. */

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               &had_props, &props_mod, &have_base, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  /* Checks whether the node exists and returns the hidden flag */
  if (status == svn_wc__db_status_not_present
      || status == svn_wc__db_status_absent
      || status == svn_wc__db_status_excluded)
    {
      return svn_error_createf(
                    SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                    _("The node '%s' was not found."),
                    svn_dirent_local_style(local_abspath, scratch_pool));
    }
  else if (status != svn_wc__db_status_normal
           && status != svn_wc__db_status_added
           && status != svn_wc__db_status_incomplete)
    {
      return svn_error_createf(
                    SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                    _("The node '%s' does not have properties in this state."),
                    svn_dirent_local_style(local_abspath, scratch_pool));
    }

  /* The PROPCHANGES may not have non-"normal" properties in it. If entry
     or wc props were allowed, then the following code would install them
     into the BASE and/or WORKING properties(!).  */
  for (i = propchanges->nelts; i--; )
    {
      const svn_prop_t *change = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      if (!svn_wc_is_normal_prop(change->name))
        return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                                 _("The property '%s' may not be merged "
                                   "into '%s'."),
                                 change->name,
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
    }

  if (had_props)
    SVN_ERR(svn_wc__get_pristine_props(&pristine_props, db, local_abspath,
                                       scratch_pool, scratch_pool));
  if (pristine_props == NULL)
    pristine_props = apr_hash_make(scratch_pool);

  if (props_mod)
    SVN_ERR(svn_wc__get_actual_props(&actual_props, db, local_abspath,
                                     scratch_pool, scratch_pool));
  else
    actual_props = apr_hash_copy(scratch_pool, pristine_props);

  /* Note that while this routine does the "real" work, it's only
     prepping tempfiles and writing log commands.  */
  SVN_ERR(svn_wc__merge_props(&work_items, state,
                              &new_pristine_props, &new_actual_props,
                              db, local_abspath, kind,
                              left_version, right_version,
                              baseprops /* server_baseprops */,
                              pristine_props,
                              actual_props,
                              propchanges, base_merge, dry_run,
                              conflict_func, conflict_baton,
                              cancel_func, cancel_baton,
                              scratch_pool, scratch_pool));

  if (dry_run)
    {
      SVN_ERR_ASSERT(! work_items);
      return SVN_NO_ERROR;
    }

  {
    const char *dir_abspath;

    if (kind == svn_wc__db_kind_dir)
      dir_abspath = local_abspath;
    else
      dir_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

    /* Verify that we're holding this directory's write lock.  */
    SVN_ERR(svn_wc__write_check(db, dir_abspath, scratch_pool));

    /* After a (not-dry-run) merge, we ALWAYS have props to save.  */
    SVN_ERR_ASSERT(new_pristine_props != NULL && new_actual_props != NULL);

/* See props.h  */
#ifdef SVN__SUPPORT_BASE_MERGE
    if (status == svn_wc__db_status_added)
      SVN_ERR(svn_wc__db_temp_working_set_props(db, local_abspath,
                                                new_base_props, scratch_pool));
    else
      SVN_ERR(svn_wc__db_temp_base_set_props(db, local_abspath,
                                             new_base_props, scratch_pool));
#else
    if (base_merge)
      return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                              U_("base_merge=TRUE is no longer supported"));
#endif
    SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, new_actual_props,
                                    NULL /* conflict */,
                                    work_items,
                                    scratch_pool));

    if (work_items != NULL)
      SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                             scratch_pool));
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_merge_props3(svn_wc_notify_state_t *state,
                    svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    const svn_wc_conflict_version_t *left_version,
                    const svn_wc_conflict_version_t *right_version,
                    apr_hash_t *baseprops,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  return svn_error_return(svn_wc__perform_props_merge(
                            state,
                            wc_ctx->db,
                            local_abspath,
                            left_version, right_version,
                            baseprops,
                            propchanges,
                            FALSE /* base_merge */,
                            dry_run,
                            conflict_func, conflict_baton,
                            cancel_func, cancel_baton,
                            scratch_pool));
}


/* Generate a message to describe the property conflict among these four
   values.

   Note that this function (currently) interprets the property values as
   strings, but they could actually be binary values. We'll keep the
   types as svn_string_t in case we fix this in the future.  */
static const svn_string_t *
generate_conflict_message(const char *propname,
                          const svn_string_t *original,
                          const svn_string_t *mine,
                          const svn_string_t *incoming,
                          const svn_string_t *incoming_base,
                          apr_pool_t *result_pool)
{
  if (incoming_base == NULL)
    {
      /* Attempting to add the value INCOMING.  */
      SVN_ERR_ASSERT_NO_RETURN(incoming != NULL);

      if (mine)
        {
          /* To have a conflict, these must be different.  */
          SVN_ERR_ASSERT_NO_RETURN(!svn_string_compare(mine, incoming));

          /* Note that we don't care whether MINE is locally-added or
             edited, or just something different that is a copy of the
             pristine ORIGINAL.  */
          return svn_string_createf(result_pool,
                                    _("Trying to add new property '%s' with "
                                      "value '%s',\nbut property already "
                                      "exists with value '%s'."),
                                    propname, incoming->data, mine->data);
        }

      /* To have a conflict, we must have an ORIGINAL which has been
         locally-deleted.  */
      SVN_ERR_ASSERT_NO_RETURN(original != NULL);
      return svn_string_createf(result_pool,
                                _("Trying to create property '%s' with "
                                  "value '%s',\nbut it has been locally "
                                  "deleted."),
                                propname, incoming->data);
    }

  if (incoming == NULL)
    {
      /* Attempting to delete the value INCOMING_BASE.  */
      SVN_ERR_ASSERT_NO_RETURN(incoming_base != NULL);

      /* Are we trying to delete a local addition? */
      if (original == NULL && mine != NULL)
        return svn_string_createf(result_pool,
                                  _("Trying to delete property '%s' with "
                                    "value '%s',\nbut property has been "
                                    "locally added with value '%s'."),
                                  propname, incoming_base->data,
                                  mine->data);

      /* A conflict can only occur if we originally had the property;
         otherwise, we would have merged the property-delete into the
         non-existent property.  */
      SVN_ERR_ASSERT_NO_RETURN(original != NULL);

      if (svn_string_compare(original, incoming_base))
        {
          if (mine)
            /* We were trying to delete the correct property, but an edit
               caused the conflict.  */
            return svn_string_createf(result_pool,
                                      _("Trying to delete property '%s' with "
                                        "value '%s',\nbut it has been modified "
                                        "from '%s' to '%s'."),
                                      propname, incoming_base->data,
                                      original->data, mine->data);
        }
      else if (mine == NULL)
        {
          /* We were trying to delete the property, but we have locally
             deleted the same property, but with a different value. */
          return svn_string_createf(result_pool,
                                    _("Trying to delete property '%s' with "
                                      "value '%s',\nbut property with value "
                                      "'%s' is locally deleted."),
                                    propname, incoming_base->data,
                                    original->data);
        }

      /* We were trying to delete INCOMING_BASE but our ORIGINAL is
         something else entirely.  */
      SVN_ERR_ASSERT_NO_RETURN(!svn_string_compare(original, incoming_base));

      return svn_string_createf(result_pool,
                                _("Trying to delete property '%s' with "
                                  "value '%s',\nbut the local value is "
                                  "'%s'."),
                                propname, incoming_base->data, mine->data);
    }

  /* Attempting to change the property from INCOMING_BASE to INCOMING.  */

  /* If we have a (current) property value, then it should be different
     from the INCOMING_BASE; otherwise, the incoming change would have
     been applied to it.  */
  SVN_ERR_ASSERT_NO_RETURN(!mine || !svn_string_compare(mine, incoming_base));

  if (original && mine && svn_string_compare(original, mine))
    {
      /* We have an unchanged property, so the original values must
         have been different.  */
      SVN_ERR_ASSERT_NO_RETURN(!svn_string_compare(original, incoming_base));
      return svn_string_createf(result_pool,
                                _("Trying to change property '%s' from '%s' "
                                  "to '%s',\nbut property already exists "
                                  "with value '%s'."),
                                propname, incoming_base->data, incoming->data,
                                mine->data);
    }

  if (original && mine)
    return svn_string_createf(result_pool,
                              _("Trying to change property '%s' from '%s' "
                                "to '%s',\nbut the property has been locally "
                                "changed from '%s' to '%s'."),
                              propname, incoming_base->data, incoming->data,
                              original->data, mine->data);

  if (original)
    return svn_string_createf(result_pool,
                              _("Trying to change property '%s' from '%s' "
                                "to '%s',\nbut it has been locally deleted."),
                              propname, incoming_base->data, incoming->data);

  if (mine)
    return svn_string_createf(result_pool,
                              _("Trying to change property '%s' from '%s' "
                                "to '%s',\nbut property has been locally "
                                "added with value '%s'."),
                              propname, incoming_base->data, incoming->data,
                              mine->data);

  return svn_string_createf(result_pool,
                            _("Trying to change property '%s' from '%s' to "
                              "'%s',\nbut the property does not exist."),
                            propname, incoming_base->data, incoming->data);
}


/* SKEL will be one of:

   ()
   (VALUE)

   Return NULL for the former (the particular property value was not
   present), and VALUE for the second.  */
static const svn_string_t *
maybe_prop_value(const svn_skel_t *skel,
                 apr_pool_t *result_pool)
{
  if (skel->children == NULL)
    return NULL;

  return svn_string_ncreate(skel->children->data,
                            skel->children->len,
                            result_pool);
}


/* Generate a property conflict message (see generate_conflict_message)
   from the data contained in SKEL. The message will be allocated in
   RESULT_POOL.

   Note: SKEL is a single property conflict of the form:

   ("prop" ([ORIGINAL]) ([MINE]) ([INCOMING]) ([INCOMING_BASE]))

   See notes/wc-ng/conflict-storage for more information.  */
static const svn_string_t *
message_from_skel(const svn_skel_t *skel,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const svn_string_t *original;
  const svn_string_t *mine;
  const svn_string_t *incoming;
  const svn_string_t *incoming_base;
  const char *propname;

  /* Navigate to the property name.  */
  skel = skel->children->next;

  /* We need to copy these into SCRATCH_POOL in order to nul-terminate
     the values.  */
  propname = apr_pstrmemdup(scratch_pool, skel->data, skel->len);
  original = maybe_prop_value(skel->next, scratch_pool);
  mine = maybe_prop_value(skel->next->next, scratch_pool);
  incoming = maybe_prop_value(skel->next->next->next, scratch_pool);
  incoming_base = maybe_prop_value(skel->next->next->next->next, scratch_pool);

  return generate_conflict_message(propname, original, mine, incoming,
                                   incoming_base, result_pool);
}


/* Create a property conflict file at PREJFILE based on the property
   conflicts in CONFLICT_SKEL.  */
svn_error_t *
svn_wc__create_prejfile(const char **tmp_prejfile_abspath,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const svn_skel_t *conflict_skel,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *tempdir_abspath;
  svn_stream_t *stream;
  const char *temp_abspath;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const svn_skel_t *scan;

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tempdir_abspath,
                                         db, local_abspath,
                                         iterpool, iterpool));

  SVN_ERR(svn_stream_open_unique(&stream, &temp_abspath,
                                 tempdir_abspath, svn_io_file_del_none,
                                 scratch_pool, iterpool));

  for (scan = conflict_skel->children->next; scan != NULL; scan = scan->next)
    {
      svn_pool_clear(iterpool);

      SVN_ERR(append_prop_conflict(stream, scan, iterpool));
    }

  SVN_ERR(svn_stream_close(stream));

  svn_pool_destroy(iterpool);

  *tmp_prejfile_abspath = apr_pstrdup(result_pool, temp_abspath);
  return SVN_NO_ERROR;
}


/* Set the value of *STATE to NEW_VALUE if STATE is not NULL
 * and NEW_VALUE is a higer order value than *STATE's current value
 * using this ordering (lower order first):
 *
 * - unknown, unchanged, inapplicable
 * - changed
 * - merged
 * - missing
 * - obstructed
 * - conflicted
 *
 */
static void
set_prop_merge_state(svn_wc_notify_state_t *state,
                     svn_wc_notify_state_t new_value)
{
  static char ordering[] =
    { svn_wc_notify_state_unknown,
      svn_wc_notify_state_unchanged,
      svn_wc_notify_state_inapplicable,
      svn_wc_notify_state_changed,
      svn_wc_notify_state_merged,
      svn_wc_notify_state_obstructed,
      svn_wc_notify_state_conflicted };
  int state_pos = 0, i;

  if (! state)
    return;

  /* Find *STATE in our ordering */
  for (i = 0; i < sizeof(ordering); i++)
    {
      if (*state == ordering[i])
        {
          state_pos = i;
          break;
        }
    }

  /* Find NEW_VALUE in our ordering
   * We don't need to look further than where we found *STATE though:
   * If we find our value, it's order is too low.
   * If we don't find it, we'll want to set it, no matter its order.
   */

  for (i = 0; i <= state_pos; i++)
    {
      if (new_value == ordering[i])
        return;
    }

  *state = new_value;
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
 * copy, and OLD_VAL/NEW_VAL represents the incoming propchange.  Any
 * of these values might be NULL, indicating either non-existence or
 * intent-to-delete.
 *
 * If the callback isn't available, or if it responds with
 * 'choose_postpone', then set *CONFLICT_REMAINS to TRUE and return.
 *
 * If the callback responds with a choice of 'base', 'theirs', 'mine',
 * or 'merged', then install the proper value into WORKING_PROPS and
 * set *CONFLICT_REMAINS to FALSE.
 */
static svn_error_t *
maybe_generate_propconflict(svn_boolean_t *conflict_remains,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            const svn_wc_conflict_version_t *left_version,
                            const svn_wc_conflict_version_t *right_version,
                            svn_boolean_t is_dir,
                            const char *propname,
                            apr_hash_t *working_props,
                            const svn_string_t *old_val,
                            const svn_string_t *new_val,
                            const svn_string_t *base_val,
                            const svn_string_t *working_val,
                            svn_wc_conflict_resolver_func_t conflict_func,
                            void *conflict_baton,
                            svn_boolean_t dry_run,
                            apr_pool_t *scratch_pool)
{
  svn_wc_conflict_result_t *result = NULL;
  svn_string_t *mime_propval = NULL;
  apr_pool_t *filepool = svn_pool_create(scratch_pool);
  svn_wc_conflict_description2_t *cdesc;
  const char *dirpath = svn_dirent_dirname(local_abspath, filepool);

  if (! conflict_func || dry_run)
    {
      /* Just postpone the conflict. */
      *conflict_remains = TRUE;
      return SVN_NO_ERROR;
    }

  cdesc = svn_wc_conflict_description_create_prop2(
    local_abspath,
    is_dir ? svn_node_dir : svn_node_file, propname, scratch_pool);

  cdesc->src_left_version = left_version;
  cdesc->src_right_version = right_version;

  /* Create a tmpfile for each of the string_t's we've got.  */
  if (working_val)
    {
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, working_val->data,
                                  working_val->len,
                                  svn_io_file_del_on_pool_cleanup, filepool));
      cdesc->my_abspath = svn_dirent_join(dirpath, file_name, filepool);
    }

  if (new_val)
    {
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, new_val->data,
                                  new_val->len, svn_io_file_del_on_pool_cleanup,
                                  filepool));
      cdesc->their_abspath = svn_dirent_join(dirpath, file_name, filepool);
    }

  if (!base_val && !old_val)
    {
      /* If base and old are both NULL, then that's fine, we just let
         base_file stay NULL as-is.  Both agents are attempting to add a
         new property.  */
    }

  else if ((base_val && !old_val)
           || (!base_val && old_val))
    {
      /* If only one of base and old are defined, then we've got a
         situation where one agent is attempting to add the property
         for the first time, and the other agent is changing a
         property it thinks already exists.  In this case, we return
         whichever older-value happens to be defined, so that the
         conflict-callback can still attempt a 3-way merge. */

      const svn_string_t *the_val = base_val ? base_val : old_val;
      const char *file_name;

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, the_val->data,
                                  the_val->len, svn_io_file_del_on_pool_cleanup,
                                  filepool));
      cdesc->base_abspath = svn_dirent_join(dirpath, file_name, filepool);
    }

  else  /* base and old are both non-NULL */
    {
      const svn_string_t *the_val;
      const char *file_name;

      if (! svn_string_compare(base_val, old_val))
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
            the_val = old_val;
          else
            the_val = base_val;
        }
      else
        {
          the_val = base_val;
        }

      SVN_ERR(svn_io_write_unique(&file_name, dirpath, the_val->data,
                                  the_val->len, svn_io_file_del_on_pool_cleanup,
                                  filepool));
      cdesc->base_abspath = svn_dirent_join(dirpath, file_name, filepool);

      if (working_val && new_val)
        {
          svn_stream_t *mergestream;
          svn_diff_t *diff;
          svn_diff_file_options_t *options =
            svn_diff_file_options_create(filepool);

          SVN_ERR(svn_stream_open_unique(&mergestream, &cdesc->merged_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         filepool, scratch_pool));
          SVN_ERR(svn_diff_mem_string_diff3(&diff, the_val, working_val,
                                            new_val, options, filepool));
          SVN_ERR(svn_diff_mem_string_output_merge2
                  (mergestream, diff, the_val, working_val, new_val,
                   NULL, NULL, NULL, NULL,
                   svn_diff_conflict_display_modified_latest, filepool));
          SVN_ERR(svn_stream_close(mergestream));
        }
    }

  /* Build the rest of the description object: */
  if (!is_dir && working_props)
    mime_propval = apr_hash_get(working_props, SVN_PROP_MIME_TYPE,
                                APR_HASH_KEY_STRING);
  cdesc->mime_type = mime_propval ? mime_propval->data : NULL;
  cdesc->is_binary = mime_propval ?
      svn_mime_type_is_binary(mime_propval->data) : FALSE;

  if (!old_val && new_val)
    cdesc->action = svn_wc_conflict_action_add;
  else if (old_val && !new_val)
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
    svn_wc_conflict_description_t *cd = svn_wc__cd2_to_cd(cdesc, scratch_pool);
    SVN_ERR(conflict_func(&result, cd, conflict_baton, scratch_pool));
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
          /* No need to change working_props; it already contains working_val */
          *conflict_remains = FALSE;
          break;
        }
      /* I think _mine_full and _theirs_full are appropriate for prop
         behavior as well as the text behavior.  There should even be
         analogous behaviors for _mine and _theirs when those are
         ready, namely: fold in all non-conflicting prop changes, and
         then choose _mine side or _theirs side for conflicting ones. */
      case svn_wc_conflict_choose_theirs_full:
        {
          apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);
          *conflict_remains = FALSE;
          break;
        }
      case svn_wc_conflict_choose_base:
        {
          apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, base_val);
          *conflict_remains = FALSE;
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
              merged_string = svn_string_create_from_buf(merged_stringbuf,
                                                         scratch_pool);
              apr_hash_set(working_props, propname,
                           APR_HASH_KEY_STRING, merged_string);
              *conflict_remains = FALSE;
            }
          break;
        }
    }

  /* Delete any tmpfiles we made. */
  svn_pool_destroy(filepool);

  return SVN_NO_ERROR;
}


/* Add the property with name PROPNAME to the set of WORKING_PROPS on
 * PATH, setting *STATE or *CONFLICT_REMAINS according to merge outcomes.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_merge_prop_state().
 *
 * BASE_VAL contains the working copy base property value
 *
 * NEW_VAL contains the value to be set.
 *
 * CONFLICT_FUNC/BATON is a callback to be called before declaring a
 * property conflict;  it gives the client a chance to resolve the
 * conflict interactively.
 */
static svn_error_t *
apply_single_prop_add(svn_wc_notify_state_t *state,
                      svn_boolean_t *conflict_remains,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      const svn_wc_conflict_version_t *left_version,
                      const svn_wc_conflict_version_t *right_version,
                      svn_boolean_t is_dir,
                      apr_hash_t *working_props,
                      const char *propname,
                      const svn_string_t *base_val,
                      const svn_string_t *new_val,
                      svn_wc_conflict_resolver_func_t conflict_func,
                      void *conflict_baton,
                      svn_boolean_t dry_run,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)

{
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  *conflict_remains = FALSE;

  if (working_val)
    {
      /* the property already exists in working_props... */

      if (svn_string_compare(working_val, new_val))
        /* The value we want is already there, so it's a merge. */
        set_prop_merge_state(state, svn_wc_notify_state_merged);

      else
        {
          /* The WC difference doesn't match the new value.
           We only merge mergeinfo;  other props conflict */
          if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
            {
              const svn_string_t *merged_val;

              SVN_ERR(combine_mergeinfo_props(&merged_val, working_val,
                                              new_val, result_pool,
                                              scratch_pool));
              apr_hash_set(working_props, propname,
                           APR_HASH_KEY_STRING, merged_val);
              set_prop_merge_state(state, svn_wc_notify_state_merged);
            }
          else
            {
              SVN_ERR(maybe_generate_propconflict(conflict_remains,
                                                  db, local_abspath,
                                                  left_version, right_version,
                                                  is_dir,
                                                  propname, working_props,
                                                  NULL, new_val,
                                                  base_val, working_val,
                                                  conflict_func,
                                                  conflict_baton,
                                                  dry_run, scratch_pool));
            }
        }
    }
  else if (base_val)
    {
      SVN_ERR(maybe_generate_propconflict(conflict_remains,
                                          db, local_abspath,
                                          left_version, right_version,
                                          is_dir, propname,
                                          working_props, NULL, new_val,
                                          base_val, NULL,
                                          conflict_func, conflict_baton,
                                          dry_run, scratch_pool));
    }
  else  /* property doesn't yet exist in working_props...  */
    /* so just set it */
    apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);

  return SVN_NO_ERROR;
}


/* Delete the property with name PROPNAME from the set of
 * WORKING_PROPS on PATH, setting *STATE or *CONFLICT_REMAINS according to
 * merge outcomes.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_merge_prop_state().
 *
 * BASE_VAL contains the working copy base property value
 *
 * OLD_VAL contains the value the of the property the server
 * thinks it's deleting.
 *
 * CONFLICT_FUNC/BATON is a callback to be called before declaring a
 * property conflict;  it gives the client a chance to resolve the
 * conflict interactively.
 */
static svn_error_t *
apply_single_prop_delete(svn_wc_notify_state_t *state,
                         svn_boolean_t *conflict_remains,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const svn_wc_conflict_version_t *left_version,
                         const svn_wc_conflict_version_t *right_version,
                         svn_boolean_t is_dir,
                         apr_hash_t *working_props,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         svn_wc_conflict_resolver_func_t conflict_func,
                         void *conflict_baton,
                         svn_boolean_t dry_run,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  *conflict_remains = FALSE;

  if (! base_val)
    {
      if (working_val
          && !svn_string_compare(working_val, old_val))
        {
          /* We are trying to delete a locally-added prop. */
          SVN_ERR(maybe_generate_propconflict(conflict_remains,
                                              db, local_abspath,
                                              left_version, right_version,
                                              is_dir, propname,
                                              working_props, old_val, NULL,
                                              base_val, working_val,
                                              conflict_func, conflict_baton,
                                              dry_run, scratch_pool));
        }
      else
        {
          apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, NULL);
          if (old_val)
            /* This is a merge, merging a delete into non-existent
               property or a local addition of same prop value. */
            set_prop_merge_state(state, svn_wc_notify_state_merged);
        }
    }

  else if (svn_string_compare(base_val, old_val))
    {
       if (working_val)
         {
           if (svn_string_compare(working_val, old_val))
             /* they have the same values, so it's an update */
             apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, NULL);
           else
             {
               SVN_ERR(maybe_generate_propconflict(conflict_remains,
                                                   db, local_abspath,
                                                   left_version, right_version,
                                                   is_dir,
                                                   propname, working_props,
                                                   old_val, NULL,
                                                   base_val, working_val,
                                                   conflict_func,
                                                   conflict_baton,
                                                   dry_run, scratch_pool));
             }
         }
       else
         /* The property is locally deleted from the same value, so it's
            a merge */
         set_prop_merge_state(state, svn_wc_notify_state_merged);
    }

  else
    {
      SVN_ERR(maybe_generate_propconflict(conflict_remains,
                                          db, local_abspath,
                                          left_version, right_version,
                                          is_dir, propname,
                                          working_props, old_val, NULL,
                                          base_val, working_val,
                                          conflict_func, conflict_baton,
                                          dry_run, scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* Merge a change to the mergeinfo property. The same as
   apply_single_prop_change(), except that the PROPNAME is always
   SVN_PROP_MERGEINFO. */
/* ### This function is extracted straight from the previous all-in-one
   version of apply_single_prop_change() by removing the code paths that
   were not followed for this property, but with no attempt to rationalize
   the remainder. */
static svn_error_t *
apply_single_mergeinfo_prop_change(svn_wc_notify_state_t *state,
                                   svn_boolean_t *conflict_remains,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const svn_wc_conflict_version_t *left_version,
                                   const svn_wc_conflict_version_t *right_version,
                                   svn_boolean_t is_dir,
                                   apr_hash_t *working_props,
                                   const char *propname,
                                   const svn_string_t *base_val,
                                   const svn_string_t *old_val,
                                   const svn_string_t *new_val,
                                   svn_wc_conflict_resolver_func_t conflict_func,
                                   void *conflict_baton,
                                   svn_boolean_t dry_run,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  if ((working_val && ! base_val)
      || (! working_val && base_val)
      || (working_val && base_val
          && !svn_string_compare(working_val, base_val)))
    {
      /* Locally changed property */
      if (working_val)
        {
          if (svn_string_compare(working_val, new_val))
            /* The new value equals the changed value: a no-op merge */
            set_prop_merge_state(state, svn_wc_notify_state_merged);
          else
            {
                  /* We have base, WC, and new values.  Discover
                     deltas between base <-> WC, and base <->
                     incoming.  Combine those deltas, and apply
                     them to base to get the new value. */
                  SVN_ERR(combine_forked_mergeinfo_props(&new_val, old_val,
                                                         working_val,
                                                         new_val,
                                                         result_pool));
                  apr_hash_set(working_props, propname,
                               APR_HASH_KEY_STRING, new_val);
                  set_prop_merge_state(state, svn_wc_notify_state_merged);
            }
        }

      else
        {
          /* There is a base_val but no working_val */
          SVN_ERR(maybe_generate_propconflict(conflict_remains,
                                              db, local_abspath,
                                              left_version, right_version,
                                              is_dir, propname, working_props,
                                              old_val, new_val,
                                              base_val, working_val,
                                              conflict_func, conflict_baton,
                                              dry_run, scratch_pool));
        }
    }

  else if (! working_val) /* means !working_val && !base_val due
                             to conditions above: no prop at all */
    {
          /* Discover any mergeinfo additions in the
             incoming value relative to the base, and
             "combine" those with the empty WC value. */
          svn_mergeinfo_t deleted_mergeinfo, added_mergeinfo;
          svn_string_t *mergeinfo_string;

          SVN_ERR(diff_mergeinfo_props(&deleted_mergeinfo,
                                       &added_mergeinfo,
                                       old_val, new_val, scratch_pool));
          SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string,
                                          added_mergeinfo, result_pool));
          apr_hash_set(working_props, propname, APR_HASH_KEY_STRING,
                       mergeinfo_string);
    }

  else /* means working && base && svn_string_compare(working, base) */
    {
      if (svn_string_compare(old_val, base_val))
        apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);

      else
        {
              /* We have base, WC, and new values.  Discover
                 deltas between base <-> WC, and base <->
                 incoming.  Combine those deltas, and apply
                 them to base to get the new value. */
              SVN_ERR(combine_forked_mergeinfo_props(&new_val, old_val,
                                                     working_val,
                                                     new_val, result_pool));
              apr_hash_set(working_props, propname,
                           APR_HASH_KEY_STRING, new_val);
              set_prop_merge_state(state, svn_wc_notify_state_merged);
        }
    }

  return SVN_NO_ERROR;
}

/* Merge a change to a property, using the rule that if the working value
   is the same as OLD_VAL then apply the change as a simple update
   (replacement), otherwise invoke maybe_generate_propconflict().
   The definition of the arguments and behaviour is the same as
   apply_single_prop_change(). */
static svn_error_t *
apply_single_generic_prop_change(svn_wc_notify_state_t *state,
                                 svn_boolean_t *conflict_remains,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 const svn_wc_conflict_version_t *left_version,
                                 const svn_wc_conflict_version_t *right_version,
                                 svn_boolean_t is_dir,
                                 apr_hash_t *working_props,
                                 const char *propname,
                                 const svn_string_t *base_val,
                                 const svn_string_t *old_val,
                                 const svn_string_t *new_val,
                                 svn_wc_conflict_resolver_func_t conflict_func,
                                 void *conflict_baton,
                                 svn_boolean_t dry_run,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_string_t *working_val
    = apr_hash_get(working_props, propname, APR_HASH_KEY_STRING);

  SVN_ERR_ASSERT(old_val != NULL);

  /* If working_val is the same as old_val... */
  if (working_val && old_val
      && svn_string_compare(working_val, old_val))
    {
      /* A trivial update: change it to new_val. */
      apr_hash_set(working_props, propname, APR_HASH_KEY_STRING, new_val);
    }
  else
    {
      /* Merge the change. */
      SVN_ERR(maybe_generate_propconflict(conflict_remains,
                                          db, local_abspath,
                                          left_version, right_version,
                                          is_dir, propname, working_props,
                                          old_val, new_val,
                                          base_val, working_val,
                                          conflict_func, conflict_baton,
                                          dry_run, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Change the property with name PROPNAME in the set of WORKING_PROPS
 * on PATH, setting *STATE or *CONFLICT_REMAINS according to the merge outcome.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_prop_merge_state(). (May be null.).
 *
 * BASE_VAL contains the working copy base property value. (May be null.)
 *
 * OLD_VAL contains the value of the property the server
 * thinks it's overwriting. (Not null.)
 *
 * NEW_VAL contains the value to be set. (Not null.)
 *
 * CONFLICT_FUNC/BATON is a callback to be called before declaring a
 * property conflict;  it gives the client a chance to resolve the
 * conflict interactively.
 */
static svn_error_t *
apply_single_prop_change(svn_wc_notify_state_t *state,
                         svn_boolean_t *conflict_remains,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const svn_wc_conflict_version_t *left_version,
                         const svn_wc_conflict_version_t *right_version,
                         svn_boolean_t is_dir,
                         apr_hash_t *working_props,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         const svn_string_t *new_val,
                         svn_wc_conflict_resolver_func_t conflict_func,
                         void *conflict_baton,
                         svn_boolean_t dry_run,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  *conflict_remains = FALSE;

  /* Note: The purpose is to apply the change (old_val -> new_val) onto
     (working_val). There is no need for base_val to be involved in the
     process except as a bit of context to help the user understand and
     resolve any conflict. */

  /* Decide how to merge, based on whether we know anything special about
     the property. */
  if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
    {
      /* We know how to merge any mergeinfo property change. */

      SVN_ERR(apply_single_mergeinfo_prop_change(state, conflict_remains,
                                                 db, local_abspath,
                                                 left_version, right_version,
                                                 is_dir,
                                                 working_props,
                                                 propname, base_val, old_val,
                                                 new_val,
                                                 conflict_func, conflict_baton,
                                                 dry_run,
                                                 result_pool, scratch_pool));
    }
  else
    {
      /* The standard method: perform a simple update automatically, but
         pass any other kind of merge to maybe_generate_propconflict(). */

      SVN_ERR(apply_single_generic_prop_change(state, conflict_remains,
                                               db, local_abspath,
                                               left_version, right_version,
                                               is_dir,
                                               working_props,
                                               propname, base_val, old_val,
                                               new_val,
                                               conflict_func, conflict_baton,
                                               dry_run,
                                               result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__merge_props(svn_skel_t **work_items,
                    svn_wc_notify_state_t *state,
                    apr_hash_t **new_pristine_props,
                    apr_hash_t **new_actual_props,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_wc__db_kind_t kind,
                    const svn_wc_conflict_version_t *left_version,
                    const svn_wc_conflict_version_t *right_version,
                    apr_hash_t *server_baseprops,
                    apr_hash_t *pristine_props,
                    apr_hash_t *actual_props,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  int i;
  svn_boolean_t is_dir;
  svn_skel_t *conflict_skel = NULL;

  SVN_ERR_ASSERT(pristine_props != NULL);
  SVN_ERR_ASSERT(actual_props != NULL);

  *work_items = NULL;

  *new_pristine_props = NULL;
  *new_actual_props = NULL;

  is_dir = (kind == svn_wc__db_kind_dir);

  if (!server_baseprops)
    server_baseprops = pristine_props;

  if (state)
    {
      /* Start out assuming no changes or conflicts.  Don't bother to
         examine propchanges->nelts yet; even if we knew there were
         propchanges, we wouldn't yet know if they are "normal" props,
         as opposed wc or entry props.  */
      *state = svn_wc_notify_state_unchanged;
    }

  /* Looping over the array of incoming propchanges we want to apply: */
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < propchanges->nelts; i++)
    {
      const char *propname;
      svn_boolean_t conflict_remains;
      const svn_prop_t *incoming_change;
      const svn_string_t *from_val, *to_val, *base_val;
      const svn_string_t *mine_val;

      svn_pool_clear(iterpool);

      /* Should we stop the prop merging process?  */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* For the incoming propchange, figure out the TO and FROM values. */
      incoming_change = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);
      propname = incoming_change->name;
      to_val = incoming_change->value
        ? svn_string_dup(incoming_change->value, result_pool) : NULL;
      from_val = apr_hash_get(server_baseprops, propname, APR_HASH_KEY_STRING);

      base_val = apr_hash_get(pristine_props, propname, APR_HASH_KEY_STRING);

      if (base_merge)
        apr_hash_set(pristine_props, propname, APR_HASH_KEY_STRING, to_val);

      /* Save MINE for later message generation.  */
      mine_val = apr_hash_get(actual_props, propname, APR_HASH_KEY_STRING);

      /* We already know that state is at least `changed', so mark
         that, but remember that we may later upgrade to `merged' or
         even `conflicted'. */
      set_prop_merge_state(state, svn_wc_notify_state_changed);

      if (! from_val)  /* adding a new property */
        SVN_ERR(apply_single_prop_add(state, &conflict_remains,
                                      db, local_abspath,
                                      left_version, right_version,
                                      is_dir, actual_props,
                                      propname, base_val, to_val,
                                      conflict_func, conflict_baton,
                                      dry_run, result_pool, iterpool));

      else if (! to_val) /* delete an existing property */
        SVN_ERR(apply_single_prop_delete(state, &conflict_remains,
                                         db, local_abspath,
                                         left_version, right_version,
                                         is_dir,
                                         actual_props,
                                         propname, base_val, from_val,
                                         conflict_func, conflict_baton,
                                         dry_run, result_pool, iterpool));

      else  /* changing an existing property */
        SVN_ERR(apply_single_prop_change(state, &conflict_remains,
                                         db, local_abspath,
                                         left_version, right_version,
                                         is_dir,
                                         actual_props,
                                         propname, base_val, from_val, to_val,
                                         conflict_func, conflict_baton,
                                         dry_run, result_pool, iterpool));


      /* merging logic complete, now we need to possibly log conflict
         data to tmpfiles.  */

      if (conflict_remains)
        {
          set_prop_merge_state(state, svn_wc_notify_state_conflicted);

          if (dry_run)
            continue;   /* skip to next incoming change */

          if (conflict_skel == NULL)
            conflict_skel = svn_wc__conflict_skel_new(result_pool);

          SVN_ERR(svn_wc__conflict_skel_add_prop_conflict(conflict_skel,
                                                          propname,
                                                          base_val,
                                                          mine_val,
                                                          to_val,
                                                          from_val,
                                                          result_pool,
                                                          iterpool));
        }

    }  /* foreach propchange ... */
  svn_pool_destroy(iterpool);

  /* Finished applying all incoming propchanges to our hashes! */

  if (dry_run)
    return SVN_NO_ERROR;

  *new_pristine_props = pristine_props;
  *new_actual_props = actual_props;

  if (conflict_skel != NULL)
    {
      const char *reject_path;

      /* Now try to get the name of a pre-existing .prej file from the
         entries file */
      SVN_ERR(svn_wc__get_prejfile_abspath(&reject_path, db, local_abspath,
                                           scratch_pool, scratch_pool));

      if (! reject_path)
        {
          /* Reserve a new .prej file *above* the .svn/ directory by
             opening and closing it. */
          const char *reject_dirpath;
          const char *reject_filename;
          svn_skel_t *work_item;

          if (is_dir)
            {
              reject_dirpath = local_abspath;
              reject_filename = SVN_WC__THIS_DIR_PREJ;
            }
          else
            svn_dirent_split(&reject_dirpath, &reject_filename, local_abspath,
                             scratch_pool);

          SVN_ERR(svn_io_open_uniquely_named(NULL, &reject_path,
                                             reject_dirpath,
                                             reject_filename,
                                             SVN_WC__PROP_REJ_EXT,
                                             svn_io_file_del_none,
                                             scratch_pool, scratch_pool));

          /* This file will be overwritten when the wq is run; that's
             ok, because at least now we have a reservation on
             disk. */

          /* Mark entry as "conflicted" with a particular .prej file. */
          SVN_ERR(svn_wc__wq_tmp_build_set_property_conflict_marker(
                                          &work_item,
                                          db, local_abspath, reject_path,
                                          result_pool, scratch_pool));

          *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);
        }

      /* Once the prejfile is recorded, then install the file.  */
      {
        svn_skel_t *work_item;

        SVN_ERR(svn_wc__wq_build_prej_install(&work_item,
                                              db, local_abspath,
                                              conflict_skel,
                                              result_pool, scratch_pool));

        *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);
      }
    }

  return SVN_NO_ERROR;
}


/* Set a single 'wcprop' NAME to VALUE for versioned object LOCAL_ABSPATH.
   If VALUE is null, remove property NAME.  */
static svn_error_t *
wcprop_set(svn_wc__db_t *db,
           const char *local_abspath,
           const char *name,
           const svn_string_t *value,
           apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Note: this is not well-transacted. But... meh. This is merely a cache,
     and if two processes are trying to modify this one entry at the same
     time, then fine: we can let one be a winner, and one a loser. Of course,
     if there are *other* state changes afoot, then the lack of a txn could
     be a real issue, but we cannot solve that here.  */

  SVN_ERR(svn_wc__db_base_get_dav_cache(&prophash, db, local_abspath,
                                        scratch_pool, scratch_pool));

  if (prophash == NULL)
    prophash = apr_hash_make(scratch_pool);

  apr_hash_set(prophash, name, APR_HASH_KEY_STRING, value);
  return svn_error_return(svn_wc__db_base_set_dav_cache(db, local_abspath,
                                                        prophash,
                                                        scratch_pool));
}


svn_error_t *
svn_wc__get_actual_props(apr_hash_t **props,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* ### perform some state checking. for example, locally-deleted nodes
     ### should not have any ACTUAL props.  */

  return svn_error_return(svn_wc__db_read_props(props, db, local_abspath,
                                                result_pool, scratch_pool));
}


svn_error_t *
svn_wc_prop_list2(apr_hash_t **props,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  return svn_error_return(svn_wc__get_actual_props(props,
                                                   wc_ctx->db,
                                                   local_abspath,
                                                   result_pool,
                                                   scratch_pool));
}

svn_error_t *
svn_wc__prop_list_recursive(svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            const char *propname,
                            svn_depth_t depth,
                            svn_boolean_t base_props,
                            svn_boolean_t pristine,
                            svn_wc__proplist_receiver_t receiver_func,
                            void *receiver_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  switch (depth)
    {
    case svn_depth_empty:
      {
        apr_hash_t *props;

        if (pristine)
          SVN_ERR(svn_wc__db_read_pristine_props(&props, wc_ctx->db,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
        else
          SVN_ERR(svn_wc__db_read_props(&props, wc_ctx->db, local_abspath,
                                        scratch_pool, scratch_pool));

        if (receiver_func && props && apr_hash_count(props) > 0)
          SVN_ERR((*receiver_func)(receiver_baton, local_abspath, props,
                                   scratch_pool));
      }
      break;
    case svn_depth_files:
    case svn_depth_immediates:
    case svn_depth_infinity:
      SVN_ERR(svn_wc__db_read_props_streamily(wc_ctx->db, local_abspath,
                                              propname, depth,
                                              base_props, pristine,
                                              receiver_func, receiver_baton,
                                              cancel_func, cancel_baton,
                                              scratch_pool));
      break;
    default:
      SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__get_pristine_props(apr_hash_t **props,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;

  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Certain node stats do not have properties defined on them. Check the
     state, and return NULL for these situations.  */

  SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));
  if (status == svn_wc__db_status_added)
    {
      /* Resolve the status. copied and moved_here arrive with properties,
         while a simple add does not.  */
      SVN_ERR(svn_wc__db_scan_addition(&status, NULL,
                                       NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL,
                                       db, local_abspath,
                                       scratch_pool, scratch_pool));
    }
  if (status == svn_wc__db_status_added
#if 0
      /* ### the update editor needs to fetch properties while the directory
         ### is still marked incomplete  */
      || status == svn_wc__db_status_incomplete
#endif
      || status == svn_wc__db_status_excluded
      || status == svn_wc__db_status_absent
      || status == svn_wc__db_status_not_present)
    {
      *props = NULL;
      return SVN_NO_ERROR;
    }

  /* status: normal, moved_here, copied, deleted  */

  /* After the above checks, these pristines should always be present.  */
  return svn_error_return(
               svn_wc__db_read_pristine_props(props, db, local_abspath,
                                              result_pool, scratch_pool));
}


svn_error_t *
svn_wc_get_pristine_props(apr_hash_t **props,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return svn_error_return(svn_wc__get_pristine_props(props,
                                                     wc_ctx->db,
                                                     local_abspath,
                                                     result_pool,
                                                     scratch_pool));
}


svn_error_t *
svn_wc_prop_get2(const svn_string_t **value,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (kind == svn_prop_entry_kind)
    {
      /* we don't do entry properties here */
      return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                               _("Property '%s' is an entry property"), name);
    }

  SVN_ERR(svn_wc__internal_propget(value, wc_ctx->db, local_abspath, name,
                                   result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_propget(const svn_string_t **value,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *name,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash = NULL;
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  svn_boolean_t hidden;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(kind != svn_prop_entry_kind);

  /* This returns SVN_ERR_WC_PATH_NOT_FOUND for unversioned paths for us */
  SVN_ERR(svn_wc__db_node_hidden(&hidden, db, local_abspath, scratch_pool));
  if (hidden)
    {
      /* The node is not present, or not really "here". Therefore, the
         property is not present.  */
      *value = NULL;
      return SVN_NO_ERROR;
    }

  if (kind == svn_prop_wc_kind)
    {
      svn_error_t *err;
      /* If no dav cache can be found, just set VALUE to NULL (for
         compatibility with pre-WC-NG code). */
      err = svn_wc__db_base_get_dav_cache(&prophash, db, local_abspath,
                                          result_pool, scratch_pool);
      if (err && (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND))
        {
          *value = NULL;
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      SVN_ERR_W(err, _("Failed to load properties"));
    }
  else
    {
      /* regular prop */
      SVN_ERR_W(svn_wc__get_actual_props(&prophash, db, local_abspath,
                                         result_pool, scratch_pool),
                _("Failed to load properties"));
    }

  if (prophash)
    *value = apr_hash_get(prophash, name, APR_HASH_KEY_STRING);
  else
    *value = NULL;

  return SVN_NO_ERROR;
}


/* The special Subversion properties are not valid for all node kinds.
   Return an error if NAME is an invalid Subversion property for PATH which
   is of kind NODE_KIND. */
static svn_error_t *
validate_prop_against_node_kind(const char *name,
                                const char *path,
                                svn_node_kind_t node_kind,
                                apr_pool_t *pool)
{

  const char *file_prohibit[] = { SVN_PROP_IGNORE,
                                  SVN_PROP_EXTERNALS,
                                  NULL };
  const char *dir_prohibit[] = { SVN_PROP_EXECUTABLE,
                                 SVN_PROP_KEYWORDS,
                                 SVN_PROP_EOL_STYLE,
                                 SVN_PROP_MIME_TYPE,
                                 SVN_PROP_NEEDS_LOCK,
                                 NULL };
  const char **node_kind_prohibit;
  const char *path_display
    = svn_path_is_url(path) ? path : svn_dirent_local_style(path, pool);

  switch (node_kind)
    {
    case svn_node_dir:
      node_kind_prohibit = dir_prohibit;
      while (*node_kind_prohibit)
        if (strcmp(name, *node_kind_prohibit++) == 0)
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Cannot set '%s' on a directory ('%s')"),
                                   name, path_display);
      break;
    case svn_node_file:
      node_kind_prohibit = file_prohibit;
      while (*node_kind_prohibit)
        if (strcmp(name, *node_kind_prohibit++) == 0)
          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                   _("Cannot set '%s' on a file ('%s')"),
                                   name,
                                   path_display);
      break;
    default:
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("'%s' is not a file or directory"),
                               path_display);
    }

  return SVN_NO_ERROR;
}


struct getter_baton {
  const char *local_abspath;
  svn_wc__db_t *db;
};


/* */
static svn_error_t *
get_file_for_validation(const svn_string_t **mime_type,
                        svn_stream_t *stream,
                        void *baton,
                        apr_pool_t *pool)
{
  struct getter_baton *gb = baton;

  if (mime_type)
    SVN_ERR(svn_wc__internal_propget(mime_type, gb->db, gb->local_abspath,
                                     SVN_PROP_MIME_TYPE, pool, pool));

  if (stream)
    {
      svn_stream_t *read_stream;

      /* Open PATH. */
      SVN_ERR(svn_stream_open_readonly(&read_stream, gb->local_abspath,
                                       pool, pool));

      /* Copy from the file into the translating stream. */
      SVN_ERR(svn_stream_copy3(read_stream, svn_stream_disown(stream, pool),
                               NULL, NULL, pool));
    }

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
validate_eol_prop_against_file(const char *path,
                               svn_wc_canonicalize_svn_prop_get_file_t getter,
                               void *getter_baton,
                               apr_pool_t *pool)
{
  svn_stream_t *translating_stream;
  svn_error_t *err;
  const svn_string_t *mime_type;
  const char *path_display
    = svn_path_is_url(path) ? path : svn_dirent_local_style(path, pool);

  /* First just ask the "getter" for the MIME type. */
  SVN_ERR(getter(&mime_type, NULL, getter_baton, pool));

  /* See if this file has been determined to be binary. */
  if (mime_type && svn_mime_type_is_binary(mime_type->data))
    return svn_error_createf
      (SVN_ERR_ILLEGAL_TARGET, NULL,
       _("File '%s' has binary mime type property"),
       path_display);

  /* Now ask the getter for the contents of the file; this will do a
     newline translation.  All we really care about here is whether or
     not the function fails on inconsistent line endings.  The
     function is "translating" to an empty stream.  This is
     sneeeeeeeeeeeaky. */
  translating_stream = svn_subst_stream_translated(svn_stream_empty(pool),
                                                   "", FALSE, NULL, FALSE,
                                                   pool);

  err = getter(NULL, translating_stream, getter_baton, pool);

  if (!err)
    err = svn_stream_close(translating_stream);

  if (err && err->apr_err == SVN_ERR_IO_INCONSISTENT_EOL)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, err,
                             _("File '%s' has inconsistent newlines"),
                             path_display);
  else if (err)
    return err;

  return SVN_NO_ERROR;
}

static svn_error_t *
do_propset(svn_wc__db_t *db,
           const char *local_abspath,
           const char *name,
           const svn_string_t *value,
           svn_boolean_t skip_checks,
           const apr_hash_t *changelists,
           svn_wc_notify_func2_t notify_func,
           void *notify_baton,
           apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash;
  enum svn_prop_kind prop_kind = svn_property_kind(NULL, name);
  svn_wc_notify_action_t notify_action;
  svn_wc__db_kind_t kind;
  svn_wc__db_status_t status;
  svn_skel_t *work_item = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (!svn_wc__internal_changelist_match(db, local_abspath,
                                         changelists, scratch_pool))
    return SVN_NO_ERROR;

  /* Get the node kind for this path. */
  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  if (prop_kind == svn_prop_wc_kind)
    return svn_error_return(wcprop_set(db, local_abspath, name, value,
                                       scratch_pool));

  if (status != svn_wc__db_status_normal
      && status != svn_wc__db_status_added
      && status != svn_wc__db_status_incomplete)
    return svn_error_createf(SVN_ERR_WC_INVALID_SCHEDULE, NULL,
                             _("Can't set properties on '%s':"
                               " invalid status for updating properties."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Else, handle a regular property: */


  /* Setting an inappropriate property is not allowed (unless
     overridden by 'skip_checks', in some circumstances).  Deleting an
     inappropriate property is allowed, however, since older clients
     allowed (and other clients possibly still allow) setting it in
     the first place. */
  if (value && svn_prop_is_svn_prop(name))
    {
      const svn_string_t *new_value;
      struct getter_baton gb;

      gb.local_abspath = local_abspath;
      gb.db = db;

      SVN_ERR(svn_wc_canonicalize_svn_prop(&new_value, name, value,
                                           local_abspath,
                                           kind == svn_wc__db_kind_dir ?
                                                    svn_node_dir :
                                                    svn_node_file,
                                           skip_checks,
                                           get_file_for_validation, &gb,
                                           scratch_pool));
      value = new_value;
    }

  SVN_ERR(svn_wc__wq_build_sync_file_flags(&work_item, db, local_abspath,
                                           scratch_pool, scratch_pool));

  SVN_ERR_W(svn_wc__db_read_props(&prophash, db, local_abspath,
                                  scratch_pool, scratch_pool),
            _("Failed to load current properties"));

  /* If we're changing this file's list of expanded keywords, then
   * we'll need to invalidate its text timestamp, since keyword
   * expansion affects the comparison of working file to text base.
   *
   * Here we retrieve the old list of expanded keywords; after the
   * property is set, we'll grab the new list and see if it differs
   * from the old one.
   */
  if (kind == svn_wc__db_kind_file && strcmp(name, SVN_PROP_KEYWORDS) == 0)
    {
      svn_string_t *old_value = apr_hash_get(prophash, SVN_PROP_KEYWORDS,
                                             APR_HASH_KEY_STRING);
      apr_hash_t *old_keywords, *new_keywords;

      if (old_value)
        SVN_ERR(svn_wc__expand_keywords(&old_keywords,
                                        db, local_abspath, old_value->data,
                                        scratch_pool, scratch_pool));
      else
        old_keywords = apr_hash_make(scratch_pool);

      if (value)
        SVN_ERR(svn_wc__expand_keywords(&new_keywords,
                                        db, local_abspath, value->data,
                                        scratch_pool, scratch_pool));
      else
        new_keywords = apr_hash_make(scratch_pool);

      if (svn_subst_keywords_differ2(old_keywords, new_keywords, FALSE,
                                     scratch_pool))
        {
          /* If the keywords have changed, then the translation of the file
             may be different. We should invalidate the cached TRANSLATED_SIZE
             and LAST_MOD_TIME on this node.

             Note that we don't immediately re-translate the file. But a
             "has it changed?" check in the future will do a translation
             from the pristine, and it will want to compare the (new)
             resulting TRANSLATED_SIZE against the working copy file.

             Also, when this file is (de)translated with the new keywords,
             then it could be different, relative to the pristine. We want
             to ensure the LAST_MOD_TIME is different, to indicate that
             a full detranslate/compare is performed.  */
          /* ### we should be performing similar logic for changes to the
             ### svn:eol-style property.  */
          SVN_ERR(svn_wc__db_global_record_fileinfo(db, local_abspath,
                                                    SVN_INVALID_FILESIZE, 0,
                                                    scratch_pool));
        }
    }

  /* Find out what type of property change we are doing: add, modify, or
     delete. */
  if (apr_hash_get(prophash, name, APR_HASH_KEY_STRING) == NULL)
    {
      if (value == NULL)
        /* Deleting a non-existent property. */
        notify_action = svn_wc_notify_property_deleted_nonexistent;
      else
        /* Adding a property. */
        notify_action = svn_wc_notify_property_added;
    }
  else
    {
      if (value == NULL)
        /* Deleting the property. */
        notify_action = svn_wc_notify_property_deleted;
      else
        /* Modifying property. */
        notify_action = svn_wc_notify_property_modified;
    }

  /* Now we have all the properties in our hash.  Simply merge the new
     property into it. */
  apr_hash_set(prophash, name, APR_HASH_KEY_STRING, value);

  /* Drop it right into the db..  */
  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, prophash, NULL, work_item,
                                  scratch_pool));

  /* Run our workqueue item for sync'ing flags with props. */
  SVN_ERR(svn_wc__wq_run(db, local_abspath, NULL, NULL, scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     notify_action,
                                                     scratch_pool);
      notify->prop_name = name;

      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* A baton for propset_walk_cb. */
struct propset_walk_baton
{
  const char *propname;  /* The name of the property to set. */
  const svn_string_t *propval;  /* The value to set. */
  svn_wc__db_t *db;  /* Database for the tree being walked. */
  svn_boolean_t force;  /* True iff force was passed. */
  const apr_hash_t *changelists;  /* Changelists to filter on. */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

/* An node-walk callback for svn_wc_prop_set4().
 *
 * For LOCAL_ABSPATH, set the property named wb->PROPNAME to the value
 * wb->PROPVAL, where "wb" is the WALK_BATON of type "struct
 * propset_walk_baton *".
 */
static svn_error_t *
propset_walk_cb(const char *local_abspath,
                svn_node_kind_t kind,
                void *walk_baton,
                apr_pool_t *scratch_pool)
{
  struct propset_walk_baton *wb = walk_baton;
  svn_error_t *err;

  err = do_propset(wb->db, local_abspath, wb->propname, wb->propval,
                   wb->force, wb->changelists, wb->notify_func,
                   wb->notify_baton, scratch_pool);
  if (err && (err->apr_err == SVN_ERR_ILLEGAL_TARGET
              || err->apr_err == SVN_ERR_WC_INVALID_SCHEDULE))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_prop_set4(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 const svn_string_t *value,
                 svn_depth_t depth,
                 svn_boolean_t skip_checks,
                 const apr_array_header_t *changelists,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *scratch_pool)
{
  enum svn_prop_kind prop_kind = svn_property_kind(NULL, name);
  apr_hash_t *changelist_hash = NULL;
  svn_wc__db_kind_t kind;
  const char *dir_abspath;

  /* we don't do entry properties here */
  if (prop_kind == svn_prop_entry_kind)
    return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                             _("Property '%s' is an entry property"), name);

  /* We have to do this little DIR_ABSPATH dance for backwards compat.
     But from 1.7 onwards, all locks are of infinite depth, and from 1.6
     backward we never call this API with depth > empty, so we only need
     to do the write check once per call, here (and not for every node in
     the node walker). */
  SVN_ERR(svn_wc__db_read_kind(&kind, wc_ctx->db, local_abspath, TRUE,
                               scratch_pool));

  if (kind == svn_wc__db_kind_dir)
    dir_abspath = local_abspath;
  else
    dir_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(svn_wc__write_check(wc_ctx->db, dir_abspath, scratch_pool));

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists,
                                       scratch_pool));

  if (depth == svn_depth_empty)
    {
      if (!svn_wc__internal_changelist_match(wc_ctx->db, local_abspath,
                                             changelist_hash, scratch_pool))
        return SVN_NO_ERROR;

      SVN_ERR(do_propset(wc_ctx->db, local_abspath, name, value, skip_checks,
                         changelist_hash, notify_func, notify_baton,
                         scratch_pool));
    }
  else
    {
      struct propset_walk_baton wb = { name, value, wc_ctx->db, skip_checks,
                                       changelist_hash, notify_func,
                                       notify_baton };

      SVN_ERR(svn_wc__internal_walk_children(wc_ctx->db, local_abspath, FALSE,
                                             propset_walk_cb, &wb,
                                             depth,
                                             NULL, NULL,  /* cancellation */
                                             scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_canonicalize_svn_prop(const svn_string_t **propval_p,
                             const char *propname,
                             const svn_string_t *propval,
                             const char *path,
                             svn_node_kind_t kind,
                             svn_boolean_t skip_some_checks,
                             svn_wc_canonicalize_svn_prop_get_file_t getter,
                             void *getter_baton,
                             apr_pool_t *pool)
{
  svn_stringbuf_t *new_value = NULL;

  /* Keep this static, it may get stored (for read-only purposes) in a
     hash that outlives this function. */
  static const svn_string_t boolean_value =
    {
      SVN_PROP_BOOLEAN_TRUE,
      sizeof(SVN_PROP_BOOLEAN_TRUE) - 1
    };

  SVN_ERR(validate_prop_against_node_kind(propname, path, kind, pool));

  if (!skip_some_checks && (strcmp(propname, SVN_PROP_EOL_STYLE) == 0))
    {
      svn_subst_eol_style_t eol_style;
      const char *ignored_eol;
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
      svn_subst_eol_style_from_value(&eol_style, &ignored_eol, new_value->data);
      if (eol_style == svn_subst_eol_style_unknown)
        return svn_error_createf(SVN_ERR_IO_UNKNOWN_EOL, NULL,
                                 _("Unrecognized line ending style '%s' for '%s'"),
                                 new_value->data,
                                 svn_dirent_local_style(path, pool));
      SVN_ERR(validate_eol_prop_against_file(path, getter, getter_baton,
                                             pool));
    }
  else if (!skip_some_checks && (strcmp(propname, SVN_PROP_MIME_TYPE) == 0))
    {
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
      SVN_ERR(svn_mime_type_validate(new_value->data, pool));
    }
  else if (strcmp(propname, SVN_PROP_IGNORE) == 0
           || strcmp(propname, SVN_PROP_EXTERNALS) == 0)
    {
      /* Make sure that the last line ends in a newline */
      if (propval->data[propval->len - 1] != '\n')
        {
          new_value = svn_stringbuf_create_from_string(propval, pool);
          svn_stringbuf_appendbyte(new_value, '\n');
        }

      /* Make sure this is a valid externals property.  Do not
         allow 'skip_some_checks' to override, as there is no circumstance in
         which this is proper (because there is no circumstance in
         which Subversion can handle it). */
      if (strcmp(propname, SVN_PROP_EXTERNALS) == 0)
        {
          /* We don't allow "." nor ".." as target directories in
             an svn:externals line.  As it happens, our parse code
             checks for this, so all we have to is invoke it --
             we're not interested in the parsed result, only in
             whether or the parsing errored. */
          SVN_ERR(svn_wc_parse_externals_description3
                  (NULL, path, propval->data, FALSE, pool));
        }
    }
  else if (strcmp(propname, SVN_PROP_KEYWORDS) == 0)
    {
      new_value = svn_stringbuf_create_from_string(propval, pool);
      svn_stringbuf_strip_whitespace(new_value);
    }
  else if (svn_prop_is_boolean(propname))
    {
      /* SVN_PROP_EXECUTABLE, SVN_PROP_NEEDS_LOCK, SVN_PROP_SPECIAL */
      new_value = svn_stringbuf_create_from_string(&boolean_value, pool);
    }
  else if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
    {
      apr_hash_t *mergeinfo;
      svn_string_t *new_value_str;

      SVN_ERR(svn_mergeinfo_parse(&mergeinfo, propval->data, pool));

      /* Non-inheritable mergeinfo is only valid on directories. */
      if (kind != svn_node_dir
          && svn_mergeinfo__is_noninheritable(mergeinfo, pool))
        return svn_error_createf(
          SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
          _("Cannot set non-inheritable mergeinfo on a non-directory ('%s')"),
          svn_dirent_local_style(path, pool));

      SVN_ERR(svn_mergeinfo_to_string(&new_value_str, mergeinfo, pool));
      new_value = svn_stringbuf_create_from_string(new_value_str, pool);
    }

  if (new_value)
    *propval_p = svn_string_create_from_buf(new_value, pool);
  else
    *propval_p = propval;

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc_is_normal_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  return (kind == svn_prop_regular_kind);
}


svn_boolean_t
svn_wc_is_wc_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  return (kind == svn_prop_wc_kind);
}


svn_boolean_t
svn_wc_is_entry_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind(NULL, name);
  return (kind == svn_prop_entry_kind);
}


svn_error_t *
svn_wc__props_modified(svn_boolean_t *modified_p,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, modified_p, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_props_modified_p2(svn_boolean_t *modified_p,
                         svn_wc_context_t* wc_ctx,
                         const char *local_abspath,
                         apr_pool_t *scratch_pool)
{
  return svn_error_return(
             svn_wc__props_modified(modified_p,
                                    wc_ctx->db,
                                    local_abspath,
                                    scratch_pool));
}

svn_error_t *
svn_wc__internal_propdiff(apr_array_header_t **propchanges,
                          apr_hash_t **original_props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_hash_t *baseprops;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* ### if pristines are not defined, then should this raise an error,
     ### or use an empty set?  */
  SVN_ERR(svn_wc__db_read_pristine_props(&baseprops, db, local_abspath,
                                         result_pool, scratch_pool));

  if (original_props != NULL)
    *original_props = baseprops;

  if (propchanges != NULL)
    {
      apr_hash_t *actual_props;

      /* Some nodes do not have pristine props, so let's just use an empty
         set here. Thus, any ACTUAL props are additions.  */
      if (baseprops == NULL)
        baseprops = apr_hash_make(scratch_pool);

      SVN_ERR(svn_wc__db_read_props(&actual_props, db, local_abspath,
                                    result_pool, scratch_pool));
      /* ### be wary. certain nodes don't have ACTUAL props either. we
         ### may want to raise an error. or maybe that is a deletion of
         ### any potential pristine props?  */

      SVN_ERR(svn_prop_diffs(propchanges, actual_props, baseprops,
                             result_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_prop_diffs2(apr_array_header_t **propchanges,
                       apr_hash_t **original_props,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  return svn_error_return(svn_wc__internal_propdiff(propchanges,
                                    original_props, wc_ctx->db, local_abspath,
                                    result_pool, scratch_pool));
}



/** Externals **/

/*
 * Look for either
 *
 *   -r N
 *   -rN
 *
 * in the LINE_PARTS array and update the revision field in ITEM with
 * the revision if the revision is found.  Set REV_IDX to the index in
 * LINE_PARTS where the revision specification starts.  Remove from
 * LINE_PARTS the element(s) that specify the revision.
 * PARENT_DIRECTORY_DISPLAY and LINE are given to return a nice error
 * string.
 *
 * If this function returns successfully, then LINE_PARTS will have
 * only two elements in it.
 */
static svn_error_t *
find_and_remove_externals_revision(int *rev_idx,
                                   const char **line_parts,
                                   int num_line_parts,
                                   svn_wc_external_item2_t *item,
                                   const char *parent_directory_display,
                                   const char *line,
                                   apr_pool_t *pool)
{
  int i;

  for (i = 0; i < 2; ++i)
    {
      const char *token = line_parts[i];

      if (token[0] == '-' && token[1] == 'r')
        {
          svn_opt_revision_t end_revision = { svn_opt_revision_unspecified };
          const char *digits_ptr;
          int shift_count;
          int j;

          *rev_idx = i;

          if (token[2] == '\0')
            {
              /* There must be a total of four elements in the line if
                 -r N is used. */
              if (num_line_parts != 4)
                goto parse_error;

              shift_count = 2;
              digits_ptr = line_parts[i+1];
            }
          else
            {
              /* There must be a total of three elements in the line
                 if -rN is used. */
              if (num_line_parts != 3)
                goto parse_error;

              shift_count = 1;
              digits_ptr = token+2;
            }

          if (svn_opt_parse_revision(&item->revision,
                                     &end_revision,
                                     digits_ptr, pool) != 0)
            goto parse_error;
          /* We want a single revision, not a range. */
          if (end_revision.kind != svn_opt_revision_unspecified)
            goto parse_error;
          /* Allow only numbers and dates, not keywords. */
          if (item->revision.kind != svn_opt_revision_number
              && item->revision.kind != svn_opt_revision_date)
            goto parse_error;

          /* Shift any line elements past the revision specification
             down over the revision specification. */
          for (j = i; j < num_line_parts-shift_count; ++j)
            line_parts[j] = line_parts[j+shift_count];
          line_parts[num_line_parts-shift_count] = NULL;

          /* Found the revision, so leave the function immediately, do
           * not continue looking for additional revisions. */
          return SVN_NO_ERROR;
        }
    }

  /* No revision was found, so there must be exactly two items in the
     line array. */
  if (num_line_parts == 2)
    return SVN_NO_ERROR;

 parse_error:
  return svn_error_createf
    (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
     _("Error parsing %s property on '%s': '%s'"),
     SVN_PROP_EXTERNALS,
     parent_directory_display,
     line);
}

svn_error_t *
svn_wc_parse_externals_description3(apr_array_header_t **externals_p,
                                    const char *parent_directory,
                                    const char *desc,
                                    svn_boolean_t canonicalize_url,
                                    apr_pool_t *pool)
{
  apr_array_header_t *lines = svn_cstring_split(desc, "\n\r", TRUE, pool);
  int i;
  const char *parent_directory_display = svn_path_is_url(parent_directory) ?
    parent_directory : svn_dirent_local_style(parent_directory, pool);

  if (externals_p)
    *externals_p = apr_array_make(pool, 1, sizeof(svn_wc_external_item2_t *));

  for (i = 0; i < lines->nelts; i++)
    {
      const char *line = APR_ARRAY_IDX(lines, i, const char *);
      apr_status_t status;
      char **line_parts;
      int num_line_parts;
      svn_wc_external_item2_t *item;
      const char *token0;
      const char *token1;
      svn_boolean_t token0_is_url;
      svn_boolean_t token1_is_url;

      /* Index into line_parts where the revision specification
         started. */
      int rev_idx = -1;

      if ((! line) || (line[0] == '#'))
        continue;

      /* else proceed */

      status = apr_tokenize_to_argv(line, &line_parts, pool);
      if (status)
        return svn_error_wrap_apr(status,
                                  _("Can't split line into components: '%s'"),
                                  line);
      /* Count the number of tokens. */
      for (num_line_parts = 0; line_parts[num_line_parts]; num_line_parts++)
        ;

      SVN_ERR(svn_wc_external_item_create
              ((const svn_wc_external_item2_t **) &item, pool));
      item->revision.kind = svn_opt_revision_unspecified;
      item->peg_revision.kind = svn_opt_revision_unspecified;

      /*
       * There are six different formats of externals:
       *
       * 1) DIR URL
       * 2) DIR -r N URL
       * 3) DIR -rN  URL
       * 4) URL DIR
       * 5) -r N URL DIR
       * 6) -rN URL DIR
       *
       * The last three allow peg revisions in the URL.
       *
       * With relative URLs and no '-rN' or '-r N', there is no way to
       * distinguish between 'DIR URL' and 'URL DIR' when URL is a
       * relative URL like /svn/repos/trunk, so this case is taken as
       * case 4).
       */
      if (num_line_parts < 2 || num_line_parts > 4)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Error parsing %s property on '%s': '%s'"),
           SVN_PROP_EXTERNALS,
           parent_directory_display,
           line);

      /* To make it easy to check for the forms, find and remove -r N
         or -rN from the line item array.  If it is found, rev_idx
         contains the index into line_parts where '-r' was found and
         set item->revision to the parsed revision. */
      /* ### ugh. stupid cast. */
      SVN_ERR(find_and_remove_externals_revision(&rev_idx,
                                                 (const char **)line_parts,
                                                 num_line_parts, item,
                                                 parent_directory_display,
                                                 line, pool));

      token0 = line_parts[0];
      token1 = line_parts[1];

      token0_is_url = svn_path_is_url(token0);
      token1_is_url = svn_path_is_url(token1);

      if (token0_is_url && token1_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use two absolute URLs ('%s' and '%s') in an external; "
             "one must be a path where an absolute or relative URL is "
             "checked out to"),
           SVN_PROP_EXTERNALS, parent_directory_display, token0, token1);

      if (0 == rev_idx && token1_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use a URL '%s' as the target directory for an external "
             "definition"),
           SVN_PROP_EXTERNALS, parent_directory_display, token1);

      if (1 == rev_idx && token0_is_url)
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "cannot use a URL '%s' as the target directory for an external "
             "definition"),
           SVN_PROP_EXTERNALS, parent_directory_display, token0);

      /* The appearence of -r N or -rN forces the type of external.
         If -r is at the beginning of the line or the first token is
         an absolute URL or if the second token is not an absolute
         URL, then the URL supports peg revisions. */
      if (0 == rev_idx ||
          (-1 == rev_idx && (token0_is_url || ! token1_is_url)))
        {
          /* The URL is passed to svn_opt_parse_path in
             uncanonicalized form so that the scheme relative URL
             //hostname/foo is not collapsed to a server root relative
             URL /hostname/foo. */
          SVN_ERR(svn_opt_parse_path(&item->peg_revision, &item->url,
                                     token0, pool));
          item->target_dir = token1;
        }
      else
        {
          item->target_dir = token0;
          item->url = token1;
          item->peg_revision = item->revision;
        }

      SVN_ERR(svn_opt_resolve_revisions(&item->peg_revision,
                                        &item->revision, TRUE, FALSE,
                                        pool));

      item->target_dir = svn_dirent_internal_style(item->target_dir, pool);

      if (item->target_dir[0] == '\0' || item->target_dir[0] == '/'
          || svn_path_is_backpath_present(item->target_dir))
        return svn_error_createf
          (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
           _("Invalid %s property on '%s': "
             "target '%s' is an absolute path or involves '..'"),
           SVN_PROP_EXTERNALS,
           parent_directory_display,
           item->target_dir);

      if (canonicalize_url)
        {
          /* Uh... this is stupid.  But it's consistent with what our
             code did before we split up the relpath/dirent/uri APIs.
             Still, given this, it's no wonder that our own libraries
             don't ask this function to canonicalize the results.  */
          if (svn_path_is_url(item->url))
            item->url = svn_uri_canonicalize(item->url, pool);
          else
            item->url = svn_dirent_canonicalize(item->url, pool);
        }

      if (externals_p)
        APR_ARRAY_PUSH(*externals_p, svn_wc_external_item2_t *) = item;
    }

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc__has_magic_property(const apr_array_header_t *properties)
{
  int i;

  for (i = 0; i < properties->nelts; i++)
    {
      const svn_prop_t *property = &APR_ARRAY_IDX(properties, i, svn_prop_t);

      if (strcmp(property->name, SVN_PROP_EXECUTABLE) == 0
          || strcmp(property->name, SVN_PROP_KEYWORDS) == 0
          || strcmp(property->name, SVN_PROP_EOL_STYLE) == 0
          || strcmp(property->name, SVN_PROP_SPECIAL) == 0
          || strcmp(property->name, SVN_PROP_NEEDS_LOCK) == 0)
        return TRUE;
    }
  return FALSE;
}
