/* dir.c : operations on directories
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

#include "string.h"

#include "svn_fs.h"
#include "fs.h"
#include "node.h"
#include "dir.h"
#include "revision.h"
#include "err.h"
#include "skel.h"


/* Building error objects.  */

static svn_error_t *
path_syntax (svn_fs_t *fs, svn_string_t *path)
{
  /* Sloppy.  What if PATH contains null characters?  */
  return
    svn_error_createf
    (SVN_ERR_FS_PATH_SYNTAX, 0, 0, fs->pool,
     "malformed path: `%s'",
     path->data);
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
corrupt_node_revision (svn_fs_node_t *node)
{
  svn_fs_t *fs = svn_fs__node_fs (node);
  svn_fs_id_t *id = svn_fs__node_id (node);
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);

  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "corrupt node revision for node `%s' in filesystem `%s'",
     unparsed_id->data, fs->env_path);
}


static svn_error_t *
node_not_mutable (svn_fs_node_t *node)
{
  svn_fs_t *fs = svn_fs__node_fs (node);
  svn_fs_id_t *id = svn_fs__node_id (node);
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);

  return
    svn_error_createf
    (SVN_ERR_FS_NOT_MUTABLE, 0, 0, fs->pool,
     "attempt to change immutable node `%s' in filesystem `%s'",
     unparsed_id->data, fs->env_path);
}


static svn_error_t *
not_a_directory (svn_fs_t *fs, char *path, int len)
{
  char *copy = alloca (len + 1);

  memcpy (copy, path, len);
  copy[len] = '\0';

  return
    svn_error_createf
    (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, fs->pool,
     "path `%s' is not a directory in filesystem `%s'",
     copy, fs->env_path);
}


/* Finding a revision's root directory.  */

svn_error_t *
svn_fs_open_root (svn_fs_dir_t **dir_p,
		  svn_fs_t *fs,
		  svn_revnum_t v)
{
  svn_fs_id_t *root_id;
  svn_fs_node_t *root_node;

  SVN_ERR (svn_fs__check_fs (fs));
  SVN_ERR (svn_fs__revision_root (&root_id, fs, v, fs->pool));
  SVN_ERR (svn_fs__open_node_by_id (&root_node, fs, root_id, 0));
  if (! svn_fs_node_is_dir (root_node))
    {
      svn_fs_close_node (root_node);
      return
	svn_error_createf
	(SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
	 "the root of revision %ld in filesystem `%s' is not a directory",
	 v, fs->env_path);
    }

  *dir_p = svn_fs_node_to_dir (root_node);
  return 0;
}



/* Opening nodes by name.  */


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


static svn_error_t *
search (svn_fs_id_t **id_p,
	svn_fs_dir_t *dir,
	char *name,
	apr_size_t name_len,
	DB_TXN *db_txn,
	apr_pool_t *pool)
{
  svn_fs_node_t *dir_node = svn_fs_dir_to_node (dir);
  skel_t *dir_skel, *entry_list, *entry;

  /* Read the contents of DIR.  */
  SVN_ERR (svn_fs__get_node_revision (&dir_skel, dir_node, db_txn, pool));

  entry_list = dir_skel->children->next;
  if (! entry_list || entry_list->is_atom)
    return corrupt_node_revision (dir_node);

  for (entry = entry_list->children; entry; entry = entry->next)
    {
      skel_t *entry_name, *entry_id;
      if (svn_fs__list_length (entry) != 2
	  || ! entry->children->is_atom
	  || ! entry->children->next->is_atom)
	return corrupt_node_revision (dir_node);

      entry_name = entry->children;
      entry_id = entry->children->next;

      if (entry_name->len == name_len
	  && ! memcmp (entry_name->data, name, name_len))
	{
	  svn_fs_id_t *id = svn_fs_parse_id (entry_id->data,
					     entry_id->len,
					     pool);
	  if (! id) 
	    return corrupt_node_revision (dir_node);

	  *id_p = id;
	  return 0;
	}
    }

  *id_p = 0;
  return 0;
}


svn_error_t *
svn_fs_open_node (svn_fs_node_t **child_p,
		  svn_fs_dir_t *parent_dir,
		  svn_string_t *name,
		  apr_pool_t *pool)
{
  svn_fs_t *fs = svn_fs__node_fs (svn_fs_dir_to_node (parent_dir));
  svn_fs_dir_t *dir;
  svn_fs_node_t *node;
  char *scan, *name_end;

  /* NAME must not be empty.  Also, the Subversion filesystem
     interface doesn't support absolute paths; to avoid
     misunderstandings, treat them as errors.  */
  if (name->len <= 0
      || name->data[0] == '/')
    return path_syntax (fs, name);

  /* Get our own `open' of PARENT_NODE, so we can close it without 
     affecting the caller.  */
  dir = (svn_fs_node_to_dir
	 (svn_fs__reopen_node
	  (svn_fs_dir_to_node (parent_dir))));

  scan = name->data;
  name_end = name->data + name->len;
  
  /* Walk down PARENT_DIR to the desired node, traversing NAME one
     path component at a time.  */
  for (;;)
    {
      svn_error_t *svn_err;
      svn_fs_id_t *entry_id;
      char *start = scan;

      /* At this point, we know scan points to at least one character,
         and that character is not a slash --- so it's a valid path
         component.  Scan for its end.  */
      scan = memchr (start, '/', name_end - start);
      if (! scan)
	scan = name_end;

      if (! is_valid_dirent_name (start, scan - start))
	{
	  svn_fs_close_dir (dir);
	  return path_syntax (fs ,name);
	}

      /* Try to find a entry by that name in DIR.  */
      svn_err = search (&entry_id, dir, start, scan - start, 0, pool);

      /* Close the parent directory.  */ 
      svn_fs_close_dir (dir);

      /* Handle any error returned by `search'.  */
      SVN_ERR (svn_err);

      /* If we didn't find a matching entry, then return an error.  */
      if (! entry_id)
	return path_not_found (fs, name);

      /* Try to open the node whose ID we've found.  */
      SVN_ERR (svn_fs__open_node_by_id (&node, fs, entry_id, 0));

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

  *child_p = node;
  return 0;
}



/* Listing directory contents.  */


svn_error_t *
svn_fs_dir_entries (apr_hash_t **table_p,
		    svn_fs_dir_t *dir,
		    apr_pool_t *pool)
{
  svn_fs_node_t *dir_node = svn_fs_dir_to_node (dir);
  int dir_node_is_mutable = svn_fs_node_is_mutable (dir_node);
  skel_t *dir_skel, *entry;
  apr_hash_t *table;

  SVN_ERR (svn_fs__get_node_revision (&dir_skel, dir_node, 0, pool));
  table = apr_make_hash (pool);
  
  /* Walk DIR's list of entries, adding an entry to TABLE for each one.  */
  if (svn_fs__list_length (dir_skel) != 2
      || dir_skel->children->next->is_atom)
    return corrupt_node_revision (dir_node);

  for (entry = dir_skel->children->next->children; entry; entry = entry->next)
    {
      skel_t *name_skel, *id_skel;
      svn_fs_dirent_t *dirent;

      if (svn_fs__list_length (entry) != 2
	  || ! entry->children->is_atom
	  || ! entry->children->next->is_atom)
	return corrupt_node_revision (dir_node);

      name_skel = entry->children;
      id_skel = entry->children->next;

      dirent = NEW (pool, svn_fs_dirent_t);

      /* If the node is immutable, name_skel points into the node's
	 copy of the data, so we need to copy it.  Otherwise, we know
	 it's already allocated in pool, so we can just reference it.

         dirent->name should be considered "const" by callers, so it
         should be safe to return them a reference to our data. */
      if (dir_node_is_mutable)
	{
	  dirent->name = NEW (pool, svn_string_t);
	  dirent->name->data = (char *)name_skel->data;
	  dirent->name->len = name_skel->len;
	}
      else
	dirent->name = svn_string_ncreate (name_skel->data, name_skel->len,
					   pool);

      dirent->id = svn_fs_parse_id (id_skel->data, id_skel->len, pool);

      if (! dirent->id)
	return corrupt_node_revision (dir_node);

      apr_hash_set (table, dirent->name->data, dirent->name->len, dirent);
    }

  *table_p = table;
  return 0;
}



/* Deleting files.  */


/* The arguments to delete_body, to be passed through svn_fs__retry_txn.  */
struct delete_args {
  svn_fs_node_t *dir_node;
  svn_string_t *name;
  apr_pool_t *pool;
};
    

static svn_error_t *
delete_body (void *baton,
	     DB_TXN *db_txn)
{
  /* Unpack the arguments, passed through svn_fs__retry_txn.  */
  struct delete_args *args = baton;
  svn_fs_node_t *dir_node = args->dir_node;
  svn_string_t *name = args->name;
  apr_pool_t *pool = args->pool;

  svn_fs_t *fs = svn_fs__node_fs (dir_node);
  skel_t *skel, *entry_list, **entry;

  if (! is_valid_dirent_name (name->data, name->len))
    return path_syntax (fs, name);

  /* Make sure this is a mutable node.  */
  if (! svn_fs_node_is_mutable (dir_node))
    return node_not_mutable (dir_node);
  
  /* Read the node's contents.  */
  SVN_ERR (svn_fs__get_node_revision (&skel, dir_node, db_txn, pool));

  /* Find an entry whose name is NAME, and delete it.  */
  entry_list = skel->children->next;
  if (entry_list->is_atom)
    return corrupt_node_revision (dir_node);

  for (entry = &entry_list->children; *entry; entry = &(*entry)->next)
    {
      skel_t *entry_name, *entry_id;
      if (svn_fs__list_length (*entry) != 2
	  || ! (*entry)->children->is_atom
	  || ! (*entry)->children->next->is_atom)
	return corrupt_node_revision (dir_node);

      entry_name = (*entry)->children;
      entry_id = (*entry)->children->next;

      if (entry_name->len == name->len
	  && ! memcmp (entry_name->data, name->data, name->len))
	break;
    }

  /* Did we find a matching entry?  */
  if (! *entry)
    return path_not_found (fs, name);

  /* Remove the entry, and write back the directory.  We just drop all
     references to the node; the commit process will notice that it's
     not referenced and clean it up.

     If we change this, then svn_fs_open_node will need to use a
     transaction to make sure directories don't go away while it
     works.  */
  *entry = (*entry)->next;

  SVN_ERR (svn_fs__put_node_revision (dir_node, skel, db_txn));

  return 0;
}


svn_error_t *
svn_fs_delete (svn_fs_dir_t *dir,
	       svn_string_t *name,
	       apr_pool_t *pool)
{
  svn_fs_node_t *dir_node = svn_fs_dir_to_node (dir);
  svn_fs_t *fs = svn_fs__node_fs (dir_node);
  struct delete_args args;

  args.dir_node = dir_node;
  args.name = name;
  args.pool = pool;

  return svn_fs__retry_txn (fs, delete_body, &args);
}


/* Trivial bookkeeping operations on directories.  */


svn_fs_dir_t *
svn_fs_node_to_dir (svn_fs_node_t *node)
{
  if (svn_fs_node_is_dir (node))
    return (svn_fs_dir_t *) node;
  else
    return 0;
}


svn_fs_node_t *
svn_fs_dir_to_node (svn_fs_dir_t *dir)
{
  return (svn_fs_node_t *) dir;
}


void
svn_fs_close_dir (svn_fs_dir_t *dir)
{
  svn_fs_close_node (svn_fs_dir_to_node (dir));
}
