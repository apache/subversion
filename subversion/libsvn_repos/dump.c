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
  /* The path which implicitly prepends all full paths coming into
     this editor.  This will almost always be "" or "/".  */
  const char *path;

  /* The stream to dump to. */
  svn_stream_t *stream; 

  /* The fs revision root, so we can read the contents of paths. */
  svn_fs_root_t *fs_root;

  /* reusable buffer for writing file contents */
  char buffer[SVN_STREAM_CHUNK_SIZE];
  apr_size_t bufsize;
};

struct dir_baton
{
  struct edit_baton *edit_baton;
  struct dir_baton *parent_dir_baton;
  const char *path;        /* the absolute path to this directory */
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
  const char *full_path;

  /* A path relative to nothing?  I don't think so. */
  if (path && (! pb))
    abort();

  /* Construct the full path of this node. */
  if (pb)
    full_path = svn_path_join (eb->path, path, pool);
  else
    full_path = apr_pstrdup (pool, eb->path);

  new_db->edit_baton = eb;
  new_db->parent_dir_baton = pb;
  new_db->path = full_path;
  new_db->added = added;
  new_db->written_out = FALSE;
  new_db->deleted_entries = apr_hash_make (pool);
  new_db->pool = pool;
  
  return new_db;
}


/* This helper is the main "meat" of the editor -- it does all the
   work of writing a node record.
   
   Write out a node record for PATH of type KIND under FS_ROOT.
   ACTION describes what is happening to the node (see enum svn_node_action).
   Write record to writable STREAM, using BUFFER to write in chunks.
  */
static svn_error_t *
dump_node (svn_fs_root_t *fs_root,
           const char *path,    /* an absolute path. */
           enum svn_node_kind kind,
           enum svn_node_action action,
           svn_stream_t *stream,
           void *buffer,
           apr_size_t bufsize,
           apr_pool_t *pool)
{
  svn_stringbuf_t *propstring;
  apr_hash_t *prophash;
  apr_off_t textlen;
  apr_size_t content_length = 0, len;

  /* Write out metadata headers for this file node. */
  SVN_ERR (svn_stream_printf (stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_PATH ": %s\n", path));
  
  if (kind == svn_node_file)
    SVN_ERR (svn_stream_printf (stream, pool,
                                SVN_REPOS_DUMPFILE_NODE_KIND ": file\n"));
  else if (kind == svn_node_dir)
    SVN_ERR (svn_stream_printf (stream, pool,
                                SVN_REPOS_DUMPFILE_NODE_KIND ": dir\n"));
  
  if (action == svn_node_action_change)
    {
      SVN_ERR (svn_stream_printf (stream, pool,
                                  SVN_REPOS_DUMPFILE_NODE_ACTION
                                  ": change\n"));  
    }
  else if (action == svn_node_action_replace)
    {
      SVN_ERR (svn_stream_printf (stream, pool,
                                  SVN_REPOS_DUMPFILE_NODE_ACTION
                                  ": replace\n"));  
    }
  else if (action == svn_node_action_delete)
    {
      SVN_ERR (svn_stream_printf (stream, pool,
                                  SVN_REPOS_DUMPFILE_NODE_ACTION
                                  ": delete\n"));  

      /* Get out!  We're done! */
      return SVN_NO_ERROR;
    }
  else if (action == svn_node_action_add)
    {
      const char *copyfrom_path;
      svn_revnum_t copyfrom_rev;

      SVN_ERR (svn_stream_printf (stream, pool,
                                  SVN_REPOS_DUMPFILE_NODE_ACTION ": add\n"));

      /* ### Does this node have a copyfrom history?  Really,
         dir_delta should be sending it to add_file or add_dir -- but
         it doesn't yet.  So we manually make a check. */
      SVN_ERR (svn_fs_copied_from (&copyfrom_rev, &copyfrom_path,
                                   fs_root, path, pool));

      /* ### someday put a copyfrom-sources-checksum in fb. */

      if (copyfrom_path != NULL)
        {
          SVN_ERR (svn_stream_printf (stream, pool,
                                      SVN_REPOS_DUMPFILE_NODE_COPIED_FROM 
                                      ": %" SVN_REVNUM_T_FMT ", %s\n",
                                      copyfrom_rev, copyfrom_path));
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
  SVN_ERR (svn_stream_printf (stream, pool,
                              SVN_REPOS_DUMPFILE_CONTENT_LENGTH 
                              ": %" APR_SIZE_T_FMT "\n\n", content_length));

  /* Dump property content. */
  len = propstring->len;
  SVN_ERR (svn_stream_write (stream, propstring->data, &len));
  
  /* Dump text content */
  /*    (this stream "pull and push" code was stolen from
        libsvn_ra_local/ra_plugin.c:get_file().  */
  if (kind == svn_node_file)
    {
      apr_size_t rlen, wlen;
      svn_stream_t *contents;
          
      SVN_ERR (svn_fs_file_contents (&contents, fs_root, path, pool));
      
      while (1)
        {
          /* read a maximum number of bytes from the file, please. */
          rlen = bufsize; 
          SVN_ERR (svn_stream_read (contents, buffer, &rlen));
          
          /* write however many bytes you read, please. */
          wlen = rlen;
          SVN_ERR (svn_stream_write (stream, buffer, &wlen));
          if (wlen != rlen)
            {
              /* Uh oh, didn't write as many bytes as we read, and no
                 error was returned.  According to the docstring, this
                 should never happen. */
              return 
                svn_error_createf (SVN_ERR_UNEXPECTED_EOF, 0, NULL, pool,
                                   "Error dumping textual contents of %s.",
                                   path);
            }
        
        if (rlen != bufsize)
          {
            /* svn_stream_read didn't throw an error, yet it didn't read
               all the bytes requested.  According to the docstring,
               this means a plain old EOF happened, so we're done. */
            break;
          }
        }
    }
  
  len = 2;
  SVN_ERR (svn_stream_write (stream, "\n\n", &len)); /* ### needed? */
  
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
                      val ? svn_node_action_replace : svn_node_action_add,
                      eb->stream, eb->buffer, eb->bufsize, pool));

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
                          svn_node_unknown, svn_node_action_delete,
                          eb->stream, eb->buffer, eb->bufsize, subpool));     

      svn_pool_clear (subpool);
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
                      val ? svn_node_action_replace : svn_node_action_add,
                      eb->stream, eb->buffer, eb->bufsize, pool));

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
                      svn_node_file, svn_node_action_change, 
                      eb->stream, eb->buffer, eb->bufsize, pool));

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
      SVN_ERR (dump_node (eb->fs_root, db->path, 
                          svn_node_dir, svn_node_action_change, 
                          eb->stream, eb->buffer, eb->bufsize, pool));
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
                 svn_stream_t *stream,
                 apr_pool_t *pool)
{
  /* Allocate an edit baton to be stored in every directory baton.
     Set it up for the directory baton we create here, which is the
     root baton. */
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_editor_t *dump_editor = svn_delta_default_editor (pool);

  /* Set up the edit baton. */
  eb->stream = stream;
  eb->bufsize = sizeof(eb->buffer);
  eb->path = apr_pstrdup (pool, root_path);
  SVN_ERR (svn_fs_revision_root (&(eb->fs_root), fs, to_rev, pool));

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

   Write a revision record of REV in FS to writable STREAM, using POOL.
 */
static svn_error_t *
write_revision_record (svn_stream_t *stream,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  apr_size_t len;
  apr_hash_t *props;
  svn_stringbuf_t *encoded_prophash;

  SVN_ERR (svn_fs_revision_proplist (&props, fs, rev, pool));
  write_hash_to_stringbuf (props, svn_unpack_bytestring,
                           &encoded_prophash, pool);

  /* ### someday write a revision-content-checksum */

  SVN_ERR (svn_stream_printf (stream, pool,
                              SVN_REPOS_DUMPFILE_REVISION_NUMBER 
                              ": %" SVN_REVNUM_T_FMT "\n", rev));
  SVN_ERR (svn_stream_printf (stream, pool,
                              SVN_REPOS_DUMPFILE_CONTENT_LENGTH
                              ": %" APR_SIZE_T_FMT "\n\n",
                              encoded_prophash->len));
  
  len = encoded_prophash->len;
  SVN_ERR (svn_stream_write (stream, encoded_prophash->data, &len));

  len = 1;
  SVN_ERR (svn_stream_write (stream, "\n", &len));
  
  return SVN_NO_ERROR;
}



/* The main dumper. */
svn_error_t *
svn_repos_dump_fs (svn_repos_t *repos,
                   svn_stream_t *stream,
                   svn_revnum_t start_rev,
                   svn_revnum_t end_rev,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *dump_editor;
  const svn_delta_edit_fns_t *editor;
  void *dump_edit_baton, *edit_baton;
  svn_revnum_t i;  
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
  SVN_ERR (svn_stream_printf (stream, pool, SVN_REPOS_DUMPFILE_MAGIC_HEADER
                              ": %d\n\n", SVN_REPOS_DUMPFILE_FORMAT_VERSION));
                   
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
              /* Just write out the one revision 0 record and move on.
                 The parser might want to use its properties. */
              SVN_ERR (write_revision_record (stream, fs, 0, subpool));
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
      SVN_ERR (write_revision_record (stream, fs, to_rev, subpool));

      /* The editor which dumps nodes to a file. */
      SVN_ERR (get_dump_editor (&dump_editor, &dump_edit_baton, 
                                fs, to_rev, "/", stream, subpool));
      /* ### remove this wrapper someday: */
      svn_delta_compat_wrap (&editor, &edit_baton,
                             dump_editor, dump_edit_baton, subpool);

      /* Drive the editor. */
      SVN_ERR (svn_fs_revision_root (&from_root, fs, from_rev, subpool));
      SVN_ERR (svn_fs_revision_root (&to_root, fs, to_rev, subpool));
      SVN_ERR (svn_repos_dir_delta (from_root, "/", NULL, 
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
