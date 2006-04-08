/* dbt.c --- DBT-frobbing functions
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>

#define APU_WANT_DB
#include <apu_want.h>

#include "../id.h"
#include "dbt.h"


DBT *
svn_fs_base__clear_dbt(DBT *dbt)
{
  memset(dbt, 0, sizeof(*dbt));

  return dbt;
}


DBT *svn_fs_base__nodata_dbt(DBT *dbt)
{
  svn_fs_base__clear_dbt(dbt);

  /* A `nodata' dbt is one which retrieves zero bytes from offset zero,
     and stores them in a zero-byte buffer in user-allocated memory.  */
  dbt->flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);
  dbt->doff = dbt->dlen = 0;

  return dbt;
}


DBT *
svn_fs_base__set_dbt(DBT *dbt, const void *data, u_int32_t size)
{
  svn_fs_base__clear_dbt(dbt);

  dbt->data = (void *) data;
  dbt->size = size;

  return dbt;
}


DBT *
svn_fs_base__result_dbt(DBT *dbt)
{
  svn_fs_base__clear_dbt(dbt);
  dbt->flags |= DB_DBT_MALLOC;

  return dbt;
}


/* An APR pool cleanup function that simply applies `free' to its
   argument.  */
static apr_status_t
apr_free_cleanup(void *arg)
{
  free(arg);

  return 0;
}


DBT *
svn_fs_base__track_dbt(DBT *dbt, apr_pool_t *pool)
{
  if (dbt->data)
    apr_pool_cleanup_register(pool, dbt->data, apr_free_cleanup,
                              apr_pool_cleanup_null);

  return dbt;
}


DBT *
svn_fs_base__recno_dbt(DBT *dbt, db_recno_t *recno)
{
  svn_fs_base__set_dbt(dbt, recno, sizeof(*recno));
  dbt->ulen = dbt->size;
  dbt->flags |= DB_DBT_USERMEM;

  return dbt;
}


int
svn_fs_base__compare_dbt(const DBT *a, const DBT *b)
{
  int common_size = a->size > b->size ? b->size : a->size;
  int cmp = memcmp(a->data, b->data, common_size);

  if (cmp)
    return cmp;
  else
    return a->size - b->size;
}



/* Building DBT's from interesting things.  */


/* Set DBT to the unparsed form of ID; allocate memory from POOL.
   Return DBT.  */
DBT *
svn_fs_base__id_to_dbt(DBT *dbt,
                       const svn_fs_id_t *id,
                       apr_pool_t *pool)
{
  svn_string_t *unparsed_id = svn_fs_base__id_unparse(id, pool);
  svn_fs_base__set_dbt(dbt, unparsed_id->data, unparsed_id->len);
  return dbt;
}


/* Set DBT to the unparsed form of SKEL; allocate memory from POOL.  */
DBT *
svn_fs_base__skel_to_dbt(DBT *dbt,
                         skel_t *skel,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *unparsed_skel = svn_fs_base__unparse_skel(skel, pool);
  svn_fs_base__set_dbt(dbt, unparsed_skel->data, unparsed_skel->len);
  return dbt;
}


/* Set DBT to the text of the null-terminated string STR.  DBT will
   refer to STR's storage.  Return DBT.  */
DBT *
svn_fs_base__str_to_dbt(DBT *dbt, const char *str)
{
  svn_fs_base__set_dbt(dbt, str, strlen(str));
  return dbt;
}
