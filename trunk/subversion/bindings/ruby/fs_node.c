/*
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
#include <ruby.h>
#include <svn_fs.h>

#include <svn_pools.h>
#include <svn_fs.h>
#include <svn_repos.h>

#include "svn_ruby.h"
#include "fs_root.h"
#include "fs_node.h"
#include "stream.h"
#include "error.h"
#include "util.h"
#include "delta_editor.h"

static VALUE cSvnFsDir, cSvnFsFile;

typedef struct svn_ruby_fs_node
{
  VALUE fs_root;
  VALUE path;
  apr_pool_t *pool;
} svn_ruby_fs_node;



static void
mark_node (svn_ruby_fs_node *node)
{
  rb_gc_mark (node->fs_root);
  rb_gc_mark (node->path);
}

static void
free_node (svn_ruby_fs_node *node)
{
  apr_pool_destroy (node->pool);
  free (node);
}

static VALUE
fs_node_new (VALUE class, VALUE fsRoot, VALUE path)
{
  VALUE obj;
  svn_ruby_fs_node *node;
  
  obj = Data_Make_Struct (class, svn_ruby_fs_node,
                          mark_node, free_node, node);
  node->fs_root = fsRoot;
  node->path = path;
  node->pool = svn_pool_create (NULL);

  return obj;
}

VALUE
svn_ruby_fs_file_new (VALUE fsRoot, VALUE path)
{
  return fs_node_new (cSvnFsFile, fsRoot, path);
}

VALUE
svn_ruby_fs_dir_new (VALUE fsRoot, VALUE path)
{
  return fs_node_new (cSvnFsDir, fsRoot, path);
}




static VALUE
path (VALUE self)
{
  svn_ruby_fs_node *node;
  Data_Get_Struct (self, svn_ruby_fs_node, node);
  return node->path;
}

static VALUE
proplist (VALUE self)
{
  apr_hash_t *table;
  svn_fs_root_t *root;
  apr_pool_t *pool;
  svn_error_t *err;
  svn_ruby_fs_node *node;

  VALUE obj;

  Data_Get_Struct (self, svn_ruby_fs_node, node);
  root = svn_ruby_fs_root (node->fs_root);
  pool = svn_pool_create (node->pool);
  err = svn_fs_node_proplist (&table, root, StringValuePtr (node->path), pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  obj = svn_ruby_str_hash (table, pool);
  apr_pool_destroy (pool);

  return obj;
}

static VALUE
prop (VALUE self, VALUE aPropname)
{
  svn_string_t *value;
  svn_fs_root_t *root;
  apr_pool_t *pool;
  svn_error_t *err;
  svn_ruby_fs_node *node;

  Check_Type (aPropname, T_STRING);
  Data_Get_Struct (self, svn_ruby_fs_node, node);
  root = svn_ruby_fs_root (node->fs_root);
  pool = svn_pool_create (node->pool);
  err = svn_fs_node_prop (&value, root, StringValuePtr (node->path),
                          StringValuePtr (aPropname), pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  {
    VALUE obj;

    if (!value)
      obj = Qnil;
    else
      obj = rb_str_new (value->data, value->len);
    apr_pool_destroy (pool);

    return obj;
  }
}

static VALUE
dir_entries (VALUE self)
{
  apr_hash_t *table;
  svn_fs_root_t *root;
  apr_pool_t *pool;
  svn_error_t *err;
  svn_ruby_fs_node *node;

  Data_Get_Struct (self, svn_ruby_fs_node, node);
  root = svn_ruby_fs_root (node->fs_root);
  pool = svn_pool_create (node->pool);
  err = svn_fs_dir_entries (&table, root, StringValuePtr (node->path), pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }
  
  {
    VALUE obj;
    apr_hash_index_t *hi;

    obj = rb_hash_new ();
    for (hi = apr_hash_first (pool, table); hi; hi = apr_hash_next (hi))
      {
        const void *key;
        void *val;
        apr_ssize_t key_len;
        svn_stringbuf_t *id;

        apr_hash_this (hi, &key, &key_len, &val);
        id = svn_fs_unparse_id (((svn_fs_dirent_t *) val)->id, pool);
        rb_hash_aset (obj, rb_str_new (key, key_len),
                      rb_str_new (id->data, id->len));
      }
    apr_pool_destroy (pool);
    return obj;
  }
}

static VALUE
dir_delta (VALUE self,
           VALUE srcEntry,
           VALUE srcRevs,
           VALUE tgtRoot,
           VALUE tgtPath,
           VALUE aEditor,
           VALUE text_deltas,
           VALUE recurse,
           VALUE use_copyfrom_args)
{
  svn_ruby_fs_node *node;
  svn_fs_root_t *src_root, *tgt_root;
  apr_hash_t *src_revs;
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  apr_pool_t *pool;
  svn_error_t *err;
  const char *src_entry;

  VALUE srcRevsArray;
  int i;

  Data_Get_Struct (self, svn_ruby_fs_node, node);
  if (! svn_ruby_is_fs_root (tgtRoot))
    rb_raise (rb_eArgError, "tgtRoot must be Svn::FsRoot object");
  if (srcEntry != Qnil)
    Check_Type (srcEntry, T_STRING);
  Check_Type (srcRevs, T_HASH);
  Check_Type (tgtPath, T_STRING);
  srcRevsArray = rb_funcall (srcRevs, rb_intern ("to_a"), 0);
  for (i = 0; i < RARRAY (srcRevsArray)->len; i++)
    {
      VALUE elt = RARRAY (srcRevsArray)->ptr[i];
      Check_Type (RARRAY (elt)->ptr[0], T_STRING);
      (void) NUM2LONG (RARRAY (elt)->ptr[1]);
    }
  svn_ruby_delta_editor (&editor, &edit_baton, aEditor);
  src_root = svn_ruby_fs_root (node->fs_root);
  pool = svn_pool_create (node->pool);
  if (srcEntry == Qnil)
    src_entry = NULL;
  else
    src_entry = StringValuePtr (srcEntry);

  {
    svn_revnum_t *rev_ptr = apr_palloc (pool, sizeof (*rev_ptr));
    src_revs = apr_hash_make (pool);

    for (i = 0; i < RARRAY (srcRevsArray)->len; i++)
      {
        VALUE elt = RARRAY (srcRevsArray)->ptr[i];
      
        *rev_ptr = NUM2LONG (RARRAY (elt)->ptr[1]);
        apr_hash_set (src_revs, StringValuePtr (RARRAY (elt)->ptr[0]),
                      APR_HASH_KEY_STRING, rev_ptr);

      }
  }
  tgt_root = svn_ruby_fs_root (tgtRoot);

  err = svn_repos_dir_delta (src_root,
                             StringValuePtr (node->path),
                             src_entry, src_revs,
                             tgt_root, StringValuePtr (tgtPath),
                             editor, edit_baton,
                             RTEST (text_deltas), RTEST (recurse),
                             RTEST (use_copyfrom_args),
                             pool);
  apr_pool_destroy (pool);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}


static VALUE
file_length (VALUE self)
{
  apr_off_t length;
  svn_fs_root_t *root;
  apr_pool_t *pool;
  svn_error_t *err;
  svn_ruby_fs_node *node;

  VALUE obj;

  Data_Get_Struct (self, svn_ruby_fs_node, node);
  root = svn_ruby_fs_root (node->fs_root);
  pool = svn_pool_create (NULL);

  err = svn_fs_file_length (&length, root,
                            StringValuePtr (node->path), pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  obj = LONG2NUM (length);
  apr_pool_destroy (pool);

  return obj;
}

static VALUE
file_contents (VALUE self)
{
  svn_stream_t *contents;
  svn_fs_root_t *root;
  apr_pool_t *pool;
  svn_error_t *err;
  svn_ruby_fs_node *node;

  Data_Get_Struct (self, svn_ruby_fs_node, node);
  root = svn_ruby_fs_root (node->fs_root);
  pool = svn_pool_create (NULL);

  err = svn_fs_file_contents (&contents, root,
                              StringValuePtr (node->path), pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  return svn_ruby_stream_new (Qnil, contents, pool);
}

void
svn_ruby_init_fs_node (void)
{
  VALUE cSvnFsNode;

  cSvnFsNode = rb_define_class_under (svn_ruby_mSvn, "FsNode", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnFsNode), "new");
  rb_define_method (cSvnFsNode, "path", path, 0);
  rb_define_method (cSvnFsNode, "prop", prop, 1);
  rb_define_method (cSvnFsNode, "proplist", proplist, 0);
  cSvnFsDir = rb_define_class_under (svn_ruby_mSvn, "FsDir", cSvnFsNode);
  rb_define_method (cSvnFsDir, "entries", dir_entries, 0);
  rb_define_method (cSvnFsDir, "delta", dir_delta, 8);
  cSvnFsFile = rb_define_class_under (svn_ruby_mSvn, "FsFile", cSvnFsNode);
  rb_define_method (cSvnFsFile, "length", file_length, 0);
  rb_define_method (cSvnFsFile, "contents", file_contents, 0);
}
