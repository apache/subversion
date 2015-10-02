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
 * @file org_apache_subversion_javahl_util_SubstLib.cpp
 * @brief Implementation of the native methods in the Java class SubstLib
 */

#include <memory>

#include "../include/org_apache_subversion_javahl_util_SubstLib.h"

#include "jniwrapper/jni_stack.hpp"
#include "jniwrapper/jni_array.hpp"
#include "jniwrapper/jni_string.hpp"
#include "jniwrapper/jni_string_map.hpp"
#include "jniwrapper/jni_io_stream.hpp"

#include "JNIUtil.h"
#include "NativeStream.hpp"
#include "Utility.hpp"

#include <apr_hash.h>

#include "svn_subst.h"

#include "svn_private_config.h"


namespace {
apr_hash_t*
build_keywords_common(Java::Env env, const SVN::Pool& pool,
                      jbyteArray jkeywords_value, jlong jrevision,
                      jstring jurl, jstring jrepos_root_url,
                      jobject jdate, jstring jauthor)
{
  const Java::ByteArray keywords_value(env, jkeywords_value);
  const Java::String url(env, jurl);
  const Java::String repos_root_url(env, jrepos_root_url);
  const Java::String author(env, jauthor);

  const Java::ByteArray::Contents keywords_contents(keywords_value);
  svn_string_t* keywords_string = keywords_contents.get_string(pool);
  const char* revision = (jrevision < 0 ? NULL
                          : apr_psprintf(pool.getPool(),
                                         "%" APR_UINT64_T_FMT,
                                         apr_uint64_t(jrevision)));
  const Java::String::Contents url_contents(url);
  const Java::String::Contents root_url_contents(repos_root_url);
  const Java::String::Contents author_contents(author);

  apr_hash_t* kw = NULL;
  SVN_JAVAHL_CHECK(env,
                   svn_subst_build_keywords3(
                       &kw,
                       keywords_string->data,
                       revision,
                       url_contents.c_str(),
                       root_url_contents.c_str(),
                       (jdate ? JNIUtil::getDate(jdate) : 0),
                       author_contents.c_str(),
                       pool.getPool()));
  return kw;
}

svn_stream_t*
translate_stream_common(Java::Env env, const SVN::Pool& pool,
                        svn_stream_t* stream,
                        jbyteArray jeol_marker, jboolean jrepair_eol,
                        jobject jkeywords, jboolean juse_keywords,
                        jboolean jexpand_keywords,
                        jbyteArray jkeywords_value, jlong jrevision,
                        jstring jurl, jstring jrepos_root_url,
                        jobject jdate, jstring jauthor)
{
  apr_hash_t* const keywords =
    (juse_keywords
     ? JavaHL::Util::make_keyword_hash(env, jkeywords, pool)
     : build_keywords_common(
         env, pool, jkeywords_value, jrevision,
         jurl, jrepos_root_url, jdate, jauthor));

  const Java::ByteArray eol_marker(env, jeol_marker);
  svn_string_t* const eol_str =
    Java::ByteArray::Contents(eol_marker).get_string(pool);
  return svn_subst_stream_translated(stream,
                                     eol_str->data,
                                     svn_boolean_t(jrepair_eol),
                                     keywords,
                                     svn_boolean_t(jexpand_keywords),
                                     pool.getPool());
}
} // anoymous namespace


JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_SubstLib_buildKeywords(
    JNIEnv* jenv, jobject jthis,
    jbyteArray jkeywords_value, jlong jrevision,
    jstring jurl, jstring jrepos_root_url,
    jobject jdate, jstring jauthor)
{
  typedef Java::Map<Java::ByteArray, jbyteArray> ByteArrayMap;

  SVN_JAVAHL_JNI_TRY(SubstLib, buildKeywords)
    {
      const Java::Env env(jenv);

      // Using a "global" request pool since we don't keep a context with
      // its own pool around for these functions.
      SVN::Pool pool;

      apr_hash_t* const kw = build_keywords_common(
          env, pool, jkeywords_value, jrevision,
          jurl, jrepos_root_url, jdate, jauthor);

      ByteArrayMap keywords(env, jint(apr_hash_count(kw)));
      for (apr_hash_index_t* hi = apr_hash_first(pool.getPool(), kw);
           hi; hi = apr_hash_next(hi))
        {
          const void* rkey;
          void* rval;
          apr_hash_this(hi, &rkey, NULL, &rval);

          svn_string_t* const val = static_cast<svn_string_t*>(rval);
          keywords.put(static_cast<const char*>(rkey),
                       Java::ByteArray(env, val->data, jsize(val->len)));
        }
      return keywords.get();
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}


JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_SubstLib_translateInputStream(
    JNIEnv* jenv, jobject jthis,
    jobject jsource, jbyteArray jeol_marker, jboolean jrepair_eol,
    jobject jkeywords, jboolean juse_keywords, jboolean jexpand_keywords,
    jbyteArray jkeywords_value, jlong jrevision,
    jstring jurl, jstring jrepos_root_url,
    jobject jdate, jstring jauthor)
{
  SVN_JAVAHL_JNI_TRY(SubstLib, translateInputStream)
    {
      const Java::Env env(jenv);

      // We'll allocate the stream in the bound object's pool.
      std::auto_ptr<JavaHL::NativeInputStream>
        translated(new JavaHL::NativeInputStream());
      svn_stream_t* source = Java::InputStream::get_global_stream(
          env, jsource, translated->get_pool());

      translated->set_stream(translate_stream_common(
                                 env, translated->get_pool(), source,
                                 jeol_marker, jrepair_eol,
                                 jkeywords, juse_keywords, jexpand_keywords,
                                 jkeywords_value, jrevision,
                                 jurl, jrepos_root_url, jdate, jauthor));
      const jobject jtranslated = translated->create_java_wrapper();
      translated.release();
      return jtranslated;
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}


JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_util_SubstLib_translateOutputStream(
    JNIEnv* jenv, jobject jthis,
    jobject jdestination, jbyteArray jeol_marker, jboolean jrepair_eol,
    jobject jkeywords, jboolean juse_keywords, jboolean jexpand_keywords,
    jbyteArray jkeywords_value, jlong jrevision,
    jstring jurl, jstring jrepos_root_url,
    jobject jdate, jstring jauthor)
{
  SVN_JAVAHL_JNI_TRY(SubstLib, translateInputStream)
    {
      const Java::Env env(jenv);

      // We'll allocate the stream in the bound object's pool.
      std::auto_ptr<JavaHL::NativeOutputStream>
        translated(new JavaHL::NativeOutputStream());
      svn_stream_t* destination = Java::OutputStream::get_global_stream(
          env, jdestination, translated->get_pool());

      translated->set_stream(translate_stream_common(
                                 env, translated->get_pool(), destination,
                                 jeol_marker, jrepair_eol,
                                 jkeywords, juse_keywords, jexpand_keywords,
                                 jkeywords_value, jrevision,
                                 jurl, jrepos_root_url, jdate, jauthor));
      const jobject jtranslated = translated->create_java_wrapper();
      translated.release();
      return jtranslated;
    }
  SVN_JAVAHL_JNI_CATCH;
  return NULL;
}
