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
#include "validate.h"


/* Finding the length of an ID.  */


int
svn_fs__id_length (const svn_fs_id_t *id)
{
  int len;

  for (len = 0; id->digits[len] != -1; len++)
    continue;

  return len;
}


/* Comparing node ID's.  */

int
svn_fs__id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  int i;

  for (i = 0; a->digits[i] == b->digits[i]; i++)
    if (a->digits[i] == -1)
      return 1;

  return 0;
}


int
svn_fs__id_is_ancestor (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  int i = 0;

  for (;;)
    {
      /* Invariant: i is even, and for all j < i, a[j] == b[j].
         Keep in mind: every even-numbered entry in A or B is a
         node/branch number; every odd-numbered entry is a revision
         number.  So i is pointing at a node/branch number.  */

      /* If we've reached the end of A, then either A and B are equal,
         or A is a prefix of B, so A is an ancestor of B.  Examples:
         100.20 vs. 100.20; 100.20 vs. 100.20.3.2  */
      if (a->digits[i] == -1)
        return 1;

      /* If the node/branch numbers differ, then they're unrelated.
         Examples: 100.20 vs. 101.20; 100.20.2.3 vs. 100.20.3.1;

         This also catches the case where B has ended before A ---
         they're related, but A isn't B's ancestor.
         Example: 100.20.3.2 vs 100.20.  */
      if (a->digits[i] != b->digits[i])
        return 0;

      /* If A's revision number is greater than B's, then A is not B's
         ancestor.  Examples: 100.30 vs 100.20; 2.3.4.5 vs 2.3.4.4.  */
      if (a->digits[i+1] > b->digits[i+1])
        return 0;

      /* If A's revision number is less than B's, then A is an ancestor
         iff its ID ends now.  Examples: 100.30 vs 100.31; 100.30 vs
         100.32.2.4.  */
      if (a->digits[i+1] < b->digits[i+1])
        return (a->digits[i+2] == -1);

      /* Otherwise, we've established that the node/branch numbers and
         revision numbers are equal, so go around again.  */
      i += 2;
    }
}


/* Compute the distance from the node revision A to the node revision
   identified by the first PREFIX elements of A.  In other words, this
   is the distance from a node revision to some branch of the node
   revision.  */
static int
distance_from_prefix (const svn_fs_id_t *a, int prefix)
{
  int i;
  int d = 0;

  for (i = 0; a->digits[prefix + i] != -1; i += 2)
    d += a->digits[prefix + i + 1];

  return d;
}


int
svn_fs_id_distance (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  int i;

  /* Are they completely unrelated?  */
  if (a->digits[0] != b->digits[0])
    return -1;

  /* Skip any common prefix.  */
  for (i = 0; (a->digits[i] == b->digits[i] 
               && a->digits[i] != -1 
               && a->digits[i+1] == b->digits[i+1]); i += 2)
    continue;

  /* If they're completely identical, then the distance is zero.  */
  if (a->digits[i] == -1 && b->digits[i] == -1)
    return 0;

  /* Are they (branches off) different revisions of the same node?
     Account for the distance between the two revisions.  */
  if (a->digits[i] == b->digits[i])
    return (distance_from_prefix (a, i+2)
            + distance_from_prefix (b, i+2)
            + abs (a->digits[i+1] - b->digits[i+1]));
  else
    /* Or two branches off the same node revision?  */
    return (distance_from_prefix (a, i)
            + distance_from_prefix (b, i));
}


int
svn_fs__id_is_parent (const svn_fs_id_t *parent,
                      const svn_fs_id_t *child)
{
  int i;

  for (i = 0; parent->digits[i] == child->digits[i]; i++)
    {
      /* If they're completely identical, then CHILD isn't a direct
         child of PARENT.  */
      if (parent->digits[i] == -1)
        return 0;
    }

  /* Is CHILD the next revision of PARENT?  */
  if ((i & 1) == 1
      && child->digits[i] == parent->digits[i] + 1
      && child->digits[i + 1] == -1
      && parent->digits[i + 1] == -1)
    return 1;

  /* Is CHILD the first revision of any branch from PARENT?  */
  if ((i & 1) == 0
      && parent->digits[i] == -1
      && child->digits[i + 1] != -1
      && child->digits[i + 2] == 1
      && child->digits[i + 3] == -1)
    return 1;

  /* Anything else is no good.  */
  return 0;
}



/* Parsing and unparsing node ID's.  */

svn_fs_id_t *
svn_fs_parse_id (const char *data,
                 apr_size_t data_len,
                 apr_pool_t *pool)
{
  svn_fs_id_t *id;
  int id_len;
  
  /* Count the number of components in the ID, and check its syntax.  */
  id_len = svn_fs__count_id_components (data, data_len);
  if (id_len == 0)
      return 0;

  /* Allocate the ID array.  Note that if pool is zero, apr_palloc
     just calls malloc, which meets our promised interface.  */
  if (pool)
    {
      id = apr_palloc (pool, sizeof (*id));
      id->digits = apr_palloc (pool, sizeof (*(id->digits)) * (id_len + 1));
    }
  else
    {
      id = malloc (sizeof (*id));
      if (! id)
        abort(); /* couldn't malloc */
      id->digits = malloc (sizeof (*(id->digits)) * (id_len + 1));
      if (! (id->digits))
        abort(); /* couldn't malloc */
    }

  {
    int i = 0;
    const char *end = data + data_len;

    for (;;)
      {
        const char *next;
        id->digits[i++] = svn_fs__getsize (data, end - data, &next, 100000000);
        if (next == end)
          break;
        if (! next
            || *next != '.')
          {
            if (! pool) free (id);
            return 0;
          }

        data = next + 1;
      }

    id->digits[i] = -1;
  }

  return id;
}


svn_stringbuf_t *
svn_fs_unparse_id (const svn_fs_id_t *id,
                   apr_pool_t *pool)
{
  svn_stringbuf_t *unparsed = svn_stringbuf_ncreate (0, 0, pool);
  int i;

  for (i = 0; id->digits[i] != -1; i++)
    {
      char buf[200];
      int len = svn_fs__putsize (buf, sizeof (buf), id->digits[i]);

      if (len == 0)
        abort ();

      if (id->digits[i + 1] != -1)
        buf[len++] = '.';

      svn_stringbuf_appendbytes (unparsed, buf, len);
    }

  return unparsed;
}



/* Copying ID's.  */

svn_fs_id_t *
svn_fs__id_copy (const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *new_id = apr_palloc (pool, sizeof (*new_id));
  new_id->digits = apr_pmemdup 
    (pool, id->digits, (svn_fs__id_length (id) + 1) * sizeof (id->digits[0]));
  return new_id;
}



/* Predecessor ID's. */

/* ### kff todo: might it be a good thing to abstract out the
   successor logic from svn_fs__new_successor_id() and put it in a
   function here, svn_fs_successor_id(), to match
   svn_fs__id_predecessor()?  Investigate. */

svn_fs_id_t *
svn_fs__id_predecessor (const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *predecessor_id;
  int len;

  len = svn_fs__id_length (id);
  predecessor_id = svn_fs__id_copy (id, pool);

  predecessor_id->digits[len - 1]--;

  if (predecessor_id->digits[len - 1] > 0)
    {
      /* Decrementing the last digit still resulted in a valid node
         revision number, so that must be the predecessor of ID.
         Return the predecessor. */
      return predecessor_id;
    }

  /* Else decrementing the last digit still resulted in a branch
     number, so the predecessor is the node revision on which the
     branch itself is based. */
  if (len > 2)
    predecessor_id->digits[len - 2] = -1;
  else
    predecessor_id = NULL;
  
  return predecessor_id;
}

/* --------------------------------------------------------------------- */

/*** Related-ness checking */

/*  Things to remember:

    - If B is a copy of directory A, B's children are id-related to the
      corresponding children of A.
 
    - Brand new nodes (like, resulting from adds and copies) have the
      first component of their node id > older nodes. 

    Also note:  it is acceptable for this function to call back into
    public FS API interfaces because it does not itself use trails.  */
svn_error_t *
svn_fs_check_related (int *related, 
                      svn_fs_t *fs,
                      const svn_fs_id_t *id1,
                      const svn_fs_id_t *id2,
                      apr_pool_t *pool)
{
  const svn_fs_id_t *older, *younger;
  svn_fs_id_t *tmp_id;

  /* Default answer: not related, until proven otherwise. */
  *related = 0;

  /* Are the two IDs related via node id ancestry? */
  if (svn_fs_id_distance (id1, id2) != -1)
    {
      *related = 1;
      return SVN_NO_ERROR;
    }
  
  /* Figure out which id is youngest. */
  if (id1->digits[0] > id2->digits[0])
    {
      older = id2;
      younger = id1;
    }
  else
    {
      older = id1;
      younger = id2;
    }

  /* Copy YOUNGER so we can possible tweak it later. */
  tmp_id = svn_fs__id_copy (younger, pool);

  /* Now, we loop here from TMP_ID, through each of its predecessors,
     until no predecessors exist, trying to find some relationship to
     the OLDER id. */
  do
    {
      svn_revnum_t rev = SVN_INVALID_REVNUM;
      const char *cp_path = NULL;
      svn_fs_root_t *root;
      svn_stringbuf_t *id_str = svn_fs_unparse_id (tmp_id, pool);
      svn_fs_id_t *copy_id;
      int len = svn_fs__id_length (tmp_id);

      /* See if OLDER is a copy of another node. */
      svn_fs_id_root (&root, fs, pool);
      SVN_ERR (svn_fs_copied_from (&rev, &cp_path, root, id_str->data, pool));
      if (SVN_IS_VALID_REVNUM (rev))
        {
          SVN_ERR (svn_fs_revision_root (&root, fs, rev, pool));
          SVN_ERR (svn_fs_node_id (&copy_id, root, cp_path, pool));
          svn_fs_check_related (related, fs, older, copy_id, pool);
          if (*related)
            return SVN_NO_ERROR;
        }

      /* Hack up TMP_ID so that it represents its own predecessor. */
      tmp_id->digits[len - 1]--;
      if (tmp_id->digits[len - 1] == 0)
        tmp_id->digits[len - 2] = -1;
    }
  while (tmp_id->digits[0] != -1);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
