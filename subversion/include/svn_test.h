/*
 * svn_test.h:  public interfaces for test programs
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_TEST_H
#define SVN_TEST_H

#include <apr_pools.h>
#include "svn_delta.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"



/* Set *EDITOR and *EDIT_BATON to an editor that prints its arguments
 * to OUT_STREAM.  The edit starts at PATH, that is, PATH will be
 * prepended to the appropriate paths in the output.  Allocate the
 * editor in POOL. 
 *
 * INDENTATION is the number of spaces to indent by at each level; use
 * 0 for no indentation.  The indent level is always the same for a
 * given call (i.e, stack frame).
 *
 * Without indentation, the output looks like this (where "blah" and
 * "N" are strings and numbers, respectively):
 *
 *   CALLED set_target_revision
 *   target_revision: N
 *
 *   CALLED replace_root
 *   path: blah
 *   base_revision: N
 *
 *   CALLED delete_entry
 *   parent: blah
 *   name: blah
 *
 *   CALLED add_directory
 *   parent: blah
 *   name: blah
 *   copyfrom_path: blah
 *   copyfrom_revision: N
 *
 *   CALLED replace_directory
 *   parent: blah
 *   name: blah
 *   base_revision: N
 *
 *   CALLED change_dir_prop
 *   path: blah
 *   name: blah
 *   value: blah
 *
 *   CALLED close_directory
 *   path: blah
 *
 *   CALLED add_file
 *   parent: blah
 *   name: blah
 *   copyfrom_path: blah
 *   copyfrom_revision: N
 *
 *   CALLED replace_file
 *   parent: blah
 *   name: blah
 *   base_revision: N
 *
 *   CALLED apply_textdelta
 *   path: blah
 *
 *   CALLED window_handler
 *   new text: length N                          // For window_handler,
 *   source text: offset N, length M             // just one of these four
 *   target text: offset N, length M             // lines will be printed
 *   end                                         // for a given call.
 *
 *   CALLED change_file_prop
 *   path: blah
 *   name: blah
 *   value: blah
 *
 *   CALLED close_file
 *   path: blah
 *
 *   CALLED close_edit
 *
 * This is implemented in tests/libsvn_test_editor.la
 */
svn_error_t *svn_test_get_editor (const svn_delta_edit_fns_t **editor,
                                  void **edit_baton,
                                  svn_stream_t *out_stream,
                                  int indentation,
                                  svn_string_t *path,
                                  apr_pool_t *pool);
     
#endif /* SVN_TEST_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
