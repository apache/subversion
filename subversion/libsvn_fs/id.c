/* id.c : operations on node and node revision ID's
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

#include <string.h>
#include <stdlib.h>

#include "svn_fs.h"
#include "id.h"
#include "convert-size.h"
#include "validate.h"


/* Finding the length of an ID.  */


int
svn_fs_id_length (const svn_fs_id_t *id)
{
  int len;

  for (len = 0; id[len] != -1; len++)
    ;

  return len;
}


/* Comparing node ID's.  */

int
svn_fs_id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  int i;

  for (i = 0; a[i] == b[i]; i++)
    if (a[i] == -1)
      return 1;

  return 0;
}


int
svn_fs_id_is_ancestor (const svn_fs_id_t *a, const svn_fs_id_t *b)
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
      if (a[i] == -1)
        return 1;

      /* If the node/branch numbers differ, then they're unrelated.
         Examples: 100.20 vs. 101.20; 100.20.2.3 vs. 100.20.3.1;

         This also catches the case where B has ended before A ---
         they're related, but A isn't B's ancestor.
         Example: 100.20.3.2 vs 100.20.  */
      if (a[i] != b[i])
        return 0;

      /* If A's revision number is greater than B's, then A is not B's
         ancestor.  Examples: 100.30 vs 100.20; 2.3.4.5 vs 2.3.4.4.  */
      if (a[i+1] > b[i+1])
        return 0;

      /* If A's revision number is less than B's, then A is an ancestor
         iff its ID ends now.  Examples: 100.30 vs 100.31; 100.30 vs
         100.32.2.4.  */
      if (a[i+1] < b[i+1])
        return (a[i+2] == -1);

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

  for (i = 0; a[prefix + i] != -1; i += 2)
    d += a[prefix + i + 1] - 1;

  return d;
}


int
svn_fs_id_distance (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  int i;

  /* Are they completely unrelated?  */
  if (a[0] != b[0])
    return -1;

  /* Skip any common prefix.  */
  for (i = 0; a[i] == b[i] && a[i+1] == b[i+1] && a[i] != -1; i+=2);

  /* If they're completely identical, then the distance is zero.  */
  if (a[i] == -1 && b[i] == -1)
    return 0;

  /* Are they (branches off) different revisions of the same node?
     Account for the distance between the two revisions.  */
  if (a[i] == b[i])
    return (distance_from_prefix (a, i+2)
            + distance_from_prefix (b, i+2)
            + abs (a[i+1] - b[i+1]));
  else
    /* Or two branches off the same node revision?  */
    return (distance_from_prefix (a, i)
            + distance_from_prefix (b, i));
}


int
svn_fs__is_parent (const svn_fs_id_t *parent,
                   const svn_fs_id_t *child)
{
  int i;

  for (i = 0; parent[i] == child[i]; i++)
    {
      /* If they're completely identical, then CHILD isn't a direct
         child of PARENT.  */
      if (parent[i] == -1)
        return 0;
    }

  /* Is CHILD the next revision of PARENT?  */
  if ((i & 1) == 1
      && child[i] == parent[i] + 1
      && child[i + 1] == -1
      && parent[i + 1] == -1)
    return 1;

  /* Is CHILD the first revision of any branch from PARENT?  */
  if ((i & 1) == 0
      && parent[i] == -1
      && child[i + 1] != -1
      && child[i + 2] == 1)
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
    id = apr_palloc (pool, sizeof (*id) * (id_len + 1));
  else
    {
      id = malloc (sizeof (*id) * (id_len + 1));
      if (! id)
        abort(); /* couldn't malloc */
    }

  {
    int i = 0;
    const char *end = data + data_len;

    for (;;)
      {
        const char *next;
        id[i++] = svn_fs__getsize (data, end - data, &next, 100000000);
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

    id[i] = -1;
  }

  return id;
}


svn_string_t *
svn_fs_unparse_id (const svn_fs_id_t *id,
                   apr_pool_t *pool)
{
  svn_string_t *unparsed = svn_string_ncreate (0, 0, pool);
  int i;

  for (i = 0; id[i] != -1; i++)
    {
      char buf[200];
      int len = svn_fs__putsize (buf, sizeof (buf), id[i]);

      if (len == 0)
        abort ();

      if (id[i + 1] != -1)
        buf[len++] = '.';

      svn_string_appendbytes (unparsed, buf, len);
    }

  return unparsed;
}



/* Copying ID's.  */

svn_fs_id_t *
svn_fs_copy_id (const svn_fs_id_t *id, apr_pool_t *pool)
{
  apr_size_t id_size = (svn_fs_id_length (id) + 1) * sizeof (id[0]);
  svn_fs_id_t *copy = apr_palloc (pool, id_size);
  memcpy (copy, id, id_size);
  
  return copy;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
