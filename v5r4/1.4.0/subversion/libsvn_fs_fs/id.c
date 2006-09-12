/* id.c : operations on node-revision IDs
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

#include <string.h>
#include <stdlib.h>

#include "id.h"
#include "../libsvn_fs/fs-loader.h"



typedef struct {
  const char *node_id;
  const char *copy_id;
  const char *txn_id;
  svn_revnum_t rev;
  apr_off_t offset;
} id_private_t;


/* Accessing ID Pieces.  */

const char *
svn_fs_fs__id_node_id(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->node_id;
}


const char *
svn_fs_fs__id_copy_id(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->copy_id;
}


const char *
svn_fs_fs__id_txn_id(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->txn_id;
}


svn_revnum_t
svn_fs_fs__id_rev(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->rev;
}


apr_off_t
svn_fs_fs__id_offset(const svn_fs_id_t *id)
{
  id_private_t *pvt = id->fsap_data;

  return pvt->offset;
}


svn_string_t *
svn_fs_fs__id_unparse(const svn_fs_id_t *id,
                      apr_pool_t *pool)
{
  const char *txn_rev_id;
  id_private_t *pvt = id->fsap_data;

  if ((! pvt->txn_id))
    {
      txn_rev_id = apr_psprintf(pool, "%ld/%"
                                APR_OFF_T_FMT, pvt->rev, pvt->offset);
    }
  else
    {
      txn_rev_id = pvt->txn_id;
    }
  return svn_string_createf(pool, "%s.%s.%c%s",
                            pvt->node_id, pvt->copy_id,
                            (pvt->txn_id ? 't' : 'r'),
                            txn_rev_id);
}


/*** Comparing node IDs ***/

svn_boolean_t
svn_fs_fs__id_eq(const svn_fs_id_t *a,
                 const svn_fs_id_t *b)
{
  id_private_t *pvta = a->fsap_data, *pvtb = b->fsap_data;

  if (a == b)
    return TRUE;
  if (strcmp(pvta->node_id, pvtb->node_id) != 0)
     return FALSE;
  if (strcmp(pvta->copy_id, pvtb->copy_id) != 0)
    return FALSE;
  if ((pvta->txn_id == NULL) != (pvtb->txn_id == NULL))
    return FALSE;
  if (pvta->txn_id && pvtb->txn_id && strcmp(pvta->txn_id, pvtb->txn_id) != 0)
    return FALSE;
  if (pvta->rev != pvtb->rev)
    return FALSE;
  if (pvta->offset != pvtb->offset)
    return FALSE;
  return TRUE;
}


svn_boolean_t
svn_fs_fs__id_check_related(const svn_fs_id_t *a,
                            const svn_fs_id_t *b)
{
  id_private_t *pvta = a->fsap_data, *pvtb = b->fsap_data;

  if (a == b)
    return TRUE;
  /* If both node_ids start with _ and they have differing transaction
     IDs, then it is impossible for them to be related. */
  if (pvta->node_id[0] == '_')
    {
      if (pvta->txn_id && pvtb->txn_id &&
          (strcmp(pvta->txn_id, pvtb->txn_id) != 0))
        return FALSE;
    }

  return (strcmp(pvta->node_id, pvtb->node_id) == 0) ? TRUE : FALSE;
}


int
svn_fs_fs__id_compare(const svn_fs_id_t *a,
                      const svn_fs_id_t *b)
{
  if (svn_fs_fs__id_eq(a, b))
    return 0;
  return (svn_fs_fs__id_check_related(a, b) ? 1 : -1);
}



/* Creating ID's.  */

static id_vtable_t id_vtable = {
  svn_fs_fs__id_unparse,
  svn_fs_fs__id_compare
};


svn_fs_id_t *
svn_fs_fs__id_txn_create(const char *node_id,
                         const char *copy_id,
                         const char *txn_id,
                         apr_pool_t *pool)
{
  svn_fs_id_t *id = apr_palloc(pool, sizeof(*id));
  id_private_t *pvt = apr_palloc(pool, sizeof(*pvt));

  pvt->node_id = apr_pstrdup(pool, node_id);
  pvt->copy_id = apr_pstrdup(pool, copy_id);
  pvt->txn_id = apr_pstrdup(pool, txn_id);
  pvt->rev = SVN_INVALID_REVNUM;
  pvt->offset = -1;
  id->vtable = &id_vtable;
  id->fsap_data = pvt;
  return id;
}


svn_fs_id_t *
svn_fs_fs__id_rev_create(const char *node_id,
                         const char *copy_id,
                         svn_revnum_t rev,
                         apr_off_t offset,
                         apr_pool_t *pool)
{
  svn_fs_id_t *id = apr_palloc(pool, sizeof(*id));
  id_private_t *pvt = apr_palloc(pool, sizeof(*pvt));

  pvt->node_id = apr_pstrdup(pool, node_id);
  pvt->copy_id = apr_pstrdup(pool, copy_id);
  pvt->txn_id = NULL;
  pvt->rev = rev;
  pvt->offset = offset;
  id->vtable = &id_vtable;
  id->fsap_data = pvt;
  return id;
}


svn_fs_id_t *
svn_fs_fs__id_copy(const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *new_id = apr_palloc(pool, sizeof(*new_id));
  id_private_t *new_pvt = apr_palloc(pool, sizeof(*new_pvt));
  id_private_t *pvt = id->fsap_data;

  new_pvt->node_id = apr_pstrdup(pool, pvt->node_id);
  new_pvt->copy_id = apr_pstrdup(pool, pvt->copy_id);
  new_pvt->txn_id = pvt->txn_id ? apr_pstrdup(pool, pvt->txn_id) : NULL;
  new_pvt->rev = pvt->rev;
  new_pvt->offset = pvt->offset;
  new_id->vtable = &id_vtable;
  new_id->fsap_data = new_pvt;
  return new_id;
}


svn_fs_id_t *
svn_fs_fs__id_parse(const char *data,
                    apr_size_t len,
                    apr_pool_t *pool)
{
  svn_fs_id_t *id;
  id_private_t *pvt;
  char *data_copy, *str, *last_str;

  /* Dup the ID data into POOL.  Our returned ID will have references
     into this memory. */
  data_copy = apr_pstrmemdup(pool, data, len);

  /* Alloc a new svn_fs_id_t structure. */
  id = apr_palloc(pool, sizeof(*id));
  pvt = apr_palloc(pool, sizeof(*pvt));
  id->vtable = &id_vtable;
  id->fsap_data = pvt;

  /* Now, we basically just need to "split" this data on `.'
     characters.  We will use apr_strtok, which will put terminators
     where each of the '.'s used to be.  Then our new id field will
     reference string locations inside our duplicate string.*/

  /* Node Id */
  str = apr_strtok(data_copy, ".", &last_str);
  if (str == NULL)
    return NULL;
  pvt->node_id = str;

  /* Copy Id */
  str = apr_strtok(NULL, ".", &last_str);
  if (str == NULL)
    return NULL;
  pvt->copy_id = str;

  /* Txn/Rev Id */
  str = apr_strtok(NULL, ".", &last_str);
  if (str == NULL)
    return NULL;

  if (str[0] == 'r')
    {
      /* This is a revision type ID */
      pvt->txn_id = NULL;

      str = apr_strtok(str + 1, "/", &last_str);
      if (str == NULL)
        return NULL;
      pvt->rev = SVN_STR_TO_REV(str);

      str = apr_strtok(NULL, "/", &last_str);
      if (str == NULL)
        return NULL;
      pvt->offset = apr_atoi64(str);
    }
  else if (str[0] == 't')
    {
      /* This is a transaction type ID */
      pvt->txn_id = str + 1;
      pvt->rev = SVN_INVALID_REVNUM;
      pvt->offset = -1;
    }
  else
    return NULL;

  return id;
}
