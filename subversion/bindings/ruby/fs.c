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

#include <svn_pools.h>
#include <svn_error.h>
#include <svn_string.h>
#include <svn_fs.h>

#include "svn_ruby.h"
#include "fs.h"
#include "fs_root.h"
#include "fs_txn.h"
#include "error.h"
#include "util.h"

static VALUE cSvnFS;


/* Class Fs */

typedef struct svn_ruby_fs_t
{
  svn_fs_t *fs;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_fs_t;

static void
close_fs (svn_ruby_fs_t *fs)
{
  long count;

  if (fs->closed)
    return;

  count = svn_ruby_get_refcount (fs->pool);
  if (count == 1)
    apr_pool_destroy (fs->pool);
  else
    svn_ruby_set_refcount (fs->pool, count - 1);

  fs->closed = TRUE;
}

static void
fs_free (void *p)
{
  svn_ruby_fs_t *fs = p;
  close_fs (fs);
  free (fs);
}

VALUE
svn_ruby_fs_new (VALUE class, svn_fs_t *fs, apr_pool_t *pool)
{
  VALUE obj;
  svn_ruby_fs_t *rb_fs;
  if (class == Qnil)
    {
      /* Reference count parent's pool from svn_repos_t so that child pool
         fs is allocated won't be blown away. */
      long count;
      obj = Data_Make_Struct (cSvnFS, svn_ruby_fs_t, 0, fs_free, rb_fs);
      count = svn_ruby_get_refcount (pool);
      svn_ruby_set_refcount (pool, count + 1);
    }
  else
    {
      obj = Data_Make_Struct (class, svn_ruby_fs_t, 0, fs_free, rb_fs);
      svn_ruby_set_refcount (pool, 1);
    }
  rb_fs->fs = fs;
  rb_fs->pool = pool;
  rb_fs->closed = FALSE;
  return obj;
}

static VALUE
open_or_create (VALUE class, VALUE aPath, int create)
{
  apr_pool_t *pool;
  svn_fs_t *fs;
  svn_error_t *err;
  char *path;

  VALUE obj, argv[1];

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);
  fs = svn_fs_new (pool);
  obj = svn_ruby_fs_new (class, fs, pool);
  argv[0] = aPath;
  rb_obj_call_init (obj, 1, argv);
  
  if (create)
    err = svn_fs_create_berkeley (fs, path);
  else
    err = svn_fs_open_berkeley (fs, path);
  if (err)
    svn_ruby_raise (err);

  return obj;
}

static VALUE
fs_open (VALUE class, VALUE aPath)
{
  return open_or_create (class, aPath, 0);
}

static VALUE
fs_create (VALUE class, VALUE aPath)
{
  return open_or_create (class, aPath, 1);
}

static VALUE
fs_delete (VALUE class, VALUE aPath)
{
  apr_pool_t *pool;
  svn_error_t *err;
  char *path;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);
  err = svn_fs_delete_berkeley (path, pool);
  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);
  return Qnil;
}

static VALUE
fs_recover (VALUE class, VALUE aPath)
{
  apr_pool_t *pool;
  svn_error_t *err;
  char *path;

  Check_Type (aPath, T_STRING);
  path = StringValuePtr (aPath);
  pool = svn_pool_create (NULL);
  err = svn_fs_berkeley_recover (path, pool);
  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);
  return Qnil;
}


/* Instance methods. */

static VALUE
fs_initialize (int argc, VALUE *argv, VALUE self)
{
  /* Do nothing. */
  return self;
}

static VALUE
fs_is_closed (VALUE self)
{
  svn_ruby_fs_t *fs;

  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    return Qtrue;
  else
    return Qfalse;
}

static VALUE
fs_close (VALUE self)
{
  svn_ruby_fs_t *fs;

  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");

  close_fs (fs);

  return Qnil;
}


static VALUE
fs_youngest_rev (VALUE self)
{
  apr_pool_t *pool;
  svn_revnum_t youngest;
  svn_ruby_fs_t *fs;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");
  pool = svn_pool_create (fs->pool);
  err = svn_fs_youngest_rev (&youngest, fs->fs, pool);
  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);

  return INT2NUM (youngest);
}

static VALUE
fs_revision_prop (VALUE self, VALUE aRev, VALUE aPropname)
{
  svn_string_t *value;
  svn_ruby_fs_t *fs;
  svn_revnum_t rev;
  apr_pool_t *pool;
  svn_error_t *err;
  VALUE obj;

  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");

  rev = NUM2LONG (aRev);
  Check_Type (aPropname, T_STRING);
  pool = svn_pool_create (fs->pool);

  err = svn_fs_revision_prop (&value, fs->fs, rev,
			      StringValuePtr (aPropname), pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  if (!value)
    obj = Qnil;
  else
    obj = rb_str_new (value->data, value->len);
  apr_pool_destroy (pool);
  return obj;
}

static VALUE
fs_revision_proplist (VALUE self, VALUE aRev)
{
  svn_ruby_fs_t *fs;
  apr_hash_t *table_p;
  svn_revnum_t rev;
  apr_pool_t *pool;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");
  rev = NUM2LONG (aRev);

  pool = svn_pool_create (NULL);
  err = svn_fs_revision_proplist (&table_p, fs->fs, rev, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  {
    VALUE obj;
    apr_hash_index_t *hi;

    obj = rb_hash_new ();

    for (hi = apr_hash_first (pool, table_p); hi; hi = apr_hash_next (hi))
      {
        const void *key;
        void *val;
        apr_ssize_t key_len;
        svn_stringbuf_t *value;

        apr_hash_this (hi, &key, &key_len, &val);
        value = (svn_stringbuf_t *) val;
        rb_hash_aset (obj, rb_str_new (key, key_len),
                      rb_str_new (value->data, value->len));
      }
    apr_pool_destroy (pool);
    return obj;
  }
}

static VALUE
fs_change_rev_prop (VALUE self, VALUE aRev, VALUE aName, VALUE aValue)
{
  svn_ruby_fs_t *fs;
  svn_revnum_t rev;
  const svn_string_t *value;
  apr_pool_t *pool;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");

  rev = NUM2LONG (aRev);
  Check_Type (aName, T_STRING);
  if (aValue != Qnil)
    Check_Type (aValue, T_STRING);

  pool = svn_pool_create (fs->pool);
  if (aValue == Qnil)
    value = NULL;
  else
    value = svn_string_ncreate (StringValuePtr (aValue),
                                RSTRING (aValue)->len, pool);

  err = svn_fs_change_rev_prop (fs->fs, rev,
				StringValuePtr (aName), value, pool);
  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);

  return Qnil;

}



static VALUE
fs_rev_root (VALUE self, VALUE aRev)
{
  svn_fs_root_t *root;
  svn_ruby_fs_t *fs;
  svn_revnum_t rev;
  apr_pool_t *pool;
  svn_error_t *err;

  rev = NUM2LONG (aRev);
  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");
  pool = svn_pool_create (NULL);
  err = svn_fs_revision_root (&root, fs->fs, rev, pool);
  if (err)
    svn_ruby_raise (err);

  return svn_ruby_fs_rev_root_new (root, pool);
}

static VALUE
fs_begin_txn (VALUE self, VALUE aRev)
{
  svn_fs_txn_t *txn;
  svn_ruby_fs_t *fs;
  svn_revnum_t rev;
  apr_pool_t *pool;
  svn_error_t *err;

  rev = NUM2LONG (aRev);
  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");
  pool = svn_pool_create (NULL);
  err = svn_fs_begin_txn (&txn, fs->fs, rev, pool);
  if (err)
    svn_ruby_raise (err);

  return svn_ruby_fs_txn_new (txn, pool);
}

static VALUE
fs_open_txn (VALUE self, VALUE aName)
{
  svn_fs_txn_t *txn;
  svn_ruby_fs_t *fs;
  apr_pool_t *pool;
  svn_error_t *err;

  Check_Type (aName, T_STRING);
  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");
  pool = svn_pool_create (NULL);
  err = svn_fs_open_txn (&txn, fs->fs, StringValuePtr (aName), pool);
  if (err)
    svn_ruby_raise (err);

  return svn_ruby_fs_txn_new (txn, pool);
}

static VALUE
fs_list_transactions (VALUE self)
{
  char **name;
  svn_ruby_fs_t *fs;
  apr_pool_t *pool;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_t, fs);
  if (fs->closed)
    rb_raise (rb_eRuntimeError, "closed fs");
  pool = svn_pool_create (fs->pool);
  err = svn_fs_list_transactions (&name, fs->fs, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  {
    VALUE obj;
    int i;

    obj = rb_ary_new ();

    for (i = 0; name[i]; i++)
      rb_ary_push (obj, rb_str_new2 (name[i]));

    apr_pool_destroy (pool);
    return obj;
  }
}



void
svn_ruby_init_fs ()
{
  cSvnFS = rb_define_class_under (svn_ruby_mSvn, "Fs", rb_cObject);
  rb_define_singleton_method (cSvnFS, "new", fs_open, 1);
  rb_define_singleton_method (cSvnFS, "open", fs_open, 1);
  rb_define_singleton_method (cSvnFS, "create", fs_create, 1);
  rb_define_singleton_method (cSvnFS, "delete", fs_delete, 1);
  rb_define_singleton_method (cSvnFS, "recover", fs_recover, 1);

  rb_define_method (cSvnFS, "initialize", fs_initialize, -1);
  rb_define_method (cSvnFS, "closed?", fs_is_closed, 0);
  rb_define_method (cSvnFS, "close", fs_close, 0);
  rb_define_method (cSvnFS, "youngestRev", fs_youngest_rev, 0);
  rb_define_method (cSvnFS, "revisionProp", fs_revision_prop, 2);
  rb_define_method (cSvnFS, "revisionProplist", fs_revision_proplist, 1);
  rb_define_method (cSvnFS, "changeRevProp", fs_change_rev_prop, 3);
  rb_define_method (cSvnFS, "revisionRoot", fs_rev_root, 1);
  rb_define_method (cSvnFS, "beginTxn", fs_begin_txn, 1);
  rb_define_method (cSvnFS, "openTxn", fs_open_txn, 1);
  rb_define_method (cSvnFS, "listTransactions", fs_list_transactions, 0);
}
