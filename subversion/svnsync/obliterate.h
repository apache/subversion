/*
 * obliterate.h :  The obliteration editor for svnsync.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef OBLITERATE_H
#define OBLITERATE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include "svn_types.h"
#include "svn_delta.h"


/* Obliteration set. An obliteration set is a set of patterns. In the current
 * implementation, a pattern can be a string in "PATH@REV" format, where PATH
 * is repository-relative and does not start with "/", and REV is numeric, or
 * a string that is any prefix of such a string.
 */
typedef apr_array_header_t svnsync_obliteration_set_t;

/* Add the obliteration pattern NODE_REV to *OBLITERATION_SET. If
 * *OBLITERATION_SET is NULL, first create a new obliteration set and set
 * *OBLITERATION_SET to point to it. Allocate *OBLITERATION_SET and its
 * contents in POOL.
 *
 * ### An array probably isn't the best data type for this. */
void
svnsync_add_obliteration_spec(svnsync_obliteration_set_t **obliteration_set,
                              const char *node_rev,
                              apr_pool_t *pool);


/*** Obliteration Editor ***/

/*** Editor factory function ***/

/* ### Set *EDITOR and *EDIT_BATON to an editor/baton pair that wraps
 * WRAPPED_EDITOR/WRAPPED_EDIT_BATON.  BASE_REVISION is the revision on which
 * the driver of this returned editor will be basing the commit.
 * OBLITERATION_SET is an array of node-revs to omit, as (char *) PATH@REV.
 *
 * The resulting editor will filter out text changes and property changes
 * to nodes that match the patterns in OBLITERATION_SET.
 */
svn_error_t *
svnsync_get_obliterate_editor(const svn_delta_editor_t *wrapped_editor,
                              void *wrapped_edit_baton,
                              svn_revnum_t base_revision,
                              svnsync_obliteration_set_t *obliteration_set,
                              svn_boolean_t quiet,
                              const svn_delta_editor_t **editor,
                              void **edit_baton,
                              apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* OBLITERATE_H */
