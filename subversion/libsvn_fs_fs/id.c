/* id.c : operations on node and node revision ID's
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

#include "svn_fs.h"
#include "id.h"
#include "key-gen.h"



/* Creating ID's.  */

svn_fs_id_t *
svn_fs__create_id (const char *node_id,
                   const char *copy_id,
                   const char *txn_id,
                   apr_pool_t *pool)
{
  svn_fs_id_t *id = apr_palloc (pool, sizeof (*id));
  id->node_id = apr_pstrdup (pool, node_id);
  id->copy_id = apr_pstrdup (pool, copy_id);
  id->txn_id = apr_pstrdup (pool, txn_id);
  return id;
}



/* Accessing ID Pieces.  */

const char *
svn_fs__id_node_id (const svn_fs_id_t *id)
{
  return id->node_id;
}

const char *
svn_fs__id_copy_id (const svn_fs_id_t *id)
{
  return id->copy_id;
}

const char *
svn_fs__id_txn_id (const svn_fs_id_t *id)
{
  return id->txn_id;
}

const svn_revnum_t
svn_fs__id_rev (const svn_fs_id_t *id)
{
  return id->rev;
}

const apr_off_t
svn_fs__id_offset (const svn_fs_id_t *id)
{
  return id->offset;
}


/* Copying ID's.  */

svn_fs_id_t *
svn_fs__id_copy (const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *new_id = apr_palloc (pool, sizeof (*new_id));
  new_id->node_id = apr_pstrdup (pool, id->node_id);
  new_id->copy_id = apr_pstrdup (pool, id->copy_id);
  new_id->txn_id = apr_pstrdup (pool, id->txn_id);
  new_id->rev = id->rev;
  new_id->offset = id->offset;
  return new_id;
}



/* Comparing node ID's.  */

svn_boolean_t
svn_fs__id_eq (const svn_fs_id_t *a, 
               const svn_fs_id_t *b)
{
  if (a != b)
    {  
      if ((a->node_id != b->node_id) && (strcmp (a->node_id, b->node_id)))
        return FALSE;
      if ((a->copy_id != b->copy_id) && (strcmp (a->copy_id, b->copy_id)))
        return FALSE;
      if ((a->txn_id != b->txn_id) && (strcmp (a->txn_id, b->txn_id)))
        return FALSE;
      if (a->rev != b->rev)
        return FALSE;
      if (a->offset != b->offset)
        return FALSE;
    }
  return TRUE;
}



/* Parsing and unparsing node ID's.  */

svn_fs_id_t *
svn_fs_parse_id (const char *data,
                 apr_size_t data_len,
                 apr_pool_t *pool)
{
  svn_fs_id_t *id;
  char *data_copy;
  char *str, *last_str;

  /* Dup the ID data into POOL.  Our returned ID will have references
     into this memory. */
  data_copy = apr_pstrmemdup (pool, data, data_len);

  /* Alloc a new svn_fs_id_t structure. */
  id = apr_palloc (pool, sizeof (*id));

  /* Now, we basically just need to "split" this data on `.'
     characters.  We will use apr_strtok, which will essentially put
     NULLs where each of the '.'s used to be.  Then our new id field
     will reference string locations inside our duplicate string.*/

  /* Node Id */
  str = apr_strtok (data_copy, ".", &last_str);
  if (str == NULL) return NULL;
  id->node_id = str;

  /* Copy Id */
  str = apr_strtok (NULL, ".", &last_str);
  if (str == NULL) return NULL;
  id->copy_id = str;
  
  /* Txn Id */
  str = apr_strtok (NULL, ".", &last_str);
  if (str == NULL) return NULL;
  id->txn_id = str;

  /* Revision */
  str = apr_strtok (NULL, ".", &last_str);
  if (str == NULL) return NULL;
  id->rev = atoi (str);

  str = apr_strtok (NULL, ".", &last_str);
  if (str == NULL) return NULL;
  id->offset = apr_atoi64 (str);

  /* Return our ID */
  return id;
}


svn_string_t *
svn_fs_unparse_id (const svn_fs_id_t *id,
                   apr_pool_t *pool)
{
  return svn_string_createf (pool, "%s.%s.%s.%" SVN_REVNUM_T_FMT
                             ".%" APR_SIZE_T_FMT,
                             id->node_id, id->copy_id, id->txn_id,
                             id->rev, id->offset);
}


/* --------------------------------------------------------------------- */

/*** Related-ness checking */

svn_boolean_t
svn_fs_check_related (const svn_fs_id_t *id1,
                      const svn_fs_id_t *id2)
{
  if (id1 == id2)
    return TRUE;
  if (id1->node_id == id2->node_id)
    return TRUE;
  return (strcmp (id1->node_id, id2->node_id) == 0) ? TRUE : FALSE;
}


int 
svn_fs_compare_ids (const svn_fs_id_t *a, 
                    const svn_fs_id_t *b)
{
  if (svn_fs__id_eq (a, b))
    return 0;
  return (svn_fs_check_related (a, b) ? 1 : -1);
}
