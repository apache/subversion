/* dag.c : DAG-like interface filesystem, private to libsvn_fs
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <string.h>
#include <assert.h>

#include "svn_pools.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "dag.h"
#include "err.h"
#include "fs.h"
#include "nodes-table.h"
#include "node-rev.h"
#include "txn-table.h"
#include "rev-table.h"
#include "reps-table.h"
#include "strings-table.h"
#include "reps-strings.h"
#include "skel.h"
#include "fs_skels.h"
#include "trail.h"
#include "validate.h"
#include "id.h"


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

  /* The node's type (file, dir, copy, etc.) */
  svn_node_kind_t kind;

  /* The node's NODE-REVISION skel, or zero if we haven't read it in
     yet.  This is allocated either in this node's POOL, if the node
     is immutable, or in some trail's pool, if the node is mutable.
     For mutable nodes, this must be reset to zero as soon as the
     trail in which we read it is completed.  Otherwise, we will end
     up with out-of-date content here.

     If you're willing to respect all the rules above, you can munge
     this yourself, but you're probably better off just calling
     `get_node_revision' and `set_node_revision', which take care of
     things for you.  */
  skel_t *node_revision;

};



/* Trivial helper/accessor functions. */
svn_node_kind_t svn_fs__dag_node_kind (dag_node_t *node)
{
  return node->kind;
}


int 
svn_fs__dag_is_file (dag_node_t *node)
{
  return (node->kind == svn_node_file);
}


int 
svn_fs__dag_is_directory (dag_node_t *node)
{
  return (node->kind == svn_node_dir);
}


const svn_fs_id_t *
svn_fs__dag_get_id (dag_node_t *node)
{
  return node->id;
}


svn_fs_t *
svn_fs__dag_get_fs (dag_node_t *node)
{
  return node->fs;
}


/* Looks at node-revision NODE_REV's 'kind' to see if it matches the
   kind described by KINDSTR. */
static int
node_is_kind_p (skel_t *node_rev,
                const char *kindstr)
{
  skel_t *header = SVN_FS__NR_HEADER (node_rev);
  return svn_fs__matches_atom (SVN_FS__NR_HDR_KIND (header), kindstr);
}


/* Helper for svn_fs__dag_check_mutable.  
   WARNING! WARNING! WARNING!  This should not be called by *anything*
   that doesn't first get an up-to-date NODE-REVISION skel! */
static int
node_rev_is_mutable (skel_t *node_content)
{
  skel_t *header = SVN_FS__NR_HEADER (node_content);
  skel_t *rev = SVN_FS__NR_HDR_REV (header);
  
  return (rev->len == 0);
}


/* Set the revision field in the header of NODE_REV to a skel
   representing the empty string, an indication that this
   NODE_REV is uncommitted.  

   Also, set its copy history to null.  If this node is a copy,
   someone will set it so later; but no matter what, it shouldn't
   claim to be a copy of whatever its predecessor was a copy of. */
static void
node_rev_make_mutable (skel_t *node_rev)
{
  (SVN_FS__NR_HDR_REV (SVN_FS__NR_HEADER (node_rev)))->len = 0;
  (SVN_FS__NR_HDR_COPY (SVN_FS__NR_HEADER (node_rev))) = NULL;
  return;
}


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
  if (node_rev_is_mutable (skel))
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
get_node_revision (skel_t **skel_p,
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
set_node_revision (dag_node_t *node,
                   skel_t *skel,
                   trail_t *trail)
{
  /* Write it out.  */
  SVN_ERR (svn_fs__put_node_revision (node->fs, node->id, skel, trail));

  /* Since the write succeeded, update the cache.  */
  cache_node_revision (node, skel, trail);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_check_mutable (svn_boolean_t *is_mutable, 
                           dag_node_t *node, 
                           trail_t *trail)
{
  skel_t *node_rev;
  SVN_ERR (get_node_revision (&node_rev, node, trail));
  *is_mutable = node_rev_is_mutable (node_rev);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_get_node (dag_node_t **node,
                      svn_fs_t *fs,
                      svn_fs_id_t *id,
                      trail_t *trail)
{
  dag_node_t *new_node;
  skel_t *contents;

  /* Construct the node. */
  new_node = apr_pcalloc (trail->pool, sizeof (*new_node));
  new_node->fs = fs;
  new_node->id = svn_fs__id_copy (id, trail->pool); 
  new_node->pool = trail->pool;

  /* Grab the contents so we can inspect the node's kind. */
  SVN_ERR (get_node_revision (&contents, new_node, trail));

  /* Initialize the KIND attribute */
  if (node_is_kind_p (contents, "file"))
    new_node->kind = svn_node_file;
  else if (node_is_kind_p (contents, "dir"))
    new_node->kind = svn_node_dir;
  else
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, 0, fs->pool,
                             "Attempt to create unknown kind of node");
  
  /* Return a fresh new node */
  *node = new_node;
  
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs__dag_get_revision (svn_revnum_t *rev,
                          dag_node_t *node,
                          trail_t *trail)
{
  skel_t *node_rev, *rev_str;
  const char *rev_cstr;
  
  SVN_ERR (get_node_revision (&node_rev, node, trail));
  rev_str = SVN_FS__NR_HDR_REV (SVN_FS__NR_HEADER (node_rev));

  if (rev_str->len)
    {
      rev_cstr = apr_pstrndup (trail->pool, rev_str->data, rev_str->len);
      *rev = SVN_STR_TO_REV(rev_cstr);
    }
  else
    *rev = SVN_INVALID_REVNUM;

  return SVN_NO_ERROR;
}


/* Trail body for svn_fs__dag_init_fs. */
static svn_error_t *
txn_body_dag_init_fs (void *fs_baton, trail_t *trail)
{
  svn_fs_t *fs = fs_baton;

  /* Create empty root directory with node revision 0.0. */
  {
    /* ### this should be const. we should make parse_skel() take a const */
    static char unparsed_node_rev[] = "((dir 1 0) 0 0 )";
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
    /* ### this should be const. we should make parse_skel() take a const */
    static char rev_skel_str[] = "(revision 3 0.0 ())";
    svn_fs__revision_t *revision;
    skel_t *rev_skel;
    svn_revnum_t rev = 0;
    
    rev_skel = svn_fs__parse_skel (rev_skel_str, strlen (rev_skel_str), 
                                   trail->pool);
    SVN_ERR (svn_fs__parse_revision_skel (&revision, rev_skel, trail->pool));
    SVN_ERR (svn_fs__put_rev (&rev, fs, revision, trail));
    if (rev != 0)
      return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
                                "initial revision number is not `0'"
                                " in filesystem `%s'",
                                fs->path);
  }

  /* Set a date on revision 0. */
  {
    svn_string_t date;

    date.data = svn_time_to_nts (apr_time_now(), trail->pool);
    date.len = strlen (date.data);

    SVN_ERR (svn_fs__set_rev_prop (fs, 0, SVN_PROP_REVISION_DATE, &date,
                                   trail));
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_init_fs (svn_fs_t *fs)
{
  return svn_fs__retry_txn (fs, txn_body_dag_init_fs, fs, fs->pool);
}



/*** Directory node functions ***/

/* Some of these are helpers for functions outside this section. */

/* Given directory NODE_REV in FS, set *ENTRIES to its entries list
   skel, as part of TRAIL.  The entries list will be allocated in
   TRAIL->pool.  If NODE_REV is not a directory, return the error
   SVN_ERR_FS_NOT_DIRECTORY.  */
static svn_error_t *
get_dir_entries (skel_t **entries,
                 svn_fs_t *fs,
                 skel_t *node_rev,
                 trail_t *trail)
{
  skel_t *header = SVN_FS__NR_HEADER (node_rev);

  if (header)
    {
      /* Make sure we're looking at a directory node here */
      if (svn_fs__matches_atom (SVN_FS__NR_HDR_KIND (header), "dir"))
        {
          skel_t *rep_key_skel = SVN_FS__NR_DATA_KEY (node_rev);
          const char *rep_key = apr_pstrndup (trail->pool,
                                              rep_key_skel->data,
                                              rep_key_skel->len);
          svn_string_t entries_raw;
          skel_t *entry;

          /* Empty rep key means no entries exist. */
          if ((! rep_key) || (rep_key[0] == '\0'))
            {
              *entries = svn_fs__make_empty_list (trail->pool);
              return SVN_NO_ERROR;
            }

          /* Now we have a rep, follow through to get the entries. */
          SVN_ERR (svn_fs__rep_contents (&entries_raw, fs, rep_key, trail));
          *entries = svn_fs__parse_skel ((char *) entries_raw.data,
                                         entries_raw.len,
                                         trail->pool);

          /* Check entries are well-formed. */
          for (entry = (*entries)->children; entry; entry = entry->next)
            {
              /* ENTRY must be a list of two elements. */
              if (svn_fs__list_length (entry) != 2)
                return svn_error_create (SVN_ERR_FS_CORRUPT, 0, 
                                         NULL, trail->pool,
                                         "Malformed directory entry.");
            }
        }
      else
        return 
          svn_error_create
          (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
           "Attempted to create entry in non-directory parent");
    }
  else
    return 
      svn_error_create
      (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
       "Bad skel");
  
  return SVN_NO_ERROR;
}


/* Search for an entry NAME in directory entries list ENTRIES.
   NAME must be a single path component.

   If there is such an entry, then
        - set *ENTRY_P to point to that list skel (a reference into
          the memory allocated for ENTRIES);
        - and if PREV_ENTRY_P is non-null, then
               - if the entry found is not the first entry, set
                 *PREV_ENTRY_P to point to the entry before it,
               - else set *PREV_ENTRY_P to null.

   Else if there is no such entry, set *ENTRY_P to NULL and, if
   PREV_ENTRY_P is non-null, set *PREV_ENTRY_P to the last entry in
   the list.  */
static svn_error_t *
find_dir_entry (skel_t **entry_p,
                skel_t **prev_entry_p,
                skel_t *entries,
                const char *name, 
                trail_t *trail)
{
  skel_t *cur_entry, *prev_entry;

  /* search the entry list for one whose name matches NAME.  */
  for (prev_entry = NULL, cur_entry = entries->children;
       cur_entry != NULL;
       prev_entry = cur_entry, cur_entry = cur_entry->next)
    {
      if (svn_fs__matches_atom (cur_entry->children, name))
        {
          if (svn_fs__list_length (cur_entry) != 2)
            return svn_error_createf
              (SVN_ERR_FS_CORRUPT, 0, 0, trail->pool,
               "directory entry \"%s\" ill-formed", name);
          else
            {
              *entry_p = cur_entry;
              if (prev_entry_p)
                *prev_entry_p = prev_entry;
              return SVN_NO_ERROR;
            }
        }
    }

  /* We never found the entry, but this is non-fatal. */
  *entry_p = (skel_t *) NULL;
  if (prev_entry_p)
    *prev_entry_p = prev_entry;

  return SVN_NO_ERROR;
}


/* Set *ENTRY to the skel for entry NAME in PARENT, as part of TRAIL.
   If no such entry, set *ENTRY to null but do not error.  The entry
   is allocated in TRAIL->pool or in the same pool as PARENT; the
   caller should copy if it cares.  */
static svn_error_t *
dir_entry_from_node (skel_t **entry, 
                     dag_node_t *parent,
                     const char *name,
                     trail_t *trail)
{
  skel_t *entries;

  SVN_ERR (svn_fs__dag_dir_entries_skel (&entries, parent, trail));
  return find_dir_entry (entry, NULL, entries, name, trail);
}


/* Add or set in PARENT a directory entry NAME pointing to ID.
   Allocations are done in TRAIL.

   Assumptions:
   - PARENT is a mutable directory.
   - ID does not refer to an ancestor of parent
   - NAME is a single path component
*/
static svn_error_t *
set_entry (dag_node_t *parent,
           const char *name,
           const svn_fs_id_t *id,
           trail_t *trail)
{
  skel_t *parent_node_rev;
  const char *rep_key, *mutable_rep_key;
  svn_fs_t *fs = parent->fs;

  SVN_ERR (get_node_revision (&parent_node_rev, parent, trail));
  rep_key = apr_pstrndup (trail->pool,
                          (SVN_FS__NR_DATA_KEY (parent_node_rev))->data,
                          (SVN_FS__NR_DATA_KEY (parent_node_rev))->len);

  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, rep_key, fs, trail));

  /* If the parent node already pointed at a mutable representation,
     we don't need to do anything.  But if it didn't, either because
     the parent didn't refer to any rep yet or because it referred to
     an immutable one, we must make the parent refer to the mutable
     rep we just created. */ 
  if (strcmp (rep_key, mutable_rep_key) != 0)
    {
      skel_t *new_node_rev = svn_fs__copy_skel (parent_node_rev, trail->pool);
      (SVN_FS__NR_DATA_KEY (new_node_rev))->data = mutable_rep_key;
      (SVN_FS__NR_DATA_KEY (new_node_rev))->len = strlen (mutable_rep_key);
      SVN_ERR (set_node_revision (parent, new_node_rev, trail));
    }

  /* If the new representation inherited nothing, fill it with a skel
     representing an empty entries list. */ 
  if (rep_key[0] == '\0')
    {
      svn_stream_t *wstream;
      skel_t *empty_list;
      svn_stringbuf_t *empty;
      apr_size_t len;
      
      empty_list = svn_fs__make_empty_list (trail->pool);
      empty = svn_fs__unparse_skel (empty_list, trail->pool);
      wstream = svn_fs__rep_contents_write_stream (fs, mutable_rep_key,
                                                   trail, trail->pool);
      len = empty->len;
      svn_stream_write (wstream, empty->data, &len);
    }
  
  /* Change the entries list. */
  {
    skel_t *entries;
    skel_t *entry;
    svn_string_t str;
    svn_stringbuf_t *unparsed_entries;
    svn_stringbuf_t *id_str = svn_fs_unparse_id (id, trail->pool);

    SVN_ERR (svn_fs__rep_contents (&str, fs, mutable_rep_key, trail));
    entries = svn_fs__parse_skel ((char *) str.data, str.len, trail->pool);
    SVN_ERR (find_dir_entry (&entry, NULL, entries, name, trail));

    if (entry)
      {
        /* Tweak an existing entry. */
        entry->children->next->data = id_str->data;
        entry->children->next->len  = id_str->len;
      }
    else
      {
        /* Create a new entry. */
        skel_t *new_entry_skel;

        new_entry_skel = svn_fs__make_empty_list (trail->pool);
        svn_fs__prepend (svn_fs__str_atom (id_str->data, trail->pool),
                         new_entry_skel);
        svn_fs__prepend (svn_fs__str_atom (name, trail->pool), new_entry_skel);
        svn_fs__prepend (new_entry_skel, entries);
      }

    unparsed_entries = svn_fs__unparse_skel (entries, trail->pool);

    /* Replace the old entries list with the new one. */
    {
      svn_stream_t *wstream;
      apr_size_t len;

      SVN_ERR (svn_fs__rep_contents_clear (fs, mutable_rep_key, trail));
      wstream = svn_fs__rep_contents_write_stream (fs, mutable_rep_key,
                                                   trail, trail->pool);
      len = unparsed_entries->len;
      svn_stream_write (wstream, unparsed_entries->data, &len);
    }
  }

  return SVN_NO_ERROR;
}


/* Make a new entry named NAME in PARENT, as part of TRAIL.  If IS_DIR
 * is true, then the node revision the new entry points to will be a
 * directory, else it will be a file.  The new node will be allocated
 * in TRAIL->pool.  PARENT must be mutable, and must not have an entry
 * named NAME.
 */
static svn_error_t *
make_entry (dag_node_t **child_p,
            dag_node_t *parent,
            const char *name,
            svn_boolean_t is_dir,
            trail_t *trail)
{
  svn_fs_id_t *new_node_id;
  skel_t *new_node_skel;

  /* Make sure that NAME is a single path component. */
  if (! svn_fs__is_single_path_component (name))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to create a node with an illegal name `%s'", name);

  /* Make sure that parent is a directory */
  if (! svn_fs__dag_is_directory (parent))
    return 
      svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to create entry in non-directory parent");
    
  /* Check that the parent is mutable. */
  {
    svn_boolean_t is_mutable;

    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, parent, trail));
    if (! is_mutable)
      {
        return 
          svn_error_createf 
          (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
           "Attempted to clone child of non-mutable node");
      }
  }

  /* Check that parent does not already have an entry named NAME. */
  {
    skel_t *entry_skel;

    SVN_ERR (dir_entry_from_node (&entry_skel, parent, name, trail));
    if (entry_skel)
      {
        return 
          svn_error_createf 
          (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, trail->pool,
           "Attempted to create entry that already exists");
      }
  }

  /* Create the new node's NODE-REVISION skel */
  {
    skel_t *header_skel;
    svn_stringbuf_t *id_str;

    /* Call .toString() on parent's id -- oops!  This isn't Java! */
    id_str = svn_fs_unparse_id (parent->id, trail->pool);
    
    /* Create a new skel for our new node.  If we are making a
       directory, NODE-REVISION is:

          ((TYPE REV) PROP-KEY DATA-KEY)

       where TYPE is `file' or `dir', and REV is initially the empty
       string.

       For new both types, PROP-KEY and DATA-KEY start out as empty
       atoms -- that is, they point to no representations.  They will
       be filled in on demand by other code.  */

    /* First, create the HEADER skel */
    header_skel = svn_fs__make_empty_list (trail->pool);
    svn_fs__prepend (svn_fs__str_atom ("", trail->pool), header_skel);
    if (is_dir)
      svn_fs__prepend (svn_fs__str_atom ("dir", trail->pool), header_skel);
    else
      svn_fs__prepend (svn_fs__str_atom ("file", trail->pool), header_skel);
    
    /* Now, assemble the NODE-REVISION skel. */
    new_node_skel = svn_fs__make_empty_list (trail->pool);
    svn_fs__prepend (svn_fs__str_atom ("", trail->pool), new_node_skel);
    svn_fs__prepend (svn_fs__str_atom ("", trail->pool), new_node_skel);
    svn_fs__prepend (header_skel, new_node_skel);

    /* All done, skel-wise.  We have a NODE-REVISION skel as described
       far above.  Time to actually create our new node in the
       filesystem. */
    SVN_ERR (svn_fs__create_node (&new_node_id, parent->fs,
                                  new_node_skel, trail));
  }

  /* Create a new node_dag_t for our new node */
  SVN_ERR (svn_fs__dag_get_node (child_p,
                                 svn_fs__dag_get_fs (parent),
                                 new_node_id, trail));

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we just created CHILD, so we know it has
     no ancestors (therefore, PARENT cannot be an ancestor of CHILD) */
  SVN_ERR (set_entry (parent, name, svn_fs__dag_get_id (*child_p), trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_dir_entries_skel (skel_t **entries_p,
                              dag_node_t *node,
                              trail_t *trail)
{
  skel_t *node_rev;

  if (! svn_fs__dag_is_directory (node))
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to get entry from non-directory node.");
  
  /* Get the NODE-REVISION for this node. */
  SVN_ERR (get_node_revision (&node_rev, node, trail));
  SVN_ERR (get_dir_entries (entries_p, node->fs, node_rev, trail));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_dir_entries_hash (apr_hash_t **table_p,
                              dag_node_t *node,
                              trail_t *trail)
{
  skel_t *entries, *entry;
  apr_hash_t *table;

  SVN_ERR (svn_fs__dag_dir_entries_skel (&entries, node, trail));

  /* Build a hash table from the directory entry list.  */
  table = apr_hash_make (trail->pool);
  for (entry = entries->children; entry; entry = entry->next)
    {
      skel_t *name_skel = entry->children;
      skel_t *id_skel   = entry->children->next;
      svn_fs_dirent_t *dirent = apr_pcalloc (trail->pool, sizeof (*dirent));

      dirent->name = apr_pstrndup (trail->pool, name_skel->data,
                                   name_skel->len);
      dirent->id = svn_fs_parse_id (id_skel->data, id_skel->len, trail->pool);

      apr_hash_set (table, dirent->name, name_skel->len, dirent);
    }

  *table_p = table;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_set_entry (dag_node_t *node,
                       const char *entry_name,
                       const svn_fs_id_t *id,
                       trail_t *trail)
{
  svn_boolean_t is_mutable;

  /* Check it's a directory. */
  if (! svn_fs__dag_is_directory (node))
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to set entry in non-directory node.");
  
  /* Check it's mutable. */
  SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, node, trail));
  if (! is_mutable)
    return svn_error_create
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to set entry in immutable node.");

  return set_entry (node, entry_name, id, trail);
}



/*** Proplists. ***/

svn_error_t *
svn_fs__dag_get_proplist (skel_t **proplist_p,
                          dag_node_t *node,
                          trail_t *trail)
{
  skel_t *node_rev;
  skel_t *rep_key_skel;
  const char *rep_key;
  svn_string_t propstr;
  
  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR (get_node_revision (&node_rev, node, trail));

  /* Get key skel for properties. */
  rep_key_skel = SVN_FS__NR_PROP_KEY (node_rev);

  /* Get the string associated with the property rep, parsing it as a
     skel. */
  if (rep_key_skel->len == 0)
    {
      *proplist_p = svn_fs__make_empty_list (trail->pool);
      return SVN_NO_ERROR;
    }

  rep_key = apr_pstrndup (trail->pool, rep_key_skel->data, rep_key_skel->len);
  SVN_ERR (svn_fs__rep_contents (&propstr, node->fs, rep_key, trail));
  *proplist_p = svn_fs__parse_skel ((char *) propstr.data,
                                    propstr.len,
                                    trail->pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_set_proplist (dag_node_t *node,
                          skel_t *proplist,
                          trail_t *trail)
{
  skel_t *node_rev;
  const char *orig_rep_key, *mutable_rep_key;
  
  /* Sanity check: this node better be mutable! */
  {
    svn_boolean_t is_mutable;

    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, node, trail));
    if (! is_mutable)
      {
        svn_stringbuf_t *idstr = svn_fs_unparse_id (node->id, node->pool);
        return 
          svn_error_createf 
          (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
           "Can't set_proplist on *immutable* node-revision %s", 
           idstr->data);
      }
  }

  /* Make sure it's a valid proplist. */
  if (! svn_fs__is_valid_proplist (proplist))
    return svn_error_create
      (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
       "svn_fs__dag_set_proplist: Malformed property list.");
  
  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR (get_node_revision (&node_rev, node, trail));

  /* Get the property rep key. */
  orig_rep_key = apr_pstrndup (trail->pool,
                               (SVN_FS__NR_PROP_KEY (node_rev))->data,
                               (SVN_FS__NR_PROP_KEY (node_rev))->len);

  /* Get a mutable version of this rep. */
  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, orig_rep_key,
                                    node->fs, trail));

  /* If we made a new rep, record it in the node revision. */
  if (strcmp (mutable_rep_key, orig_rep_key) != 0)
    {
      (SVN_FS__NR_PROP_KEY (node_rev))->data = mutable_rep_key;
      (SVN_FS__NR_PROP_KEY (node_rev))->len = strlen (mutable_rep_key);
      SVN_ERR (svn_fs__put_node_revision (node->fs, node->id,
                                          node_rev, trail));
    }

  /* Replace the old property list with the new one. */
  {
    svn_stream_t *wstream;
    apr_size_t len;
    svn_stringbuf_t *unparsed_props;

    unparsed_props = svn_fs__unparse_skel (proplist, trail->pool);
    wstream = svn_fs__rep_contents_write_stream (node->fs, mutable_rep_key,
                                                 trail, trail->pool);
    SVN_ERR (svn_fs__rep_contents_clear (node->fs, mutable_rep_key, trail));
    len = unparsed_props->len;
    SVN_ERR (svn_stream_write (wstream, unparsed_props->data, &len));
             
  }

  return SVN_NO_ERROR;
}



/*** Roots. ***/

svn_error_t *
svn_fs__dag_revision_root (dag_node_t **node_p,
                           svn_fs_t *fs,
                           svn_revnum_t rev,
                           trail_t *trail)
{
  svn_fs_id_t *root_id;

  SVN_ERR (svn_fs__rev_get_root (&root_id, fs, rev, trail));
  SVN_ERR (svn_fs__dag_get_node (node_p, fs, root_id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_txn_root (dag_node_t **node_p,
                      svn_fs_t *fs,
                      const char *txn,
                      trail_t *trail)
{
  svn_fs_id_t *root_id, *ignored;
  
  SVN_ERR (svn_fs__get_txn_ids (&root_id, &ignored, fs, txn, trail));
  SVN_ERR (svn_fs__dag_get_node (node_p, fs, root_id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_txn_base_root (dag_node_t **node_p,
                           svn_fs_t *fs,
                           const char *txn,
                           trail_t *trail)
{
  svn_fs_id_t *base_root_id, *ignored;
  
  SVN_ERR (svn_fs__get_txn_ids (&ignored, &base_root_id, fs, txn, trail));
  SVN_ERR (svn_fs__dag_get_node (node_p, fs, base_root_id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_clone_child (dag_node_t **child_p,
                         dag_node_t *parent,
                         const char *name,
                         trail_t *trail)
{
  dag_node_t *cur_entry; /* parent's current entry named NAME */
  svn_fs_id_t *new_node_id; /* node id we'll put into NEW_NODE */

  /* First check that the parent is mutable. */
  {
    svn_boolean_t is_mutable;

    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, parent, trail));

    if (! is_mutable)
    {
      /* return some nasty error */
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
         "Attempted to clone child of non-mutable node");
    }
  }

  /* Make sure that NAME is a single path component. */
  if (! svn_fs__is_single_path_component (name))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to make a child clone with an illegal name `%s'", name);

  /* Find the node named NAME in PARENT's entries list if it exists. */
  SVN_ERR (svn_fs__dag_open (&cur_entry, parent, name, trail));

  {
    svn_boolean_t is_mutable;

    /* Check for mutability in the node we found.  If it's mutable, we
       don't need to clone it. */
    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, cur_entry, trail));
      
    if (is_mutable)
      {
        /* This has already been cloned */
        new_node_id = cur_entry->id;
      }
    else
      {
        skel_t *node_rev;

        /* Go get a fresh NODE-REVISION for current child node. */
        SVN_ERR (get_node_revision (&node_rev, cur_entry, trail));
        
        /* Ensure mutability (a noop if it's already so) */
        node_rev_make_mutable (node_rev);

        /* Do the clone thingy here. */
        SVN_ERR (svn_fs__create_successor (&new_node_id, 
                                           parent->fs, 
                                           cur_entry->id, 
                                           node_rev,
                                           trail));

        /* Replace the ID in the parent's ENTRY list with the ID which
           refers to the mutable clone of this child. */
        SVN_ERR (set_entry (parent, name, new_node_id, trail));
      }
  }

  /* Initialize the youngster. */
  SVN_ERR (svn_fs__dag_get_node (child_p, 
                                 svn_fs__dag_get_fs (parent), 
                                 new_node_id, trail));
  
  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs__dag_clone_root (dag_node_t **root_p,
                        svn_fs_t *fs,
                        const char *svn_txn,
                        trail_t *trail)
{
  svn_fs_id_t *base_root_id, *root_id;
  skel_t *root_skel;      /* Skel contents of the node we'll return. */
  
  /* Get the node ID's of the root directories of the transaction and
     its base revision.  */
  SVN_ERR (svn_fs__get_txn_ids (&root_id, &base_root_id, fs, svn_txn, trail));

  /* Oh, give me a clone...
     (If they're the same, we haven't cloned the transaction's root
     directory yet.)  */
  if (svn_fs__id_eq (root_id, base_root_id)) 
    {
      /* Of my own flesh and bone...
         (Get the NODE-REVISION skel for the base node, and then write
         it back out as the clone.) */
      SVN_ERR (svn_fs__get_node_revision (&root_skel, fs, base_root_id,
                                          trail));

      /* With its Y-chromosome changed to X...
         (Make sure this node is mutable, a noop if it is already.) */
      node_rev_make_mutable (root_skel);

      /* Store it. */
      SVN_ERR (svn_fs__create_successor (&root_id, fs, base_root_id, root_skel,
                                         trail));

      /* ... And when it is grown
       *      Then my own little clone
       *        Will be of the opposite sex!
       */
      SVN_ERR (svn_fs__set_txn_root (fs, svn_txn, root_id, trail));
    }

  /* One way or another, root_id now identifies a cloned root node. */
  SVN_ERR (svn_fs__dag_get_node (root_p, fs, root_id, trail));

  /*
   * (Sung to the tune of "Home, Home on the Range", with thanks to
   * Randall Garrett and Isaac Asimov.)
   */

  return SVN_NO_ERROR;
}


/* Delete the directory entry named NAME from PARENT, as part of
   TRAIL.  PARENT must be mutable.  NAME must be a single path
   component.  If REQUIRE_EMPTY is true and the node being deleted is
   a directory, it must be empty.  

   If return SVN_ERR_FS_NO_SUCH_ENTRY, then there is no entry NAME in
   PARENT.  */
static svn_error_t *
delete_entry (dag_node_t *parent,
              const char *name,
              svn_boolean_t require_empty,
              trail_t *trail)
{
  skel_t *parent_node_rev;
  const char *rep_key, *mutable_rep_key;
  svn_fs_t *fs = parent->fs;
  skel_t *prev_entry, *entry;
  skel_t *entries;
  svn_string_t str;
  svn_fs_id_t *id;
  dag_node_t *node; 

  /* Make sure parent is a directory. */
  if (! svn_fs__dag_is_directory (parent))
    return 
      svn_error_createf
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, parent->pool,
       "Attempted to delete entry `%s' from *non*-directory node.",
       name);    

  /* Make sure parent is mutable. */
  {
    svn_boolean_t is_mutable;
    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, parent, trail));
    if (! is_mutable)
      {
        return 
          svn_error_createf
          (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, parent->pool,
           "Attempted to delete entry `%s' from *immutable* directory node.",
           name);
      }
  }

  /* Make sure that NAME is a single path component. */
  if (! svn_fs__is_single_path_component (name))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to delete a node with an illegal name `%s'", name);

  /* Get a fresh NODE-REVISION for this node. */
  SVN_ERR (get_node_revision (&parent_node_rev, parent, trail));

  /* Get the key for this node's data representation. */
  rep_key = apr_pstrndup (trail->pool,
                          (SVN_FS__NR_DATA_KEY (parent_node_rev))->data,
                          (SVN_FS__NR_DATA_KEY (parent_node_rev))->len);

  /* No REP_KEY means no representation, and no representation means
     no data, and no data means no entries...there's nothing here to
     delete! */
  if (rep_key[0] == '\0')
      return svn_error_createf 
        (SVN_ERR_FS_NO_SUCH_ENTRY, 0, NULL, trail->pool,
         "Delete failed--directory has no entry `%s'", name);

  /* Ensure we have a key to a mutable representation of the entries
     list.  We'll have to update the NODE-REVISION if it points to an
     immutable version.  */
  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, rep_key, fs, trail));
  if (strcmp (rep_key, mutable_rep_key) != 0)
    {
      skel_t *new_node_rev = svn_fs__copy_skel (parent_node_rev, trail->pool);
      (SVN_FS__NR_DATA_KEY (new_node_rev))->data = mutable_rep_key;
      (SVN_FS__NR_DATA_KEY (new_node_rev))->len = strlen (mutable_rep_key);
      SVN_ERR (set_node_revision (parent, new_node_rev, trail));
    }

  /* Read the representation, then use it to get the string that holds
     the entries list.  Parse that list into a browsable skel. */
  SVN_ERR (svn_fs__rep_contents (&str, fs, mutable_rep_key, trail));
  entries = svn_fs__parse_skel ((char *) str.data, str.len, trail->pool);

  /* Find NAME in the ENTRIES skel.  */
  SVN_ERR (find_dir_entry (&entry, &prev_entry, entries, name, trail));
  if (! entry)
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_ENTRY, 0, NULL, trail->pool,
       "Delete failed--directory has no entry `%s'", name);

  /* Use the ID of this ENTRY to get the entry's node.  If the node we
     get is a directory, make sure it meets up to our emptiness
     standards (as determined by REQUIRE_EMPTY).  */
  id = svn_fs_parse_id (entry->children->next->data, 
                        entry->children->next->len,
                        trail->pool);
  SVN_ERR (svn_fs__dag_get_node (&node, parent->fs, id, trail));
  if (svn_fs__dag_is_directory (node))
    {
      skel_t *entries_here;
      SVN_ERR (svn_fs__dag_dir_entries_skel (&entries_here, node, trail));

      if (require_empty && (svn_fs__list_length (entries_here)))
        {
          return svn_error_createf
            (SVN_ERR_FS_DIR_NOT_EMPTY, 0, NULL, parent->pool,
             "Attempt to delete non-empty directory `%s'.", name);
        }
    }

  /* If mutable, remove it and any mutable children from db. */
  SVN_ERR (svn_fs__dag_delete_if_mutable (parent->fs, id, trail));
        
  /* Just "lose" this entry by setting the previous entry's
       next ptr to the current entry's next ptr. */
  if (! prev_entry)
    entries->children = entry->next;
  else
    prev_entry->next = entry->next;

  /* Replace the old entries list with the new one. */
  {
    svn_stream_t *ws;
    svn_stringbuf_t *unparsed_entries;
    apr_size_t len;

    unparsed_entries = svn_fs__unparse_skel (entries, trail->pool);

    SVN_ERR (svn_fs__rep_contents_clear (fs, mutable_rep_key, trail));
    ws = svn_fs__rep_contents_write_stream (fs, mutable_rep_key,
                                            trail, trail->pool);
    len = unparsed_entries->len;
    SVN_ERR (svn_stream_write (ws, unparsed_entries->data, &len));
  }
    
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_delete (dag_node_t *parent,
                    const char *name,
                    trail_t *trail)
{
  return delete_entry (parent, name, TRUE, trail);
}


svn_error_t *
svn_fs__dag_delete_tree (dag_node_t *parent,
                         const char *name,
                         trail_t *trail)
{
  return delete_entry (parent, name, FALSE, trail);
}


svn_error_t *
svn_fs__dag_delete_if_mutable (svn_fs_t *fs,
                               svn_fs_id_t *id,
                               trail_t *trail)
{
  svn_boolean_t is_mutable;
  dag_node_t *node;
  skel_t *node_rev;

  SVN_ERR (svn_fs__dag_get_node (&node, fs, id, trail));

  /* If immutable, do nothing and return immediately. */
  SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, node, trail));
  if (! is_mutable)
    return SVN_NO_ERROR;

  /* Else it's mutable.  Recurse on directories... */
  if (svn_fs__dag_is_directory (node))
    {
      skel_t *entries, *entry;
      SVN_ERR (svn_fs__dag_dir_entries_skel (&entries, node, trail));
          
      for (entry = entries->children; entry; entry = entry->next)
        {
          skel_t *id_skel = entry->children->next;
          svn_fs_id_t *this_id
            = svn_fs_parse_id (id_skel->data, id_skel->len, trail->pool);

          SVN_ERR (svn_fs__dag_delete_if_mutable (fs, this_id, trail));
        }
    }

  /* ... then delete the node itself, after deleting any mutable
     representations and strings it points to. */

  SVN_ERR (svn_fs__get_node_revision (&node_rev, fs, id, trail));

  /* Delete any mutable property representation. */
  {
    const char *prop_rep_key;
    prop_rep_key = apr_pstrndup (trail->pool,
                                 (SVN_FS__NR_PROP_KEY (node_rev))->data,
                                 (SVN_FS__NR_PROP_KEY (node_rev))->len);
    if (prop_rep_key[0] != '\0')
      SVN_ERR (svn_fs__delete_rep_if_mutable (fs, prop_rep_key, trail));
  }
  
  /* Delete any mutable data representation. */
  {
    const char *data_rep_key;
    data_rep_key = apr_pstrndup (trail->pool,
                                 (SVN_FS__NR_DATA_KEY (node_rev))->data,
                                 (SVN_FS__NR_DATA_KEY (node_rev))->len);
    if (data_rep_key[0] != '\0')
      SVN_ERR (svn_fs__delete_rep_if_mutable (fs, data_rep_key, trail));
  }

  /* Delete any mutable edit representation. */
  if (SVN_FS__NR_EDIT_KEY (node_rev))
    {
      const char *edit_rep_key;
      edit_rep_key = apr_pstrndup (trail->pool,
                                   (SVN_FS__NR_EDIT_KEY (node_rev))->data,
                                   (SVN_FS__NR_EDIT_KEY (node_rev))->len);
      if (edit_rep_key[0] != '\0')
        SVN_ERR (svn_fs__delete_rep_if_mutable (fs, edit_rep_key, trail));
    }

  /* Delete the node revision itself. */
  SVN_ERR (svn_fs__delete_node_revision (fs, id, trail));
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_make_file (dag_node_t **child_p,
                       dag_node_t *parent,
                       const char *name,
                       trail_t *trail)
{
  /* Call our little helper function */
  return make_entry (child_p, parent, name, FALSE, trail);
}


svn_error_t *
svn_fs__dag_make_dir (dag_node_t **child_p,
                      dag_node_t *parent,
                      const char *name,
                      trail_t *trail)
{
  /* Call our little helper function */
  return make_entry (child_p, parent, name, TRUE, trail);
}


/* ### somebody todo: figure out why this *reaaaaaaally* exists.  It
   has no callers (though kfogel has some speculation about a possible
   use (see tree.c:merge) */
svn_error_t *
svn_fs__dag_link (dag_node_t *parent,
                  dag_node_t *child,
                  const char *name,
                  trail_t *trail)
{
  /* Make sure that parent is a directory */
  if (! svn_fs__dag_is_directory (parent))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_DIRECTORY, 0, NULL, trail->pool,
       "Attempted to create entry in non-directory parent");
    
  {
    svn_boolean_t is_mutable;

    /* Make sure parent is mutable */
    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, parent, trail));
    if (! is_mutable)
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
         "Can't add a link from an immutable parent");

    /* Make sure child is IMmutable */
    SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, child, trail));
    if (is_mutable)
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
         "Can't add a link to a mutable child");
  }

  /* Make sure that NAME is a single path component. */
  if (! svn_fs__is_single_path_component (name))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to link to a node with an illegal name `%s'", name);

  {
    skel_t *entry_skel;

    /* Verify that this parent node does not already have an entry named
       NAME. */
    SVN_ERR (dir_entry_from_node (&entry_skel, parent, name, trail));
    if (entry_skel)
      return 
        svn_error_createf 
        (SVN_ERR_FS_ALREADY_EXISTS, 0, NULL, trail->pool,
         "Attempted to create entry that already exists");
  }

  /* We can safely call set_entry because we already know that
     PARENT is mutable, and we know that CHILD is immutable (since
     every parent of a mutable node is mutable itself, we know that
     CHILD can't be equal to, or a parent of, PARENT).  */
  SVN_ERR (set_entry (parent, name, svn_fs__dag_get_id (child), trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_get_contents (svn_stream_t **contents,
                          dag_node_t *file,
                          apr_pool_t *pool,
                          trail_t *trail)
{ 
  skel_t *node_rev;
  const char *rep_key;

  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to get textual contents of a *non*-file node.");
  
  /* Go get a fresh node-revision for FILE. */
  SVN_ERR (get_node_revision (&node_rev, file, trail));

  /* Get the rep key. */
  if ((SVN_FS__NR_DATA_KEY (node_rev))->len != 0)
    {
      rep_key = apr_pstrndup (trail->pool,
                              (SVN_FS__NR_DATA_KEY (node_rev))->data,
                              (SVN_FS__NR_DATA_KEY (node_rev))->len);
    }
  else
    rep_key = NULL;

  /* Our job is to _return_ a stream on the file's contents, so the
     stream has to be trail-independent.  Here, we pass NULL to tell
     the stream that we're not providing it a trail that lives across
     reads.  This means the stream will do each read in a one-off,
     temporary trail.  */

  *contents = svn_fs__rep_contents_read_stream (file->fs, rep_key,
                                                0, NULL, pool);

  /* Note that we're not registering any `close' func, because there's
     nothing to cleanup outside of our trail.  When the trail is
     freed, the stream/baton will be too. */ 

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_file_length (apr_size_t *length,
                         dag_node_t *file,
                         trail_t *trail)
{ 
  skel_t *node_rev;
  
  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to get length of a *non*-file node.");

  /* Go get a fresh node-revision for FILE. */
  SVN_ERR (get_node_revision (&node_rev, file, trail));

  /* Seg-fault protection. */
  assert (svn_fs__list_length (node_rev) >= 3);

  /* Get the rep key, get the size through that. */
  {
    const char *rep_key = apr_pstrndup (trail->pool,
                                        (SVN_FS__NR_DATA_KEY (node_rev))->data,
                                        (SVN_FS__NR_DATA_KEY (node_rev))->len);

    SVN_ERR (svn_fs__rep_contents_size (length, file->fs, rep_key, trail));
  }

  return SVN_NO_ERROR;
}




svn_error_t *
svn_fs__dag_get_edit_stream (svn_stream_t **contents,
                             dag_node_t *file,
                             apr_pool_t *pool,
                             trail_t *trail)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  skel_t *node_rev_skel;
  svn_boolean_t is_mutable;
  const char *mutable_rep_key;
  svn_stream_t *ws;

  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to set textual contents of a *non*-file node.");
  
  /* Make sure our node is mutable. */
  SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, file, trail));
  if (! is_mutable)
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Attempted to set textual contents of an immutable node.");

  /* Get the node revision. */
  SVN_ERR (get_node_revision (&node_rev_skel, file, trail));

  /* If this node already has an EDIT-DATA-KEY, destroy the data
     associated with that key.  ### todo: should this return an error
     instead?  */
  if (SVN_FS__NR_EDIT_KEY (node_rev_skel))
    {
      const char *rep_key;
      rep_key = apr_pstrndup (trail->pool,
                              (SVN_FS__NR_EDIT_KEY (node_rev_skel))->data,
                              (SVN_FS__NR_EDIT_KEY (node_rev_skel))->len);
      SVN_ERR (svn_fs__delete_rep_if_mutable (fs, rep_key, trail));
    }

  /* Now, let's ensure that we have a new EDIT-DATA-KEY available for
     use. */
  SVN_ERR (svn_fs__get_mutable_rep (&mutable_rep_key, NULL, fs, trail));
  
  /* We made a new rep, so update the node revision. */
  svn_fs__append (svn_fs__str_atom (mutable_rep_key, trail->pool), 
                  node_rev_skel);
  SVN_ERR (svn_fs__put_node_revision (fs, file->id, node_rev_skel, trail));

  /* Return a writable stream with which to set new contents. */
  ws = svn_fs__rep_contents_write_stream (fs, mutable_rep_key, NULL, pool);
  *contents = ws;

  return SVN_NO_ERROR;
}



svn_error_t *
svn_fs__dag_finalize_edits (dag_node_t *file,
                            trail_t *trail)
{
  svn_fs_t *fs = file->fs;   /* just for nicer indentation */
  skel_t *node_rev_skel;
  svn_boolean_t is_mutable;
  const char *old_data_key;

  /* Make sure our node is a file. */
  if (! svn_fs__dag_is_file (file))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_FILE, 0, NULL, trail->pool,
       "Attempted to set textual contents of a *non*-file node.");
  
  /* Make sure our node is mutable. */
  SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, file, trail));
  if (! is_mutable)
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_MUTABLE, 0, NULL, trail->pool,
       "Attempted to set textual contents of an immutable node.");

  /* Get the node revision. */
  SVN_ERR (get_node_revision (&node_rev_skel, file, trail));

  /* If this node has no EDIT-DATA-KEY, this is a no-op.  ### todo:
     should this return an error? */
  if (! SVN_FS__NR_EDIT_KEY (node_rev_skel))
    return SVN_NO_ERROR;

  /* Now, we want to delete the old representation and replace it with
     the new.  Of course, we don't actually delete anything until
     everything is being properly referred to by the node-revision
     skel. */
  old_data_key = apr_pstrndup (trail->pool,
                               (SVN_FS__NR_DATA_KEY (node_rev_skel))->data,
                               (SVN_FS__NR_DATA_KEY (node_rev_skel))->len);
  (SVN_FS__NR_PROP_KEY (node_rev_skel))->next = 
                                       SVN_FS__NR_EDIT_KEY (node_rev_skel);
  SVN_ERR (svn_fs__put_node_revision (fs, file->id, node_rev_skel, trail));
  
  /* Only *now* can we safely destroy the old representation (if it
     even existed in the first place). */
  if (old_data_key && *old_data_key)
    SVN_ERR (svn_fs__delete_rep_if_mutable (fs, old_data_key, trail));

  return SVN_NO_ERROR;
}



dag_node_t *
svn_fs__dag_dup (dag_node_t *node,
                 trail_t *trail)
{
  /* Allocate our new node. */
  dag_node_t *new_node = apr_pcalloc (trail->pool, sizeof (*new_node));

  new_node->fs = node->fs;
  new_node->pool = trail->pool;
  new_node->id = svn_fs__id_copy (node->id, node->pool);
  new_node->kind = node->kind;

  /* Leave new_node->node_revision zero for now, so it'll get read in.
     We can get fancy and duplicate node's cache later.  */

  return new_node;
}


svn_error_t *
svn_fs__dag_open (dag_node_t **child_p,
                  dag_node_t *parent,
                  const char *name,
                  trail_t *trail)
{
  skel_t *entry_skel;
  svn_fs_id_t *node_id;
  
  SVN_ERR (dir_entry_from_node (&entry_skel, parent, name, trail));
  if (! entry_skel)
    {
      /* return some other nasty error */
      return 
        svn_error_createf 
        (SVN_ERR_FS_NOT_FOUND, 0, NULL, trail->pool,
         "Attempted to open non-existant child node \"%s\"", name);
    }
  
  /* Make sure that NAME is a single path component. */
  if (! svn_fs__is_single_path_component (name))
    return 
      svn_error_createf 
      (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, 0, NULL, trail->pool,
       "Attempted to open node with an illegal name `%s'", name);

  /* Get the node id this entry points to. */
  {
    skel_t *id_skel = entry_skel->children->next;
    node_id = svn_fs_parse_id (id_skel->data, 
                               id_skel->len,
                               trail->pool);
  }

  SVN_ERR (svn_fs__dag_get_node (child_p, 
                                 svn_fs__dag_get_fs (parent),
                                 node_id, trail));

  return SVN_NO_ERROR;
}




/* Rename the node named FROM_NAME in FROM_DIR to TO_NAME in TO_DIR,
   as part of TRAIL.  FROM_DIR and TO_DIR must both be mutable; the
   node being renamed may be either mutable or immutable.  FROM_NAME
   and TO_NAME must be single path components; they cannot be
   slash-separated directory paths.

   This function ensures that the rename does not create a cyclic
   directory structure, by checking that TO_DIR is not a child of
   FROM_DIR.  */
svn_error_t *
svn_fs__dag_rename (dag_node_t *from_dir, 
                    const char *from_name,
                    dag_node_t *to_dir, 
                    const char *to_name,
                    trail_t *trail)
{
  abort();
  /* NOTREACHED */
  return NULL;
}



svn_error_t *
svn_fs__dag_copy (dag_node_t *to_node,
                  const char *entry,
                  dag_node_t *from_node,
                  svn_boolean_t preserve_history,
                  svn_revnum_t from_rev,
                  const char *from_path,
                  trail_t *trail)
{
  const svn_fs_id_t *id;

  if (preserve_history)
    {
      skel_t *from_node_rev, *to_node_rev;
      
      /* Make a copy of the original node revision skel. */
      SVN_ERR (get_node_revision (&from_node_rev, from_node, trail));
      to_node_rev = svn_fs__copy_skel (from_node_rev, trail->pool);
      
      /* Set the copy option in the new skel. */
      {
        skel_t *copy_opt;
        char *rev_str = apr_psprintf (trail->pool, "%" SVN_REVNUM_T_FMT,
                                      from_rev);
        
        copy_opt = svn_fs__make_empty_list (trail->pool);
        svn_fs__prepend (svn_fs__str_atom (from_path, trail->pool), copy_opt);
        svn_fs__prepend (svn_fs__str_atom (rev_str, trail->pool), copy_opt);
        svn_fs__prepend (svn_fs__str_atom ("copy", trail->pool), copy_opt);
        
        /* If the from_node was itself a copy, we don't want to preserve
           that copy history in the new node. */
        if (SVN_FS__NR_HDR_COPY (SVN_FS__NR_HEADER (to_node_rev)))
          SVN_FS__NR_HDR_COPY (SVN_FS__NR_HEADER (to_node_rev)) = NULL;
        
        /* Set or replace with the new copy history. */
        svn_fs__append (copy_opt, SVN_FS__NR_HEADER (to_node_rev));
      }
      
      /* The new node doesn't know what revision it was created in yet. */
      (SVN_FS__NR_HDR_REV (SVN_FS__NR_HEADER (to_node_rev)))->len = 0;
      
      /* Store the new node under a new id in the filesystem.
         Note: The id is not related to from_node's id.  This is
         because the new node is not a next revision of from_node, but
         rather a copy of it.  Since for copies, all the ancestry
         information we care about is recorded in the copy options, there
         is no reason to make the id's be related.  */
      SVN_ERR (svn_fs__create_node ((svn_fs_id_t **) &id,
                                    to_node->fs, to_node_rev, trail));
    }
  else  /* don't preserve history */
    {
      id = svn_fs__dag_get_id (from_node);
    }
      
  /* Set the entry in to_node to the new id. */
  SVN_ERR (svn_fs__dag_set_entry (to_node, entry, id, trail));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_copied_from (svn_revnum_t *rev_p,
                         const char **path_p,
                         dag_node_t *node,
                         trail_t *trail)
{
  skel_t *node_rev;
  skel_t *copy_skel;

  SVN_ERR (get_node_revision (&node_rev, node, trail));
  copy_skel = SVN_FS__NR_HDR_COPY (SVN_FS__NR_HEADER (node_rev));

  if (copy_skel)
    {
      char *rev_str = apr_pstrndup (trail->pool,
                                    copy_skel->children->next->data,
                                    copy_skel->children->next->len);

      *rev_p = SVN_STR_TO_REV (rev_str);
      *path_p = apr_pstrndup (trail->pool,
                              copy_skel->children->next->next->data,
                              copy_skel->children->next->next->len);
    }
  else
    {
      *rev_p = SVN_INVALID_REVNUM;
      *path_p = NULL;
    }

  return SVN_NO_ERROR;
}



/*** Committing ***/

/* If NODE is mutable, make it immutable by setting it's revision to
   REV and immutating any mutable representations referred to by NODE,
   as part of TRAIL.  NODE's revision skel is not reallocated, however
   its data field will be allocated in TRAIL->pool.

   If NODE is immutable, do nothing.

   Callers beware: if NODE is a directory, this does _not_ check that
   all the directory's children are immutable.  You probably meant to
   use stabilize_node().  */
static svn_error_t *
make_node_immutable (dag_node_t *node, 
                     svn_revnum_t rev, 
                     trail_t *trail)
{
  skel_t *node_rev;

  /* Go get a fresh NODE-REVISION for this node. */
  SVN_ERR (get_node_revision (&node_rev, node, trail));

  /* If this node revision is immutable already, do nothing. */
  if (! node_rev_is_mutable (node_rev))
    return SVN_NO_ERROR;

  /* Make sure there is no outstanding EDIT-DATA-KEY associated with
     this node.  If there is, we have a problem. */
  if (SVN_FS__NR_EDIT_KEY (node_rev))
    {
      svn_stringbuf_t *id_str = svn_fs_unparse_id (node->id, trail->pool);
      return svn_error_createf 
        (SVN_ERR_FS_CORRUPT, 0, NULL, trail->pool,
         "make_node_immutable: node `%s' has unfinished edits",
         id_str->data);
    }

  /* Copy the node_rev skel into our subpool. */
  node_rev = svn_fs__copy_skel (node_rev, trail->pool);

  /* The PROP-KEY is the second element. */
  {
    const char *prop_rep_key;
  
    prop_rep_key = apr_pstrndup (trail->pool,
                                 (SVN_FS__NR_PROP_KEY (node_rev))->data,
                                 (SVN_FS__NR_PROP_KEY (node_rev))->len);
    if (prop_rep_key && prop_rep_key[0] != '\0')
      SVN_ERR (svn_fs__make_rep_immutable (node->fs, prop_rep_key, trail));
  }

  /* The DATA-KEY is the third element. */
  {
    const char *data_rep_key;

    data_rep_key = apr_pstrndup (trail->pool,
                                 (SVN_FS__NR_DATA_KEY (node_rev))->data,
                                 (SVN_FS__NR_DATA_KEY (node_rev))->len);
    if (data_rep_key && data_rep_key[0] != '\0')
      SVN_ERR (svn_fs__make_rep_immutable (node->fs, data_rep_key, trail));
  }

  /* Update the revision field with REV, and store the updated
     node-revision.  */
  {
    char *revstr;

    revstr = apr_psprintf (trail->pool, "%" SVN_REVNUM_T_FMT, rev);
    (SVN_FS__NR_HDR_REV (SVN_FS__NR_HEADER (node_rev)))->data = revstr;
    (SVN_FS__NR_HDR_REV (SVN_FS__NR_HEADER (node_rev)))->len = strlen (revstr);
    SVN_ERR (set_node_revision (node, node_rev, trail));
  }


  return SVN_NO_ERROR;
}


/* If NODE is mutable, make it immutable (after recursively
   stabilizing all of its mutable descendants), by setting it's
   revision to REV and immutating any mutable representations referred
   to by NODE, as part of TRAIL.  NODE's revision skel is not
   reallocated, however its data field will be allocated in
   TRAIL->pool.

   If NODE is immutable, do nothing. */
static svn_error_t *
stabilize_node (dag_node_t *node, svn_revnum_t rev, trail_t *trail)
{
  svn_boolean_t is_mutable;

  SVN_ERR (svn_fs__dag_check_mutable (&is_mutable, node, trail));

  if (is_mutable)
    {
      if (svn_fs__dag_is_directory (node))
        {
          skel_t *entries;
          skel_t *entry;
          
          SVN_ERR (svn_fs__dag_dir_entries_skel (&entries, node, trail));
          
          /* Each entry looks like (NAME ID).  */
          for (entry = entries->children; entry; entry = entry->next)
            {
              dag_node_t *child;
              skel_t *id_skel = entry->children->next;
              svn_fs_id_t *id
                = svn_fs_parse_id (id_skel->data, id_skel->len, trail->pool);
              
              SVN_ERR (svn_fs__dag_get_node (&child, node->fs, id, trail));
              SVN_ERR (stabilize_node (child, rev, trail));
            }
        }
      else if (svn_fs__dag_is_file (node))
        ;
      else
        abort ();
      
      SVN_ERR (make_node_immutable (node, rev, trail));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_commit_txn (svn_revnum_t *new_rev,
                        svn_fs_t *fs,
                        const char *svn_txn,
                        trail_t *trail)
{
  dag_node_t *root;

  SVN_ERR (svn_fs__dag_txn_root (&root, fs, svn_txn, trail));

  /* Add new revision entry to `revisions' table, copying the
     transaction's property list.  */
  {
    svn_fs__revision_t revision;
    svn_fs__transaction_t *transaction;
    
    SVN_ERR (svn_fs__get_txn (&transaction, fs, svn_txn, trail));
    revision.id = root->id;
    revision.proplist = transaction->proplist;
    SVN_ERR (svn_fs__put_rev (new_rev, fs, &revision, trail));
  }

  /* Set a date on the commit.  We wait until now to fetch the date,
     so it's definitely newer than any previous revision's date. */
  {
    svn_string_t date;

    date.data = svn_time_to_nts (apr_time_now(), trail->pool);
    date.len = strlen (date.data);

    SVN_ERR (svn_fs__set_rev_prop (fs, *new_rev, SVN_PROP_REVISION_DATE, &date,
				   trail));
  }

  /* Recursively stabilize from ROOT using the new revision.  */
  SVN_ERR (stabilize_node (root, *new_rev, trail));

  /* Delete transaction from `transactions' table.  */
  SVN_ERR (svn_fs__delete_txn (fs, svn_txn, trail));

  return SVN_NO_ERROR;
}



/*** Comparison. ***/

svn_error_t *
svn_fs__things_different (int *props_changed,
                          int *contents_changed,
                          dag_node_t *node1,
                          dag_node_t *node2,
                          trail_t *trail)
{
  skel_t *node_rev1, *node_rev2;

  /* If we have no place to store our results, don't bother doing
     anything. */
  if (! props_changed && ! contents_changed)
    return SVN_NO_ERROR;

  /* The the node revision skels for these two nodes. */
  SVN_ERR (get_node_revision (&node_rev1, node1, trail));
  SVN_ERR (get_node_revision (&node_rev2, node2, trail));

  /* Compare property keys. */
  if (props_changed != NULL)
    {
      if (svn_fs__skels_are_equal 
          (SVN_FS__NR_PROP_KEY (node_rev1), SVN_FS__NR_PROP_KEY (node_rev2)))
        *props_changed = 0;
      else
        *props_changed = 1;
    }

  /* Compare contents keys. */
  if (contents_changed != NULL)
    {
      if (svn_fs__skels_are_equal 
          (SVN_FS__NR_DATA_KEY (node_rev1), SVN_FS__NR_DATA_KEY (node_rev2)))
        *contents_changed = 0;
      else
        *contents_changed = 1;
    }
  
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
