/*
 * xml-output-test.c:  simple XML-generation test
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

#include <stdio.h>

#include <apr_pools.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"

/* This is a really rough first-cut test program just to exercise the
 * code and see if it works.  It doesn't verify the output and can't
 * be hooked into the test framework. */

int main(int argc, char **argv)
{
  apr_pool_t *pool;
  const svn_delta_editor_t *editor;
  svn_txdelta_window_handler_t handler;
  svn_txdelta_window_t window;
  svn_txdelta_op_t op;
  void *edit_baton, *root_baton, *dir_baton, *file_baton, *handler_baton;
  svn_string_t *bbb_string;
  svn_string_t *ccc_string;

  apr_initialize();
  pool = svn_pool_create (NULL);

  bbb_string = svn_string_create ("bbb", pool);
  ccc_string = svn_string_create ("ccc", pool);

  window.sview_offset = 0;
  window.sview_len = 0;
  window.tview_len = 10;

  op.action_code = svn_txdelta_new;
  op.offset = 0;
  op.length = 10;
  window.num_ops = 1;
  window.ops = &op;

  window.new_data = svn_string_create ("test delta", pool);

  svn_delta_get_xml_editor (svn_stream_from_stdio (stdout, pool),
			    &editor, &edit_baton, pool);

  editor->set_target_revision (edit_baton, 3);
  editor->open_root (edit_baton, 2, pool, &root_baton);
  editor->open_directory ("foo", root_baton, 2, pool, &dir_baton);
  editor->open_file ("bar", dir_baton, 0, pool, &file_baton);
  editor->apply_textdelta (file_baton, &handler, &handler_baton);
  handler (&window, handler_baton);
  handler (NULL, handler_baton);
  editor->close_file (file_baton);
  editor->open_file ("baz", dir_baton, 0, pool, &file_baton);
  editor->change_file_prop (file_baton, "bbb", ccc_string, pool);
  editor->change_file_prop (file_baton, "aaa", NULL, pool);
  editor->change_dir_prop (dir_baton, "ccc", bbb_string, pool);
  editor->close_directory (dir_baton);
  editor->close_directory (root_baton);
  editor->apply_textdelta (file_baton, &handler, &handler_baton);
  handler (NULL, handler_baton);
  editor->close_file (file_baton);
  editor->close_edit (edit_baton);
  return 0;
}
