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

svn_error_t *
svn_client_propset (const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_boolean_t recurse,
                    apr_pool_t *pool)
{
  svn_wc_entry_t *node;

  SVN_ERR (svn_wc_entry (&node, target, FALSE, pool));
  if (!node)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
                              "'%s' -- not a versioned resource", 
                              target);

  if (recurse && node->kind == svn_node_dir)
    {
      apr_hash_t *entries;
      apr_hash_index_t *hi;
      SVN_ERR (svn_wc_entries_read (&entries, target, FALSE, pool));

      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          const char *keystring;
          apr_ssize_t klen;
          void * val;
          const char *current_entry_name;
          svn_stringbuf_t *full_entry_path = svn_stringbuf_create (target,
                                                                   pool);
          svn_wc_entry_t *current_entry;

          apr_hash_this (hi, &key, &klen, &val);
          keystring = key;
          current_entry = val;
        
          if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
              current_entry_name = NULL;
          else
              current_entry_name = keystring;

          /* Compute the complete path of the entry */
          if (current_entry_name)
            svn_path_add_component_nts (full_entry_path, current_entry_name);

          if (current_entry->schedule != svn_wc_schedule_delete)
            {
              if (current_entry->kind == svn_node_dir && current_entry_name)
                {
                  SVN_ERR (svn_client_propset (propname, propval,
                                               full_entry_path->data, recurse,
                                               pool));
                }
              else
                {
                  SVN_ERR (svn_wc_prop_set (propname, propval,
                                            full_entry_path->data, pool));
                }
            }
        }
      
    }
  else
    {
      SVN_ERR (svn_wc_prop_set (propname, propval, target, pool));
    }
  return SVN_NO_ERROR;
}

/* Helper for svn_client_propget. */
static svn_error_t *
recursive_propget (apr_hash_t *props,
                   const char *propname,
                   const char *target,
                   apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  SVN_ERR (svn_wc_entries_read (&entries, target, FALSE, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      void * val;
      const char *current_entry_name;
      const char *full_entry_path;
      svn_wc_entry_t *current_entry;

      apr_hash_this (hi, &key, NULL, &val);
      keystring = key;
      current_entry = val;
    
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
          current_entry_name = NULL;
      else
          current_entry_name = keystring;

      /* Compute the complete path of the entry */
      if (current_entry_name)
        full_entry_path = svn_path_join (target, current_entry_name, pool);
      else
        full_entry_path = apr_pstrdup (pool, target);

      if (current_entry->schedule != svn_wc_schedule_delete)
        {
          if (current_entry->kind == svn_node_dir && current_entry_name)
            {
              SVN_ERR (recursive_propget (props, propname,
                                          full_entry_path, pool));
            }
          else
            {
              const svn_string_t *propval;
              SVN_ERR (svn_wc_prop_get (&propval, propname,
                                        full_entry_path, pool));
              if (propval)
                apr_hash_set (props, full_entry_path,
                              APR_HASH_KEY_STRING, propval);
            }
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_propget (apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    svn_boolean_t recurse,
                    apr_pool_t *pool)
{
  apr_hash_t *prop_hash = apr_hash_make (pool);
  svn_wc_entry_t *node;

  SVN_ERR (svn_wc_entry (&node, target, FALSE, pool));
  if (!node)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
                              "'%s' -- not a versioned resource", target);

  if (recurse && node->kind == svn_node_dir)
    {
      SVN_ERR (recursive_propget (prop_hash, propname, target, pool));
    }
  else
    {
      const svn_string_t *propval;
      SVN_ERR (svn_wc_prop_get (&propval, propname, target, pool));
      if (propval)
        apr_hash_set (prop_hash, target, APR_HASH_KEY_STRING, propval);
    }

  *props = prop_hash;
  return SVN_NO_ERROR;
}

/* Helper for svn_client_proplist, and recursive_proplist. */
static svn_error_t *
add_to_proplist (apr_array_header_t *prop_list,
                 const char *node_name,
                 apr_pool_t *pool)
{
  apr_hash_t *hash;

  SVN_ERR (svn_wc_prop_list (&hash, node_name, pool));

  if (hash && apr_hash_count (hash))
    {
      svn_client_proplist_item_t *item
          = apr_palloc(pool, sizeof(svn_client_proplist_item_t));
      item->node_name = svn_stringbuf_create (node_name, pool);
      item->prop_hash = hash;

      *((svn_client_proplist_item_t **)apr_array_push(prop_list)) = item;
    }

  return SVN_NO_ERROR;
}

/* Helper for svn_client_proplist. */
static svn_error_t *
recursive_proplist (apr_array_header_t *props,
                    const char *target,
                    apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  SVN_ERR (svn_wc_entries_read (&entries, target, FALSE, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *keystring;
      apr_ssize_t klen;
      void * val;
      const char *current_entry_name;
      const char *full_entry_path;
      svn_wc_entry_t *current_entry;

      apr_hash_this (hi, &key, &klen, &val);
      keystring = key;
      current_entry = val;
    
      if (! strcmp (keystring, SVN_WC_ENTRY_THIS_DIR))
          current_entry_name = NULL;
      else
          current_entry_name = keystring;

      /* Compute the complete path of the entry */
      if (current_entry_name)
        full_entry_path = svn_path_join (target, current_entry_name, pool);
      else
        full_entry_path = apr_pstrdup (pool, target);

      if (current_entry->schedule != svn_wc_schedule_delete)
        {
          if (current_entry->kind == svn_node_dir && current_entry_name)
              SVN_ERR (recursive_proplist (props, full_entry_path, pool));
          else
              SVN_ERR (add_to_proplist (props, full_entry_path, pool));
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_proplist (apr_array_header_t **props,
                     const char *target, 
                     svn_boolean_t recurse,
                     apr_pool_t *pool)
{
  apr_array_header_t *prop_list
      = apr_array_make (pool, 5, sizeof (svn_client_proplist_item_t *));
  svn_wc_entry_t *entry;

  SVN_ERR (svn_wc_entry (&entry, target, FALSE, pool));
  if (! entry)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, 0, NULL, pool,
                              "'%s' -- not a versioned resource", 
                              target);


  if (recurse && entry->kind == svn_node_dir)
      SVN_ERR (recursive_proplist (prop_list, target, pool));
  else 
      SVN_ERR (add_to_proplist (prop_list, target, pool));

  *props = prop_list;
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: */
