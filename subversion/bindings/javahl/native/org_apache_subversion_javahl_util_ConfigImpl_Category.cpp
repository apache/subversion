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
 * @file org_apache_subversion_javahl_util_ConfigImpl_Category.cpp
 * @brief Implementation of the native methods in the Java class
 *        util.ConfigImpl.Category.
 */

#include <string>
#include <vector>

#include "../include/org_apache_subversion_javahl_util_ConfigImpl_Category.h"
#include "JNIUtil.h"
#include "JNIStackElement.h"
#include "JNIStringHolder.h"
#include "OperationContext.h"
#include "CreateJ.h"
#include "EnumMapper.h"

#include "svn_config.h"
#include "svn_hash.h"
#include "svn_private_config.h"

namespace {
struct ImplContext
{
  ImplContext(JNIEnv* env, jobject jthis,
              jstring jcategory, jlong jcontext,
              jstring jsection, jstring joption) : m_config(NULL)
    {
      OperationContext* const context(
          reinterpret_cast<OperationContext*>(jcontext));
      CPPADDR_NULL_PTR(context,);

      JNIStringHolder category(jcategory);
      if (JNIUtil::isJavaExceptionThrown())
        return;
      if (category.c_str())
        {
          apr_hash_t* cfgdata = context->getConfigData();
          if (cfgdata)
            m_config = static_cast<svn_config_t*>(
                svn_hash_gets(cfgdata, category.c_str()));
          else
            JNIUtil::throwNullPointerException("getConfigData");
        }
      if (!m_config)
        JNIUtil::throwNullPointerException("category");

      JNIStringHolder section(jsection);
      if (JNIUtil::isJavaExceptionThrown())
        return;
      if (section.c_str())
        m_section = section.c_str();

      JNIStringHolder option(joption);
      if (JNIUtil::isJavaExceptionThrown())
        return;
      if (option.c_str())
        m_option = option.c_str();
    }

  svn_config_t* m_config;
  std::string m_section;
  std::string m_option;
};
} // anonymous namespace



JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_get_1str(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption, jstring jdefault_value)
{
  JNIEntry(ConfigImpl$Category, get_str);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  JNIStringHolder default_value(jdefault_value);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  const char* value;
  svn_config_get(ctx.m_config, &value,
                 ctx.m_section.c_str(), ctx.m_option.c_str(),
                 default_value.c_str());
  return JNIUtil::makeJString(value);
}

JNIEXPORT jboolean JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_get_1bool(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption, jboolean jdefault_value)
{
  JNIEntry(ConfigImpl$Category, get_bool);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  svn_boolean_t value;
  SVN_JNI_ERR(svn_config_get_bool(ctx.m_config, &value,
                                  ctx.m_section.c_str(), ctx.m_option.c_str(),
                                  bool(jdefault_value)),
              jdefault_value);
  return jboolean(value);
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_get_1long(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption, jlong jdefault_value)
{
  JNIEntry(ConfigImpl$Category, get_long);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  apr_int64_t value;
  SVN_JNI_ERR(svn_config_get_int64(ctx.m_config, &value,
                                   ctx.m_section.c_str(), ctx.m_option.c_str(),
                                   apr_int64_t(jdefault_value)),
              jdefault_value);
  return jlong(value);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_get_1tri(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption,
    jstring junknown, jobject jdefault_value)
{
  JNIEntry(ConfigImpl$Category, get_tri);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  JNIStringHolder unknown(junknown);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  svn_tristate_t value;
  SVN_JNI_ERR(svn_config_get_tristate(ctx.m_config, &value,
                                      ctx.m_section.c_str(),
                                      ctx.m_option.c_str(),
                                      unknown.c_str(),
                                      EnumMapper::toTristate(jdefault_value)),
              NULL);
  return EnumMapper::mapTristate(value);
}

JNIEXPORT jstring JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_get_1yna(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption, jstring jdefault_value)
{
  JNIEntry(ConfigImpl$Category, get_yna);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  JNIStringHolder default_value(jdefault_value);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  const char* value;
  SVN_JNI_ERR(svn_config_get_yes_no_ask(
                  ctx.m_config, &value,
                  ctx.m_section.c_str(), ctx.m_option.c_str(),
                  default_value.c_str()),
              NULL);
  return JNIUtil::makeJString(value);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_set_1str(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption, jstring jvalue)
{
  JNIEntry(ConfigImpl$Category, set_str);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  JNIStringHolder value(jvalue);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  svn_config_set(ctx.m_config,
                 ctx.m_section.c_str(), ctx.m_option.c_str(),
                 value.c_str());
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_set_1bool(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption, jboolean jvalue)
{
  JNIEntry(ConfigImpl$Category, set_bool);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  svn_config_set_bool(ctx.m_config,
                      ctx.m_section.c_str(), ctx.m_option.c_str(),
                      bool(jvalue));
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_set_1long(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jstring joption, jlong jvalue)
{
  JNIEntry(ConfigImpl$Category, set_long);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, joption);

  svn_config_set_int64(ctx.m_config,
                       ctx.m_section.c_str(), ctx.m_option.c_str(),
                       apr_int64_t(jvalue));
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_sections(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext)
{
  JNIEntry(ConfigImpl$Category, sections);
  const ImplContext ctx(env, jthis, jcategory, jcontext, NULL, NULL);

  struct enumerator_t
  {
    static svn_boolean_t process(const char* name, void* baton,
                                 apr_pool_t *pool)
      {
        jstring jname = JNIUtil::makeJString(name);
        if (JNIUtil::isJavaExceptionThrown())
          return false;
        static_cast<enumerator_t*>(baton)
          ->m_sections.push_back(jobject(jname));
        return true;
      }
    std::vector<jobject> m_sections;
  } enumerator;

  SVN::Pool requestPool;
  svn_config_enumerate_sections2(ctx.m_config, enumerator.process, &enumerator,
                                 requestPool.getPool());
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;
  return CreateJ::Set(enumerator.m_sections);
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_util_ConfigImpl_00024Category_enumerate(
    JNIEnv* env, jobject jthis, jstring jcategory, jlong jcontext,
    jstring jsection, jobject jhandler)
{
  JNIEntry(ConfigImpl$Category, enumerate);
  const ImplContext ctx(env, jthis, jcategory, jcontext, jsection, NULL);

  struct enumerator_t
  {
    static svn_boolean_t process(const char* name, const char* value,
                                 void* baton, apr_pool_t *pool)
      {
        enumerator_t* enmr = static_cast<enumerator_t*>(baton);
        JNIEnv* const e = enmr->m_env;
        const jobject jh = enmr->m_jhandler;

        static jmethodID mid = 0;
        if (0 == mid)
          {
            jclass cls = e->FindClass(JAVAHL_CLASS("/ISVNConfig$Enumerator"));
            if (JNIUtil::isJavaExceptionThrown())
              return false;
            mid = e->GetMethodID(cls, "option",
                                   "(Ljava/lang/String;Ljava/lang/String;)V");
            if (JNIUtil::isJavaExceptionThrown())
              return false;
          }

        jstring jname = JNIUtil::makeJString(name);
        if (JNIUtil::isJavaExceptionThrown())
          return false;
        jstring jvalue = JNIUtil::makeJString(value);
        if (JNIUtil::isJavaExceptionThrown())
          return false;

        e->CallVoidMethod(jh, mid, jname, jvalue);
        if (JNIUtil::isJavaExceptionThrown())
          return false;

        e->DeleteLocalRef(jname);
        e->DeleteLocalRef(jvalue);
        return true;
      }

    JNIEnv* m_env;
    jobject m_jhandler;
  } enumerator;

  enumerator.m_env = env;
  enumerator.m_jhandler = jhandler;

  SVN::Pool requestPool;
  svn_config_enumerate2(ctx.m_config, ctx.m_section.c_str(),
                        enumerator.process, &enumerator,
                        requestPool.getPool());
}
