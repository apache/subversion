/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 * @file SVNAdmin.cpp
 * @brief Implementation of the class SVNAdmin
 */

#include "SVNAdmin.h"
#include "JNIUtil.h"
#include "svn_repos.h"
#include "svn_config.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_private_config.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

SVNAdmin::SVNAdmin()
{

}

SVNAdmin::~SVNAdmin()
{

}
jlong SVNAdmin::getCppAddr()
{
    return reinterpret_cast<jlong>(this);
}
SVNAdmin * SVNAdmin::getCppObject(jobject jthis)
{
    static jfieldID fid = 0;
    JNIEnv *env = JNIUtil::getEnv();
    if(fid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNAdmin");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        fid = env->GetFieldID(clazz, "cppAddr", "J");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }

    jlong cppAddr = env->GetLongField(jthis, fid);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    return reinterpret_cast<SVNAdmin*>(cppAddr);

}

void SVNAdmin::dispose(jobject jthis)
{
    delete this;
    static jfieldID fid = 0;
    JNIEnv *env = JNIUtil::getEnv();
    if(fid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNAdmin");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
        fid = env->GetFieldID(clazz, "cppAddr", "J");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return;
        }
    }

    env->SetLongField(jthis, fid, 0);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return;
    }
}

void SVNAdmin::finalize()
{
    JNIUtil::putFinalizedClient(this);
}

void SVNAdmin::create(const char *path, bool disableFsyncCommits,
                      bool keepLogs, const char *configPath,
                      const char *fstype)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    if(configPath != NULL)
        configPath = svn_path_internal_style(configPath, requestPool.pool());
    svn_repos_t *repos;
    apr_hash_t *config;
    apr_hash_t *fs_config = apr_hash_make (requestPool.pool());;

    apr_hash_set (fs_config, SVN_FS_CONFIG_BDB_TXN_NOSYNC,
                  APR_HASH_KEY_STRING,
                  (disableFsyncCommits? "1" : "0"));

    apr_hash_set (fs_config, SVN_FS_CONFIG_BDB_LOG_AUTOREMOVE,
                  APR_HASH_KEY_STRING,
                  (keepLogs ? "0" : "1"));
    apr_hash_set (fs_config, SVN_FS_CONFIG_FS_TYPE,
                  APR_HASH_KEY_STRING,
                  fstype);

    svn_error_t *err = 
        svn_config_get_config (&config, configPath, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
    err = svn_repos_create (&repos, path,
                            NULL, NULL,
                            config, fs_config, requestPool.pool());

    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
}

void SVNAdmin::deltify(const char *path, Revision &revStart, Revision &revEnd)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_repos_t *repos;
    svn_fs_t *fs;
    svn_revnum_t start = SVN_INVALID_REVNUM, end = SVN_INVALID_REVNUM;
    svn_revnum_t youngest, revision;
    apr_pool_t *revisionPool = svn_pool_create (requestPool.pool());

    svn_error_t *err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
    fs = svn_repos_fs (repos);
    err = svn_fs_youngest_rev (&youngest, fs, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

    if(revStart.revision()->kind == svn_opt_revision_number)
    /* ### We only handle revision numbers right now, not dates. */
        start = revStart.revision()->value.number;
    else if (revStart.revision()->kind == svn_opt_revision_head)
        start = youngest;
    else
        start = SVN_INVALID_REVNUM;

    if (revEnd.revision()->kind == svn_opt_revision_number)
        end = revEnd.revision()->value.number;
    else if (revEnd.revision()->kind == svn_opt_revision_head)
        end = youngest;
    else
        end = SVN_INVALID_REVNUM;

    /* Fill in implied revisions if necessary. */
    if (start == SVN_INVALID_REVNUM)
        start = youngest;
    if (end == SVN_INVALID_REVNUM)
        end = start;

    if (start > end)
    {
        JNIUtil::handleSVNError( svn_error_create
           (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
           _("First revision cannot be higher than second")));
        return;
    }
    if ((start > youngest) || (end > youngest))
    {
        JNIUtil::handleSVNError(svn_error_createf
           (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
           _("Revisions must not be greater than the youngest revision (%"
           SVN_REVNUM_T_FMT ")"), youngest));
        return;
    }

    /* Loop over the requested revision range, performing the
       predecessor deltification on paths changed in each. */
    for (revision = start; revision <= end; revision++)
    {
        svn_pool_clear (revisionPool);
        err = svn_fs_deltify_revision (fs, revision, revisionPool);
        if(err != SVN_NO_ERROR)
        {
            JNIUtil::handleSVNError(err);
            return;
        }
    }
    svn_pool_destroy (revisionPool);

    return;
}

void SVNAdmin::dump(const char *path, Outputer &dataOut, Outputer &messageOut, 
                    Revision &revsionStart, Revision &revisionEnd, 
                    bool incremental)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_repos_t *repos;
    svn_fs_t *fs;
    svn_revnum_t lower = SVN_INVALID_REVNUM, upper = SVN_INVALID_REVNUM;
    svn_revnum_t youngest;

    svn_error_t *err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
    fs = svn_repos_fs (repos);
    err = svn_fs_youngest_rev (&youngest, fs, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

    /* ### We only handle revision numbers right now, not dates. */
    if (revsionStart.revision()->kind == svn_opt_revision_number)
        lower = revsionStart.revision()->value.number;
    else if (revsionStart.revision()->kind == svn_opt_revision_head)
        lower = youngest;
    else
        lower = SVN_INVALID_REVNUM;

    if (revisionEnd.revision()->kind == svn_opt_revision_number)
        upper = revisionEnd.revision()->value.number;
    else if (revisionEnd.revision()->kind == svn_opt_revision_head)
        upper = youngest;
    else
        upper = SVN_INVALID_REVNUM;

    /* Fill in implied revisions if necessary. */
    if (lower == SVN_INVALID_REVNUM)
    {
        lower = 0;
        upper = youngest;
    }
    else if (upper == SVN_INVALID_REVNUM)
    {
        upper = lower;
    }

    if (lower > upper)
    {
        JNIUtil::handleSVNError(svn_error_create
          (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("First revision cannot be higher than second")));
        return;
    }
    if ((lower > youngest) || (upper > youngest))
    {
        JNIUtil::handleSVNError(svn_error_createf
          (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("Revisions must not be greater than the youngest revision (%"
            SVN_REVNUM_T_FMT ")"), youngest));
        return;
    }

    err= svn_repos_dump_fs (repos, dataOut.getStream(requestPool),
                            messageOut.getStream(requestPool),
                            lower, upper, incremental,
                            NULL, NULL, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

}

void SVNAdmin::hotcopy(const char *path, const char *targetPath, 
                       bool cleanLogs)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(targetPath == NULL)
    {
        JNIUtil::throwNullPointerException("targetPath");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    targetPath = svn_path_internal_style(targetPath, requestPool.pool());
    svn_error_t *err = svn_repos_hotcopy (path,
                                          targetPath,
                                          cleanLogs,
                                          requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

}
static void
list_dblogs (const char *path, MessageReceiver &receiver, bool only_unused)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    apr_array_header_t *logfiles;
    int i;
    svn_error_t *err = svn_repos_db_logfiles (&logfiles,
                                              path,
                                              only_unused,
                                              requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

    /* Loop, printing log files.  We append the log paths to the
       repository path, making sure to return everything to the native
       style and encoding before printing. */
    for (i = 0; i < logfiles->nelts; i++)
    {
        const char *log_utf8;
        log_utf8 = svn_path_join (path,
                                  APR_ARRAY_IDX (logfiles, i, const char *),
                                  requestPool.pool());
        log_utf8 = svn_path_local_style (log_utf8, requestPool.pool());
        receiver.receiveMessage(log_utf8);
    }
}

void SVNAdmin::listDBLogs(const char *path, MessageReceiver &messageReceiver)
{
    list_dblogs(path, messageReceiver, false);
}

void SVNAdmin::listUnusedDBLogs(const char *path, MessageReceiver &messageReceiver)
{
    list_dblogs(path, messageReceiver, true);
}

void SVNAdmin::load(const char *path, Inputer &dataIn, Outputer &messageOut, bool ignoreUUID, bool forceUUID, const char *relativePath)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_repos_t *repos;
    enum svn_repos_load_uuid uuid_action;
    if(ignoreUUID)
        uuid_action = svn_repos_load_uuid_ignore;
    else if(forceUUID)
        uuid_action = svn_repos_load_uuid_force;
    svn_error_t *err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

    err = svn_repos_load_fs (repos, dataIn.getStream(requestPool), 
                             messageOut.getStream(requestPool),
                             uuid_action, relativePath,
                             NULL, NULL, requestPool.pool());

    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

}

void SVNAdmin::lstxns(const char *path, MessageReceiver &messageReceiver)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_repos_t *repos;
    svn_fs_t *fs;
    apr_array_header_t *txns;
    int i;

    svn_error_t *err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
    fs = svn_repos_fs (repos);
    err = svn_fs_list_transactions (&txns, fs, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

    /* Loop, printing revisions. */
    for (i = 0; i < txns->nelts; i++)
    {
        messageReceiver.receiveMessage(APR_ARRAY_IDX (txns, i, const char *));
    }


}

jlong SVNAdmin::recover(const char *path)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return -1;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_revnum_t youngest_rev;
    svn_repos_t *repos;

    svn_error_t *err = svn_repos_recover2 (path, FALSE, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return -1;
    }

    /* Since db transactions may have been replayed, it's nice to tell
       people what the latest revision is.  It also proves that the
       recovery actually worked. */
    err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return -1;
    }
    err = svn_fs_youngest_rev (&youngest_rev, svn_repos_fs (repos), 
                               requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return -1;
    }
    return youngest_rev;
}

void SVNAdmin::rmtxns(const char *path, Targets &transactions)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_repos_t *repos;
    svn_fs_t *fs;
    svn_fs_txn_t *txn;
    const apr_array_header_t *args;
    int i;
    apr_pool_t *transactionPool = svn_pool_create (requestPool.pool());

    svn_error_t *err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
    fs = svn_repos_fs (repos);

    args = transactions.array(requestPool);
    /* All the rest of the arguments are transaction names. */
    for (i = 0; i < args->nelts; i++)
    {
        const char *txn_name = APR_ARRAY_IDX (args, i, const char *);

        /* Try to open the txn.  If that succeeds, try to abort it. */
        err = svn_fs_open_txn (&txn, fs, txn_name, transactionPool);
        if (! err)
            err = svn_fs_abort_txn (txn, transactionPool);

        /* If either the open or the abort of the txn fails because that
           transaction is dead, just try to purge the thing.  Else,
           there was either an error worth reporting, or not error at
           all.  */
        if (err && (err->apr_err == SVN_ERR_FS_TRANSACTION_DEAD))
        {
            svn_error_clear (err);
            err = svn_fs_purge_txn (fs, txn_name, transactionPool);
        }

        /* If we had a real from the txn open, abort, or purge, we clear
           that error and just report to the user that we had an issue
           with this particular txn. */
        if (err)
        {
            JNIUtil::handleSVNError(err);
            return;
        }
        svn_pool_clear (transactionPool);
    }

}

void SVNAdmin::setLog(const char *path, Revision &revision, 
                      const char *message, bool bypassHooks)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(message == NULL)
    {
        JNIUtil::throwNullPointerException("message");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_repos_t *repos;
    svn_string_t *log_contents = svn_string_create (message, 
                                                    requestPool.pool());

    if (revision.revision()->kind != svn_opt_revision_number)
    {
        JNIUtil::handleSVNError(
            svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Missing revision")));
        return;
    }
    else if (revision.revision()->kind != svn_opt_revision_unspecified)
    { 
        JNIUtil::handleSVNError(
            svn_error_createf (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Only one revision allowed")));
      return;
    }
    /* Open the filesystem  */
    svn_error_t *err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }

    /* If we are bypassing the hooks system, we just hit the filesystem
       directly. */
    if (bypassHooks)
    {
        svn_fs_t *fs = svn_repos_fs (repos);
        err = svn_fs_change_rev_prop
               (fs, revision.revision()->value.number,
                SVN_PROP_REVISION_LOG, log_contents, requestPool.pool());
    }
    else
    {
        err = svn_repos_fs_change_rev_prop
               (repos, revision.revision()->value.number,
                NULL, SVN_PROP_REVISION_LOG, log_contents, requestPool.pool());
    }
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
}

void SVNAdmin::verify(const char *path, Outputer &messageOut, 
                      Revision &revisionStart, Revision &revisionEnd)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    path = svn_path_internal_style(path, requestPool.pool());
    svn_repos_t *repos;
    svn_revnum_t youngest;

    /* This whole process is basically just a dump of the repository
       with no interest in the output. */
    svn_error_t *err = svn_repos_open (&repos, path, requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
    err = svn_fs_youngest_rev (&youngest, svn_repos_fs (repos), 
                               requestPool.pool());
    if(err != SVN_NO_ERROR)
    { 
        JNIUtil::handleSVNError(err);
        return;
    }
    err = svn_repos_dump_fs (repos, NULL, messageOut.getStream(requestPool),
                             0, youngest, FALSE, NULL, NULL, 
                             requestPool.pool());
    if(err != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(err);
        return;
    }
}
