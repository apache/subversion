/* file.c : implementation of file functions
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
#include "id.h"
#include "proplist.h"


/* Building error objects.  */

static svn_error_t *
corrupt_node_revision (svn_fs_t *fs, svn_fs_id_t *id)
{
  svn_string_t *unparsed_id = svn_fs__unparse_id (id, fs->pool);

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
