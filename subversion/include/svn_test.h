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
 * EDITOR_NAME is a name for the editor, a string that will be
 * prepended to the editor output as shown below.  EDITOR_NAME may
 * be the empty string, but it may not be null.
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
 * With an indentation of 3, editor name of "COMMIT-TEST" and with
 * verbose = TRUE
 */

/*
 * [COMMIT-TEST] replace_root (wc)
 * base_revision: 1
 * 
 *    [COMMIT-TEST] replace_directory (wc/A)
 *    parent: wc
 *    base_revision: 1
 * 
 *       [COMMIT-TEST] delete_entry (wc/A/B)
 * 
 *       [COMMIT-TEST] replace_file (wc/A/mu)
 *       parent: wc/A
 *       base_revision: 1
 * 
 *          [COMMIT-TEST] change_file_prop (wc/A/mu)
 *          name: foo
 *          value: bar
 * 
 *       [COMMIT-TEST] close_file (wc/A/mu)
 * 
 *    [COMMIT-TEST] close_directory (wc/A)
 * 
 *    [COMMIT-TEST] add_file (wc/zeta)
 *    parent: wc
 *    copyfrom_path: 
 *    copyfrom_revision: 0
 * 
 *    [COMMIT-TEST] replace_file (wc/iota)
 *    parent: wc
 *    base_revision: 1
 * 
 * [COMMIT-TEST] close_directory (wc)
 * 
 *       [COMMIT-TEST] apply_textdelta (wc/iota)
 * 
 *          [COMMIT-TEST] window_handler (2 ops)
 *          (1) new text: length 11
 *          (2) source text: offset 0, length 0
 * 
 *          [COMMIT-TEST] window_handler (EOT)
 * 
 *    [COMMIT-TEST] close_file (wc/iota)
 * 
 *       [COMMIT-TEST] apply_textdelta (wc/zeta)
 * 
 *          [COMMIT-TEST] window_handler (1 ops)
 *          (1) new text: length 11
 * 
 *          [COMMIT-TEST] window_handler (EOT)
 * 
 *    [COMMIT-TEST] close_file (wc/zeta)
 * 
 * [COMMIT-TEST] close_edit
 *  
 */

/* 
 * The same example as above, but with verbose = FALSE
 */

/*
 * [COMMIT-TEST] replace_root (wc)
 *    [COMMIT-TEST] replace_directory (wc/A)
 *       [COMMIT-TEST] delete_entry (wc/A/B)
 *       [COMMIT-TEST] replace_file (wc/A/mu)
 *          [COMMIT-TEST] change_file_prop (wc/A/mu)
 *       [COMMIT-TEST] close_file (wc/A/mu)
 *    [COMMIT-TEST] close_directory (wc/A)
 *    [COMMIT-TEST] add_file (wc/zeta)
 *    [COMMIT-TEST] replace_file (wc/iota)
 * [COMMIT-TEST] close_directory (wc)
 *       [COMMIT-TEST] apply_textdelta (wc/iota)
 *    [COMMIT-TEST] close_file (wc/iota)
 *       [COMMIT-TEST] apply_textdelta (wc/zeta)
 *    [COMMIT-TEST] close_file (wc/zeta)
 * [COMMIT-TEST] close_edit
 */


/*
 * This is implemented in tests/libsvn_test_editor.la
 */
svn_error_t *svn_test_get_editor (const svn_delta_edit_fns_t **editor,
                                  void **edit_baton,
                                  svn_stringbuf_t *editor_name,
                                  svn_stream_t *out_stream,
                                  int indentation,
                                  svn_boolean_t verbose,
                                  svn_stringbuf_t *path,
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
