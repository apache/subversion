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
#include "fs_txn.h"
#include "fs_root.h"
#include "error.h"
#include "util.h"

static VALUE cSvnFsTxn;

typedef struct svn_ruby_fs_txn_t
{
  svn_fs_txn_t *txn;
  apr_pool_t *pool;
  svn_boolean_t closed;
} svn_ruby_fs_txn_t;

static void
free_fs_txn (void *p)
{
  svn_ruby_fs_txn_t *txn = p;
  if (!txn->closed)
    svn_fs_close_txn (txn->txn);
  free (txn);
}

VALUE
svn_ruby_fs_txn_new (svn_fs_txn_t *txn, apr_pool_t *pool)
{
  VALUE obj;
  svn_ruby_fs_txn_t *rb_txn;

  obj = Data_Make_Struct (cSvnFsTxn, svn_ruby_fs_txn_t,
                          0, free_fs_txn, rb_txn);
  rb_txn->txn = txn;
  rb_txn->pool = pool;
  rb_txn->closed = FALSE;

  return obj;
}

static VALUE
txn_name (VALUE self)
{
  const char *name;
  svn_ruby_fs_txn_t *txn;
  apr_pool_t *pool;
  svn_error_t *err;
  VALUE obj;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");
  pool = svn_pool_create (txn->pool);
  err = svn_fs_txn_name (&name, txn->txn, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }
  
  obj = rb_str_new2 (name);
  apr_pool_destroy (pool);

  return obj;
}

/* #### Redo exception raising function so that it can contain
   message. */
static VALUE
commit_txn (VALUE self)
{
  const char *conflict;
  svn_revnum_t new_rev;
  svn_ruby_fs_txn_t *txn;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");
  err = svn_fs_commit_txn (&conflict, &new_rev, txn->txn);
  if (err)
    svn_ruby_raise (err);

  return LONG2NUM (new_rev);
}

static VALUE
abort_txn (VALUE self)
{
  svn_ruby_fs_txn_t *txn;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");
  err = svn_fs_abort_txn (txn->txn);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
txn_prop (VALUE self, VALUE aPropname)
{
  svn_string_t *value;
  svn_ruby_fs_txn_t *txn;
  apr_pool_t *pool;
  svn_error_t *err;
  VALUE obj;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");

  Check_Type (aPropname, T_STRING);
  pool = svn_pool_create (txn->pool);

  err = svn_fs_txn_prop (&value, txn->txn, StringValuePtr (aPropname), pool);
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
txn_proplist (VALUE self, VALUE aRev)
{
  svn_ruby_fs_txn_t *txn;
  apr_hash_t *table_p;
  apr_pool_t *pool;
  svn_error_t *err;

  VALUE obj;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");

  pool = svn_pool_create (NULL);
  err = svn_fs_txn_proplist (&table_p, txn->txn, pool);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  obj = svn_ruby_str_hash (table_p, pool);
  apr_pool_destroy (pool);
  return obj;
}

static VALUE
change_txn_prop (VALUE self, VALUE aName, VALUE aValue)
{
  svn_ruby_fs_txn_t *txn;
  const svn_string_t *value;
  apr_pool_t *pool;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");

  Check_Type (aName, T_STRING);
  if (aValue != Qnil)
    Check_Type (aValue, T_STRING);

  pool = svn_pool_create (txn->pool);
  if (aValue == Qnil)
    value = NULL;
  else
    value = svn_string_ncreate (StringValuePtr (aValue),
                                RSTRING (aValue)->len, pool);

  err = svn_fs_change_txn_prop (txn->txn, StringValuePtr (aName), value, pool);
  apr_pool_destroy (pool);
  if (err)
    svn_ruby_raise (err);

  return Qnil;

}


static VALUE
close_txn (VALUE self)
{
  svn_ruby_fs_txn_t *txn;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");
  err = svn_fs_close_txn (txn->txn);
  if (err)
    svn_ruby_raise (err);
  txn->closed = 1;

  return Qnil;
}

static VALUE
closed (VALUE self)
{
  svn_ruby_fs_txn_t *txn;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    return Qtrue;
  else
    return Qfalse;
}

static VALUE
base_revision (VALUE self)
{
  svn_ruby_fs_txn_t *txn;
  svn_revnum_t rev;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");
  rev = svn_fs_txn_base_revision (txn->txn);

  return INT2NUM (rev);
}

static VALUE
txn_root (VALUE self)
{
  svn_fs_root_t *root;
  svn_ruby_fs_txn_t *txn;
  apr_pool_t *pool;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_fs_txn_t, txn);
  if (txn->closed)
    rb_raise (rb_eRuntimeError, "closed transaction");
  pool = svn_pool_create (txn->pool);
  err = svn_fs_txn_root (&root, txn->txn, pool);
  if (err)
    svn_ruby_raise (err);

  return svn_ruby_fs_txn_root_new (root, pool);
}

void
svn_ruby_init_fs_txn (void)
{
  cSvnFsTxn = rb_define_class_under (svn_ruby_mSvn, "FsTxn", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnFsTxn), "new");
  rb_define_method (cSvnFsTxn, "name", txn_name, 0);
  rb_define_method (cSvnFsTxn, "commit", commit_txn, 0);
  rb_define_method (cSvnFsTxn, "prop", txn_prop, 1);
  rb_define_method (cSvnFsTxn, "proplist", txn_proplist, 0);
  rb_define_method (cSvnFsTxn, "changeProp", change_txn_prop, 2);
  rb_define_method (cSvnFsTxn, "abort", abort_txn, 0);
  rb_define_method (cSvnFsTxn, "close", close_txn, 0);
  rb_define_method (cSvnFsTxn, "closed?", closed, 0);
  rb_define_method (cSvnFsTxn, "baseRevision", base_revision, 0);
  rb_define_method (cSvnFsTxn, "txnRoot", txn_root, 0);

}
