/* file.c : implementation of file functions
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

#include "apr.h"
#include "apr_pools.h"
#include "apr_hash.h"

#include "svn_string.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "fs.h"
#include "skel.h"
#include "id.h"
#include "err.h"
#include "node.h"
#include "file.h"
<<<<<<< file.c
#include "dir.h"
=======
#include "node.h"
#include "skel.h"
#include "proplist.h"
>>>>>>> 1.4


/* Building error objects.  */

static svn_error_t *
<<<<<<< file.c
corrupt_node_version (svn_fs_node_t *node)
=======
corrupt_node_revision (svn_fs_t *fs, svn_fs_id_t *id)
>>>>>>> 1.4
{
<<<<<<< file.c
  svn_fs_t *fs = svn_fs__node_fs (node);
  svn_fs_id_t *id = svn_fs__node_id (node);
  svn_string_t *unparsed_id = svn_fs__unparse_id (id, fs->pool);
=======
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);
>>>>>>> 1.4

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
  svn_string_t *unparsed_id = svn_fs__unparse_id (id, fs->pool);

  return
    svn_error_createf
    (SVN_ERR_FS_NOT_MUTABLE, 0, 0, fs->pool,
     "attempt to change immutable node `%s' in filesystem `%s'",
     unparsed_id->data, fs->env_path);
}


static svn_error_t *
bad_default_base (svn_fs_node_t *node)
{
  svn_fs_t *fs = svn_fs__node_fs (node);

<<<<<<< file.c
  return
    svn_error_createf
    (SVN_ERR_FS_BAD_DEFAULT_BASE, 0, 0, fs->pool,
     "`svn_fs_default_base' passed to an `add' function in filesystem `%s'",
     fs->env_path);
=======
  /* Do a quick check of the syntax of the skel, before we do any more
     work.  */
  if (svn_fs__list_length (nv) != 3
      || ! nv->children->next->is_atom
      || ! nv->children->next->next->is_atom)
    return corrupt_node_revision (fs, id);

  file = ((svn_fs_file_t *)
	  svn_fs__init_node (sizeof (*file), fs, id, kind_file));

  /* Try to parse the file's property list.  */
  file->node.proplist = svn_fs__make_proplist (nv->children->next,
					       file->node.pool);
  if (! file->node.proplist)
    {
      apr_destroy_pool (file->node.pool);
      return corrupt_node_revision (fs, id);
    }

  /* Make a copy of the file's contents.  */
  {
    skel_t *content_skel = nv->children->next->next;
    
    file->contents = svn_string_ncreate (content_skel->data,
					 content_skel->len,
					 file->node.pool);
  }

  /* ANSI C guarantees that, if the first element of `struct S' has
     type `T', then casting a `struct S *' to `T *' and a pointer to
     S's first element are equivalent.  */
  *node = &file->node;
  return 0;
>>>>>>> 1.4
}


/* Casting and closing file objects, and other trivial bookkeeping.  */


svn_fs_file_t *
svn_fs_node_to_file (svn_fs_node_t *node)
{
  if (svn_fs_node_is_file (node))
    return (svn_fs_file_t *) node;
  else
    return 0;
}


svn_fs_node_t *
svn_fs_file_to_node (svn_fs_file_t *file)
{
  return (svn_fs_node_t *) file;
}


void
svn_fs_close_file (svn_fs_file_t *file)
{
  svn_fs_close_node (svn_fs_file_to_node (file));
}



/* Build an svn_read_fn_t function for a string.  */
/* Perhaps this should be in libsvn_string.  */


/* The type for a string contents baton.  */
struct read_string {
  svn_string_t *contents;
  apr_size_t offset;
};


/* A read-like function for reading from a string content baton.  */
static svn_error_t *
read_string_fn (void *baton,
		char *buffer,
		apr_size_t *len,
		apr_pool_t *pool)
{
  struct read_string *rs = baton;
  int remaining = rs->contents->len - rs->offset;

  /* How many bytes are we actually going to deliver?  */
  int provide = (*len > remaining ? remaining : *len);

  memcpy (buffer, rs->contents->data + rs->offset, provide);
  *len = provide;
  return 0;
}



/* Accessing file contents.  */


/* Return the DATA skel from the FILE skel SKEL, or zero if SKEL is
   misformed.  */
static skel_t *
file_data (skel_t *skel)
{
  if (svn_fs__list_length (skel) != 2
      || ! skel->children->next->is_atom)
    return 0;

  return skel->children->next;
}


svn_error_t *
svn_fs_file_length (apr_off_t *length,
		    svn_fs_file_t *file,
		    apr_pool_t *pool)
{
  svn_fs_node_t *node = svn_fs_file_to_node (file);
  skel_t *skel, *data;
  
  SVN_ERR (svn_fs__get_node_version (&skel, node, 0, pool));
  data = file_data (skel);
  if (! data)
    return corrupt_node_version (node);
  
  *length = data->len;

  return 0;
}


svn_error_t *
svn_fs_file_contents (svn_read_fn_t **contents,
		      void **contents_baton,
		      svn_fs_file_t *file,
		      apr_pool_t *pool)
{
  svn_fs_node_t *node = svn_fs_file_to_node (file);
  skel_t *skel, *data;
  struct read_string *rs;

  SVN_ERR (svn_fs__get_node_version (&skel, node, 0, pool));
  data = file_data (skel);
  if (! data)
    return corrupt_node_version (node);

  rs = NEW (pool, struct read_string);
  rs->contents = NEW (pool, svn_string_t);

  /* If the node is immutable, the string will go away when file is closed.
     If the node is mutable, the string has been copied into pool.  */
  if (svn_fs_node_is_mutable (node))
    {
      rs->contents->data = data->data;
      rs->contents->len  = data->len;
    }
  else
    rs->contents = svn_string_ncreate (data->data, data->len, pool);

  rs->offset = 0;

  *contents = read_string_fn;
  *contents_baton = rs;

  return 0;
}



/* Adding files.  */


struct add_file_args {
  svn_fs_file_t **file_p;
  svn_fs_dir_t *dir;
  svn_string_t *name;
  svn_fs_file_t *base;
};


static svn_error_t *
add_file_body (void *baton,
	       DB_TXN *db_txn)
{
  struct add_file_args *args = baton;
  svn_fs_file_t **file_p = args->file_p;
  svn_fs_dir_t   *dir    = args->dir;
  svn_string_t   *name   = args->name;
  svn_fs_file_t  *base   = args->base;

  svn_fs_node_t *dir_node = svn_fs_dir_to_node (dir);
  svn_fs_t *fs = svn_fs__node_fs (dir_node);
  char *svn_txn_id = svn_fs__node_txn_id (dir_node);
  svn_fs_node_t *file_node;

  /* Are we adding a completely new file, or an existing file?  */
  if (base)
    {
      file_node = svn_fs__reopen_node (base);
    }
  else
    {
      /* Build a skel for the new file.  */
      apr_pool_t *pool = svn_pool_create (fs->pool);
      skel_t *header = svn_fs__new_header ("file", svn_txn_id, pool);
      skel_t *data = svn_fs__make_atom ("", pool);
      skel_t *node_version = svn_fs__make_empty_list (pool);

      svn_fs__prepend (data, node_version);
      svn_fs__prepend (header, node_version);

      SVN_ERR (svn_fs__create_node (&file_node, fs, node_version, db_txn,
				    pool));
    }

  SVN_ERR (svn_fs__link (dir, name,
			 svn_fs__node_id (file_node),
			 db_txn));

  *file_p = svn_fs_node_to_file (file_node);
  return 0;
}


svn_error_t *
svn_fs_add_file (svn_fs_file_t **file_p,
		 svn_fs_dir_t *dir,
		 svn_string_t *name,
		 svn_fs_file_t *base)
{
  svn_fs_node_t *dir_node = svn_fs_dir_to_node (dir);
  svn_fs_file_t *file;
  struct add_file_args args;

  if (! svn_fs_node_is_mutable (dir_node))
    return node_not_mutable (dir_node);

  if (base == svn_fs_default_base_file)
    return bad_default_base (dir_node);

  args.file_p = &file;
  args.dir    = dir;
  args.name   = name;
  args.base   = base;
  return svn_fs__retry_txn (svn_fs__node_fs (dir_node), add_file_body, &args);

  *file_p = file;
  return 0;
}



/* Replacing things with files.  */


/* The contents of this object don't matter --- we only use its address.  */
svn_fs_file_t *svn_fs_default_base_file = (svn_fs_file_t *) "hi there";
