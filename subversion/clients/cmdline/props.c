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
#include "cl.h"




void
svn_cl__print_prop_hash (apr_hash_t *prop_hash,
                         apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first (pool, prop_hash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_stringbuf_t *propval;

      apr_hash_this (hi, &key, NULL, &val);
      propval = (svn_stringbuf_t *) val;

      printf ("  %s : %s\n", (char *) key, propval->data);
    } 
}


void
svn_cl__print_prop_names (apr_hash_t *prop_hash,
                          apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first (pool, prop_hash); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_hash_this (hi, &key, NULL, NULL);
      printf ("  %s\n", (char *) key);
    } 
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: */
