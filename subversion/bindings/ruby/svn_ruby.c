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

#include "svn_ruby.h"
void Init_svn (void);

VALUE svn_ruby_mSvn;

void
Init_svn (void)
{
  svn_ruby_init_apr ();

  svn_ruby_mSvn = rb_define_module ("Svn");

  svn_ruby_init_stream ();
  svn_ruby_init_txdelta ();
#if 0
  svn_ruby_init_delta_editor ();
#endif
  svn_ruby_init_error ();
  svn_ruby_init_types ();
#if 0
  svn_ruby_init_fs ();
  svn_ruby_init_fs_root ();
  svn_ruby_init_fs_node ();
  svn_ruby_init_fs_txn ();
  svn_ruby_init_repos ();

  svn_ruby_init_ra ();
#endif
  svn_ruby_init_wc ();
  svn_ruby_init_client ();
}
