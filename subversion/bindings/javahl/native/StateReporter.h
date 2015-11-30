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
 * @file StateReporter.h
 * @brief Interface of the class UpdateReporter
 */

#ifndef JAVAHL_UPDATE_REPORTER_H
#define JAVAHL_UPDATE_REPORTER_H

#include <jni.h>

#include "svn_ra.h"
#include "SVNBase.h"
#include "EditorProxy.h"

class RemoteSession;

/*
 * This class wraps the update/status/switch/diff reporter in svn_ra.h
 */
class StateReporter : public SVNBase
{
public:
  StateReporter();
  virtual ~StateReporter();

  static StateReporter* getCppObject(jobject jreporter);

  virtual void dispose(jobject jthis);

  void setPath(jstring jpath, jlong jrevision, jobject jdepth,
               jboolean jstart_empty, jstring jlock_token);
  void deletePath(jstring jpath);
  void linkPath(jstring jurl, jstring jpath, jlong jrevision, jobject jdepth,
                jboolean jstart_empty, jstring jlock_token);
  jlong finishReport();
  void abortReport();

private:

  bool m_valid;
  const svn_ra_reporter3_t* m_raw_reporter;
  void* m_report_baton;
  EditorProxy::UniquePtr m_editor;

  friend class RemoteSession;
  apr_pool_t* get_report_pool() const { return pool.getPool(); }
  void set_reporter_data(const svn_ra_reporter3_t* raw_reporter,
                         void* report_baton,
                         EditorProxy::UniquePtr editor);
  svn_revnum_t m_target_revision;
};

#endif // JAVAHL_UPDATE_REPORTER_H
