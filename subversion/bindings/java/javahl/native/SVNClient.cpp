/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 */
// SVNClient.cpp: implementation of the SVNClient class.
//
//////////////////////////////////////////////////////////////////////

#include "SVNClient.h"
#include "JNIUtil.h"
#include "Notify.h"
#include "Prompter.h"
#include "Pool.h"
#include "Targets.h"
#include "Revision.h"
#include "BlameCallback.h"
#include "JNIByteArray.h"
#include "CommitMessage.h"
#include <svn_client.h>
#include <svn_sorts.h>
#include <svn_time.h>
#include <svn_config.h>
#include <svn_io.h>
#include <svn_path.h>
#include "svn_private_config.h"
#include "../include/org_tigris_subversion_javahl_StatusKind.h"
#include "../include/org_tigris_subversion_javahl_Revision.h"
#include "../include/org_tigris_subversion_javahl_NodeKind.h"
#include "JNIStringHolder.h"
#include <vector>
#include <iostream>
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
struct log_msg_baton
{
    const char *message;
    CommitMessage *messageHandler;
};

SVNClient::SVNClient()
{
    m_notify = NULL;
    m_prompter = NULL;
    m_commitMessage = NULL;
}

SVNClient::~SVNClient()
{
    delete m_notify;
    delete m_prompter;
}

jlong SVNClient::getCppAddr()
{
    return reinterpret_cast<jlong>(this);
}

SVNClient * SVNClient::getCppObject(jobject jthis)
{
    static jfieldID fid = 0;
    JNIEnv *env = JNIUtil::getEnv();
    if(fid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNClient");
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
    return reinterpret_cast<SVNClient*>(cppAddr);

}

void SVNClient::dispose(jobject jthis)
{
    delete this;
    static jfieldID fid = 0;
    JNIEnv *env = JNIUtil::getEnv();
    if(fid == 0)
    {
        jclass clazz = env->FindClass(JAVA_PACKAGE"/SVNClient");
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

void SVNClient::finalize()
{
    JNIUtil::putFinalizedClient(this);
}

const char * SVNClient::getLastPath()
{
    return m_lastPath.c_str();
}

/**
 * List directory entries of a URL
 */
jobjectArray SVNClient::list(const char *url, Revision &revision, bool recurse)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }

    if(url == NULL)
    {
        JNIUtil::throwNullPointerException("path or url");
        return NULL;
    }

    apr_hash_t *dirents;
    svn_error_t *Err = svn_client_ls (&dirents, url, 
                         const_cast<svn_opt_revision_t*>(revision.revision ()),
                         recurse, ctx, requestPool.pool());
    if (Err == NULL)
    {
        apr_array_header_t *array =
               svn_sort__hash (dirents, svn_sort_compare_items_as_paths,
                               requestPool.pool());

        // create the array of DirEntry
        JNIEnv *env = JNIUtil::getEnv();
        jclass clazz = env->FindClass(JAVA_PACKAGE"/DirEntry");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        jobjectArray ret = env->NewObjectArray(array->nelts, clazz, NULL);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }

        for (int i = 0; i < array->nelts; i++)
        {
            const svn_sort__item_t *item;
            svn_dirent_t *dirent = NULL;

            item = &APR_ARRAY_IDX (array, i, const svn_sort__item_t);
            dirent = (svn_dirent_t *) item->value;

            jobject obj = createJavaDirEntry((const char *)item->key, dirent);
            env->SetObjectArrayElement(ret, i, obj);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            env->DeleteLocalRef(obj);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
        }
        return ret;
    }
    else
    {
        JNIUtil::handleSVNError(Err);
        return NULL;
    }
}


struct status_entry
{
    const char *path;
    svn_wc_status_t *status;
};

struct status_baton
{
    std::vector<status_entry> statusVect;
    apr_pool_t *pool;
};


/**
 * callback for svn_client_status (used by status and singleStatus)
 */
void SVNClient::statusReceiver(void *baton, const char *path, 
                               svn_wc_status_t *status)
{
    if(JNIUtil::isJavaExceptionThrown())
        return;

    // we don't create here java Status object as we don't want too many local 
    // references
    status_baton *statusBaton = (status_baton*)baton;
    status_entry statusEntry;
    statusEntry.path = apr_pstrdup(statusBaton->pool,path);
    statusEntry.status = svn_wc_dup_status(status,statusBaton->pool);
    statusBaton->statusVect.push_back(statusEntry);
}


jobjectArray SVNClient::status(const char *path, bool descend, bool onServer, 
                               bool getAll, bool noIgnore)
{
    status_baton statusBaton;
    Pool requestPool;
    svn_revnum_t youngest = SVN_INVALID_REVNUM;
    svn_opt_revision_t rev;

    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }

    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }

    rev.kind = svn_opt_revision_unspecified;
    statusBaton.pool = requestPool.pool();
    const char *intPath = svn_path_internal_style (path, requestPool.pool());

    svn_error_t *Err = svn_client_status (
                             &youngest, intPath, &rev, statusReceiver, 
                             &statusBaton, descend ? TRUE : FALSE,
                             getAll ? TRUE : FALSE,
                             onServer ? TRUE : FALSE,
                             noIgnore ? TRUE : FALSE,
                             ctx,
                             requestPool.pool());
    if (Err == NULL)
    {
        JNIEnv *env = JNIUtil::getEnv();
        int size = statusBaton.statusVect.size();
        jclass clazz = env->FindClass(JAVA_PACKAGE"/Status");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        jobjectArray ret = env->NewObjectArray(size, clazz, NULL);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }

        for(int i = 0; i < size; i++)
        {
            status_entry statusEntry = statusBaton.statusVect[i];

            jobject jStatus = createJavaStatus(statusEntry.path, 
                                               statusEntry.status);
            env->SetObjectArrayElement(ret, i, jStatus);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            env->DeleteLocalRef(jStatus);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
        }
        return ret;
    }
    else
    {
        JNIUtil::handleSVNError(Err);
        return NULL;
    }
}

jobject SVNClient::singleStatus(const char *path, bool onServer)
{
    status_baton statusBaton;
    Pool requestPool;
    svn_revnum_t youngest = SVN_INVALID_REVNUM;
    svn_opt_revision_t rev;

    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }

    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }


    rev.kind = svn_opt_revision_unspecified;
    statusBaton.pool = requestPool.pool();
    const char *intPath = svn_path_internal_style (path, requestPool.pool());

    svn_error_t *Err = svn_client_status (&youngest, intPath, &rev, 
                             statusReceiver, &statusBaton,
                             FALSE,
                             TRUE,  // get_All
                             onServer ? TRUE : FALSE,     //update
                             FALSE,     //no_ignore,
                             ctx,
                             requestPool.pool());
    if(Err == NULL)
    {
        int size = statusBaton.statusVect.size();
        if (size == 0)
            return NULL;

        // when svn_client_status is used with a directory, the status of the 
        // directory itself and the status of all its direct children are 
        // returned
        // we just want the status of the directory (ie the status of the 
        // element with the shortest path)
        int j = 0;
        for (int i = 0; i < size; i++)
        {
            if (strlen(statusBaton.statusVect[i].path) < 
                   strlen(statusBaton.statusVect[j].path))
                j = i;
        }

        jobject jStatus = createJavaStatus(statusBaton.statusVect[j].path, 
                                           statusBaton.statusVect[j].status);

        return jStatus;
    }
    else
    {
         JNIUtil::handleSVNError(Err);
        return NULL;
    }
}

void SVNClient::username(const char *username)
{
    m_userName = username;
}

void SVNClient::password(const char *password)
{
    m_passWord = password;
}

void SVNClient::setPrompt(Prompter *prompter)
{
    delete m_prompter;
    m_prompter = prompter;
}

jobjectArray SVNClient::logMessages(const char *path, Revision &revisionStart,
                                    Revision &revisionEnd, bool stopOnCopy)
{
    std::vector<jobject> logs;
    Pool requestPool;

    if(path ==NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }

    m_lastPath = svn_path_internal_style (path, requestPool.pool());
    Targets target (m_lastPath.c_str () );
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }
    svn_error_t *Err = svn_client_log (target.array (requestPool),
                        revisionStart.revision (),
                        revisionEnd.revision (),
                        FALSE, // not reverse by default
                        stopOnCopy,
                        messageReceiver, &logs, ctx, requestPool.pool());
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    if(Err == NULL)
    {
        int size = logs.size();

        JNIEnv *env = JNIUtil::getEnv();
        jclass clazz = env->FindClass(JAVA_PACKAGE"/LogMessage");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        jobjectArray ret = env->NewObjectArray(size, clazz, NULL);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        for(int i = 0; i < size; i++)
        {
            jobject log = logs[i];
            env->SetObjectArrayElement(ret, i, log);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            env->DeleteLocalRef(log);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
        }
        return ret;
    }
    else
    {
         JNIUtil::handleSVNError(Err);
        return NULL;
    }
}

jlong SVNClient::checkout(const char *moduleName, const char *destPath, 
                          Revision &revision, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();

    if(moduleName == NULL)
    {
        JNIUtil::throwNullPointerException("moduleName");
        return -1;
    }
    if(destPath == NULL)
    {
        JNIUtil::throwNullPointerException("destPath");
        return -1;
    }

    m_lastPath = svn_path_internal_style (destPath, apr_pool);
    svn_revnum_t retval;

    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return -1;
    }

    svn_error_t *Err = svn_client_checkout (&retval, moduleName,
                                 m_lastPath.c_str (),
                                 revision.revision (),
                                 recurse, ctx,
                                 apr_pool);

    if(Err != NULL)
    {
        JNIUtil::handleSVNError(Err);
        return -1;
    }
    return retval;

}

void SVNClient::notification(Notify *notify)
{
    delete m_notify;
    m_notify = notify;
}

void SVNClient::remove(Targets &targets, const char *message, bool force)
{
    svn_client_commit_info_t *commit_info = NULL;
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
//    m_lastPath = path;
    svn_client_ctx_t *ctx = getContext(message);
    if(ctx == NULL)
    {
        return;
    }

    svn_error_t *Err = svn_client_delete (&commit_info, 
                                          targets.array(requestPool), force,
                                          ctx, apr_pool);
    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

void SVNClient::revert(const char *path, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();

    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }

    m_lastPath = svn_path_internal_style (path, apr_pool);
    svn_client_ctx_t *ctx = getContext(NULL);
    Targets target (m_lastPath.c_str () );
    if(ctx == NULL)
    {
        return;
    }
    svn_error_t *Err = svn_client_revert (target.array(requestPool), recurse, 
                                          ctx, apr_pool);

    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

void SVNClient::add(const char *path, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();

    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }

    m_lastPath = svn_path_internal_style (path, apr_pool);
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return;
    }
    svn_error_t *Err = svn_client_add (m_lastPath.c_str (), recurse, ctx, 
                                       apr_pool);

    if(Err != NULL)
         JNIUtil::handleSVNError(Err);
}

jlong SVNClient::update(const char *path, Revision &revision, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();

    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return -1;
    }

    m_lastPath = svn_path_internal_style (path, apr_pool);
       svn_client_ctx_t *ctx = getContext(NULL);
    svn_revnum_t retval;
    if(ctx == NULL)
    {
        return -1;
    }
    svn_error_t *Err = svn_client_update (&retval, m_lastPath.c_str (),
                                          revision.revision (),
                                          recurse,
                                          ctx,
                                          apr_pool);
    if(Err != NULL)
    {
         JNIUtil::handleSVNError(Err);
        return -1;
    }

    return retval;

}

jlong SVNClient::commit(Targets &targets, const char *message, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    svn_client_commit_info_t *commit_info = NULL;
    svn_client_ctx_t *ctx = getContext(message);
    if(ctx == NULL)
    {
        return -1;
    }
    svn_error_t *Err = svn_client_commit (&commit_info,
                                          targets.array (requestPool),
                                          !recurse, ctx, apr_pool);
    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

    if(commit_info && SVN_IS_VALID_REVNUM (commit_info->revision))
      return commit_info->revision;

    return -1;
}

void SVNClient::copy(const char *srcPath, const char *destPath, 
                     const char *message, Revision &revision)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();

    if(srcPath == NULL)
    {
        JNIUtil::throwNullPointerException("srcPath");
        return;
    }
    if(destPath == NULL)
    {
        JNIUtil::throwNullPointerException("destPath");
        return;
    }

    Path sourcePath = srcPath;
    m_lastPath = svn_path_internal_style (destPath, apr_pool);

    svn_client_commit_info_t *commit_info = NULL;
       svn_client_ctx_t *ctx = getContext(message);
    if(ctx == NULL)
    {
        return;
    }

    svn_error_t *Err = svn_client_copy (&commit_info,
                             sourcePath.c_str (),
                             revision.revision(),
                             m_lastPath.c_str (),
                             ctx,
                             apr_pool);
    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

void SVNClient::move(const char *srcPath, const char *destPath, 
                     const char *message, Revision &revision, bool force)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();

    if(srcPath == NULL)
    {
        JNIUtil::throwNullPointerException("srcPath");
        return;
    }
    if(destPath == NULL)
    {
        JNIUtil::throwNullPointerException("destPath");
        return;
    }
    svn_client_commit_info_t *commit_info = NULL;
    Path sourcePath = srcPath;
    m_lastPath = svn_path_internal_style (destPath, apr_pool);
    svn_client_ctx_t *ctx = getContext(message);
    if(ctx == NULL)
    {
        return;
    }


    svn_error_t *Err = svn_client_move (&commit_info,
                                        sourcePath.c_str (),
                                        revision.revision (),
                                        m_lastPath.c_str (),
                                        force,
                                        ctx,
                                        apr_pool);
    if(Err != NULL)
         JNIUtil::handleSVNError(Err);
}

void SVNClient::mkdir(Targets &targets, const char *message)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    svn_client_commit_info_t *commit_info = NULL;
    svn_client_ctx_t *ctx = getContext(message);
    if(ctx == NULL)
    {
        return;
    }

    svn_error_t *Err = svn_client_mkdir (&commit_info,
                                         targets.array(requestPool),
                                         ctx,
                                         apr_pool);

    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

void SVNClient::cleanup(const char *path)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    m_lastPath = svn_path_internal_style (path, apr_pool);
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return;
    }
    svn_error_t *Err = svn_client_cleanup (m_lastPath.c_str (), ctx, apr_pool);

    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

void SVNClient::resolved(const char *path, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    m_lastPath = svn_path_internal_style (path, apr_pool);
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return;
    }
    svn_error_t *Err = svn_client_resolved (m_lastPath.c_str (),
                                            recurse,
                                            ctx,
                                            apr_pool);

    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

jlong SVNClient::doExport(const char *srcPath, const char *destPath, 
                          Revision &revision,bool force)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    if(srcPath == NULL)
    {
        JNIUtil::throwNullPointerException("srcPath");
        return -1;
    }
    if(destPath == NULL)
    {
        JNIUtil::throwNullPointerException("destPath");
        return -1;
    }
    Path sourcePath = srcPath;
    m_lastPath = svn_path_internal_style (destPath, apr_pool);
    svn_revnum_t retval;
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return -1;
    }
    svn_error_t *Err = svn_client_export (&retval, sourcePath.c_str (),
                                          m_lastPath.c_str (),
                                          const_cast<svn_opt_revision_t*>(
                                                 revision.revision ()),
                                          force,
                                          ctx,
                                          apr_pool);
    if(Err != NULL)
    {
        JNIUtil::handleSVNError(Err);
        return -1;
    }

    return retval;

}

jlong SVNClient::doSwitch(const char *path, const char *url, 
                          Revision &revision, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return -1;
    }
    if(url == NULL)
    {
        JNIUtil::throwNullPointerException("url");
        return -1;
    }
    m_lastPath = svn_path_internal_style (path, apr_pool);
    svn_revnum_t retval;
       svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return -1;
    }
    svn_error_t *Err = svn_client_switch (&retval, m_lastPath.c_str (),
                                          url,
                                          revision.revision (),
                                          recurse,
                                          ctx,
                                          apr_pool);

    if(Err != NULL)
    {
         JNIUtil::handleSVNError(Err);
        return -1;
    }
    return retval;
}

void SVNClient::doImport(const char *path, const char *url, 
                         const char *message, bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(url == NULL)
    {
        JNIUtil::throwNullPointerException("url");
        return;
    }
    m_lastPath = svn_path_internal_style (path, apr_pool);
    svn_client_commit_info_t *commit_info = NULL;
    svn_client_ctx_t *ctx = getContext(message);
    if(ctx == NULL)
    {
        return;
    }

    svn_error_t *Err = svn_client_import (&commit_info,
                                          m_lastPath.c_str (),
                                          url,
                                          !recurse,
                                          ctx,
                                          apr_pool);

    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

void SVNClient::merge(const char *path1, Revision &revision1, 
                      const char *path2, Revision &revision2, 
                      const char *localPath, bool force, bool recurse)
{
    Pool requestPool;
    if(path1 == NULL)
    {
        JNIUtil::throwNullPointerException("path1");
        return;
    }
    if(path2 == NULL)
    {
        JNIUtil::throwNullPointerException("path2");
        return;
    }
    if(localPath == NULL)
    {
        JNIUtil::throwNullPointerException("localPath");
        return;
    }
    apr_pool_t * apr_pool = requestPool.pool ();
    m_lastPath = svn_path_internal_style (localPath, apr_pool);
    Path srcPath1 = path1;
    Path srcPath2 = path2;
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return;
    }

    svn_error_t *Err = svn_client_merge (srcPath1.c_str (),
                                         revision1.revision (),
                                         srcPath2.c_str (),
                                         revision2.revision (),
                                         localPath,
                                         recurse,
                                         FALSE,   // ignore_ancestry
                                         force,
                                         FALSE,
                                         ctx,
                                         apr_pool);

    if(Err != NULL)
         JNIUtil::handleSVNError(Err);

}

/**
 * Get a property
 */
jobject SVNClient::propertyGet(jobject jthis, const char *path, const char *name)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }
    if(name == NULL)
    {
        JNIUtil::throwNullPointerException("name");
        return NULL;
    }
    apr_pool_t * apr_pool = requestPool.pool ();
    m_lastPath = svn_path_internal_style (path, apr_pool);

    Revision rev(Revision::START);
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }

    apr_hash_t *props;
    svn_error_t *Err = svn_client_propget(&props,
                                          name,
                                          m_lastPath.c_str(),
                                          rev.revision(),
                                          FALSE,
                                          ctx,
                                          apr_pool);

    if(Err != NULL)
    {
        JNIUtil::handleSVNError(Err);
        return NULL;
    }

    apr_hash_index_t *hi;
    // only one element since we disabled recurse
    hi = apr_hash_first (apr_pool, props); 
    if (hi == NULL)
        return NULL; // no property with this name

    const char *filename;
    svn_string_t *propval;
    apr_hash_this (hi, (const void **)&filename, NULL, (void**)&propval);

    return createJavaProperty(jthis, path, name, propval);
}

jobjectArray SVNClient::properties(jobject jthis, const char *path)
{
    apr_array_header_t * props;
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }
    apr_pool_t * apr_pool = requestPool.pool ();
    m_lastPath = svn_path_internal_style (path, apr_pool);

    Revision rev(Revision::START);
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }

    svn_error_t *Err = svn_client_proplist (&props,
                                            m_lastPath.c_str (),
                                            rev.revision(),
                                            FALSE,
                                            ctx,
                                            apr_pool);
    if(Err != NULL)
    {
        JNIUtil::handleSVNError(Err);
        return NULL;
    }

    // since we disabled recurse, props->nelts should be 1
    for (int j = 0; j < props->nelts; ++j)
    {
        svn_client_proplist_item_t *item =
            ((svn_client_proplist_item_t **)props->elts)[j];

        apr_hash_index_t *hi;

        int count = apr_hash_count (item->prop_hash);

        JNIEnv *env = JNIUtil::getEnv();
        jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        jobjectArray ret = env->NewObjectArray(count, clazz, NULL);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
        env->DeleteLocalRef(clazz);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }

        int i = 0;
        for (hi = apr_hash_first (apr_pool, item->prop_hash); hi;
             hi = apr_hash_next (hi), i++)
        {
            const char *key;
            svn_string_t *val;

            apr_hash_this (hi, (const void **)&key, NULL, (void**)&val);

            jobject object = createJavaProperty(jthis, item->node_name->data, 
                                                key, val);

            env->SetObjectArrayElement(ret, i, object);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            env->DeleteLocalRef(object);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
        }
        return ret;
    }
    return NULL;
}

void SVNClient::propertySet(const char *path, const char *name, 
                            const char *value, bool recurse)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(name == NULL)
    {
        JNIUtil::throwNullPointerException("name");
        return;
    }
    if(value == NULL)
    {
        JNIUtil::throwNullPointerException("value");
        return;
    }
    svn_string_t *val = svn_string_create(value, requestPool.pool());
    propertySet(path, name, val, recurse);
}

void SVNClient::propertySet(const char *path, const char *name, JNIByteArray &value, bool recurse)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(name == NULL)
    {
        JNIUtil::throwNullPointerException("name");
        return;
    }
    if(value.isNull())
    {
        JNIUtil::throwNullPointerException("value");
        return;
    }
    svn_string_t *val = svn_string_ncreate((const char *)value.getBytes(), 
                                           value.getLength(), 
                                           requestPool.pool());
    propertySet(path, name, val, recurse);
}

void SVNClient::propertyRemove(const char *path, const char *name, 
                               bool recurse)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(name == NULL)
    {
        JNIUtil::throwNullPointerException("name");
        return;
    }
    propertySet(path, name, (svn_string_t*)NULL, recurse);
}

void SVNClient::propertyCreate(const char *path, const char *name, 
                               const char *value, bool recurse)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(name == NULL)
    {
        JNIUtil::throwNullPointerException("name");
        return;
    }
    if(value == NULL)
    {
        JNIUtil::throwNullPointerException("value");
        return;
    }
    svn_string_t *val = svn_string_create(value, requestPool.pool());
    propertySet(path, name, val, recurse);
}

void SVNClient::propertyCreate(const char *path, const char *name, 
                               JNIByteArray &value, bool recurse)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(name == NULL)
    {
        JNIUtil::throwNullPointerException("name");
        return;
    }
    if(value.isNull())
    {
        JNIUtil::throwNullPointerException("value");
        return;
    }

    svn_string_t *val = svn_string_ncreate((const char *)value.getBytes(), 
                                            value.getLength(), 
                                            requestPool.pool());
    propertySet(path, name, val, recurse);
}


void SVNClient::diff(const char *target1, Revision &revision1,
                    const char *target2, Revision &revision2,
                    const char *outfileName,bool recurse)
{
    Pool requestPool;
    svn_error_t *err = NULL;
    apr_array_header_t *options;
    if(target1 == NULL)
    {
        JNIUtil::throwNullPointerException("target1");
        return;
    }
    if(target2 == NULL)
    {
        JNIUtil::throwNullPointerException("target2");
        return;
    }
    if(outfileName == NULL)
    {
        JNIUtil::throwNullPointerException("outfileName");
        return;
    }
    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
        return;

    apr_file_t *outfile = NULL;
    apr_status_t rv;
    rv = apr_file_open(&outfile, 
                       svn_path_internal_style (outfileName, 
                                                requestPool.pool()),
                       APR_CREATE|APR_WRITE|APR_TRUNCATE , APR_OS_DEFAULT,
                       requestPool.pool());
    if (rv != APR_SUCCESS)
    {
        err = svn_error_create(rv, NULL,_("Cannot open file."));
        JNIUtil::handleSVNError(err);
        return;
    }

    // we don't use any options
    options = svn_cstring_split ("", " \t\n\r", TRUE, requestPool.pool());

    svn_error_t *Err = svn_client_diff (
                            options,    // options
                            svn_path_internal_style (target1, 
                                                     requestPool.pool()),
                            revision1.revision(),
                            svn_path_internal_style (target2, 
                                                     requestPool.pool()) ,
                            revision2.revision(),
                            recurse ? TRUE : FALSE,
                            TRUE,  // ignore_ancestry
                            FALSE, // no_diff_deleted
                            outfile,
                            NULL,  
                            // errFile (not needed when using default diff)
                            ctx,
                            requestPool.pool());

    rv = apr_file_close(outfile);
    if (rv != APR_SUCCESS)
    {
        err = svn_error_create(rv, NULL,_("Cannot close file."));
         JNIUtil::handleSVNError(err);
        return;
    }

    if(Err != NULL)
    {
         JNIUtil::handleSVNError(Err);
        return;
    }
}


svn_client_ctx_t * SVNClient::getContext(const char *message)
{
    apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
    svn_auth_baton_t *ab;
    svn_client_ctx_t *ctx;
    svn_error_t *err = NULL;
    if (( err = svn_client_create_context(&ctx, pool)))
    {
        JNIUtil::handleSVNError(err);
        return NULL;
    }

    apr_array_header_t *providers
      = apr_array_make (pool, 10, sizeof (svn_auth_provider_object_t *));

    /* The main disk-caching auth providers, for both
       'username/password' creds and 'username' creds.  */
    svn_auth_provider_object_t *provider;
    svn_client_get_simple_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_client_get_username_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

    /* The server-cert, client-cert, and client-cert-password providers. */
    svn_client_get_ssl_server_trust_file_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_client_get_ssl_client_cert_file_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
    svn_client_get_ssl_client_cert_pw_file_provider (&provider, pool);
    APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

    if(m_prompter != NULL)
    {
         /* Two basic prompt providers: username/password, and just username.*/
        provider = m_prompter->getProviderSimple();

        APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderUsername();
        APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

        /* Three ssl prompt providers, for server-certs, client-certs,
           and client-cert-passphrases.  */
        provider = m_prompter->getProviderServerSSLTrust();
        APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderClientSSL();
        APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderClientSSLPassword();
        APR_ARRAY_PUSH (providers, svn_auth_provider_object_t *) = provider;
      }



    /* Build an authentication baton to give to libsvn_client. */
    svn_auth_open (&ab, providers, pool);

    /* Place any default --username or --password credentials into the
       auth_baton's run-time parameter hash.  ### Same with --no-auth-cache? */
    if (!m_userName.empty())
      svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_USERNAME,
                             m_userName.c_str());
    if (!m_passWord.empty())
      svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_PASSWORD,
                             m_passWord.c_str());


    ctx->auth_baton = ab;
    ctx->notify_func = Notify::notify;
    ctx->notify_baton = m_notify;
    ctx->log_msg_func = getCommitMessage;
    ctx->log_msg_baton = getCommitMessageBaton(message);
    ctx->cancel_func = checkCancel;
    m_cancelOperation = false;
    ctx->cancel_baton = this;
    if (( err = 
            svn_config_get_config (&(ctx->config), m_configDir.c_str(), pool)))
    {
        JNIUtil::handleSVNError(err);
        return NULL;
    }

    return ctx;
}

svn_error_t *SVNClient::getCommitMessage(const char **log_msg, 
                                         const char **tmp_file,
                                         apr_array_header_t *commit_items, 
                                         void *baton,
                                         apr_pool_t *pool)
{
    *log_msg = NULL;
    *tmp_file = NULL;
    log_msg_baton *lmb = (log_msg_baton *) baton;

    if (lmb && lmb->messageHandler)
    {
        jstring jmsg = lmb->messageHandler->getCommitMessage(commit_items);
        if(jmsg != NULL)
        {
            JNIStringHolder msg(jmsg);
            *log_msg = apr_pstrdup (pool, msg);
        }
        return SVN_NO_ERROR;
    }
    else if (lmb && lmb->message)
    {
        *log_msg = apr_pstrdup (pool, lmb->message);
        return SVN_NO_ERROR;
    }

    return SVN_NO_ERROR;
}
void *SVNClient::getCommitMessageBaton(const char *message)
{
    if(message != NULL || m_commitMessage)
    {
        log_msg_baton *baton = (log_msg_baton *)
            apr_palloc (JNIUtil::getRequestPool()->pool(), sizeof (*baton));

        baton->message = message;
        baton->messageHandler = m_commitMessage;

        return baton;
    }
    return NULL;
}

jobject SVNClient::createJavaStatus(const char *path, svn_wc_status_t *status)
{
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/Status");
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    static jmethodID mid = 0;
    if(mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;IJJJLjava/lang/String;IIIIZZ"
             "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
             "Ljava/lang/String;JZ)V");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    jstring jPath = JNIUtil::makeJString(path);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    jstring jUrl = NULL;
    jint jNodeKind = org_tigris_subversion_javahl_NodeKind_unknown;
    jlong jRevision = org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;
    jlong jLastChangedRevision = 
                    org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;
    jlong jLastChangedDate = 0;
    jstring jLastCommitAuthor = NULL;
    jint jTextType = org_tigris_subversion_javahl_StatusKind_none;
    jint jPropType = org_tigris_subversion_javahl_StatusKind_none;
    jint jRepositoryTextType = org_tigris_subversion_javahl_StatusKind_none;
    jint jRepositoryPropType = org_tigris_subversion_javahl_StatusKind_none;
    jboolean jIsLocked = JNI_FALSE;
    jboolean jIsCopied = JNI_FALSE;
    jboolean jIsSwitched = JNI_FALSE;
    jstring jConflictOld = NULL;
    jstring jConflictNew = NULL;
    jstring jConflictWorking = NULL;
    jstring jURLCopiedFrom = NULL;
    jlong jRevisionCopiedFrom = 
                    org_tigris_subversion_javahl_Revision_SVN_INVALID_REVNUM;

    if(status != NULL)
    {

        jTextType = mapStatusKind(status->text_status);
        jPropType = mapStatusKind(status->prop_status);
        jRepositoryTextType = mapStatusKind(status->repos_text_status);
        jRepositoryPropType = mapStatusKind(status->repos_prop_status);
        jIsCopied = (status->copied == 1) ? JNI_TRUE: JNI_FALSE;
        jIsLocked = (status->locked == 1) ? JNI_TRUE: JNI_FALSE;
        jIsSwitched = (status->switched == 1) ? JNI_TRUE: JNI_FALSE;

        svn_wc_entry_t * entry = status->entry;
        if (entry != NULL)
        {
            jUrl = JNIUtil::makeJString(entry->url);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            jNodeKind = entry->kind;
            jRevision = entry->revision;
            jLastChangedRevision = entry->cmt_rev;
            jLastChangedDate = entry->cmt_date;
            jLastCommitAuthor = JNIUtil::makeJString(entry->cmt_author);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }

            jConflictNew = JNIUtil::makeJString(entry->conflict_new);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            jConflictOld = JNIUtil::makeJString(entry->conflict_old);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            jConflictWorking= JNIUtil::makeJString(entry->conflict_wrk);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            jURLCopiedFrom = JNIUtil::makeJString(entry->copyfrom_url);
            if(JNIUtil::isJavaExceptionThrown())
            {
                return NULL;
            }
            jRevisionCopiedFrom = entry->copyfrom_rev;
        }
    }

    jobject ret = env->NewObject(clazz, mid, jPath, jUrl, jNodeKind, jRevision, 
        jLastChangedRevision, jLastChangedDate, jLastCommitAuthor,
        jTextType, jPropType, jRepositoryTextType, jRepositoryPropType, 
        jIsLocked, jIsCopied, jConflictOld, jConflictNew, jConflictWorking,
        jURLCopiedFrom, jRevisionCopiedFrom,jIsSwitched);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(jPath);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    if (jUrl != NULL)
    {
        env->DeleteLocalRef(jUrl);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    if(jLastCommitAuthor != NULL)
    {
        env->DeleteLocalRef(jLastCommitAuthor);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    if(jConflictNew != NULL)
    {
        env->DeleteLocalRef(jConflictNew);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    if(jConflictOld != NULL)
    {
        env->DeleteLocalRef(jConflictOld);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    if(jConflictWorking != NULL)
    {
        env->DeleteLocalRef(jConflictWorking);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    if(jURLCopiedFrom != NULL)
    {
        env->DeleteLocalRef(jURLCopiedFrom);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    return ret;
}

jint SVNClient::mapStatusKind(int svnKind)
{
    switch(svnKind)
    {
    case svn_wc_status_none:
    default:
        return org_tigris_subversion_javahl_StatusKind_none;
    case svn_wc_status_unversioned:
        return org_tigris_subversion_javahl_StatusKind_unversioned;
    case svn_wc_status_normal:
        return org_tigris_subversion_javahl_StatusKind_normal;
    case svn_wc_status_added:
        return org_tigris_subversion_javahl_StatusKind_added;
    case svn_wc_status_missing:
        return org_tigris_subversion_javahl_StatusKind_missing;
    case svn_wc_status_deleted:
        return org_tigris_subversion_javahl_StatusKind_deleted;
    case svn_wc_status_replaced:
        return org_tigris_subversion_javahl_StatusKind_replaced;
    case svn_wc_status_modified:
        return org_tigris_subversion_javahl_StatusKind_modified;
    case svn_wc_status_merged:
        return org_tigris_subversion_javahl_StatusKind_merged;
    case svn_wc_status_conflicted:
        return org_tigris_subversion_javahl_StatusKind_conflicted;
    case svn_wc_status_ignored:
        return org_tigris_subversion_javahl_StatusKind_ignored;
    case svn_wc_status_obstructed:
        return org_tigris_subversion_javahl_StatusKind_obstructed;
    case svn_wc_status_external:
        return org_tigris_subversion_javahl_StatusKind_external;
    case svn_wc_status_incomplete:
        return org_tigris_subversion_javahl_StatusKind_incomplete;
    }
}
svn_error_t *SVNClient::messageReceiver (void *baton, apr_hash_t *changed_paths,
                 svn_revnum_t rev, const char *author, const char *date,
                 const char *msg, apr_pool_t * pool)
{
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    svn_error_t * error = NULL;
    std::vector<jobject> *logs = (std::vector<jobject>*)baton;

    apr_time_t timeTemp;
    svn_time_from_cstring (&timeTemp, date, pool);

    static jmethodID mid = 0;
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/LogMessage");
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    if(mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>", 
                   "(Ljava/lang/String;Ljava/util/Date;JLjava/lang/String;)V");
        if(JNIUtil::isJavaExceptionThrown() || mid == 0)
        {
            return SVN_NO_ERROR;
        }
    }

    jstring jmessage = JNIUtil::makeJString(msg);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    jobject jdate = JNIUtil::createDate(timeTemp);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    jstring jauthor = JNIUtil::makeJString(author);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }

    jobject log = env->NewObject(clazz, mid, jmessage, jdate, (jlong)rev, 
                                 jauthor);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    logs->push_back(log);
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    env->DeleteLocalRef(jmessage);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    env->DeleteLocalRef(jdate);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return SVN_NO_ERROR;
    }
    env->DeleteLocalRef(jauthor);
    return SVN_NO_ERROR;
}

jobject SVNClient::createJavaProperty(jobject jthis, const char *path, 
                                      const char *name, svn_string_t *value)
{
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    static jmethodID mid = 0;
    if(mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>", 
                  "(L"JAVA_PACKAGE"/SVNClient;Ljava/lang/String;"
                   "Ljava/lang/String;Ljava/lang/String;[B)V");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    jstring jPath = JNIUtil::makeJString(path);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jstring jName = JNIUtil::makeJString(name);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jstring jValue = JNIUtil::makeJString(value->data);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jbyteArray jData = JNIUtil::makeJByteArray((const signed char *)value->data, 
                                               value->len);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jobject ret = env->NewObject(clazz, mid, jthis, jPath, jName, jValue, 
                                 jData);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(jPath);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(jName);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(jValue);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(jData);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    return ret;
}

void SVNClient::propertySet(const char *path, const char *name, 
                            svn_string_t *value, bool recurse)
{
    svn_error_t * error = svn_client_propset (name, value, 
                                svn_path_internal_style (path, 
                                            JNIUtil::getRequestPool()->pool()),
                                recurse, JNIUtil::getRequestPool()->pool());
    if(error != NULL)
        JNIUtil::handleSVNError(error);
}

jbyteArray SVNClient::fileContent(const char *path, Revision &revision)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }
    path = svn_path_internal_style (path, requestPool.pool());

    svn_stream_t *read_stream = NULL;
    size_t size = 0;

    if(revision.revision()->kind == svn_opt_revision_base)
    // we want the base of the current working copy. Bad hack to avoid going to 
    // the server
    {

        const char *ori_path = svn_path_internal_style(path, 
                                                       requestPool.pool());
        const char *base_path;
        svn_error_t *err = svn_wc_get_pristine_copy_path (ori_path,
                               &base_path,
                               requestPool.pool());
        if(err != NULL)
        {
            JNIUtil::handleSVNError(err);
            return NULL;
        }
        apr_file_t *file = NULL;
        apr_finfo_t finfo;
        apr_status_t apr_err = apr_stat(&finfo, base_path,
                                   APR_FINFO_MIN, requestPool.pool());
        if(apr_err)
        {
            JNIUtil::handleAPRError(apr_err, _("open file"));
            return NULL;
        }
        apr_err = apr_file_open(&file, base_path, APR_READ, 0, 
                                requestPool.pool());
        if(apr_err)
        {
            JNIUtil::handleAPRError(apr_err, _("open file"));
            return NULL;
        }
        read_stream = svn_stream_from_aprfile(file, requestPool.pool());
        size = finfo.size;
    }
    else if(revision.revision()->kind == svn_opt_revision_working)
    // we want the working copy. Going back to the server returns base instead 
    // (not good)
    {

        const char *ori_path = svn_path_internal_style(path, 
                                                       requestPool.pool());
        apr_file_t *file = NULL;
        apr_finfo_t finfo;
        apr_status_t apr_err = apr_stat(&finfo, ori_path,
                                   APR_FINFO_MIN, requestPool.pool());
        if(apr_err)
        {
            JNIUtil::handleAPRError(apr_err, _("open file"));
            return NULL;
        }
        apr_err = apr_file_open(&file, ori_path, APR_READ, 0, 
                                requestPool.pool());
        if(apr_err)
        {
            JNIUtil::handleAPRError(apr_err, _("open file"));
            return NULL;
        }
        read_stream = svn_stream_from_aprfile(file, requestPool.pool());
        size = finfo.size;
    }
    else
    {
        svn_client_ctx_t * ctx = getContext(NULL);
        if(ctx == NULL)
        {
            return NULL;
        }
        svn_stringbuf_t *buf = svn_stringbuf_create("", requestPool.pool());
        read_stream = svn_stream_from_stringbuf(buf, requestPool.pool());
        svn_error_t *err = svn_client_cat (read_stream,
                path, revision.revision(), ctx, requestPool.pool());
        if(err != NULL)
        {
            JNIUtil::handleSVNError(err);
            return NULL;
        }
        size = buf->len;
    }
    if(read_stream == NULL)
    {
        return NULL;
    }

    JNIEnv *env = JNIUtil::getEnv();
    jbyteArray ret = env->NewByteArray(size);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jbyte *retdata = env->GetByteArrayElements(ret, NULL);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    svn_error_t *err = svn_stream_read (read_stream, (char *)retdata,
                              &size);

    if(err != NULL)
    {
        env->ReleaseByteArrayElements(ret, retdata, 0);
        JNIUtil::handleSVNError(err);
        return NULL;
    }
    env->ReleaseByteArrayElements(ret, retdata, 0);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }


    return ret;
}

/**
 * create a DirEntry java object from svn_dirent_t structure
 */
jobject SVNClient::createJavaDirEntry(const char *path, svn_dirent_t *dirent)
{
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/DirEntry");
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    static jmethodID mid = 0;
    if(mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>", 
                               "(Ljava/lang/String;IJZJJLjava/lang/String;)V");
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    jstring jPath = JNIUtil::makeJString(path);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jint jNodeKind = dirent->kind;
    jlong jSize = dirent->size;
    jboolean jHasProps = (dirent->has_props? JNI_TRUE : JNI_FALSE);
    jlong jLastChangedRevision = dirent->created_rev;
    jlong jLastChanged = dirent->time;
    jstring jLastAuthor = JNIUtil::makeJString(dirent->last_author);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jobject ret = env->NewObject(clazz, mid, jPath, jNodeKind, jSize, 
                                 jHasProps, jLastChangedRevision, 
                                 jLastChanged, jLastAuthor);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(clazz);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(jPath);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    if(jLastAuthor != NULL)
    {
        env->DeleteLocalRef(jLastAuthor);
        if(JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    return ret;
}

jobject SVNClient::revProperty(jobject jthis, const char *path, 
                               const char *name, Revision &rev)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }
    if(name == NULL)
    {
        JNIUtil::throwNullPointerException("name");
        return NULL;
    }
    apr_pool_t * apr_pool = requestPool.pool ();
    m_lastPath = svn_path_internal_style (path, apr_pool);

    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }
    const char *URL;
    svn_string_t *propval;
    svn_revnum_t set_rev;
    svn_error_t * error = svn_client_url_from_path (&URL, path, apr_pool);

    if(error != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(error);
        return NULL;
    }

    if(URL == NULL)
    {
        JNIUtil::handleSVNError(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, 
                                                 NULL,
                            _("Either a URL or versioned item is required.")));
        return NULL;
    }

    error = svn_client_revprop_get (name, &propval,
                                    URL, rev.revision(),
                                    &set_rev, ctx, apr_pool);
    if(error != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(error);
        return NULL;
    }

    return createJavaProperty(jthis, path, name, propval);
}
void SVNClient::relocate(const char *from, const char *to, const char *path, 
                         bool recurse)
{
    Pool requestPool;
    apr_pool_t * apr_pool = requestPool.pool ();
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    if(from == NULL)
    {
        JNIUtil::throwNullPointerException("from");
        return;
    }
    if(to == NULL)
    {
        JNIUtil::throwNullPointerException("to");
        return;
    }
    m_lastPath = svn_path_internal_style (path, apr_pool);

    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return;
    }


    svn_error_t * error = svn_client_relocate (path != NULL? path:"", from, to, 
                                               recurse, ctx, apr_pool);

    if(error != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(error);
        return;
    }
}
static svn_error_t *
blame_receiver (void *baton,
                apr_int64_t line_no,
                svn_revnum_t revision,
                const char *author,
                const char *date,
                const char *line,
                apr_pool_t *pool)
{
  svn_stream_t *out = (svn_stream_t*)baton;
  const char *rev_str = SVN_IS_VALID_REVNUM (revision)
                        ? apr_psprintf (pool, _("%6" SVN_REVNUM_T_FMT), 
                                        revision)
                        : _("     -");
  return svn_stream_printf (out, pool, _("%s %10s %s\n"), rev_str,
                            author ? author : _("         -"), line);
}
jbyteArray SVNClient::blame(const char *path, Revision &revisionStart, 
                            Revision &revisionEnd)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return NULL;
    }
    apr_pool_t * apr_pool = requestPool.pool ();
    m_lastPath = svn_path_internal_style (path, apr_pool);

    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return NULL;
    }
    svn_stringbuf_t *buf = svn_stringbuf_create("", apr_pool);
    svn_stream_t *read_stream = svn_stream_from_stringbuf(buf, apr_pool);
    svn_error_t * error = svn_client_blame (path,
                                            revisionStart.revision(),
                                            revisionEnd.revision(),
                                            blame_receiver,
                                            read_stream,
                                            ctx,
                                            apr_pool);
    if(error != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(error);
        return NULL;
    }
    size_t size = buf->len;

    JNIEnv *env = JNIUtil::getEnv();
    jbyteArray ret = env->NewByteArray(size);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jbyte *retdata = env->GetByteArrayElements(ret, NULL);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    svn_error_t *err = svn_stream_read (read_stream, (char *)retdata,
                              &size);

    if(err != NULL)
    {
        env->ReleaseByteArrayElements(ret, retdata, 0);
        JNIUtil::handleSVNError(err);
        return NULL;
    }
    env->ReleaseByteArrayElements(ret, retdata, 0);
    if(JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    return ret;
}
static svn_error_t *
blame_receiver2 (void *baton,
                apr_int64_t line_no,
                svn_revnum_t revision,
                const char *author,
                const char *date,
                const char *line,
                apr_pool_t *pool)
{
    ((BlameCallback *)baton)->callback(revision, author, date, line, pool);
    return NULL;
}
void SVNClient::blame(const char *path, Revision &revisionStart,
                      Revision &revisionEnd, BlameCallback *callback)
{
    Pool requestPool;
    if(path == NULL)
    {
        JNIUtil::throwNullPointerException("path");
        return;
    }
    apr_pool_t * apr_pool = requestPool.pool ();
    m_lastPath = svn_path_internal_style (path, apr_pool);

    svn_client_ctx_t *ctx = getContext(NULL);
    if(ctx == NULL)
    {
        return;
    }
    svn_error_t * error = svn_client_blame (path,
                                            revisionStart.revision(),
                                            revisionEnd.revision(),
                                            blame_receiver2,
                                            callback,
                                            ctx,
                                            apr_pool);
    if(error != SVN_NO_ERROR)
    {
        JNIUtil::handleSVNError(error);
        return;
    }
}

void SVNClient::setConfigDirectory(const char *configDir)
{
    m_configDir = configDir;
}

const char * SVNClient::getConfigDirectory()
{
    return m_configDir.c_str();
}

void SVNClient::commitMessageHandler(CommitMessage *commitMessage)
{
    delete m_commitMessage;
    m_commitMessage = commitMessage;
}

void SVNClient::cancelOperation()
{
    m_cancelOperation = true;
}

svn_error_t * SVNClient::checkCancel(void *cancelBaton)
{
    SVNClient *that = (SVNClient*)cancelBaton;
    if(that->m_cancelOperation)
        return svn_error_create (SVN_ERR_CANCELLED, NULL,
            _("Operation canceled"));
    else
        return SVN_NO_ERROR;
}
