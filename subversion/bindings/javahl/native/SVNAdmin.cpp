/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2007 CollabNet.  All rights reserved.
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
#include "SVNClient.h"
#include "JNIUtil.h"
#include "svn_repos.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_utf.h"
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

SVNAdmin *SVNAdmin::getCppObject(jobject jthis)
{
    static jfieldID fid = 0;
    jlong cppAddr = SVNBase::findCppAddrForJObject(jthis, &fid,
                                                   JAVA_PACKAGE"/SVNAdmin");
    return (cppAddr == 0 ? NULL : reinterpret_cast<SVNAdmin *>(cppAddr));
}

void SVNAdmin::dispose(jobject jthis)
{
    static jfieldID fid = 0;
    SVNBase::dispose(jthis, &fid, JAVA_PACKAGE"/SVNAdmin");
}

void SVNAdmin::create(const char *path, bool disableFsyncCommits,
                      bool keepLogs, const char *configPath,
                      const char *fstype)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    if (configPath != NULL)
        configPath = svn_path_internal_style(configPath, requestPool);
    svn_repos_t *repos;
    apr_hash_t *config;
    apr_hash_t *fs_config = apr_hash_make (requestPool);;

    apr_hash_set (fs_config, SVN_FS_CONFIG_BDB_TXN_NOSYNC,
                  APR_HASH_KEY_STRING,
                  (disableFsyncCommits? "1" : "0"));

    apr_hash_set (fs_config, SVN_FS_CONFIG_BDB_LOG_AUTOREMOVE,
                  APR_HASH_KEY_STRING,
                  (keepLogs ? "0" : "1"));
    apr_hash_set (fs_config, SVN_FS_CONFIG_FS_TYPE,
                  APR_HASH_KEY_STRING,
                  fstype);

    SVN_JNI_ERR(svn_config_get_config(&config, configPath, requestPool),
                );
    SVN_JNI_ERR(svn_repos_create(&repos, path, NULL, NULL,
                                 config, fs_config, requestPool), );
}

void SVNAdmin::deltify(const char *path, Revision &revStart, Revision &revEnd)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    svn_fs_t *fs;
    svn_revnum_t start = SVN_INVALID_REVNUM, end = SVN_INVALID_REVNUM;
    svn_revnum_t youngest, revision;
    Pool revisionPool;

    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );
    fs = svn_repos_fs (repos);
    SVN_JNI_ERR(svn_fs_youngest_rev(&youngest, fs, requestPool), );

    if (revStart.kind() == svn_opt_revision_number)
    /* ### We only handle revision numbers right now, not dates. */
        start = revStart.number();
    else if (revStart.kind() == svn_opt_revision_head)
        start = youngest;
    else
        start = SVN_INVALID_REVNUM;

    if (revEnd.kind() == svn_opt_revision_number)
        end = revEnd.number();
    else if (revEnd.kind() == svn_opt_revision_head)
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
        SVN_JNI_ERR(svn_error_create
           (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
           _("First revision cannot be higher than second")), );
    }
    if ((start > youngest) || (end > youngest))
    {
        SVN_JNI_ERR(svn_error_createf
           (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
           _("Revisions must not be greater than the youngest revision (%"
           SVN_REVNUM_T_FMT ")"), youngest), );
    }

    /* Loop over the requested revision range, performing the
       predecessor deltification on paths changed in each. */
    for (revision = start; revision <= end; revision++)
    {
        revisionPool.clear();
        SVN_JNI_ERR(svn_fs_deltify_revision (fs, revision, revisionPool), );
    }

    return;
}

void SVNAdmin::dump(const char *path, Outputer &dataOut, Outputer &messageOut,
                    Revision &revsionStart, Revision &revisionEnd,
                    bool incremental)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    svn_fs_t *fs;
    svn_revnum_t lower = SVN_INVALID_REVNUM, upper = SVN_INVALID_REVNUM;
    svn_revnum_t youngest;

    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );
    fs = svn_repos_fs (repos);
    SVN_JNI_ERR(svn_fs_youngest_rev(&youngest, fs, requestPool), );

    /* ### We only handle revision numbers right now, not dates. */
    if (revsionStart.kind() == svn_opt_revision_number)
        lower = revsionStart.number();
    else if (revsionStart.kind() == svn_opt_revision_head)
        lower = youngest;
    else
        lower = SVN_INVALID_REVNUM;

    if (revisionEnd.kind() == svn_opt_revision_number)
        upper = revisionEnd.number();
    else if (revisionEnd.kind() == svn_opt_revision_head)
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
        SVN_JNI_ERR(svn_error_create
          (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("First revision cannot be higher than second")), );
    }
    if ((lower > youngest) || (upper > youngest))
    {
        SVN_JNI_ERR(svn_error_createf
          (SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
          _("Revisions must not be greater than the youngest revision (%"
            SVN_REVNUM_T_FMT ")"), youngest), );
    }

    SVN_JNI_ERR(svn_repos_dump_fs(repos, dataOut.getStream(requestPool),
                                  messageOut.getStream(requestPool),
                                  lower, upper, incremental,
                                  NULL, NULL, requestPool), );
}

void SVNAdmin::hotcopy(const char *path, const char *targetPath,
                       bool cleanLogs)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(targetPath, "targetPath", );
    path = svn_path_internal_style(path, requestPool);
    targetPath = svn_path_internal_style(targetPath, requestPool);
    SVN_JNI_ERR(svn_repos_hotcopy(path, targetPath, cleanLogs,
                                  requestPool), );
}

static void
list_dblogs (const char *path, MessageReceiver &receiver, bool only_unused)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    apr_array_header_t *logfiles;
    int i;
    SVN_JNI_ERR(svn_repos_db_logfiles(&logfiles, path, only_unused,
                                      requestPool), );

    /* Loop, printing log files.  We append the log paths to the
       repository path, making sure to return everything to the native
       style and encoding before printing. */
    for (i = 0; i < logfiles->nelts; i++)
    {
        const char *log_utf8;
        log_utf8 = svn_path_join (path,
                                  APR_ARRAY_IDX (logfiles, i, const char *),
                                  requestPool);
        log_utf8 = svn_path_local_style (log_utf8, requestPool);
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
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    enum svn_repos_load_uuid uuid_action = svn_repos_load_uuid_default;
    if (ignoreUUID)
        uuid_action = svn_repos_load_uuid_ignore;
    else if (forceUUID)
        uuid_action = svn_repos_load_uuid_force;
    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );

    SVN_JNI_ERR(svn_repos_load_fs(repos, dataIn.getStream(requestPool),
                                  messageOut.getStream(requestPool),
                                  uuid_action, relativePath,
                                  NULL, NULL, requestPool), );
}

void SVNAdmin::lstxns(const char *path, MessageReceiver &messageReceiver)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    svn_fs_t *fs;
    apr_array_header_t *txns;
    int i;

    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );
    fs = svn_repos_fs (repos);
    SVN_JNI_ERR(svn_fs_list_transactions(&txns, fs, requestPool), );

    /* Loop, printing revisions. */
    for (i = 0; i < txns->nelts; i++)
    {
        messageReceiver.receiveMessage(APR_ARRAY_IDX (txns, i, const char *));
    }


}

jlong SVNAdmin::recover(const char *path)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", -1);
    path = svn_path_internal_style(path, requestPool);
    svn_revnum_t youngest_rev;
    svn_repos_t *repos;

    SVN_JNI_ERR(svn_repos_recover2(path, FALSE, NULL, NULL,
                                   requestPool),
                -1);

    /* Since db transactions may have been replayed, it's nice to tell
       people what the latest revision is.  It also proves that the
       recovery actually worked. */
    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), -1);
    SVN_JNI_ERR(svn_fs_youngest_rev(&youngest_rev, svn_repos_fs (repos),
                                    requestPool),
                -1);
    return youngest_rev;
}

void SVNAdmin::rmtxns(const char *path, Targets &transactions)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    svn_fs_t *fs;
    svn_fs_txn_t *txn;
    const apr_array_header_t *args;
    int i;
    Pool transactionPool;

    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );
    fs = svn_repos_fs (repos);

    args = transactions.array(requestPool);
    /* All the rest of the arguments are transaction names. */
    for (i = 0; i < args->nelts; i++)
    {
        const char *txn_name = APR_ARRAY_IDX (args, i, const char *);
        svn_error_t *err;

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
        SVN_JNI_ERR(err, );
        transactionPool.clear();
    }

}

void SVNAdmin::setRevProp(const char *path, Revision &revision,
                          const char *propName, const char *propValue,
                          bool usePreRevPropChangeHook,
                          bool usePostRevPropChangeHook)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(propName, "propName", );
    SVN_JNI_NULL_PTR_EX(propValue, "propValue", );
    if (revision.kind() != svn_opt_revision_number)
    {
        SVN_JNI_ERR(svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                              _("Missing revision")), );
    }

    /* Open the filesystem  */
    svn_repos_t *repos;
    path = svn_path_internal_style(path, requestPool);
    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );

    /* If we are bypassing the hooks system, we just hit the filesystem
       directly. */
    svn_error_t *err;
    svn_string_t *propValStr = svn_string_create(propValue, requestPool);
    if (usePreRevPropChangeHook || usePostRevPropChangeHook)
    {
        err = svn_repos_fs_change_rev_prop3
            (repos, revision.number(), NULL,
             propName, propValStr, usePreRevPropChangeHook,
             usePostRevPropChangeHook, NULL, NULL, requestPool);
    }
    else
    {
        svn_fs_t *fs = svn_repos_fs (repos);
        err = svn_fs_change_rev_prop
            (fs, revision.number(), propName, propValStr, requestPool);
    }
    SVN_JNI_ERR(err, );
}

void SVNAdmin::verify(const char *path, Outputer &messageOut,
                      Revision &revisionStart, Revision &revisionEnd)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    svn_revnum_t youngest;

    /* This whole process is basically just a dump of the repository
       with no interest in the output. */
    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );
    SVN_JNI_ERR(svn_fs_youngest_rev(&youngest, svn_repos_fs (repos),
                                    requestPool), );
    SVN_JNI_ERR(svn_repos_dump_fs(repos, NULL, messageOut.getStream(requestPool),
                                  0, youngest, FALSE, NULL, NULL,
                                  requestPool), );
}

jobjectArray SVNAdmin::lslocks(const char *path)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    svn_fs_t *fs;
    apr_hash_t *locks;
    apr_hash_index_t *hi;

    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), NULL);
    fs = svn_repos_fs (repos);
    /* Fetch all locks on or below the root directory. */
    SVN_JNI_ERR(svn_repos_fs_get_locks(&locks, repos, "/", NULL, NULL,
                                       requestPool),
                NULL);

    int count = apr_hash_count (locks);

    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/Lock");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobjectArray ret = env->NewObjectArray(count, clazz, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    int i = 0;
    for (hi = apr_hash_first (requestPool, locks); hi;
            hi = apr_hash_next (hi),i++)
    {
        void *val;
        apr_hash_this (hi, NULL, NULL, &val);
        svn_lock_t *lock = (svn_lock_t *)val;
        jobject jLock = SVNClient::createJavaLock(lock);
        env->SetObjectArrayElement(ret, i, jLock);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;

        env->DeleteLocalRef(jLock);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return ret;
}

void SVNAdmin::rmlocks(const char *path, Targets &locks)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    path = svn_path_internal_style(path, requestPool);
    svn_repos_t *repos;
    svn_fs_t *fs;
    svn_fs_access_t *access;

    SVN_JNI_ERR(svn_repos_open(&repos, path, requestPool), );
    fs = svn_repos_fs (repos);
    const char *username;

    /* svn_fs_unlock() demands that some username be associated with the
       filesystem, so just use the UID of the person running 'svnadmin'.*/
    {
        apr_uid_t uid;
        apr_gid_t gid;
        char *un;
        if (apr_uid_current (&uid, &gid, requestPool) == APR_SUCCESS &&
            apr_uid_name_get (&un, uid, requestPool) == APR_SUCCESS)
        {
            svn_error_t *err = svn_utf_cstring_to_utf8(&username, un, requestPool);
            svn_error_clear (err);
            if (err)
                username = "administrator";
        }
    }

    /* Create an access context describing the current user. */
    SVN_JNI_ERR(svn_fs_create_access(&access, username, requestPool), );

    /* Attach the access context to the filesystem. */
    SVN_JNI_ERR(svn_fs_set_access(fs, access), );

    Pool subpool;
    const apr_array_header_t *args = locks.array(requestPool);
    for (int i = 0; i < args->nelts; i++)
    {
        const char *lock_path = APR_ARRAY_IDX (args, i, const char *);
        svn_lock_t *lock;

        /* Fetch the path's svn_lock_t. */
        svn_error_t *err = svn_fs_get_lock(&lock, fs, lock_path, subpool);
        if (err)
            goto move_on;
        if (! lock)
            continue;

        /* Now forcibly destroy the lock. */
        err = svn_fs_unlock (fs, lock_path,
                             lock->token, 1 /* force */, subpool);
        if (err)
            goto move_on;

    move_on:
        svn_error_clear (err);
        subpool.clear();
    }

    return;
}
