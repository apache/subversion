/* id.c : operations on node and node revision ID's
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



/* Copying ID's.  */

svn_fs_id_t *
svn_fs__id_copy (const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *new_id = apr_palloc (pool, sizeof (*new_id));
  new_id->node_id = apr_pstrdup (pool, id->node_id);
  new_id->copy_id = apr_pstrdup (pool, id->copy_id);
  new_id->txn_id = apr_pstrdup (pool, id->txn_id);
  return new_id;
}



/* Comparing node ID's.  */

int
svn_fs__id_eq (const svn_fs_id_t *a, 
               const svn_fs_id_t *b)
{
  if (a != b)
    {  
      if ((a->node_id != b->node_id) && (strcmp (a->node_id, b->node_id)))
        return 0;
      if ((a->copy_id != b->copy_id) && (strcmp (a->copy_id, b->copy_id)))
        return 0;
      if ((a->txn_id != b->txn_id) && (strcmp (a->txn_id, b->txn_id)))
        return 0;
    }
  return 1;
}



/* Parsing and unparsing node ID's.  */

svn_fs_id_t *
svn_fs_parse_id (const char *data,
                 apr_size_t data_len,
                 apr_pool_t *pool)
{
  svn_fs_id_t *id;
  char *data_copy;
  char *dot;

  /* Dup the ID data into POOL, if we have one, or just malloc a copy
     it otherwise. */
  if (pool)
    {
      data_copy = apr_pstrmemdup (pool, data, data_len);
    }
  else
    {
      data_copy = malloc (sizeof (*data_copy) * data_len + 1);
      if (! data_copy)
        abort (); /* couldn't malloc */
      memcpy (data_copy, data, data_len);
      data_copy[data_len] = 0;
    }
  
  /* Alloc a new svn_fs_id_t structure. */
  if (pool)
    {
      id = apr_palloc (pool, sizeof (*id));
    }
  else
    {
      id = malloc (sizeof (*id));
      if (! id)
        abort (); /* couldn't malloc */
    }

  /* Now, we basically just need to "split" this data on `.'
     characters.  There should be exactly three pieces (around two
     `.'s) as a result.  To do this, we'll just replace the `.'s with
     NULL terminators, and do fun pointer-y things.  */

  /* Node Id */
  id->node_id = data_copy;
  dot = strchr (id->node_id, '.');
  if ((! dot) || (dot <= id->node_id))
    goto cleanup;
  *dot = 0;

  /* Copy Id */
  id->copy_id = dot + 1;
  dot = strchr (id->copy_id, '.');
  if ((! dot) || (dot <= id->copy_id))
    goto cleanup;
  *dot = 0;
  
  /* Txn Id */
  id->txn_id = dot + 1;
  dot = strchr (id->copy_id, '.');
  if (dot)
    goto cleanup;

  /* Return our ID */
  return id;

 cleanup:

  /* Don't bother cleaning up if we have a POOL ... this will happen
     when someone else clears/destroys the pool. */
  if (! pool)
    {
      if (id)
        free (id);
      if (data_copy)
        free (data_copy);
    }
  return NULL;
}


svn_string_t *
svn_fs_unparse_id (const svn_fs_id_t *id,
                   apr_pool_t *pool)
{
  return svn_string_createf (pool, "%s.%s.%s", 
                             id->node_id, id->copy_id, id->txn_id);
}


/* --------------------------------------------------------------------- */

/*** Related-ness checking */

int
svn_fs_check_related (const svn_fs_id_t *id1,
                      const svn_fs_id_t *id2)
{
  if (id1 == id2)
    return 1;
  if (id1->node_id == id2->node_id)
    return 1;
  return (! strcmp (id1->node_id, id2->node_id));
}


int 
svn_fs_compare_ids (const svn_fs_id_t *a, 
                    const svn_fs_id_t *b)
{
  if (svn_fs__id_eq (a, b))
    return 0;
  return (svn_fs_check_related (a, b) ? 1 : -1);
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
