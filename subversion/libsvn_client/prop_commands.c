/*
 * prop_commands.c:  Implementation of propset, propget, and proplist.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

/* ==================================================================== */



/*** Includes. ***/

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_error.h"
#include "svn_client.h"
#include "client.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_hash.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* Check whether NAME is a revision property name.
 *
 * Return TRUE if it is.
 * Return FALSE if it is not.
 */
static svn_boolean_t
is_revision_prop_name(const char *name)
{
  apr_size_t i;
  static const char *revision_props[] =
    {
      SVN_PROP_REVISION_ALL_PROPS
    };

  for (i = 0; i < sizeof(revision_props) / sizeof(revision_props[0]); i++)
    {
      if (strcmp(name, revision_props[i]) == 0)
        return TRUE;
    }
  return FALSE;
}


/* Return an SVN_ERR_CLIENT_PROPERTY_NAME error if NAME is a wcprop,
   else return SVN_NO_ERROR. */
static svn_error_t *
error_if_wcprop_name(const char *name)
{
  if (svn_property_kind(NULL, name) == svn_prop_wc_kind)
    {
      return svn_error_createf
        (SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
         _("'%s' is a wcprop, thus not accessible to clients"),
         name);
    }

  return SVN_NO_ERROR;
}


/* A baton for propset_walk_cb. */
struct propset_walk_baton
{
  const char *propname;  /* The name of the property to set. */
  const svn_string_t *propval;  /* The value to set. */
  svn_wc_adm_access_t *base_access;  /* Access for the tree being walked. */
  svn_boolean_t force;  /* True iff force was passed. */
  apr_hash_t *changelist_hash;  /* Keys are changelists to filter on. */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

/* An entries-walk callback for svn_client_propset3.
 *
 * For the path given by PATH and ENTRY,
 * set the property named wb->PROPNAME to the value wb->PROPVAL,
 * where "wb" is the WALK_BATON of type "struct propset_walk_baton *".
 */
static svn_error_t *
propset_walk_cb(const char *path,
                const svn_wc_entry_t *entry,
                void *walk_baton,
                apr_pool_t *pool)
{
  struct propset_walk_baton *wb = walk_baton;
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access;

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only use
     the second one (where we're looking at THIS_DIR).  */
  if ((entry->kind == svn_node_dir)
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0))
    return SVN_NO_ERROR;

  /* Ignore the entry if it does not exist at the time of interest. */
  if (entry->schedule == svn_wc_schedule_delete)
    return SVN_NO_ERROR;

  /* If our entry doesn't pass changelist filtering, get outta here. */
  if (! SVN_WC__CL_MATCH(wb->changelist_hash, entry))
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, wb->base_access,
                              (entry->kind == svn_node_dir ? path
                               : svn_path_dirname(path, pool)),
                              pool));
  err = svn_wc_prop_set3(wb->propname, wb->propval, path, adm_access,
                         wb->force, wb->notify_func, wb->notify_baton, pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_ILLEGAL_TARGET)
        return err;
      svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}


struct getter_baton
{
  svn_ra_session_t *ra_session;
  svn_revnum_t base_revision_for_url;
};


static svn_error_t *
get_file_for_validation(const svn_string_t **mime_type,
                        svn_stream_t *stream,
                        void *baton,
                        apr_pool_t *pool)
{
  struct getter_baton *gb = baton;
  svn_ra_session_t *ra_session = gb->ra_session;
  apr_hash_t *props;

  SVN_ERR(svn_ra_get_file(ra_session, "", gb->base_revision_for_url,
                          stream, NULL,
                          (mime_type ? &props : NULL),
                          pool));

  if (mime_type)
    *mime_type = apr_hash_get(props, SVN_PROP_MIME_TYPE, APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}


static
svn_error_t *
do_url_propset(const char *propname,
               const svn_string_t *propval,
               const svn_node_kind_t kind,
               const svn_revnum_t base_revision_for_url,
               const svn_delta_editor_t *editor,
               void *edit_baton,
               apr_pool_t *pool)
{
  void *root_baton;

  SVN_ERR(editor->open_root(edit_baton, base_revision_for_url, pool,
                            &root_baton));

  if (kind == svn_node_file)
    {
      void *file_baton;
      SVN_ERR(editor->open_file("", root_baton, base_revision_for_url,
                                pool, &file_baton));
      SVN_ERR(editor->change_file_prop(file_baton, propname, propval, pool));
      SVN_ERR(editor->close_file(file_baton, NULL, pool));
    }
  else
    {
      SVN_ERR(editor->change_dir_prop(root_baton, propname, propval, pool));
    }

  return editor->close_directory(root_baton, pool);
}

static svn_error_t *
propset_on_url(svn_commit_info_t **commit_info_p,
               const char *propname,
               const svn_string_t *propval,
               const char *target,
               svn_boolean_t skip_checks,
               svn_revnum_t base_revision_for_url,
               const apr_hash_t *revprop_table,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  enum svn_prop_kind prop_kind = svn_property_kind(NULL, propname);
  svn_ra_session_t *ra_session;
  svn_node_kind_t node_kind;
  const char *message;
  const svn_delta_editor_t *editor;
  void *commit_baton, *edit_baton;
  apr_hash_t *commit_revprops;
  svn_error_t *err;

  if (prop_kind != svn_prop_regular_kind)
    return svn_error_createf
      (SVN_ERR_BAD_PROP_KIND, NULL,
       _("Property '%s' is not a regular property"), propname);

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, target,
                                               NULL, NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", base_revision_for_url,
                            &node_kind, pool));
  if (node_kind == svn_node_none)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FOUND, NULL,
       _("Path '%s' does not exist in revision %ld"),
       target, base_revision_for_url);

  /* Setting an inappropriate property is not allowed (unless
     overridden by 'skip_checks', in some circumstances).  Deleting an
     inappropriate property is allowed, however, since older clients
     allowed (and other clients possibly still allow) setting it in
     the first place. */
  if (propval && svn_prop_is_svn_prop(propname))
    {
      const svn_string_t *new_value;
      struct getter_baton gb;

      gb.ra_session = ra_session;
      gb.base_revision_for_url = base_revision_for_url;
      SVN_ERR(svn_wc_canonicalize_svn_prop(&new_value, propname, propval,
                                           target, node_kind, skip_checks,
                                           get_file_for_validation, &gb, pool));
      propval = new_value;
    }

  /* Create a new commit item and add it to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items = apr_array_make(pool, 1, sizeof(item));

      item = svn_client_commit_item3_create(pool);
      item->url = target;
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_PROP_MODS;
      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           message, ctx, pool));

  /* Fetch RA commit editor. */
  SVN_ERR(svn_client__commit_get_baton(&commit_baton, commit_info_p, pool));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    svn_client__commit_callback,
                                    commit_baton,
                                    NULL, TRUE, /* No lock tokens */
                                    pool));

  err = do_url_propset(propname, propval, node_kind, base_revision_for_url,
                       editor, edit_baton, pool);

  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      svn_error_clear(editor->abort_edit(edit_baton, pool));
      return err;
    }

  /* Close the edit. */
  return editor->close_edit(edit_baton, pool);
}


svn_error_t *
svn_client_propset3(svn_commit_info_t **commit_info_p,
                    const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_depth_t depth,
                    svn_boolean_t skip_checks,
                    svn_revnum_t base_revision_for_url,
                    const apr_array_header_t *changelists,
                    const apr_hash_t *revprop_table,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  /* Since Subversion controls the "svn:" property namespace, we
     don't honor the 'skip_checks' flag here.  Unusual property
     combinations, like svn:eol-style with a non-text svn:mime-type,
     are understandable, but revprops on local targets are not. */

  if (is_revision_prop_name(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Revision property '%s' not allowed "
                               "in this context"), propname);

  SVN_ERR(error_if_wcprop_name(propname));

  if (propval && ! svn_prop_name_is_valid(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Bad property name: '%s'"), propname);

  if (svn_path_is_url(target))
    {
      /* The rationale for requiring the base_revision_for_url
         argument is that without it, it's too easy to possibly
         overwrite someone else's change without noticing.  (See also
         tools/examples/svnput.c). */
      if (! SVN_IS_VALID_REVNUM(base_revision_for_url))
        return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                 _("Setting property on non-local target '%s' "
                                   "needs a base revision"), target);

      if (depth > svn_depth_empty)
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Setting property recursively on non-local "
                                   "target '%s' is not supported"), target);

      /* ### When you set svn:eol-style or svn:keywords on a wc file,
         ### Subversion sends a textdelta at commit time to properly
         ### normalize the file in the repository.  If we want to
         ### support editing these properties on URLs, then we should
         ### generate the same textdelta; for now, we won't support
         ### editing these properties on URLs.  (Admittedly, this
         ### means that all the machinery with get_file_for_validation
         ### is unused.)
       */
      if ((strcmp(propname, SVN_PROP_EOL_STYLE) == 0) ||
          (strcmp(propname, SVN_PROP_KEYWORDS) == 0))
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Setting property '%s' on non-local target "
                                   "'%s' is not supported"), propname, target);

      return propset_on_url(commit_info_p, propname, propval, target,
                            skip_checks, base_revision_for_url, revprop_table,
                            ctx, pool);
    }
  else
    {
      svn_wc_adm_access_t *adm_access;
      int adm_lock_level = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);
      const svn_wc_entry_t *entry;
      apr_hash_t *changelist_hash = NULL;

      if (changelists && changelists->nelts)
        SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash,
                                           changelists, pool));

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target, TRUE,
                                     adm_lock_level, ctx->cancel_func,
                                     ctx->cancel_baton, pool));
      SVN_ERR(svn_wc__entry_versioned(&entry, target, adm_access,
                                      FALSE, pool));

      if (depth >= svn_depth_files && entry->kind == svn_node_dir)
        {
          static const svn_wc_entry_callbacks2_t walk_callbacks
            = { propset_walk_cb, svn_client__default_walker_error_handler };
          struct propset_walk_baton wb;

          wb.base_access = adm_access;
          wb.propname = propname;
          wb.propval = propval;
          wb.force = skip_checks;
          wb.changelist_hash = changelist_hash;
          wb.notify_func = ctx->notify_func2;
          wb.notify_baton = ctx->notify_baton2;
          SVN_ERR(svn_wc_walk_entries3(target, adm_access, &walk_callbacks,
                                       &wb, depth, FALSE, ctx->cancel_func,
                                       ctx->cancel_baton, pool));
        }
      else if (SVN_WC__CL_MATCH(changelist_hash, entry))
        {
          SVN_ERR(svn_wc_prop_set3(propname, propval, target,
                                   adm_access, skip_checks, ctx->notify_func2,
                                   ctx->notify_baton2, pool));
        }
      return svn_wc_adm_close2(adm_access, pool);
    }
}

svn_error_t *
svn_client_revprop_set2(const char *propname,
                        const svn_string_t *propval,
                        const svn_string_t *original_propval,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_revnum_t *set_rev,
                        svn_boolean_t force,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;

  if ((strcmp(propname, SVN_PROP_REVISION_AUTHOR) == 0)
      && propval
      && strchr(propval->data, '\n') != NULL
      && (! force))
    return svn_error_create(SVN_ERR_CLIENT_REVISION_AUTHOR_CONTAINS_NEWLINE,
                            NULL, _("Value will not be set unless forced"));

  if (propval && ! svn_prop_name_is_valid(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Bad property name: '%s'"), propname);

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, URL, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR(svn_client__get_revision_number
          (set_rev, NULL, ra_session, revision, NULL, pool));

  if (original_propval)
    {
      /* Ensure old value hasn't changed behind our back. */
      svn_string_t *current;
      SVN_ERR(svn_ra_rev_prop(ra_session, *set_rev, propname, &current, pool));

      if (original_propval->data && (! current))
        {
          return svn_error_createf(
                  SVN_ERR_RA_OUT_OF_DATE, NULL,
                  _("revprop '%s' in r%ld is unexpectedly absent "
                    "in repository (maybe someone else deleted it?)"),
                  propname, *set_rev);
        }
      else if (original_propval->data
               && (! svn_string_compare(original_propval, current)))
        {
          return svn_error_createf(
                  SVN_ERR_RA_OUT_OF_DATE, NULL,
                  _("revprop '%s' in r%ld has unexpected value "
                    "in repository (maybe someone else changed it?)"),
                  propname, *set_rev);
        }
      else if ((! original_propval->data) && current)
        {
          return svn_error_createf(
                  SVN_ERR_RA_OUT_OF_DATE, NULL,
                  _("revprop '%s' in r%ld is unexpectedly present "
                    "in repository (maybe someone else set it?)"),
                  propname, *set_rev);
        }
    }

  /* The actual RA call. */
  SVN_ERR(svn_ra_change_rev_prop(ra_session, *set_rev, propname, propval,
                                 pool));

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify_url(URL,
                                             propval == NULL
                                               ? svn_wc_notify_revprop_deleted
                                               : svn_wc_notify_revprop_set,
                                             pool);
      notify->prop_name = propname;
      notify->revision = *set_rev;

      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* Set *PROPS to the pristine (base) properties at PATH, if PRISTINE
 * is true, or else the working value if PRISTINE is false.
 *
 * The keys of *PROPS will be 'const char *' property names, and the
 * values 'const svn_string_t *' property values.  Allocate *PROPS
 * and its contents in POOL.
 */
static svn_error_t *
pristine_or_working_props(apr_hash_t **props,
                          const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t pristine,
                          apr_pool_t *pool)
{
  if (pristine)
    return svn_wc_get_prop_diffs(NULL, props, path, adm_access, pool);
  else
    return svn_wc_prop_list(props, path, adm_access, pool);
}


/* Set *PROPVAL to the pristine (base) value of property PROPNAME at
 * PATH, if PRISTINE is true, or else the working value if PRISTINE is
 * false.  Allocate *PROPVAL in POOL.
 */
static svn_error_t *
pristine_or_working_propval(const svn_string_t **propval,
                            const char *propname,
                            const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_boolean_t pristine,
                            apr_pool_t *pool)
{
  if (pristine)
    {
      apr_hash_t *pristine_props;

      SVN_ERR(svn_wc_get_prop_diffs(NULL, &pristine_props, path, adm_access,
                                    pool));
      *propval = apr_hash_get(pristine_props, propname, APR_HASH_KEY_STRING);
    }
  else  /* get the working revision */
    {
      SVN_ERR(svn_wc_prop_get(propval, propname, path, adm_access, pool));
    }

  return SVN_NO_ERROR;
}


/* A baton for propget_walk_cb. */
struct propget_walk_baton
{
  const char *propname;  /* The name of the property to get. */
  svn_boolean_t pristine;  /* Select base rather than working props. */
  svn_wc_adm_access_t *base_access;  /* Access for the tree being walked. */
  apr_hash_t *changelist_hash;  /* Keys are changelists to filter on. */
  apr_hash_t *props;  /* Out: mapping of (path:propval). */
};

/* An entries-walk callback for svn_client_propget.
 *
 * For the path given by PATH and ENTRY,
 * populate wb->PROPS with the values of property wb->PROPNAME,
 * where "wb" is the WALK_BATON of type "struct propget_walk_baton *".
 * If wb->PRISTINE is true, use the base value, else use the working value.
 *
 * The keys of wb->PROPS will be 'const char *' paths, rooted at the
 * path svn_wc_adm_access_path(ADM_ACCESS), and the values are
 * 'const svn_string_t *' property values.
 */
static svn_error_t *
propget_walk_cb(const char *path,
                const svn_wc_entry_t *entry,
                void *walk_baton,
                apr_pool_t *pool)
{
  struct propget_walk_baton *wb = walk_baton;
  const svn_string_t *propval;

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only use
     the second one (where we're looking at THIS_DIR).  */
  if ((entry->kind == svn_node_dir)
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0))
    return SVN_NO_ERROR;

  /* Ignore the entry if it does not exist at the time of interest. */
  if (entry->schedule
      == (wb->pristine ? svn_wc_schedule_add : svn_wc_schedule_delete))
    return SVN_NO_ERROR;

  /* If our entry doesn't pass changelist filtering, get outta here. */
  if (! SVN_WC__CL_MATCH(wb->changelist_hash, entry))
    return SVN_NO_ERROR;

  SVN_ERR(pristine_or_working_propval(&propval, wb->propname, path,
                                      wb->base_access, wb->pristine,
                                      pool));

  if (propval)
    {
      path = apr_pstrdup(apr_hash_pool_get(wb->props), path);
      propval = svn_string_dup(propval, apr_hash_pool_get(wb->props));
      apr_hash_set(wb->props, path, APR_HASH_KEY_STRING, propval);
    }

  return SVN_NO_ERROR;
}


/* Helper for the remote case of svn_client_propget.
 *
 * Get the value of property PROPNAME in REVNUM, using RA_LIB and
 * SESSION.  Store the value ('svn_string_t *') in PROPS, under the
 * path key "TARGET_PREFIX/TARGET_RELATIVE" ('const char *').
 *
 * Recurse according to DEPTH, similarly to svn_client_propget3().
 *
 * KIND is the kind of the node at "TARGET_PREFIX/TARGET_RELATIVE".
 * Yes, caller passes this; it makes the recursion more efficient :-).
 *
 * Allocate the keys and values in PERM_POOL, but do all temporary
 * work in WORK_POOL.  The two pools can be the same; recursive
 * calls may use a different WORK_POOL, however.
 */
static svn_error_t *
remote_propget(apr_hash_t *props,
               const char *propname,
               const char *target_prefix,
               const char *target_relative,
               svn_node_kind_t kind,
               svn_revnum_t revnum,
               svn_ra_session_t *ra_session,
               svn_depth_t depth,
               apr_pool_t *perm_pool,
               apr_pool_t *work_pool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash;

  if (kind == svn_node_dir)
    {
      SVN_ERR(svn_ra_get_dir2(ra_session,
                              (depth >= svn_depth_files ? &dirents : NULL),
                              NULL, &prop_hash, target_relative, revnum,
                              SVN_DIRENT_KIND, work_pool));
    }
  else if (kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(ra_session, target_relative, revnum,
                              NULL, NULL, &prop_hash, work_pool));
    }
  else if (kind == svn_node_none)
    {
      return svn_error_createf
        (SVN_ERR_ENTRY_NOT_FOUND, NULL,
         _("'%s' does not exist in revision %ld"),
         svn_path_join(target_prefix, target_relative, work_pool), revnum);
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         _("Unknown node kind for '%s'"),
         svn_path_join(target_prefix, target_relative, work_pool));
    }

  {
    svn_string_t *val = apr_hash_get(prop_hash, propname,
                                     APR_HASH_KEY_STRING);
    if (val)
      {
        apr_hash_set(props,
                     svn_path_join(target_prefix, target_relative, perm_pool),
                     APR_HASH_KEY_STRING, svn_string_dup(val, perm_pool));
      }
  }

  if (depth >= svn_depth_files
      && kind == svn_node_dir
      && apr_hash_count(dirents) > 0)
    {
      apr_hash_index_t *hi;
      apr_pool_t *iterpool = svn_pool_create(work_pool);

      for (hi = apr_hash_first(work_pool, dirents);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *this_name;
          svn_dirent_t *this_ent;
          const char *new_target_relative;
          svn_depth_t depth_below_here = depth;

          svn_pool_clear(iterpool);

          apr_hash_this(hi, &key, NULL, &val);
          this_name = key;
          this_ent = val;

          if (depth == svn_depth_files && this_ent->kind == svn_node_dir)
            continue;

          if (depth == svn_depth_files || depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          new_target_relative = svn_path_join(target_relative,
                                              this_name, iterpool);

          SVN_ERR(remote_propget(props,
                                 propname,
                                 target_prefix,
                                 new_target_relative,
                                 this_ent->kind,
                                 revnum,
                                 ra_session,
                                 depth_below_here,
                                 perm_pool, iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Squelch ERR by returning SVN_NO_ERROR if ERR is caused by a missing
   path (i.e. SVN_ERR_WC_PATH_NOT_FOUND). */
static svn_error_t *
wc_walker_error_handler(const char *path,
                        svn_error_t *err,
                        void *walk_baton,
                        apr_pool_t *pool)
{
  svn_error_t *root_err = svn_error_root_cause(err);
  if (root_err == SVN_NO_ERROR)
    return err;

  /* Suppress errors from missing paths. */
  if (root_err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    {
      return err;
    }
}

svn_error_t *
svn_client__get_prop_from_wc(apr_hash_t *props,
                             const char *propname,
                             const char *target,
                             svn_boolean_t pristine,
                             const svn_wc_entry_t *entry,
                             svn_wc_adm_access_t *adm_access,
                             svn_depth_t depth,
                             const apr_array_header_t *changelists,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  apr_hash_t *changelist_hash = NULL;
  static const svn_wc_entry_callbacks2_t walk_callbacks =
    { propget_walk_cb, wc_walker_error_handler };
  struct propget_walk_baton wb;

  if (changelists && changelists->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelists, pool));

  /* Technically, svn_depth_unknown just means use whatever depth(s)
     we find in the working copy.  But this is a walk over extant
     working copy paths: if they're there at all, then by definition
     the local depth reaches them, so let's just use svn_depth_infinity
     to get there. */
  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  wb.propname = propname;
  wb.pristine = pristine;
  wb.base_access = adm_access;
  wb.changelist_hash = changelist_hash;
  wb.props = props;

  /* Fetch the property, recursively or for a single resource. */
  if (depth >= svn_depth_files && entry->kind == svn_node_dir)
    {
      SVN_ERR(svn_wc_walk_entries3(target, adm_access,
                                   &walk_callbacks, &wb, depth, FALSE,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   pool));
    }
  else if (SVN_WC__CL_MATCH(changelist_hash, entry))
    {
      SVN_ERR(propget_walk_cb(target, entry, &wb, pool));
    }

  return SVN_NO_ERROR;
}

/* Note: this implementation is very similar to svn_client_proplist. */
svn_error_t *
svn_client_propget3(apr_hash_t **props,
                    const char *propname,
                    const char *path_or_url,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_revnum_t *actual_revnum,
                    svn_depth_t depth,
                    const apr_array_header_t *changelists,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_revnum_t revnum;

  SVN_ERR(error_if_wcprop_name(propname));

  peg_revision = svn_cl__rev_default_to_head_or_working(peg_revision,
                                                        path_or_url);
  revision = svn_cl__rev_default_to_peg(revision, peg_revision);

  *props = apr_hash_make(pool);

  if (! svn_path_is_url(path_or_url)
      && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(peg_revision->kind)
      && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind))
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *node;
      svn_boolean_t pristine;
      int adm_lock_level = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url,
                                     FALSE, adm_lock_level,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     pool));
      SVN_ERR(svn_wc__entry_versioned(&node, path_or_url, adm_access,
                                      FALSE, pool));

      SVN_ERR(svn_client__get_revision_number
              (&revnum, NULL, NULL, revision, path_or_url, pool));

      /* If FALSE, we must want the working revision. */
      pristine = (revision->kind == svn_opt_revision_committed
                  || revision->kind == svn_opt_revision_base);

      SVN_ERR(svn_client__get_prop_from_wc(*props, propname, path_or_url,
                                           pristine, node, adm_access,
                                           depth, changelists, ctx, pool));

      SVN_ERR(svn_wc_adm_close2(adm_access, pool));
    }
  else
    {
      const char *url;
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;

      /* Get an RA plugin for this filesystem object. */
      SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                               &url, path_or_url, NULL,
                                               peg_revision,
                                               revision, ctx, pool));

      SVN_ERR(svn_ra_check_path(ra_session, "", revnum, &kind, pool));

      SVN_ERR(remote_propget(*props, propname, url, "",
                             kind, revnum, ra_session,
                             depth, pool, pool));
    }

  if (actual_revnum)
    *actual_revnum = revnum;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_revprop_get(const char *propname,
                       svn_string_t **propval,
                       const char *URL,
                       const svn_opt_revision_t *revision,
                       svn_revnum_t *set_rev,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, URL, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR(svn_client__get_revision_number
          (set_rev, NULL, ra_session, revision, NULL, pool));

  /* The actual RA call. */
  return svn_ra_rev_prop(ra_session, *set_rev, propname, propval, pool);
}


/* Call RECEIVER for the given PATH and PROP_HASH.
 *
 * If PROP_HASH is null or has zero count, do nothing.
 */
static svn_error_t*
call_receiver(const char *path,
              apr_hash_t *prop_hash,
              svn_proplist_receiver_t receiver,
              void *receiver_baton,
              apr_pool_t *pool)
{
  if (prop_hash && apr_hash_count(prop_hash))
    SVN_ERR(receiver(receiver_baton, path, prop_hash, pool));

  return SVN_NO_ERROR;
}


/* Helper for the remote case of svn_client_proplist.
 *
 * Push a new 'svn_client_proplist_item_t *' item onto PROPLIST,
 * containing the properties for "TARGET_PREFIX/TARGET_RELATIVE" in
 * REVNUM, obtained using RA_LIB and SESSION.  The item->node_name
 * will be "TARGET_PREFIX/TARGET_RELATIVE", and the value will be a
 * hash mapping 'const char *' property names onto 'svn_string_t *'
 * property values.
 *
 * Allocate the new item and its contents in POOL.
 * Do all looping, recursion, and temporary work in SCRATCHPOOL.
 *
 * KIND is the kind of the node at "TARGET_PREFIX/TARGET_RELATIVE".
 *
 * If the target is a directory, only fetch properties for the files
 * and directories at depth DEPTH.
 */
static svn_error_t *
remote_proplist(const char *target_prefix,
                const char *target_relative,
                svn_node_kind_t kind,
                svn_revnum_t revnum,
                svn_ra_session_t *ra_session,
                svn_depth_t depth,
                svn_proplist_receiver_t receiver,
                void *receiver_baton,
                apr_pool_t *pool,
                apr_pool_t *scratchpool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash, *final_hash;
  apr_hash_index_t *hi;

  if (kind == svn_node_dir)
    {
      SVN_ERR(svn_ra_get_dir2(ra_session,
                              (depth > svn_depth_empty) ? &dirents : NULL,
                              NULL, &prop_hash, target_relative, revnum,
                              SVN_DIRENT_KIND, scratchpool));
    }
  else if (kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(ra_session, target_relative, revnum,
                              NULL, NULL, &prop_hash, scratchpool));
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         _("Unknown node kind for '%s'"),
         svn_path_join(target_prefix, target_relative, pool));
    }

  /* Filter out non-regular properties, since the RA layer returns all
     kinds.  Copy regular properties keys/vals from the prop_hash
     allocated in SCRATCHPOOL to the "final" hash allocated in POOL. */
  final_hash = apr_hash_make(pool);
  for (hi = apr_hash_first(scratchpool, prop_hash);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_prop_kind_t prop_kind;
      const char *name;
      svn_string_t *value;

      apr_hash_this(hi, &key, &klen, &val);
      prop_kind = svn_property_kind(NULL, (const char *) key);

      if (prop_kind == svn_prop_regular_kind)
        {
          name = apr_pstrdup(pool, (const char *) key);
          value = svn_string_dup((svn_string_t *) val, pool);
          apr_hash_set(final_hash, name, klen, value);
        }
    }

  call_receiver(svn_path_join(target_prefix, target_relative,
                              scratchpool),
                final_hash, receiver, receiver_baton,
                pool);

  if (depth > svn_depth_empty
      && (kind == svn_node_dir) && (apr_hash_count(dirents) > 0))
    {
      apr_pool_t *subpool = svn_pool_create(scratchpool);

      for (hi = apr_hash_first(scratchpool, dirents);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *this_name;
          svn_dirent_t *this_ent;
          const char *new_target_relative;

          svn_pool_clear(subpool);

          apr_hash_this(hi, &key, NULL, &val);
          this_name = key;
          this_ent = val;

          new_target_relative = svn_path_join(target_relative,
                                              this_name, subpool);

          if (this_ent->kind == svn_node_file
              || depth > svn_depth_files)
            {
              svn_depth_t depth_below_here = depth;

              if (depth == svn_depth_immediates)
                depth_below_here = svn_depth_empty;

              SVN_ERR(remote_proplist(target_prefix,
                                      new_target_relative,
                                      this_ent->kind,
                                      revnum,
                                      ra_session,
                                      depth_below_here,
                                      receiver,
                                      receiver_baton,
                                      pool,
                                      subpool));
            }
        }

      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}


/* A baton for proplist_walk_cb. */
struct proplist_walk_baton
{
  svn_boolean_t pristine;  /* Select base rather than working props. */
  svn_wc_adm_access_t *base_access;  /* Access for the tree being walked. */
  apr_hash_t *changelist_hash; /* Keys are changelists to filter on. */
  svn_proplist_receiver_t receiver;  /* Proplist receiver to call. */
  void *receiver_baton;    /* Baton for the proplist receiver. */
};

/* An entries-walk callback for svn_client_proplist.
 *
 * For the path given by PATH and ENTRY,
 * populate wb->PROPS with a svn_client_proplist_item_t for each path,
 * where "wb" is the WALK_BATON of type "struct proplist_walk_baton *".
 * If wb->PRISTINE is true, use the base values, else use the working values.
 */
static svn_error_t *
proplist_walk_cb(const char *path,
                 const svn_wc_entry_t *entry,
                 void *walk_baton,
                 apr_pool_t *pool)
{
  struct proplist_walk_baton *wb = walk_baton;
  apr_hash_t *hash;

  /* We're going to receive dirents twice;  we want to ignore the
     first one (where it's a child of a parent dir), and only use
     the second one (where we're looking at THIS_DIR).  */
  if ((entry->kind == svn_node_dir)
      && (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) != 0))
    return SVN_NO_ERROR;

  /* Ignore the entry if it does not exist at the time of interest. */
  if (entry->schedule
      == (wb->pristine ? svn_wc_schedule_add : svn_wc_schedule_delete))
    return SVN_NO_ERROR;

  /* If our entry doesn't pass changelist filtering, get outta here. */
  if (! SVN_WC__CL_MATCH(wb->changelist_hash, entry))
    return SVN_NO_ERROR;

  path = apr_pstrdup(pool, path);

  SVN_ERR(pristine_or_working_props(&hash, path, wb->base_access,
                                    wb->pristine, pool));
  return call_receiver(path, hash, wb->receiver, wb->receiver_baton, pool);
}


/* Note: this implementation is very similar to svn_client_propget3(). */
svn_error_t *
svn_client_proplist3(const char *path_or_url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     const apr_array_header_t *changelists,
                     svn_proplist_receiver_t receiver,
                     void *receiver_baton,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const char *url;

  peg_revision = svn_cl__rev_default_to_head_or_working(peg_revision,
                                                        path_or_url);
  revision = svn_cl__rev_default_to_peg(revision, peg_revision);

  if (depth == svn_depth_unknown)
    depth = svn_depth_empty;

  if (! svn_path_is_url(path_or_url)
      && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(peg_revision->kind)
      && SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind))
    {
      svn_boolean_t pristine;
      int levels_to_lock = SVN_WC__LEVELS_TO_LOCK_FROM_DEPTH(depth);
      const svn_wc_entry_t *entry;
      apr_hash_t *changelist_hash = NULL;

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url,
                                     FALSE, levels_to_lock,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     pool));
      SVN_ERR(svn_wc__entry_versioned(&entry, path_or_url, adm_access,
                                      FALSE, pool));

      if ((revision->kind == svn_opt_revision_committed)
          || (revision->kind == svn_opt_revision_base))
        {
          pristine = TRUE;
        }
      else  /* must be the working revision */
        {
          pristine = FALSE;
        }

      if (changelists && changelists->nelts)
        SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash,
                                           changelists, pool));

      /* Fetch, recursively or not. */
      if (depth >= svn_depth_files && (entry->kind == svn_node_dir))
        {
          static const svn_wc_entry_callbacks2_t walk_callbacks
            = { proplist_walk_cb, svn_client__default_walker_error_handler };
          struct proplist_walk_baton wb;

          wb.base_access = adm_access;
          wb.pristine = pristine;
          wb.changelist_hash = changelist_hash;
          wb.receiver = receiver;
          wb.receiver_baton = receiver_baton;

          SVN_ERR(svn_wc_walk_entries3(path_or_url, adm_access,
                                       &walk_callbacks, &wb, depth, FALSE,
                                       ctx->cancel_func, ctx->cancel_baton,
                                       pool));
        }
      else if (SVN_WC__CL_MATCH(changelist_hash, entry))
        {
          apr_hash_t *hash;

          SVN_ERR(pristine_or_working_props(&hash, path_or_url, adm_access,
                                            pristine, pool));
          SVN_ERR(call_receiver(path_or_url, hash,
                                receiver, receiver_baton, pool));

        }

      SVN_ERR(svn_wc_adm_close2(adm_access, pool));
    }
  else /* remote target */
    {
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;
      apr_pool_t *subpool = svn_pool_create(pool);
      svn_revnum_t revnum;

      /* Get an RA session for this URL. */
      SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                               &url, path_or_url, NULL,
                                               peg_revision,
                                               revision, ctx, pool));

      SVN_ERR(svn_ra_check_path(ra_session, "", revnum, &kind, pool));

      SVN_ERR(remote_proplist(url, "", kind, revnum, ra_session, depth,
                              receiver, receiver_baton, pool, subpool));
      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_revprop_list(apr_hash_t **props,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_revnum_t *set_rev,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  apr_hash_t *proplist;

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, URL, NULL,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR(svn_client__get_revision_number
          (set_rev, NULL, ra_session, revision, NULL, pool));

  /* The actual RA call. */
  SVN_ERR(svn_ra_rev_proplist(ra_session, *set_rev, &proplist, pool));

  *props = proplist;
  return SVN_NO_ERROR;
}
