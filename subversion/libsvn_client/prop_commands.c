/*
 * prop_commands.c:  Implementation of propset, propget, and proplist.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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



/*** Code. ***/

static svn_error_t *
recursive_propset (const char *propname,
                   const svn_string_t *propval,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      void * val;
      const char *current_entry_name;
      svn_stringbuf_t *full_entry_path
        = svn_stringbuf_create (svn_wc_adm_access_path (adm_access), pool);
      const svn_wc_entry_t *current_entry;

      apr_hash_this (hi, &key, NULL, &val);
      keystring = key;
      current_entry = val;
        
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
        current_entry_name = NULL;
      else
        current_entry_name = keystring;

      /* Compute the complete path of the entry */
      if (current_entry_name)
        svn_path_add_component (full_entry_path, current_entry_name);

      if (current_entry->schedule != svn_wc_schedule_delete)
        {
          svn_error_t *err;
          if (current_entry->kind == svn_node_dir && current_entry_name)
            {
              svn_wc_adm_access_t *dir_access;

              SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                            full_entry_path->data, pool));
              err = recursive_propset (propname, propval, dir_access, pool);
            }
          else
            {
              err = svn_wc_prop_set (propname, propval,
                                     full_entry_path->data, adm_access, pool);
            }
          if (err)
            {
              if (err->apr_err != SVN_ERR_ILLEGAL_TARGET)
                return err;
              svn_error_clear (err);
            }
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_propset (const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_boolean_t recurse,
                    apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *node;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target, TRUE, TRUE, pool));
  SVN_ERR (svn_wc_entry (&node, target, adm_access, FALSE, pool));
  if (!node)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
                              "'%s' -- not a versioned resource", 
                              target);

  if (recurse && node->kind == svn_node_dir)
    {
      SVN_ERR (recursive_propset (propname, propval, adm_access, pool));
    }
  else
    {
      SVN_ERR (svn_wc_prop_set (propname, propval, target, adm_access, pool));
    }

  SVN_ERR (svn_wc_adm_close (adm_access));
  return SVN_NO_ERROR;
}



svn_error_t *
svn_client_revprop_set (const char *propname,
                        const svn_string_t *propval,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_client_auth_baton_t *auth_baton,
                        svn_revnum_t *set_rev,
                        apr_pool_t *pool)
{
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files or store the auth data. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, NULL,
                                        NULL, NULL, FALSE, FALSE, TRUE,
                                        auth_baton, pool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR (svn_client__get_revision_number
           (set_rev, ra_lib, session, revision, NULL, pool));

  /* The actual RA call. */
  SVN_ERR (ra_lib->change_rev_prop (session, *set_rev, propname, propval));

  /* All done. */
  SVN_ERR (ra_lib->close(session));

  return SVN_NO_ERROR;
}


/* Set *PROPS to the pristine (base) properties at PATH, if PRISTINE
 * is true, or else the working value if PRISTINE is false.  
 *
 * The keys of *PROPS will be `const char *' property names, and the
 * values `const svn_stringbuf_t *' property values.  Allocate *PROPS
 * and its contents in POOL.
 */
static svn_error_t *
pristine_or_working_props (apr_hash_t **props,
                           const char *path,
                           svn_boolean_t pristine,
                           apr_pool_t *pool)
{
  if (pristine)
    SVN_ERR (svn_wc_get_prop_diffs (NULL, props, path, pool));
  else
    SVN_ERR (svn_wc_prop_list (props, path, pool));
  
  return SVN_NO_ERROR;
}


/* Set *PROPVAL to the pristine (base) value of property PROPNAME at
 * PATH, if PRISTINE is true, or else the working value if PRISTINE is
 * false.  Allocate *PROPVAL in POOL.
 */
static svn_error_t *
pristine_or_working_propval (const svn_string_t **propval,
                             const char *propname,
                             const char *path,
                             svn_boolean_t pristine,
                             apr_pool_t *pool)
{
  if (pristine)
    {
      apr_hash_t *pristine_props;
      
      SVN_ERR (svn_wc_get_prop_diffs (NULL, &pristine_props, path, pool));
      *propval = apr_hash_get (pristine_props, propname, APR_HASH_KEY_STRING);
    }
  else  /* get the working revision */
    {
      SVN_ERR (svn_wc_prop_get (propval, propname, path, pool));
    }
  
  return SVN_NO_ERROR;
}


/* Helper for svn_client_propget.
 * 
 * Starting from the path associated with ADM_ACCESS, populate PROPS
 * with the values of property PROPNAME.  If PRISTINE is true, use the
 * base values, else use working values.
 *
 * The keys of PROPS will be `const char *' paths, rooted at the
 * path svn_wc_adm_access_path(ADM_ACCESS), and the values are
 * `const svn_string_t *' property values.
 */
static svn_error_t *
recursive_propget (apr_hash_t *props,
                   const char *propname,
                   svn_boolean_t pristine,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      void * val;
      const char *current_entry_name;
      const char *full_entry_path;
      const svn_wc_entry_t *current_entry;

      apr_hash_this (hi, &key, NULL, &val);
      keystring = key;
      current_entry = val;
    
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
          current_entry_name = NULL;
      else
          current_entry_name = keystring;

      /* Compute the complete path of the entry */
      if (current_entry_name)
        full_entry_path = svn_path_join (svn_wc_adm_access_path (adm_access),
                                         current_entry_name, pool);
      else
        full_entry_path = apr_pstrdup (pool,
                                       svn_wc_adm_access_path (adm_access));

      if (current_entry->schedule != svn_wc_schedule_delete)
        {
          if (current_entry->kind == svn_node_dir && current_entry_name)
            {
              svn_wc_adm_access_t *dir_access;
              SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                            full_entry_path, pool));
              SVN_ERR (recursive_propget (props, propname, pristine,
                                          dir_access, pool));
            }
          else
            {
              const svn_string_t *propval;

              SVN_ERR (pristine_or_working_propval (&propval, propname,
                                                    full_entry_path,
                                                    pristine, pool));
              if (propval)
                apr_hash_set (props, full_entry_path,
                              APR_HASH_KEY_STRING, propval);
            }
        }
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
maybe_convert_to_url (const char **new_target,
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
      && (! svn_path_is_url (target)))
    {
      svn_wc_adm_access_t *adm_access;
      svn_node_kind_t kind;
      const char *pdir;
      const svn_wc_entry_t *entry;
      
      SVN_ERR (svn_io_check_path (target, &kind, pool));
      if (kind == svn_node_file)
        svn_path_split (target, &pdir, NULL, pool);
      else
        pdir = target;
      
      SVN_ERR (svn_wc_adm_open (&adm_access, NULL, pdir, FALSE, FALSE, pool));
      SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, pool));
      if (! entry)
        return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
                                  "'%s' is not a versioned resource", 
                                  target);
      *new_target = entry->url;
    }
  else
    *new_target = target;

  return SVN_NO_ERROR;
}


/* Helper for the remote case of svn_client_propget.
 *
 * Get the value of property PROPNAME in REVNUM, using RA_LIB and
 * SESSION.  Store the value (`svn_string_t *') in PROPS, under the
 * path key "TARGET_PREFIX/TARGET_RELATIVE" (`const char *').
 *
 * If RECURSE is true and KIND is svn_node_dir, then recurse.
 *
 * KIND is the kind of the node at "TARGET_PREFIX/TARGET_RELATIVE".
 * Yes, caller passes this; it makes the recursion more efficient :-). 
 *
 * Allocate the keys and values in POOL.
 */
static svn_error_t *
remote_propget (apr_hash_t *props,
                const char *propname,
                const char *target_prefix,
                const char *target_relative,
                svn_node_kind_t kind,
                svn_revnum_t revnum,
                svn_ra_plugin_t *ra_lib,
                void *session,
                svn_boolean_t recurse,
                apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash;
  
  if (kind == svn_node_dir)
    {
      SVN_ERR (ra_lib->get_dir (session, target_relative, revnum,
                                (recurse ? &dirents : NULL),
                                NULL, &prop_hash));
    }
  else if (kind == svn_node_file)
    {
      SVN_ERR (ra_lib->get_file (session, target_relative, revnum,
                                 NULL, NULL, &prop_hash));
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, 0, NULL,
         "unknown node kind for \"%s\"",
         svn_path_join (target_prefix, target_relative, pool));
    }
  
  apr_hash_set (props,
                svn_path_join (target_prefix, target_relative, pool),
                APR_HASH_KEY_STRING,
                apr_hash_get (prop_hash, propname, APR_HASH_KEY_STRING));
  
  
  if (recurse && (kind == svn_node_dir) && (apr_hash_count (dirents) > 0))
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first (pool, dirents);
           hi;
           hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *this_name;
          svn_dirent_t *this_ent;
          const char *new_target_relative;

          apr_hash_this (hi, &key, NULL, &val);
          this_name = key;
          this_ent = val;

          new_target_relative = svn_path_join (target_relative,
                                               this_name, pool);

          SVN_ERR (remote_propget (props,
                                   propname,
                                   target_prefix,
                                   new_target_relative,
                                   this_ent->kind,
                                   revnum,
                                   ra_lib,
                                   session,
                                   recurse,
                                   pool));
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_propget (apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *revision,
                    svn_client_auth_baton_t *auth_baton,
                    svn_boolean_t recurse,
                    apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *node;
  const char *utarget;  /* target, or the url for target */
  svn_node_kind_t kind;
  svn_revnum_t revnum;

  SVN_ERR (maybe_convert_to_url (&utarget, target, revision, pool));

  /* Iff utarget is a url, that means we must use it, that is, the
     requested property information is not available locally. */
  if (svn_path_is_url (utarget))
    {
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;
      svn_opt_revision_t new_revision;  /* only used in one case */

      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, utarget, pool));
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, utarget,
                                            NULL, NULL, NULL, TRUE,
                                            FALSE, FALSE, auth_baton, pool));

      *props = apr_hash_make (pool);

      /* Default to HEAD. */
      if (revision->kind == svn_opt_revision_unspecified)
        {
          new_revision.kind = svn_opt_revision_head;
          revision = &new_revision;
        }

      /* Handle the various different kinds of revisions. */
      if ((revision->kind == svn_opt_revision_head)
          || (revision->kind == svn_opt_revision_date)
          || (revision->kind == svn_opt_revision_number))
        {
          SVN_ERR (svn_client__get_revision_number
                   (&revnum, ra_lib, session, revision, NULL, pool));

          SVN_ERR (ra_lib->check_path (&kind, session, "", revnum));

          SVN_ERR (remote_propget (*props, propname, utarget, "",
                                   kind, revnum, ra_lib, session,
                                   recurse, pool));
        }
      else if (revision->kind == svn_opt_revision_previous)
        {
          if (svn_path_is_url (target))
            {
              return svn_error_createf
                (SVN_ERR_ILLEGAL_TARGET, 0, NULL,
                 "\"%s\" is a url, but revision kind requires a working copy",
                 target);
            }
          else  /* target is a working copy path */
            {
              if (! recurse)
                {
                  SVN_ERR (svn_client__get_revision_number
                           (&revnum, NULL, NULL, revision, target, pool));
                  
                  SVN_ERR (ra_lib->check_path (&kind, session, "", revnum));

                  SVN_ERR (remote_propget (*props, propname, utarget, "",
                                           kind, revnum, ra_lib, session,
                                           recurse, pool));
                }
              else  /* ### recursive case unimplemented */
                {
                  return svn_error_createf
                    (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
                 "Recursing on previous revision not yet supported.\n"
                 "(http://subversion.tigris.org/issues/show_bug.cgi?id=943)\n",
                     target);
                  
                  /* In the recursive case, the correctest behavior is
                     hard to implement.  If the command is

                         svn propget -rPREV -R SOMEDIR

                     then the PREV keyword will expand to the previous
                     revision for SOMEDIR, and then *that same*
                     revision will be used all the way down the
                     recursion, even though there might be objects
                     beneath SOMEDIR that have different previous
                     revisions.  For now, we punt and don't handle
                     'svn propget -R -rPREV' at all.  I suspect it's
                     an edge case anyway, and I'm not sure that my
                     idea of the "correct" interpretation of PREV
                     will be the user's idea anyway. */
                }
            }
        }
      else
        {
          return svn_error_create
            (SVN_ERR_CLIENT_BAD_REVISION, 0, NULL, "unknown revision kind");
        }

      /* Close the RA session. */
      SVN_ERR (ra_lib->close (session));
    }
  else  /* working copy path */
    {
      apr_hash_t *prop_hash;
      svn_boolean_t pristine;

      prop_hash = apr_hash_make (pool);
      
      SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target,
                                      FALSE, TRUE, pool));
      
      SVN_ERR (svn_wc_entry (&node, target, adm_access, FALSE, pool));
      if (! node)
        return svn_error_createf
          (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
           "'%s' -- not a versioned resource", target);
      
      SVN_ERR (svn_client__get_revision_number
               (&revnum, NULL, NULL, revision, target, pool));

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
          SVN_ERR (recursive_propget (prop_hash, propname, pristine,
                                      adm_access, pool));
        }
      else
        {
          const svn_string_t *propval;
          
          SVN_ERR (pristine_or_working_propval (&propval, propname,
                                                target, pristine, pool));

          apr_hash_set (prop_hash, target, APR_HASH_KEY_STRING, propval);
        }
      
      SVN_ERR (svn_wc_adm_close (adm_access));
      
      *props = prop_hash;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_revprop_get (const char *propname,
                        svn_string_t **propval,
                        const char *URL,
                        const svn_opt_revision_t *revision,
                        svn_client_auth_baton_t *auth_baton,
                        svn_revnum_t *set_rev,
                        apr_pool_t *pool)
{
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files or store the auth data. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, NULL,
                                        NULL, NULL, FALSE, FALSE, TRUE,
                                        auth_baton, pool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR (svn_client__get_revision_number
           (set_rev, ra_lib, session, revision, NULL, pool));

  /* The actual RA call. */
  SVN_ERR (ra_lib->rev_prop (session, *set_rev, propname, propval));

  /* All done. */
  SVN_ERR (ra_lib->close(session));

  return SVN_NO_ERROR;
}


/* Push a new `svn_client_proplist_item_t *' item onto LIST.
 * Allocate the item itself in POOL; set item->node_name to an
 * `svn_stringbuf_t *' created from PATH and also allocated in POOL,
 * and set item->prop_hash to PROP_HASH.
 *
 * If PROP_HASH is null or has zero count, do nothing.
 */
static void
push_props_on_list (apr_array_header_t *list,
                    apr_hash_t *prop_hash,
                    const char *path,
                    apr_pool_t *pool)
{
  if (prop_hash && apr_hash_count (prop_hash))
    {
      svn_client_proplist_item_t *item
        = apr_palloc (pool, sizeof (svn_client_proplist_item_t));
      item->node_name = svn_stringbuf_create (path, pool);
      item->prop_hash = prop_hash;
      
      *((svn_client_proplist_item_t **) apr_array_push (list)) = item;
    }
}


/* Helper for the remote case of svn_client_proplist.
 *
 * Push a new `svn_client_proplist_item_t *' item onto PROPLIST,
 * containing the properties for "TARGET_PREFIX/TARGET_RELATIVE" in
 * REVNUM, obtained using RA_LIB and SESSION.  The item->node_name
 * will be "TARGET_PREFIX/TARGET_RELATIVE", and the value will be a
 * hash mapping `const char *' property names onto `svn_stringbuf_t *'
 * property values.  Allocate the new item and its contents in POOL.
 *
 * KIND is the kind of the node at "TARGET_PREFIX/TARGET_RELATIVE".
 *
 * If RECURSE is true and KIND is svn_node_dir, then recurse.
 */
static svn_error_t *
remote_proplist (apr_array_header_t *proplist,
                 const char *target_prefix,
                 const char *target_relative,
                 svn_node_kind_t kind,
                 svn_revnum_t revnum,
                 svn_ra_plugin_t *ra_lib,
                 void *session,
                 svn_boolean_t recurse,
                 apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_hash_t *prop_hash;
  apr_hash_index_t *hi;
  
  if (kind == svn_node_dir)
    {
      SVN_ERR (ra_lib->get_dir (session, target_relative, revnum,
                                (recurse ? &dirents : NULL),
                                NULL, &prop_hash));
    }
  else if (kind == svn_node_file)
    {
      SVN_ERR (ra_lib->get_file (session, target_relative, revnum,
                                 NULL, NULL, &prop_hash));
    }
  else
    {
      return svn_error_createf
        (SVN_ERR_NODE_UNKNOWN_KIND, 0, NULL,
         "unknown node kind for \"%s\"",
         svn_path_join (target_prefix, target_relative, pool));
    }
  
  /* Filter out non-regular properties, since the RA layer
     returns all kinds. */
  for (hi = apr_hash_first (pool, prop_hash);
       hi;
       hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      svn_prop_kind_t prop_kind;
      
      apr_hash_this (hi, &key, &klen, NULL);
      prop_kind = svn_property_kind (NULL, (const char *) key);
      
      if (prop_kind != svn_prop_regular_kind)
        apr_hash_set (prop_hash, key, klen, NULL);
    }
  
  push_props_on_list (proplist, prop_hash,
                      svn_path_join (target_prefix, target_relative, pool),
                      pool);
  
  if (recurse && (kind == svn_node_dir) && (apr_hash_count (dirents) > 0))
    {
      for (hi = apr_hash_first (pool, dirents);
           hi;
           hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *this_name;
          svn_dirent_t *this_ent;
          const char *new_target_relative;

          apr_hash_this (hi, &key, NULL, &val);
          this_name = key;
          this_ent = val;

          new_target_relative = svn_path_join (target_relative,
                                               this_name, pool);

          SVN_ERR (remote_proplist (proplist,
                                    target_prefix,
                                    new_target_relative,
                                    this_ent->kind,
                                    revnum,
                                    ra_lib,
                                    session,
                                    recurse,
                                    pool));
        }
    }

  return SVN_NO_ERROR;
}


/* Push an `svn_client_proplist_item_t *' item onto PROP_LIST, where
 * item->node_name is an `svn_stringbuf_t *' created from NODE_NAME,
 * and item->prop_hash is the property hash for NODE_NAME.
 *
 * If PRISTINE is true, get base props, else get working props.
 *
 * Allocate the item and its contents in POOL.
 */
static svn_error_t *
add_to_proplist (apr_array_header_t *prop_list,
                 const char *node_name,
                 svn_boolean_t pristine,
                 apr_pool_t *pool)
{
  apr_hash_t *hash;

  SVN_ERR (pristine_or_working_props (&hash, node_name, pristine, pool));
  push_props_on_list (prop_list, hash, node_name, pool);

  return SVN_NO_ERROR;
}

/* Helper for svn_client_proplist.
 * 
 * Starting from the path associated with ADM_ACCESS, populate PROPS
 * with the values of property PROPNAME.  If PRISTINE is true, use the
 * base values, else use working values.
 *
 * The keys of PROPS will be `const char *' paths, rooted at the
 * path svn_wc_adm_access_path(ADM_ACCESS), and the values are
 * `const svn_string_t *' property values.
 */
static svn_error_t *
recursive_proplist (apr_array_header_t *props,
                    svn_wc_adm_access_t *adm_access,
                    svn_boolean_t pristine,
                    apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      void * val;
      const char *current_entry_name;
      const char *full_entry_path;
      const svn_wc_entry_t *current_entry;

      apr_hash_this (hi, &key, NULL, &val);
      keystring = key;
      current_entry = val;
    
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
          current_entry_name = NULL;
      else
          current_entry_name = keystring;

      /* Compute the complete path of the entry */
      if (current_entry_name)
        full_entry_path = svn_path_join (svn_wc_adm_access_path (adm_access),
                                         current_entry_name, pool);
      else
        full_entry_path = apr_pstrdup (pool,
                                       svn_wc_adm_access_path (adm_access));

      if (current_entry->schedule != svn_wc_schedule_delete)
        {
          if (current_entry->kind == svn_node_dir && current_entry_name)
            {
              svn_wc_adm_access_t *dir_access;
              SVN_ERR (svn_wc_adm_retrieve (&dir_access, adm_access,
                                            full_entry_path, pool));
              SVN_ERR (recursive_proplist (props, dir_access, pristine, pool));
            }
          else
            SVN_ERR (add_to_proplist (props, full_entry_path, pristine, pool));
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_proplist (apr_array_header_t **props,
                     const char *target, 
                     const svn_opt_revision_t *revision,
                     svn_client_auth_baton_t *auth_baton,
                     svn_boolean_t recurse,
                     apr_pool_t *pool)
{
  apr_array_header_t *prop_list
      = apr_array_make (pool, 5, sizeof (svn_client_proplist_item_t *));
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  const char *utarget;  /* target, or the url for target */
  svn_revnum_t revnum;

  SVN_ERR (maybe_convert_to_url (&utarget, target, revision, pool));

  if (svn_path_is_url (utarget))
    {
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;
      svn_node_kind_t kind;
      svn_opt_revision_t new_revision;  /* only used in one case */

      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, utarget, pool));
      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, utarget,
                                            NULL, NULL, NULL, TRUE,
                                            FALSE, FALSE, auth_baton, pool));

      /* Default to HEAD. */
      if (revision->kind == svn_opt_revision_unspecified)
        {
          new_revision.kind = svn_opt_revision_head;
          revision = &new_revision;
        }

      /* Handle the various different kinds of revisions. */
      if ((revision->kind == svn_opt_revision_head)
          || (revision->kind == svn_opt_revision_date)
          || (revision->kind == svn_opt_revision_number))
        {
          SVN_ERR (svn_client__get_revision_number
                   (&revnum, ra_lib, session, revision, NULL, pool));

          SVN_ERR (ra_lib->check_path (&kind, session, "", revnum));

          SVN_ERR (remote_proplist (prop_list, utarget, "",
                                    kind, revnum, ra_lib, session,
                                    recurse, pool));
        }
      else if (revision->kind == svn_opt_revision_previous)
        {
          if (svn_path_is_url (target))
            {
              return svn_error_createf
                (SVN_ERR_ILLEGAL_TARGET, 0, NULL,
                 "\"%s\" is a url, but revision kind requires a working copy",
                 target);
            }
          else  /* it's a working copy path */
            {
              if (! recurse)
                {
                  SVN_ERR (svn_client__get_revision_number
                           (&revnum, NULL, NULL, revision, target, pool));
                  
                  SVN_ERR (ra_lib->check_path (&kind, session, "", revnum));

                  SVN_ERR (remote_proplist (prop_list, utarget, "",
                                            kind, revnum, ra_lib, session,
                                            recurse, pool));
                }
              else  /* ### recursive case unimplemented */
                {
                  return svn_error_createf
                    (SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL,
                 "Recursing on previous revision not yet supported.\n"
                 "(http://subversion.tigris.org/issues/show_bug.cgi?id=943)\n",
                     target);
                  
                  /* In the recursive case, the correctest behavior is
                     hard to implement.  If the command is

                         svn proplist -rPREV -R SOMEDIR

                     then the PREV keyword will expand to the previous
                     revision for SOMEDIR, and then *that same*
                     revision will be used all the way down the
                     recursion, even though there might be objects
                     beneath SOMEDIR that have different previous
                     revisions.  For now, we punt and don't handle
                     'svn proplist -R -rPREV' at all.  I suspect it's
                     an edge case anyway, and I'm not sure that my
                     idea of the "correct" interpretation of PREV
                     will be the user's idea anyway. */
                }
            }
        }
      else
        {
          return svn_error_create
            (SVN_ERR_CLIENT_BAD_REVISION, 0, NULL, "unknown revision kind");
        }

      /* Close the RA session. */
      SVN_ERR (ra_lib->close (session));
    }
  else  /* working copy path */
    {
      svn_boolean_t pristine;

      SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target, FALSE, TRUE,
                                      pool));
      SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, pool));
      if (! entry)
        return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL,
                                  "'%s' -- not a versioned resource", 
                                  target);
      
      SVN_ERR (svn_client__get_revision_number
               (&revnum, NULL, NULL, revision, target, pool));
      
      if ((revision->kind == svn_opt_revision_committed)
          || (revision->kind == svn_opt_revision_base))
        {
          pristine = TRUE;
        }
      else  /* must be the working revision */
        {
          pristine = FALSE;
        }

      if (recurse && entry->kind == svn_node_dir)
        SVN_ERR (recursive_proplist (prop_list, adm_access, pristine, pool));
      else 
        SVN_ERR (add_to_proplist (prop_list, target, pristine, pool));
      
      SVN_ERR (svn_wc_adm_close (adm_access));
    }

  *props = prop_list;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_revprop_list (apr_hash_t **props,
                         const char *URL,
                         const svn_opt_revision_t *revision,
                         svn_client_auth_baton_t *auth_baton,
                         svn_revnum_t *set_rev,
                         apr_pool_t *pool)
{
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  apr_hash_t *proplist;
  apr_hash_index_t *hi;

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files or store the auth data. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, NULL,
                                        NULL, NULL, FALSE, FALSE, TRUE,
                                        auth_baton, pool));

  /* Resolve the revision into something real, and return that to the
     caller as well. */
  SVN_ERR (svn_client__get_revision_number
           (set_rev, ra_lib, session, revision, NULL, pool));

  /* The actual RA call. */
  SVN_ERR (ra_lib->rev_proplist (session, *set_rev, &proplist));

  for (hi = apr_hash_first (pool, proplist); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_ssize_t klen;
      
      apr_hash_this (hi, &key, &klen, &val);
      apr_hash_set (proplist, key, klen, val);
    } 

  /* All done. */
  SVN_ERR (ra_lib->close(session));
  
  *props = proplist;
  return SVN_NO_ERROR;
}
