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
      const char *pname;
      svn_stringbuf_t *propval;
      const char *pname_native;

      apr_hash_this (hi, &key, NULL, &val);
      pname = key;
      propval = val;

      /* Distinguish between svn: and non-svn: props -- the former are
         stored in UTF-8, the latter are stored as binary values.  All
         property names, however, are stored in UTF-8.  */
      if (svn_prop_is_svn_prop (pname))
        SVN_ERR (svn_utf_stringbuf_from_utf8 (&propval, propval, pool));
      SVN_ERR (svn_utf_cstring_from_utf8 (&pname_native, pname, pool));
      printf ("  %s : %s\n", pname_native, propval->data);
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
      SVN_ERR (svn_utf_cstring_from_utf8 (&key_native, key, pool));
      printf ("  %s\n", key_native);
    } 
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: */
