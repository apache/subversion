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

#include "svn_path.h"
#include "svn_fs.h"
#include "dag.h"
#include "err.h"
#include "fs.h"
#include "node-rev.h"
#include "nodes-table.h"
#include "txn-table.h"
#include "rev-table.h"
#include "skel.h"
#include "trail.h"



/* Initializing a filesystem.  */

struct dag_node_t
{
  /* The filesystem this dag node came from. */
  svn_fs_t *fs;

  /* The pool in which this dag_node_t was allocated.  Unlike
     filesystem and root pools, this is not a private pool for this
     structure!  The caller may have allocated other objects of their
     own in it.  */
  apr_pool_t *pool;

  /* The node revision ID for this dag node, allocated in POOL.  */
  svn_fs_id_t *id;

  /* The node's NODE-REVISION skel.  
     jimb todo: the contents of mutable nodes could be changed by
     other processes, so we should fetch them afresh within each
     trail. 
     jimb todo: What pool is this allocated in?  Chaos!  */
  skel_t *contents;

};


/* Trail body for svn_fs__dag_init_fs. */
static svn_error_t *
txn_body_dag_init_fs (void *fs_baton, trail_t *trail)
{
  svn_fs_t *fs = fs_baton;

  /* Create empty root directory with node revision 0.0:
     "nodes" : "0.0" -> "(fulltext [(dir ()) ()])" */
  {
    static char unparsed_node_rev[] = "((dir ()) ())";
    skel_t *node_rev = svn_fs__parse_skel (unparsed_node_rev,
                                           sizeof (unparsed_node_rev) - 1,
                                           trail->pool);
    svn_fs_id_t *root_id = svn_fs_parse_id ("0.0", 3, trail->pool);

    SVN_ERR (svn_fs__put_node_revision (fs, root_id, node_rev, trail));
    SVN_ERR (svn_fs__stable_node (fs, root_id, trail));
  } 

  /* Link it into filesystem revision 0:
     "revisions" : 0 -> "(revision 3 0.0 ())" */
  {
    static char rev_skel[] = "(revision 3 0.0 ())";
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
  return svn_fs__retry_txn (fs, txn_body_dag_init_fs, fs, fs->pool);
}



/* ### these functions are defined so that we can load the library.
   ### without them, we get undefined references from tree.c
   ### obviously, they don't work and will need to be filled in...
*/


const svn_fs_id_t *svn_fs__dag_get_id (dag_node_t *node)
{
  return node->id;
}


svn_fs_t *svn_fs__dag_get_fs (dag_node_t *node)
{
  return node->fs;
}


/* Helper for next three funcs */
static int
node_is_kind_p (dag_node_t *node, const char *kindstr)
{
  /* No gratutitous syntax (or null-value) checks in here, because
     we're assuming that lower layers have already scanned the content
     skel for validity. */

  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node->contents->children;
  
  /* The first element of the header should be an atom defining the
     node kind. */
  skel_t *kind = header->children;

  apr_size_t kindstr_len = strlen(kindstr);
  
  if ((kind->len == kindstr_len)
      && (! memcmp (kind->data, kindstr, kindstr_len)))
    return TRUE;
  else
    return FALSE;
}

int svn_fs__dag_is_file (dag_node_t *node)
{
  return node_is_kind_p (node, "file");
}

int svn_fs__dag_is_directory (dag_node_t *node)
{
  return node_is_kind_p (node, "dir");
}

int svn_fs__dag_is_copy (dag_node_t *node)
{
  return node_is_kind_p (node, "copy");
}


int svn_fs__dag_is_mutable (dag_node_t *node)
{
  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node->contents->children;
  
  /* The 3rd element of the header, IF it exists, is the header's
     first `flag'.  It could be NULL.  */
  skel_t *flag = header->children->next->next;
  
  while (flag)
    {
      /* Looking for the `mutable' flag, which is itself a list. */
      if (svn_fs__matches_atom (flag->children, "mutable"))
        return TRUE;

      /* Move to next header flag. */
      flag = flag->next;
    }
  
  /* Reached the end of the header skel, no mutable flag was found. */
  return FALSE;
}


skel_t *
svn_fs__dag_find_dir_entry (dag_node_t *node, const char *name )
{
  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node->contents->children;

  if (header)
    {
      /* Make sure we're looking at a directory node here */
      if (svn_fs__matches_atom (header->children, "dir"))
        {
          /* The entries are the 2nd, 3rd, etc. elements of the
             node-revision skel. */
          skel_t *entry = node->contents->children->next;
 
          /* Loop through all the entries in this node-revision... */
          while (entry)
            {
              /* ...returning true if we find one whose name matches
                 the entry name passed into this function. */
              if (svn_fs__matches_atom (entry->children, name))
                return entry;

              entry = entry->next;
            }
        }
    }
  return (skel_t *)NULL;
}
        

svn_error_t *svn_fs__dag_get_proplist (skel_t **proplist_p,
                                       dag_node_t *node,
                                       trail_t *trail)
{
  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node->contents->children;

  /* The property list is the 2nd item in the header skel. */
  skel_t *props = header->children->next;

  /* Return a copy dup'd in TRAIL's pool, to fufill this routine's
     promise about lifetimes.  This is instead of doing fancier
     cache-y things. */
  *proplist_p = svn_fs__copy_skel (props, trail->pool);
   
  return SVN_NO_ERROR;
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
svn_error_t *
svn_fs__dag_revision_root (dag_node_t **node_p,
                           svn_fs_t *fs,
                           svn_revnum_t rev,
                           trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}


svn_error_t *
svn_fs__dag_clone_root (dag_node_t **root_p,
                        svn_fs_t *fs,
                        const char *svn_txn,
                        trail_t *trail)
{
  svn_fs_id_t *base_root_id, *root_id;
  dag_node_t *root_node;  /* The node we'll return. */
  skel_t *root_skel;      /* Skel contents of the node we'll return. */

  /* Get the node ID's of the root directories of the transaction and
     its base revision.  */
  SVN_ERR (svn_fs__get_txn (&root_id, &base_root_id, fs, svn_txn, trail));

  /* Oh, give me a clone...
     (If they're the same, we haven't cloned the transaction's root
     directory yet.)  */
  if (svn_fs_id_eq (root_id, base_root_id)) 
    {
      /* Of my own flesh and bone...
         (Get the NODE-REVISION skel for the base node, and then write
         it back out as the clone.) */
      SVN_ERR (svn_fs__get_node_revision (&root_skel, fs, base_root_id,
                                          trail));
      SVN_ERR (svn_fs__create_successor (&root_id, fs, base_root_id, root_skel,
                                         trail));
    }
  else
    {
      /* With its Y-chromosome changed to X...
         (If the root has already been cloned, read its current contents.)  */
      SVN_ERR (svn_fs__get_node_revision (&root_skel, fs, root_id, trail));
    }

  /* One way or another, root_id now identifies a cloned root node,
     and root_skel is its NODE-REVISION skel.  */

  /* kff todo: Hmm, time for a constructor?  Do any of these need to
     be copied?  I don't think so... */
  root_node = apr_pcalloc (trail->pool, sizeof (*root_node));
  root_node->fs = fs;
  root_node->id = root_id;
  root_node->contents = root_skel;
  root_node->pool = trail->pool;
  
  /* ... And when it is grown
   *      Then my own little clone
   *        Will be of the opposite sex!
   *
   * (Sung to the tune of "Home, Home on the Range", with thanks to
   * Randall Garrett and Isaac Asimov.)
   */

  *root_p = root_node;
  return SVN_NO_ERROR;
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


/* Create a new mutable file named NAME in PARENT, as part of TRAIL.
   Set *CHILD_P to a reference to the new node, allocated in
   TRAIL->pool.  The new file's contents are the empty string, and it
   has no properties.  PARENT must be mutable.  NAME must be a single
   path component; it cannot be a slash-separated directory path.  */
svn_error_t *svn_fs__dag_make_file (dag_node_t **child_p,
                                    dag_node_t *parent,
                                    const char *name,
                                    trail_t *trail)
{
  svn_fs_id_t *new_node_id;

  if (! svn_fs__dag_is_mutable (parent)) /* is the parent mutable? */
    {
      /* return some nasty error */
    }

  {
    skel_t *noderev_skel;
    skel_t *header_skel;
    skel_t *flag_skel;
    svn_string_t *id_str;

    /* Call .toString() on parent's id -- oops!  This isn't Java! */
    id_str = svn_fs_unparse_id (parent->id, trail->pool);
    
    /* Create a new skel for our new node, the format of which is
       (HEADER KIND-SPECIFIC), where HEADER is (file PROPLIST
       (mutable PARENT-ID)), and KIND-SPECIFIC is an empty atom. */
    
    /* Step 1: create the FLAG skel. */
    flag_skel = svn_fs__make_empty_list (trail->pool);
    svn_fs__prepend (svn_fs__str_atom (id_str->data, trail->pool),
                     flag_skel);
    svn_fs__prepend (svn_fs__str_atom ((char *) "mutable", trail->pool), 
                     flag_skel);
    /* Now we have a FLAG skel: (mutable PARENT-ID) */
    
    /* Step 2: create the HEADER skel. */
    header_skel = svn_fs__make_empty_list (trail->pool);
    svn_fs__prepend (flag_skel, header_skel);
    svn_fs__prepend (svn_fs__make_empty_list (trail->pool),
                     header_skel);
    svn_fs__prepend (svn_fs__str_atom ((char *) "file", trail->pool),
                     header_skel);
    /* Now we have a HEADER skel: (file () FLAG) */
    
    /* Step 3: assemble the NODE-REVISION skel. */
    noderev_skel = svn_fs__make_empty_list (trail->pool);
    svn_fs__prepend (svn_fs__str_atom ((char *) "", trail->pool),
                     noderev_skel);
    svn_fs__prepend (header_skel, noderev_skel);
    /* All done, skel-wise.  We have a NODE-REVISION skel:
       (HEADER DATA), where DATA is empty. */
    
    /* Time to actually create our new node */
    SVN_ERR (svn_fs__create_node (&new_node_id, parent->fs,
                                  noderev_skel, trail));
  }

  /* Verify that this parent node does not already have an entry named
     NAME. */
  if (! svn_fs__dag_find_dir_entry (parent, name))
    {
      skel_t *pnoderev_skel;
      skel_t *entry_skel;
      svn_string_t *node_id_str;

      /* Get a string representation of the node id we created above. */
      node_id_str = svn_fs_unparse_id (new_node_id, trail->pool);

      /* Now, we need to tell the parent that it has another new mouth
         to feed.  So, we get the NODE-REVISION skel of the parent... */
      SVN_ERR (svn_fs__get_node_revision (&pnoderev_skel,
                                          parent->fs,
                                          parent->id,
                                          trail));

      /* ...and we construct a new ENTRY skel to be added to the
         parent's NODE-REVISION skel... */
      entry_skel = svn_fs__make_empty_list (trail->pool);
      svn_fs__prepend (svn_fs__str_atom (node_id_str->data, trail->pool),
                       entry_skel);
      svn_fs__prepend (svn_fs__str_atom ((char *) name, trail->pool),
                       entry_skel);
      
      /* ...and now we have an ENTRY skel for this new child: (NAME ID).
         So.  We now get to slap this entry into the parent's list of
         entries. 
      */
      svn_fs__append (entry_skel, pnoderev_skel);
      
      /* Finally, update the parent's stored skel. */
      SVN_ERR (svn_fs__put_node_revision (parent->fs,
                                          parent->id,
                                          pnoderev_skel,
                                          trail));
    }
  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
