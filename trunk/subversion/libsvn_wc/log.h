/*
 * log.h :  interfaces for running .svn/log files.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#ifndef SVN_LIBSVN_WC_LOG_H
#define SVN_LIBSVN_WC_LOG_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Note: every entry in the logfile is either idempotent or atomic.
 * This allows us to remove the entire logfile when every entry in it
 * has been completed -- if you crash in the middle of running a
 * logfile, and then later are running over it again as part of the
 * recovery, a given entry is "safe" in the sense that you can either
 * tell it has already been done (in which case, ignore it) or you can
 * do it again without ill effect.
 *
 * All log commands are self-closing tags with attributes.
 */

/** Log actions. **/

/* Set some attributes on SVN_WC__LOG_ATTR_NAME's entry.  Unmentioned
   attributes are unaffected. */
#define SVN_WC__LOG_MODIFY_ENTRY \
        "\x6d\x6f\x64\x69\x66\x79\x2d\x65\x6e\x74\x72\x79"
        /* "modify-entry" */

/* Delete lock related fields from the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_LOCK \
        "\x64\x65\x6c\x65\x74\x65\x2d\x6c\x6f\x63\x6b"
        /* "delete-lock" */

/* Delete the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_ENTRY \
        "\x64\x65\x6c\x65\x74\x65\x2d\x65\x6e\x74\x72\x79"
        /* "delete-entry" */

/* Move file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_MV \
        "\x6d\x76"
        /* "mv" */

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_CP \
        "\x63\x70"
        /* "cp" */

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST, but
   expand any keywords and use any eol-style defined by properties of
   the DEST. */
#define SVN_WC__LOG_CP_AND_TRANSLATE \
        "\x63\x70\x2d\x61\x6e\x64\x2d\x74\x72\x61\x6e\x73\x6c\x61\x74\x65"
        /* "cp-and-translate" */

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST, but
   contract any keywords and convert to LF eol, according to
   properties of NAME. */
#define SVN_WC__LOG_CP_AND_DETRANSLATE \
        "\x63\x70\x2d\x61\x6e\x64\x2d\x64\x65\x74\x72\x61\x6e\x73\x6c\x61" \
        "\x74\x65"
        /* "cp-and-detranslate" */

/* Remove file SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_RM \
        "\x72\x6d"
        /* "rm" */

/* Append file from SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_APPEND \
        "\x61\x70\x70\x65\x6e\x64"
        /* "append" */

/* Make file SVN_WC__LOG_ATTR_NAME readonly */
#define SVN_WC__LOG_READONLY \
        "\x72\x65\x61\x64\x6f\x6e\x6c\x79"
        /* "readonly" */

/* Make file SVN_WC__LOG_ATTR_NAME readonly if needs-lock property is set
   and there is no lock token for the file in the working copy. */
#define SVN_WC__LOG_MAYBE_READONLY \
        "\x6d\x61\x79\x62\x65\x2d\x72\x65\x61\x64\x6f\x6e\x6c\x79"
        /* "maybe-readonly" */

/* Set SVN_WC__LOG_ATTR_NAME to have timestamp SVN_WC__LOG_ATTR_TIMESTAMP. */
#define SVN_WC__LOG_SET_TIMESTAMP \
        "\x73\x65\x74\x2d\x74\x69\x6d\x65\x73\x74\x61\x6d\x70"
        /* "set-timestamp" */


/* Handle closure after a commit completes successfully:  
 *
 *   If SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME exists, then
 *      compare SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME with working file
 *         if they're the same, use working file's timestamp
 *         else use SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME's timestamp
 *      set SVN_WC__LOG_ATTR_NAME's revision to N
 */
#define SVN_WC__LOG_COMMITTED \
        "\x63\x6f\x6d\x6d\x69\x74\x74\x65\x64"
        /* "committed" */

/* On target SVN_WC__LOG_ATTR_NAME, set wc property
   SVN_WC__LOG_ATTR_PROPNAME to value SVN_WC__LOG_ATTR_PROPVAL.  If
   SVN_WC__LOG_ATTR_PROPVAL is absent, then remove the property. */
#define SVN_WC__LOG_MODIFY_WCPROP \
        "\x6d\x6f\x64\x69\x66\x79\x2d\x77\x63\x70\x72\x6f\x70"
        /* "modify-wcprop" */


/* A log command which runs svn_wc_merge().
   See its documentation for details.

   Here is a map of entry-attributes to svn_wc_merge arguments:

         SVN_WC__LOG_NAME         : MERGE_TARGET
         SVN_WC__LOG_ATTR_ARG_1   : LEFT
         SVN_WC__LOG_ATTR_ARG_2   : RIGHT
         SVN_WC__LOG_ATTR_ARG_3   : LEFT_LABEL
         SVN_WC__LOG_ATTR_ARG_4   : RIGHT_LABEL
         SVN_WC__LOG_ATTR_ARG_5   : TARGET_LABEL

   Of course, the three paths should be *relative* to the directory in
   which the log is running, as with all other log commands.  (Usually
   they're just basenames within loggy->path.)
 */
#define SVN_WC__LOG_MERGE \
        "\x6d\x65\x72\x67\x65"
        /* "merge" */


/** Log attributes.  See the documentation above for log actions for
    how these are used. **/
#define SVN_WC__LOG_ATTR_NAME \
        "\x6e\x61\x6d\x65"
        /* "name" */

#define SVN_WC__LOG_ATTR_DEST \
        "\x64\x65\x73\x74"
        /* "dest" */

#define SVN_WC__LOG_ATTR_PROPNAME \
        "\x70\x72\x6f\x70\x6e\x61\x6d\x65"
        /* "propname" */

#define SVN_WC__LOG_ATTR_PROPVAL \
        "\x70\x72\x6f\x70\x76\x61\x6c"
        /* "propval" */

#define SVN_WC__LOG_ATTR_REVISION \
        "\x72\x65\x76\x69\x73\x69\x6f\x6e"
        /* "revision" */

#define SVN_WC__LOG_ATTR_TEXT_REJFILE \
        "\x74\x65\x78\x74\x2d\x72\x65\x6a\x66\x69\x6c\x65"
        /* "text-rejfile" */

#define SVN_WC__LOG_ATTR_PROP_REJFILE \
        "\x70\x72\x6f\x70\x2d\x72\x65\x6a\x66\x69\x6c\x65"
        /* "prop-rejfile" */

#define SVN_WC__LOG_ATTR_TIMESTAMP \
        "\x74\x69\x6d\x65\x73\x74\x61\x6d\x70"
        /* "timestamp" */

/* The rest are for SVN_WC__LOG_MERGE.  Extend as necessary. */
#define SVN_WC__LOG_ATTR_ARG_1 \
        "\x61\x72\x67\x31"
        /* "arg1" */

#define SVN_WC__LOG_ATTR_ARG_2 \
        "\x61\x72\x67\x32"
        /* "arg2" */

#define SVN_WC__LOG_ATTR_ARG_3 \
        "\x61\x72\x67\x33"
        /* "arg3" */

#define SVN_WC__LOG_ATTR_ARG_4 \
        "\x61\x72\x67\x34"
        /* "arg4" */

#define SVN_WC__LOG_ATTR_ARG_5 \
        "\x61\x72\x67\x35"
        /* "arg5" */

/* Return the path to use for logfile number LOG_NUMBER.  The returned
   string will be allocated from POOL.

   For log number 0, this will just be SVN_WC__ADM_LOG to maintain
   compatibility with 1.0.x.  Higher numbers have the digits of the
   number appended to SVN_WC__ADM_LOG so that they look like "log.1",
   "log.2", etc. */
const char *svn_wc__logfile_path (int log_number,
                                  apr_pool_t *pool);

/* Process the instructions in the log file for ADM_ACCESS. 
   DIFF3_CMD is the external differ used by the 'SVN_WC__LOG_MERGE'
   log entry.  It is always safe to pass null for this.

   If the log fails on its first command, return the error
   SVN_ERR_WC_BAD_ADM_LOG_START.  If it fails on some subsequent
   command, return SVN_ERR_WC_BAD_ADM_LOG. */
svn_error_t *svn_wc__run_log (svn_wc_adm_access_t *adm_access,
                              const char *diff3_cmd,
                              apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_LOG_H */
