/*
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include <svn_pools.h>
#include <svn_fs.h>

#include "svn_ruby.h"
#include "fs_root.h"
#include "fs_node.h"
#include "txdelta.h"
#include "error.h"

static VALUE cSvnFsRoot;
static VALUE cSvnFsRevisionRoot;
static VALUE cSvnFsTxnRoot;

typedef struct svn_ruby_fs_root_t
{
  svn_fs_root_t *root;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_fs_root_t; 

svn_fs_root_t *
svn_ruby_fs_root (VALUE aRoot)
{
  svn_ruby_fs_root_t *root;
  Data_Get_Struct (aRoot, svn_ruby_fs_root_t, root);
  return root->root;
}


static void
free_fs_root (void *p)
{
  svn_ruby_fs_root_t *root = p;
  if (!root->closed)
    svn_fs_close_root (root->root);
  apr_pool_destroy (root->pool);
}

VALUE
svn_ruby_fs_rev_root_new (svn_fs_root_t *root, apr_pool_t *pool)
{
  svn_ruby_fs_root_t *rb_root;
  VALUE obj;

  obj = Data_Make_Struct (cSvnFsRevisionRoot, svn_ruby_fs_root_t,
                          0, free_fs_root, rb_root);
  rb_root->root = root;
  rb_root->pool = pool;
  rb_root->closed = FALSE;
  rb_obj_call_init (obj, 0, 0);

  return obj;
}

VALUE
svn_ruby_fs_txn_root_new (svn_fs_root_t *root, apr_pool_t *pool)
{
  svn_ruby_fs_root_t *rb_root;
  VALUE obj;

  obj = Data_Make_Struct (cSvnFsTxnRoot, svn_ruby_fs_root_t,
                          0, free_fs_root, rb_root);
  rb_root->root = root;
  rb_root->pool = pool;
  rb_root->closed = FALSE;
  rb_obj_call_init (obj, 0, 0);

  return obj;
}



/* FsRoot instance methods. */

static VALUE
fs_root_close (VALUE self)
{
  svn_ruby_fs_root_t *root;

  Data_Get_Struct (self, svn_ruby_fs_root_t, root);
  if (root->closed)
    rb_raise (rb_eRuntimeError, "closed root");
  svn_fs_close_root (root->root);
  root->closed = TRUE;

  return Qnil;
}

static VALUE
check_path (VALUE self, VALUE aPath)
{
  svn_node_kind_t kind;
  svn_ruby_fs_root_t *root;
  apr_pool_t *pool;

  Check_Type (aPath, T_STRING);
  Data_Get_Struct (self, svn_ruby_fs_root_t, root);
  if (root->closed)
    rb_raise (rb_eRuntimeError, "closed root");
  pool = svn_pool_create (root->pool);
  kind = svn_fs_check_path (root->root, StringValuePtr (aPath), pool);
  apr_pool_destroy (pool);

  return INT2FIX (kind);
}

static VALUE
is_dir (VALUE self, VALUE aPath)
{
  int is_dir;
  svn_ruby_fs_root_t *root;
  const char *path;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  Data_Get_Struct (self, svn_ruby_fs_root_t, root);
  if (root->closed)
    rb_raise (rb_eRuntimeError, "closed root");
  pool = svn_pool_create (root->pool);
  err = svn_fs_is_dir (&is_dir, root->root, path, pool);
  apr_pool_destroy (pool);

  if (err)
    svn_ruby_raise (err);

  if (is_dir)
    return Qtrue;
  else
    return Qfalse;
}

static VALUE
is_file (VALUE self, VALUE aPath)
{
  int is_file;
  svn_ruby_fs_root_t *root;
  const char *path;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  Data_Get_Struct (self, svn_ruby_fs_root_t, root);
  if (root->closed)
    rb_raise (rb_eRuntimeError, "closed root");
  pool = svn_pool_create (root->pool);
  err = svn_fs_is_file (&is_file, root->root, path, pool);
  apr_pool_destroy (pool);

  if (err)
    svn_ruby_raise (err);

  if (is_file)
    return Qtrue;
  else
    return Qfalse;
}



static VALUE
file (VALUE self, VALUE aPath)
{
  Check_Type (aPath, T_STRING);
  if (rb_funcall (self, rb_intern ("file?"), 1, aPath) != Qtrue)
    rb_raise (rb_eRuntimeError, "No such file: %s", StringValuePtr (aPath));

  return svn_ruby_fs_file_new (self, aPath);
}

static VALUE
dir (VALUE self, VALUE aPath)
{
  Check_Type (aPath, T_STRING);
  if (rb_funcall (self, rb_intern ("dir?"), 1, aPath) != Qtrue)
    rb_raise (rb_eRuntimeError, "No such directory: %s", StringValuePtr (aPath));

  return svn_ruby_fs_dir_new (self, aPath);
}

static VALUE
node (VALUE self, VALUE aPath)
{
  Check_Type (aPath, T_STRING);
  if (rb_funcall (self, rb_intern ("file?"), 1, aPath) == Qtrue)
    return svn_ruby_fs_file_new (self, aPath);
  else if (rb_funcall (self, rb_intern ("dir?"), 1, aPath) == Qtrue)
    return svn_ruby_fs_dir_new (self, aPath);
  else
    {
      rb_raise (rb_eRuntimeError, "Unknown node type");
      return Qnil; /* Silence compiler. */
    }
}


/* TxnRoot instance methods. */

static VALUE
make_dir (VALUE self, VALUE aPath)
{
  svn_ruby_fs_root_t *root;
  const char *path;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  Data_Get_Struct (self, svn_ruby_fs_root_t, root);
  if (root->closed)
    rb_raise (rb_eRuntimeError, "closed root");
  pool = svn_pool_create (root->pool);
  err = svn_fs_make_dir (root->root, path, pool);
  apr_pool_destroy (pool);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
make_file (VALUE self, VALUE aPath)
{
  svn_ruby_fs_root_t *root;
  const char *path;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  Data_Get_Struct (self, svn_ruby_fs_root_t, root);
  pool = svn_pool_create (root->pool);
  err = svn_fs_make_file (root->root, path, pool);
  apr_pool_destroy (pool);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}


static VALUE
fs_apply_textdelta (VALUE self, VALUE aPath)
{
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  svn_ruby_fs_root_t *root;
  const char *path;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  Data_Get_Struct (self, svn_ruby_fs_root_t, root);
  if (root->closed)
    rb_raise (rb_eRuntimeError, "closed root");
  pool = svn_pool_create (NULL);
  err = svn_fs_apply_textdelta (&handler, &handler_baton, root->root, path, pool);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  return svn_ruby_txdelta_new (handler, handler_baton, pool);
}

static VALUE
change_node_prop (VALUE self, VALUE path, VALUE aName, VALUE aValue)
{
  svn_ruby_fs_root_t *root;
  const svn_string_t *value;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (path, T_STRING);
  Check_Type (aName, T_STRING);
  if (aValue != Qnil)
    Check_Type (aValue, T_STRING);

  Data_Get_Struct (self, svn_ruby_fs_root_t, root);

  pool = svn_pool_create (root->pool);
  if (aValue == Qnil)
    value = NULL;
  else
    value = svn_string_ncreate (StringValuePtr (aValue),
                                RSTRING (aValue)->len, pool);
  err = svn_fs_change_node_prop (root->root, StringValuePtr (path),
				 StringValuePtr (aName), value, pool);
  apr_pool_destroy (pool);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

svn_boolean_t
svn_ruby_is_fs_root (VALUE obj)
{
  if (rb_obj_is_kind_of (obj, cSvnFsRoot) == Qtrue)
    return TRUE;
  else
    return FALSE;
}



void
svn_ruby_init_fs_root (void)
{
  cSvnFsRoot = rb_define_class_under (svn_ruby_mSvn, "FsRoot", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnFsRoot), "new");
  rb_define_method (cSvnFsRoot, "close", fs_root_close, 0);
  rb_define_method (cSvnFsRoot, "checkPath", check_path, 1);
  rb_define_method (cSvnFsRoot, "dir?", is_dir, 1);
  rb_define_method (cSvnFsRoot, "file?", is_file, 1);
  rb_define_method (cSvnFsRoot, "file", file, 1);
  rb_define_method (cSvnFsRoot, "dir", dir, 1);
  rb_define_method (cSvnFsRoot, "node", node, 1);
  cSvnFsRevisionRoot = rb_define_class_under (svn_ruby_mSvn, "FsRevisionRoot",
                                              cSvnFsRoot);
  cSvnFsTxnRoot = rb_define_class_under (svn_ruby_mSvn, "FsTxnRoot", cSvnFsRoot);
  rb_define_method (cSvnFsTxnRoot, "makeDir", make_dir, 1);
  rb_define_method (cSvnFsTxnRoot, "makeFile", make_file, 1);
  rb_define_method (cSvnFsTxnRoot, "applyTextDelta",
                    fs_apply_textdelta, 1);
  rb_define_method (cSvnFsTxnRoot, "changeNodeProp", change_node_prop, 3);

}
