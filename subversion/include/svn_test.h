/*
 * svn_test.h:  public interfaces for test programs
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"



/* Set *EDITOR and *EDIT_BATON to an editor that prints its arguments
 * to OUT_STREAM.  The edit starts at PATH, that is, PATH will be
 * prepended to the appropriate paths in the output.  Allocate the
 * editor in POOL.  The STYLE parameter exists to make this editor
 * fully compatible with all supported Subversion path types, and
 * should of course represent the path style appropriate for the
 * supplied PATH.
 *
 * VERBOSE is a flag for specifying whether or not your want all the
 * nitty gritty details displayed.  When VERBOSE is FALSE, each editor
 * function will print only a one-line summary. 
 *
 * INDENTATION is the number of spaces to indent by at each level; use
 * 0 for no indentation.  The indent level is always the same for a
 * given call (i.e, stack frame).
 * 
 */

/* SOME EXAMPLES */

/* 
 * With an indentation of 3, and with verbose = TRUE
 *

[EDITOR] set_target_revision (23)
[EDITOR] replace_root (wc)
   base_revision: 1
   [EDITOR] replace_directory (A)
      parent: wc
      base_revision: 1
      [EDITOR] replace_directory (B)
         parent: wc/A
         base_revision: 1
      [EDITOR] change_dir_prop (wc/A/B)
         name: foo
         value: bar
      [EDITOR] close_directory (wc/A/B)
   [EDITOR] delete_entry (mu)
   [EDITOR] close_directory (wc/A)
   [EDITOR] add_file (zeta)
      parent: wc
      copyfrom_path: 
      copyfrom_revision: 0
   [EDITOR] replace_file (iota)
      parent: wc
      base_revision: 1
   [EDITOR] apply_textdelta (iota)
      [EDITOR] window_handler (3 ops)
         (1) new text: length 4
         (2) target text: offset 24, length 6
         (3) unknown window type
      [EDITOR] window_handler (EOT)
   [EDITOR] close_directory (iota)
   [EDITOR] apply_textdelta (zeta)
      [EDITOR] window_handler (1 ops)
         (1) new text: length 4
      [EDITOR] window_handler (EOT)
   [EDITOR] close_directory (zeta)
[EDITOR] close_edit
*/

/* 
 * With an indentation of 3, and with verbose = FALSE
 *

[EDITOR] set_target_revision (23)
[EDITOR] replace_root (wc)
   [EDITOR] replace_directory (A)
      [EDITOR] replace_directory (B)
      [EDITOR] change_dir_prop (wc/A/B)
      [EDITOR] close_directory (wc/A/B)
   [EDITOR] delete_entry (mu)
   [EDITOR] close_directory (wc/A)
   [EDITOR] add_file (zeta)
   [EDITOR] replace_file (iota)
   [EDITOR] apply_textdelta (iota)
   [EDITOR] close_directory (iota)
   [EDITOR] apply_textdelta (zeta)
   [EDITOR] close_directory (zeta)
[EDITOR] close_edit
*/


/*
 * This is implemented in tests/libsvn_test_editor.la
 */
svn_error_t *svn_test_get_editor (const svn_delta_edit_fns_t **editor,
                                  void **edit_baton,
                                  svn_string_t *editor_name,
                                  svn_stream_t *out_stream,
                                  int indentation,
                                  svn_boolean_t verbose,
                                  svn_string_t *path,
                                  enum svn_path_style style,
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
