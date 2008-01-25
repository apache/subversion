/*
 * checksum.c:   checksum routines
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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


#include "svn_types.h"
#include "svn_md5.h"
#include "svn_sha1.h"



svn_checksum_t *
svn_checksum_create(svn_checksum_kind_t kind,
                    apr_pool_t *pool)
{
  svn_checksum_t *checksum = apr_palloc(pool, sizeof(*checksum));

  switch (kind)
    {
      case svn_checksum_md5:
        checksum->digest = apr_palloc(pool, APR_MD5_DIGESTSIZE);
        break;

      case svn_checksum_sha1:
        checksum->digest = apr_palloc(pool, APR_SHA1_DIGESTSIZE);
        break;

      default:
        return NULL;
    }

  checksum->kind = kind;
  checksum->pool = pool;

  return checksum;
}
