/*
 * props.c: Utility functions for property handling
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

#include <apr_hash.h>
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "cl.h"




svn_error_t *
svn_cl__print_prop_hash (apr_hash_t *prop_hash,
                         apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first (pool, prop_hash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_stringbuf_t *propval;
      const char *key_native, *val_native;

      apr_hash_this (hi, &key, NULL, &val);
      propval = (svn_stringbuf_t *) val;

      SVN_ERR (svn_utf_cstring_from_utf8 ((const char *) key, &key_native, pool));
      SVN_ERR (svn_utf_cstring_from_utf8 (propval->data, &val_native, pool));

      printf ("  %s : %s\n", key_native, val_native);
    } 
  return SVN_NO_ERROR;
}


svn_error_t *
svn_cl__print_prop_names (apr_hash_t *prop_hash,
                          apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first (pool, prop_hash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *key_native;
      apr_hash_this (hi, &key, NULL, NULL);
      SVN_ERR (svn_utf_cstring_from_utf8 ((const char *) key, &key_native, pool));
      printf ("  %s\n", key_native);
    } 
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: */
