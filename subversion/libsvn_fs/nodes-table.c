/* nodes-table.c : working with the `nodes' table
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "db.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "dbt.h"
#include "skel.h"
#include "nodes-table.h"



/* Opening/creating the `nodes' table.  */


/* Compare two node ID's, according to the rules in `structure'.  */
static int
compare_ids (svn_fs_id_t *a, svn_fs_id_t *b)
{
  int i = 0;

  while (a[i] == b[i])
    {
      if (a[i] == -1)
	return 0;
      i++;
    }

  /* Different nodes, or different branches, are ordered by their
     node / branch numbers.  */
  if ((i & 1) == 0)
    return a[i] - b[i];

  /* This function is only prepared to handle node revision ID's.  */
  if (a[i] == -1 || b[i] == -1)
    abort ();

  /* Different revisions of the same node are ordered by revision number.  */
  if (a[i + 1] == -1 && b[i + 1] == -1)
    return a[i] - b[i];

  /* A branch off of any revision of a node comes after all revisions
     of that node.  */
  if (a[i + 1] == -1)
    return -1;
  if (b[i + 1] == -1)
    return 1;

  /* Branches are ordered by increasing revision number.  */
  return a[i] - b[i];
}


/* Parse a node revision ID from D.
   Return zero if D does not contain a well-formed node revision ID.  */
static svn_fs_id_t *
parse_node_revision_dbt (const DBT *d)
{
  svn_fs_id_t *id = svn_fs_parse_id (d->data, d->size, 0);

  if (! id)
    return 0;

  /* It must be a node revision ID, not a node ID.  */
  if (svn_fs_id_length (id) & 1)
    {
      free (id);
      return 0;
    }

  return id;
}


/* The key comparison function for the `nodes' table.

   Strictly speaking, this function only needs to handle strings that
   we actually use as keys in the table.  However, if we happen to
   insert garbage keys, and this comparison function doesn't do
   something consistent with them (i.e., something transitive and
   reflexive), we can actually corrupt the btree structure.  Which
   seems unfriendly.

   So this function tries to act as a proper comparison for any two
   arbitrary byte strings.  Two well-formed node revisions ID's compare
   according to the rules described in the `structure' file; any
   malformed key comes before any well-formed key; and two malformed
   keys come in byte-by-byte order.  */
static int
compare_nodes_keys (const DBT *ak, const DBT *bk)
{
  svn_fs_id_t *a = parse_node_revision_dbt (ak);
  svn_fs_id_t *b = parse_node_revision_dbt (bk);
  int result;

  /* Two well-formed keys are compared by the rules in `structure'.  */
  if (a && b)
    result = compare_ids (a, b);

  /* Malformed keys come before well-formed keys.  */
  else if (a)
    result = 1;
  else if (b)
    result = -1;

  /* Two malformed keys are compared byte-by-byte.  */
  else
    result = svn_fs__compare_dbt (ak, bk);

  if (a) free (a);
  if (b) free (b);

  return result;
}


int
svn_fs__open_nodes_table (DB **nodes_p,
			  DB_ENV *env,
			  int create)
{
  DB *nodes;

  DB_ERR (db_create (&nodes, env, 0));
  DB_ERR (nodes->set_bt_compare (nodes, compare_nodes_keys));
  DB_ERR (nodes->open (nodes, "nodes", 0, DB_BTREE,
		       create ? (DB_CREATE | DB_EXCL) : 0,
		       0666));

  *nodes_p = nodes;
  return 0;
}



/* Validating REPRESENTATION skels.  */


static int
is_valid_proplist (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len >= 0
      && (len & 1) == 0)
    {
      skel_t *elt;

      for (elt = skel->children; elt; elt = elt->next)
	if (! elt->is_atom)
	  return 0;

      return 1;
    }

  return 0;
}


static int
is_valid_flag (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len == 2
      && svn_fs__is_atom (skel->children, "mutable")
      && skel->children->next->is_atom)
    return 1;

  return 0;
}

static int
is_valid_header (skel_t *skel, skel_t **kind_p)
{
  int len = svn_fs__list_length (skel);

  if (len >= 2)
    {
      if (skel->children->is_atom
	  && is_valid_proplist (skel->children->next))
	{
	  skel_t *flag;

	  for (flag = skel->children->next->next;
	       flag;
	       flag = flag->next)
	    if (! is_valid_flag (flag))
	      return 0;

	  *kind_p = skel->children;
	  return 1;
	}
    }

  return 0;
}


static int
is_valid_node_revision (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len >= 1)
    {
      skel_t *header = skel->children;
      skel_t *kind;

      if (is_valid_header (header, &kind))
	{
	  if (svn_fs__is_atom (kind, "file")
	      && len == 2
	      && header->next->is_atom)
	    return 1;
	  else if (svn_fs__is_atom (kind, "dir")
		   && len == 2)
	    {
	      skel_t *entry_list = header->next;

	      if (! entry_list->is_atom)
		{
		  skel_t *entry;

		  for (entry = entry_list->children; 
		       entry;
		       entry = entry->next)
		    {
		      int entry_len = svn_fs__list_length (entry);

		      if ((entry_len == 2 || entry_len == 3)
			  && entry->children->is_atom
			  && entry->children->next->is_atom
			  && (! entry->children->next->next
			      || entry->children->next->next->is_atom))
			;
		      else
			return 0;
		    }
		  
		  return 1;
		}
	    }
	}
    }

  return 0;
}


static int
is_valid_representation (skel_t *skel)
{
  int len = svn_fs__list_length (skel);

  if (len >= 1)
    {
      skel_t *type = skel->children;

      if (svn_fs__is_atom (type, "fulltext")
	  && len == 2
	  && is_valid_node_revision (type->next))
	return 1;
#if 0
      else if (svn_fs__is_atom (type, "younger")
	       && len == 3
	       && is_valid_delta (type->next)
	       && is_valid_checksum (type->next->next))
	return 1;
#endif
    }

  return 0;
}



/* Storing and retrieving representations.  */


svn_error_t *
svn_fs__get_rep (skel_t **skel_p,
		 svn_fs_t *fs,
		 const svn_fs_id_t *id,
		 DB_TXN *db_txn,
		 apr_pool_t *pool)
{
  int db_err;
  DBT key, value;
  skel_t *skel;

  db_err = fs->nodes->get (fs->nodes, db_txn,
			   svn_fs__id_to_dbt (&key, id, pool),
			   svn_fs__result_dbt (&value),
			   0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_fs__err_dangling_id (fs, id);

  /* Handle any other error conditions.  */
  SVN_ERR (DB_WRAP (fs, "reading node representation", db_err));

  /* Make sure the skel's contents get freed when POOL is destroyed.  */
  svn_fs__track_dbt (&value, pool);

  /* Parse and check the REPRESENTATION skel.  */
  skel = svn_fs__parse_skel (value.data, value.size, pool);
  if (! skel
      || ! is_valid_representation (skel))
    return svn_fs__err_corrupt_representation (fs, id);

  *skel_p = skel;
  return 0;
}


svn_error_t *
svn_fs__put_rep (svn_fs_t *fs,
		 const svn_fs_id_t *id, 
		 skel_t *skel,
		 DB_TXN *db_txn,
		 apr_pool_t *pool)
{
  DBT key, value;

  if (! is_valid_representation (skel))
    return svn_fs__err_corrupt_representation (fs, id);

  SVN_ERR (DB_WRAP (fs, "storing node representation",
		    fs->nodes->put (fs->nodes, db_txn,
				    svn_fs__id_to_dbt (&key, id, pool),
				    svn_fs__skel_to_dbt (&value, skel, pool),
				    0)));

  return 0;
}
