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
 * @file StateReporter.cpp
 * @brief Implementation of the class StateReporter
 */

#include <jni.h>

#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "StateReporter.h"
#include "EnumMapper.h"
#include "Path.h"

#include "svn_private_config.h"

StateReporter::StateReporter()
  : m_valid(false),
    m_raw_reporter(NULL),
    m_report_baton(NULL),
    m_editor(NULL),
    m_target_revision(SVN_INVALID_REVNUM)
{}

StateReporter::~StateReporter()
{}

StateReporter*
StateReporter::getCppObject(jobject jthis)
{
  static jfieldID fid = 0;
  jlong cppAddr = SVNBase::findCppAddrForJObject(jthis, &fid,
      JAVAHL_CLASS("/remote/StateReporter"));
  return (cppAddr == 0 ? NULL : reinterpret_cast<StateReporter*>(cppAddr));
}

void
StateReporter::dispose(jobject jthis)
{
  //DEBUG:fprintf(stderr, "  (n) StateReporter::dispose()\n");

  if (m_valid)
    abortReport();

  static jfieldID fid = 0;
  SVNBase::dispose(jthis, &fid, JAVAHL_CLASS("/remote/StateReporter"));
}

namespace {
void throw_reporter_inactive()
{
  JNIUtil::raiseThrowable("java/lang/IllegalStateException",
                          _("The reporter is not active"));
}
} // anonymous namespace

void
StateReporter::setPath(jstring jpath, jlong jrevision, jobject jdepth,
                       jboolean jstart_empty, jstring jlock_token)
{
  //DEBUG:fprintf(stderr, "  (n) StateReporter::setPath()\n");

  if (!m_valid) { throw_reporter_inactive(); return; }

  JNIStringHolder lock_token(jlock_token);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN::Pool subPool(pool);
  Relpath path(jpath, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  svn_depth_t depth = EnumMapper::toDepth(jdepth);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN_JNI_ERR(m_raw_reporter->set_path(m_report_baton, path.c_str(),
                                       svn_revnum_t(jrevision), depth,
                                       bool(jstart_empty), lock_token.c_str(),
                                       subPool.getPool()),);
}

void
StateReporter::deletePath(jstring jpath)
{
  //DEBUG:fprintf(stderr, "  (n) StateReporter::deletePath()\n");

  if (!m_valid) { throw_reporter_inactive(); return; }

  SVN::Pool subPool(pool);
  Relpath path(jpath, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN_JNI_ERR(m_raw_reporter->delete_path(m_report_baton, path.c_str(),
                                          subPool.getPool()),);
}

void
StateReporter::linkPath(jstring jurl, jstring jpath,
                        jlong jrevision, jobject jdepth,
                        jboolean jstart_empty, jstring jlock_token)
{
  //DEBUG:fprintf(stderr, "  (n) StateReporter::linkPath()\n");

  if (!m_valid) { throw_reporter_inactive(); return; }

  JNIStringHolder lock_token(jlock_token);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN::Pool subPool(pool);
  Relpath path(jpath, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  URL url(jurl, subPool);
  if (JNIUtil::isJavaExceptionThrown())
    return;
  svn_depth_t depth = EnumMapper::toDepth(jdepth);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  SVN_JNI_ERR(m_raw_reporter->link_path(m_report_baton, path.c_str(),
                                        url.c_str(), svn_revnum_t(jrevision),
                                        depth, bool(jstart_empty),
                                        lock_token.c_str(),
                                        subPool.getPool()),);
}

jlong
StateReporter::finishReport()
{
  //DEBUG:fprintf(stderr, "  (n) StateReporter::finishReport()\n");

  if (!m_valid) { throw_reporter_inactive(); return SVN_INVALID_REVNUM; }

  SVN::Pool subPool(pool);
  SVN_JNI_ERR(m_raw_reporter->finish_report(m_report_baton,
                                            subPool.getPool()),
              SVN_INVALID_REVNUM);
  m_valid = false;
  return jlong(m_target_revision);
}

void
StateReporter::abortReport()
{
  //DEBUG:fprintf(stderr, "  (n) StateReporter::abortReport()\n");

  if (!m_valid) { throw_reporter_inactive(); return; }

  SVN::Pool subPool(pool);
  SVN_JNI_ERR(m_raw_reporter->abort_report(m_report_baton,
                                           subPool.getPool()),);
  m_valid = false;
}

void
StateReporter::set_reporter_data(const svn_ra_reporter3_t* raw_reporter,
                                 void* report_baton,
                                 EditorProxy::UniquePtr editor)
{
  //DEBUG:fprintf(stderr, "  (n) StateReporter::set_reporter_data()\n");

  m_editor = editor;
  m_raw_reporter = raw_reporter;
  m_report_baton = report_baton;
  m_valid = true;
}
