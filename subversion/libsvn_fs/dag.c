/* dag.c : DAG-like interface filesystem, private to libsvn_fs
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

#include "svn_fs.h"
#include "dag.h"
#include "err.h"
#include "fs.h"
#include "nodes-table.h"
#include "rev-table.h"
#include "skel.h"
#include "trail.h"



/* Initializing a filesystem.  */

struct dag_node_t
{
  svn_fs_t *fs;
  svn_fs_id_t *id;
  apr_pool_t *pool;
  /* kff todo: working here */
};


/* Trail body for svn_fs__dag_init_fs. */
static svn_error_t *
dag_init_fs (void *fs_baton, trail_t *trail)
{
  svn_fs_t *fs = fs_baton;

  /* Create empty root directory with node revision 0.0:
     "nodes" : "0.0" -> "(fulltext [(dir ()) ()])" */
  {
    static char rep_skel[] = "(fulltext ((dir ()) ()))";
    SVN_ERR (svn_fs__put_rep (fs,
                              svn_fs_parse_id ("0.0", 3, trail->pool),
                              svn_fs__parse_skel (rep_skel,
                                                  sizeof (rep_skel) - 1,
                                                  trail->pool),
                              trail->db_txn,
                              trail->pool));
  } 

  /* Link it into filesystem revision 0:
     "revisions" : 0 -> "(revision  3 0.0  ())" */
  {
    static char rev_skel[] = "(revision  3 0.0  ())";
    svn_revnum_t rev = 0;
    SVN_ERR (svn_fs__put_rev (&rev, fs,
                              svn_fs__parse_skel (rev_skel,
                                                  sizeof (rev_skel) - 1,
                                                  trail->pool),
                              trail->db_txn,
                              trail->pool));

    if (rev != 0)
      return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
                                "initial revision number is not `0'"
                                " in filesystem `%s'",
                                fs->env_path);
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_init_fs (svn_fs_t *fs)
{
  return svn_fs__retry_txn (fs, dag_init_fs, fs, fs->pool);
}



/* ### these functions are defined so that we can load the library.
   ### without them, we get undefined references from tree.c
   ### obviously, they don't work and will need to be filled in...
*/
svn_error_t *svn_fs__dag_txn_node (dag_node_t **node_p,
				   svn_fs_t *fs,
				   const char *txn,
				   const svn_fs_id_t *id,
				   trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}
void svn_fs__dag_close (dag_node_t *node)
{
  abort();
}
const svn_fs_id_t *svn_fs__dag_get_id (dag_node_t *node)
{
  abort();
  /* NOTREACHED */
  return NULL;
}
svn_error_t *svn_fs__dag_get_proplist (skel_t **proplist_p,
				       dag_node_t *node,
				       trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}
svn_error_t *svn_fs__dag_set_proplist (dag_node_t *node,
				       skel_t *proplist,
				       trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}
svn_error_t *svn_fs__dag_clone_child (dag_node_t **child_p,
				      dag_node_t *parent,
				      const char *name,
				      trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}
svn_error_t *svn_fs__dag_clone_root (dag_node_t **root_p,
				     svn_fs_t *fs,
				     const char *svn_txn,
				     trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}
svn_error_t *svn_fs__dag_open (dag_node_t **child_p,
			       dag_node_t *parent,
			       const char *name,
			       trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}
svn_error_t *svn_fs__dag_delete (dag_node_t *parent,
				 const char *name,
				 trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
