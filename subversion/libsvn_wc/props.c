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
#include "svn_sorts.h"

#include "private/svn_wc_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_skel.h"
#include "private/svn_string_private.h"

#include "wc.h"
#include "props.h"
#include "translate.h"
#include "workqueue.h"
#include "conflicts.h"

#include "svn_private_config.h"

/* Forward declaration.  */
static svn_error_t *
prop_conflict_from_skel(const svn_string_t **conflict_desc,
                        const svn_skel_t *skel,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Given a *SINGLE* property conflict in PROP_SKEL, generate a description
   for it, and write it to STREAM, along with a trailing EOL sequence.

   See prop_conflict_from_skel() for details on PROP_SKEL.  */
static svn_error_t *
append_prop_conflict(svn_stream_t *stream,
                     const svn_skel_t *prop_skel,
                     apr_pool_t *pool)
{
  /* TODO:  someday, perhaps prefix each conflict_description with a
     timestamp or something? */
  const svn_string_t *conflict_desc;
  const char *native_text;

  SVN_ERR(prop_conflict_from_skel(&conflict_desc, prop_skel, pool, pool));
  native_text = svn_utf_cstring_from_utf8_fuzzy(conflict_desc->data, pool);

  return svn_stream_puts(stream, native_text);
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
      SVN_ERR(svn_mergeinfo_diff2(deleted, added, from, to,
                                  TRUE, pool, pool));
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
  SVN_ERR(svn_mergeinfo_merge2(mergeinfo1, mergeinfo2, scratch_pool,
                               scratch_pool));
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
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t from_mergeinfo, l_deleted, l_added, r_deleted, r_added;
  svn_string_t *mergeinfo_string;

  /* ### OPTIMIZE: Use from_mergeinfo when diff'ing. */
  SVN_ERR(diff_mergeinfo_props(&l_deleted, &l_added, from_prop_val,
                               working_prop_val, scratch_pool));
  SVN_ERR(diff_mergeinfo_props(&r_deleted, &r_added, from_prop_val,
                               to_prop_val, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(l_deleted, r_deleted,
                               scratch_pool, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(l_added, r_added,
                               scratch_pool, scratch_pool));

  /* Apply the combined deltas to the base. */
  SVN_ERR(svn_mergeinfo_parse(&from_mergeinfo, from_prop_val->data,
                              scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(from_mergeinfo, l_added,
                               scratch_pool, scratch_pool));

  SVN_ERR(svn_mergeinfo_remove2(&from_mergeinfo, l_deleted, from_mergeinfo,
                                TRUE, scratch_pool, scratch_pool));

  SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_string, from_mergeinfo,
                                  result_pool));
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
                            svn_wc_conflict_resolver_func2_t conflict_func,
                            void *conflict_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  int i;
  svn_wc__db_status_t status;
  svn_kind_t kind;
  apr_hash_t *pristine_props = NULL;
  apr_hash_t *actual_props = NULL;
  apr_hash_t *new_pristine_props;
  apr_hash_t *new_actual_props;
  svn_boolean_t had_props, props_mod;
  svn_boolean_t have_base;
  svn_boolean_t conflicted;
  svn_skel_t *work_items = NULL;
  svn_skel_t *conflict_skel = NULL;

  /* IMPORTANT: svn_wc_merge_prop_diffs relies on the fact that baseprops
     may be NULL. */

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, &conflicted, NULL,
                               &had_props, &props_mod, &have_base, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  /* Checks whether the node exists and returns the hidden flag */
  if (status == svn_wc__db_status_not_present
      || status == svn_wc__db_status_server_excluded
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
  else if (conflicted)
      {
        svn_boolean_t text_conflicted;
        svn_boolean_t prop_conflicted;
        svn_boolean_t tree_conflicted;

        SVN_ERR(svn_wc__internal_conflicted_p(&text_conflicted,
                                              &prop_conflicted,
                                              &tree_conflicted,
                                              db, local_abspath,
                                              scratch_pool));

        /* We can't install two text/prop conflicts on a single node, so
           avoid even checking that we have to merge it */
        if (text_conflicted || prop_conflicted || tree_conflicted)
          {
            return svn_error_createf(
                            SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                            _("Can't merge into conflicted node '%s'"),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool));
          }
        /* else: Conflict was resolved by removing markers */
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
    SVN_ERR(svn_wc__db_read_pristine_props(&pristine_props, db, local_abspath,
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
  SVN_ERR(svn_wc__merge_props(&conflict_skel, state,
                              &new_pristine_props, &new_actual_props,
                              db, local_abspath, kind,
                              baseprops /* server_baseprops */,
                              pristine_props,
                              actual_props,
                              propchanges, base_merge, dry_run,
                              cancel_func, cancel_baton,
                              scratch_pool, scratch_pool));

  if (dry_run)
    {
      return SVN_NO_ERROR;
    }

  if (conflict_skel)
    {
      svn_skel_t *work_item;
      SVN_ERR(svn_wc__conflict_skel_set_op_merge(conflict_skel,
                                                 left_version,
                                                 right_version,
                                                 scratch_pool,
                                                 scratch_pool));

      SVN_ERR(svn_wc__conflict_create_markers(&work_item,
                                              db, local_abspath,
                                              conflict_skel,
                                              scratch_pool, scratch_pool));

      work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
    }

  {
    const char *dir_abspath;

    if (kind == svn_kind_dir)
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
                                    svn_wc__has_magic_property(propchanges),
                                    conflict_skel,
                                    work_items,
                                    scratch_pool));

    if (work_items != NULL)
      SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                             scratch_pool));
  }

  if (conflict_skel && conflict_func)
    SVN_ERR(svn_wc__conflict_invoke_resolver(db, local_abspath, conflict_skel,
                                             NULL /* merge_options */,
                                             conflict_func, conflict_baton,
                                             scratch_pool));

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
                    svn_wc_conflict_resolver_func2_t conflict_func,
                    void *conflict_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__perform_props_merge(
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
static svn_stringbuf_t *
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
          return svn_stringbuf_createf(result_pool,
                                       _("Trying to add new property '%s'\n"
                                         "but the property already exists.\n"),
                                       propname);
        }

      /* To have a conflict, we must have an ORIGINAL which has been
         locally-deleted.  */
      SVN_ERR_ASSERT_NO_RETURN(original != NULL);
      return svn_stringbuf_createf(result_pool,
                                   _("Trying to add new property '%s'\n"
                                     "but the property has been locally "
                                     "deleted.\n"),
                                   propname);
    }

  if (incoming == NULL)
    {
      /* Attempting to delete the value INCOMING_BASE.  */
      SVN_ERR_ASSERT_NO_RETURN(incoming_base != NULL);

      /* Are we trying to delete a local addition? */
      if (original == NULL && mine != NULL)
        return svn_stringbuf_createf(result_pool,
                                     _("Trying to delete property '%s'\n"
                                       "but the property has been locally "
                                       "added.\n"),
                                     propname);

      /* A conflict can only occur if we originally had the property;
         otherwise, we would have merged the property-delete into the
         non-existent property.  */
      SVN_ERR_ASSERT_NO_RETURN(original != NULL);

      if (svn_string_compare(original, incoming_base))
        {
          if (mine)
            /* We were trying to delete the correct property, but an edit
               caused the conflict.  */
            return svn_stringbuf_createf(result_pool,
                                         _("Trying to delete property '%s'\n"
                                           "but the property has been locally "
                                           "modified.\n"),
                                         propname);
        }
      else if (mine == NULL)
        {
          /* We were trying to delete the property, but we have locally
             deleted the same property, but with a different value. */
          return svn_stringbuf_createf(result_pool,
                                       _("Trying to delete property '%s'\n"
                                         "but the property has been locally "
                                         "deleted and had a different "
                                         "value.\n"),
                                       propname);
        }

      /* We were trying to delete INCOMING_BASE but our ORIGINAL is
         something else entirely.  */
      SVN_ERR_ASSERT_NO_RETURN(!svn_string_compare(original, incoming_base));

      return svn_stringbuf_createf(result_pool,
                                   _("Trying to delete property '%s'\n"
                                     "but the local property value is "
                                     "different.\n"),
                                   propname);
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
      return svn_stringbuf_createf(result_pool,
                                   _("Trying to change property '%s'\n"
                                     "but the local property value conflicts "
                                     "with the incoming change.\n"),
                                   propname);
    }

  if (original && mine)
    return svn_stringbuf_createf(result_pool,
                                 _("Trying to change property '%s'\n"
                                   "but the property has already been locally "
                                   "changed to a different value.\n"),
                                 propname);

  if (original)
    return svn_stringbuf_createf(result_pool,
                                 _("Trying to change property '%s'\nbut "
                                   "the property has been locally deleted.\n"),
                                 propname);

  if (mine)
    return svn_stringbuf_createf(result_pool,
                                 _("Trying to change property '%s'\nbut the "
                                   "property has been locally added with a "
                                   "different value.\n"),
                                 propname);

  return svn_stringbuf_createf(result_pool,
                               _("Trying to change property '%s'\nbut "
                                 "the property does not exist locally.\n"),
                               propname);
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


/* Parse a property conflict description from the provided SKEL.
   The result includes a descriptive message (see generate_conflict_message)
   and maybe a diff of property values containing conflict markers.
   The result will be allocated in RESULT_POOL.

   Note: SKEL is a single property conflict of the form:

   ("prop" ([ORIGINAL]) ([MINE]) ([INCOMING]) ([INCOMING_BASE]))

   See notes/wc-ng/conflict-storage for more information.  */
static svn_error_t *
prop_conflict_from_skel(const svn_string_t **conflict_desc,
                        const svn_skel_t *skel,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const svn_string_t *original;
  const svn_string_t *mine;
  const svn_string_t *incoming;
  const svn_string_t *incoming_base;
  const char *propname;
  svn_diff_t *diff;
  svn_diff_file_options_t *diff_opts;
  svn_stringbuf_t *buf;
  svn_boolean_t original_is_binary;
  svn_boolean_t mine_is_binary;
  svn_boolean_t incoming_is_binary;

  /* Navigate to the property name.  */
  skel = skel->children->next;

  /* We need to copy these into SCRATCH_POOL in order to nul-terminate
     the values.  */
  propname = apr_pstrmemdup(scratch_pool, skel->data, skel->len);
  original = maybe_prop_value(skel->next, scratch_pool);
  mine = maybe_prop_value(skel->next->next, scratch_pool);
  incoming = maybe_prop_value(skel->next->next->next, scratch_pool);
  incoming_base = maybe_prop_value(skel->next->next->next->next, scratch_pool);

  buf = generate_conflict_message(propname, original, mine, incoming,
                                  incoming_base, scratch_pool);

  if (mine == NULL)
    mine = svn_string_create_empty(scratch_pool);
  if (incoming == NULL)
    incoming = svn_string_create_empty(scratch_pool);

  /* Pick a suitable base for the conflict diff.
   * The incoming value is always a change,
   * but the local value might not have changed. */
  if (original == NULL)
    {
      if (incoming_base)
        original = incoming_base;
      else
        original = svn_string_create_empty(scratch_pool);
    }
  else if (incoming_base && svn_string_compare(original, mine))
    original = incoming_base;

  /* If any of the property values involved in the diff is binary data,
   * do not generate a diff. */
  original_is_binary = svn_io_is_binary_data(original->data, original->len);
  mine_is_binary = svn_io_is_binary_data(mine->data, mine->len);
  incoming_is_binary = svn_io_is_binary_data(incoming->data, incoming->len);

  if (!(original_is_binary || mine_is_binary || incoming_is_binary))
    {
      diff_opts = svn_diff_file_options_create(scratch_pool);
      diff_opts->ignore_space = svn_diff_file_ignore_space_none;
      diff_opts->ignore_eol_style = FALSE;
      diff_opts->show_c_function = FALSE;
      SVN_ERR(svn_diff_mem_string_diff3(&diff, original, mine, incoming,
                                        diff_opts, scratch_pool));
      if (svn_diff_contains_conflicts(diff))
        {
          svn_stream_t *stream;
          svn_diff_conflict_display_style_t style;
          const char *mine_marker = _("<<<<<<< (local property value)");
          const char *incoming_marker = _(">>>>>>> (incoming property value)");
          const char *separator = "=======";

          style = svn_diff_conflict_display_modified_latest;
          stream = svn_stream_from_stringbuf(buf, scratch_pool);
          SVN_ERR(svn_stream_skip(stream, buf->len));
          SVN_ERR(svn_diff_mem_string_output_merge2(stream, diff,
                                                    original, mine, incoming,
                                                    NULL, mine_marker,
                                                    incoming_marker, separator,
                                                    style, scratch_pool));
          SVN_ERR(svn_stream_close(stream));

          *conflict_desc = svn_string_create_from_buf(buf, result_pool);
          return SVN_NO_ERROR;
        }
    }

  /* If we could not print a conflict diff just print full values . */
  if (mine->len > 0)
    {
      svn_stringbuf_appendcstr(buf, _("Local property value:\n"));
      if (mine_is_binary)
        svn_stringbuf_appendcstr(buf, _("Cannot display: property value is "
                                        "binary data\n"));
      else
        svn_stringbuf_appendbytes(buf, mine->data, mine->len);
      svn_stringbuf_appendcstr(buf, "\n");
    }

  if (incoming->len > 0)
    {
      svn_stringbuf_appendcstr(buf, _("Incoming property value:\n"));
      if (incoming_is_binary)
        svn_stringbuf_appendcstr(buf, _("Cannot display: property value is "
                                        "binary data\n"));
      else
        svn_stringbuf_appendbytes(buf, incoming->data, incoming->len);
      svn_stringbuf_appendcstr(buf, "\n");
    }

  *conflict_desc = svn_string_create_from_buf(buf, result_pool);
  return SVN_NO_ERROR;
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

/* Add the property with name PROPNAME to the set of ACTUAL_PROPS on
 * PATH, setting *STATE or *CONFLICT_REMAINS according to merge outcomes.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_merge_prop_state().
 *
 * BASE_VAL contains the working copy base property value
 *
 * NEW_VAL contains the value to be set.
 */
static svn_error_t *
apply_single_prop_add(svn_wc_notify_state_t *state,
                      svn_boolean_t *conflict_remains,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_hash_t *actual_props,
                      const char *propname,
                      const svn_string_t *base_val,
                      const svn_string_t *new_val,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)

{
  svn_string_t *working_val
    = apr_hash_get(actual_props, propname, APR_HASH_KEY_STRING);

  *conflict_remains = FALSE;

  if (working_val)
    {
      /* the property already exists in actual_props... */

      if (svn_string_compare(working_val, new_val))
        /* The value we want is already there, so it's a merge. */
        set_prop_merge_state(state, svn_wc_notify_state_merged);

      else
        {
          svn_boolean_t merged_prop = FALSE;

          /* The WC difference doesn't match the new value.
           We only merge mergeinfo;  other props conflict */
          if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
            {
              const svn_string_t *merged_val;
              svn_error_t *err = combine_mergeinfo_props(&merged_val,
                                                         working_val,
                                                         new_val,
                                                         result_pool,
                                                         scratch_pool);

              /* Issue #3896 'mergeinfo syntax errors should be treated
                 gracefully': If bogus mergeinfo is present we can't
                 merge intelligently, so raise a conflict instead. */
              if (err)
                {
                  if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
                    svn_error_clear(err);
                  else
                    return svn_error_trace(err);
                  }
              else
                {
                  merged_prop = TRUE;
                  apr_hash_set(actual_props, propname,
                               APR_HASH_KEY_STRING, merged_val);
                  set_prop_merge_state(state, svn_wc_notify_state_merged);
                }
            }

          if (!merged_prop)
            *conflict_remains = TRUE;
        }
    }
  else if (base_val)
    *conflict_remains = TRUE;
  else  /* property doesn't yet exist in actual_props...  */
    /* so just set it */
    apr_hash_set(actual_props, propname, APR_HASH_KEY_STRING, new_val);

  return SVN_NO_ERROR;
}


/* Delete the property with name PROPNAME from the set of
 * ACTUAL_PROPS on PATH, setting *STATE or *CONFLICT_REMAINS according to
 * merge outcomes.
 *
 * *STATE is an input and output parameter, its value is to be
 * set using set_merge_prop_state().
 *
 * BASE_VAL contains the working copy base property value
 *
 * OLD_VAL contains the value the of the property the server
 * thinks it's deleting.
 */
static svn_error_t *
apply_single_prop_delete(svn_wc_notify_state_t *state,
                         svn_boolean_t *conflict_remains,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_hash_t *actual_props,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_string_t *working_val
    = apr_hash_get(actual_props, propname, APR_HASH_KEY_STRING);

  *conflict_remains = FALSE;

  if (! base_val)
    {
      if (working_val
          && !svn_string_compare(working_val, old_val))
        {
          /* We are trying to delete a locally-added prop. */
          *conflict_remains = TRUE;
        }
      else
        {
          apr_hash_set(actual_props, propname, APR_HASH_KEY_STRING, NULL);
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
             apr_hash_set(actual_props, propname, APR_HASH_KEY_STRING, NULL);
           else
             *conflict_remains = TRUE;
         }
       else
         /* The property is locally deleted from the same value, so it's
            a merge */
         set_prop_merge_state(state, svn_wc_notify_state_merged);
    }

  else
    *conflict_remains = TRUE;

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
                                   apr_hash_t *actual_props,
                                   const char *propname,
                                   const svn_string_t *base_val,
                                   const svn_string_t *old_val,
                                   const svn_string_t *new_val,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_string_t *working_val
    = apr_hash_get(actual_props, propname, APR_HASH_KEY_STRING);

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
                                                         result_pool,
                                                         scratch_pool));
                  apr_hash_set(actual_props, propname,
                               APR_HASH_KEY_STRING, new_val);
                  set_prop_merge_state(state, svn_wc_notify_state_merged);
            }
        }

      else
        {
          /* There is a base_val but no working_val */
          *conflict_remains = TRUE;
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
          apr_hash_set(actual_props, propname, APR_HASH_KEY_STRING,
                       mergeinfo_string);
    }

  else /* means working && base && svn_string_compare(working, base) */
    {
      if (svn_string_compare(old_val, base_val))
        apr_hash_set(actual_props, propname, APR_HASH_KEY_STRING, new_val);

      else
        {
              /* We have base, WC, and new values.  Discover
                 deltas between base <-> WC, and base <->
                 incoming.  Combine those deltas, and apply
                 them to base to get the new value. */
              SVN_ERR(combine_forked_mergeinfo_props(&new_val, old_val,
                                                     working_val,
                                                     new_val, result_pool,
                                                     scratch_pool));
              apr_hash_set(actual_props, propname,
                           APR_HASH_KEY_STRING, new_val);
              set_prop_merge_state(state, svn_wc_notify_state_merged);
        }
    }

  return SVN_NO_ERROR;
}

/* Merge a change to a property, using the rule that if the working value
   is equal to the new value then there is nothing we need to do. Else, if
   the working value is the same as the old value then apply the change as a
   simple update (replacement), otherwise invoke maybe_generate_propconflict().
   The definition of the arguments and behaviour is the same as
   apply_single_prop_change(). */
static svn_error_t *
apply_single_generic_prop_change(svn_wc_notify_state_t *state,
                                 svn_boolean_t *conflict_remains,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_hash_t *actual_props,
                                 const char *propname,
                                 const svn_string_t *base_val,
                                 const svn_string_t *old_val,
                                 const svn_string_t *new_val,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_string_t *working_val
    = apr_hash_get(actual_props, propname, APR_HASH_KEY_STRING);

  SVN_ERR_ASSERT(old_val != NULL);

  /* If working_val is the same as new_val already then there is
   * nothing to do */
  if (working_val && new_val
      && svn_string_compare(working_val, new_val))
    {
      /* All values identical is a trivial, non-notifiable merge */
      if (! old_val || ! svn_string_compare(old_val, new_val))
        set_prop_merge_state(state, svn_wc_notify_state_merged);
    }
  /* If working_val is the same as old_val... */
  else if (working_val && old_val
      && svn_string_compare(working_val, old_val))
    {
      /* A trivial update: change it to new_val. */
      apr_hash_set(actual_props, propname, APR_HASH_KEY_STRING, new_val);
    }
  else
    {
      /* Merge the change. */
      *conflict_remains = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Change the property with name PROPNAME in the set of ACTUAL_PROPS
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
 */
static svn_error_t *
apply_single_prop_change(svn_wc_notify_state_t *state,
                         svn_boolean_t *conflict_remains,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_hash_t *actual_props,
                         const char *propname,
                         const svn_string_t *base_val,
                         const svn_string_t *old_val,
                         const svn_string_t *new_val,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_boolean_t merged_prop = FALSE;

  *conflict_remains = FALSE;

  /* Note: The purpose is to apply the change (old_val -> new_val) onto
     (working_val). There is no need for base_val to be involved in the
     process except as a bit of context to help the user understand and
     resolve any conflict. */

  /* Decide how to merge, based on whether we know anything special about
     the property. */
  if (strcmp(propname, SVN_PROP_MERGEINFO) == 0)
    {
      /* We know how to merge any mergeinfo property change...

         ...But Issue #3896 'mergeinfo syntax errors should be treated
         gracefully' might thwart us.  If bogus mergeinfo is present we
         can't merge intelligently, so let the standard method deal with
         it instead. */
      svn_error_t *err = apply_single_mergeinfo_prop_change(state,
                                                            conflict_remains,
                                                            db, local_abspath,
                                                            actual_props,
                                                            propname,
                                                            base_val,
                                                            old_val,
                                                            new_val,
                                                            result_pool,
                                                            scratch_pool);
       if (err)
         {
           if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
             svn_error_clear(err);
           else
             return svn_error_trace(err);
           }
       else
         {
           merged_prop = TRUE;
         }
    }

  if (!merged_prop)
    {
      /* The standard method: perform a simple update automatically, but
         pass any other kind of merge to maybe_generate_propconflict(). */

      SVN_ERR(apply_single_generic_prop_change(state, conflict_remains,
                                               db, local_abspath,
                                               actual_props,
                                               propname, base_val, old_val,
                                               new_val,
                                               result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__merge_props(svn_skel_t **conflict_skel,
                    svn_wc_notify_state_t *state,
                    apr_hash_t **new_pristine_props,
                    apr_hash_t **new_actual_props,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_kind_t kind,
                    apr_hash_t *server_baseprops,
                    apr_hash_t *pristine_props,
                    apr_hash_t *actual_props,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  int i;
  apr_hash_t *conflict_props = NULL;
  apr_hash_t *old_actual_props;
  apr_hash_t *their_props;

  SVN_ERR_ASSERT(pristine_props != NULL);
  SVN_ERR_ASSERT(actual_props != NULL);

  /* Just copy the pointers as we copy the data in the skel if
     necessary */
  old_actual_props = apr_hash_copy(scratch_pool, actual_props);

  *new_pristine_props = NULL;
  *new_actual_props = NULL;

  if (!server_baseprops)
    server_baseprops = apr_hash_copy(scratch_pool, pristine_props);

  their_props = apr_hash_copy(scratch_pool, server_baseprops);

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
      const svn_string_t *base_val; /* Pristine in WC */
      const svn_string_t *from_val; /* Merge left */
      const svn_string_t *to_val; /* Merge right */

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

      apr_hash_set(their_props, propname, APR_HASH_KEY_STRING, to_val);

      /* We already know that state is at least `changed', so mark
         that, but remember that we may later upgrade to `merged' or
         even `conflicted'. */
      set_prop_merge_state(state, svn_wc_notify_state_changed);

      if (! from_val)  /* adding a new property */
        SVN_ERR(apply_single_prop_add(state, &conflict_remains,
                                      db, local_abspath,
                                      actual_props,
                                      propname, base_val, to_val,
                                      result_pool, iterpool));

      else if (! to_val) /* delete an existing property */
        SVN_ERR(apply_single_prop_delete(state, &conflict_remains,
                                         db, local_abspath,
                                         actual_props,
                                         propname, base_val, from_val,
                                         result_pool, iterpool));

      else  /* changing an existing property */
        SVN_ERR(apply_single_prop_change(state, &conflict_remains,
                                         db, local_abspath,
                                         actual_props,
                                         propname, base_val, from_val, to_val,
                                         result_pool, iterpool));


      /* merging logic complete, now we need to possibly log conflict
         data to tmpfiles.  */

      if (conflict_remains)
        {
          set_prop_merge_state(state, svn_wc_notify_state_conflicted);

          if (dry_run)
            continue;   /* skip to next incoming change */

          if (!conflict_props)
            conflict_props = apr_hash_make(result_pool);

          apr_hash_set(conflict_props, apr_pstrdup(result_pool, propname),
                       APR_HASH_KEY_STRING, "");
        }

    }  /* foreach propchange ... */
  svn_pool_destroy(iterpool);

  /* Finished applying all incoming propchanges to our hashes! */

  if (dry_run)
    return SVN_NO_ERROR;

  *new_pristine_props = pristine_props;
  *new_actual_props = actual_props;

  if (conflict_props != NULL)
    {
      /* Ok, we got some conflict. Lets store all the property knowledge we
         have for resolving later */

      if (!*conflict_skel)
        *conflict_skel = svn_wc__conflict_skel_create(result_pool);

      SVN_ERR(svn_wc__conflict_skel_add_prop_conflict(*conflict_skel,
                                                      db, local_abspath,
                                                      NULL /* reject_path */,
                                                      old_actual_props,
                                                      server_baseprops,
                                                      their_props,
                                                      conflict_props,
                                                      result_pool,
                                                      scratch_pool));
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
  return svn_error_trace(svn_wc__db_base_set_dav_cache(db, local_abspath,
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

  return svn_error_trace(svn_wc__db_read_props(props, db, local_abspath,
                                               result_pool, scratch_pool));
}


svn_error_t *
svn_wc_prop_list2(apr_hash_t **props,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__get_actual_props(props,
                                                  wc_ctx->db,
                                                  local_abspath,
                                                  result_pool,
                                                  scratch_pool));
}

struct propname_filter_baton_t {
  svn_wc__proplist_receiver_t receiver_func;
  void *receiver_baton;
  const char *propname;
};

static svn_error_t *
propname_filter_receiver(void *baton,
                         const char *local_abspath,
                         apr_hash_t *props,
                         apr_pool_t *scratch_pool)
{
  struct propname_filter_baton_t *pfb = baton;
  const svn_string_t *propval = apr_hash_get(props, pfb->propname,
                                             APR_HASH_KEY_STRING);

  if (propval)
    {
      props = apr_hash_make(scratch_pool);
      apr_hash_set(props, pfb->propname, APR_HASH_KEY_STRING, propval);

      SVN_ERR(pfb->receiver_func(pfb->receiver_baton, local_abspath, props,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__prop_list_recursive(svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            const char *propname,
                            svn_depth_t depth,
                            svn_boolean_t pristine,
                            const apr_array_header_t *changelists,
                            svn_wc__proplist_receiver_t receiver_func,
                            void *receiver_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  svn_wc__proplist_receiver_t receiver = receiver_func;
  void *baton = receiver_baton;
  struct propname_filter_baton_t pfb;

  pfb.receiver_func = receiver_func;
  pfb.receiver_baton = receiver_baton;
  pfb.propname = propname;

  SVN_ERR_ASSERT(receiver_func);

  if (propname)
    {
      baton = &pfb;
      receiver = propname_filter_receiver;
    }

  switch (depth)
    {
    case svn_depth_empty:
      {
        apr_hash_t *props;
        apr_hash_t *changelist_hash = NULL;

        if (changelists && changelists->nelts)
          SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash,
                                             changelists, scratch_pool));

        if (!svn_wc__internal_changelist_match(wc_ctx->db, local_abspath,
                                               changelist_hash, scratch_pool))
          break;

        if (pristine)
          SVN_ERR(svn_wc__db_read_pristine_props(&props, wc_ctx->db,
                                                 local_abspath,
                                                 scratch_pool, scratch_pool));
        else
          SVN_ERR(svn_wc__db_read_props(&props, wc_ctx->db, local_abspath,
                                        scratch_pool, scratch_pool));

        if (props && apr_hash_count(props) > 0)
          SVN_ERR(receiver(baton, local_abspath, props, scratch_pool));
      }
      break;
    case svn_depth_files:
    case svn_depth_immediates:
    case svn_depth_infinity:
      {
        SVN_ERR(svn_wc__db_read_props_streamily(wc_ctx->db, local_abspath,
                                                depth, pristine,
                                                changelists, receiver, baton,
                                                cancel_func, cancel_baton,
                                                scratch_pool));
      }
      break;
    default:
      SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__prop_retrieve_recursive(apr_hash_t **values,
                                svn_wc_context_t *wc_ctx,
                                const char *local_abspath,
                                const char *propname,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc__db_prop_retrieve_recursive(values,
                                               wc_ctx->db,
                                               local_abspath,
                                               propname,
                                               result_pool, scratch_pool));
}

svn_error_t *
svn_wc_get_pristine_props(apr_hash_t **props,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Certain node stats do not have properties defined on them. Check the
     state, and return NULL for these situations.  */

  err = svn_wc__db_read_pristine_props(props, wc_ctx->db, local_abspath,
                                       result_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
        return svn_error_trace(err);

      svn_error_clear(err);

      /* Documented behavior is to set *PROPS to NULL */
      *props = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_prop_get2(const svn_string_t **value,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (kind == svn_prop_entry_kind)
    {
      /* we don't do entry properties here */
      return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                               _("Property '%s' is an entry property"), name);
    }

  err = svn_wc__internal_propget(value, wc_ctx->db, local_abspath, name,
                                 result_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
        return svn_error_trace(err);

      svn_error_clear(err);
      /* Documented behavior is to set *VALUE to NULL */
      *value = NULL;
    }

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
  enum svn_prop_kind kind = svn_property_kind2(name);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(kind != svn_prop_entry_kind);

  if (kind == svn_prop_wc_kind)
    {
      SVN_ERR_W(svn_wc__db_base_get_dav_cache(&prophash, db, local_abspath,
                                              result_pool, scratch_pool),
                _("Failed to load properties"));
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
   is of kind NODE_KIND.  NAME must be in the "svn:" name space.

   Note that we only disallow the property if we're sure it's one that
   already has a meaning for a different node kind.  We don't disallow
   setting an *unknown* svn: prop here, at this level; a higher level
   should disallow that if desired.
  */
static svn_error_t *
validate_prop_against_node_kind(const char *name,
                                const char *path,
                                svn_node_kind_t node_kind,
                                apr_pool_t *pool)
{
  const char *path_display
    = svn_path_is_url(path) ? path : svn_dirent_local_style(path, pool);

  switch (node_kind)
    {
    case svn_node_dir:
      if (! svn_prop_is_known_svn_dir_prop(name)
          && svn_prop_is_known_svn_file_prop(name))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Cannot set '%s' on a directory ('%s')"),
                                 name, path_display);
      break;
    case svn_node_file:
      if (! svn_prop_is_known_svn_file_prop(name)
          && svn_prop_is_known_svn_dir_prop(name))
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
           svn_node_kind_t kind,
           const char *name,
           const svn_string_t *value,
           svn_boolean_t skip_checks,
           svn_wc_notify_func2_t notify_func,
           void *notify_baton,
           apr_pool_t *scratch_pool)
{
  apr_hash_t *prophash;
  svn_wc_notify_action_t notify_action;
  svn_skel_t *work_item = NULL;
  svn_boolean_t clear_recorded_info = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

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
                                           local_abspath, kind,
                                           skip_checks,
                                           get_file_for_validation, &gb,
                                           scratch_pool));
      value = new_value;
    }

  if (kind == svn_node_file
        && (strcmp(name, SVN_PROP_EXECUTABLE) == 0
            || strcmp(name, SVN_PROP_NEEDS_LOCK) == 0))
    {
      SVN_ERR(svn_wc__wq_build_sync_file_flags(&work_item, db, local_abspath,
                                               scratch_pool, scratch_pool));
    }

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
  if (kind == svn_node_file && strcmp(name, SVN_PROP_KEYWORDS) == 0)
    {
      svn_string_t *old_value = apr_hash_get(prophash, SVN_PROP_KEYWORDS,
                                             APR_HASH_KEY_STRING);
      apr_hash_t *old_keywords, *new_keywords;

      if (old_value)
        SVN_ERR(svn_wc__expand_keywords(&old_keywords,
                                        db, local_abspath, NULL,
                                        old_value->data, TRUE,
                                        scratch_pool, scratch_pool));
      else
        old_keywords = apr_hash_make(scratch_pool);

      if (value)
        SVN_ERR(svn_wc__expand_keywords(&new_keywords,
                                        db, local_abspath, NULL,
                                        value->data, TRUE,
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
          clear_recorded_info = TRUE;
        }
    }
  else if (kind == svn_node_file && strcmp(name, SVN_PROP_EOL_STYLE) == 0)
    {
      svn_string_t *old_value = apr_hash_get(prophash, SVN_PROP_EOL_STYLE,
                                             APR_HASH_KEY_STRING);

      if (((value == NULL) != (old_value == NULL))
          || (value && ! svn_string_compare(value, old_value)))
        {
          clear_recorded_info = TRUE;
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
  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath, prophash,
                                  clear_recorded_info, NULL, work_item,
                                  scratch_pool));

  /* Run our workqueue item for sync'ing flags with props. */
  if (work_item)
    SVN_ERR(svn_wc__wq_run(db, local_abspath, NULL, NULL, scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     notify_action,
                                                     scratch_pool);
      notify->prop_name = name;
      notify->kind = kind;

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

  err = do_propset(wb->db, local_abspath, kind, wb->propname, wb->propval,
                   wb->force, wb->notify_func, wb->notify_baton, scratch_pool);
  if (err && (err->apr_err == SVN_ERR_ILLEGAL_TARGET
              || err->apr_err == SVN_ERR_WC_INVALID_SCHEDULE))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }

  return svn_error_trace(err);
}

svn_error_t *
svn_wc_prop_set4(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 const char *name,
                 const svn_string_t *value,
                 svn_depth_t depth,
                 svn_boolean_t skip_checks,
                 const apr_array_header_t *changelist_filter,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *scratch_pool)
{
  enum svn_prop_kind prop_kind = svn_property_kind2(name);
  svn_wc__db_status_t status;
  svn_kind_t kind;
  const char *dir_abspath;

  /* we don't do entry properties here */
  if (prop_kind == svn_prop_entry_kind)
    return svn_error_createf(SVN_ERR_BAD_PROP_KIND, NULL,
                             _("Property '%s' is an entry property"), name);

  /* Check to see if we're setting the dav cache. */
  if (prop_kind == svn_prop_wc_kind)
    {
      SVN_ERR_ASSERT(depth == svn_depth_empty);
      return svn_error_trace(wcprop_set(wc_ctx->db, local_abspath,
                                        name, value, scratch_pool));
    }

  /* We have to do this little DIR_ABSPATH dance for backwards compat.
     But from 1.7 onwards, all locks are of infinite depth, and from 1.6
     backward we never call this API with depth > empty, so we only need
     to do the write check once per call, here (and not for every node in
     the node walker). */
    /* Get the node status for this path. */
  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  if (status != svn_wc__db_status_normal
      && status != svn_wc__db_status_added
      && status != svn_wc__db_status_incomplete)
    return svn_error_createf(SVN_ERR_WC_INVALID_SCHEDULE, NULL,
                             _("Can't set properties on '%s':"
                               " invalid status for updating properties."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (kind == svn_kind_dir)
    dir_abspath = local_abspath;
  else
    dir_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(svn_wc__write_check(wc_ctx->db, dir_abspath, scratch_pool));

  if (depth == svn_depth_empty || kind != svn_kind_dir)
    {
      apr_hash_t *changelist_hash = NULL;

      if (changelist_filter && changelist_filter->nelts)
        SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelist_filter,
                                           scratch_pool));

      if (!svn_wc__internal_changelist_match(wc_ctx->db, local_abspath,
                                             changelist_hash, scratch_pool))
        return SVN_NO_ERROR;

      SVN_ERR(do_propset(wc_ctx->db, local_abspath,
                         kind == svn_kind_dir
                            ? svn_node_dir
                            : svn_node_file,
                         name, value, skip_checks,
                         notify_func, notify_baton, scratch_pool));
    }
  else
    {
      struct propset_walk_baton wb;

      wb.propname = name;
      wb.propval = value;
      wb.db = wc_ctx->db;
      wb.force = skip_checks;
      wb.notify_func = notify_func;
      wb.notify_baton = notify_baton;

      SVN_ERR(svn_wc__internal_walk_children(wc_ctx->db, local_abspath,
                                             FALSE, changelist_filter,
                                             propset_walk_cb, &wb,
                                             depth,
                                             cancel_func, cancel_baton,
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

  /* This code may place the new prop val in either NEW_VALUE or PROPVAL. */
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
           || strcmp(propname, SVN_PROP_EXTERNALS) == 0
           || strcmp(propname, SVN_PROP_INHERITABLE_IGNORES) == 0
           || strcmp(propname, SVN_PROP_INHERITABLE_AUTO_PROPS) == 0)
    {
      /* Make sure that the last line ends in a newline */
      if (propval->len == 0
          || propval->data[propval->len - 1] != '\n')
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
             whether or not the parsing errored. */
          apr_array_header_t *externals = NULL;
          apr_array_header_t *duplicate_targets = NULL;
          SVN_ERR(svn_wc_parse_externals_description3(&externals, path,
                                                      propval->data, FALSE,
                                                      /*scratch_*/pool));
          SVN_ERR(svn_wc__externals_find_target_dups(&duplicate_targets,
                                                     externals,
                                                     /*scratch_*/pool,
                                                     /*scratch_*/pool));
          if (duplicate_targets && duplicate_targets->nelts > 0)
            {
              const char *more_str = "";
              if (duplicate_targets->nelts > 1)
                {
                  more_str = apr_psprintf(/*scratch_*/pool,
                               _(" (%d more duplicate targets found)"),
                               duplicate_targets->nelts - 1);
                }
              return svn_error_createf(
                SVN_ERR_WC_DUPLICATE_EXTERNALS_TARGET, NULL,
                _("Invalid %s property on '%s': "
                  "target '%s' appears more than once%s"),
                SVN_PROP_EXTERNALS,
                svn_dirent_local_style(path, pool),
                APR_ARRAY_IDX(duplicate_targets, 0, const char*),
                more_str);
            }
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
      propval = &boolean_value;
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
      propval = new_value_str;
    }

  if (new_value)
    *propval_p = svn_stringbuf__morph_into_string(new_value);
  else
    *propval_p = propval;

  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc_is_normal_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
  return (kind == svn_prop_regular_kind);
}


svn_boolean_t
svn_wc_is_wc_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
  return (kind == svn_prop_wc_kind);
}


svn_boolean_t
svn_wc_is_entry_prop(const char *name)
{
  enum svn_prop_kind kind = svn_property_kind2(name);
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
  return svn_error_trace(
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
  return svn_error_trace(svn_wc__internal_propdiff(propchanges,
                                    original_props, wc_ctx->db, local_abspath,
                                    result_pool, scratch_pool));
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

svn_error_t *
svn_wc__get_iprops(apr_array_header_t **inherited_props,
                   svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   const char *propname,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc__db_read_inherited_props(inherited_props,
                                            wc_ctx->db, local_abspath,
                                            propname,
                                            result_pool, scratch_pool));
}

svn_error_t *
svn_wc__get_cached_iprop_children(apr_hash_t **iprop_paths,
                                  svn_depth_t depth,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_get_children_with_cached_iprops(iprop_paths,
                                                     depth,
                                                     local_abspath,
                                                     wc_ctx->db,
                                                     result_pool,
                                                     scratch_pool));
  return SVN_NO_ERROR;
}
