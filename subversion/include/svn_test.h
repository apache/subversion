/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_test.h
 * @brief public interfaces for test programs
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


/** Prototype for test driver functions. */
typedef svn_error_t* (*svn_test_driver_t) (const char **msg, 
                                           svn_boolean_t msg_only,
                                           apr_pool_t *pool);

/** Each test gets a test descriptor, holding the function and other associated 
 * data. */
struct svn_test_descriptor_t
{
  /** A pointer to the test driver function. */
  svn_test_driver_t func;

  /** Is the test marked XFAIL? */
  int xfail;
};

/** All Subversion test programs include an array of @c svn_test_descriptor_t's
 * (all of our sub-tests) that begins and ends with a @c SVN_TEST_NULL entry.
 */
extern struct svn_test_descriptor_t test_funcs[];

/** A null initializer for the test descriptor. */
#define SVN_TEST_NULL  {NULL, 0}

/** Initializer for PASS tests */
#define SVN_TEST_PASS(func)  {func, 0}

/** Initializer for XFAIL tests */
#define SVN_TEST_XFAIL(func) {func, 1}


/** Return a pseudo-random number based on @a seed, and modify @a seed.
 *
 * Return a pseudo-random number based on @a seed, and modify @a seed.  
 * This is a "good" pseudo-random number generator, intended to replace 
 * all those "bad" @c rand() implementations out there.
 */
apr_uint32_t svn_test_rand (apr_uint32_t *seed);


/** Set @a *editor and @a *edit_baton to an editor that prints its arguments 
 * to @a out_stream.
 *
 * Set @a *editor and @a *edit_baton to an editor that prints its 
 * arguments to @a out_stream.  The edit starts at @a path, that is, 
 * @a path will be prepended to the appropriate paths in the output.
 * Allocate the editor in @a pool.
 *
 * @a editor_name is a name for the editor, a string that will be
 * prepended to the editor output as shown below.  @a editor_name may
 * be the empty string, but it may not be null.
 *
 * @a verbose is a flag for specifying whether or not your want all the
 * nitty gritty details displayed.  When @a verbose is @c FALSE, each 
 * editor function will print only a one-line summary. 
 *
 * @a indentation is the number of spaces to indent by at each level; use
 * 0 for no indentation.  The indent level is always the same for a
 * given call (i.e, stack frame).
 * 
 * SOME EXAMPLES
 *
 * With an indentation of 3, editor name of "COMMIT-TEST" and with
 * verbose = @c TRUE
 *
 *<pre> [COMMIT-TEST] open_root (wc)
 * base_revision: 1</pre>
 *<pre>    [COMMIT-TEST] open_directory (wc/A)
 *    parent: wc
 *    base_revision: 1</pre>
 *<pre>       [COMMIT-TEST] delete_entry (wc/A/B)</pre>
 *<pre>       [COMMIT-TEST] open_file (wc/A/mu)
 *       parent: wc/A
 *       base_revision: 1</pre>
 *<pre>          [COMMIT-TEST] change_file_prop (wc/A/mu)
 *          name: foo
 *          value: bar</pre>
 *<pre>       [COMMIT-TEST] close_file (wc/A/mu)</pre>
 *<pre>    [COMMIT-TEST] close_directory (wc/A)</pre>
 *<pre>    [COMMIT-TEST] add_file (wc/zeta)
 *    parent: wc
 *    copyfrom_path: 
 *    copyfrom_revision: 0</pre>
 *<pre>    [COMMIT-TEST] open_file (wc/iota)
 *    parent: wc
 *    base_revision: 1</pre>
 *<pre> [COMMIT-TEST] close_directory (wc)</pre>
 *<pre>       [COMMIT-TEST] apply_textdelta (wc/iota)</pre>
 *<pre>          [COMMIT-TEST] window_handler (2 ops)
 *          (1) new text: length 11
 *          (2) source text: offset 0, length 0</pre>
 *<pre>          [COMMIT-TEST] window_handler (EOT)</pre>
 *<pre>    [COMMIT-TEST] close_file (wc/iota)</pre>
 *<pre>       [COMMIT-TEST] apply_textdelta (wc/zeta)</pre>
 *<pre>          [COMMIT-TEST] window_handler (1 ops)
 *          (1) new text: length 11</pre>
 *<pre>          [COMMIT-TEST] window_handler (EOT)</pre>
 *<pre>    [COMMIT-TEST] close_file (wc/zeta)</pre>
 *<pre> [COMMIT-TEST] close_edit</pre>
 *  
 * The same example as above, but with verbose = @c FALSE
 *
 *<pre> [COMMIT-TEST] open_root (wc)
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
 * [COMMIT-TEST] close_edit</pre>
 *
 * This is implemented in tests/libsvn_test_editor.la
 */
svn_error_t *svn_test_get_editor (const svn_delta_editor_t **editor,
                                  void **edit_baton,
                                  const char *editor_name,
                                  svn_stream_t *out_stream,
                                  int indentation,
                                  svn_boolean_t verbose,
                                  const char *path,
                                  apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */
     
#endif /* SVN_TEST_H */
