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

#include <svn_types.h>
#include <svn_error.h>
#include "svn_ruby.h"

static VALUE
is_valid_revnum (VALUE class, VALUE aRevnum)
{
  svn_revnum_t revnum = NUM2LONG (aRevnum);

  if (SVN_IS_VALID_REVNUM (revnum))
    return Qtrue;
  else
    return Qfalse;
}

static void
define_prop (VALUE module, const char *name, const char *value)
{
  rb_define_const (module, name, rb_str_new2 (value));
}

void
svn_ruby_init_types (void)
{
  VALUE mSvnNodeKind, mSvnRevnum, mSvnProp;
  mSvnNodeKind = rb_define_module_under (svn_ruby_mSvn, "NodeKind");
  rb_define_const (mSvnNodeKind, "NONE", INT2FIX (svn_node_none));
  rb_define_const (mSvnNodeKind, "FILE", INT2FIX (svn_node_file));
  rb_define_const (mSvnNodeKind, "DIR", INT2FIX (svn_node_dir));
  rb_define_const (mSvnNodeKind, "UNKNOWN", INT2FIX (svn_node_unknown));
  mSvnRevnum = rb_define_module_under (svn_ruby_mSvn, "Revnum");
  rb_define_const (mSvnRevnum, "INVALID_REVNUM", INT2FIX (SVN_INVALID_REVNUM));
  rb_define_const (mSvnRevnum, "IGNORED_REVNUM", INT2FIX (SVN_IGNORED_REVNUM));
  rb_define_singleton_method (mSvnRevnum, "validRevnum?", is_valid_revnum, 1);
  mSvnProp = rb_define_module_under (svn_ruby_mSvn, "Prop");
  define_prop (mSvnProp, "PREFIX", SVN_PROP_PREFIX);
  define_prop (mSvnProp, "REVISION_AUTHOR", SVN_PROP_REVISION_AUTHOR);
  define_prop (mSvnProp, "REVISION_LOG", SVN_PROP_REVISION_LOG);
  define_prop (mSvnProp, "REVISION_DATE", SVN_PROP_REVISION_DATE);
  define_prop (mSvnProp, "MIME_TYPE", SVN_PROP_MIME_TYPE);
  define_prop (mSvnProp, "IGNORE", SVN_PROP_IGNORE);
  define_prop (mSvnProp, "CHARSET", SVN_PROP_CHARSET);
  define_prop (mSvnProp, "WC_PREFIX", SVN_PROP_WC_PREFIX);
  define_prop (mSvnProp, "CUSTOM_PREFIX", SVN_PROP_CUSTOM_PREFIX);
}
