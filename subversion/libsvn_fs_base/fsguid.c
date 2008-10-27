/* fsguid.c : operations on FS-global unique identifiers
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

#include "fs.h"
#include "trail.h"
#include "err.h"
#include "key-gen.h"
#include "fsguid.h"
#include "bdb/miscellaneous-table.h"
#include "../libsvn_fs/fs-loader.h"

#include "private/svn_fs_util.h"


/* Trail function for svn_fs_base__reserve_fsguid().  BATON in this
   case is a 'const char **FSGUID', into which is written the returned
   value.  */
static svn_error_t *
txn_body_reserve_fsguid(void *baton, trail_t *trail)
{
  const char **fsguid_p = baton;
  const char *next_fsguid;
  const char *new_fsguid;

  SVN_ERR(svn_fs_bdb__miscellaneous_get(&next_fsguid, trail->fs, 
                                        SVN_FS_BASE__MISC_NEXT_FSGUID,
                                        trail, trail->pool));

  /* If we've no next-fsguid value, let's hope it's because this is the
     first time we're asking for one -- we'll use '0' as the next key,
     and initialize the next-fsguid value with '1'.  Otherwise, we
     use what we find and increment it as a base-36 number.  */
  if (! next_fsguid)
    {
      next_fsguid = "0";
      new_fsguid = "1";
    }
  else
    {
      char next_next_fsguid[MAX_KEY_SIZE];
      apr_size_t len;
      len = strlen(next_fsguid);

      svn_fs_base__next_key(next_fsguid, &len, next_next_fsguid);
      if (! len)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                "next-fsguid is not a base-36 value");
      new_fsguid = apr_pstrmemdup(trail->pool, next_next_fsguid, len);
    }
  SVN_ERR(svn_fs_bdb__miscellaneous_set(trail->fs, 
                                        SVN_FS_BASE__MISC_NEXT_FSGUID,
                                        new_fsguid, trail, trail->pool));
  *fsguid_p = next_fsguid;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_base__reserve_fsguid(svn_fs_t *fs,
                            const char **fsguid,
                            trail_t *trail,
                            apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR_ASSERT(bfd->format >= SVN_FS_BASE__MIN_MISCELLANY_FORMAT);

  /* Have no trail?  We'll make a one-off, do the work, and get outta here. */
  if (! trail)
    return svn_fs_base__retry_txn(fs, txn_body_reserve_fsguid, fsguid, pool);
    
  SVN_ERR(txn_body_reserve_fsguid((void *)fsguid, trail));
  *fsguid = apr_pstrdup(pool, *fsguid);
  return SVN_NO_ERROR;
}
