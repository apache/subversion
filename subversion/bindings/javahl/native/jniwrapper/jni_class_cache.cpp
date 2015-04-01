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
 */

#include <stdexcept>

#include <apr_atomic.h>

#define SVN_JAVAHL_JNIWRAPPER_LOG(expr)
#include "jni_env.hpp"
#include "jni_globalref.hpp"
#include "jni_exception.hpp"
#include "jni_object.hpp"
#include "jni_string.hpp"

#include "jni_channel.hpp"
#include "jni_io_stream.hpp"
#include "jni_list.hpp"
#include "jni_string_map.hpp"

#include "../SubversionException.hpp"
#include "../AuthnCallback.hpp"
#include "../Credential.hpp"
#include "../ExternalItem.hpp"
#include "../EditorCallbacks.hpp"

namespace
{
/* This class behaves like a dumbed-down std:auto_ptr, but it
   implements atomic access and modification of the wrapped
   pointer. */
class ClassImplPtr
{
  typedef ::Java::Object::ClassImpl ClassImpl;

 public:
  /* Default constructor; initializes the wrapped pointer to NULL */
  explicit ClassImplPtr()
    : m_ptr(NULL)
  {}

  /* Initializing constructor; sets the wrapped pointer to PTR */
  explicit ClassImplPtr(ClassImpl* ptr)
    : m_ptr(ptr)
  {}

  /* Destructor deletes the object and resets the wrapped pointer to NULL. */
  ~ClassImplPtr()
    {
      delete static_cast<ClassImpl*>(
          apr_atomic_casptr(&m_ptr, NULL, get()));
    }

  /* Sets the wrapped pointer to PTR iff it is NULL, and returns the
     old value. */
  ClassImpl* test_and_set(ClassImpl* ptr)
    {
      return static_cast<ClassImpl*>(
          apr_atomic_casptr(&m_ptr, ptr, NULL));
    }

  /* Returns the current value of the the wrapped pointer. */
  ClassImpl* get() const
    {
      return static_cast<ClassImpl*>(
          apr_atomic_casptr(&m_ptr, NULL, NULL));
    }

private:
  // Non-copyable
  ClassImplPtr(const ClassImplPtr&);
  ClassImplPtr& operator=(const ClassImplPtr&);

  mutable volatile void* m_ptr;
};
} // anonymous namespace


namespace Java {

class ClassCacheImpl
{

  friend class ClassCache;

  // We only statically initialize a few of the common class wrappers.
  explicit ClassCacheImpl(Env env) :

#define JNIWRAPPER_INIT_CACHED_CLASS(M, C)     \
  m_impl_##M(new C::ClassImpl(env, env.FindClass(C::m_class_name)))

      JNIWRAPPER_INIT_CACHED_CLASS(object, Object),
      JNIWRAPPER_INIT_CACHED_CLASS(classtype, Class),
      JNIWRAPPER_INIT_CACHED_CLASS(throwable, Exception),
      JNIWRAPPER_INIT_CACHED_CLASS(string, String)
#undef JNIWRAPPER_INIT_CACHED_CLASS
    {}

  // We can't do this in the constructor above, because the satic
  // initializers will expect that ClassCache::m_impl is already set;
  // that doesn't happen until the constructor returns.
  void static_init(Env env)
    {
#define JNIWRAPPER_STATIC_CACHED_CLASS(M, C)            \
      C::static_init(env, m_impl_##M->get_class())

      // No-op JNIWRAPPER_STATIC_CACHED_CLASS(object, Object);
      JNIWRAPPER_STATIC_CACHED_CLASS(classtype, Class);
      JNIWRAPPER_STATIC_CACHED_CLASS(throwable, Exception);
      // No-op JNIWRAPPER_STATIC_CACHED_CLASS(string, String);
#undef JNIWRAPPER_STATIC_CACHED_CLASS
    }

  // The statically initialized calss wrappers are always defined and
  // therefore do not need atomic access.
#define JNIWRAPPER_DEFINE_CACHED_CLASS(M, C)            \
  std::auto_ptr<Object::ClassImpl> m_impl_##M;          \
  const Object::ClassImpl* get_##M(Env)                 \
    {                                                   \
      return m_impl_##M.get();                          \
    }

  JNIWRAPPER_DEFINE_CACHED_CLASS(object, Object)
  JNIWRAPPER_DEFINE_CACHED_CLASS(classtype, Class)
  JNIWRAPPER_DEFINE_CACHED_CLASS(throwable, Exception)
  JNIWRAPPER_DEFINE_CACHED_CLASS(string, String)
#undef JNIWRAPPER_DEFINE_CACHED_CLASS

  // All other class wrappers must be atomically initialized
#define JNIWRAPPER_DEFINE_CACHED_CLASS(M, C)                    \
  ClassImplPtr m_impl_##M;                                      \
  const Object::ClassImpl* get_##M(Env env)                     \
    {                                                           \
      Object::ClassImpl* pimpl = m_impl_##M.get();              \
      if (!pimpl)                                               \
        {                                                       \
          std::auto_ptr<Object::ClassImpl> tmp(                 \
              new C::ClassImpl(                                 \
                  env, env.FindClass(C::m_class_name)));        \
          pimpl = m_impl_##M.test_and_set(tmp.get());           \
          if (!pimpl)                                           \
            pimpl = tmp.release();                              \
        }                                                       \
      return pimpl;                                             \
    }

  JNIWRAPPER_DEFINE_CACHED_CLASS(exc_index_out_of_bounds,
                                 IndexOutOfBoundsException);
  JNIWRAPPER_DEFINE_CACHED_CLASS(exc_no_such_element,
                                 NoSuchElementException);

  JNIWRAPPER_DEFINE_CACHED_CLASS(iterator, BaseIterator);

  JNIWRAPPER_DEFINE_CACHED_CLASS(list, BaseImmutableList);
  JNIWRAPPER_DEFINE_CACHED_CLASS(array_list, BaseList);

  JNIWRAPPER_DEFINE_CACHED_CLASS(map, BaseImmutableMap);
  JNIWRAPPER_DEFINE_CACHED_CLASS(set, BaseImmutableMap::Set);
  JNIWRAPPER_DEFINE_CACHED_CLASS(map_entry, BaseImmutableMap::Entry);
  JNIWRAPPER_DEFINE_CACHED_CLASS(hash_map, BaseMap);

  JNIWRAPPER_DEFINE_CACHED_CLASS(input_stream, InputStream);
  JNIWRAPPER_DEFINE_CACHED_CLASS(output_stream, OutputStream);

  JNIWRAPPER_DEFINE_CACHED_CLASS(byte_buffer,
                                 ByteChannel::ByteBuffer);

  JNIWRAPPER_DEFINE_CACHED_CLASS(subversion_exception,
                                 ::JavaHL::SubversionException);

  JNIWRAPPER_DEFINE_CACHED_CLASS(authn_cb,
                                 ::JavaHL::AuthnCallback);
  JNIWRAPPER_DEFINE_CACHED_CLASS(authn_result,
                                 ::JavaHL::AuthnCallback::AuthnResult);
  JNIWRAPPER_DEFINE_CACHED_CLASS(authn_ssl_server_cert_failures,
                                 ::JavaHL::AuthnCallback::SSLServerCertFailures);
  JNIWRAPPER_DEFINE_CACHED_CLASS(authn_ssl_server_cert_info,
                                 ::JavaHL::AuthnCallback::SSLServerCertInfo);
  JNIWRAPPER_DEFINE_CACHED_CLASS(user_passwd_cb,
                                 ::JavaHL::UserPasswordCallback);

  JNIWRAPPER_DEFINE_CACHED_CLASS(credential,
                                 ::JavaHL::Credential);
  JNIWRAPPER_DEFINE_CACHED_CLASS(credential_kind,
                                 ::JavaHL::Credential::Kind);

  JNIWRAPPER_DEFINE_CACHED_CLASS(external_item,
                                 ::JavaHL::ExternalItem);

  JNIWRAPPER_DEFINE_CACHED_CLASS(editor_provide_base_cb,
                                 ::JavaHL::ProvideBaseCallback);
  JNIWRAPPER_DEFINE_CACHED_CLASS(editor_provide_base_cb_ret,
                                 ::JavaHL::ProvideBaseCallback::ReturnValue);
  JNIWRAPPER_DEFINE_CACHED_CLASS(editor_provide_props_cb,
                                 ::JavaHL::ProvidePropsCallback);
  JNIWRAPPER_DEFINE_CACHED_CLASS(editor_provide_props_cb_ret,
                                 ::JavaHL::ProvidePropsCallback::ReturnValue);
  JNIWRAPPER_DEFINE_CACHED_CLASS(editor_get_kind_cb,
                                 ::JavaHL::GetNodeKindCallback);
#undef JNIWRAPPER_DEFINE_CACHED_CLASS
};


ClassCacheImpl* ClassCache::m_impl = NULL;

void ClassCache::create()
{
  const char* exception_message = NULL;

  try
    {
      const Env env;
      m_impl = new ClassCacheImpl(env);
      m_impl->static_init(env);
    }
  catch (const SignalExceptionThrown&)
    {}
  catch (const std::exception& ex)
    {
      exception_message = ex.what();
    }
  catch (...)
    {
      exception_message = "Caught unknown C++ exception";
    }

  // Do not throw any more exceptions from here, so use the raw environment.
  ::JNIEnv* const jenv = Env().get();
  if (exception_message || jenv->ExceptionCheck())
    {
      jobject cause = jenv->ExceptionOccurred();
      if (cause)
        jenv->ExceptionClear();

      const jclass rtx = jenv->FindClass("java/lang/RuntimeException");
      const jmethodID ctor = jenv->GetMethodID(rtx, "<init>",
                                               "(Ljava/lang/String;"
                                               "Ljava/lang/Throwable;)V");
      if (!cause && exception_message)
        {
          const jstring msg = jenv->NewStringUTF(exception_message);
          cause = jenv->NewObject(rtx, ctor, msg, jthrowable(0));
        }
      const jstring reason =
        jenv->NewStringUTF("JavaHL native library initialization failed");
      const jobject exception = jenv->NewObject(rtx, ctor, reason, cause);
      jenv->Throw(jthrowable(exception));
    }
}

void ClassCache::destroy()
{
  ClassCacheImpl* const pimpl = m_impl;
  m_impl = NULL;
  delete pimpl;
}

#define JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(M)         \
const Object::ClassImpl* ClassCache::get_##M(Env env)   \
{                                                       \
  return m_impl->get_##M(env);                          \
}

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(object);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(classtype);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(throwable);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(string);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(exc_index_out_of_bounds);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(exc_no_such_element);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(list);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(array_list);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(map);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(set);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(iterator);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(map_entry);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(hash_map);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(input_stream);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(output_stream);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(byte_buffer);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(subversion_exception);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(authn_cb);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(authn_result);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(authn_ssl_server_cert_failures);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(authn_ssl_server_cert_info);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(user_passwd_cb);


JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(credential);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(credential_kind);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(external_item);

JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(editor_provide_base_cb);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(editor_provide_base_cb_ret);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(editor_provide_props_cb);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(editor_provide_props_cb_ret);
JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR(editor_get_kind_cb);
#undef JNIWRAPPER_IMPL_CLASS_CACHE_ACCESSOR

} // namespace Java
