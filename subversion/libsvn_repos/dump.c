/* dump.c --- writing filesystem contents into a portable 'dumpfile' format.
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


#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_hash.h"
#include "svn_path.h"


/* RFC822-style headers that exist in the dumpfile. */
#define SVN_REPOS_DUMPFILE_MAGIC_HEADER            "SVN-fs-dump-format-version"
#define SVN_REPOS_DUMPFILE_FORMAT_VERSION           1

#define SVN_REPOS_DUMPFILE_REVISION_NUMBER           "Revision-number"
#define SVN_REPOS_DUMPFILE_REVISION_CONTENT_CHECKSUM "Revision-content-md5"
#define SVN_REPOS_DUMPFILE_REVISION_CONTENT_LENGTH   "Content-length"

#define SVN_REPOS_DUMPFILE_NODE_PATH                 "Node-path"
#define SVN_REPOS_DUMPFILE_NODE_KIND                 "Node-kind"
#define SVN_REPOS_DUMPFILE_NODE_ACTION               "Node-action"
#define SVN_REPOS_DUMPFILE_NODE_COPIED_FROM          "Node-copied-from"
#define SVN_REPOS_DUMPFILE_NODE_COPY_SOURCE_CHECKSUM "Node-copy-source-checksum"
#define SVN_REPOS_DUMPFILE_NODE_CONTENT_CHECKSUM     "Node-content-md5"
#define SVN_REPOS_DUMPFILE_NODE_CONTENT_LENGTH       "Content-length"


/*----------------------------------------------------------------------*/

/** A variant of our hash-writing routine in libsvn_subr;  this one
    writes to a stringbuf instead of a file, and outputs PROPS-END
    instead of END. **/

static void
write_hash_to_stringbuf (apr_hash_t *hash, 
                         apr_size_t (*unpack_func) 
                         (char **unpacked_data,
                          void *val),
                         svn_stringbuf_t **strbuf,
                         apr_pool_t *pool)
{
  apr_hash_index_t *this;      /* current hash entry */
  char buf[SVN_KEYLINE_MAXLEN];

  *strbuf = svn_stringbuf_create ("", pool);

  for (this = apr_hash_first (pool, hash); this; this = apr_hash_next (this))
    {
      const void *key;
      void *val;
      apr_ssize_t keylen;
      size_t vallen;
      int bytes_used;
      char *valstring;

      /* Get this key and val. */
      apr_hash_this (this, &key, &keylen, &val);

      /* Output name length, then name. */

      svn_stringbuf_appendbytes (*strbuf, "K ", 2);

      sprintf (buf, "%" APR_SSIZE_T_FMT "%n", keylen, &bytes_used);
      svn_stringbuf_appendbytes (*strbuf, buf, bytes_used);
      svn_stringbuf_appendbytes (*strbuf, "\n", 1);

      svn_stringbuf_appendbytes (*strbuf, (char *) key, keylen);
      svn_stringbuf_appendbytes (*strbuf, "\n", 1);

      /* Output value length, then value. */

      vallen = (size_t) (*unpack_func) (&valstring, val); /* secret decoder! */
      svn_stringbuf_appendbytes (*strbuf, "V ", 2);

      sprintf (buf, "%ld%n", (long int) vallen, &bytes_used);
      svn_stringbuf_appendbytes (*strbuf,  buf, bytes_used);
      svn_stringbuf_appendbytes (*strbuf, "\n", 1);

      svn_stringbuf_appendbytes (*strbuf, valstring, vallen);
      svn_stringbuf_appendbytes (*strbuf, "\n", 1);
    }

  svn_stringbuf_appendbytes (*strbuf, "PROPS-END\n", 10);
}


/*----------------------------------------------------------------------*/

/** An editor which dumps node-data in 'dumpfile format' to a file. **/

/* Look, mom!  No file batons! */

struct edit_baton
{
  svn_stringbuf_t *path;        /* this will almost always be "/" */
  apr_file_t *file;
  svn_revnum_t rev;
  svn_fs_t *fs;              
  svn_fs_root_t *fs_root;

  /* reusable buffer for writing file contents */
  char buffer[SVN_STREAM_CHUNK_SIZE];
  apr_size_t bufsize;
};

struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  svn_stringbuf_t *path;        /* the absolute path to this directory */
  svn_boolean_t added;
  svn_boolean_t written_out;

  /* hash of paths that need to be deleted, though some -might- be
     replaced.  maps const char * paths to this dir_baton.  (they're
     full paths, because that's what the editor driver gives us.  but
     really, they're all within this directory.) */
  apr_hash_t *deleted_entries;

  /* pool to be used for deleting the hash items */
  apr_pool_t *pool;
};


static struct dir_baton *
make_dir_baton (const char *path,
                void *edit_baton,
                void *parent_dir_baton,
                svn_boolean_t added,
                apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *pb = parent_dir_baton;
  struct dir_baton *new_db = apr_pcalloc (pool, sizeof (*new_db));
  svn_stringbuf_t *full_path = svn_stringbuf_dup (eb->path, pool);

  /* A path relative to nothing?  I don't think so. */
  if (path && (! pb))
    abort();

  /* Construct the full path of this node. */
  if (pb)
    svn_path_add_component_nts (full_path, path);

  new_db->edit_baton = eb;
  new_db->parent_dir_baton = pb;
  new_db->path = full_path;
  new_db->added = added;
  new_db->written_out = FALSE;
  new_db->deleted_entries = apr_hash_make (pool);
  new_db->pool = pool;
  
  return new_db;
}

enum node_action
{
  node_action_change,
  node_action_add,
  node_action_delete,
  node_action_replace
};

/* The helper function called by add_* and open_* -- does all the work
   of writing a node record.
   
   Write out a node record for PATH of type KIND under FS_ROOT.
   ACTION describes what is happening to the node (see enum node_action).
   Write record to already-open FILE, using BUFFER to write in chunks.
  */
static svn_error_t *
dump_node (svn_fs_root_t *fs_root,
           const char *path,    /* an absolute path. */
           enum svn_node_kind kind,
           enum node_action action,
           apr_file_t *file,
           void *buffer,
           apr_size_t bufsize,
           apr_pool_t *pool)
{
  svn_stringbuf_t *propstring;
  apr_hash_t *prophash;
  apr_off_t textlen;
  apr_size_t content_length = 0;

  /* Write out metadata headers for this file node. */
  apr_file_printf (file, 
                   SVN_REPOS_DUMPFILE_NODE_PATH ": %s\n", path);

  if (kind == svn_node_file)
    apr_file_printf (file, 
                     SVN_REPOS_DUMPFILE_NODE_KIND ": file\n");
  else if (kind == svn_node_dir)
    apr_file_printf (file, 
                     SVN_REPOS_DUMPFILE_NODE_KIND ": dir\n");

  if (action == node_action_change)
    {
      apr_file_printf (file, 
                       SVN_REPOS_DUMPFILE_NODE_ACTION ": change\n");  
    }
  else if (action == node_action_replace)
    {
      apr_file_printf (file, 
                       SVN_REPOS_DUMPFILE_NODE_ACTION ": replace\n");  
    }
  else if (action == node_action_delete)
    {
      apr_file_printf (file, 
                       SVN_REPOS_DUMPFILE_NODE_ACTION ": delete\n");  

      /* Get out!  We're done! */
      return SVN_NO_ERROR;
    }
  else if (action == node_action_add)
    {
      const char *copyfrom_path;
      svn_revnum_t copyfrom_rev;

      apr_file_printf (file, 
                       SVN_REPOS_DUMPFILE_NODE_ACTION ": add\n");

      /* ### Does this node have a copyfrom history?  Really,
         dir_delta should be sending it to add_file or add_dir -- but
         it doesn't yet.  So we manually make a check. */
      SVN_ERR (svn_fs_copied_from (&copyfrom_rev, &copyfrom_path,
                                   fs_root, path, pool));

      /* ### someday put a copyfrom-sources-checksum in fb. */

      if (copyfrom_path != NULL)
        {
          apr_file_printf (file, 
                           SVN_REPOS_DUMPFILE_NODE_COPIED_FROM 
                           ": %" SVN_REVNUM_T_FMT ", %s\n",
                           copyfrom_rev, copyfrom_path);
          /* ### someday write a node-copyfrom-source-checksum. */
        }
    }
  
  /* The content-length is going to be a combination of the full
     proplist and full text of the file.  Let's make a prop-string to
     write out. */

  /* If the file has no props, then the prophash will be empty, and
     the propstring will be nothing but "END".  */    
  SVN_ERR (svn_fs_node_proplist (&prophash, fs_root, path, pool));
  write_hash_to_stringbuf (prophash, svn_unpack_bytestring, 
                           &propstring, pool);
  content_length += propstring->len;
  
  /* Add the length of file's text, too. */
  if (kind == svn_node_file)
    {
      SVN_ERR (svn_fs_file_length (&textlen, fs_root, path, pool));
      content_length += textlen;
    }

  /* ### someday write a node-content-checksum here.  */

  /* This is the last header before we dump the content. */
  apr_file_printf (file, 
                   SVN_REPOS_DUMPFILE_NODE_CONTENT_LENGTH ": %ld\n\n",
                   (long int) content_length);

  /* Dump property content. */
  apr_file_printf (file, propstring->data);
  
  /* Dump text content. */
  if (kind == svn_node_file)
    {  
      apr_status_t apr_err;
      apr_size_t len, len2;
      svn_stream_t *stream;
      
      SVN_ERR (svn_fs_file_contents (&stream, fs_root, path, pool));
      
      while (1)
        {
          len = bufsize;
          SVN_ERR (svn_stream_read (stream, buffer, &len));
          len2 = len;
          apr_err = apr_file_write (file, buffer, &len2);
          if ((apr_err) || (len2 != len))
            return svn_error_createf 
              (apr_err ? apr_err : SVN_ERR_INCOMPLETE_DATA, 0, NULL, pool,
               "Error writing contents of %s", path);
          if (len != bufsize)
            break;
        }
    }
  
  apr_file_printf (file, "\n\n"); /* ### needed? */
  
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton, 
           svn_revnum_t base_revision, 
           apr_pool_t *pool,
           void **root_baton)
{
  *root_baton = make_dir_baton (NULL, edit_baton, NULL, FALSE, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision, 
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  const char *mypath = apr_pstrdup (pb->pool, path);

  /* remember this path needs to be deleted. */
  apr_hash_set (pb->deleted_entries, mypath, APR_HASH_KEY_STRING, pb);

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *new_db = make_dir_baton (path, eb, pb, TRUE, pool);
  void *val;

  /* This might be a replacement -- is the path already deleted? */
  val = apr_hash_get (pb->deleted_entries, path, APR_HASH_KEY_STRING);

  SVN_ERR (dump_node (eb->fs_root, path, 
                      svn_node_dir,
                      val ? node_action_replace : node_action_add,
                      eb->file, eb->buffer, eb->bufsize, pool));

  if (val)
    /* delete the path, it's now been dumped. */
    apr_hash_set (pb->deleted_entries, path, APR_HASH_KEY_STRING, NULL);
  
  new_db->written_out = TRUE;

  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *pool,
                void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *new_db = make_dir_baton (path, eb, pb, FALSE, pool);

  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory (void *dir_baton)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  apr_hash_index_t *hi;
  apr_pool_t *pool = db->pool;
  apr_pool_t *subpool = svn_pool_create (pool);
  
  for (hi = apr_hash_first (pool, db->deleted_entries);
       hi;
       hi = apr_hash_next (hi))
    {
      const void *key;
      const char *path;
      apr_hash_this (hi, &key, NULL, NULL);
      path = key;

      /* By sending 'svn_node_unknown', the Node-kind: header simply won't
         be written out.  No big deal at all, really.  The loader
         shouldn't care.  */
      SVN_ERR (dump_node (eb->fs_root, path,
                          svn_node_unknown, node_action_delete,
                          eb->file, eb->buffer, eb->bufsize, subpool));     
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  void *val;

  /* This might be a replacement -- is the path already deleted? */
  val = apr_hash_get (pb->deleted_entries, path, APR_HASH_KEY_STRING);

  SVN_ERR (dump_node (eb->fs_root, path, 
                      svn_node_file,
                      val ? node_action_replace : node_action_add,
                      eb->file, eb->buffer, eb->bufsize, pool));

  if (val)
    /* delete the path, it's now been dumped. */
    apr_hash_set (pb->deleted_entries, path, APR_HASH_KEY_STRING, NULL);

  *file_baton = NULL;  /* muhahahaha */
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t ancestor_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  SVN_ERR (dump_node (eb->fs_root, path, 
                      svn_node_file, node_action_change, 
                      eb->file, eb->buffer, eb->bufsize, pool));

  *file_baton = NULL;  /* muhahahaha again */
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *parent_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;

  /* This function is what distinguishes between a directory that is
     opened to merely get somewhere, vs. one that is opened because it
     *actually* changed by itself.  */
  if (! db->written_out)
    {
      SVN_ERR (dump_node (eb->fs_root, db->path->data, 
                          svn_node_dir, node_action_change, 
                          eb->file, eb->buffer, eb->bufsize, pool));
      db->written_out = TRUE;
    }
  return SVN_NO_ERROR;
}



static svn_error_t *
get_dump_editor (const svn_delta_editor_t **editor,
                 void **edit_baton,
                 svn_fs_t *fs,
                 svn_revnum_t to_rev,
                 const char *root_path,
                 apr_file_t *file,
                 apr_pool_t *pool)
{
  /* Allocate an edit baton to be stored in every directory baton.
     Set it up for the directory baton we create here, which is the
     root baton. */
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_editor_t *dump_editor = svn_delta_default_editor (pool);

  /* Set up the edit baton. */
  eb->file = file;
  eb->fs = fs;
  eb->bufsize = sizeof(eb->buffer);
  eb->path = svn_stringbuf_create (root_path, pool);
  SVN_ERR (svn_fs_revision_root (&(eb->fs_root), eb->fs, to_rev, pool));

  /* Set up the editor. */
  dump_editor->open_root = open_root;
  dump_editor->delete_entry = delete_entry;
  dump_editor->add_directory = add_directory;
  dump_editor->open_directory = open_directory;
  dump_editor->close_directory = close_directory;
  dump_editor->change_dir_prop = change_dir_prop;
  dump_editor->add_file = add_file;
  dump_editor->open_file = open_file;

  *edit_baton = eb;
  *editor = dump_editor;
  
  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------------*/

/** The main dumping routine, svn_repos_dump_fs. **/


/* Helper for svn_repos_dump_fs.

   Write a revision record of REV in FS to already-open FILE, using POOL.
 */
static svn_error_t *
write_revision_record (apr_file_t *file,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  apr_hash_t *props;
  svn_stringbuf_t *encoded_prophash;

  SVN_ERR (svn_fs_revision_proplist (&props, fs, rev, pool));
  write_hash_to_stringbuf (props, svn_unpack_bytestring,
                           &encoded_prophash, pool);

  /* ### someday write a revision-content-checksum */

  apr_file_printf (file, 
                   SVN_REPOS_DUMPFILE_REVISION_NUMBER ": %ld\n", rev);
  apr_file_printf (file,
                   SVN_REPOS_DUMPFILE_REVISION_CONTENT_LENGTH ": %ld\n\n",
                   (long int) encoded_prophash->len);
  apr_file_printf (file,
                   encoded_prophash->data);
  apr_file_printf (file, "\n");
  
  return SVN_NO_ERROR;
}



/* The main dumper. */
svn_error_t *
svn_repos_dump_fs (svn_repos_t *repos,
                   apr_file_t *file,
                   svn_revnum_t start_rev,
                   svn_revnum_t end_rev,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *dump_editor;
  const svn_delta_edit_fns_t *editor;
  void *dump_edit_baton, *edit_baton;
  svn_revnum_t i;  
  apr_hash_t *src_revs;

  svn_fs_t *fs = svn_repos_fs (repos);

  apr_pool_t *subpool = svn_pool_create (pool);

  /* Use default vals if necessary. */
  if (! SVN_IS_VALID_REVNUM(start_rev))
    start_rev = 0;
  if (! SVN_IS_VALID_REVNUM(end_rev))
    SVN_ERR (svn_fs_youngest_rev (&end_rev, fs, pool));

  /* ### todo: validate the starting and ending revs: do they exist? */

  /* sanity check */
  if (start_rev > end_rev)
    return svn_error_createf (SVN_ERR_REPOS_BAD_ARGS, 0, NULL, pool,
                              "start_rev %ld is greater than end_rev %ld",
                              start_rev, end_rev);

  /* Write out "general" metadata for the dumpfile.  In this case, a
     magic string followed by a dumpfile format version. */
  apr_file_printf (file, SVN_REPOS_DUMPFILE_MAGIC_HEADER ": ");
  apr_file_printf (file, "%d\n\n", SVN_REPOS_DUMPFILE_FORMAT_VERSION);
                   
  /* Main loop:  we're going to dump revision i.  */
  for (i = start_rev; i <= end_rev; i++)
    {
      svn_revnum_t from_rev, to_rev;
      svn_fs_root_t *from_root, *to_root;

      /* Special-case the initial revision dump: it needs to contain
         *all* nodes, because it's the foundation of all future
         revisions in the dumpfile. */
      if (i == start_rev)
        {
          /* Special-special-case a dump of revision 0. */
          if (i == 0)
            {
              svn_fs_root_t *root_0;
              char buffer[1024];

              /* Just write out the one revision record and the one
                 root-node record (for formality), and continue
                 looping. */
              SVN_ERR (write_revision_record (file, fs, 0, subpool));

              SVN_ERR (svn_fs_revision_root (&root_0, fs, 0, subpool));
              SVN_ERR (dump_node (root_0, "/",
                                  svn_node_dir, node_action_add,
                                  file, buffer, 1024, subpool));

              goto loop_end;
            }

          /* Compare START_REV to revision 0, so that everything
             appears to be added.  */
          from_rev = 0;
          to_rev = i;
        }
      else
        {
          /* In the normal case, we want to compare consecutive revs. */
          from_rev = i - 1;
          to_rev = i;
        }

      /* Write the revision record. */
      SVN_ERR (write_revision_record (file, fs, to_rev, subpool));

      /* The editor which dumps nodes to a file. */
      SVN_ERR (get_dump_editor (&dump_editor, &dump_edit_baton, 
                                fs, to_rev, "/", file, subpool));
      /* ### remove this wrapper someday: */
      svn_delta_compat_wrap (&editor, &edit_baton,
                             dump_editor, dump_edit_baton, subpool);

      /* Drive the editor. */
      SVN_ERR (svn_fs_revision_root (&from_root, fs, from_rev, subpool));
      SVN_ERR (svn_fs_revision_root (&to_root, fs, to_rev, subpool));

      src_revs = apr_hash_make (subpool);
      apr_hash_set (src_revs, "", APR_HASH_KEY_STRING, &from_rev);

      SVN_ERR (svn_repos_dir_delta (from_root, "/", NULL, 
                                    src_revs,
                                    to_root, "/",
                                    editor, edit_baton,
                                    FALSE, /* don't send text-deltas */
                                    TRUE, /* recurse */
                                    FALSE, /* don't send entry props */
                                    FALSE, /* ### don't send copyfrom args */
                                    subpool));

    loop_end:
      /* Reuse all memory consumed by the dump of of this one revision. */
      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
