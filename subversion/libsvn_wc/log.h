/*
 * log.h :  interfaces for running .svn/log files.
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


#ifndef SVN_LIBSVN_WC_LOG_H
#define SVN_LIBSVN_WC_LOG_H

#include <apr_pools.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_xml.h"
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
 */

/** Log actions. **/

/* Set some attributes on SVN_WC__LOG_ATTR_NAME's entry.  Unmentioned
   attributes are unaffected. */
#define SVN_WC__LOG_MODIFY_ENTRY        "modify-entry"

/* Delete the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_ENTRY        "delete-entry"

/* Run an external command:
 *    - command to run is SVN_WC__LOG_ATTR_NAME
 *    - arguments are SVN_WC__LOG_ATTR_ARG_[1,2,3,...]
 *    - input from SVN_WC__LOG_ATTR_INFILE, defaults to stdin
 *    - output into SVN_WC__LOG_ATTR_OUTFILE, defaults to stdout
 *    - stderr into SVN_WC__LOG_ATTR_ERRFILE, defaults to stderr
 *
 * The program will be run in the working copy directory, that is, the
 * same directory from which paths in the log file are rooted.
 */
#define SVN_WC__LOG_RUN_CMD             "run"

/* Move file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_MV                  "mv"

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_CP                  "cp"

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST, but
   expand any keywords and use any eol-style defined by properties of
   the DEST. */
#define SVN_WC__LOG_CP_AND_TRANSLATE    "cp-and-translate"

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST, but
   contract any keywords and convert to LF eol, according to
   properties of NAME. */
#define SVN_WC__LOG_CP_AND_DETRANSLATE    "cp-and-detranslate"

/* Remove file SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_RM                  "rm"

/* Append file from SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_APPEND              "append"

/* Make file SVN_WC__LOG_ATTR_NAME readonly */
#define SVN_WC__LOG_READONLY            "readonly"

/* Handle closure after a commit completes successfully:  
 *
 *   If SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME exists, then
 *      compare SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME with working file
 *         if they're the same, use working file's timestamp
 *         else use SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME's timestamp
 *      set SVN_WC__LOG_ATTR_NAME's revision to N
 */
#define SVN_WC__LOG_COMMITTED           "committed"


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
#define SVN_WC__LOG_MERGE        "merge"


/** Log attributes. **/
#define SVN_WC__LOG_ATTR_NAME           "name"
#define SVN_WC__LOG_ATTR_DEST           "dest"
#define SVN_WC__LOG_ATTR_REVISION       "revision"
#define SVN_WC__LOG_ATTR_TEXT_REJFILE   "text-rejfile"
#define SVN_WC__LOG_ATTR_PROP_REJFILE   "prop-rejfile"
/* The rest are for SVN_WC__LOG_RUN_CMD.  Extend as necessary. */
#define SVN_WC__LOG_ATTR_INFILE         "infile"
#define SVN_WC__LOG_ATTR_OUTFILE        "outfile"
#define SVN_WC__LOG_ATTR_ERRFILE        "errfile"
#define SVN_WC__LOG_ATTR_ARG_1          "arg1"
#define SVN_WC__LOG_ATTR_ARG_2          "arg2"
#define SVN_WC__LOG_ATTR_ARG_3          "arg3"
#define SVN_WC__LOG_ATTR_ARG_4          "arg4"
#define SVN_WC__LOG_ATTR_ARG_5          "arg5"
#define SVN_WC__LOG_ATTR_ARG_6          "arg6"
#define SVN_WC__LOG_ATTR_ARG_7          "arg7"
#define SVN_WC__LOG_ATTR_ARG_8          "arg8"
#define SVN_WC__LOG_ATTR_ARG_9          "arg9"


/* Starting at PATH, write out log entries indicating that a commit
 * succeeded, using REVISION as the new revision number.  run_log will
 * use these log items to complete the commit. 
 * 
 * Targets is a hash of files/dirs that actually got committed --
 * these are the only ones who we can write log items for, and whose
 * revision numbers will get set.
 */
svn_error_t *svn_wc__log_commit (const char *path,
                                 apr_hash_t *targets,
                                 svn_revnum_t revision,
                                 apr_pool_t *pool);


/* Process the instructions in the log file for ADM_ACCESS. */
svn_error_t *svn_wc__run_log (svn_wc_adm_access_t *adm_access,
                              apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_LOG_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

