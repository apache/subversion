/*
 * ra_pipe.h: shared internal declarations for ra_pipe module
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING,
 * which you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_RA_PIPE_H
#define SVN_LIBSVN_RA_PIPE_H

#include "svn_ra.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** XML Stuff for this file (needs to go somewhere public, so libsvn_server
 * can see it.) */
#define SVN_RA_PIPE__NAMESPACE          "svn-pipe"
#define SVN_RA_PIPE__REQUEST_TAG        "S:request"
#define SVN_RA_PIPE__CLOSE_SESSION_TAG  "S:close"
#define SVN_RA_PIPE__LATEST_REVNUM_TAG  "S:latest-revnum"
#define SVN_RA_PIPE__GET_LOG_TAG        "S:get-log"
#define SVN_RA_PIPE__PATH_TAG           "S:path"
#define SVN_RA_PIPE__CHECK_PATH_TAG     "S:check-path"
#define SVN_RA_PIPE__GET_FILE_TAG       "S:get-file"
#define SVN_RA_PIPE__CHECKOUT_TAG       "S:checkout"
#define SVN_RA_PIPE__COMMIT_TAG         "S:commit"
#define SVN_RA_PIPE__ENTRY_TAG          "S:entry"
#define SVN_RA_PIPE__MISSING_TAG        "S:missing"
#define SVN_RA_PIPE__REPORT_TAG         "S:report"
#define SVN_RA_PIPE__TARGET_REVISION_TAG    "S:target-rev"
#define SVN_RA_PIPE__UPDATE_TARGET_TAG  "S:update-target"
#define SVN_RA_PIPE__DST_PATH_TAG       "S:dst-path"
#define SVN_RA_PIPE__RECURSIVE_TAG      "S:recurse"
#define SVN_RA_PIPE__FETCH_TEXT_TAG     "S:fetch-text"

/* ### A bunch of these could be combined with attributes from the log stuff
 * in libsvn_wc if we made it public somewhere. (Which this will have to be
 * so that the server can use it). */
#define SVN_RA_PIPE__ATT_DATE           "date"
#define SVN_RA_PIPE__ATT_REV            "rev"
#define SVN_RA_PIPE__ATT_STARTREV       "start-revision"
#define SVN_RA_PIPE__ATT_ENDREV         "end-revision"
#define SVN_RA_PIPE__ATT_CHANGED_PATHS  "changed-paths"
#define SVN_RA_PIPE__ATT_VALUE          "value"
#define SVN_RA_PIPE__ATT_PATH           "path"
#define SVN_RA_PIPE__ATT_RECURSE        "recurse"
#define SVN_RA_PIPE__ATT_LOG_MSG        "log-msg"
#define SVN_RA_PIPE__ATT_URL            "url"




    /* ### NEED NEW DOCSTRING */
/* Set *REPORTER and *REPORT_BATON to a reporter/baton pair, allocated in
 * POOL, which will write their report to FILE, performing all allocations
 * in POOL. */
svn_error_t *
svn_ra_pipe__get_reporter (const svn_ra_reporter_t **reporter,
                           void **report_baton,
                           apr_file_t *input,
                           apr_file_t *output,
                           const char *url,
                           svn_stringbuf_t *target,
                           const char *dst_path,
                           svn_revnum_t revision,
                           svn_boolean_t recurse,
                           const svn_delta_edit_fns_t *update_editor,
                           void *update_baton,
                           svn_boolean_t fetch_text,
                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_RA_PIPE_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
