/* dir.c --- implementing directories
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */

#include <stdlib.h>
#include <string.h>

#include "svn_fs.h"

#include "fs.h"
#include "node.h"
#include "dir.h"
#include "version.h"
#include "id.h"
#include "skel.h"
#include "proplist.h"


/* Forward declarations for functions.  */

static int build_entries (skel_t *entries_skel,
			  svn_fs_dirent_t ***entries_p,
			  int *num_entries_p,
			  int *entries_size_p,
			  apr_pool_t *pool);


/* Building error objects.  */

static svn_error_t *
corrupt_node_version (svn_fs_t *fs, svn_fs_id_t *id)
{
  svn_string_t *unparsed_id = svn_fs__unparse_id (id, fs->pool);

  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "corrupt node version for node `%s' in filesystem `%s'",
     unparsed_id->data, fs->env_path);
}


static svn_error_t *
path_syntax (svn_fs_t *fs, svn_string_t *path)
{
  /* Sloppy.  What if PATH contains null characters?  */
  return
    svn_error_createf
    (SVN_ERR_FS_PATH_SYNTAX, 0, 0, fs->pool,
     "misformed path `%s' looked up in filesystem `%s'",
     path->data, fs->env_path);
}


static svn_error_t *
path_not_found (svn_fs_t *fs, svn_string_t *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NOT_FOUND, 0, 0, fs->pool,
     "file `%s' not found in filesystem `%s'",
     path->data, fs->env_path);
}


static svn_error_t *
not_a_directory (svn_fs_t *fs, char *path, int len)
{
  char *copy = alloca (len + 1);

  memcpy (copy, path, len);
  copy[len] = '\0';

  return
    svn_error_createf
    (SVN_ERR_FS_NOT_FOUND, 0, 0, fs->pool,
     "path `%s' is not a directory in filesystem `%s'",
     copy, fs->env_path);
}


/* Building directory objects.  */


/* A comparison function we can pass to qsort to sort directory entries.  */
static int
compare_dirents (const void *a, const void *b)
{
  return svn_fs_compare_dirents (* (const svn_fs_dirent_t * const *) a,
				 * (const svn_fs_dirent_t * const *) b);
}


/* Return true iff the LEN bytes at DATA are a valid directory entry
   name.  A valid directory entry name must be non-null, valid UTF-8,
   and contain no slash characters or null characters.  */
static int
is_valid_dirent_name (char *data, apr_size_t len)
{
  /* The empty string is not a valid directory entry name.  */
  if (len == 0)
    return 0;

  /* For friendliness with Unix and POSIX, `.' and `..' are not
     valid directory entry names.  */
  if ((len == 1 && data[0] == '.')
      || (len == 2 && data[0] == '.' && data[1] == '.'))
    return 0;

  /* Scan the string to make sure it's valid UTF-8.  */
  {
    int i;

    for (i = 0; i < len; i++)
      {
	unsigned char head = data[i++];

	/* The way UTF-8 works is this:
	   - If the first byte has the binary form `0xxxxxxx', 
	   that's the whole encoding --- there are no other bytes,
	   and the character's value is `xxxxxxx'.
	   - If the first byte has the binary form `110xxxxx', 
	   then it's followed by another byte of the form `10yyyyyy',
	   and the character's value is `xxxxxyyyyyy'.
	   - If the first byte has the binary form `1110xxxx', then
	   it's followed by two more bytes of the form `10yyyyyy'
	   and `10zzzzzz', and the character's value is
	   `xxxxyyyyyyzzzzzz'.
	   ... and so on.  The first byte indicates how many trailing
	   bytes there are, if any, and each trailing byte contributes
	   six more bits to the value.  */
	if (head > 128)
	  /* Look for the proper number of trailing bytes.  */
	  for (; head & 0x40; head <<= 1)
	    {
	      if (i >= len
		  || (data[i] & 0xc0) != 0x80)
		return 0;
	      i++;
	    }
      }
  }

  /* Because of UTF-8's nice characteristics, we know that ASCII
     characters cannot occur as part of any other character's
     encoding, so this test is sufficient.  */
  if (memchr (data, '/', len)
      || memchr (data, '\0', len))
    return 0;

  return 1;
}


/* Produce an array of directory entries, given a list of ENTRY skels.

   Parse ENTRIES_SKEL as a list of directory ENTRY skels; set *ENTRIES
   to a null-terminated array of pointers to directory entry
   structures, set *NUM_ENTRIES_P to the number of entries in the
   array, and set *ENTRIES_SIZE_P to the number of entries allocated
   to the array.  Return non-zero on success, or zero if ENTRIES_SKEL
   is not a well-formed entries list.

   Do all allocation in POOL.  */
static int
build_entries (skel_t *entries_skel,
	       svn_fs_dirent_t ***entries_p,
	       int *num_entries_p,
	       int *entries_size_p,
	       apr_pool_t *pool)
{
  /* How many entries are there in the list?  */
  int num_entries = svn_fs__list_length (entries_skel);
  svn_fs_dirent_t **entries;

  if (num_entries < 0)
    return 0;

  entries = NEWARRAY (pool, svn_fs_dirent_t *, num_entries + 1);

  /* Walk the skel and build the individual directory entries.  */
  {
    skel_t *entry;
    int i;

    for (i = 0, entry = entries_skel->children;
	 entry;
	 entry = entry->next, i++)
      {
	svn_fs_id_t *id;
	skel_t *name_skel, *id_skel;

	if (svn_fs__list_length (entry) != 2
	    || ! entry->children->is_atom
	    || ! entry->children->next->is_atom)
	  return 0;

	name_skel = entry->children;
	id_skel = entry->children->next;

	id = svn_fs__parse_id (id_skel->data, id_skel->len, 0, pool);
	if (! id)
	  return 0;

	/* Check for invalid names.  */
	if (! is_valid_dirent_name (name_skel->data, name_skel->len))
	  return 0;

	/* Build a directory entry for this.  */
	entries[i] = NEW (pool, svn_fs_dirent_t);
	entries[i]->name = svn_string_ncreate (name_skel->data,
					       name_skel->len,
					       pool);
	entries[i]->id = id;
      }

    entries[i] = 0;
    
    if (i != num_entries)
      abort ();
  }

  /* Sort the list.  */
  qsort (entries, num_entries, sizeof (entries[0]), compare_dirents);

  /* Check the list for duplicate names.  */
  if (num_entries > 1)
    {
      int i;

      for (i = 1; i < num_entries; i++)
	{
	  if (svn_string_compare (entries[i - 1]->name, entries[i]->name))
	    return 0;
	}
    }

  *entries_p = entries;
  *num_entries_p = num_entries;
  *entries_size_p = num_entries + 1;
  return 1;
}


svn_error_t *
svn_fs__dir_from_skel (svn_fs_node_t **node,
		       svn_fs_t *fs, 
		       svn_fs_id_t *id,
		       skel_t *nv, 
		       apr_pool_t *skel_pool)
{
  svn_fs_dir_t *dir = 0;

  /* Do a quick check of the syntax of the skel, before we do any more
     work.  */
  if (svn_fs__list_length (nv) != 3
      || ! nv->children->next->is_atom
      || nv->children->next->next->is_atom)
    goto corrupt;

  /* Allocate the node itself.  */
  dir = ((svn_fs_dir_t *)
	 svn_fs__init_node (sizeof (*dir), fs, id, kind_dir));

  /* Try to parse the dir's property list.  */
  dir->node.proplist = svn_fs__make_proplist (nv->children->next,
					      dir->node.pool);
  if (! dir->node.proplist)
    goto corrupt;

  /* Parse the dir's contents.  */
  if (! build_entries (nv->children->next->next,
		       &dir->entries, &dir->num_entries, &dir->entries_size,
		       dir->node.pool))
    goto corrupt;

  /* ANSI C guarantees that, if the first element of `struct S' has
     type `T', then casting a `struct S *' to `T *' and a pointer to
     S's first element are equivalent.  */
  *node = &dir->node;
  return 0;

 corrupt:
  if (dir)
    apr_destroy_pool (dir->node.pool);
  return corrupt_node_version (fs, id);
}



/* Casting, typing, and other trivial bookkeeping operations on dirs.  */

svn_fs_dir_t *
svn_fs_node_to_dir (svn_fs_node_t *node)
{
  if (node->kind != kind_dir)
    return 0;
  else
    return (svn_fs_dir_t *) node;
}


svn_fs_node_t *
svn_fs_dir_to_node (svn_fs_dir_t *dir)
{
  return &dir->node;
}


void
svn_fs_close_dir (svn_fs_dir_t *dir)
{
  svn_fs_close_node (svn_fs_dir_to_node (dir));
}



/* Accessing directory contents.  */

svn_error_t *
svn_fs_dir_entries (svn_fs_dirent_t ***entries,
		    svn_fs_dir_t *dir)
{
  *entries = dir->entries;

  return 0;
}


svn_error_t *
svn_fs_open_root (svn_fs_dir_t **dir,
		  svn_fs_t *fs,
		  svn_vernum_t v)
{
  svn_fs_id_t *id;
  svn_fs_node_t *root;
  apr_pool_t *pool = svn_pool_create (fs->pool);

  SVN_ERR (svn_fs__version_root (&id, fs, v, pool));
  SVN_ERR (svn_fs__open_node_by_id (&root, fs, id));
  if (! svn_fs_node_is_dir (root))
    {
      apr_destroy_pool (pool);
      return
	svn_error_createf
	(SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
	 "the root of version %ld in filesystem `%s' is not a directory",
	 v, fs->env_path);
    }

  *dir = svn_fs_node_to_dir (root);
  apr_destroy_pool (pool);
  return 0;
}


svn_error_t *
svn_fs_open_node (svn_fs_node_t **child,
		  svn_fs_dir_t *parent_dir,
		  svn_string_t *name)
{
  svn_error_t *svn_err;
  svn_fs_t *fs = parent_dir->node.fs;
  svn_fs_dir_t *dir;
  svn_fs_node_t *node;
  char *name_end = name->data + name->len;
  char *scan;

  /* NAME must not be empty.  Also, the Subversion filesystem
     interface doesn't support absolute paths; to avoid
     misunderstandings, treat them as errors.  */
  if (name->len <= 0
      || name->data[0] == '/')
    return path_syntax (fs, name);

  dir = parent_dir;
  scan = name->data;

  /* Pretend we re-opened the top directory ourselves.  As we walk
     down the directory tree, we close each directory object after
     we've traversed it, but we don't want to close PARENT_DIR ---
     that's the caller's object.  Bumping the open count has the same
     effect as re-opening the directory ourselves, so we have the
     right to close it --- once.

     Perhaps it would be a good idea to have a "reopen" call in the
     public interface, and just use that.  */
  dir->node.open_count++;

  /* Walk down from PARENT_DIR to the desired node, traversing NAME one path
     component at a time.  */
  for (;;)
    {
      char *start;
      svn_fs_dirent_t **entry;

      /* At this point, we know scan points to something that isn't a
	 slash, and that there's at least one character there ---
	 there's a path component.  Scan for its end.  */
      start = scan;
      scan = memchr (start, '/', name_end - start);

      /* If we hit the end of the string, then everything to the end
	 is the next component.  */
      if (! scan)
	scan = name_end;

      /* Now everything from start to scan is a filename component.  */
	
      /* Yes, but is it a *valid* filename component?  */
      if (! is_valid_dirent_name (start, scan - start))
	{
	  svn_fs_close_dir (dir);
	  return path_syntax (fs, name);
	}

      /* Try to find a matching entry in dir.  */
      for (entry = dir->entries; *entry; entry++)
	if ((*entry)->name->len == scan - start
	    && ! memcmp ((*entry)->name->data, start, scan - start))
	  break;

      /* If we didn't find a matching entry, then return an error.  */
      if (! *entry)
	{
	  svn_fs_close_dir (dir);
	  return path_not_found (fs, name);
	}

      /* Try to open that node.  */
      svn_err = svn_fs__open_node_by_id (&node, fs, (*entry)->id);
      if (svn_err)
	{
	  svn_fs_close_dir (dir);
	  return svn_err;
	}

      /* Close the parent directory.  */ 
      svn_fs_close_dir (dir);

      /* Are we done with the name?  */
      if (scan >= name_end)
	break;

      /* The new node is now our current directory...  */
      dir = svn_fs_node_to_dir (node);

      /* ... so it had better actually be a directory.  */
      if (! dir)
	{
	  svn_fs_close_node (node);
	  return not_a_directory (fs, name->data, scan - name->data);
	}

      /* Skip however many slashes we're looking at.  */
      while (scan < name_end && *scan == '/')
	scan++;

      /* Slashes are permitted at the end of the name.  */
      if (scan >= name_end)
	break;
    }

  *child = node;
  return 0;
}



/* The directory entry sort order.  */

int
svn_fs_compare_dirents (const svn_fs_dirent_t *a,
			const svn_fs_dirent_t *b)
{
  if (a == b)
    return 0;
  else if (! a)
    return 1;
  else if (! b)
    return -1;
  else
    {
      int cmp = memcmp (a->name->data, b->name->data,
			(a->name->len > b->name->len
			 ? b->name->len
			 : a->name->len));
      if (cmp)
	return cmp;
      else
	return a->name->len - b->name->len;
    }
}
