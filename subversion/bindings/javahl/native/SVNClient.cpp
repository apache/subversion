/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
 * @endcopyright
 *
 * @file SVNClient.cpp
 * @brief: Implementation of the SVNClient class
 */

#include <vector>
#include <iostream>
#include <sstream>
#include <string>

#include "SVNClient.h"
#include "JNIUtil.h"
#include "CopySources.h"
#include "DiffSummaryReceiver.h"
#include "ClientContext.h"
#include "Prompter.h"
#include "RemoteSession.h"
#include "Pool.h"
#include "Targets.h"
#include "Revision.h"
#include "OutputStream.h"
#include "RevisionRange.h"
#include "VersionExtended.h"
#include "BlameCallback.h"
#include "ProplistCallback.h"
#include "LogMessageCallback.h"
#include "InfoCallback.h"
#include "PatchCallback.h"
#include "CommitCallback.h"
#include "StatusCallback.h"
#include "ChangelistCallback.h"
#include "ListCallback.h"
#include "ImportFilterCallback.h"
#include "JNIByteArray.h"
#include "CommitMessage.h"
#include "EnumMapper.h"
#include "StringArray.h"
#include "PropertyTable.h"
#include "DiffOptions.h"
#include "CreateJ.h"
#include "JNIStringHolder.h"

#include "svn_auth.h"
#include "svn_dso.h"
#include "svn_types.h"
#include "svn_client.h"
#include "svn_sorts.h"
#include "svn_time.h"
#include "svn_diff.h"
#include "svn_config.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_utf.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h"

#include "ExternalItem.hpp"
#include "jniwrapper/jni_list.hpp"
#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_string_map.hpp"


SVNClient::SVNClient(jobject jthis_in)
    : m_lastPath("", pool), context(jthis_in, pool)
{
}

SVNClient::~SVNClient()
{
}

SVNClient *SVNClient::getCppObject(jobject jthis)
{
    static jfieldID fid = 0;
    jlong cppAddr = SVNBase::findCppAddrForJObject(jthis, &fid,
                                                   JAVAHL_CLASS("/SVNClient"));
    return (cppAddr == 0 ? NULL : reinterpret_cast<SVNClient *>(cppAddr));
}

void SVNClient::dispose(jobject jthis)
{
    static jfieldID fid = 0;
    SVNBase::dispose(jthis, &fid, JAVAHL_CLASS("/SVNClient"));
}

jobject SVNClient::getVersionExtended(bool verbose)
{
    JNIEnv *const env = JNIUtil::getEnv();

    jclass clazz = env->FindClass(JAVAHL_CLASS("/types/VersionExtended"));
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    static volatile jmethodID ctor = 0;
    if (!ctor)
    {
        ctor = env->GetMethodID(clazz, "<init>", "()V");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    static volatile jfieldID fid = 0;
    if (!fid)
    {
        fid = env->GetFieldID(clazz, "cppAddr", "J");
        if (JNIUtil::isJavaExceptionThrown())
            return NULL;
    }

    jobject j_ext_info = env->NewObject(clazz, ctor);
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    VersionExtended *vx = new VersionExtended(verbose);
    env->SetLongField(j_ext_info, fid, vx->getCppAddr());

    env->DeleteLocalRef(clazz);
    return j_ext_info;
}

jstring SVNClient::getAdminDirectoryName()
{
    SVN::Pool subPool(pool);
    jstring name =
        JNIUtil::makeJString(svn_wc_get_adm_dir(subPool.getPool()));
    if (JNIUtil::isJavaExceptionThrown())
        return NULL;

    return name;
}

jboolean SVNClient::isAdminDirectory(const char *name)
{
    SVN::Pool subPool(pool);
    return svn_wc_is_adm_dir(name, subPool.getPool()) ? JNI_TRUE : JNI_FALSE;
}

const char *SVNClient::getLastPath()
{
    return m_lastPath.c_str();
}

/**
 * List directory entries of a URL.
 */
void SVNClient::list(const char *url, Revision &revision,
                     Revision &pegRevision, StringArray &patterns,
                     svn_depth_t depth, int direntFields,
                     bool fetchLocks, bool includeExternals,
                     ListCallback *callback)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_NULL_PTR_EX(url, "path or url", );

    Path urlPath(url, subPool);
    SVN_JNI_ERR(urlPath.error_occurred(), );

    SVN_JNI_ERR(svn_client_list4(urlPath.c_str(),
                                 pegRevision.revision(),
                                 revision.revision(),
                                 patterns.array(subPool),
                                 depth,
                                 direntFields,
                                 fetchLocks,
                                 includeExternals,
                                 ListCallback::callback,
                                 callback,
                                 ctx, subPool.getPool()), );
}

void
SVNClient::status(const char *path, svn_depth_t depth,
                  bool onServer, bool onDisk, bool getAll,
                  bool noIgnore, bool ignoreExternals,
                  bool depthAsSticky, StringArray &changelists,
                  StatusCallback *callback)
{
    SVN::Pool subPool(pool);
    svn_revnum_t youngest = SVN_INVALID_REVNUM;
    svn_opt_revision_t rev;

    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;
    callback->setWcCtx(ctx->wc_ctx);

    Path checkedPath(path, subPool);
    SVN_JNI_ERR(checkedPath.error_occurred(), );

    rev.kind = svn_opt_revision_unspecified;

    SVN_JNI_ERR(svn_client_status6(&youngest, ctx, checkedPath.c_str(),
                                   &rev, depth,
                                   getAll, onServer, onDisk,
                                   noIgnore, ignoreExternals, depthAsSticky,
                                   changelists.array(subPool),
                                   StatusCallback::callback, callback,
                                   subPool.getPool()), );
}

/* Convert a vector of revision ranges to an APR array of same. */
static apr_array_header_t *
rev_range_vector_to_apr_array(std::vector<RevisionRange> &revRanges,
                              SVN::Pool &subPool)
{
    apr_array_header_t *ranges =
      apr_array_make(subPool.getPool(),
                     static_cast<int>(revRanges.size()),
                     sizeof(svn_opt_revision_range_t *));

    std::vector<RevisionRange>::const_iterator it;
    for (it = revRanges.begin(); it != revRanges.end(); ++it)
    {
        const svn_opt_revision_range_t *range = it->toRange(subPool);

        if (range->start.kind == svn_opt_revision_unspecified
            && range->end.kind == svn_opt_revision_unspecified)
        {
            svn_opt_revision_range_t *full =
                reinterpret_cast<svn_opt_revision_range_t *>
                    (apr_pcalloc(subPool.getPool(), sizeof(*range)));
            full->start.kind = svn_opt_revision_number;
            full->start.value.number = 1;
            full->end.kind = svn_opt_revision_head;
            full->end.value.number = 0;
            APR_ARRAY_PUSH(ranges, const svn_opt_revision_range_t *) = full;
        }
        else
        {
            APR_ARRAY_PUSH(ranges, const svn_opt_revision_range_t *) = range;
        }
        if (JNIUtil::isExceptionThrown())
            return NULL;
    }
    return ranges;
}

void SVNClient::logMessages(const char *path, Revision &pegRevision,
                            std::vector<RevisionRange> &logRanges,
                            bool stopOnCopy, bool discoverPaths,
                            bool includeMergedRevisions,
                            StringArray &revProps, bool allRevProps,
                            int limit, LogMessageCallback *callback)
{
    SVN::Pool subPool(pool);

    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Targets target(path, subPool);
    const apr_array_header_t *targets = target.array(subPool);
    SVN_JNI_ERR(target.error_occurred(), );

    apr_array_header_t *ranges =
      rev_range_vector_to_apr_array(logRanges, subPool);
    if (JNIUtil::isExceptionThrown())
        return;

    const apr_array_header_t *revprops = NULL;
    if (!allRevProps)
      revprops = revProps.array(subPool);

    SVN_JNI_ERR(svn_client_log5(targets, pegRevision.revision(), ranges,
                                limit, discoverPaths, stopOnCopy,
                                includeMergedRevisions, revprops,
                                LogMessageCallback::callback, callback, ctx,
                                subPool.getPool()), );
}

jlong SVNClient::checkout(const char *moduleName, const char *destPath,
                          Revision &revision, Revision &pegRevision,
                          svn_depth_t depth, bool ignoreExternals,
                          bool allowUnverObstructions)
{
    SVN::Pool subPool;

    SVN_JNI_NULL_PTR_EX(moduleName, "moduleName", -1);
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", -1);

    Path url(moduleName, subPool);
    Path path(destPath, subPool);
    SVN_JNI_ERR(url.error_occurred(), -1);
    SVN_JNI_ERR(path.error_occurred(), -1);
    svn_revnum_t rev;

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
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
                                     subPool.getPool()),
                -1);

    return rev;
}

void SVNClient::remove(Targets &targets, CommitMessage *message, bool force,
                       bool keep_local, PropertyTable &revprops,
                       CommitCallback *callback)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(message, subPool);
    if (ctx == NULL)
        return;

    const apr_array_header_t *targets2 = targets.array(subPool);
    SVN_JNI_ERR(targets.error_occurred(), );

    SVN_JNI_ERR(svn_client_delete4(targets2, force, keep_local,
                                   revprops.hash(subPool),
                                   CommitCallback::callback, callback,
                                   ctx, subPool.getPool()), );
}

void SVNClient::revert(StringArray &paths, svn_depth_t depth,
                       StringArray &changelists,
                       bool clear_changelists,
                       bool metadata_only)
{
    SVN::Pool subPool(pool);

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Targets targets(paths, subPool);
    SVN_JNI_ERR(targets.error_occurred(), );
    SVN_JNI_ERR(svn_client_revert3(targets.array(subPool), depth,
                                   changelists.array(subPool),
                                   clear_changelists,
                                   metadata_only,
                                   ctx, subPool.getPool()), );
}

void SVNClient::add(const char *path,
                    svn_depth_t depth, bool force,
                    bool no_ignore, bool no_autoprops,
                    bool add_parents)
{
    SVN::Pool subPool(pool);

    SVN_JNI_NULL_PTR_EX(path, "path", );

    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_add5(intPath.c_str(), depth, force,
                                no_ignore, no_autoprops, add_parents, ctx,
                                subPool.getPool()), );
}

jlongArray SVNClient::update(Targets &targets, Revision &revision,
                             svn_depth_t depth, bool depthIsSticky,
                             bool makeParents, bool ignoreExternals,
                             bool allowUnverObstructions)
{
    SVN::Pool subPool(pool);

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    apr_array_header_t *revs;
    if (ctx == NULL)
        return NULL;

    const apr_array_header_t *array = targets.array(subPool);
    SVN_JNI_ERR(targets.error_occurred(), NULL);
    SVN_JNI_ERR(svn_client_update4(&revs, array,
                                   revision.revision(),
                                   depth,
                                   depthIsSticky,
                                   ignoreExternals,
                                   allowUnverObstructions,
                                   TRUE /* adds_as_modification */,
                                   makeParents,
                                   ctx, subPool.getPool()),
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

void SVNClient::commit(Targets &targets, CommitMessage *message,
                       svn_depth_t depth, bool noUnlock, bool keepChangelist,
                       StringArray &changelists, PropertyTable &revprops,
                       CommitCallback *callback)
{
    SVN::Pool subPool(pool);
    const apr_array_header_t *targets2 = targets.array(subPool);
    SVN_JNI_ERR(targets.error_occurred(), );
    svn_client_ctx_t *ctx = context.getContext(message, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_commit6(targets2, depth,
                                   noUnlock, keepChangelist,
                                   TRUE,
                                   FALSE, // include_file_externals
                                   FALSE, // include_dir_externals
                                   changelists.array(subPool),
                                   revprops.hash(subPool),
                                   CommitCallback::callback, callback,
                                   ctx, subPool.getPool()),
                );
}


namespace {
typedef Java::ImmutableList<JavaHL::ExternalItem> PinList;
typedef Java::ImmutableMap<PinList> PinMap;

struct PinListFunctor
{
  explicit PinListFunctor(const Java::Env& env, SVN::Pool& pool, int refs_len)
    : m_pool(pool),
      m_refs(apr_array_make(pool.getPool(), refs_len,
                            sizeof(svn_wc_external_item2_t*)))
      {}

  void operator()(const JavaHL::ExternalItem& item)
    {
      APR_ARRAY_PUSH(m_refs, svn_wc_external_item2_t*) =
        item.get_external_item(m_pool);
    }

  SVN::Pool& m_pool;
  apr_array_header_t *m_refs;
};

struct PinMapFunctor
{
  explicit PinMapFunctor(const Java::Env& env, SVN::Pool& pool)
    : m_env(env),
      m_pool(pool),
      m_pin_set(svn_hash__make(pool.getPool()))
    {}

  void operator()(const std::string& path, const PinList& refs)
    {
      PinListFunctor lf(m_env, m_pool, refs.length());
      refs.for_each(lf);
      const char* key = static_cast<const char*>(
          apr_pmemdup(m_pool.getPool(), path.c_str(), path.size() + 1));
      svn_hash_sets(m_pin_set, key, lf.m_refs);
    }

  const Java::Env& m_env;
  SVN::Pool& m_pool;
  apr_hash_t *m_pin_set;
};

apr_hash_t *get_externals_to_pin(jobject jexternalsToPin, SVN::Pool& pool)
{
  if (!jexternalsToPin)
    return NULL;

  const Java::Env env;
  JNIEnv *jenv = env.get();

  try
    {
      PinMap pin_map(env, jexternalsToPin);
      PinMapFunctor mf(env, pool);
      pin_map.for_each(mf);
      return mf.m_pin_set;
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}
} // anonymous namespace

void SVNClient::copy(CopySources &copySources, const char *destPath,
                     CommitMessage *message, bool copyAsChild,
                     bool makeParents, bool ignoreExternals,
                     bool metadataOnly,
                     bool pinExternals, jobject jexternalsToPin,
                     PropertyTable &revprops, CommitCallback *callback)
{
    SVN::Pool subPool(pool);

    apr_array_header_t *srcs = copySources.array(subPool);
    SVN_JNI_NULL_PTR_EX(srcs, "sources", );
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", );
    Path destinationPath(destPath, subPool);
    SVN_JNI_ERR(destinationPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(message, subPool);
    if (ctx == NULL)
        return;

    apr_hash_t *pin_set = get_externals_to_pin(jexternalsToPin, subPool);
    if (!JNIUtil::isJavaExceptionThrown())
      SVN_JNI_ERR(svn_client_copy7(srcs, destinationPath.c_str(),
                                   copyAsChild, makeParents, ignoreExternals,
                                   metadataOnly,
                                   pinExternals, pin_set,
                                   revprops.hash(subPool),
                                   CommitCallback::callback, callback,
                                 ctx, subPool.getPool()), );
}

void SVNClient::move(Targets &srcPaths, const char *destPath,
                     CommitMessage *message, bool force, bool moveAsChild,
                     bool makeParents, bool metadataOnly, bool allowMixRev,
                     PropertyTable &revprops, CommitCallback *callback)
{
    SVN::Pool subPool(pool);

    const apr_array_header_t *srcs = srcPaths.array(subPool);
    SVN_JNI_ERR(srcPaths.error_occurred(), );
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", );
    Path destinationPath(destPath, subPool);
    SVN_JNI_ERR(destinationPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(message, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_move7((apr_array_header_t *) srcs,
                                 destinationPath.c_str(), moveAsChild,
                                 makeParents,
                                 allowMixRev,
                                 metadataOnly,
                                 revprops.hash(subPool),
                                 CommitCallback::callback, callback, ctx,
                                 subPool.getPool()), );
}

void SVNClient::mkdir(Targets &targets, CommitMessage *message,
                      bool makeParents, PropertyTable &revprops,
                      CommitCallback *callback)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(message, subPool);
    if (ctx == NULL)
        return;

    const apr_array_header_t *targets2 = targets.array(subPool);
    SVN_JNI_ERR(targets.error_occurred(), );

    SVN_JNI_ERR(svn_client_mkdir4(targets2, makeParents,
                                  revprops.hash(subPool),
                                  CommitCallback::callback, callback,
                                  ctx, subPool.getPool()), );
}

void SVNClient::cleanup(const char *path,
                        bool break_locks,
                        bool fix_recorded_timestamps,
                        bool clear_dav_cache,
                        bool remove_unused_pristines,
                        bool include_externals)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_cleanup2(intPath.c_str(),
                                    break_locks,
                                    fix_recorded_timestamps,
                                    clear_dav_cache,
                                    remove_unused_pristines,
                                    include_externals,
                                    ctx, subPool.getPool()),);
}

void SVNClient::resolve(const char *path, svn_depth_t depth,
                        svn_wc_conflict_choice_t choice)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_resolve(intPath.c_str(), depth, choice,
                                   ctx, subPool.getPool()), );
}

jlong SVNClient::doExport(const char *srcPath, const char *destPath,
                          Revision &revision, Revision &pegRevision,
                          bool force, bool ignoreExternals,
                          bool ignoreKeywords,
                          svn_depth_t depth, const char *nativeEOL)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(srcPath, "srcPath", -1);
    SVN_JNI_NULL_PTR_EX(destPath, "destPath", -1);
    Path sourcePath(srcPath, subPool);
    SVN_JNI_ERR(sourcePath.error_occurred(), -1);
    Path destinationPath(destPath, subPool);
    SVN_JNI_ERR(destinationPath.error_occurred(), -1);
    svn_revnum_t rev;
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_export5(&rev, sourcePath.c_str(),
                                   destinationPath.c_str(),
                                   pegRevision.revision(),
                                   revision.revision(), force,
                                   ignoreExternals, ignoreKeywords,
                                   depth,
                                   nativeEOL, ctx,
                                   subPool.getPool()),
                -1);

    return rev;

}

jlong SVNClient::doSwitch(const char *path, const char *url,
                          Revision &revision, Revision &pegRevision,
                          svn_depth_t depth, bool depthIsSticky,
                          bool ignoreExternals,
                          bool allowUnverObstructions,
                          bool ignoreAncestry)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", -1);
    SVN_JNI_NULL_PTR_EX(url, "url", -1);
    Path intUrl(url, subPool);
    SVN_JNI_ERR(intUrl.error_occurred(), -1);
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), -1);

    svn_revnum_t rev;
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return -1;

    SVN_JNI_ERR(svn_client_switch3(&rev, intPath.c_str(),
                                   intUrl.c_str(),
                                   pegRevision.revision(),
                                   revision.revision(),
                                   depth,
                                   depthIsSticky,
                                   ignoreExternals,
                                   allowUnverObstructions,
                                   ignoreAncestry,
                                   ctx,
                                   subPool.getPool()),
                -1);

    return rev;
}

void SVNClient::doImport(const char *path, const char *url,
                         CommitMessage *message, svn_depth_t depth,
                         bool noIgnore, bool noAutoProps,
                         bool ignoreUnknownNodeTypes,
                         PropertyTable &revprops,
                         ImportFilterCallback *ifCallback,
                         CommitCallback *commitCallback)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(url, "url", );
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );
    Path intUrl(url, subPool);
    SVN_JNI_ERR(intUrl.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(message, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_import5(intPath.c_str(), intUrl.c_str(), depth,
                                   noIgnore, noAutoProps, ignoreUnknownNodeTypes,
                                   revprops.hash(subPool),
                                   ImportFilterCallback::callback, ifCallback,
                                   CommitCallback::callback, commitCallback,
                                   ctx, subPool.getPool()), );
}

jobject
SVNClient::suggestMergeSources(const char *path, Revision &pegRevision)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return NULL;

    apr_array_header_t *sources;
    SVN_JNI_ERR(svn_client_suggest_merge_sources(&sources, path,
                                                 pegRevision.revision(),
                                                 ctx, subPool.getPool()),
                NULL);

    return CreateJ::StringSet(sources);
}

void SVNClient::merge(const char *path1, Revision &revision1,
                      const char *path2, Revision &revision2,
                      const char *localPath, bool forceDelete, svn_depth_t depth,
                      bool ignoreMergeinfo, bool diffIgnoreAncestry,
                      bool dryRun, bool allowMixedRev, bool recordOnly)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path1, "path1", );
    SVN_JNI_NULL_PTR_EX(path2, "path2", );
    SVN_JNI_NULL_PTR_EX(localPath, "localPath", );
    Path intLocalPath(localPath, subPool);
    SVN_JNI_ERR(intLocalPath.error_occurred(), );

    Path srcPath1(path1, subPool);
    SVN_JNI_ERR(srcPath1.error_occurred(), );

    Path srcPath2(path2, subPool);
    SVN_JNI_ERR(srcPath2.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_merge5(srcPath1.c_str(), revision1.revision(),
                                  srcPath2.c_str(), revision2.revision(),
                                  intLocalPath.c_str(),
                                  depth,
                                  ignoreMergeinfo, diffIgnoreAncestry,
                                  forceDelete, recordOnly, dryRun,
                                  allowMixedRev, NULL, ctx,
                                  subPool.getPool()), );
}

void SVNClient::merge(const char *path, Revision &pegRevision,
                      std::vector<RevisionRange> *rangesToMerge,
                      const char *localPath, bool forceDelete, svn_depth_t depth,
                      bool ignoreMergeinfo, bool diffIgnoreAncestry,
                      bool dryRun, bool allowMixedRev, bool recordOnly)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(localPath, "localPath", );
    Path intLocalPath(localPath, subPool);
    SVN_JNI_ERR(intLocalPath.error_occurred(), );

    Path srcPath(path, subPool);
    SVN_JNI_ERR(srcPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    apr_array_header_t *ranges =
      (!rangesToMerge ? NULL
       : rev_range_vector_to_apr_array(*rangesToMerge, subPool));
    if (JNIUtil::isExceptionThrown())
        return;

    SVN_JNI_ERR(svn_client_merge_peg5(srcPath.c_str(),
                                      ranges,
                                      pegRevision.revision(),
                                      intLocalPath.c_str(),
                                      depth,
                                      ignoreMergeinfo, diffIgnoreAncestry,
                                      forceDelete, recordOnly,
                                      dryRun, allowMixedRev, NULL, ctx,
                                      subPool.getPool()), );
}

/* SVNClient::mergeReintegrate is implemented in deprecated.cpp. */

jobject
SVNClient::getMergeinfo(const char *target, Revision &pegRevision)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return NULL;

    svn_mergeinfo_t mergeinfo;
    Path intLocalTarget(target, subPool);
    SVN_JNI_ERR(intLocalTarget.error_occurred(), NULL);
    SVN_JNI_ERR(svn_client_mergeinfo_get_merged(&mergeinfo,
                                                intLocalTarget.c_str(),
                                                pegRevision.revision(), ctx,
                                                subPool.getPool()),
                NULL);
    if (mergeinfo == NULL)
        return NULL;
    return CreateJ::Mergeinfo(mergeinfo, subPool.getPool());
}

void SVNClient::getMergeinfoLog(int type, const char *pathOrURL,
                                Revision &pegRevision,
                                const char *mergeSourceURL,
                                Revision &srcPegRevision,
                                Revision &srcStartRevision,
                                Revision &srcEndRevision,
                                bool discoverChangedPaths,
                                svn_depth_t depth,
                                StringArray &revProps,
                                LogMessageCallback *callback)
{
    SVN::Pool subPool(pool);

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_NULL_PTR_EX(pathOrURL, "path or url", );
    Path urlPath(pathOrURL, subPool);
    SVN_JNI_ERR(urlPath.error_occurred(), );

    SVN_JNI_NULL_PTR_EX(mergeSourceURL, "merge source url", );
    Path srcURL(mergeSourceURL, subPool);
    SVN_JNI_ERR(srcURL.error_occurred(), );

    SVN_JNI_ERR(svn_client_mergeinfo_log2((type == 1),
                                          urlPath.c_str(),
                                          pegRevision.revision(),
                                          srcURL.c_str(),
                                          srcPegRevision.revision(),
                                          srcStartRevision.revision(),
                                          srcEndRevision.revision(),
                                          LogMessageCallback::callback,
                                          callback,
                                          discoverChangedPaths,
                                          depth,
                                          revProps.array(subPool),
                                          ctx,
                                          subPool.getPool()), );

    return;
}

/**
 * Get a property.
 */
jbyteArray SVNClient::propertyGet(const char *path, const char *name,
                                  Revision &revision, Revision &pegRevision,
                                  StringArray &changelists)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    SVN_JNI_NULL_PTR_EX(name, "name", NULL);
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), NULL);

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return NULL;

    apr_hash_t *props;
    SVN_JNI_ERR(svn_client_propget5(&props, NULL, name,
                                    intPath.c_str(), pegRevision.revision(),
                                    revision.revision(), NULL, svn_depth_empty,
                                    changelists.array(subPool), ctx,
                                    subPool.getPool(), subPool.getPool()),
                NULL);

    apr_hash_index_t *hi;
    // only one element since we disabled recurse
    hi = apr_hash_first(subPool.getPool(), props);
    if (hi == NULL)
        return NULL; // no property with this name

    svn_string_t *propval;
    apr_hash_this(hi, NULL, NULL, reinterpret_cast<void**>(&propval));

    if (propval == NULL)
        return NULL;

    return JNIUtil::makeJByteArray(propval);
}

void SVNClient::properties(const char *path, Revision &revision,
                           Revision &pegRevision, svn_depth_t depth,
                           StringArray &changelists,
                           ProplistCallback *callback)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_proplist4(intPath.c_str(), pegRevision.revision(),
                                     revision.revision(), depth,
                                     changelists.array(subPool),
                                     callback->inherited(),
                                     ProplistCallback::callback, callback,
                                     ctx, subPool.getPool()), );
}

void SVNClient::propertySetLocal(Targets &targets, const char *name,
                                 JNIByteArray &value, svn_depth_t depth,
                                 StringArray &changelists, bool force)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(name, "name", );

    svn_string_t *val;
    if (value.isNull())
      val = NULL;
    else
      val = svn_string_ncreate
               (reinterpret_cast<const char *>(value.getBytes()),
                value.getLength(),
                subPool.getPool());

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    const apr_array_header_t *targetsApr = targets.array(subPool);
    SVN_JNI_ERR(svn_client_propset_local(name, val, targetsApr,
                                         depth, force,
                                         changelists.array(subPool),
                                         ctx, subPool.getPool()), );
}

void SVNClient::propertySetRemote(const char *path, long base_rev,
                                  const char *name,
                                  CommitMessage *message,
                                  JNIByteArray &value, bool force,
                                  PropertyTable &revprops,
                                  CommitCallback *callback)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(name, "name", );

    svn_string_t *val;
    if (value.isNull())
      val = NULL;
    else
      val = svn_string_ncreate
               (reinterpret_cast<const char *>(value.getBytes()),
                value.getLength(),
                subPool.getPool());

    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(message, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_propset_remote(name, val, intPath.c_str(),
                                          force, base_rev,
                                          revprops.hash(subPool),
                                          CommitCallback::callback, callback,
                                          ctx, subPool.getPool()), );
}

void SVNClient::diff(const char *target1, Revision &revision1,
                     const char *target2, Revision &revision2,
                     Revision *pegRevision, const char *relativeToDir,
                     OutputStream &outputStream, svn_depth_t depth,
                     StringArray &changelists,
                     bool ignoreAncestry, bool noDiffDelete, bool force,
                     bool showCopiesAsAdds, bool ignoreProps, bool propsOnly,
                     DiffOptions const& options)
{
    SVN::Pool subPool(pool);
    const char *c_relToDir = relativeToDir ?
      svn_dirent_canonicalize(relativeToDir, subPool.getPool()) :
      relativeToDir;
    bool noDiffAdded = false; /* ### Promote to argument */

    SVN_JNI_NULL_PTR_EX(target1, "target", );
    // target2 is ignored when pegRevision is provided.
    if (pegRevision == NULL)
        SVN_JNI_NULL_PTR_EX(target2, "target2", );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Path path1(target1, subPool);
    SVN_JNI_ERR(path1.error_occurred(), );

    apr_array_header_t *diffOptions = options.optionsArray(subPool);

    if (pegRevision)
    {
        SVN_JNI_ERR(svn_client_diff_peg6(diffOptions,
                                   path1.c_str(),
                                   pegRevision->revision(),
                                   revision1.revision(),
                                   revision2.revision(),
                                   c_relToDir,
                                   depth,
                                   ignoreAncestry,
                                   noDiffAdded,
                                   noDiffDelete,
                                   showCopiesAsAdds,
                                   force,
                                   ignoreProps,
                                   propsOnly,
                                   options.useGitDiffFormat(),
                                   SVN_APR_LOCALE_CHARSET,
                                   outputStream.getStream(subPool),
                                   NULL /* error file */,
                                   changelists.array(subPool),
                                   ctx,
                                   subPool.getPool()),
                    );
    }
    else
    {
        // "Regular" diff (without a peg revision).
        Path path2(target2, subPool);
        SVN_JNI_ERR(path2.error_occurred(), );

        SVN_JNI_ERR(svn_client_diff6(diffOptions,
                               path1.c_str(),
                               revision1.revision(),
                               path2.c_str(),
                               revision2.revision(),
                               c_relToDir,
                               depth,
                               ignoreAncestry,
                               noDiffAdded,
                               noDiffDelete,
                               showCopiesAsAdds,
                               force,
                               ignoreProps,
                               propsOnly,
                               options.useGitDiffFormat(),
                               SVN_APR_LOCALE_CHARSET,
                               outputStream.getStream(subPool),
                               NULL /* error stream */,
                               changelists.array(subPool),
                               ctx,
                               subPool.getPool()),
                    );
    }
}

void SVNClient::diff(const char *target1, Revision &revision1,
                     const char *target2, Revision &revision2,
                     const char *relativeToDir, OutputStream &outputStream,
                     svn_depth_t depth, StringArray &changelists,
                     bool ignoreAncestry, bool noDiffDelete, bool force,
                     bool showCopiesAsAdds, bool ignoreProps, bool propsOnly,
                     DiffOptions const& options)
{
    diff(target1, revision1, target2, revision2, NULL, relativeToDir,
         outputStream, depth, changelists, ignoreAncestry, noDiffDelete, force,
         showCopiesAsAdds, ignoreProps, propsOnly, options);
}

void SVNClient::diff(const char *target, Revision &pegRevision,
                     Revision &startRevision, Revision &endRevision,
                     const char *relativeToDir, OutputStream &outputStream,
                     svn_depth_t depth, StringArray &changelists,
                     bool ignoreAncestry, bool noDiffDelete, bool force,
                     bool showCopiesAsAdds, bool ignoreProps, bool propsOnly,
                     DiffOptions const& options)
{
    diff(target, startRevision, NULL, endRevision, &pegRevision,
         relativeToDir, outputStream, depth, changelists,
         ignoreAncestry, noDiffDelete, force, showCopiesAsAdds,
         ignoreProps, propsOnly, options);
}

void
SVNClient::diffSummarize(const char *target1, Revision &revision1,
                         const char *target2, Revision &revision2,
                         svn_depth_t depth, StringArray &changelists,
                         bool ignoreAncestry,
                         DiffSummaryReceiver &receiver)
{
    SVN::Pool subPool(pool);

    SVN_JNI_NULL_PTR_EX(target1, "target1", );
    SVN_JNI_NULL_PTR_EX(target2, "target2", );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Path path1(target1, subPool);
    SVN_JNI_ERR(path1.error_occurred(), );
    Path path2(target2, subPool);
    SVN_JNI_ERR(path2.error_occurred(), );

    SVN_JNI_ERR(svn_client_diff_summarize2(path1.c_str(), revision1.revision(),
                                           path2.c_str(), revision2.revision(),
                                           depth,
                                           ignoreAncestry,
                                           changelists.array(subPool),
                                           DiffSummaryReceiver::summarize,
                                           &receiver,
                                           ctx, subPool.getPool()), );
}

void
SVNClient::diffSummarize(const char *target, Revision &pegRevision,
                         Revision &startRevision, Revision &endRevision,
                         svn_depth_t depth, StringArray &changelists,
                         bool ignoreAncestry, DiffSummaryReceiver &receiver)
{
    SVN::Pool subPool(pool);

    SVN_JNI_NULL_PTR_EX(target, "target", );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Path path(target, subPool);
    SVN_JNI_ERR(path.error_occurred(), );

    SVN_JNI_ERR(svn_client_diff_summarize_peg2(path.c_str(),
                                               pegRevision.revision(),
                                               startRevision.revision(),
                                               endRevision.revision(),
                                               depth,
                                               ignoreAncestry,
                                               changelists.array(subPool),
                                               DiffSummaryReceiver::summarize,
                                               &receiver, ctx,
                                               subPool.getPool()), );
}

apr_hash_t *SVNClient::streamFileContent(const char *path,
                                         Revision &revision,
                                         Revision &pegRevision,
                                         bool expand_keywords,
                                         bool return_props,
                                         OutputStream &outputStream)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), NULL);

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return NULL;

    apr_hash_t *props = NULL;
    SVN_JNI_ERR(svn_client_cat3((return_props ? &props : NULL),
                                outputStream.getStream(subPool),
                                intPath.c_str(),
                                pegRevision.revision(), revision.revision(),
                                expand_keywords, ctx,
                                subPool.getPool(), subPool.getPool()),
        NULL);
    return props;
}

jbyteArray SVNClient::revProperty(const char *path,
                                  const char *name, Revision &rev)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    SVN_JNI_NULL_PTR_EX(name, "name", NULL);
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), NULL);

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return NULL;

    const char *URL;
    svn_string_t *propval;
    svn_revnum_t set_rev;
    SVN_JNI_ERR(svn_client_url_from_path2(&URL, intPath.c_str(), ctx,
                                          subPool.getPool(),
                                          subPool.getPool()),
                NULL);

    if (URL == NULL)
    {
        SVN_JNI_ERR(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                     _("Either a URL or versioned item is required.")),
                    NULL);
    }

    SVN_JNI_ERR(svn_client_revprop_get(name, &propval, URL,
                                       rev.revision(), &set_rev, ctx,
                                       subPool.getPool()),
                NULL);
    if (propval == NULL)
        return NULL;

    return JNIUtil::makeJByteArray(propval);
}
void SVNClient::relocate(const char *from, const char *to, const char *path,
                         bool ignoreExternals)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(from, "from", );
    SVN_JNI_NULL_PTR_EX(to, "to", );
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );

    Path intFrom(from, subPool);
    SVN_JNI_ERR(intFrom.error_occurred(), );

    Path intTo(to, subPool);
    SVN_JNI_ERR(intTo.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_relocate2(intPath.c_str(), intFrom.c_str(),
                                     intTo.c_str(), ignoreExternals, ctx,
                                     subPool.getPool()), );
}

void SVNClient::blame(const char *path, Revision &pegRevision,
                      Revision &revisionStart, Revision &revisionEnd,
                      bool ignoreMimeType, bool includeMergedRevisions,
                      BlameCallback *callback, DiffOptions const& options)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    SVN_JNI_ERR(svn_client_blame5(
          intPath.c_str(), pegRevision.revision(), revisionStart.revision(),
          revisionEnd.revision(),
          options.fileOptions(subPool), ignoreMimeType,
          includeMergedRevisions, BlameCallback::callback, callback, ctx,
          subPool.getPool()),
        );
}

void SVNClient::addToChangelist(Targets &srcPaths, const char *changelist,
                                svn_depth_t depth, StringArray &changelists)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);

    const apr_array_header_t *srcs = srcPaths.array(subPool);
    SVN_JNI_ERR(srcPaths.error_occurred(), );

    SVN_JNI_ERR(svn_client_add_to_changelist(srcs, changelist, depth,
                                             changelists.array(subPool),
                                             ctx, subPool.getPool()), );
}

void SVNClient::removeFromChangelists(Targets &srcPaths, svn_depth_t depth,
                                      StringArray &changelists)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);

    const apr_array_header_t *srcs = srcPaths.array(subPool);
    SVN_JNI_ERR(srcPaths.error_occurred(), );

    SVN_JNI_ERR(svn_client_remove_from_changelists(srcs, depth,
                                                changelists.array(subPool),
                                                ctx, subPool.getPool()), );
}

void SVNClient::getChangelists(const char *rootPath,
                               StringArray *changelists,
                               svn_depth_t depth,
                               ChangelistCallback *callback)
{
    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);

    const apr_array_header_t *cl_array = (!changelists ? NULL
                                          : changelists->array(subPool));

    SVN_JNI_ERR(svn_client_get_changelists(rootPath, cl_array,
                                           depth, ChangelistCallback::callback,
                                           callback, ctx, subPool.getPool()),
                );
}

void SVNClient::lock(Targets &targets, const char *comment, bool force)
{
    SVN::Pool subPool(pool);
    const apr_array_header_t *targetsApr = targets.array(subPool);
    SVN_JNI_ERR(targets.error_occurred(), );
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);

    SVN_JNI_ERR(svn_client_lock(targetsApr, comment, force, ctx,
                                subPool.getPool()), );
}

void SVNClient::unlock(Targets &targets, bool force)
{
    SVN::Pool subPool(pool);

    const apr_array_header_t *targetsApr = targets.array(subPool);
    SVN_JNI_ERR(targets.error_occurred(), );
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    SVN_JNI_ERR(svn_client_unlock(
        targetsApr, force, ctx, subPool.getPool()), );
}
void SVNClient::setRevProperty(const char *path,
                               const char *name, Revision &rev,
                               const char *value, const char *original_value,
                               bool force)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );
    SVN_JNI_NULL_PTR_EX(name, "name", );
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    const char *URL;
    SVN_JNI_ERR(svn_client_url_from_path2(&URL, intPath.c_str(), ctx,
                                          subPool.getPool(),
                                          subPool.getPool()), );

    if (URL == NULL)
    {
        SVN_JNI_ERR(svn_error_create(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                     _("Either a URL or versioned item is required.")),
            );
    }

    svn_string_t *val = svn_string_create(value, subPool.getPool());
    svn_string_t *orig_val;
    if (original_value != NULL)
      orig_val = svn_string_create(original_value, subPool.getPool());
    else
      orig_val = NULL;

    svn_revnum_t set_revision;
    SVN_JNI_ERR(svn_client_revprop_set2(name, val, orig_val, URL, rev.revision(),
                                        &set_revision, force, ctx,
                                        subPool.getPool()), );
}

jstring SVNClient::getVersionInfo(const char *path, const char *trailUrl,
                                  bool lastChanged)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);

    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), NULL);

    int wc_format;
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return NULL;
    SVN_JNI_ERR(svn_wc_check_wc2(&wc_format, ctx->wc_ctx, intPath.c_str(),
                                 subPool.getPool()),
                NULL);

    if (! wc_format)
    {
        svn_node_kind_t kind;
        SVN_JNI_ERR(svn_io_check_path(intPath.c_str(), &kind,
                                      subPool.getPool()),
                    NULL);
        if (kind == svn_node_dir)
        {
            return JNIUtil::makeJString("exported");
        }
        else
        {
            char buffer[2048];
            apr_snprintf(buffer, sizeof(buffer),
                         _("'%s' not versioned, and not exported\n"), path);
            return JNIUtil::makeJString(buffer);
        }
    }

    svn_wc_revision_status_t *result;
    const char *local_abspath;

    SVN_JNI_ERR(svn_dirent_get_absolute(&local_abspath, intPath.c_str(),
                                        subPool.getPool()), NULL);
    SVN_JNI_ERR(svn_wc_revision_status2(&result, ctx->wc_ctx, local_abspath,
                                        trailUrl, lastChanged,
                                        ctx->cancel_func, ctx->cancel_baton,
                                        subPool.getPool(),
                                        subPool.getPool()), NULL);

    std::ostringstream value;
    value << result->min_rev;
    if (result->min_rev != result->max_rev)
    {
        value << ":";
        value << result->max_rev;
    }
    if (result->modified)
        value << "M";
    if (result->switched)
        value << "S";
    if (result->sparse_checkout)
        value << "P";

    return JNIUtil::makeJString(value.str().c_str());
}

void SVNClient::upgrade(const char *path)
{
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", );

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Path checkedPath(path, subPool);
    SVN_JNI_ERR(checkedPath.error_occurred(), );

    SVN_JNI_ERR(svn_client_upgrade(path, ctx, subPool.getPool()), );
}

jobject SVNClient::revProperties(const char *path, Revision &revision)
{
    apr_hash_t *props;
    SVN::Pool subPool(pool);
    SVN_JNI_NULL_PTR_EX(path, "path", NULL);
    Path intPath(path, subPool);
    SVN_JNI_ERR(intPath.error_occurred(), NULL);

    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    const char *URL;
    svn_revnum_t set_rev;
    SVN_JNI_ERR(svn_client_url_from_path2(&URL, intPath.c_str(), ctx,
                                          subPool.getPool(),
                                          subPool.getPool()),
                NULL);

    if (ctx == NULL)
        return NULL;

    SVN_JNI_ERR(svn_client_revprop_list(&props, URL, revision.revision(),
                                        &set_rev, ctx, subPool.getPool()),
                NULL);

    return CreateJ::PropertyMap(props, subPool.getPool());
}

void
SVNClient::info(const char *path,
                Revision &revision, Revision &pegRevision, svn_depth_t depth,
                svn_boolean_t fetchExcluded, svn_boolean_t fetchActualOnly,
                svn_boolean_t includeExternals,
                 StringArray &changelists, InfoCallback *callback)
{
    SVN_JNI_NULL_PTR_EX(path, "path", );

    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Path checkedPath(path, subPool);
    SVN_JNI_ERR(checkedPath.error_occurred(), );

    SVN_JNI_ERR(svn_client_info4(checkedPath.c_str(),
                                 pegRevision.revision(),
                                 revision.revision(), depth,
                                 fetchExcluded, fetchActualOnly,
                                 includeExternals,
                                 changelists.array(subPool),
                                 InfoCallback::callback, callback,
                                 ctx, subPool.getPool()), );
}

void
SVNClient::patch(const char *patchPath, const char *targetPath, bool dryRun,
                 int stripCount, bool reverse, bool ignoreWhitespace,
                 bool removeTempfiles, PatchCallback *callback)
{
    SVN_JNI_NULL_PTR_EX(patchPath, "patchPath", );
    SVN_JNI_NULL_PTR_EX(targetPath, "targetPath", );

    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Path checkedPatchPath(patchPath, subPool);
    SVN_JNI_ERR(checkedPatchPath.error_occurred(), );
    Path checkedTargetPath(targetPath, subPool);
    SVN_JNI_ERR(checkedTargetPath.error_occurred(), );

    // Should parameterize the following, instead of defaulting to FALSE
    SVN_JNI_ERR(svn_client_patch(checkedPatchPath.c_str(),
                                 checkedTargetPath.c_str(),
                                 dryRun, stripCount, reverse,
                                 ignoreWhitespace, removeTempfiles,
                                 PatchCallback::callback, callback,
                                 ctx, subPool.getPool()), );
}

void SVNClient::vacuum(const char *path,
                       bool remove_unversioned_items,
                       bool remove_ignored_items,
                       bool fix_recorded_timestamps,
                       bool remove_unused_pristines,
                       bool include_externals)
{
    SVN_JNI_NULL_PTR_EX(path, "path", );

    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return;

    Path checkedPath(path, subPool);
    SVN_JNI_ERR(checkedPath.error_occurred(),);

    SVN_JNI_ERR(svn_client_vacuum(checkedPath.c_str(),
                                  remove_unversioned_items,
                                  remove_ignored_items,
                                  fix_recorded_timestamps,
                                  remove_unused_pristines,
                                  include_externals,
                                  ctx, subPool.getPool()), );
}

jobject
SVNClient::openRemoteSession(const char* path, int retryAttempts)
{
    static const svn_opt_revision_t HEAD = { svn_opt_revision_head, {0}};
    static const svn_opt_revision_t NONE = { svn_opt_revision_unspecified, {0}};

    SVN_JNI_NULL_PTR_EX(path, "path", NULL);

    SVN::Pool subPool(pool);
    svn_client_ctx_t *ctx = context.getContext(NULL, subPool);
    if (ctx == NULL)
        return NULL;

    Path checkedPath(path, subPool);
    SVN_JNI_ERR(checkedPath.error_occurred(), NULL);

    struct PathInfo
    {
        std::string url;
        std::string uuid;
        static svn_error_t *callback(void *baton,
                                     const char *,
                                     const svn_client_info2_t *info,
                                     apr_pool_t *)
          {
              PathInfo* const pi = static_cast<PathInfo*>(baton);
              pi->url = info->URL;
              pi->uuid = info->repos_UUID;
              return SVN_NO_ERROR;
          }
    } path_info;

    SVN_JNI_ERR(svn_client_info4(
                    checkedPath.c_str(), &NONE,
                    (svn_path_is_url(checkedPath.c_str()) ? &HEAD : &NONE),
                    svn_depth_empty, FALSE, TRUE, FALSE, NULL,
                    PathInfo::callback, &path_info,
                    ctx, subPool.getPool()),
                NULL);

    /* Decouple the RemoteSession's context from SVNClient's context
       by creating a copy of the prompter here. */

    jobject jremoteSession = RemoteSession::open(
        retryAttempts, path_info.url.c_str(), path_info.uuid.c_str(),
        context.getConfigDirectory(),
        context.getUsername(), context.getPassword(),
        context.clonePrompter(), context.getSelf(),
        context.getConfigEventHandler(), context.getTunnelCallback());
    if (JNIUtil::isJavaExceptionThrown())
      jremoteSession = NULL;

    return jremoteSession;
}

ClientContext &
SVNClient::getClientContext()
{
    return context;
}
