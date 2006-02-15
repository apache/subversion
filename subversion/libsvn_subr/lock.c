/*
 * lock.c:  routines for svn_lock_t objects.
 *
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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

#include <apr_strings.h>

#include "svn_types.h"


/*** Code. ***/

svn_lock_t *
svn_lock_create(apr_pool_t *pool)
{
  return apr_pcalloc(pool, sizeof(svn_lock_t));
}

svn_lock_t *
svn_lock_dup(const svn_lock_t *lock, apr_pool_t *pool)
{
  svn_lock_t *new_l = apr_palloc(pool, sizeof(*new_l));

  *new_l = *lock;

  new_l->path = apr_pstrdup(pool, new_l->path);
  new_l->token = apr_pstrdup(pool, new_l->token);
  new_l->owner = apr_pstrdup(pool, new_l->owner);
  if (new_l->comment)
    new_l->comment = apr_pstrdup(pool, new_l->comment);

  return new_l;
}
