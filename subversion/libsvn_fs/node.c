/* node.c : implementation of node functions
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

#include "svn_fs.h"
#include "fs.h"
#include "node.h"
#include "id.h"
#include "skel.h"
#include "err.h"
#include "dbt.h"


/* Building some often-used error objects.  */

static svn_error_t *
corrupt_representation (svn_fs_t *fs, svn_fs_id_t *id)
{
  svn_string_t *unparsed_id = svn_fs__unparse_id (id, fs->pool);

  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
     "corrupt representation for node `%s' in filesystem `%s'",
     unparsed_id->data, fs->env_path);
}



/* Reading node representations from the database.  */


/* Set *SKEL to point to the REPRESENTATION skel for the node ID in FS.
   Allocate the skel and the data it points into in POOL.

   Beyond verifying that it's a syntactically valid skel, this doesn't
   validate the data returned at all.  */
static svn_error_t *
get_representation_skel (skel_t **skel, svn_fs_t *fs, svn_fs_id_t *id,
			 apr_pool_t *pool)
{
  DBT key, value;
  svn_string_t *unparsed_id;
  skel_t *rep_skel;

  SVN_ERR (svn_fs__check_fs (fs));

  /* Generate the ASCII form of the node version ID.  */
  unparsed_id = svn_fs__unparse_id (id, pool);
  svn_fs__set_dbt (&key, unparsed_id->data, unparsed_id->len);
  svn_fs__result_dbt (&value);
  SVN_ERR (DB_ERR (fs, "reading node representation",
		   fs->nodes->get (fs->nodes,
				   0, /* no transaction */
				   &key, &value, 0)));
  svn_fs__track_dbt (&value, pool);

  rep_skel = svn_fs__parse_skel (value.data, value.size, pool);
  if (! rep_skel)
    return corrupt_representation (fs, id);

  *skel = rep_skel;
  return 0;
}



/* Recovering the full text of NODE-VERSION skels from the database.  */


/* Set SKEL to point to the NODE-VERSION skel for the node ID in FS.
   Allocate the skel and the data it points into in POOL.

   This takes care of applying any necessary deltas to reconstruct the
   node version.  */

static svn_error_t *
get_node_version_skel (skel_t **skel, svn_fs_t *fs, svn_fs_id_t *id,
		       apr_pool_t *pool)
{
  skel_t *rep;

  /* Well, this would take care of applying any necessary deltas, but
     we don't have anything that generates vcdiff-format output yet,
     so I can't store deltas in the database.

     So for now, every node is stored using the "fulltext"
     representation.  I'm off the hook!!  */
  SVN_ERR (get_representation_skel (&rep, fs, id, pool));
  if (svn_fs__list_length (rep) != 2
      || ! svn_fs__is_atom (rep->children, "fulltext"))
    return corrupt_representation (fs, id);

  *skel = rep->children->next;
  return 0;
}



/* The node cache.  */

/* The interfaces to these functions will need to change if the
   filesystem becomes multi-threaded.  Suppose one thread checks the
   cache for a node, doesn't find it, and decides to go read it from
   the database and put it in the cache.  While it's off doing that,
   another thread comes in looking for the same node.  That thread
   should *not* also go off and try to read the node from the database
   --- perhaps it should wait for the first thread to finish doing so,
   or perhaps something else should happen.  But the race condition
   needs to be settled somehow.  */


/* Look for the node named by ID in FS's node cache.  If we find the
   node, increment its open count by one, and return it.  Otherwise,
   return zero.  */
static svn_fs_node_t *
get_cached_node (svn_fs_t *fs, svn_fs_id_t *id)
{
  int id_size = svn_fs_id_length (id) * sizeof (id[0]);
  svn_fs_node_t *node = apr_hash_get (fs->node_cache, id, id_size);

  if (node)
    node->open_count++;

  return node;
}


/* Add NODE to its filesystem's node cache, under its ID.  */
static void
cache_node (svn_fs_node_t *node)
{
  svn_fs_t *fs = node->fs;
  svn_fs_id_t *id = node->id;
  int id_size = svn_fs_id_length (id) * sizeof (id[0]);

  /* Sanity check: we can't cache nodes that live in pools other than
     the filesystem's pool --- we might hand out pointers to it to
     random modules, and then the user could free it.  */
  if (node->pool != fs->pool)
    abort ();

  /* Sanity check: make sure we're not writing over another node
     object that's already in the cache.  */
  {
    svn_fs_node_t *other = apr_hash_get (fs->node_cache, id, id_size);

    if (other)
      abort ();
  }

  apr_hash_set (fs->node_cache, id, id_size, node);
}



/* Building node structures.  */

#if 0  /* incomplete */
svn_error_t *
svn_fs__open_id (svn_fs_node_t **node_p,
		 svn_fs_t *fs,
		 svn_fs_id_t *id)
{
  svn_fs_node_t *node = get_cached_node (fs, id);

  if (node)
    return node;

  /* It's not cached; we'll have to read it in ourselves.  */
  return 0;
}
#endif
