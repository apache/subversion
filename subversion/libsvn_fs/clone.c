/* A linked list representing a particular path from some node up to
   the root directory of some revision.

   Revisions can share directory structure, having several links to
   the same subdirectory.  However, this is simply an abbreviated
   representation of the "virtual tree" --- the tree you'd see if you
   simply traversed the structure without regard for node that appear
   more than once.

   We use a path to uniquely identify a node in the virtual tree of a
   particular revision.  */

struct path {

  /* The ID of this node.  */
  svn_fs_id_t *id;

  /* The parent directory of this node.  */
  struct parent *parent;

  /* The name this node has in that parent.  */
  char *entry;
};


/* In the filesystem FS, as part of the Berkeley DB transaction
   DB_TXN, and as part of the Subversion transaction SVN_TXN:

   Create a clone of the node revision whose path in in SVN_TXN's base
   revision is BASE_PATH, and record the clone in the `clones' table.
   Set *CLONE_ID_P to the clone's ID.

   Allocate CLONE_ID, and do any necessary temporary allocation, in
   POOL.  */

static svn_error_t *
clone_one (svn_fs_id_t *clone_id_p,
	   svn_fs_t *fs,
	   DB_TXN *db_txn,
	   svn_fs_txn_t *svn_txn,
	   struct path *base_path;
	   apr_pool_t *pool)
{
  skel_t *base_skel;
  svn_fs_id_t *clone_id;

  SVN_ERR (svn_fs__get_node_revision (&base_skel, fs, db_txn,
				      base_path->id,
				      pool));
  SVN_ERR (svn_fs__create_successor (&clone_id, fs, db_txn, 
				     base_path->id, base_skel,
				     pool));
  if (! base_path->parent)
    SVN_ERR (record_new_txn_root (fs, db_txn, svn_txn, clone_id));
  else
    SVN_ERR (record_clone (fs, db_txn, svn_txn, base_path, clone_id));

  *clone_id_p = clone_id;
  return 0;
}


/* In the filesystem FS, as part of the Berkeley DB transaction
   DB_TXN, and as part of the Subversion transaction SVN_TXN:

   Clone the node revision whose path in SVN_TXN's base revision is
   BASE_PATH.  Do any necessary bubbling-up.  Set *CLONE_ID_P to the
   clone's ID.

   Allocate CLONE_ID, and do any necessary temporary allocation, in
   POOL.  */
svn_error_t *
svn_fs__clone_path (svn_fs_id_t **clone_id_p,
		    svn_fs_t *fs,
		    DB_TXN *db_txn,
		    svn_fs_txn_t *svn_txn,
		    struct path *base_path,
		    apr_pool_t *pool)
{
  skel_t *clone_info;
  svn_fs_id_t *clone_id;
  svn_fs_id_t *parent_clone;
  const char *entry;
  
  /* Are we trying to clone the root directory?  */
  if (! base_path->parent)
    {
      svn_fs_id_t *txn_root, *base_root;
      SVN_ERR (get_txn_roots (&txn_root, &base_root, fs, db_txn, svn_txn,
			      pool));

      /* Sanity check.  */
      if (! svn_fs_id_eq (base_path->id, base_root))
	abort ();

      /* If the transaction's root directory differs from the base revision's
	 root directory, then the root has already been cloned.  */
      if (! svn_fs_id_eq (txn_root, base_root))
	{
	  *clone_id_p = txn_root;
	  return 0;
	}

      return clone_one (clone_id_p, fs, db_txn, svn_txn, base_path, pool);
    }

  /* Check the clones table, to see if someone else has done something
     with this node already.  */
  SVN_ERR (check_clone (&clone_info, fs, db_txn, svn_txn, base_path, pool));

  /* If the node has already been cloned by someone else, we're done
     before we start.  */
  if (is_cloned (&clone_id, clone_info))
    ;

  /* If the node has a new parent, then we know that parent has
     already been cloned.  Just clone this node, and update the parent
     dir entry.  */
  else if (is_moved (&parent_clone, &entry, clone_info))
    {
      SVN_ERR (clone_one (&clone_id, fs, db_txn, svn_txn, base_path, pool));
      SVN_ERR (svn_fs__change_dir_entry (fs, db_txn,
					 parent_clone, entry, clone_id,
					 pool));
    }

  /* Recursively clone the parent, and then update its dir entry.  */
  else
    {
      SVN_ERR (svn_fs__clone_path (&parent_clone,
				   fs, db_txn, svn_txn, base_path->parent,
				   pool));
      SVN_ERR (clone_one (&clone_id, fs, db_txn, svn_txn, base_path, pool));
      SVN_ERR (svn_fs__change_dir_entry (fs, db_txn,
					 parent_clone, base_path->name,
					 clone_id, pool));
    }

  *clone_id_p = clone_id;
  return 0;
}
