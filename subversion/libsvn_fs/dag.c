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

  /* The node's NODE-REVISION skel, or zero if we haven't read it in
     yet.  This is allocated either in this node's POOL, if the node
     is immutable, or in some trail's pool, if the node is mutable.
     For mutable nodes, this must be reset to zero as soon as the
     trail in which we read it is completed.  Otherwise, we will end
     up with out-of-date content here.

     If you're willing to respect all the rules above, you can munge
     this yourself, but you're probably better off just calling
     `get_node_revision_cached' and `set_node_revision_cached', which
     take care of things for you.  */
 skel_t *node_revision;

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



/* Getting and setting the NODE-REVISION skel for a dag node.  */


/* This function and its friends should be moved to a page above here,
   making this declaration unnecessary.  */
static int has_mutable_flag (skel_t *node_content);


/* Clear NODE's cache of its node revision.  */
static void
uncache_node_revision (void *baton)
{
  dag_node_t *node = baton;

  node->node_revision = 0;
}


/* Set NODE's node revision cache to SKEL, as part of TRAIL.
   SKEL must be allocated in TRAIL->pool.  */
static void
cache_node_revision (dag_node_t *node,
                     skel_t *skel,
                     trail_t *trail)
{
  if (has_mutable_flag (skel))
    {
      /* Mutable nodes might have other processes change their
         contents, so we must throw away this skel once the trail is
         complete.  */
      svn_fs__record_completion (trail, uncache_node_revision, node);
      node->node_revision = skel;
    }
  else
    {
      /* For immutable nodes, we can cache the contents permanently,
         but we need to copy them over into the node's own pool.  */
      node->node_revision = svn_fs__copy_skel (skel, node->pool);
    }
}
                     

/* Set *SKEL_P to the cached NODE-REVISION skel for NODE, as part of
   TRAIL.  If NODE is immutable, the skel is allocated in NODE->pool.
   If NODE is mutable, the skel is allocated in TRAIL->pool, and the
   cache will be cleared as soon as TRAIL completes.

   If you plan to change the contents of NODE, be careful!  We're
   handing you a pointer directly to our cached skel, not your own
   copy.  If you change the skel as part of some operation, but then
   some Berkeley DB function deadlocks or gets an error, you'll need
   to back out your skel changes, or else the cache will reflect
   changes that never got committed.  It's probably best not to change
   the skel structure at all.  */
static svn_error_t *
get_node_revision_cached (skel_t **skel_p,
                          dag_node_t *node,
                          trail_t *trail)
{
  skel_t *node_revision;

  /* If we've already got a copy, there's no need to read it in.  */
  if (! node->node_revision)
    {
      /* Read it in, and cache it.  */
      SVN_ERR (svn_fs__get_node_revision (&node_revision, node->fs, node->id,
                                          trail));
      cache_node_revision (node, node_revision, trail);
    }
          
  /* Now NODE->node_revision is set.  */
  *skel_p = node->node_revision;
  return SVN_NO_ERROR;
}


/* Set the NODE-REVISION skel of NODE to SKEL as part of TRAIL, and
   keep NODE's cache up to date.  SKEL must be allocated in
   TRAIL->pool.  */
static svn_error_t *
set_node_revision_cached (dag_node_t *node,
                          skel_t *skel,
                          trail_t *trail)
{
  /* Write it out.  */
  SVN_ERR (svn_fs__put_node_revision (node->fs, node->id, skel, trail));

  /* Since the write succeeded, update the cache.  */
  cache_node_revision (node, skel, trail);

  return SVN_NO_ERROR;
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


/* Helper function */
static skel_t *
find_dir_entry (dag_node_t *node, const char *name)
{
  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node->contents->children;

  if (header)
    {
      /* Make sure we're looking at a directory node here */
      if (svn_fs__matches_atom (header->children, "dir"))
        {
          /* The entry list is the 2nd element of the node-revision
             skel. */
          skel_t *entry_list = node->contents->children->next;
          skel_t *entry = entry_list->children;

          /* search the entry list for one whose name matches NAME.  */
          for (entry = entry_list->children; entry; entry = entry->next)
            if (svn_fs__matches_atom (entry->children, name))
              return entry;
        }
    }
  return (skel_t *)NULL;
}
        

/* Helper for next three funcs */
static int
node_is_kind_p (dag_node_t *node, const char *kindstr)
{
  /* ben todo: once dag_node_t no longer has a `contents' field, call
     into node-rev.c to get the "fresh" content skel for our trail. */

  /* No gratutitous syntax (or null-value) checks in here, because
     we're assuming that lower layers have already scanned the content
     skel for validity. */

  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node->contents->children;
  
  /* The first element of the header should be an atom defining the
     node kind. */
  skel_t *kind = header->children;

  return svn_fs__matches_atom (kind, kindstr);
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


/* Helper for __dag_is_mutable and __dag_delete */
static int
has_mutable_flag (skel_t *node_content)
{
  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node_content->children;
  skel_t *flag;

  /* Search the list of flags (the 3rd and later elements of the
     header, possibly empty) for a `mutable' flag.  */
  for (flag = header->children->next->next; flag; flag = flag->next)
    /* Looking for the `mutable' flag, which is itself a list. */
    if (! flag->is_atom
        && svn_fs__matches_atom (flag->children, "mutable"))
      return TRUE;
  
  /* Reached the end of the header skel, no mutable flag was found. */
  return FALSE;
}


int svn_fs__dag_is_mutable (dag_node_t *node)
{
  /* ben todo: once dag_node_t no longer has a `contents' field, call
     into node-rev.c to get the "fresh" content skel for our trail. */
  return has_mutable_flag (node->contents);
}


svn_error_t *svn_fs__dag_get_proplist (skel_t **proplist_p,
                                       dag_node_t *node,
                                       trail_t *trail)
{
  /* ben todo: once dag_node_t no longer has a `contents' field, call
     into node-rev.c to get the "fresh" content skel for our trail. */

  /* The node "header" is the first element of a node-revision skel,
     itself a list. */
  skel_t *header = node->contents->children;

  /* The property list is the 2nd item in the header skel. */
  skel_t *props = header->children->next;

  /* Return a copy dup'd in TRAIL's pool, to fulfill this routine's
     promise about lifetimes.  This is instead of doing fancier
     cache-y things. */
  *proplist_p = svn_fs__copy_skel (props, trail->pool);
   
  return SVN_NO_ERROR;
}


svn_error_t *svn_fs__dag_set_proplist (dag_node_t *node,
                                       skel_t *proplist,
                                       trail_t *trail)
{
  skel_t *content_skel;

  /* Sanity check: this node better be mutable! */
  if (! svn_fs__dag_is_mutable (node))
    {
      svn_string_t *idstr = svn_fs_unparse_id (node->id, node->pool);
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
         "Can't set_proplist on *immutable* node-revision %s", idstr->data);
    }

  /* Well-formed tests:  make sure the incoming proplist is of the
     form 
               PROPLIST ::= (PROP ...) ;
                   PROP ::= atom atom ;                     */
  {
    skel_t *this;
    int len = svn_fs__list_length (proplist);

    /* Does proplist contain an even number of elements? (If proplist
       isn't a list in the first place, list_length will return -1,
       which will still fail the test.) */
    if (len % 2 != 0)
      abort ();
    
    /* Is each element an atom? */
    for (this = proplist->children; this; this = this->next)
      {
        if (! this->is_atom)
          abort ();
      }
  }
  
  /* ben todo: once dag_node_t no longer has a `contents' field, call
     into node-rev.c to get the "fresh" content skel for our trail. */
  content_skel = node->contents;
  
  /* Insert the new proplist into the content_skel.
     jimb: Watch out!  Once we've got content caching working, this
     will be changing the cached skel.  If the operation below fails
     or deadlocks, the cache will be wrong.  */
  content_skel->children->children->next = proplist;
  
  /* Commit the new content_skel, within the given trail. */
  SVN_ERR (svn_fs__put_node_revision (node->fs,
                                      node->id,
                                      content_skel,
                                      trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_revision_root (dag_node_t **node_p,
                           svn_fs_t *fs,
                           svn_revnum_t rev,
                           trail_t *trail)
{
  svn_fs_id_t *root_id;
  skel_t *root_contents;
  dag_node_t *root_node;

  SVN_ERR (svn_fs__rev_get_root (&root_id, fs, rev, trail));
  SVN_ERR (svn_fs__get_node_revision (&root_contents, fs, root_id, trail));

  root_node = apr_pcalloc (trail->pool, sizeof (*root_node));
  root_node->fs = fs;
  root_node->id = root_id;
  root_node->contents = root_contents;
  root_node->pool = trail->pool;

  *node_p = root_node;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_txn_root (dag_node_t **node_p,
                      svn_fs_t *fs,
                      const char *txn,
                      trail_t *trail)
{
  svn_fs_id_t *root_id, *ignored;
  skel_t *root_contents;
  dag_node_t *root_node;
  
  SVN_ERR (svn_fs__get_txn (&root_id, &ignored, fs, txn, trail));
  SVN_ERR (svn_fs__get_node_revision (&root_contents, fs, root_id, trail));
  
  root_node = apr_pcalloc (trail->pool, sizeof (*root_node));
  root_node->fs = fs;
  root_node->id = root_id;
  root_node->contents = root_contents;
  root_node->pool = trail->pool;
  
  *node_p = root_node;
  return SVN_NO_ERROR;
}
 

svn_error_t *
svn_fs__dag_clone_child (dag_node_t **child_p,
                         dag_node_t *parent,
                         const char *name,
                         trail_t *trail)
{
  dag_node_t *cur_entry_node; /* node currently in the parent with
                                 name NAME */
  svn_fs_id_t *new_node_id; /* node id we'll put into NEW_NODE */

  if (! svn_fs__dag_is_mutable (parent)) /* is the parent mutable? */
    {
      /* return some nasty error */
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
         "Attempted to clone child of non-mutable node");
    }

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR (svn_fs__dag_open (&cur_entry_node,
                             parent,
                             name,
                             trail));
  
  if (svn_fs__dag_is_mutable (cur_entry_node))
    {
      /* This has already been cloned */
      new_node_id = cur_entry_node->id;
    }
  else
    {
      svn_string_t *id_str;
      skel_t *entry_skel;

      /* Do the clone thingy here. */
      SVN_ERR (svn_fs__create_successor (&new_node_id, parent->fs, 
                                         cur_entry_node->id, 
                                         cur_entry_node->contents,
                                         trail));

      /* We can take our old ENTRY_SKEL and make it's ID point to
         whole new SKEL_T that contains the new clone's ID, now. */
      id_str = svn_fs_unparse_id (new_node_id, trail->pool);
      entry_skel = find_dir_entry (parent, name);
      entry_skel->children->next = svn_fs__str_atom (id_str->data,
                                                     trail->pool);
      /* jimb: don't forget to write the changed directory back into
         the database!  */
    }
  {
    /* Initialize the youngster. */
    dag_node_t *new_node = apr_pcalloc (trail->pool, sizeof (*new_node));
    new_node->fs = parent->fs;
    new_node->id = new_node_id;
    new_node->contents = cur_entry_node->contents;
    new_node->pool = trail->pool;

    /* Prepare this newborn for safe return. */
    *child_p = new_node;
  }
  
  return SVN_NO_ERROR;
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



svn_error_t *svn_fs__dag_delete (dag_node_t *parent,
                                 const char *name,
                                 trail_t *trail)
{
  skel_t *content_skel, *new_dirent_list, *old_entry, *entry;
  int deleted = FALSE;

  /* Make sure we're looking at a directory node. */
  if (! svn_fs__dag_is_directory (parent))
    return 
      svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, parent->pool,
       "Attempted to delete entry `%s' from *non*-directory node.",
       name);    

  /* Make sure the node is mutable. */
  if (! svn_fs__dag_is_mutable (parent))
    return 
      svn_error_createf
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, parent->pool,
       "Attempted to delete entry `%s' from *immutable* directory node.",
       name);      

  /* ben todo: once dag_node_t no longer has a `contents' field, call
     into node-rev.c to get the "fresh" content skel for our trail. */
  content_skel = parent->contents;
  
  /* Dup the parent's dirent list in trail->pool.  Then we can safely
     munge it all we want. */
  new_dirent_list = svn_fs__copy_skel (content_skel->children->next,
                                       trail->pool);

  entry = new_dirent_list->children;
  old_entry = NULL;

  while (entry)
    {
      if (svn_fs__matches_atom (entry->children, name))
        {
          /* Aha!  We want to remove this entry from the list. */

          /* We actually have to *retrieve* this entry, however, and
             make sure that we're not trying to remove a non-empty
             dir.  (This is part of this routine's promise.) */
          skel_t *entry_content;
          skel_t *id_skel = entry->children->next;
          svn_fs_id_t *id = svn_fs_parse_id (id_skel->data, id_skel->len,
                                             trail->pool);
          SVN_ERR (svn_fs__get_node_revision (&entry_content,
                                              parent->fs,
                                              id,
                                              trail));

          if (svn_fs__matches_atom (entry_content->children->children,
                                    "dir"))
            {
              if (has_mutable_flag (entry_content))
                {
                  int len = 
                    svn_fs__list_length (entry_content->children->next);
                  if (len != 0)
                    return 
                      svn_error_createf
                      (SVN_ERR_FS_DIR_NOT_EMPTY, 0, NULL, parent->pool,
                       "Attempted to delete *non-empty* directory `%s'.",
                       name);                        
                }
            }

          /* Just "lose" this entry by setting the *previous* entry's
             next ptr to the current entry's next ptr. */          
          if (! old_entry)
            /* Base case:  the very *first* entry matched. */
            new_dirent_list->children = entry->next;
          else
            old_entry->next = entry->next;

          deleted = TRUE;
          break;
        }

      /* No match, move to next entry. */
      old_entry = entry;
      entry = entry->next;
    }
    
  if (! deleted)
    return 
      svn_error_createf
      (SVN_ERR_FS_NO_SUCH_ENTRY, 0, NULL, parent->pool,
       "Can't delete entry `%s', not found in parent dir.",
       name);      
    
  /* Else, the linked list has been appropriately modified.  Hook it
     back into the content skel and re-write the node-revision. */
  content_skel->children->next = new_dirent_list;

  SVN_ERR (svn_fs__put_node_revision (parent->fs,
                                      parent->id,
                                      content_skel,
                                      trail));
  
  return SVN_NO_ERROR;
}


/* Helper for the next two functions. */
static svn_error_t *
make_entry (dag_node_t **child_p,
            dag_node_t *parent,
            const char *name,
            svn_boolean_t is_dir,
            trail_t *trail)
{
  svn_fs_id_t *new_node_id;
  skel_t *new_node_skel;

  if (! svn_fs__dag_is_mutable (parent)) /* is the parent mutable? */
    {
      /* return some nasty error */
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
         "Attempted to clone child of non-mutable node");
    }

  {
    skel_t *header_skel;
    skel_t *flag_skel;
    svn_string_t *id_str;

    /* Call .toString() on parent's id -- oops!  This isn't Java! */
    id_str = svn_fs_unparse_id (parent->id, trail->pool);
    
    /* Create a new skel for our new node, the format of which is
       (HEADER KIND-SPECIFIC).  If we are making a directory, the
       HEADER is (dir PROPLIST (mutable PARENT-ID)).  If not, then
       this is a file, whose HEADER is (file PROPLIST (mutable
       PARENT-ID)).  KIND-SPECIFIC is an empty atom for files, an
       empty list for directories. */
    
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
    if (is_dir)
      {
        svn_fs__prepend (svn_fs__str_atom ((char *) "dir", trail->pool),
                         header_skel);
      }
    else
      {
        svn_fs__prepend (svn_fs__str_atom ((char *) "file", trail->pool),
                         header_skel);
      }
    /* Now we have a HEADER skel: (file-or-dir () FLAG) */
    
    /* Step 3: assemble the NODE-REVISION skel. */
    new_node_skel = svn_fs__make_empty_list (trail->pool);
    if (is_dir)
      {
        svn_fs__prepend (svn_fs__make_empty_list (trail->pool),
                         new_node_skel);
      }
    else
      {
        svn_fs__prepend (svn_fs__str_atom ((char *) "", trail->pool),
                         new_node_skel);
      }
    svn_fs__prepend (header_skel, new_node_skel);
    /* All done, skel-wise.  We have a NODE-REVISION skel as described
       far above. */
    
    /* Time to actually create our new node */
    SVN_ERR (svn_fs__create_node (&new_node_id, parent->fs,
                                  new_node_skel, trail));
  }

  /* Verify that this parent node does not already have an entry named
     NAME. */
  if (! find_dir_entry (parent, name))
    {
      skel_t *pnew_node_skel;
      skel_t *entry_skel;
      svn_string_t *node_id_str;

      /* Get a string representation of the node id we created above. */
      node_id_str = svn_fs_unparse_id (new_node_id, trail->pool);

      /* Now, we need to tell the parent that it has another new mouth
         to feed.  So, we get the NODE-REVISION skel of the parent... */
      SVN_ERR (svn_fs__get_node_revision (&pnew_node_skel,
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
      svn_fs__append (entry_skel, pnew_node_skel);
      
      /* Finally, update the parent's stored skel. */
      SVN_ERR (svn_fs__put_node_revision (parent->fs,
                                          parent->id,
                                          pnew_node_skel,
                                          trail));
    }
  else
    {
      /* return some nasty error */
      return 
        svn_error_createf 
        (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, trail->pool,
         "Attempted to create entry that already exists");
    }
  {
    /* Initialize the youngster. */
    dag_node_t *new_node = apr_pcalloc (trail->pool, sizeof (*new_node));
    new_node->fs = parent->fs;
    new_node->id = new_node_id;
    new_node->contents = new_node_skel;
    new_node->pool = trail->pool;

    /* Prepare this newborn for safe return. */
    *child_p = new_node;
  }

  return SVN_NO_ERROR;
}


svn_error_t *svn_fs__dag_make_file (dag_node_t **child_p,
                                    dag_node_t *parent,
                                    const char *name,
                                    trail_t *trail)
{
  /* Call our little helper function */
  return make_entry (child_p, parent, name, FALSE, trail);
}


svn_error_t *svn_fs__dag_make_dir (dag_node_t **child_p,
                                   dag_node_t *parent,
                                   const char *name,
                                   trail_t *trail)
{
  /* Call our little helper function */
  return make_entry (child_p, parent, name, TRUE, trail);
}


svn_error_t *
svn_fs__dag_set_contents (dag_node_t *file,
                          svn_string_t *contents,
                          trail_t *trail)
{
  /* This whole routine will have to be reincarnated as a "streamy"
     interface someday. */
  skel_t *content_skel;

  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to set textual contents of a *non*-file node.");
  
  /* Stash the file's new contents in the db. */

  /* ben todo: once dag_node_t no longer has a `contents' field, call
     into node-rev.c to get the "fresh" content skel for our trail. */
  content_skel = file->contents;
  
  /* NOTE: When we create a new "text" skel from the svn_string_t,
     allocate it in the *node's* pool.  As long as node->contents is
     allocated in node->pool, all of its subcomponents should be
     too.  */
  /* ben todo: once dag_node_t no longer has a `contents' field,
     reverse what I said above and do the allocation in trail->pool.
     (No need to fill node->pool with junk.) */
  content_skel->children->next = svn_fs__str_atom (contents->data,
                                                   file->pool);

  SVN_ERR (svn_fs__put_node_revision (file->fs, file->id,
                                      content_skel, trail));

  return SVN_NO_ERROR;
}



/* THE LAND OF CMPILATO */
/* cmpilato todo:  all this stuff down here. */

dag_node_t *svn_fs__dag_dup (dag_node_t *node,
                             trail_t *trail)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pcalloc (trail->pool, sizeof (*new_node));

  new_node->fs = node->fs;
  new_node->pool = trail->pool;
  new_node->id = svn_fs_copy_id (node->id, node->pool);
  
  /* Leave new_node->node_revision zero for now, so it'll get read in.
     We can get fancy and duplicate node's cache later.  */

  return new_node;
}


/* Open the node named NAME in the directory PARENT, as part of TRAIL.
   Set *CHILD_P to the new node, allocated in TRAIL->pool.  NAME must be a
   single path component; it cannot be a slash-separated directory
   path.  */
svn_error_t *svn_fs__dag_open (dag_node_t **child_p,
                               dag_node_t *parent,
                               const char *name,
                               trail_t *trail)
{
  skel_t *entry_skel;
  skel_t *node_skel;
  svn_fs_id_t *node_id;

  /* Find the entry named NAME in PARENT if it exists. */
  entry_skel = find_dir_entry (parent, name);
  if (! entry_skel)
    {
      /* return some other nasty error */
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_FOUND, 0, NULL, trail->pool,
         "Attempted to open non-existant child node");
    }
  
  /* Alrighty then.  We'll snatch the NODE-REVISION for the child we
     found (based on it's ID, the second atom in the ENTRY that
     find_dir_entry returned to us), and see what we can see. */
  if ((entry_skel->children) && (entry_skel->children->next))
    {
      skel_t *id_skel = entry_skel->children->next;
      node_id = svn_fs_parse_id (id_skel->data, 
                                 id_skel->len,
                                 trail->pool);
      SVN_ERR (svn_fs__get_node_revision (&node_skel,
                                          parent->fs,
                                          node_id,
                                          trail));
  
    }
  {
    /* Allocate our new node. */
    dag_node_t *new_node = apr_pcalloc (trail->pool, sizeof (*new_node));
    new_node->fs = parent->fs;
    new_node->id = node_id;
    new_node->contents = node_skel;
    new_node->pool = trail->pool;

    /* Prepare this newborn for safe return. */
    *child_p = new_node;
  }

  return SVN_NO_ERROR;
}


/* Create a link to CHILD in PARENT named NAME, as part of TRAIL.
   PARENT must be mutable.  NAME must be a single path component; it
   cannot be a slash-separated directory path.  */
svn_error_t *svn_fs__dag_link (dag_node_t *parent,
                               dag_node_t *child,
                               const char *name,
                               trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}

/* Create a copy node named NAME in PARENT which refers to SOURCE_PATH
   in SOURCE_REVISION, as part of TRAIL.  Set *CHILD_P to a reference
   to the new node, allocated in TRAIL->pool.  PARENT must be mutable.
   NAME must be a single path component; it cannot be a slash-
   separated directory path.  */
svn_error_t *svn_fs__dag_make_copy (dag_node_t **child_p,
                                    dag_node_t *parent,
                                    const char *name,
                                    svn_revnum_t source_revision,
                                    const char *source_path,
                                    trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}


/* Set *REV_P and *PATH_P to the revision and path of NODE, which must
   be a copy node, as part of TRAIL.  Allocate *PATH_P in TRAIL->pool.  */
svn_error_t *svn_fs__dag_get_copy (svn_revnum_t *rev_p,
                                   char **path_p,
                                   dag_node_t *node,
                                   trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}

/* OTALIPMC FO DNAL EHT */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
