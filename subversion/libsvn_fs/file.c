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
#include "file.h"
#include "node.h"
#include "skel.h"
#include "proplist.h"


/* Building error objects.  */

static svn_error_t *
corrupt_node_revision (svn_fs_t *fs, svn_fs_id_t *id)
{
  svn_string_t *unparsed_id = svn_fs_unparse_id (id, fs->pool);

  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "corrupt node revision for node `%s' in filesystem `%s'",
     unparsed_id->data, fs->env_path);
}



/* Building file objects.  */

svn_error_t *
svn_fs__file_from_skel (svn_fs_node_t **node,
			svn_fs_t *fs, 
			svn_fs_id_t *id,
			skel_t *nv, 
			apr_pool_t *skel_pool)
{
  svn_fs_file_t *file;

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
}



/* Casting and closing file objects, and other trivial bookkeeping.  */


svn_fs_file_t *
svn_fs_node_to_file (svn_fs_node_t *node)
{
  if (node->kind != kind_file)
    return 0;
  else
    return (svn_fs_file_t *) node;
}


svn_fs_node_t *
svn_fs_file_to_node (svn_fs_file_t *file)
{
  return &file->node;
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


svn_error_t *
svn_fs_file_length (apr_off_t *length,
		    svn_fs_file_t *file)
{
  *length = file->contents->len;

  return 0;
}


svn_error_t *
svn_fs_file_contents (svn_read_fn_t **contents,
		      void **contents_baton,
		      svn_fs_file_t *file,
		      apr_pool_t *pool)
{
  struct read_string *rs = NEW (pool, struct read_string);
  rs->contents = file->contents;
  rs->offset = 0;

  *contents = read_string_fn;
  *contents_baton = rs;

  return 0;
}
