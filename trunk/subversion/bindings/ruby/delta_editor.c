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

#include <svn_delta.h>
#include <svn_error.h>
#include <svn_pools.h>

#include "svn_ruby.h"
#include "util.h"
#include "txdelta.h"
#include "delta_editor.h"
#include "error.h"

static VALUE cSvnRubyEditor;
static VALUE cSvnCommitEditor;

typedef struct svn_ruby_delta_edit_t
{
  const svn_delta_edit_fns_t *editor;
  apr_pool_t *pool;
} svn_ruby_delta_edit_t;

typedef struct baton_list
{
  void *baton;
  struct baton_list *next;
} baton_list;

typedef struct svn_ruby_commit_editor_t
{
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  baton_list *dir_baton, *file_baton;
  apr_pool_t *pool;
} svn_ruby_commit_editor_t;

void
svn_ruby_delta_editor (const svn_delta_edit_fns_t **editor,
                       void **edit_baton, VALUE aEditor)
{
  VALUE c;

  for (c = CLASS_OF (aEditor); RCLASS (c)->super; c = RCLASS (c)->super)
    {
      if (c == cSvnRubyEditor)
        {
          svn_ruby_delta_edit_t *rb_editor;
          Data_Get_Struct (aEditor, svn_ruby_delta_edit_t, rb_editor);
          *editor = rb_editor->editor;
          *edit_baton = (void *) aEditor;
          return;
        }
    }

  rb_raise (rb_eTypeError, "Object must be the subclass of Svn::DeltaEditor");
}



static svn_error_t *
set_target_revision (void *edit_baton,
                     svn_revnum_t target_revision)
{
  VALUE self = (VALUE) edit_baton;
  int error;
  VALUE args[3];

  args[0] = self;
  args[1] = (VALUE) "setTargetRevision";
  args[2] = LONG2NUM (target_revision);
  
  rb_protect (svn_ruby_protect_call1, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;
      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);

      return svn_ruby_error ("setTargetRevision", editor->pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           void **root_baton)
{
  VALUE self = (VALUE) edit_baton;
  int error;
  VALUE args[3];

  *root_baton = edit_baton;

  args[0] = self;
  args[1] = (VALUE) "openRoot";
  args[2] = LONG2NUM (base_revision);
  
  rb_protect (svn_ruby_protect_call1, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("openRoot", editor->pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry (svn_stringbuf_t *name,
	      svn_revnum_t revision,
              void *parent_baton)
{
  VALUE self = (VALUE) parent_baton;
  int error;
  VALUE args[4];

  args[0] = self;
  args[1] = (VALUE) "deleteEntry";
  args[2] = rb_str_new (name->data, name->len);
  args[3] = LONG2NUM (revision);
  
  rb_protect (svn_ruby_protect_call2, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("deleteEntry", editor->pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  VALUE self = (VALUE) parent_baton;
  int error;
  VALUE args[5];

  *child_baton =  parent_baton;

  args[0] = self;
  args[1] = (VALUE) "addDirectory";
  args[2] = rb_str_new (name->data, name->len);
  if (copyfrom_path)
    {
      args[3] = rb_str_new (copyfrom_path->data, copyfrom_path->len);
      args[4] = LONG2NUM (copyfrom_revision);
    }
  else
    {
      args[3] = Qnil;
      args[4] = Qnil;
    }
  
  rb_protect (svn_ruby_protect_call3, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("addDirectory", editor->pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  VALUE self = (VALUE) parent_baton;
  int error;
  VALUE args[4];

  *child_baton = parent_baton;

  args[0] = self;
  args[1] = (VALUE) "openDirectory";
  args[2] = rb_str_new (name->data, name->len);
  args[3] = LONG2NUM (base_revision);
  
  rb_protect (svn_ruby_protect_call2, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("openDirectory", editor->pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop (void *dir_baton,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *value)
{
  VALUE self = (VALUE) dir_baton;
  int error;
  VALUE args[4];

  args[0] = self;
  args[1] = (VALUE) "changeDirProp";
  args[2] = rb_str_new (name->data, name->len);
  args[3] = rb_str_new (value->data, value->len);
  
  rb_protect (svn_ruby_protect_call2, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("changeDirProp", editor->pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
close_directory (void *dir_baton)
{
  VALUE self = (VALUE) dir_baton;
  int error;
  VALUE args[2];

  args[0] = self;
  args[1] = (VALUE) "closeDirectory";

  rb_protect (svn_ruby_protect_call0, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("closeDirectory", editor->pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  VALUE self = (VALUE) parent_baton;
  int error;
  VALUE args[5];

  *file_baton = parent_baton;

  args[0] = self;
  args[1] = (VALUE) "addFile";
  args[2] = rb_str_new (name->data, name->len);
  if (copyfrom_path)
    {
      args[3] = rb_str_new (copyfrom_path->data, copyfrom_path->len);
      args[4] = LONG2NUM (copyfrom_revision);
    }
  else
    {
      args[3] = Qnil;
      args[4] = Qnil;
    }
  
  rb_protect (svn_ruby_protect_call3, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("addFile", editor->pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
open_file (svn_stringbuf_t *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           void **file_baton)
{
  VALUE self = (VALUE) parent_baton;
  int error;
  VALUE args[4];

  *file_baton = parent_baton;

  args[0] = self;
  args[1] = (VALUE) "openFile";
  args[2] = rb_str_new (name->data, name->len);
  args[3] = LONG2NUM (base_revision);
  
  rb_protect (svn_ruby_protect_call2, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("openFile", editor->pool);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  VALUE self = (VALUE) file_baton;
  VALUE obj;

  int error;
  VALUE args[2];

  args[0] = self;
  args[1] = (VALUE) "applyTextDelta";

  obj = rb_protect (svn_ruby_protect_call0, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("applyTextDelta", editor->pool);
    }
  else
    {
      svn_ruby_txdelta (obj, handler, handler_baton);
      if (*handler == NULL)
        {
          svn_ruby_delta_edit_t *editor;

          Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
	  return svn_ruby_error ("applyTextDelta returned wrong object",
				 editor->pool);
        }
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  svn_stringbuf_t *name,
                  svn_stringbuf_t *value)
{
  VALUE self = (VALUE) file_baton;
  int error;
  VALUE args[4];

  args[0] = self;
  args[1] = (VALUE) "changeFileProp";
  args[2] = rb_str_new (name->data, name->len);
  args[3] = rb_str_new (value->data, value->len);
  
  rb_protect (svn_ruby_protect_call2, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("changeFileProp", editor->pool);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton)
{
  VALUE self = (VALUE) file_baton;
  int error;
  VALUE args[2];

  args[0] = self;
  args[1] = (VALUE) "closeFile";

  rb_protect (svn_ruby_protect_call0, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("closeFile", editor->pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit (void *edit_baton)
{
  VALUE self = (VALUE) edit_baton;
  int error;
  VALUE args[2];

  args[0] = self;
  args[1] = (VALUE) "closeEdit";

  rb_protect (svn_ruby_protect_call0, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("closeEdit", editor->pool);
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
abort_edit (void *edit_baton)
{
  VALUE self = (VALUE) edit_baton;
  int error;
  VALUE args[2];

  args[0] = self;
  args[1] = (VALUE) "abortEdit";

  rb_protect (svn_ruby_protect_call0, (VALUE) args, &error);

  if (error)
    {
      svn_ruby_delta_edit_t *editor;

      Data_Get_Struct (self, svn_ruby_delta_edit_t, editor);
      return svn_ruby_error ("abortEdit", editor->pool);
    }
  return SVN_NO_ERROR;
}

static const svn_delta_edit_fns_t rb_editor =
{
  set_target_revision,
  open_root,
  delete_entry,
  add_directory,
  open_directory,
  change_dir_prop,
  close_directory,
  add_file,
  open_file,
  apply_textdelta,
  change_file_prop,
  close_file,
  close_edit,
  abort_edit
};

static void
free_delta (void *p)
{
  svn_ruby_delta_edit_t *editor = p;

  apr_pool_destroy (editor->pool);
  free (editor);
}

static VALUE
delta_new (int argc, VALUE *argv, VALUE class)
{
  VALUE obj;
  svn_ruby_delta_edit_t *editor;

  obj = Data_Make_Struct (class, svn_ruby_delta_edit_t,
                          0, free_delta, editor);
  editor->pool = svn_pool_create (NULL);
  editor->editor = apr_palloc (editor->pool, sizeof (*editor->editor));
  editor->editor = &rb_editor;
  rb_obj_call_init (obj, argc, argv);

  return obj;
}



static VALUE
em_set_target_revision (VALUE self, VALUE aRevision)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_open_root (VALUE self, VALUE aRevision)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_delete_entry (VALUE self, VALUE aName, VALUE aRevision)
{
  rb_notimplement ();
  return Qnil;
}


static VALUE
em_add_directory (VALUE self, VALUE copyfromPath, VALUE copyfromRevision)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_open_directory (VALUE self, VALUE aName, VALUE aRevision)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_change_dir_prop (VALUE self, VALUE aName, VALUE aValue)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_close_directory (VALUE self)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_add_file (VALUE self, VALUE copyfromPath, VALUE coypfromRevision)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_open_file (VALUE self, VALUE aName, VALUE aRevision)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_apply_textdelta (VALUE self)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_change_file_prop (VALUE self, VALUE aName, VALUE aValue)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_close_file (VALUE self)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_close_edit (VALUE self)
{
  rb_notimplement ();
  return Qnil;
}

static VALUE
em_abort_edit (VALUE self)
{
  rb_notimplement ();
  return Qnil;
}



/* #### CommitEditor is allocated as part of ra's session baton.
   You must devise some way (like reference counting) to keep pool
   alive.  You also have to prevent crash when editor is drived after
   ra is closed.  */

static void
free_ce (void *p)
{
  svn_ruby_commit_editor_t *ce = p;
  free (ce);
}

VALUE
svn_ruby_commit_editor_new (const svn_delta_edit_fns_t *editor,
                            void *edit_baton,
                            apr_pool_t *pool)
{
  VALUE obj;
  svn_ruby_commit_editor_t *ce;

  obj = Data_Make_Struct (cSvnCommitEditor, svn_ruby_commit_editor_t,
                          0, free_ce, ce);
  ce->editor = editor;
  ce->edit_baton = edit_baton;
  ce->pool = pool;
  rb_obj_call_init (obj, 0, 0);

  return obj;
}

static VALUE
ce_set_target_revision (VALUE self, VALUE aRevision)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;

  svn_revnum_t revision;

  revision = NUM2LONG (aRevision);
  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);

  err = ce->editor->set_target_revision (ce->edit_baton, revision);
  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
ce_open_root (VALUE self, VALUE aRevision)
{
  svn_ruby_commit_editor_t *ce;
  baton_list *dir_baton;
  svn_error_t *err;

  svn_revnum_t revision;

  revision = NUM2LONG (aRevision);
  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  dir_baton = apr_pcalloc (ce->pool, sizeof (*dir_baton));

  err = ce->editor->open_root (ce->edit_baton, revision, &dir_baton->baton);
  if (err)
    svn_ruby_raise (err);
  ce->dir_baton = dir_baton;

  return Qnil;
}

static VALUE
ce_delete_entry (VALUE self, VALUE aName, VALUE aRevision)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;
  apr_pool_t *pool;

  svn_stringbuf_t *name;
  svn_revnum_t revision;

  Check_Type (aName, T_STRING);
  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  revision = NUM2LONG (aRevision);
  pool = svn_pool_create (ce->pool);
  name = svn_stringbuf_create (StringValuePtr (aName), pool);

  err = ce->editor->delete_entry (name, revision, ce->dir_baton->baton);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  apr_pool_destroy (pool);
  return Qnil;
}

static VALUE
ce_add_directory (int argc, VALUE *argv, VALUE self)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;
  apr_pool_t *pool;
  baton_list *dir_baton;

  VALUE aName, aPath, aRevision;

  svn_stringbuf_t *name;
  svn_stringbuf_t *copyfrom_path = NULL;
  svn_revnum_t copyfrom_revision = SVN_INVALID_REVNUM;

  rb_scan_args (argc, argv, "12", &aName, &aPath, &aRevision);

  if (aRevision != Qnil)
    copyfrom_revision = NUM2LONG (aRevision);

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  if (ce->dir_baton == NULL)
    rb_raise (rb_eRuntimeError,
              "openRoot, openDirectory or addDirectory "
              "must be called beforehand");
  Check_Type (aName, T_STRING);
  if (aPath != Qnil)
    Check_Type (aPath, T_STRING);
  pool = svn_pool_create (ce->pool);
  name = svn_stringbuf_create (StringValuePtr (aName), pool);

  if (aPath != Qnil)
    copyfrom_path = svn_stringbuf_create (StringValuePtr (aPath), pool);

  dir_baton = apr_pcalloc (ce->pool, sizeof (*dir_baton));
  err = ce->editor->add_directory (name, ce->dir_baton->baton,
                                   copyfrom_path, copyfrom_revision,
                                   &dir_baton->baton);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  dir_baton->next = ce->dir_baton;
  ce->dir_baton = dir_baton;
  apr_pool_destroy (pool);
  return Qnil;
}

static VALUE
ce_open_directory (VALUE self, VALUE aName, VALUE aRevision)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;
  apr_pool_t *pool;
  baton_list *dir_baton;

  svn_stringbuf_t *name;
  svn_revnum_t base_revision;

  base_revision = NUM2LONG (aRevision);

  Check_Type (aName, T_STRING);
  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  pool = svn_pool_create (ce->pool);
  name = svn_stringbuf_create (StringValuePtr (aName), pool);

  dir_baton = apr_pcalloc (ce->pool, sizeof (*dir_baton));
  err = ce->editor->open_directory (name, ce->dir_baton->baton,
                                       base_revision,
                                       &dir_baton->baton);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  dir_baton->next = ce->dir_baton;
  ce->dir_baton = dir_baton;
  apr_pool_destroy (pool);
  return Qnil;
}

static VALUE
ce_change_dir_prop (VALUE self, VALUE aName, VALUE aValue)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;
  apr_pool_t *pool;

  svn_stringbuf_t *name, *value;

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  if (ce->dir_baton == NULL)
    rb_raise (rb_eRuntimeError,
              "openRoot, openDirectory or addDirectory "
              "must be called beforehand");

  Check_Type (aName, T_STRING);
  Check_Type (aValue, T_STRING);
  pool = svn_pool_create (ce->pool);
  name = svn_stringbuf_create (StringValuePtr (aName), pool);
  value = svn_stringbuf_ncreate (StringValuePtr (aValue),
                                 RSTRING (aValue)->len, pool);

  err = ce->editor->change_dir_prop (ce->dir_baton->baton,
                                     name, value);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  apr_pool_destroy (pool);

  return Qnil;
}

static VALUE
ce_close_directory (VALUE self)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  if (ce->dir_baton == NULL)
    rb_raise (rb_eRuntimeError, "No directory to close");

  err = ce->editor->close_directory (ce->dir_baton->baton);

  if (err)
    svn_ruby_raise (err);

  ce->dir_baton = ce->dir_baton->next;
  return Qnil;
}

static VALUE
ce_add_file (int argc, VALUE *argv, VALUE self)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;
  apr_pool_t *pool;
  baton_list *file_baton;

  VALUE aName, aPath, aRevision;

  svn_stringbuf_t *name;
  svn_stringbuf_t *copyfrom_path = NULL;
  svn_revnum_t copyfrom_revision = SVN_INVALID_REVNUM;

  rb_scan_args (argc, argv, "12", &aName, &aPath, &aRevision);

  if (aRevision != Qnil)
    copyfrom_revision = NUM2LONG (aRevision);

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  if (ce->dir_baton == NULL)
    rb_raise (rb_eRuntimeError,
              "openRoot, openDirectory or addDirectory "
              "must be called beforehand");
  Check_Type (aName, T_STRING);
  if (aPath != Qnil)
    Check_Type (aPath, T_STRING);
  pool = svn_pool_create (ce->pool);
  name = svn_stringbuf_create (StringValuePtr (aName), pool);

  if (aPath != Qnil)
    copyfrom_path = svn_stringbuf_create (StringValuePtr (aPath), pool);

  file_baton = apr_pcalloc (ce->pool, sizeof (*file_baton));
  err = ce->editor->add_file (name, ce->dir_baton->baton,
                              copyfrom_path, copyfrom_revision,
                              &file_baton->baton);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  file_baton->next = ce->file_baton;
  ce->file_baton = file_baton;
  apr_pool_destroy (pool);
  return Qnil;
}

static VALUE
ce_open_file (VALUE self, VALUE aName, VALUE aRevision)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;
  apr_pool_t *pool;
  baton_list *file_baton;

  svn_stringbuf_t *name;
  svn_revnum_t base_revision;

  base_revision = NUM2LONG (aRevision);

  Check_Type (aName, T_STRING);
  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  pool = svn_pool_create (ce->pool);
  name = svn_stringbuf_create (StringValuePtr (aName), pool);

  file_baton = apr_pcalloc (ce->pool, sizeof (*file_baton));
  err = ce->editor->open_file (name, ce->dir_baton->baton,
                               base_revision,
                               &file_baton->baton);
  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  file_baton->next = ce->file_baton;
  ce->file_baton = file_baton;
  apr_pool_destroy (pool);
  return Qnil;
}

static VALUE
ce_apply_textdelta (VALUE self)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;

  svn_txdelta_window_handler_t handler;
  void *handler_baton;

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  if (ce->file_baton == NULL)
    rb_raise (rb_eRuntimeError,
              "openFile or addFile must be called beforehand");
  err = ce->editor->apply_textdelta (ce->file_baton->baton,
                                     &handler, &handler_baton);
  if (err)
    svn_ruby_raise (err);

  /* #### Fix unused pool creation. */
  return svn_ruby_txdelta_new (handler, handler_baton,
                               svn_pool_create (ce->pool));
}

static VALUE
ce_change_file_prop (VALUE self, VALUE aName, VALUE aValue)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;
  apr_pool_t *pool;

  svn_stringbuf_t *name, *value;

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  if (ce->file_baton == NULL)
    rb_raise (rb_eRuntimeError,
              "openFile or addFile must be called beforehand");

  Check_Type (aName, T_STRING);
  Check_Type (aValue, T_STRING);
  pool = svn_pool_create (ce->pool);
  name = svn_stringbuf_create (StringValuePtr (aName), pool);
  value = svn_stringbuf_ncreate (StringValuePtr (aValue),
                                 RSTRING (aValue)->len, pool);

  err = ce->editor->change_file_prop (ce->file_baton->baton,
                                      name, value);

  if (err)
    {
      apr_pool_destroy (pool);
      svn_ruby_raise (err);
    }

  apr_pool_destroy (pool);

  return Qnil;
}

static VALUE
ce_close_file (VALUE self)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);
  if (ce->dir_baton == NULL)
    rb_raise (rb_eRuntimeError, "No file to close");

  err = ce->editor->close_file (ce->file_baton->baton);

  if (err)
    svn_ruby_raise (err);

  ce->file_baton = ce->file_baton->next;
  return Qnil;
}

static VALUE
ce_close_edit (VALUE self)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);

  err = ce->editor->close_edit (ce->edit_baton);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}

static VALUE
ce_abort_edit (VALUE self)
{
  svn_ruby_commit_editor_t *ce;
  svn_error_t *err;

  Data_Get_Struct (self, svn_ruby_commit_editor_t, ce);

  err = ce->editor->abort_edit (ce->edit_baton);

  if (err)
    svn_ruby_raise (err);

  return Qnil;
}



void
svn_ruby_init_delta_editor (void)
{
  VALUE cSvnDeltaEditor;

  cSvnDeltaEditor = rb_define_class_under (svn_ruby_mSvn,
                                           "DeltaEditor", rb_cObject);
  rb_undef_method (CLASS_OF (cSvnRubyEditor), "new");
  cSvnRubyEditor = rb_define_class_under (svn_ruby_mSvn,
                                          "RubyEditor", cSvnDeltaEditor);
  rb_define_singleton_method (cSvnRubyEditor, "new", delta_new, -1);
  rb_define_method (cSvnRubyEditor, "setTargetRevision",
                    em_set_target_revision, 1);
  rb_define_method (cSvnRubyEditor, "openRoot", em_open_root, 1);
  rb_define_method (cSvnRubyEditor, "deleteEntry", em_delete_entry, 2);
  rb_define_method (cSvnRubyEditor, "addDirectory", em_add_directory, 3);
  rb_define_method (cSvnRubyEditor, "openDirectory",
                    em_open_directory, 2);
  rb_define_method (cSvnRubyEditor, "changeDirProp", em_change_dir_prop, 2);
  rb_define_method (cSvnRubyEditor, "closeDirectory", em_close_directory, 0);
  rb_define_method (cSvnRubyEditor, "addFile", em_add_file, 3);
  rb_define_method (cSvnRubyEditor, "openFile", em_open_file, 2);
  rb_define_method (cSvnRubyEditor, "applyTextDelta", em_apply_textdelta, 0);
  rb_define_method (cSvnRubyEditor, "changeFileProp", em_change_file_prop, 2);
  rb_define_method (cSvnRubyEditor, "closeFile", em_close_file, 0);
  rb_define_method (cSvnRubyEditor, "closeEdit", em_close_edit, 0);
  rb_define_method (cSvnRubyEditor, "abortEdit", em_abort_edit, 0);
  cSvnCommitEditor = rb_define_class_under (svn_ruby_mSvn,
                                            "CommitEditor", cSvnDeltaEditor);
  rb_define_method (cSvnCommitEditor, "setTargetRevision",
                    ce_set_target_revision, 1);
  rb_define_method (cSvnCommitEditor, "openRoot", ce_open_root, 1);
  rb_define_method (cSvnCommitEditor, "deleteEntry", ce_delete_entry, 2);
  rb_define_method (cSvnCommitEditor, "addDirectory", ce_add_directory, -1);
  rb_define_method (cSvnCommitEditor, "openDirectory",
                    ce_open_directory, 2);
  rb_define_method (cSvnCommitEditor, "changeDirProp", ce_change_dir_prop, 2);
  rb_define_method (cSvnCommitEditor, "closeDirectory", ce_close_directory, 0);
  rb_define_method (cSvnCommitEditor, "addFile", ce_add_file, -1);
  rb_define_method (cSvnCommitEditor, "openFile", ce_open_file, 2);
  rb_define_method (cSvnCommitEditor, "applyTextDelta", ce_apply_textdelta, 0);
  rb_define_method (cSvnCommitEditor, "changeFileProp", ce_change_file_prop, 2);
  rb_define_method (cSvnCommitEditor, "closeFile", ce_close_file, 0);
  rb_define_method (cSvnCommitEditor, "closeEdit", ce_close_edit, 0);
  rb_define_method (cSvnCommitEditor, "abortEdit", ce_abort_edit, 0);
}
