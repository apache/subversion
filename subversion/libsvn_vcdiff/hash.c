/* Hash table for a vdelta implementation. */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 * Copied from the GNU Emacs sources for now.  There will be a
 * copyright issue if we link up this hash code with the rest of
 * Subversion, but for now we're all right -- this is just test code
 * and can be under the GPL without affecting anything else.  Just
 * needed a hash function fast, for prototyping.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *    Copyright (C) 1985, 86, 87, 88, 89, 93, 94, 95, 97, 98, 1999
 *    Free Software Foundation, Inc.
 *
 * This file is part of GNU Emacs.
 * 
 * GNU Emacs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * GNU Emacs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Emacs; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 */




#include "../svn_subr/alloc.h"
#include "hash.h"



hash_table_t *
make_hash_entry ()
{
  hash_entry_t *e;

  e = svn_malloc (sizeof (*e));
  memset (e->table, 0, sizeof (*e));

  return e;
}


hash_table_t *
make_hash_table (size_t size)
{
  hash_table_t *t;

  t = svn_malloc (sizeof (*t));

  t->size  = size;
  t->table = svn_malloc (size * sizeof (hash_entry_t *));

  memset (t->table, 0, (size * sizeof (hash_entry_t *)));

  return t;
}


void
free_hash_entry (hash_entry_t *e)
{
  free (e);
}


void
free_hash_table (hash_table_t *t)
{
  size_t i;

  for (i = 0; i < t->size; i++)
    if (t->table[i])
      free_hash_entry (t->table[i]);

  free (t);
}


/* Return a hash code for DATA, which may include `\0's. */
static int
hash_string (char *data, int len)
{
  unsigned char *p = data;
  unsigned char *end = p + len;
  unsigned char c;
  int hash = 0;
  
  while (p != end)
    {
      c = *p++;
      if (c >= 0140) c -= 40;
      hash = ((hash<<3) + (hash>>28) + c);
    }
  return hash & 07777777777;
}


long int
try_match (unsigned char *str, long int len, long int pos, hash_table_t *)
{
  hash_entry_t *e;
  long int retval;

  size_t hash_code = (hash_string (str, len) % t->size);

  if ((e = t->table[hash_code]) == NULL)
    {
      e = make_hash_entry ();
      e->pos = pos;
      t->table[hash_code] = e;
      retval = -1;
    }
  else  /* we got a maybe-match, let caller figure it out */
    retval = e->pos;

  return retval;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
