/*
 * svn_test.h:  public interfaces for test programs
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

#ifndef SVN_TEST_H
#define SVN_TEST_H

#include <apr_pools.h>
#include "svn_delta.h"
#include "svn_path.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Prototype for test driver functions. */
typedef svn_error_t* (*svn_test_driver_t) (const char **msg, 
                                           svn_boolean_t msg_only,
                                           apr_pool_t *pool);

/* All Subversion test programs include an array of test descriptors
   (all of our sub-tests) that begins and ends with a SVN_TEST_NULL entry. */
struct svn_test_descriptor_t
{
  svn_test_driver_t func;       /* A pointer to the test driver function. */
  int xfail;                    /* Is the test marked XFAIL? */
};
extern struct svn_test_descriptor_t test_funcs[];

/* A null initializer for the test descriptor. */
#define SVN_TEST_NULL  {NULL, 0}

/* Initializers for PASS and XFAIL tests */
#define SVN_TEST_PASS(func)  {func, 0}
#define SVN_TEST_XFAIL(func) {func, 1}


/* Return a pseudo-random number based on SEED, and modify SEED.  This
   is a "good" pseudo-random number generator, intended to replace all
   those "bad" rand() implementations out there. */
apr_uint32_t svn_test_rand (apr_uint32_t *seed);


/* Set *EDITOR and *EDIT_BATON to an editor that prints its arguments
 * to OUT_STREAM.  The edit starts at PATH, that is, PATH will be
 * prepended to the appropriate paths in the output.  Allocate the
 * editor in POOL.
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
 * [COMMIT-TEST] open_root (wc)
 * base_revision: 1
 * 
 *    [COMMIT-TEST] open_directory (wc/A)
 *    parent: wc
 *    base_revision: 1
 * 
 *       [COMMIT-TEST] delete_entry (wc/A/B)
 * 
 *       [COMMIT-TEST] open_file (wc/A/mu)
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
 *    [COMMIT-TEST] open_file (wc/iota)
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
 * [COMMIT-TEST] open_root (wc)
 *    [COMMIT-TEST] open_directory (wc/A)
 *       [COMMIT-TEST] delete_entry (wc/A/B)
 *       [COMMIT-TEST] open_file (wc/A/mu)
 *          [COMMIT-TEST] change_file_prop (wc/A/mu)
 *       [COMMIT-TEST] close_file (wc/A/mu)
 *    [COMMIT-TEST] close_directory (wc/A)
 *    [COMMIT-TEST] add_file (wc/zeta)
 *    [COMMIT-TEST] open_file (wc/iota)
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
                                  apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */
     
#endif /* SVN_TEST_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
