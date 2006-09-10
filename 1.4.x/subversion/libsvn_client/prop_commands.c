/*
 * prop_commands.c:  Implementation of propset, propget, and proplist.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#include "svn_client.h"
#include "client.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_ctype.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Check whether the UTF8 NAME is a valid property name.  For now, this means
 * the ASCII subset of an XML "Name".
 * XML "Name" is defined at http://www.w3.org/TR/REC-xml#sec-common-syn */
static svn_boolean_t
is_valid_prop_name(const char *name)
{
  const char *p = name;

  /* The characters we allow use identical representations in UTF8
     and ASCII, so we can just test for the appropriate ASCII codes.
     But we can't use standard C character notation ('A', 'B', etc)
     because there's no guarantee that this C environment is using
     ASCII. */

  if (!(svn_ctype_isalpha(*p)
        || *p == SVN_CTYPE_ASCII_COLON
        || *p == SVN_CTYPE_ASCII_UNDERSCORE))
    return FALSE;
  p++;
  for (; *p; p++)
    {
      if (!(svn_ctype_isalnum(*p)
            || *p == SVN_CTYPE_ASCII_MINUS
            || *p == SVN_CTYPE_ASCII_DOT
            || *p == SVN_CTYPE_ASCII_COLON
            || *p == SVN_CTYPE_ASCII_UNDERSCORE))
        return FALSE;
    }
  return TRUE;
}


/* Check whether NAME is a revision property name.
 * 
 * Return TRUE if it is.
 * Return FALSE if it is not.  
 */ 
static svn_boolean_t
is_revision_prop_name(const char *name)
{
  apr_size_t i;
  const char *revision_props[] = 
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
};

/* An entries-walk callback for svn_client_propset2.
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

  SVN_ERR(svn_wc_adm_retrieve(&adm_access, wb->base_access,
                              (entry->kind == svn_node_dir ? path
                               : svn_path_dirname(path, pool)),
                              pool));
  err = svn_wc_prop_set2(wb->propname, wb->propval,
                         path, adm_access, wb->force, pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_ILLEGAL_TARGET)
        return err;
      svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_propset2(const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_boolean_t recurse,
                    svn_boolean_t skip_checks,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *node;

  /* Since Subversion controls the "svn:" property namespace, we
     don't honor the 'skip_checks' flag here.  Unusual property
     combinations, like svn:eol-style with a non-text svn:mime-type,
     are understandable, but revprops on local targets are not. */
  if (is_revision_prop_name(propname))
    {
      return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                               _("Revision property '%s' not allowed "
                                 "in this context"), propname);
    }

  SVN_ERR(error_if_wcprop_name(propname));

  if (svn_path_is_url(target))
    {
      /* The rationale for not supporting this is that it makes it too
         easy to possibly overwrite someone else's change without noticing.
         (See also tools/examples/svnput.c).

         Besides, we don't have a client context for auth or log getting in
         this function anyway. */
      return svn_error_createf
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Setting property on non-local target '%s' is not supported"),
         target);
    }

  if (propval && ! is_valid_prop_name(propname))
    return svn_error_createf(SVN_ERR_CLIENT_PROPERTY_NAME, NULL,
                             _("Bad property name: '%s'"), propname);

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target, TRUE,
                                 recurse ? -1 : 0, ctx->cancel_func,
                                 ctx->cancel_baton, pool));
  SVN_ERR(svn_wc_entry(&node, target, adm_access, FALSE, pool));
  if (!node)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"), 
                             svn_path_local_style(target, pool));

  if (recurse && node->kind == svn_node_dir)
    {
      static const svn_wc_entry_callbacks_t walk_callbacks
        = { propset_walk_cb };
      struct propset_walk_baton wb;

      wb.base_access = adm_access;
      wb.propname = propname;
      wb.propval = propval;
      wb.force = skip_checks;

      SVN_ERR(svn_wc_walk_entries2(target, adm_access,
                                   &walk_callbacks, &wb, FALSE,
                                   ctx->cancel_func, ctx->cancel_baton,
                                   pool));
    }
  else
    {
      SVN_ERR(svn_wc_prop_set2(propname, propval, target,
                               adm_access, skip_checks, pool));
    }

  SVN_ERR(svn_wc_adm_close(adm_access));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_propset(const char *propname,
                   const svn_string_t *propval,
                   const char *target,
                   svn_boolean_t recurse,
                   apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;

  SVN_ERR(svn_client_create_context(&ctx, pool));

  return svn_client_propset2(propname, propval, target, recurse, FALSE,
                             ctx, pool);
}


svn_error_t *
svn_client_revprop_set(const char *propname,
                       const svn_string_t *propval,
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

  if (propval && ! is_valid_prop_name(propname))
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
          (set_rev, ra_session, revision, NULL, pool));

  /* The actual RA call. */
  SVN_ERR(svn_ra_change_rev_prop(ra_session, *set_rev, propname, propval,
                                 pool));

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
    SVN_ERR(svn_wc_get_prop_diffs(NULL, props, path, adm_access, pool));
  else
    SVN_ERR(svn_wc_prop_list(props, path, adm_access, pool));
  
  return SVN_NO_ERROR;
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

  SVN_ERR(pristine_or_working_propval(&propval, wb->propname, path,
                                      wb->base_access, wb->pristine,
                                      apr_hash_pool_get(wb->props)));

  if (propval)
    {
      path = apr_pstrdup(apr_hash_pool_get(wb->props), path);
      apr_hash_set(wb->props, path, APR_HASH_KEY_STRING, propval);
    }

  return SVN_NO_ERROR;
}


/* If REVISION represents a revision not present in the working copy,
 * then set *NEW_TARGET to the url for TARGET, allocated in POOL; else
 * set *NEW_TARGET to TARGET (just assign, do not copy), whether or
 * not TARGET is a url.
 *
 * TARGET and *NEW_TARGET may be the same, though most callers
 * probably don't want them to be.
 */
static svn_error_t *
maybe_convert_to_url(const char **new_target,
                     const char *target,
                     const svn_opt_revision_t *revision,
                     apr_pool_t *pool)
{
  /* If we don't already have a url, and the revision kind is such
     that we need a url, then get one. */
  if ((revision->kind != svn_opt_revision_unspecified)
      && (revision->kind != svn_opt_revision_base)
      && (revision->kind != svn_opt_revision_working)
      && (revision->kind != svn_opt_revision_committed)
      && (! svn_path_is_url(target)))
    {
      svn_wc_adm_access_t *adm_access;
      svn_node_kind_t kind;
      const char *pdir;
      const svn_wc_entry_t *entry;
      
      SVN_ERR(svn_io_check_path(target, &kind, pool));
      if (kind == svn_node_file)
        svn_path_split(target, &pdir, NULL, pool);
      else
        pdir = target;
      
      SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, pdir, FALSE,
                               0, NULL, NULL, pool));
      SVN_ERR(svn_wc_entry(&entry, target, adm_access, FALSE, pool));
      if (! entry)
        return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                 _("'%s' is not under version control"), 
                                 svn_path_local_style(target, pool));
      *new_target = entry->url;
    }
  else
    *new_target = target;

  return SVN_NO_ERROR;
}


/* Helper for the remote case of svn_client_propget.
 *
 * Get the value of property PROPNAME in REVNUM, using RA_LIB and
 * SESSION.  Store the value ('svn_string_t *') in PROPS, under the
 * path key "TARGET_PREFIX/TARGET_RELATIVE" ('const char *').
 *
 * If RECURSE is true and KIND is svn_node_dir, then recurse.
 *
 * KIND is the kind of the node at "TARGET_PREFIX/TARGET_RELATIVE".
 * Yes, caller passes this; it makes the recursion more efficient :-). 
 *
 * Allocate the keys and values in POOL.
 */
static svn_error_t *
remote_propget(apr_hash_t *props,
               const char *propname,
               const char *target_prefix,
               const char *target_relative,
               svn_node_kind_t kind,
               svn_revnum_t revnum,
               svn_ra_session_t *ra_session,
               svn_boolean_t recurse,
               apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash;
  
  if (kind == svn_node_dir)
    {
      SVN_ERR(svn_ra_get_dir2(ra_session, (recurse ? &dirents : NULL), NULL,
                              &prop_hash, target_relative, revnum,
                              SVN_DIRENT_KIND, pool));
    }
  else if (kind == svn_node_file)
    {
      SVN_ERR(svn_ra_get_file(ra_session, target_relative, revnum,
                              NULL, NULL, &prop_hash, pool));
    }
  else if (kind == svn_node_none)
    {
      return svn_error_createf
        (SVN_ERR_ENTRY_NOT_FOUND, NULL,
         _("'%s' does not exist in revision '%ld'"),
         svn_path_join(target_prefix, target_relative, pool), revnum);
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         _("Unknown node kind for '%s'"),
         svn_path_join(target_prefix, target_relative, pool));
    }
  
  apr_hash_set(props,
               svn_path_join(target_prefix, target_relative, pool),
               APR_HASH_KEY_STRING,
               apr_hash_get(prop_hash, propname, APR_HASH_KEY_STRING));
  
  
  if (recurse && (kind == svn_node_dir) && (apr_hash_count(dirents) > 0))
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(pool, dirents);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *this_name;
          svn_dirent_t *this_ent;
          const char *new_target_relative;

          apr_hash_this(hi, &key, NULL, &val);
          this_name = key;
          this_ent = val;

          new_target_relative = svn_path_join(target_relative,
                                              this_name, pool);

          SVN_ERR(remote_propget(props,
                                 propname,
                                 target_prefix,
                                 new_target_relative,
                                 this_ent->kind,
                                 revnum,
                                 ra_session,
                                 recurse,
                                 pool));
        }
    }

  return SVN_NO_ERROR;
}


/* Note: this implementation is very similar to svn_client_proplist. */
svn_error_t *
svn_client_propget2(apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *node;
  const char *utarget;  /* target, or the url for target */
  const char *url;
  svn_revnum_t revnum;

  SVN_ERR(error_if_wcprop_name(propname));

  *props = apr_hash_make(pool);

  SVN_ERR(maybe_convert_to_url(&utarget, target, revision, pool));

  /* Iff utarget is a url, that means we must use it, that is, the
     requested property information is not available locally. */
  if (svn_path_is_url(utarget))
    {
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;

      /* Get an RA plugin for this filesystem object. */
      SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                               &url, target, peg_revision,
                                               revision, ctx, pool));

      SVN_ERR(svn_ra_check_path(ra_session, "", revnum, &kind, pool));

      SVN_ERR(remote_propget(*props, propname, url, "",
                             kind, revnum, ra_session,
                             recurse, pool));
    }
  else  /* working copy path */
    {
      svn_boolean_t pristine;
      struct propget_walk_baton wb;
      static const svn_wc_entry_callbacks_t walk_callbacks
        = { propget_walk_cb };

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target,
                                     FALSE, recurse ? -1 : 0,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     pool));
      SVN_ERR(svn_wc_entry(&node, target, adm_access, FALSE, pool));
      if (! node)
        return svn_error_createf 
          (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
           _("'%s' is not under version control"),
           svn_path_local_style(target, pool));
      
      SVN_ERR(svn_client__get_revision_number
              (&revnum, NULL, revision, target, pool));

      if ((revision->kind == svn_opt_revision_committed)
          || (revision->kind == svn_opt_revision_base))
        {
          pristine = TRUE;
        }
      else  /* must be the working revision */
        {
          pristine = FALSE;
        }

      wb.base_access = adm_access;
      wb.props = *props;
      wb.propname = propname;
      wb.pristine = pristine;

      /* Fetch, recursively or not. */
      if (recurse && (node->kind == svn_node_dir))
        {
          SVN_ERR(svn_wc_walk_entries2(target, adm_access,
                                       &walk_callbacks, &wb, FALSE,
                                       ctx->cancel_func, ctx->cancel_baton,
                                       pool));
        }
      else
        {
          const svn_wc_entry_t *entry;
          SVN_ERR(svn_wc_entry(&entry, target, adm_access, FALSE, pool));
          SVN_ERR(walk_callbacks.found_entry(target, entry, &wb, pool));
        }
      
      SVN_ERR(svn_wc_adm_close(adm_access));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_propget(apr_hash_t **props,
                   const char *propname,
                   const char *target,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_propget2(props, propname, target, revision, revision,
                             recurse, ctx, pool);
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
          (set_rev, ra_session, revision, NULL, pool));

  /* The actual RA call. */
  SVN_ERR(svn_ra_rev_prop(ra_session, *set_rev, propname, propval, pool));

  return SVN_NO_ERROR;
}


/* Push a new 'svn_client_proplist_item_t *' item onto LIST.
 * Allocate the item itself in POOL; set item->node_name to an
 * 'svn_stringbuf_t *' created from PATH and also allocated in POOL,
 * and set item->prop_hash to PROP_HASH.
 *
 * If PROP_HASH is null or has zero count, do nothing.
 */
static void
push_props_on_list(apr_array_header_t *list,
                   apr_hash_t *prop_hash,
                   const char *path,
                   apr_pool_t *pool)
{
  if (prop_hash && apr_hash_count(prop_hash))
    {
      svn_client_proplist_item_t *item
        = apr_palloc(pool, sizeof(svn_client_proplist_item_t));
      item->node_name = svn_stringbuf_create(path, pool);
      item->prop_hash = prop_hash;
      
      *((svn_client_proplist_item_t **) apr_array_push(list)) = item;
    }
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
 * If RECURSE is true and KIND is svn_node_dir, then recurse.
 */
static svn_error_t *
remote_proplist(apr_array_header_t *proplist,
                const char *target_prefix,
                const char *target_relative,
                svn_node_kind_t kind,
                svn_revnum_t revnum,
                svn_ra_session_t *ra_session,
                svn_boolean_t recurse,
                apr_pool_t *pool,
                apr_pool_t *scratchpool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash, *final_hash;
  apr_hash_index_t *hi;
 
  if (kind == svn_node_dir)
    {
      SVN_ERR(svn_ra_get_dir2(ra_session, (recurse ? &dirents : NULL), NULL,
                              &prop_hash, target_relative, revnum,
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
  
  push_props_on_list(proplist, final_hash,
                     svn_path_join(target_prefix, target_relative,
                                   scratchpool),
                     pool);
  
  if (recurse && (kind == svn_node_dir) && (apr_hash_count(dirents) > 0))
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

          SVN_ERR(remote_proplist(proplist,
                                  target_prefix,
                                  new_target_relative,
                                  this_ent->kind,
                                  revnum,
                                  ra_session,
                                  recurse,
                                  pool,
                                  subpool));
        }

      svn_pool_destroy(subpool);
    }

  return SVN_NO_ERROR;
}


/* Push an 'svn_client_proplist_item_t *' item onto PROP_LIST, where
 * item->node_name is an 'svn_stringbuf_t *' created from NODE_NAME,
 * and item->prop_hash is the property hash for NODE_NAME.
 *
 * If PRISTINE is true, get base props, else get working props.
 *
 * Allocate the item and its contents in POOL.
 */
static svn_error_t *
add_to_proplist(apr_array_header_t *prop_list,
                const char *node_name,
                svn_wc_adm_access_t *adm_access,
                svn_boolean_t pristine,
                apr_pool_t *pool)
{
  apr_hash_t *hash;

  SVN_ERR(pristine_or_working_props(&hash, node_name, adm_access, pristine,
                                    pool));
  push_props_on_list(prop_list, hash, node_name, pool);

  return SVN_NO_ERROR;
}

/* A baton for proplist_walk_cb. */
struct proplist_walk_baton
{
  svn_boolean_t pristine;  /* Select base rather than working props. */
  svn_wc_adm_access_t *base_access;  /* Access for the tree being walked. */
  apr_array_header_t *props;  /* Out: array of svn_client_proplist_item_t. */
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

  path = apr_pstrdup(wb->props->pool, path);
  SVN_ERR(add_to_proplist(wb->props, path, wb->base_access,
                          wb->pristine, wb->props->pool));

  return SVN_NO_ERROR;
}


/* Note: this implementation is very similar to svn_client_propget. */
svn_error_t *
svn_client_proplist2(apr_array_header_t **props,
                     const char *target,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *node;
  const char *utarget;  /* target, or the url for target */
  const char *url;
  svn_revnum_t revnum;

  *props = apr_array_make(pool, 5, sizeof(svn_client_proplist_item_t *));

  SVN_ERR(maybe_convert_to_url(&utarget, target, revision, pool));

  /* Iff utarget is a url, that means we must use it, that is, the
     requested property information is not available locally. */
  if (svn_path_is_url(utarget))
    {
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;

      /* Get an RA session for this URL. */
      SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                               &url, target, peg_revision,
                                               revision, ctx, pool));
      
      SVN_ERR(svn_ra_check_path(ra_session, "", revnum, &kind, pool));

      SVN_ERR(remote_proplist(*props, url, "",
                              kind, revnum, ra_session,
                              recurse, pool, svn_pool_create(pool)));
    }
  else  /* working copy path */
    {
      svn_boolean_t pristine;

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, target,
                                     FALSE, recurse ? -1 : 0,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     pool));
      SVN_ERR(svn_wc_entry(&node, target, adm_access, FALSE, pool));
      if (! node)
        return svn_error_createf 
          (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
           _("'%s' is not under version control"),
           svn_path_local_style(target, pool));
      
      SVN_ERR(svn_client__get_revision_number
              (&revnum, NULL, revision, target, pool));

      if ((revision->kind == svn_opt_revision_committed)
          || (revision->kind == svn_opt_revision_base))
        {
          pristine = TRUE;
        }
      else  /* must be the working revision */
        {
          pristine = FALSE;
        }

      /* Fetch, recursively or not. */
      if (recurse && (node->kind == svn_node_dir))
        {
          static const svn_wc_entry_callbacks_t walk_callbacks
            = { proplist_walk_cb };
          struct proplist_walk_baton wb;

          wb.base_access = adm_access;
          wb.props = *props;
          wb.pristine = pristine;

          SVN_ERR(svn_wc_walk_entries2(target, adm_access,
                                       &walk_callbacks, &wb, FALSE,
                                       ctx->cancel_func, ctx->cancel_baton,
                                       pool));
        }
      else 
        SVN_ERR(add_to_proplist(*props, target, adm_access, pristine, pool));
      
      SVN_ERR(svn_wc_adm_close(adm_access));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_proplist(apr_array_header_t **props,
                    const char *target,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_proplist2(props, target, revision, revision,
                              recurse, ctx, pool);
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
          (set_rev, ra_session, revision, NULL, pool));

  /* The actual RA call. */
  SVN_ERR(svn_ra_rev_proplist(ra_session, *set_rev, &proplist, pool));

  *props = proplist;
  return SVN_NO_ERROR;
}
