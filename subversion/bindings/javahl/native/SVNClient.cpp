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
 */
// SVNClient.cpp: implementation of the SVNClient class.
//
//////////////////////////////////////////////////////////////////////

#include "SVNClient.h"
#include "JNIUtil.h"
#include "Notify.h"
#include "Notify2.h"
#include "CopySources.h"
#include "DiffSummaryReceiver.h"
#include "ProgressListener.h"
#include "Prompter.h"
#include "Pool.h"
#include "Targets.h"
#include "Revision.h"
#include "BlameCallback.h"
#include "ProplistCallback.h"
#include "LogMessageCallback.h"
#include "InfoCallback.h"
#include "StatusCallback.h"
#include "JNIByteArray.h"
#include "CommitMessage.h"
#include "EnumMapper.h"
#include "svn_types.h"
#include "svn_client.h"
#include "svn_sorts.h"
#include "svn_time.h"
#include "svn_diff.h"
#include "svn_config.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "svn_private_config.h"
#include "../include/org_tigris_subversion_javahl_Revision.h"
#include "../include/org_tigris_subversion_javahl_NodeKind.h"
#include "../include/org_tigris_subversion_javahl_StatusKind.h"
#include "JNIStringHolder.h"
#include <vector>
#include <iostream>
#include <sstream>
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
    m_notify2 = NULL;
    m_progressListener = NULL;
    m_prompter = NULL;
    m_commitMessage = NULL;
}

SVNClient::~SVNClient()
{
    delete m_notify;
    delete m_notify2;
    delete m_progressListener;
    delete m_prompter;
}

SVNClient *SVNClient::getCppObject(jobject jthis)
{
    static jfieldID fid = 0;
    jlong cppAddr = SVNBase::findCppAddrForJObject(jthis, &fid,
                                                   JAVA_PACKAGE"/SVNClient");
    return (cppAddr == 0 ? NULL : reinterpret_cast<SVNClient *>(cppAddr));
}

void SVNClient::dispose(jobject jthis)
{
    static jfieldID fid = 0;
    SVNBase::dispose(jthis, &fid, JAVA_PACKAGE"/SVNClient");
}

jstring SVNClient::getAdminDirectoryName()
{
    Pool requestPool;
    jstring name = JNIUtil::makeJString(svn_wc_get_adm_dir(requestPool));
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return name;
}

jboolean SVNClient::isAdminDirectory(const char *name)
{
    Pool requestPool;
    return svn_wc_is_adm_dir(name, requestPool) ? JNI_TRUE : JNI_FALSE;
}

const char *SVNClient::getLastPath()
{
    return m_lastPath.c_str();
}

/**
 * List directory entries of a URL
 */
jobjectArray SVNClient::list(const char *url, Revision &revision,
                             Revision &pegRevision, bool recurse)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    SVN_JNI_NULL_PTR_EX(url, "path or url", NULL);

    Path urlPath(url);
    SVN_JNI_ERR(urlPath.error_occured(), NULL);

    apr_hash_t *dirents;
    SVN_JNI_ERR(svn_client_ls3(&dirents, NULL, urlPath.c_str(),
                               pegRevision.revision(),
                               revision.revision (),
                               recurse, ctx, requestPool),
                NULL);

    apr_array_header_t *array =
           svn_sort__hash(dirents, svn_sort_compare_items_as_paths,
                          requestPool);

    // create the array of DirEntry
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/DirEntry");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobjectArray ret = env->NewObjectArray(array->nelts, clazz, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    for (int i = 0; i < array->nelts; i++)
    {
        const svn_sort__item_t *item;
        svn_dirent_t *dirent = NULL;

        item = &APR_ARRAY_IDX (array, i, const svn_sort__item_t);
        dirent = (svn_dirent_t *) item->value;

        jobject obj = createJavaDirEntry((const char *)item->key, dirent);
        env->SetObjectArrayElement(ret, i, obj);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;

        env->DeleteLocalRef(obj);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return ret;
}

void
SVNClient::status(const char *path, svn_depth_t depth,
                  bool onServer, bool getAll, bool noIgnore,
                  bool ignoreExternals, StatusCallback *callback)
{
    Pool requestPool;
    svn_revnum_t youngest = SVN_INVALID_REVNUM;
    svn_opt_revision_t rev;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Path checkedPath(path);
    SVN_JNI_ERR(checkedPath.error_occured(), );

    rev.kind = svn_opt_revision_unspecified;

    SVN_JNI_ERR(svn_client_status3(&youngest, checkedPath.c_str(),
                                   &rev, StatusCallback::callback,
                                   callback,
                                   depth,
                                   getAll, onServer, noIgnore,
                                   ignoreExternals,
                                   ctx, requestPool), );
}

void SVNClient::username(const char *pi_username)
{
    m_userName = (pi_username == NULL ? "" : pi_username);
}

void SVNClient::password(const char *pi_password)
{
    m_passWord = (pi_password == NULL ? "" : pi_password);
}

void SVNClient::setPrompt(Prompter *prompter)
{
    delete m_prompter;
    m_prompter = prompter;
}

void SVNClient::logMessages(const char *path, Revision &pegRevision,
                            Revision &revisionStart,
                            Revision &revisionEnd, bool stopOnCopy,
                            bool discoverPaths, long limit,
                            LogMessageCallback *callback)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Targets target (path);
    const apr_array_header_t *targets = target.array(requestPool);
    SVN_JNI_ERR(target.error_occured(), );
    SVN_JNI_ERR(svn_client_log3(targets,
                                pegRevision.revision(),
                                revisionStart.revision (),
                                revisionEnd.revision (),
                                limit,
                                discoverPaths,
                                stopOnCopy,
                                LogMessageCallback::callback, callback, ctx,
                                requestPool), );
}

jlong SVNClient::checkout(const char *moduleName, const char *destPath,
                          Revision &revision, Revision &pegRevision,
                          svn_depth_t depth, bool ignoreExternals,
                          bool allowUnverObstructions)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(moduleName, "moduleName", -1);
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", -1);

    Path url(moduleName);
    Path path(destPath);
    SVN_JNI_ERR(url.error_occured(), -1);
    SVN_JNI_ERR(path.error_occured(), -1);
    svn_revnum_t retval;

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_checkout3(&retval, url.c_str(),
                                     path.c_str (),
                                     pegRevision.revision (),
                                     revision.revision (),
                                     depth,
                                     ignoreExternals,
                                     allowUnverObstructions,
                                     ctx,
                                     requestPool),
                -1);

    return retval;

}

void SVNClient::notification(Notify *notify)
{
    delete m_notify;
    m_notify = notify;
}

void SVNClient::notification2(Notify2 *notify2)
{
    delete m_notify2;
    m_notify2 = notify2;
}

void SVNClient::setProgressListener(ProgressListener *listener)
{
    delete m_progressListener;
    m_progressListener = listener;
}

void SVNClient::remove(Targets &targets, const char *message, bool force,
                       bool keep_local)
{
    svn_commit_info_t *commit_info = NULL;
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    const apr_array_header_t *targets2 = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );

    SVN_JNI_ERR(svn_client_delete3(&commit_info, targets2, force, keep_local,
                                   ctx, requestPool), );
}

void SVNClient::revert(const char *path, bool recurse)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Targets target (path);
    const apr_array_header_t *targets = target.array(requestPool);
    SVN_JNI_ERR(target.error_occured(), );
    SVN_JNI_ERR(svn_client_revert(targets, recurse, ctx, requestPool), );
}

void SVNClient::add(const char *path, bool recurse, bool force)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_add3(intPath.c_str (), recurse, force, FALSE,
                                ctx, requestPool), );
}

jlongArray SVNClient::update(Targets &targets, Revision &revision,
                             svn_depth_t depth, bool ignoreExternals,
                             bool allowUnverObstructions)
{
    Pool requestPool;

    svn_client_ctx_t *ctx = getContext(NULL);
    apr_array_header_t *retval;
    if (ctx == NULL)
        return NULL;

    const apr_array_header_t *array = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), NULL);
    SVN_JNI_ERR(svn_client_update3(&retval, array,
                                   revision.revision(),
                                   depth,
                                   ignoreExternals,
                                   allowUnverObstructions,
                                   ctx, requestPool),
                NULL);

    JNIEnv *env = JNIUtil::getEnv();
    jlongArray ret = env->NewLongArray(retval->nelts);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jlong *retArray = env->GetLongArrayElements(ret, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    for(int i = 0; i < retval->nelts; i++)
    {
        jlong rev = APR_ARRAY_IDX (retval, i, svn_revnum_t);
        retArray[i] = rev;
    }
    env->ReleaseLongArrayElements(ret, retArray, 0);

    return ret;

}

jlong SVNClient::commit(Targets &targets, const char *message, bool recurse,
                        bool noUnlock, bool keepChangelist,
                        const char *changelistName)
{
    Pool requestPool;
    svn_commit_info_t *commit_info = NULL;
    const apr_array_header_t *targets2 = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), -1);
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return SVN_INVALID_REVNUM;

    SVN_JNI_ERR(svn_client_commit4(&commit_info, targets2, recurse,
                                   noUnlock, keepChangelist, changelistName,
                                   ctx, requestPool),
                SVN_INVALID_REVNUM);

    if (commit_info && SVN_IS_VALID_REVNUM(commit_info->revision))
      return commit_info->revision;

    return SVN_INVALID_REVNUM;
}

void SVNClient::copy(CopySources &copySources, const char *destPath,
                     const char *message, bool copyAsChild)
{
    Pool requestPool;

    apr_array_header_t *srcs = copySources.array(requestPool);
    if (srcs == NULL)
    {
        JNIUtil::throwNativeException(JAVA_PACKAGE "/ClientException",
                                      "Invalid copy sources");
        return;
    }
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", );
    Path destinationPath(destPath);
    SVN_JNI_ERR(destinationPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    svn_commit_info_t *commit_info;
    SVN_JNI_ERR(svn_client_copy4(&commit_info, srcs, destinationPath.c_str(),
                                 copyAsChild, ctx, requestPool), );
}

void SVNClient::move(Targets &srcPaths, const char *destPath,
                     const char *message, bool force, bool moveAsChild)
{
    Pool requestPool;

    const apr_array_header_t *srcs = srcPaths.array(requestPool);
    SVN_JNI_ERR(srcPaths.error_occured(), );
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", );
    Path destinationPath(destPath);
    SVN_JNI_ERR(destinationPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    svn_commit_info_t *commit_info;
    SVN_JNI_ERR(svn_client_move5(&commit_info, (apr_array_header_t *) srcs,
                                 destinationPath.c_str(), force, moveAsChild,
                                 ctx, requestPool), );
}

void SVNClient::mkdir(Targets &targets, const char *message)
{
    Pool requestPool;
    svn_client_commit_info_t *commit_info = NULL;
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    const apr_array_header_t *targets2 = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );

    SVN_JNI_ERR(svn_client_mkdir(&commit_info, targets2, ctx,
                                 requestPool), );
}

void SVNClient::cleanup(const char *path)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_cleanup (intPath.c_str (), ctx, requestPool),
                );
}

void SVNClient::resolved(const char *path, bool recurse)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_resolved(intPath.c_str(), recurse,
                                    ctx, requestPool), );
}

jlong SVNClient::doExport(const char *srcPath, const char *destPath,
                          Revision &revision, Revision &pegRevision, bool force,
                          bool ignoreExternals, svn_depth_t depth,
                          const char *nativeEOL)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(srcPath, "srcPath", -1);
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", -1);
    Path sourcePath(srcPath);
    SVN_JNI_ERR(sourcePath.error_occured(), -1);
    Path destinationPath(destPath);
    SVN_JNI_ERR(destinationPath.error_occured(), -1);
    svn_revnum_t retval;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_export4(&retval, sourcePath.c_str(),
                                   destinationPath.c_str(),
                                   pegRevision.revision(),
                                   revision.revision(), force,
                                   ignoreExternals,
                                   depth,
                                   nativeEOL, ctx,
                                   requestPool),
                -1);

    return retval;

}

jlong SVNClient::doSwitch(const char *path, const char *url,
                          Revision &revision, svn_depth_t depth,
                          bool allowUnverObstructions)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", -1);
    SVN_JNI_NULL_PTR_EX(url, "url", -1);
    Path intUrl(url);
    SVN_JNI_ERR(intUrl.error_occured(), -1);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), -1);

    svn_revnum_t retval;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_switch2(&retval, intPath.c_str (),
                                   intUrl.c_str(),
                                   revision.revision (),
                                   depth,
                                   allowUnverObstructions,
                                   ctx,
                                   requestPool),
                -1);

    return retval;
}

void SVNClient::doImport(const char *path, const char *url,
                         const char *message, bool recurse)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(url, "url", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );
    Path intUrl(url);
    SVN_JNI_ERR(intUrl.error_occured(), );

    svn_client_commit_info_t *commit_info = NULL;
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_import(&commit_info, intPath.c_str(), intUrl.c_str(),
                                  !recurse, ctx, requestPool), );
}

void SVNClient::merge(const char *path1, Revision &revision1,
                      const char *path2, Revision &revision2,
                      const char *localPath, bool force, svn_depth_t depth,
                      bool ignoreAncestry, bool dryRun)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path1, "path1", );
    SVN_JNI_NULL_PTR_EX(path2, "path2", );
    SVN_JNI_NULL_PTR_EX(localPath, "localPath", );
    Path intLocalPath(localPath);
    SVN_JNI_ERR(intLocalPath.error_occured(), );

    Path srcPath1(path1);
    SVN_JNI_ERR(srcPath1.error_occured(), );

    Path srcPath2 = path2;
    SVN_JNI_ERR(srcPath2.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_merge3(srcPath1.c_str(), revision1.revision(),
                                  srcPath2.c_str(), revision2.revision(),
                                  intLocalPath.c_str(),
                                  depth,
                                  ignoreAncestry, force, FALSE, dryRun, NULL,
                                  ctx, requestPool), );
}

void SVNClient::merge(const char *path, Revision &pegRevision,
                      Revision &revision1, Revision &revision2,
                      const char *localPath, bool force, svn_depth_t depth,
                      bool ignoreAncestry, bool dryRun)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(localPath, "localPath", );
    Path intLocalPath(localPath);
    SVN_JNI_ERR(intLocalPath.error_occured(), );

    Path srcPath(path);
    SVN_JNI_ERR(srcPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_merge_peg3(srcPath.c_str(), revision1.revision(),
                                      revision2.revision(),
                                      pegRevision.revision(),
                                      intLocalPath.c_str(),
                                      depth,
                                      ignoreAncestry, force, FALSE, dryRun,
                                      NULL, ctx, requestPool), );
}

jobject
SVNClient::getMergeInfo(const char *target, Revision &rev)
{
    Pool requestPool;
    JNIEnv *env = JNIUtil::getEnv();

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    apr_hash_t *mergeinfo;
    svn_error_t *err = svn_client_get_mergeinfo(&mergeinfo, target,
                                                rev.revision(), ctx,
                                                requestPool);
    if (err)
    {
        JNIUtil::handleSVNError(err);
        return NULL;
    }

    // Transform mergeinfo into Java MergeInfo object.
    jclass clazz = env->FindClass(JAVA_PACKAGE "/MergeInfo");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static jmethodID ctor = 0;
    if (ctor == 0)
    {
        ctor = env->GetMethodID(clazz, "<init>", "()V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    static jmethodID addRevisions = 0;
    if (addRevisions == 0)
    {
        addRevisions = env->GetMethodID(clazz, "addRevisions",
                                        "(Ljava/lang/String;"
                                        "Ljava/util/List;)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jobject jmergeinfo = env->NewObject(clazz, ctor);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    clazz = env->FindClass("java/util/ArrayList");
    static jmethodID listCtor = 0;
    if (listCtor == 0)
    {
        listCtor = env->GetMethodID(clazz, "<init>", "(I)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }
    static jmethodID addToList = 0;
    if (addToList == 0)
    {
        addToList = env->GetMethodID(clazz, "add", "(Ljava/lang/Object;)Z");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    apr_hash_index_t *hi;
    for (hi = apr_hash_first(requestPool, mergeinfo);
         hi;
         hi = apr_hash_next(hi))
    {
        const void *path;
        void *val;
        apr_hash_this(hi, &path, NULL, &val);

        jstring jpath = JNIUtil::makeJString((const char *) path);
        apr_array_header_t *ranges = (apr_array_header_t *) val;
        jobject jranges = env->NewObject(clazz, listCtor, ranges->nelts);
        for (int i = 0; i < ranges->nelts; i++)
        {
            // Convert svn_merge_range_t *'s to Java RevisionRange objects.
            jclass rangeClazz = env->FindClass(JAVA_PACKAGE "/RevisionRange");
            static jmethodID rangeCtor = 0;
            if (rangeCtor == 0)
            {
                rangeCtor = env->GetMethodID(rangeClazz, "<init>", "(JJ)V");
                if (JNIUtil::isJavaExceptionThrown())
                    return NULL;
            }
            svn_merge_range_t *range = APR_ARRAY_IDX(ranges, i,
                                                     svn_merge_range_t *);
            jobject jrevision = env->NewObject(rangeClazz, rangeCtor,
                                               (jlong) range->start,
                                               (jlong) range->end);
            if (JNIUtil::isJavaExceptionThrown())
                return NULL;

            env->CallBooleanMethod(jranges, addToList, jrevision);
            if (JNIUtil::isJavaExceptionThrown())
                return NULL;

            env->DeleteLocalRef(jrevision);
            if (JNIUtil::isJavaExceptionThrown())
                return NULL;
        }

        env->CallVoidMethod(jmergeinfo, addRevisions, jpath, jranges);

        env->DeleteLocalRef(jranges);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
        env->DeleteLocalRef(jpath);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return jmergeinfo;
}

/**
 * Get a property
 */
jobject SVNClient::propertyGet(jobject jthis, const char *path,
                               const char *name, Revision &revision,
                               Revision &pegRevision)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    SVN_JNI_NULL_PTR_EX(name, "name", NULL);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    apr_hash_t *props;
    SVN_JNI_ERR(svn_client_propget2(&props, name,
                                    intPath.c_str(), pegRevision.revision(),
                                    revision.revision(), FALSE,
                                    ctx, requestPool),
                NULL);

    apr_hash_index_t *hi;
    // only one element since we disabled recurse
    hi = apr_hash_first (requestPool, props);
    if (hi == NULL)
        return NULL; // no property with this name

    svn_string_t *propval;
    apr_hash_this(hi, NULL, NULL, (void**)&propval);

    if (propval == NULL)
        return NULL;

    return createJavaProperty(jthis, path, name, propval);
}

void SVNClient::properties(const char *path, Revision & revision,
                           Revision &pegRevision, bool recurse,
                           ProplistCallback *callback)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_proplist3(intPath.c_str(), pegRevision.revision(),
                                     revision.revision(), recurse,
                                     ProplistCallback::callback, callback,
                                     ctx, requestPool), );

    return;
}

void SVNClient::propertySet(const char *path, const char *name,
                            const char *value, bool recurse, bool force)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(name, "name", );
    SVN_JNI_NULL_PTR_EX(value, "value", );
    svn_string_t *val = svn_string_create(value, requestPool);
    propertySet(path, name, val, recurse, force, SVN_INVALID_REVNUM);
}

void SVNClient::propertyRemove(const char *path, const char *name,
                               bool recurse)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(name, "name", );
    propertySet(path, name, (svn_string_t*)NULL, recurse, false,
                SVN_INVALID_REVNUM);
}

void SVNClient::propertyCreate(const char *path, const char *name,
                               const char *value, bool recurse, bool force)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(name, "name", );
    SVN_JNI_NULL_PTR_EX(value, "value", );
    svn_string_t *val = svn_string_create(value, requestPool);
    propertySet(path, name, val, recurse, force, SVN_INVALID_REVNUM);
}

void SVNClient::diff(const char *target1, Revision &revision1,
                     const char *target2, Revision &revision2,
                     Revision *pegRevision, const char *outfileName,
                     svn_depth_t depth, bool ignoreAncestry,
                     bool noDiffDelete, bool force)
{
    svn_error_t *err;
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(target1, "target", );
    // target2 is ignored when pegRevision is provided.
    if (pegRevision == NULL)
        SVN_JNI_NULL_PTR_EX(target2, "target2", );

    SVN_JNI_NULL_PTR_EX(outfileName, "outfileName", );
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Path path1(target1);
    SVN_JNI_ERR(path1.error_occured(), );

    apr_file_t *outfile = NULL;
    apr_status_t rv =
        apr_file_open(&outfile,
                      svn_path_internal_style(outfileName, requestPool),
                      APR_CREATE|APR_WRITE|APR_TRUNCATE , APR_OS_DEFAULT,
                      requestPool);
    if (rv != APR_SUCCESS)
    {
        SVN_JNI_ERR(svn_error_createf(rv, NULL, _("Cannot open file '%s'"),
                                      outfileName), );
    }

    // We don't use any options to diff.
    apr_array_header_t *diffOptions = apr_array_make(requestPool,
                                                     0, sizeof(char *));

    if (pegRevision)
    {
        err = svn_client_diff_peg4(diffOptions,
                                   path1.c_str(),
                                   pegRevision->revision(),
                                   revision1.revision(),
                                   revision2.revision(),
                                   depth,
                                   ignoreAncestry,
                                   noDiffDelete,
                                   force,
                                   SVN_APR_LOCALE_CHARSET,
                                   outfile,
                                   NULL /* error file */,
                                   ctx,
                                   requestPool);
    }
    else
    {
        // "Regular" diff (without a peg revision).
        Path path2(target2);
        err = path2.error_occured();
        if (err)
        {
            if (outfile)
                goto cleanup;

            SVN_JNI_ERR(err, );
        }

        err = svn_client_diff4(diffOptions,
                               path1.c_str(),
                               revision1.revision(),
                               path2.c_str(),
                               revision2.revision(),
                               depth,
                               ignoreAncestry,
                               noDiffDelete,
                               force,
                               SVN_APR_LOCALE_CHARSET,
                               outfile,
                               NULL /* error file */,
                               ctx,
                               requestPool);
    }

 cleanup:
    rv = apr_file_close(outfile);
    if (rv != APR_SUCCESS)
    {
        svn_error_clear(err);

        SVN_JNI_ERR(svn_error_createf(rv, NULL, _("Cannot close file '%s'"),
                                      outfileName), );
    }

    SVN_JNI_ERR(err, );
}

void SVNClient::diff(const char *target1, Revision &revision1,
                     const char *target2, Revision &revision2,
                     const char *outfileName, svn_depth_t depth,
                     bool ignoreAncestry, bool noDiffDelete, bool force)
{
    diff(target1, revision1, target2, revision2, NULL, outfileName, depth,
         ignoreAncestry, noDiffDelete, force);
}

void SVNClient::diff(const char *target, Revision &pegRevision,
                     Revision &startRevision, Revision &endRevision,
                     const char *outfileName, svn_depth_t depth,
                     bool ignoreAncestry, bool noDiffDelete, bool force)
{
    diff(target, startRevision, NULL, endRevision, &pegRevision, outfileName,
         depth, ignoreAncestry, noDiffDelete, force);
}

void
SVNClient::diffSummarize(const char *target1, Revision &revision1,
                         const char *target2, Revision &revision2,
                         svn_depth_t depth, bool ignoreAncestry,
                         DiffSummaryReceiver &receiver)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(target1, "target1", );
    SVN_JNI_NULL_PTR_EX(target2, "target2", );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Path path1(target1);
    SVN_JNI_ERR(path1.error_occured(), );
    Path path2(target2);
    SVN_JNI_ERR(path2.error_occured(), );

    SVN_JNI_ERR(svn_client_diff_summarize2(path1.c_str(), revision1.revision(),
                                           path2.c_str(), revision2.revision(),
                                           depth,
                                           ignoreAncestry,
                                           DiffSummaryReceiver::summarize,
                                           &receiver,
                                           ctx, requestPool), );
}

void
SVNClient::diffSummarize(const char *target, Revision &pegRevision,
                         Revision &startRevision, Revision &endRevision,
                         svn_depth_t depth, bool ignoreAncestry,
                         DiffSummaryReceiver &receiver)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(target, "target", );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Path path(target);
    SVN_JNI_ERR(path.error_occured(), );

    SVN_JNI_ERR(svn_client_diff_summarize_peg2(path.c_str(),
                                               pegRevision.revision(),
                                               startRevision.revision(),
                                               endRevision.revision(),
                                               depth,
                                               ignoreAncestry,
                                               DiffSummaryReceiver::summarize,
                                               &receiver, ctx,
                                               requestPool), );
}

svn_client_ctx_t *SVNClient::getContext(const char *message)
{
    Pool *pool = JNIUtil::getRequestPool();
    svn_auth_baton_t *ab;
    svn_client_ctx_t *ctx;
    SVN_JNI_ERR(svn_client_create_context(&ctx, *pool), NULL);

    apr_array_header_t *providers
      = apr_array_make(*pool, 10, sizeof(svn_auth_provider_object_t *));

    /* The main disk-caching auth providers, for both
       'username/password' creds and 'username' creds.  */
    svn_auth_provider_object_t *provider;
#ifdef WIN32
    svn_client_get_windows_simple_provider(&provider, *pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
#endif
    svn_client_get_simple_provider (&provider, *pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_client_get_username_provider (&provider, *pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    /* The server-cert, client-cert, and client-cert-password providers. */
    svn_client_get_ssl_server_trust_file_provider(&provider, *pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_client_get_ssl_client_cert_file_provider(&provider, *pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_client_get_ssl_client_cert_pw_file_provider(&provider, *pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    if (m_prompter != NULL)
    {
         /* Two basic prompt providers: username/password, and just username.*/
        provider = m_prompter->getProviderSimple();

        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderUsername();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        /* Three ssl prompt providers, for server-certs, client-certs,
           and client-cert-passphrases.  */
        provider = m_prompter->getProviderServerSSLTrust();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderClientSSL();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderClientSSLPassword();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
      }



    /* Build an authentication baton to give to libsvn_client. */
    svn_auth_open(&ab, providers, *pool);

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
    ctx->log_msg_func3 = getCommitMessage;
    ctx->log_msg_baton3 = getCommitMessageBaton(message);
    ctx->cancel_func = checkCancel;
    m_cancelOperation = false;
    ctx->cancel_baton = this;
    const char *configDir = m_configDir.c_str();
    if (m_configDir.length() == 0)
        configDir = NULL;
    SVN_JNI_ERR(svn_config_get_config(&(ctx->config), configDir, *pool), NULL);
    ctx->notify_func2= Notify2::notify;
    ctx->notify_baton2 = m_notify2;

    ctx->progress_func = ProgressListener::progress;
    ctx->progress_baton = m_progressListener;

    return ctx;
}

svn_error_t *
SVNClient::getCommitMessage(const char **log_msg,
                            const char **tmp_file,
                            const apr_array_header_t *commit_items,
                            void *baton,
                            apr_pool_t *pool)
{
    *log_msg = NULL;
    *tmp_file = NULL;
    log_msg_baton *lmb = (log_msg_baton *) baton;

    if (lmb && lmb->messageHandler)
    {
        jstring jmsg = lmb->messageHandler->getCommitMessage(commit_items);
        if (jmsg != NULL)
        {
            JNIStringHolder msg(jmsg);
            *log_msg = apr_pstrdup(pool, msg);
        }
        return SVN_NO_ERROR;
    }
    else if (lmb && lmb->message)
    {
        *log_msg = apr_pstrdup(pool, lmb->message);
        return SVN_NO_ERROR;
    }

    return SVN_NO_ERROR;
}
void *SVNClient::getCommitMessageBaton(const char *message)
{
    if (message != NULL || m_commitMessage)
    {
        log_msg_baton *baton = (log_msg_baton *)
            apr_palloc(*JNIUtil::getRequestPool(), sizeof(*baton));

        baton->message = message;
        baton->messageHandler = m_commitMessage;

        return baton;
    }
    return NULL;
}

jobject SVNClient::createJavaProperty(jobject jthis, const char *path,
                                      const char *name, svn_string_t *value)
{
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
                  "(L"JAVA_PACKAGE"/SVNClient;Ljava/lang/String;"
                   "Ljava/lang/String;Ljava/lang/String;[B)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }
    jstring jPath = JNIUtil::makeJString(path);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jName = JNIUtil::makeJString(name);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jValue = JNIUtil::makeJString(value->data);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jbyteArray jData = JNIUtil::makeJByteArray((const signed char *)value->data,
                                               value->len);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobject ret = env->NewObject(clazz, mid, jthis, jPath, jName, jValue,
                                 jData);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jPath);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jName);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jValue);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jData);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return ret;
}

void SVNClient::propertySet(const char *path, const char *name,
                            svn_string_t *value, bool recurse, bool force,
                            svn_revnum_t baseRevisionForURL)
{
    svn_commit_info_t *commit_info = NULL;
    Pool requestPool;
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_propset3(&commit_info, name, value, intPath.c_str(),
                                    recurse, force, baseRevisionForURL,
                                    ctx, requestPool), );
}

jbyteArray SVNClient::fileContent(const char *path, Revision &revision,
                                  Revision &pegRevision)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    size_t size = 0;
    svn_stream_t *read_stream = createReadStream(requestPool,
                                                 intPath.c_str(), revision,
                                                 pegRevision, size);
    if (read_stream == NULL)
        return NULL;

    JNIEnv *env = JNIUtil::getEnv();
    // size will be set to the number of bytes available.
    jbyteArray ret = env->NewByteArray(size);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jbyte *retdata = env->GetByteArrayElements(ret, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    svn_error_t *err = svn_stream_read(read_stream, (char *)retdata, &size);
    env->ReleaseByteArrayElements(ret, retdata, 0);
    SVN_JNI_ERR(err, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return ret;
}

void SVNClient::streamFileContent(const char *path, Revision &revision,
                                  Revision &pegRevision, jobject outputStream,
                                  size_t bufSize)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    JNIEnv *env = JNIUtil::getEnv();
    jclass outputStreamClass = env->FindClass("java/io/OutputStream");
    if (outputStreamClass == NULL)
        return;

    jmethodID writeMethod = env->GetMethodID(outputStreamClass, "write",
                                             "([BII)V");
    if (writeMethod == NULL)
        return;

    // Create the buffer.
    jbyteArray buffer = env->NewByteArray(bufSize);
    if (JNIUtil::isJavaExceptionThrown())
        return;

    jbyte *bufData = env->GetByteArrayElements(buffer, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return;

    size_t contentSize = 0;
    svn_stream_t* read_stream = createReadStream(requestPool, path,
                                                 revision, pegRevision,
                                                 contentSize);
    if (read_stream == NULL)
        return;

    while (contentSize > 0)
    {
        size_t readSize = bufSize > contentSize ? contentSize : bufSize;
        svn_error_t *err;

        err = svn_stream_read(read_stream, (char *)bufData, &readSize);
        if (err != NULL)
        {
            env->ReleaseByteArrayElements(buffer, bufData, 0);
            svn_stream_close(read_stream);
            SVN_JNI_ERR(err, );
        }

        env->ReleaseByteArrayElements(buffer, bufData, JNI_COMMIT);
        env->CallVoidMethod(outputStream, writeMethod, buffer, 0, readSize);
        if (JNIUtil::isJavaExceptionThrown())
        {
            env->ReleaseByteArrayElements(buffer, bufData, 0);
            svn_stream_close(read_stream);
            return;
        }
        contentSize -= readSize;
    }

    env->ReleaseByteArrayElements(buffer, bufData, 0);
    return;
}

svn_stream_t* SVNClient::createReadStream(apr_pool_t* pool, const char *path,
                                          Revision& revision,
                                          Revision &pegRevision, size_t& size)
{
    svn_stream_t *read_stream = NULL;

    if (revision.revision()->kind == svn_opt_revision_working)
    {
        // We want the working copy. Going back to the server returns
        // base instead (which is not what we want).
        apr_file_t *file = NULL;
        apr_finfo_t finfo;
        apr_status_t apr_err = apr_stat(&finfo, path, APR_FINFO_MIN, pool);
        if (apr_err)
        {
            JNIUtil::handleAPRError(apr_err, _("open file"));
            return NULL;
        }
        apr_err = apr_file_open(&file, path, APR_READ, 0, pool);
        if (apr_err)
        {
            JNIUtil::handleAPRError(apr_err, _("open file"));
            return NULL;
        }
        read_stream = svn_stream_from_aprfile(file, pool);
        size = finfo.size;
    }
    else
    {
        svn_client_ctx_t *ctx = getContext(NULL);
        if (ctx == NULL)
            return NULL;

        svn_stringbuf_t *buf = svn_stringbuf_create("", pool);
        read_stream = svn_stream_from_stringbuf(buf, pool);
        SVN_JNI_ERR(svn_client_cat2(read_stream, path, pegRevision.revision(),
                                    revision.revision(), ctx, pool),
                    NULL);
        size = buf->len;
    }

    return read_stream;
}


/**
 * create a DirEntry java object from svn_dirent_t structure
 */
jobject SVNClient::createJavaDirEntry(const char *path, svn_dirent_t *dirent)
{
    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/DirEntry");
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
                               "(Ljava/lang/String;IJZJJLjava/lang/String;)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;

    }

    jstring jPath = JNIUtil::makeJString(path);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jint jNodeKind = EnumMapper::mapNodeKind(dirent->kind);
    jlong jSize = dirent->size;
    jboolean jHasProps = (dirent->has_props? JNI_TRUE : JNI_FALSE);
    jlong jLastChangedRevision = dirent->created_rev;
    jlong jLastChanged = dirent->time;
    jstring jLastAuthor = JNIUtil::makeJString(dirent->last_author);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobject ret = env->NewObject(clazz, mid, jPath, jNodeKind, jSize,
                                 jHasProps, jLastChangedRevision,
                                 jLastChanged, jLastAuthor);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jPath);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    if (jLastAuthor != NULL)
    {
        env->DeleteLocalRef(jLastAuthor);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return ret;
}

jobject SVNClient::revProperty(jobject jthis, const char *path,
                               const char *name, Revision &rev)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    SVN_JNI_NULL_PTR_EX(name, "name", NULL);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    const char *URL;
    svn_string_t *propval;
    svn_revnum_t set_rev;
    SVN_JNI_ERR(svn_client_url_from_path(&URL, intPath.c_str(),
                                         requestPool),
                NULL);

    if (URL == NULL)
    {
        SVN_JNI_ERR(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                            _("Either a URL or versioned item is required.")),
                    NULL);
    }

    SVN_JNI_ERR(svn_client_revprop_get(name, &propval, URL,
                                       rev.revision(), &set_rev, ctx,
                                       requestPool),
                NULL);
    if (propval == NULL)
        return NULL;

    return createJavaProperty(jthis, path, name, propval);
}
void SVNClient::relocate(const char *from, const char *to, const char *path,
                         bool recurse)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(from, "from", );
    SVN_JNI_NULL_PTR_EX(to, "to", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    Path intFrom(from);
    SVN_JNI_ERR(intFrom.error_occured(), );

    Path intTo(to);
    SVN_JNI_ERR(intTo.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_relocate(intPath.c_str(), intFrom.c_str(),
                                    intTo.c_str(), recurse, ctx,
                                    requestPool), );
}

void SVNClient::blame(const char *path, Revision &pegRevision,
                      Revision &revisionStart, Revision &revisionEnd,
                      bool ignoreMimeType, BlameCallback *callback)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_blame3(intPath.c_str(), pegRevision.revision(),
                                  revisionStart.revision(),
                                  revisionEnd.revision(),
                                  svn_diff_file_options_create(requestPool),
                                  false, BlameCallback::callback, callback, ctx,
                                  requestPool),
                );
}

void SVNClient::setConfigDirectory(const char *configDir)
{
    // A change to the config directory may necessitate creation of
    // the config templates.
    Pool requestPool;
    SVN_JNI_ERR(svn_config_ensure(configDir, requestPool), );

    m_configDir = (configDir == NULL ? "" : configDir);
}

const char *SVNClient::getConfigDirectory()
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

svn_error_t *SVNClient::checkCancel(void *cancelBaton)
{
    SVNClient *that = (SVNClient*)cancelBaton;
    if (that->m_cancelOperation)
        return svn_error_create (SVN_ERR_CANCELLED, NULL,
            _("Operation canceled"));
    else
        return SVN_NO_ERROR;
}

void SVNClient::addToChangelist(Targets &srcPaths, const char *changelist)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);

    const apr_array_header_t *srcs = srcPaths.array(requestPool);
    SVN_JNI_ERR(srcPaths.error_occured(), );

    SVN_JNI_ERR(svn_client_add_to_changelist(srcs, changelist, ctx,
                                             requestPool), );
}

void SVNClient::removeFromChangelist(Targets &srcPaths, const char *changelist)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);

    const apr_array_header_t *srcs = srcPaths.array(requestPool);
    SVN_JNI_ERR(srcPaths.error_occured(), );

    SVN_JNI_ERR(svn_client_remove_from_changelist(srcs, changelist, ctx,
                                                  requestPool), );
}

jobjectArray SVNClient::getChangelist(const char *changelist,
                                      const char *rootPath)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);
    apr_array_header_t *paths;

    SVN_JNI_ERR(svn_client_get_changelist(&paths, changelist, rootPath,
                                          ctx, requestPool),
                NULL);

    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass("java/lang/String");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobjectArray ret = env->NewObjectArray(paths->nelts, clazz, NULL);

    for (int i = 0; i < paths->nelts; i++)
    {
        const char *path = APR_ARRAY_IDX(paths, i, const char *);
        jstring jpath = JNIUtil::makeJString(path);

        env->SetObjectArrayElement(ret, i, jpath);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return ret;
}

jobject SVNClient::createJavaLock(const svn_lock_t *lock)
{
    if (lock == NULL)
        return NULL;
    JNIEnv *env = JNIUtil::getEnv();

    jclass clazz = env->FindClass(JAVA_PACKAGE"/Lock");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
            "Ljava/lang/String;JJ)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jstring jOwner = JNIUtil::makeJString(lock->owner);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jPath = JNIUtil::makeJString(lock->path);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jToken = JNIUtil::makeJString(lock->token);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jstring jComment = JNIUtil::makeJString(lock->comment);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jlong jCreationDate = lock->creation_date;
    jlong jExpirationDate = lock->expiration_date;
    jobject ret = env->NewObject(clazz, mid, jOwner, jPath, jToken, jComment,
        jCreationDate, jExpirationDate);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jOwner);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jPath);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jToken);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(jComment);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return ret;
}

void SVNClient::lock(Targets &targets, const char *comment, bool force)
{
    Pool requestPool;
    const apr_array_header_t *targetsApr = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);

    SVN_JNI_ERR(svn_client_lock(targetsApr, comment, force, ctx,
                                requestPool), );
}

void SVNClient::unlock(Targets &targets, bool force)
{
    Pool requestPool;

    const apr_array_header_t *targetsApr = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);
    SVN_JNI_ERR(svn_client_unlock((apr_array_header_t*)targetsApr, force,
                                  ctx, requestPool), );
}
void SVNClient::setRevProperty(jobject jthis, const char *path,
                               const char *name, Revision &rev,
                               const char *value, bool force)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(name, "name", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    const char *URL;
    SVN_JNI_ERR(svn_client_url_from_path(&URL, intPath.c_str(),
                                         requestPool), );

    if (URL == NULL)
    {
        SVN_JNI_ERR(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                            _("Either a URL or versioned item is required.")),
                    );
    }

    svn_string_t *val = svn_string_create(value, requestPool);

    svn_revnum_t set_revision;
    SVN_JNI_ERR(svn_client_revprop_set(name, val, URL, rev.revision(),
                                       &set_revision, force, ctx,
                                       requestPool), );
}

struct version_status_baton
{
  svn_revnum_t min_rev;   /* lowest revision found. */
  svn_revnum_t max_rev;   /* highest revision found. */
  svn_boolean_t switched; /* is anything switched? */
  svn_boolean_t modified; /* is anything modified? */
  svn_boolean_t committed; /* examine last committed revisions */
  svn_boolean_t done;     /* note completion of our task. */
  const char *wc_path;    /* path whose URL we're looking for. */
  const char *wc_url;     /* URL for the path whose URL we're looking for. */
  apr_pool_t *pool;       /* pool in which to store alloc-needy things. */
};

/* This implements `svn_cancel_func_t'. */
static svn_error_t *
cancel (void *baton)
{
  struct version_status_baton *sb = (version_status_baton *)baton;
  if (sb->done)
    return svn_error_create (SVN_ERR_CANCELLED, NULL, "Finished");
  else
    return SVN_NO_ERROR;
}

/* An svn_wc_status_func_t callback function for anaylyzing status
   structures. */
static void
analyze_status(void *baton,
               const char *path,
               svn_wc_status_t *status)
{
  struct version_status_baton *sb = (version_status_baton *)baton;

  if (sb->done)
    return;

  if (! status->entry)
    return;

  /* Added files have a revision of no interest */
  if (status->text_status != svn_wc_status_added)
    {
      svn_revnum_t item_rev = (sb->committed
                               ? status->entry->cmt_rev
                               : status->entry->revision);

      if (sb->min_rev == SVN_INVALID_REVNUM || item_rev < sb->min_rev)
        sb->min_rev = item_rev;

      if (sb->max_rev == SVN_INVALID_REVNUM || item_rev > sb->max_rev)
        sb->max_rev = item_rev;
    }

  sb->switched |= status->switched;
  sb->modified |= (status->text_status != svn_wc_status_normal);
  sb->modified |= (status->prop_status != svn_wc_status_normal
                   && status->prop_status != svn_wc_status_none);

  if (sb->wc_path
      && (! sb->wc_url)
      && (strcmp (path, sb->wc_path) == 0)
      && (status->entry))
    sb->wc_url = apr_pstrdup (sb->pool, status->entry->url);
}


/* This implements `svn_wc_notify_func_t'. */
static void
notify(void *baton,
       const char *path,
       svn_wc_notify_action_t action,
       svn_node_kind_t kind,
       const char *mime_type,
       svn_wc_notify_state_t content_state,
       svn_wc_notify_state_t prop_state,
       svn_revnum_t revision)
{
  struct version_status_baton *sb = (version_status_baton *)baton;
  if ((action == svn_wc_notify_status_external)
      || (action == svn_wc_notify_status_completed))
    sb->done = TRUE;
}

jstring SVNClient::getVersionInfo(const char *path, const char *trailUrl,
                                  bool lastChanged)
{
    struct version_status_baton sb;
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    sb.switched = FALSE;
    sb.modified = FALSE;
    sb.committed = FALSE;
    sb.min_rev = SVN_INVALID_REVNUM;
    sb.max_rev = SVN_INVALID_REVNUM;
    sb.wc_path = NULL;
    sb.wc_url = NULL;
    sb.done = FALSE;
    sb.pool = requestPool;

    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    int wc_format;
    svn_client_ctx_t ctx = { 0 };
    SVN_JNI_ERR(svn_wc_check_wc(intPath.c_str(), &wc_format,
                                requestPool),
                NULL);

    if (! wc_format)
    {
        svn_node_kind_t kind;
        SVN_JNI_ERR(svn_io_check_path(intPath.c_str(), &kind,
                                      requestPool),
                    NULL);
        if (kind == svn_node_dir)
        {
            return JNIUtil::makeJString("exported");
        }
        else
        {
            char *message = JNIUtil::getFormatBuffer();
            apr_snprintf(message, JNIUtil::formatBufferSize,
                _("'%s' not versioned, and not exported\n"), path);
            return JNIUtil::makeJString(message);
        }
    }

    sb.wc_path = path;
    svn_opt_revision_t rev;
    rev.kind = svn_opt_revision_unspecified;
    ctx.config = apr_hash_make (requestPool);

  /* Setup the notification and cancellation callbacks, and their
     shared baton (which is also shared with the status function). */
    ctx.notify_func = notify;
    ctx.notify_baton = &sb;
    ctx.cancel_func = cancel;
    ctx.cancel_baton = &sb;

    svn_error_t *err;
    err = svn_client_status(NULL, intPath.c_str(), &rev, analyze_status,
                            &sb, TRUE, TRUE, FALSE, FALSE, &ctx,
                            requestPool);
    if (err && (err->apr_err == SVN_ERR_CANCELLED))
        svn_error_clear (err);
    else
        SVN_JNI_ERR(err, NULL);

    if ((! sb.switched ) && (trailUrl))
    {
        /* If the trailing part of the URL of the working copy directory
           does not match the given trailing URL then the whole working
           copy is switched. */
        if (! sb.wc_url)
        {
            sb.switched = TRUE;
        }
        else
        {
            apr_size_t len1 = strlen (trailUrl);
            apr_size_t len2 = strlen (sb.wc_url);
            if ((len1 > len2) || strcmp (sb.wc_url + len2 - len1, trailUrl))
                sb.switched = TRUE;
        }
    }

    std::ostringstream value;
    value << sb.min_rev;
    if (sb.min_rev != sb.max_rev)
    {
        value << ":";
        value << sb.max_rev;
    }
    if (sb.modified)
        value << "M";
    if (sb.switched)
        value << "S";

    return JNIUtil::makeJString(value.str().c_str());
}

jobjectArray SVNClient::revProperties(jobject jthis, const char *path,
                                      Revision &revision)
{
    apr_hash_t *props;
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    const char *URL;
    svn_revnum_t set_rev;
    SVN_JNI_ERR(svn_client_url_from_path(&URL, intPath.c_str(),
                                         requestPool),
                NULL);

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    SVN_JNI_ERR(svn_client_revprop_list(&props, URL, revision.revision(),
                                        &set_rev, ctx, requestPool),
                NULL);

    apr_hash_index_t *hi;

    int count = apr_hash_count (props);

    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobjectArray ret = env->NewObjectArray(count, clazz, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    int i = 0;
    for (hi = apr_hash_first (requestPool, props); hi;
         hi = apr_hash_next (hi), i++)
    {
        const char *key;
        svn_string_t *val;

        apr_hash_this(hi, (const void **)&key, NULL, (void**)&val);

        jobject object = createJavaProperty(jthis, path,
                                            key, val);

        env->SetObjectArrayElement(ret, i, object);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;

        env->DeleteLocalRef(object);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return ret;
}

struct info_baton
{
    std::vector<info_entry> infoVect;
    int info_ver;
    apr_pool_t *pool;
};

/**
 * get information about a file or directory
 */
jobject SVNClient::info(const char *path)
{
    Pool requestPool;
    svn_wc_adm_access_t *adm_access;
    const svn_wc_entry_t *entry;

    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    SVN_JNI_ERR(svn_wc_adm_probe_open2(&adm_access, NULL, intPath.c_str(),
                                       FALSE, 0, requestPool),
                NULL);
    SVN_JNI_ERR(svn_wc_entry(&entry, intPath.c_str(), adm_access, FALSE,
                             requestPool),
                NULL);

    return createJavaInfo(entry);
}
jobject SVNClient::createJavaInfo(const svn_wc_entry_t *entry)
{
    if (entry == NULL)
        return NULL;

    JNIEnv *env = JNIUtil::getEnv();

    jclass clazz = env->FindClass(JAVA_PACKAGE"/Info");
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    static jmethodID mid = 0;
    if (mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
            "Ljava/lang/String;IILjava/lang/String;JJLjava/util/Date;"
            "Ljava/util/Date;Ljava/util/Date;ZZZZJLjava/lang/String;)V");
        if (JNIUtil::isJavaExceptionThrown())
        {
            return NULL;
        }
    }

    jstring jName = JNIUtil::makeJString(entry->name);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jstring jUrl = JNIUtil::makeJString(entry->url);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jstring jUuid = JNIUtil::makeJString(entry->uuid);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jstring jRepository = JNIUtil::makeJString(entry->repos);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jint jSchedule = EnumMapper::mapScheduleKind(entry->schedule);
    jint jNodeKind = EnumMapper::mapNodeKind(entry->kind);
    jstring jAuthor = JNIUtil::makeJString(entry->cmt_author);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jlong jRevision = entry->revision;
    jlong jLastChangedRevision = entry->cmt_rev;
    jobject jLastChangedDate = JNIUtil::createDate(entry->cmt_date);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jobject jLastDateTextUpdate = JNIUtil::createDate(entry->text_time);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jobject jLastDatePropsUpdate = JNIUtil::createDate(entry->prop_time);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }
    jboolean jCopied = entry->copied ? JNI_TRUE : JNI_FALSE;
    jboolean jDeleted = entry->deleted ? JNI_TRUE : JNI_FALSE;
    jboolean jAbsent = entry->absent ? JNI_TRUE : JNI_FALSE;
    jboolean jIncomplete = entry->incomplete ? JNI_TRUE : JNI_FALSE;
    jlong jCopyRev = entry->copyfrom_rev;
    jstring jCopyUrl = JNIUtil::makeJString(entry->copyfrom_url);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    jobject ret = env->NewObject(clazz, mid, jName, jUrl, jUuid, jRepository,
        jSchedule, jNodeKind, jAuthor, jRevision, jLastChangedRevision,
        jLastChangedDate, jLastDateTextUpdate, jLastDatePropsUpdate, jCopied,
        jDeleted, jAbsent, jIncomplete, jCopyRev, jCopyUrl);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jName);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jUrl);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jUuid);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jRepository);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jAuthor);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jLastChangedDate);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jLastDateTextUpdate);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jLastDatePropsUpdate);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    env->DeleteLocalRef(jCopyUrl);
    if (JNIUtil::isJavaExceptionThrown())
    {
        return NULL;
    }

    return ret;
}

void
SVNClient::info2(const char *path, Revision &revision, Revision &pegRevision,
                 bool recurse, InfoCallback *callback)
{
    SVN_JNI_NULL_PTR_EX(path, "path", );

    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Path checkedPath(path);
    SVN_JNI_ERR(checkedPath.error_occured(), );

    SVN_JNI_ERR(svn_client_info(checkedPath.c_str(),
                                pegRevision.revision(),
                                revision.revision(),
                                InfoCallback::callback,
                                callback,
                                recurse ? TRUE : FALSE,
                                ctx, requestPool), );
}
