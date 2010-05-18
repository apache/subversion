/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2009 CollabNet.  All rights reserved.
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
 * @file SVNClient.cpp
 * @brief: Implementation of the SVNClient class
 */

#include "SVNClient.h"
#include "JNIUtil.h"
#include "Notify.h"
#include "Notify2.h"
#include "CopySources.h"
#include "DiffSummaryReceiver.h"
#include "ConflictResolverCallback.h"
#include "ProgressListener.h"
#include "Prompter.h"
#include "Pool.h"
#include "Targets.h"
#include "Revision.h"
#include "RevisionRange.h"
#include "BlameCallback.h"
#include "ProplistCallback.h"
#include "LogMessageCallback.h"
#include "InfoCallback.h"
#include "StatusCallback.h"
#include "ChangelistCallback.h"
#include "ListCallback.h"
#include "JNIByteArray.h"
#include "CommitMessage.h"
#include "EnumMapper.h"
#include "StringArray.h"
#include "RevpropTable.h"
#include "CreateJ.h"
#include "svn_auth.h"
#include "svn_dso.h"
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
    m_conflictResolver = NULL;
}

SVNClient::~SVNClient()
{
    delete m_notify;
    delete m_notify2;
    delete m_progressListener;
    delete m_prompter;
    delete m_conflictResolver;
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
    jstring name =
        JNIUtil::makeJString(svn_wc_get_adm_dir(requestPool.pool()));
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return name;
}

jboolean SVNClient::isAdminDirectory(const char *name)
{
    Pool requestPool;
    return svn_wc_is_adm_dir(name, requestPool.pool()) ? JNI_TRUE : JNI_FALSE;
}

const char *SVNClient::getLastPath()
{
    return m_lastPath.c_str();
}

/**
 * List directory entries of a URL.
 */
void SVNClient::list(const char *url, Revision &revision,
                     Revision &pegRevision, svn_depth_t depth,
                     int direntFields, bool fetchLocks,
                     ListCallback *callback)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_NULL_PTR_EX(url, "path or url", );

    Path urlPath(url);
    SVN_JNI_ERR(urlPath.error_occured(), );

    SVN_JNI_ERR(svn_client_list2(urlPath.c_str(),
                                 pegRevision.revision(),
                                 revision.revision(),
                                 depth,
                                 direntFields,
                                 fetchLocks,
                                 ListCallback::callback,
                                 callback,
                                 ctx, requestPool.pool()), );
}

void
SVNClient::status(const char *path, svn_depth_t depth,
                  bool onServer, bool getAll, bool noIgnore,
                  bool ignoreExternals, StringArray &changelists,
                  StatusCallback *callback)
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

    SVN_JNI_ERR(svn_client_status4(&youngest, checkedPath.c_str(),
                                   &rev, StatusCallback::callback,
                                   callback,
                                   depth,
                                   getAll, onServer, noIgnore,
                                   ignoreExternals,
                                   changelists.array(requestPool),
                                   ctx, requestPool.pool()), );
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
                            std::vector<RevisionRange> &logRanges,
                            bool stopOnCopy, bool discoverPaths,
                            bool includeMergedRevisions, StringArray &revProps,
                            long limit, LogMessageCallback *callback)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Targets target(path);
    const apr_array_header_t *targets = target.array(requestPool);
    SVN_JNI_ERR(target.error_occured(), );

    apr_array_header_t *ranges =
        apr_array_make(requestPool.pool(), logRanges.size(),
                       sizeof(svn_opt_revision_range_t *));

    std::vector<RevisionRange>::const_iterator it;
    for (it = logRanges.begin(); it != logRanges.end(); ++it)
    {
        if (it->toRange(requestPool)->start.kind
            == svn_opt_revision_unspecified
            && it->toRange(requestPool)->end.kind
            == svn_opt_revision_unspecified)
        {
            svn_opt_revision_range_t *range =
                (svn_opt_revision_range_t *)apr_pcalloc(requestPool.pool(),
                                                        sizeof(*range));
            range->start.kind = svn_opt_revision_number;
            range->start.value.number = 1;
            range->end.kind = svn_opt_revision_head;
            APR_ARRAY_PUSH(ranges, const svn_opt_revision_range_t *) = range;
        }
        else
        {
            APR_ARRAY_PUSH(ranges, const svn_opt_revision_range_t *) =
                it->toRange(requestPool);
        }
        if (JNIUtil::isExceptionThrown())
            return;
    }

    SVN_JNI_ERR(svn_client_log5(targets, pegRevision.revision(), ranges,
                                limit, discoverPaths, stopOnCopy,
                                includeMergedRevisions,
                                revProps.array(requestPool),
                                LogMessageCallback::callback, callback, ctx,
                                requestPool.pool()), );
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
    svn_revnum_t rev;

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_checkout3(&rev, url.c_str(),
                                     path.c_str(),
                                     pegRevision.revision(),
                                     revision.revision(),
                                     depth,
                                     ignoreExternals,
                                     allowUnverObstructions,
                                     ctx,
                                     requestPool.pool()),
                -1);

    return rev;
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

void SVNClient::setConflictResolver(ConflictResolverCallback *conflictResolver)
{
    delete m_conflictResolver;
    m_conflictResolver = conflictResolver;
}

void SVNClient::setProgressListener(ProgressListener *listener)
{
    delete m_progressListener;
    m_progressListener = listener;
}

void SVNClient::remove(Targets &targets, const char *message, bool force,
                       bool keep_local, RevpropTable &revprops)
{
    svn_commit_info_t *commit_info = NULL;
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    const apr_array_header_t *targets2 = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );

    SVN_JNI_ERR(svn_client_delete3(&commit_info, targets2, force, keep_local,
                                   revprops.hash(requestPool), ctx,
                                   requestPool.pool()), );
}

void SVNClient::revert(const char *path, svn_depth_t depth,
                       StringArray &changelists)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Targets target(path);
    const apr_array_header_t *targets = target.array(requestPool);
    SVN_JNI_ERR(target.error_occured(), );
    SVN_JNI_ERR(svn_client_revert2(targets, depth,
                                   changelists.array(requestPool), ctx,
                                   requestPool.pool()), );
}

void SVNClient::add(const char *path,
                    svn_depth_t depth, bool force, bool no_ignore,
                    bool add_parents)
{
    Pool requestPool;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_add4(intPath.c_str(), depth, force,
                                no_ignore, add_parents, ctx,
                                requestPool.pool()), );
}

jlongArray SVNClient::update(Targets &targets, Revision &revision,
                             svn_depth_t depth, bool depthIsSticky,
                             bool ignoreExternals,
                             bool allowUnverObstructions)
{
    Pool requestPool;

    svn_client_ctx_t *ctx = getContext(NULL);
    apr_array_header_t *revs;
    if (ctx == NULL)
        return NULL;

    const apr_array_header_t *array = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), NULL);
    SVN_JNI_ERR(svn_client_update3(&revs, array,
                                   revision.revision(),
                                   depth,
                                   depthIsSticky,
                                   ignoreExternals,
                                   allowUnverObstructions,
                                   ctx, requestPool.pool()),
                NULL);

    JNIEnv *env = JNIUtil::getEnv();
    jlongArray jrevs = env->NewLongArray(revs->nelts);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    jlong *jrevArray = env->GetLongArrayElements(jrevs, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;
    for (int i = 0; i < revs->nelts; ++i)
    {
        jlong rev = APR_ARRAY_IDX(revs, i, svn_revnum_t);
        jrevArray[i] = rev;
    }
    env->ReleaseLongArrayElements(jrevs, jrevArray, 0);

    return jrevs;
}

jlong SVNClient::commit(Targets &targets, const char *message,
                        svn_depth_t depth, bool noUnlock, bool keepChangelist,
                        StringArray &changelists, RevpropTable &revprops)
{
    Pool requestPool;
    svn_commit_info_t *commit_info = NULL;
    const apr_array_header_t *targets2 = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), -1);
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return SVN_INVALID_REVNUM;

    SVN_JNI_ERR(svn_client_commit4(&commit_info, targets2, depth,
                                   noUnlock, keepChangelist,
                                   changelists.array(requestPool),
                                   revprops.hash(requestPool), ctx,
                                   requestPool.pool()),
                SVN_INVALID_REVNUM);

    if (commit_info && SVN_IS_VALID_REVNUM(commit_info->revision))
        return commit_info->revision;

    return SVN_INVALID_REVNUM;
}

void SVNClient::copy(CopySources &copySources, const char *destPath,
                     const char *message, bool copyAsChild, bool makeParents,
                     RevpropTable &revprops)
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
                                 copyAsChild, makeParents,
                                 revprops.hash(requestPool), ctx,
                                 requestPool.pool()), );
}

void SVNClient::move(Targets &srcPaths, const char *destPath,
                     const char *message, bool force, bool moveAsChild,
                     bool makeParents, RevpropTable &revprops)
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
                                 makeParents, revprops.hash(requestPool), ctx,
                                 requestPool.pool()), );
}

void SVNClient::mkdir(Targets &targets, const char *message, bool makeParents,
                      RevpropTable &revprops)
{
    Pool requestPool;
    svn_commit_info_t *commit_info = NULL;
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    const apr_array_header_t *targets2 = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );

    SVN_JNI_ERR(svn_client_mkdir3(&commit_info, targets2, makeParents,
                                  revprops.hash(requestPool), ctx,
                                  requestPool.pool()), );
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

    SVN_JNI_ERR(svn_client_cleanup(intPath.c_str(), ctx, requestPool.pool()),);
}

void SVNClient::resolve(const char *path, svn_depth_t depth,
                        svn_wc_conflict_choice_t choice)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_resolve(intPath.c_str(), depth, choice,
                                   ctx, requestPool.pool()), );
}

jlong SVNClient::doExport(const char *srcPath, const char *destPath,
                          Revision &revision, Revision &pegRevision,
                          bool force, bool ignoreExternals,
                          svn_depth_t depth, const char *nativeEOL)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(srcPath, "srcPath", -1);
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", -1);
    Path sourcePath(srcPath);
    SVN_JNI_ERR(sourcePath.error_occured(), -1);
    Path destinationPath(destPath);
    SVN_JNI_ERR(destinationPath.error_occured(), -1);
    svn_revnum_t rev;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_export4(&rev, sourcePath.c_str(),
                                   destinationPath.c_str(),
                                   pegRevision.revision(),
                                   revision.revision(), force,
                                   ignoreExternals,
                                   depth,
                                   nativeEOL, ctx,
                                   requestPool.pool()),
                -1);

    return rev;

}

jlong SVNClient::doSwitch(const char *path, const char *url,
                          Revision &revision, Revision &pegRevision,
                          svn_depth_t depth, bool depthIsSticky,
                          bool ignoreExternals,
                          bool allowUnverObstructions)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", -1);
    SVN_JNI_NULL_PTR_EX(url, "url", -1);
    Path intUrl(url);
    SVN_JNI_ERR(intUrl.error_occured(), -1);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), -1);

    svn_revnum_t rev;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_switch2(&rev, intPath.c_str(),
                                   intUrl.c_str(),
                                   pegRevision.revision(),
                                   revision.revision(),
                                   depth,
                                   depthIsSticky,
                                   ignoreExternals,
                                   allowUnverObstructions,
                                   ctx,
                                   requestPool.pool()),
                -1);

    return rev;
}

void SVNClient::doImport(const char *path, const char *url,
                         const char *message, svn_depth_t depth,
                         bool noIgnore, bool ignoreUnknownNodeTypes,
                         RevpropTable &revprops)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(url, "url", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );
    Path intUrl(url);
    SVN_JNI_ERR(intUrl.error_occured(), );

    svn_commit_info_t *commit_info = NULL;
    svn_client_ctx_t *ctx = getContext(message);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_import3(&commit_info, intPath.c_str(),
                                   intUrl.c_str(), depth, noIgnore,
                                   ignoreUnknownNodeTypes,
                                   revprops.hash(requestPool), ctx,
                                   requestPool.pool()), );
}

jobjectArray
SVNClient::suggestMergeSources(const char *path, Revision &pegRevision)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    apr_array_header_t *sources;
    SVN_JNI_ERR(svn_client_suggest_merge_sources(&sources, path,
                                                 pegRevision.revision(),
                                                 ctx, requestPool.pool()),
                NULL);

    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass("java/lang/String");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobjectArray jsuggestions = env->NewObjectArray(sources->nelts, clazz,
                                                    NULL);
    for (int i = 0; i < sources->nelts; ++i)
    {
        const char *source = APR_ARRAY_IDX(sources, i, const char *);
        jstring jpath = JNIUtil::makeJString(source);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;

        env->SetObjectArrayElement(jsuggestions, i, jpath);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return jsuggestions;
}

void SVNClient::merge(const char *path1, Revision &revision1,
                      const char *path2, Revision &revision2,
                      const char *localPath, bool force, svn_depth_t depth,
                      bool ignoreAncestry, bool dryRun, bool recordOnly)
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
                                  ignoreAncestry, force, recordOnly, dryRun,
                                  NULL, ctx, requestPool.pool()), );
}

void SVNClient::merge(const char *path, Revision &pegRevision,
                      std::vector<RevisionRange> &rangesToMerge,
                      const char *localPath, bool force, svn_depth_t depth,
                      bool ignoreAncestry, bool dryRun, bool recordOnly)
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

    apr_array_header_t *ranges =
      apr_array_make(requestPool.pool(), rangesToMerge.size(),
                     sizeof(const svn_opt_revision_range_t *));

    std::vector<RevisionRange>::const_iterator it;
    for (it = rangesToMerge.begin(); it != rangesToMerge.end(); ++it)
    {
        if (it->toRange(requestPool)->start.kind
            == svn_opt_revision_unspecified
            && it->toRange(requestPool)->end.kind
            == svn_opt_revision_unspecified)
        {
            svn_opt_revision_range_t *range =
                (svn_opt_revision_range_t *)apr_pcalloc(requestPool.pool(),
                                                        sizeof(*range));
            range->start.kind = svn_opt_revision_number;
            range->start.value.number = 1;
            range->end.kind = svn_opt_revision_head;
            APR_ARRAY_PUSH(ranges, const svn_opt_revision_range_t *) = range;
        }
        else
        {
            APR_ARRAY_PUSH(ranges, const svn_opt_revision_range_t *) =
                it->toRange(requestPool);
        }
        if (JNIUtil::isExceptionThrown())
            return;
    }

    SVN_JNI_ERR(svn_client_merge_peg3(srcPath.c_str(),
                                      ranges,
                                      pegRevision.revision(),
                                      intLocalPath.c_str(),
                                      depth,
                                      ignoreAncestry, force, recordOnly,
                                      dryRun, NULL, ctx,
                                      requestPool.pool()), );
}

void SVNClient::mergeReintegrate(const char *path, Revision &pegRevision,
                                 const char *localPath, bool dryRun)
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

    SVN_JNI_ERR(svn_client_merge_reintegrate(srcPath.c_str(),
                                             pegRevision.revision(),
                                             intLocalPath.c_str(),
                                             dryRun, NULL, ctx,
                                             requestPool.pool()), );
}

jobject
SVNClient::getMergeinfo(const char *target, Revision &pegRevision)
{
    Pool requestPool;
    JNIEnv *env = JNIUtil::getEnv();

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    svn_mergeinfo_t mergeinfo;
    Path intLocalTarget(target);
    SVN_JNI_ERR(intLocalTarget.error_occured(), NULL);
    SVN_JNI_ERR(svn_client_mergeinfo_get_merged(&mergeinfo,
                                                intLocalTarget.c_str(),
                                                pegRevision.revision(), ctx,
                                                requestPool.pool()),
                NULL);
    if (mergeinfo == NULL)
        return NULL;

    // Transform mergeinfo into Java Mergeinfo object.
    jclass clazz = env->FindClass(JAVA_PACKAGE "/Mergeinfo");
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
                                        "[L"JAVA_PACKAGE"/RevisionRange;)V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jobject jmergeinfo = env->NewObject(clazz, ctor);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    apr_hash_index_t *hi;
    for (hi = apr_hash_first(requestPool.pool(), mergeinfo);
         hi;
         hi = apr_hash_next(hi))
    {
        const void *path;
        void *val;
        apr_hash_this(hi, &path, NULL, &val);

        jstring jpath = JNIUtil::makeJString((const char *) path);
        jobjectArray jranges =
            CreateJ::RevisionRangeArray((apr_array_header_t *) val);

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

void SVNClient::getMergeinfoLog(int type, const char *pathOrURL,
                                Revision &pegRevision,
                                const char *mergeSourceURL,
                                Revision &srcPegRevision,
                                bool discoverChangedPaths,
                                StringArray &revProps,
                                LogMessageCallback *callback)
{
    Pool requestPool;

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_NULL_PTR_EX(pathOrURL, "path or url", );
    Path urlPath(pathOrURL);
    SVN_JNI_ERR(urlPath.error_occured(), );

    SVN_JNI_NULL_PTR_EX(mergeSourceURL, "merge source url", );
    Path srcURL(mergeSourceURL);
    SVN_JNI_ERR(srcURL.error_occured(), );

    switch (type)
      {
        case 0:
            SVN_JNI_ERR(
                svn_client_mergeinfo_log_eligible(urlPath.c_str(),
                                                  pegRevision.revision(),
                                                  srcURL.c_str(),
                                                  srcPegRevision.revision(),
                                                  LogMessageCallback::callback,
                                                  callback,
                                                  discoverChangedPaths,
                                                  revProps.array(requestPool),
                                                  ctx,
                                                  requestPool.pool()), );
            return;

        case 1:
            SVN_JNI_ERR(
                svn_client_mergeinfo_log_merged(urlPath.c_str(),
                                                pegRevision.revision(),
                                                srcURL.c_str(),
                                                srcPegRevision.revision(),
                                                LogMessageCallback::callback,
                                                callback,
                                                discoverChangedPaths,
                                                revProps.array(requestPool),
                                                ctx,
                                                requestPool.pool()), );
            return;
      }

    return;
}

/**
 * Get a property.
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
    SVN_JNI_ERR(svn_client_propget3(&props, name,
                                    intPath.c_str(), pegRevision.revision(),
                                    revision.revision(), NULL, svn_depth_empty,
                                    NULL, ctx, requestPool.pool()),
                NULL);

    apr_hash_index_t *hi;
    // only one element since we disabled recurse
    hi = apr_hash_first(requestPool.pool(), props);
    if (hi == NULL)
        return NULL; // no property with this name

    svn_string_t *propval;
    apr_hash_this(hi, NULL, NULL, (void**)&propval);

    if (propval == NULL)
        return NULL;

    return CreateJ::Property(jthis, path, name, propval);
}

void SVNClient::properties(const char *path, Revision &revision,
                           Revision &pegRevision, svn_depth_t depth,
                           StringArray &changelists, ProplistCallback *callback)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_proplist3(intPath.c_str(), pegRevision.revision(),
                                     revision.revision(), depth,
                                     changelists.array(requestPool),
                                     ProplistCallback::callback, callback,
                                     ctx, requestPool.pool()), );

    return;
}

void SVNClient::propertySet(const char *path, const char *name,
                            const char *value, svn_depth_t depth,
                            StringArray &changelists, bool force,
                            RevpropTable &revprops)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(name, "name", );

    svn_string_t *val;
    if (value == NULL)
      val = NULL;
    else
      val = svn_string_create(value, requestPool.pool());

    svn_commit_info_t *commit_info = NULL;
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_propset3(&commit_info, name, val, intPath.c_str(),
                                    depth, force, SVN_INVALID_REVNUM,
                                    changelists.array(requestPool),
                                    revprops.hash(requestPool),
                                    ctx, requestPool.pool()), );
}

void SVNClient::diff(const char *target1, Revision &revision1,
                     const char *target2, Revision &revision2,
                     Revision *pegRevision, const char *relativeToDir,
                     const char *outfileName, svn_depth_t depth,
                     StringArray &changelists,
                     bool ignoreAncestry, bool noDiffDelete, bool force)
{
    svn_error_t *err;
    Pool requestPool;
    const char *c_relToDir = relativeToDir ?
      svn_path_canonicalize(relativeToDir, requestPool.pool()) :
      relativeToDir;

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
                      svn_path_internal_style(outfileName, requestPool.pool()),
                      APR_CREATE|APR_WRITE|APR_TRUNCATE , APR_OS_DEFAULT,
                      requestPool.pool());
    if (rv != APR_SUCCESS)
    {
        SVN_JNI_ERR(svn_error_createf(rv, NULL, _("Cannot open file '%s'"),
                                      outfileName), );
    }

    // We don't use any options to diff.
    apr_array_header_t *diffOptions = apr_array_make(requestPool.pool(),
                                                     0, sizeof(char *));

    if (pegRevision)
    {
        err = svn_client_diff_peg4(diffOptions,
                                   path1.c_str(),
                                   pegRevision->revision(),
                                   revision1.revision(),
                                   revision2.revision(),
                                   c_relToDir,
                                   depth,
                                   ignoreAncestry,
                                   noDiffDelete,
                                   force,
                                   SVN_APR_LOCALE_CHARSET,
                                   outfile,
                                   NULL /* error file */,
                                   changelists.array(requestPool),
                                   ctx,
                                   requestPool.pool());
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
                               c_relToDir,
                               depth,
                               ignoreAncestry,
                               noDiffDelete,
                               force,
                               SVN_APR_LOCALE_CHARSET,
                               outfile,
                               NULL /* error file */,
                               changelists.array(requestPool),
                               ctx,
                               requestPool.pool());
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
                     const char *relativeToDir, const char *outfileName,
                     svn_depth_t depth, StringArray &changelists,
                     bool ignoreAncestry, bool noDiffDelete, bool force)
{
    diff(target1, revision1, target2, revision2, NULL, relativeToDir,
         outfileName, depth, changelists, ignoreAncestry, noDiffDelete, force);
}

void SVNClient::diff(const char *target, Revision &pegRevision,
                     Revision &startRevision, Revision &endRevision,
                     const char *relativeToDir, const char *outfileName,
                     svn_depth_t depth, StringArray &changelists,
                     bool ignoreAncestry, bool noDiffDelete, bool force)
{
    diff(target, startRevision, NULL, endRevision, &pegRevision,
         relativeToDir, outfileName, depth, changelists,
         ignoreAncestry, noDiffDelete, force);
}

void
SVNClient::diffSummarize(const char *target1, Revision &revision1,
                         const char *target2, Revision &revision2,
                         svn_depth_t depth, StringArray &changelists,
                         bool ignoreAncestry,
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
                                           changelists.array(requestPool),
                                           DiffSummaryReceiver::summarize,
                                           &receiver,
                                           ctx, requestPool.pool()), );
}

void
SVNClient::diffSummarize(const char *target, Revision &pegRevision,
                         Revision &startRevision, Revision &endRevision,
                         svn_depth_t depth, StringArray &changelists,
                         bool ignoreAncestry, DiffSummaryReceiver &receiver)
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
                                               changelists.array(requestPool),
                                               DiffSummaryReceiver::summarize,
                                               &receiver, ctx,
                                               requestPool.pool()), );
}

svn_client_ctx_t *SVNClient::getContext(const char *message)
{
    apr_pool_t *pool = JNIUtil::getRequestPool()->pool();
    svn_auth_baton_t *ab;
    svn_client_ctx_t *ctx;
    SVN_JNI_ERR(svn_client_create_context(&ctx, pool), NULL);

    const char *configDir = m_configDir.c_str();
    if (m_configDir.length() == 0)
        configDir = NULL;
    SVN_JNI_ERR(svn_config_get_config(&(ctx->config), configDir, pool), NULL);
    svn_config_t *config = (svn_config_t *) apr_hash_get(ctx->config,
                                                         SVN_CONFIG_CATEGORY_CONFIG,
                                                         APR_HASH_KEY_STRING);

    /* The whole list of registered providers */
    apr_array_header_t *providers;

    /* Populate the registered providers with the platform-specific providers */
    SVN_JNI_ERR(svn_auth_get_platform_specific_client_providers(&providers,
                                                                config,
                                                                pool),
                NULL);

    /* The main disk-caching auth providers, for both
     * 'username/password' creds and 'username' creds.  */
    svn_auth_provider_object_t *provider;

    svn_auth_get_simple_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_username_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    /* The server-cert, client-cert, and client-cert-password providers. */
    SVN_JNI_ERR(svn_auth_get_platform_specific_provider(&provider,
                                                        "windows",
                                                        "ssl_server_trust",
                                                        pool),
                NULL);

    if (provider)
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    svn_auth_get_ssl_server_trust_file_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_file_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    svn_auth_get_ssl_client_cert_pw_file_provider(&provider, pool);
    APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

    if (m_prompter != NULL)
    {
        /* Two basic prompt providers: username/password, and just username.*/
        provider = m_prompter->getProviderSimple();

        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderUsername();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        /* Three ssl prompt providers, for server-certs, client-certs,
         * and client-cert-passphrases.  */
        provider = m_prompter->getProviderServerSSLTrust();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderClientSSL();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;

        provider = m_prompter->getProviderClientSSLPassword();
        APR_ARRAY_PUSH(providers, svn_auth_provider_object_t *) = provider;
    }

    /* Build an authentication baton to give to libsvn_client. */
    svn_auth_open(&ab, providers, pool);

    /* Place any default --username or --password credentials into the
     * auth_baton's run-time parameter hash.  ### Same with --no-auth-cache? */
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
    ctx->notify_func2= Notify2::notify;
    ctx->notify_baton2 = m_notify2;

    ctx->progress_func = ProgressListener::progress;
    ctx->progress_baton = m_progressListener;

    if (m_conflictResolver)
    {
        ctx->conflict_func = ConflictResolverCallback::resolveConflict;
        ctx->conflict_baton = m_conflictResolver;
    }

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
            apr_palloc(JNIUtil::getRequestPool()->pool(), sizeof(*baton));

        baton->message = message;
        baton->messageHandler = m_commitMessage;

        return baton;
    }
    return NULL;
}

jbyteArray SVNClient::fileContent(const char *path, Revision &revision,
                                  Revision &pegRevision)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    size_t size = 0;
    svn_stream_t *read_stream = createReadStream(requestPool.pool(),
                                                 intPath.c_str(), revision,
                                                 pegRevision, size);
    if (read_stream == NULL)
        return NULL;

    JNIEnv *env = JNIUtil::getEnv();
    // size will be set to the number of bytes available.
    jbyteArray jcontent = env->NewByteArray(size);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jbyte *jbytes = env->GetByteArrayElements(jcontent, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    svn_error_t *err = svn_stream_read(read_stream, (char *) jbytes, &size);
    env->ReleaseByteArrayElements(jcontent, jbytes, 0);
    SVN_JNI_ERR(err, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return jcontent;
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
    svn_stream_t *read_stream = createReadStream(requestPool.pool(), path,
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

svn_stream_t *SVNClient::createReadStream(apr_pool_t *pool, const char *path,
                                          Revision &revision,
                                          Revision &pegRevision, size_t &size)
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
        read_stream = svn_stream_from_aprfile2(file, TRUE, pool);
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
                                         requestPool.pool()),
                NULL);

    if (URL == NULL)
    {
        SVN_JNI_ERR(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                     _("Either a URL or versioned item is required.")),
                    NULL);
    }

    SVN_JNI_ERR(svn_client_revprop_get(name, &propval, URL,
                                       rev.revision(), &set_rev, ctx,
                                       requestPool.pool()),
                NULL);
    if (propval == NULL)
        return NULL;

    return CreateJ::Property(jthis, path, name, propval);
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
                                    requestPool.pool()), );
}

void SVNClient::blame(const char *path, Revision &pegRevision,
                      Revision &revisionStart, Revision &revisionEnd,
                      bool ignoreMimeType, bool includeMergedRevisions,
                      BlameCallback *callback)
{
    Pool requestPool;
    SVN_JNI_NULL_PTR_EX(path, "path", );
    apr_pool_t *pool = requestPool.pool();
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), );

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_blame4(intPath.c_str(), pegRevision.revision(),
                                  revisionStart.revision(),
                                  revisionEnd.revision(),
                                  svn_diff_file_options_create(pool),
                                  ignoreMimeType, includeMergedRevisions,
                                  BlameCallback::callback, callback, ctx,
                                  pool),
        );
}

void SVNClient::setConfigDirectory(const char *configDir)
{
    // A change to the config directory may necessitate creation of
    // the config templates.
    Pool requestPool;
    SVN_JNI_ERR(svn_config_ensure(configDir, requestPool.pool()), );

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
        return svn_error_create(SVN_ERR_CANCELLED, NULL,
                                _("Operation canceled"));
    else
        return SVN_NO_ERROR;
}

void SVNClient::addToChangelist(Targets &srcPaths, const char *changelist,
                                svn_depth_t depth, StringArray &changelists)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);

    const apr_array_header_t *srcs = srcPaths.array(requestPool);
    SVN_JNI_ERR(srcPaths.error_occured(), );

    SVN_JNI_ERR(svn_client_add_to_changelist(srcs, changelist, depth,
                                             changelists.array(requestPool),
                                             ctx, requestPool.pool()), );
}

void SVNClient::removeFromChangelists(Targets &srcPaths, svn_depth_t depth,
                                      StringArray &changelists)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);

    const apr_array_header_t *srcs = srcPaths.array(requestPool);
    SVN_JNI_ERR(srcPaths.error_occured(), );

    SVN_JNI_ERR(svn_client_remove_from_changelists(srcs, depth,
                                                changelists.array(requestPool),
                                                ctx, requestPool.pool()), );
}

void SVNClient::getChangelists(const char *rootPath,
                               StringArray &changelists,
                               svn_depth_t depth,
                               ChangelistCallback *callback)
{
    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);

    SVN_JNI_ERR(svn_client_get_changelists(rootPath,
                                           changelists.array(requestPool),
                                           depth, ChangelistCallback::callback,
                                           callback, ctx, requestPool.pool()),
                );
}

void SVNClient::lock(Targets &targets, const char *comment, bool force)
{
    Pool requestPool;
    const apr_array_header_t *targetsApr = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);

    SVN_JNI_ERR(svn_client_lock(targetsApr, comment, force, ctx,
                                requestPool.pool()), );
}

void SVNClient::unlock(Targets &targets, bool force)
{
    Pool requestPool;

    const apr_array_header_t *targetsApr = targets.array(requestPool);
    SVN_JNI_ERR(targets.error_occured(), );
    svn_client_ctx_t *ctx = getContext(NULL);
    SVN_JNI_ERR(svn_client_unlock((apr_array_header_t*)targetsApr, force,
                                  ctx, requestPool.pool()), );
}
void SVNClient::setRevProperty(jobject jthis, const char *path,
                               const char *name, Revision &rev,
                               const char *value, const char *original_value,
                               bool force)
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
                                         requestPool.pool()), );

    if (URL == NULL)
    {
        SVN_JNI_ERR(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                     _("Either a URL or versioned item is required.")),
            );
    }

    svn_string_t *val = svn_string_create(value, requestPool.pool());
    svn_string_t *orig_val;
    if (original_value != NULL)
      orig_val = svn_string_create(original_value, requestPool.pool());
    else
      orig_val = NULL;

    svn_revnum_t set_revision;
    SVN_JNI_ERR(svn_client_revprop_set2(name, val, orig_val, URL, rev.revision(),
                                        &set_revision, force, ctx,
                                        requestPool.pool()), );
}

struct version_status_baton
{
    svn_revnum_t min_rev;    /* lowest revision found. */
    svn_revnum_t max_rev;    /* highest revision found. */
    svn_boolean_t switched;  /* is anything switched? */
    svn_boolean_t modified;  /* is anything modified? */
    svn_boolean_t committed; /* examine last committed revisions */
    svn_boolean_t done;      /* note completion of our task. */
    const char *wc_path;     /* path whose URL we're looking for. */
    const char *wc_url;      /* URL for the path whose URL we're looking for. */
    apr_pool_t *pool;        /* pool in which to store alloc-needy things. */
};

/* This implements `svn_cancel_func_t'. */
static svn_error_t *
cancel(void *baton)
{
    struct version_status_baton *sb = (version_status_baton *)baton;
    if (sb->done)
        return svn_error_create(SVN_ERR_CANCELLED, NULL, "Finished");
    else
        return SVN_NO_ERROR;
}

/* An svn_wc_status_func3_t callback function for analyzing status
 * structures. */
static svn_error_t *
analyze_status(void *baton,
               const char *path,
               svn_wc_status2_t *status,
               apr_pool_t *pool)
{
    struct version_status_baton *sb = (version_status_baton *)baton;

    if (sb->done)
        return SVN_NO_ERROR;

    if (! status->entry)
        return SVN_NO_ERROR;

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
        && (strcmp(path, sb->wc_path) == 0)
        && (status->entry))
        sb->wc_url = apr_pstrdup(sb->pool, status->entry->url);

    return SVN_NO_ERROR;
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
    sb.pool = requestPool.pool();

    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    int wc_format;
    svn_client_ctx_t ctx = { 0 };
    SVN_JNI_ERR(svn_wc_check_wc(intPath.c_str(), &wc_format,
                                requestPool.pool()),
                NULL);

    if (! wc_format)
    {
        svn_node_kind_t kind;
        SVN_JNI_ERR(svn_io_check_path(intPath.c_str(), &kind,
                                      requestPool.pool()),
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
    ctx.config = apr_hash_make(requestPool.pool());

    /* Setup the notification and cancellation callbacks, and their
     * shared baton (which is also shared with the status function). */
    ctx.notify_func = notify;
    ctx.notify_baton = &sb;
    ctx.cancel_func = cancel;
    ctx.cancel_baton = &sb;

    svn_error_t *err;
    err = svn_client_status4(NULL, intPath.c_str(), &rev, analyze_status,
                             &sb, svn_depth_infinity, TRUE, FALSE, FALSE,
                             FALSE, NULL, &ctx, requestPool.pool());
    if (err && (err->apr_err == SVN_ERR_CANCELLED))
        svn_error_clear(err);
    else
        SVN_JNI_ERR(err, NULL);

    if ((! sb.switched ) && (trailUrl))
    {
        /* If the trailing part of the URL of the working copy directory
         * does not match the given trailing URL then the whole working
         * copy is switched. */
        if (! sb.wc_url)
        {
            sb.switched = TRUE;
        }
        else
        {
            apr_size_t len1 = strlen(trailUrl);
            apr_size_t len2 = strlen(sb.wc_url);
            if ((len1 > len2) || strcmp(sb.wc_url + len2 - len1, trailUrl))
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
                                         requestPool.pool()),
                NULL);

    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return NULL;

    SVN_JNI_ERR(svn_client_revprop_list(&props, URL, revision.revision(),
                                        &set_rev, ctx, requestPool.pool()),
                NULL);

    apr_hash_index_t *hi;

    int count = apr_hash_count(props);

    JNIEnv *env = JNIUtil::getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/PropertyData");
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    jobjectArray jprops = env->NewObjectArray(count, clazz, NULL);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    env->DeleteLocalRef(clazz);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    int i = 0;
    for (hi = apr_hash_first(requestPool.pool(), props);
         hi;
         hi = apr_hash_next(hi), ++i)
    {
        const char *key;
        svn_string_t *val;

        apr_hash_this(hi, (const void **)&key, NULL, (void**)&val);

        jobject object = CreateJ::Property(jthis, path, key, val);

        env->SetObjectArrayElement(jprops, i, object);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;

        env->DeleteLocalRef(object);
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    return jprops;
}

struct info_baton
{
    std::vector<info_entry> infoVect;
    int info_ver;
    apr_pool_t *pool;
};

/**
 * Get information about a file or directory.
 */
jobject SVNClient::info(const char *path)
{
    Pool requestPool;
    svn_wc_adm_access_t *adm_access;
    const svn_wc_entry_t *entry;

    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    Path intPath(path);
    SVN_JNI_ERR(intPath.error_occured(), NULL);

    SVN_JNI_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, intPath.c_str(),
                                       FALSE, 0, NULL, NULL,
                                       requestPool.pool()),
                NULL);
    SVN_JNI_ERR(svn_wc_entry(&entry, intPath.c_str(), adm_access, FALSE,
                             requestPool.pool()),
                NULL);

    return CreateJ::Info(entry);
}

void
SVNClient::info2(const char *path, Revision &revision, Revision &pegRevision,
                 svn_depth_t depth, StringArray &changelists,
                 InfoCallback *callback)
{
    SVN_JNI_NULL_PTR_EX(path, "path", );

    Pool requestPool;
    svn_client_ctx_t *ctx = getContext(NULL);
    if (ctx == NULL)
        return;

    Path checkedPath(path);
    SVN_JNI_ERR(checkedPath.error_occured(), );

    SVN_JNI_ERR(svn_client_info2(checkedPath.c_str(),
                                 pegRevision.revision(),
                                 revision.revision(),
                                 InfoCallback::callback,
                                 callback, depth,
                                 changelists.array(requestPool), ctx,
                                 requestPool.pool()), );
}
