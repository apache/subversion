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
 * @file JNIUtil.cpp
 * @brief Implementation of the class JNIUtil
 */

/* Include apr.h first, or INT64_C won't be defined properly on some C99
   compilers, when other headers include <stdint.h> before defining some
   macros.

   See apr.h for the ugly details */
#include <apr.h>

#include "JNIUtil.h"
#include "Array.h"

#include <sstream>
#include <vector>
#include <locale.h>

#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_file_info.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_ra.h"
#include "svn_utf.h"
#include "svn_wc.h"
#include "svn_dso.h"
#include "svn_path.h"
#include "svn_cache_config.h"
#include "private/svn_atomic.h"
#include "private/svn_utf_private.h"
#include "svn_private_config.h"

#include "SVNBase.h"
#include "JNIMutex.h"
#include "JNICriticalSection.h"
#include "JNIStringHolder.h"
#include "Pool.h"


#include "jniwrapper/jni_env.hpp"

// Static members of JNIUtil are allocated here.
apr_pool_t *JNIUtil::g_pool = NULL;
std::list<SVNBase*> JNIUtil::g_finalizedObjects;
JNIMutex *JNIUtil::g_finalizedObjectsMutex = NULL;
JNIMutex *JNIUtil::g_logMutex = NULL;
JNIMutex *JNIUtil::g_configMutex = NULL;
bool JNIUtil::g_initException;
int JNIUtil::g_logLevel = JNIUtil::noLog;
std::ofstream JNIUtil::g_logStream;

/* The error code we will use to signal a Java exception */
static const apr_status_t
SVN_ERR_JAVAHL_WRAPPED = SVN_ERR_MALFUNC_CATEGORY_START
                         + SVN_ERR_CATEGORY_SIZE - 10;

/**
 * Return the JNI environment to use
 * @return the JNI environment
 */
JNIEnv *JNIUtil::getEnv()
{
  return Java::Env().get();
}

/**
 * Initialize the environment for all requests.
 * @param env   the JNI environment for this request
 */
bool JNIUtil::JNIInit(JNIEnv *env)
{
  // Clear all standing exceptions.
  env->ExceptionClear();

  // Lock the list of finalized objects.
  JNICriticalSection cs(*g_finalizedObjectsMutex) ;
  if (isExceptionThrown())
    return false;

  // Delete all finalized, but not yet deleted objects.
  for (std::list<SVNBase*>::iterator it = g_finalizedObjects.begin();
       it != g_finalizedObjects.end();
       ++it)
    {
      delete *it;
    }
  g_finalizedObjects.clear();

  return true;
}

/* Forwarder for calling JNIGlobalInit from JNI_OnLoad(). */
bool initialize_jni_util(JNIEnv *env)
{
  return JNIUtil::JNIGlobalInit(env);
}

namespace {

volatile svn_atomic_t *gentle_crash_write_loc = NULL;

svn_error_t *
gently_crash_the_jvm(svn_boolean_t can_return,
                     const char *file, int line, const char *expr)
{
  if (!can_return)
    {
      // Try not to abort; aborting prevents the JVM from creating
      // a crash log, which is oh so useful for debugging.
      // We can't just raise a SEGV signal, either, because it will
      // be not be caught in the context that we're interested in
      // getting the stack trace from.

      // Try reading from and writing to the zero page
      const svn_atomic_t zeropage = svn_atomic_read(gentle_crash_write_loc);
      svn_atomic_set(gentle_crash_write_loc, zeropage);
    }

  // Forward to the standard malfunction handler, which does call
  // abort when !can_return; this will only happen if the write to the
  // zero page did not cause a SEGV.
  return svn_error_raise_on_malfunction(can_return, file, line, expr);
}
} // Anonymous namespace

/**
 * Initialize the environment for all requests.
 * This method must be called in a single-threaded context.
 * @param env   the JNI environment for this request
 */
bool JNIUtil::JNIGlobalInit(JNIEnv *env)
{
  svn_error_t *err;

  /* This has to happen before any pools are created. */
  if ((err = svn_dso_initialize2()))
    {
      if (stderr && err->message)
        fprintf(stderr, "%s", err->message);

      svn_error_clear(err);
      return FALSE;
    }

  /* Create our top-level pool.
     N.B.: APR was initialized by JNI_OnLoad. */
  g_pool = svn_pool_create(NULL);

  apr_allocator_t* allocator = apr_pool_allocator_get(g_pool);

  if (allocator)
    {
      /* Keep a maximum of 1 free block, to release memory back to the JVM
         (and other modules). */
      apr_allocator_max_free_set(allocator, 1);
    }

  svn_utf_initialize2(FALSE, g_pool); /* Optimize character conversions */

  // Initialize the libraries we use
  err = svn_fs_initialize(g_pool);
  if (!err)
    err = svn_ra_initialize(g_pool);
  if (err)
    {
      if (stderr && err->message)
        fprintf(stderr, "%s", err->message);

      svn_error_clear(err);
      return FALSE;
    }

  /* We shouldn't fill the JVMs memory with FS cache data unless
     explicitly requested. And we don't either, because the caches get
     allocated outside the JVM heap. Duh. */
  {
    svn_cache_config_t settings = *svn_cache_config_get();
    settings.single_threaded = FALSE;
    svn_cache_config_set(&settings);
  }

#ifdef ENABLE_NLS
#ifdef WIN32
  {
    WCHAR ucs2_path[MAX_PATH];
    const char *utf8_path;
    const char *internal_path;
    svn_error_t *err;
    apr_pool_t *pool = svn_pool_create(g_pool);

    /* get dll name - our locale info will be in '../share/locale' */
    HINSTANCE moduleHandle = GetModuleHandle("libsvnjavahl-1");
    GetModuleFileNameW(moduleHandle, ucs2_path,
                       sizeof(ucs2_path) / sizeof(ucs2_path[0]));
    err = svn_utf__win32_utf16_to_utf8(&utf8_path, ucs2_path, NULL, pool);
    if (err)
      {
        if (stderr)
          svn_handle_error2(err, stderr, false, "svn: ");
        svn_error_clear(err);
        return false;
      }

    internal_path = svn_dirent_internal_style(utf8_path, pool);
    /* get base path name */
    internal_path = svn_dirent_dirname(internal_path, pool);
    internal_path = svn_dirent_join(internal_path, SVN_LOCALE_RELATIVE_PATH,
                                  pool);
    bindtextdomain(PACKAGE_NAME, internal_path);
    svn_pool_destroy(pool);
  }
#else
  bindtextdomain(PACKAGE_NAME, SVN_LOCALE_DIR);
#endif
#endif

#if defined(WIN32) || defined(__CYGWIN__)
  /* See http://svn.apache.org/repos/asf/subversion/trunk/notes/asp-dot-net-hack.txt */
  /* ### This code really only needs to be invoked by consumers of
     ### the libsvn_wc library, which basically means SVNClient. */
  if (getenv("SVN_ASP_DOT_NET_HACK"))
    {
      err = svn_wc_set_adm_dir("_svn", g_pool);
      if (err)
        {
          if (stderr)
            {
              fprintf(stderr,
                      "%s: error: SVN_ASP_DOT_NET_HACK failed: %s\n",
                      "svnjavahl", err->message);
            }
          svn_error_clear(err);
          return FALSE;
        }
    }
#endif

  svn_error_set_malfunction_handler(svn_error_raise_on_malfunction);

  // Build all mutexes.
  g_finalizedObjectsMutex = new JNIMutex(g_pool);
  if (isExceptionThrown())
    return false;

  g_logMutex = new JNIMutex(g_pool);
  if (isExceptionThrown())
    return false;

  g_configMutex = new JNIMutex(g_pool);
  if (isExceptionThrown())
    return false;

  // Set a malfunction handler that tries not to call abort, because
  // that would prevent the JVM from creating a crash and stack log file.
  svn_error_set_malfunction_handler(gently_crash_the_jvm);

  return true;
}

/**
 * Returns the global (not request specific) pool.
 * @return global pool
 */
apr_pool_t *JNIUtil::getPool()
{
  return g_pool;
}

void JNIUtil::raiseThrowable(const char *name, const char *message)
{
  if (getLogLevel() >= errorLog)
    {
      JNICriticalSection cs(*g_logMutex);
      g_logStream << "Throwable raised <" << message << ">" << std::endl;
    }

  JNIEnv *env = getEnv();
  jclass clazz = env->FindClass(name);
  if (isJavaExceptionThrown())
    return;

  env->ThrowNew(clazz, message);
}

void
JNIUtil::throwNativeException(const char *className, const char *msg,
                              const char *source, int aprErr)
{
  JNIEnv *env = getEnv();
  jclass clazz = env->FindClass(className);

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  if (getLogLevel() >= exceptionLog)
    {
      JNICriticalSection cs(*g_logMutex);
      g_logStream << "Subversion JavaHL exception thrown, message:<";
      g_logStream << msg << ">";
      if (source)
        g_logStream << " source:<" << source << ">";
      if (aprErr != -1)
        g_logStream << " apr-err:<" << aprErr << ">";
      g_logStream << std::endl;
    }
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  jstring jmessage = makeJString(msg);
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();
  jstring jsource = makeJString(source);
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  jmethodID mid = env->GetMethodID(clazz, "<init>",
                                   "(Ljava/lang/String;Ljava/lang/String;I)V");
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();
  jobject nativeException = env->NewObject(clazz, mid, jmessage, jsource,
                                           static_cast<jint>(aprErr));
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NOTHING();

  env->Throw(static_cast<jthrowable>(env->PopLocalFrame(nativeException)));
}

void
JNIUtil::putErrorsInTrace(svn_error_t *err,
                          std::vector<jobject> &stackTrace)
{
  if (!err)
    return;

  JNIEnv *env = getEnv();

  // First, put all our child errors in the stack trace
  putErrorsInTrace(err->child, stackTrace);

  // Now, put our own error in the stack trace
  jclass stClazz = env->FindClass("java/lang/StackTraceElement");
  if (isJavaExceptionThrown())
    return;

  static jmethodID ctor_mid = 0;
  if (ctor_mid == 0)
    {
      ctor_mid = env->GetMethodID(stClazz, "<init>",
                                  "(Ljava/lang/String;Ljava/lang/String;"
                                  "Ljava/lang/String;I)V");
      if (isJavaExceptionThrown())
        return;
    }

  jstring jdeclClass = makeJString("native");
  if (isJavaExceptionThrown())
    return;

  char *tmp_path;
  char *path = svn_dirent_dirname(err->file, err->pool);
  while ((tmp_path = strchr(path, '/')))
    *tmp_path = '.';

  jstring jmethodName = makeJString(path);
  if (isJavaExceptionThrown())
    return;

  jstring jfileName = makeJString(svn_dirent_basename(err->file, err->pool));
  if (isJavaExceptionThrown())
    return;

  jobject jelement = env->NewObject(stClazz, ctor_mid, jdeclClass, jmethodName,
                                    jfileName, (jint) err->line);

  stackTrace.push_back(jelement);

  env->DeleteLocalRef(stClazz);
  env->DeleteLocalRef(jdeclClass);
  env->DeleteLocalRef(jmethodName);
  env->DeleteLocalRef(jfileName);
}

namespace {
struct MessageStackItem
{
  apr_status_t m_code;
  std::string m_message;
  bool m_generic;

  MessageStackItem(apr_status_t code, const char* message,
                   bool generic = false)
    : m_code(code),
      m_message(message),
      m_generic(generic)
    {}
};
typedef std::vector<MessageStackItem> ErrorMessageStack;

/*
 * Build the error message from the svn error into buffer.  This
 * method iterates through all the chained errors
 *
 * @param err               the subversion error
 * @param buffer            the buffer where the formated error message will
 *                          be stored
 * @return An array of error codes and messages
 */
ErrorMessageStack assemble_error_message(
    svn_error_t *err, std::string &result)
{
  // buffer for a single error message
  char errbuf[1024];
  apr_status_t parent_apr_err = 0;
  ErrorMessageStack message_stack;

  /* Pretty-print the error */
  /* Note: we can also log errors here someday. */

  for (int depth = 0; err;
       ++depth, parent_apr_err = err->apr_err, err = err->child)
    {
      /* When we're recursing, don't repeat the top-level message if its
       * the same as before. */
      if ((depth == 0 || err->apr_err != parent_apr_err)
          && err->apr_err != SVN_ERR_JAVAHL_WRAPPED)
        {
          const char *message;
          /* Is this a Subversion-specific error code? */
          if ((err->apr_err > APR_OS_START_USEERR)
              && (err->apr_err <= APR_OS_START_CANONERR))
            message = svn_strerror(err->apr_err, errbuf, sizeof(errbuf));
          /* Otherwise, this must be an APR error code. */
          else
            {
              /* Messages coming from apr_strerror are in the native
                 encoding, it's a good idea to convert them to UTF-8. */
              apr_strerror(err->apr_err, errbuf, sizeof(errbuf));
              svn_error_t* utf8_err =
                svn_utf_cstring_to_utf8(&message, errbuf, err->pool);
              if (utf8_err)
                {
                  /* Use fuzzy transliteration instead. */
                  svn_error_clear(utf8_err);
                  message = svn_utf_cstring_from_utf8_fuzzy(errbuf, err->pool);
                }
            }

          message_stack.push_back(
              MessageStackItem(err->apr_err, message, true));
        }
      if (err->message)
        {
          message_stack.push_back(
              MessageStackItem(err->apr_err, err->message));
        }
    }

  for (ErrorMessageStack::const_iterator it = message_stack.begin();
       it != message_stack.end(); ++it)
    {
      if (!it->m_generic)
        result += "svn: ";
      result += it->m_message;
      result += '\n';
    }
  return message_stack;
}

jobject construct_Jmessage_stack(const ErrorMessageStack& message_stack)
{
  JNIEnv *env = JNIUtil::getEnv();
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass list_clazz = env->FindClass("java/util/ArrayList");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jmethodID mid = env->GetMethodID(list_clazz, "<init>", "(I)V");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jmethodID add_mid = env->GetMethodID(list_clazz, "add",
                                       "(Ljava/lang/Object;)Z");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jlist = env->NewObject(list_clazz, mid, jint(message_stack.size()));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jclass clazz = env->FindClass(JAVAHL_CLASS("/ClientException$ErrorMessage"));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  mid = env->GetMethodID(clazz, "<init>",
                         "(ILjava/lang/String;Z)V");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  for (ErrorMessageStack::const_iterator it = message_stack.begin();
       it != message_stack.end(); ++it)
    {
      jobject jmessage = JNIUtil::makeJString(it->m_message.c_str());
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
      jobject jitem = env->NewObject(clazz, mid,
                                     jint(it->m_code), jmessage,
                                     jboolean(it->m_generic));
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
      env->CallBooleanMethod(jlist, add_mid, jitem);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jmessage);
      env->DeleteLocalRef(jitem);
    }
  return env->PopLocalFrame(jlist);
}
} // anonymous namespace

std::string JNIUtil::makeSVNErrorMessage(svn_error_t *err,
                                         jstring *jerror_message,
                                         jobject *jmessage_stack)
{
  if (jerror_message)
    *jerror_message = NULL;
  if (jmessage_stack)
    *jmessage_stack = NULL;

  std::string buffer;
  err = svn_error_purge_tracing(err);
  if (err == NULL || err->apr_err == 0
      || !(jerror_message || jmessage_stack))
  return buffer;

  ErrorMessageStack message_stack = assemble_error_message(err, buffer);
  if (jerror_message)
    *jerror_message = makeJString(buffer.c_str());
  if (jmessage_stack)
    *jmessage_stack = construct_Jmessage_stack(message_stack);
  return buffer;
}

jthrowable JNIUtil::wrappedCreateClientException(svn_error_t *err, jthrowable jcause)
{
  jstring jmessage;
  jobject jstack;
  std::string msg = makeSVNErrorMessage(err, &jmessage, &jstack);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  const char *source = NULL;
#ifdef SVN_DEBUG
#ifndef SVN_ERR__TRACING
  if (err->file)
    {
      std::ostringstream buf;
      buf << err->file;
      if (err->line > 0)
        buf << ':' << err->line;
      source = buf.str().c_str();
    }
#endif
#endif

  if (!jcause)
    jcause = JNIUtil::unwrapJavaException(err);

  // Much of the following is stolen from throwNativeException().  As much as
  // we'd like to call that function, we need to do some manual stack
  // unrolling, so it isn't feasible.

  JNIEnv *env = getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVAHL_CLASS("/ClientException"));
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (getLogLevel() >= exceptionLog)
    {
      JNICriticalSection cs(*g_logMutex);
      g_logStream << "Subversion JavaHL exception thrown, message:<";
      g_logStream << msg << ">";
      if (source)
        g_logStream << " source:<" << source << ">";
      if (err->apr_err != -1)
        g_logStream << " apr-err:<" << err->apr_err << ">";
      g_logStream << std::endl;
    }
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jsource = makeJString(source);
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jmethodID mid = env->GetMethodID(clazz, "<init>",
                                   "(Ljava/lang/String;"
                                   "Ljava/lang/Throwable;"
                                   "Ljava/lang/String;I"
                                   "Ljava/util/List;)V");
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject nativeException = env->NewObject(clazz, mid, jmessage, jcause,
                                           jsource, jint(err->apr_err),
                                           jstack);
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

#ifdef SVN_ERR__TRACING
  // Add all the C error stack trace information to the Java Exception

  // Get the standard stack trace, and vectorize it using the Array class.
  static jmethodID mid_gst = 0;
  if (mid_gst == 0)
    {
      mid_gst = env->GetMethodID(clazz, "getStackTrace",
                                 "()[Ljava/lang/StackTraceElement;");
      if (isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }
  Array stackTraceArray((jobjectArray) env->CallObjectMethod(nativeException,
                                                             mid_gst));
  std::vector<jobject> oldStackTrace = stackTraceArray.vector();

  // Build the new stack trace elements from the chained errors.
  std::vector<jobject> newStackTrace;
  putErrorsInTrace(err, newStackTrace);

  // Join the new elements with the old ones
  for (std::vector<jobject>::const_iterator it = oldStackTrace.begin();
            it < oldStackTrace.end(); ++it)
    {
      newStackTrace.push_back(*it);
    }

  jclass stClazz = env->FindClass("java/lang/StackTraceElement");
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  const jsize stSize = static_cast<jsize>(newStackTrace.size());
  if (stSize < 0 || stSize != newStackTrace.size())
    {
      env->ThrowNew(env->FindClass("java.lang.ArithmeticException"),
                    "Overflow converting C size_t to JNI jsize");
      POP_AND_RETURN_NULL;
    }
  jobjectArray jStackTrace = env->NewObjectArray(stSize, stClazz, NULL);
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  int i = 0;
  for (std::vector<jobject>::const_iterator it = newStackTrace.begin();
       it < newStackTrace.end(); ++it)
    {
      env->SetObjectArrayElement(jStackTrace, i, *it);
      ++i;
    }

  // And put the entire trace back into the exception
  static jmethodID mid_sst = 0;
  if (mid_sst == 0)
    {
      mid_sst = env->GetMethodID(clazz, "setStackTrace",
                                 "([Ljava/lang/StackTraceElement;)V");
      if (isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }
  env->CallVoidMethod(nativeException, mid_sst, jStackTrace);
  if (isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
#endif

  return static_cast<jthrowable>(env->PopLocalFrame(nativeException));
}

jthrowable JNIUtil::createClientException(svn_error_t *err, jthrowable jcause)
{
  jthrowable jexc = NULL;
  try {
    jexc = wrappedCreateClientException(err, jcause);
  } catch (...) {
    svn_error_clear(err);
    throw;
  }
  svn_error_clear(err);
  return jexc;
}

void JNIUtil::handleSVNError(svn_error_t *err, jthrowable jcause)
{
  jthrowable jexc = createClientException(err, jcause);
  if (jexc)
    getEnv()->Throw(jexc);
}

void JNIUtil::putFinalizedClient(SVNBase *object)
{
  enqueueForDeletion(object);
}

void JNIUtil::enqueueForDeletion(SVNBase *object)
{
  JNICriticalSection cs(*g_finalizedObjectsMutex);
  if (!isExceptionThrown())
    g_finalizedObjects.push_back(object);
}

/**
 * Handle an apr error (those are not expected) by throwing an error.
 * @param error the apr error number
 * @param op the apr function returning the error
 */
void JNIUtil::handleAPRError(int error, const char *op)
{
  char buffer[2048];

  apr_snprintf(buffer, sizeof(buffer),
               _("an error occurred in function %s with return value %d"),
               op, error);

  throwError(buffer);
}

namespace {
const char* known_exception_to_cstring(apr_pool_t* pool)
{
  JNIEnv *env = JNIUtil::getEnv();
  jthrowable t = env->ExceptionOccurred();
  jclass cls = env->GetObjectClass(t);

  jstring jclass_name;
  {
    jmethodID mid = env->GetMethodID(cls, "getClass", "()Ljava/lang/Class;");
    jobject clsobj = env->CallObjectMethod(t, mid);
    jclass basecls = env->GetObjectClass(clsobj);
    mid = env->GetMethodID(basecls, "getName", "()Ljava/lang/String;");
    jclass_name = (jstring) env->CallObjectMethod(clsobj, mid);
  }

  jstring jmessage;
  {
    jmethodID mid = env->GetMethodID(cls, "getMessage",
                                     "()Ljava/lang/String;");
    jmessage = (jstring) env->CallObjectMethod(t, mid);
  }

  JNIStringHolder class_name(jclass_name);
  if (jmessage)
    {
      JNIStringHolder message(jmessage);
      return apr_pstrcat(pool, class_name.c_str(), ": ", message.c_str(), NULL);
    }
  else
    return class_name.pstrdup(pool);
  // ### Conditionally add t.printStackTrace() to msg?
}

const char* exception_to_cstring(apr_pool_t* pool)
{
  const char *msg;
  if (JNIUtil::getEnv()->ExceptionCheck())
    {
      msg = known_exception_to_cstring(pool);
    }
  else
    {
      msg = NULL;
    }
  return msg;
}
} // anonymous namespace

const char *
JNIUtil::thrownExceptionToCString(SVN::Pool &in_pool)
{
  return exception_to_cstring(in_pool.getPool());
}

svn_error_t*
JNIUtil::checkJavaException(apr_status_t errorcode)
{
  if (!getEnv()->ExceptionCheck())
    return SVN_NO_ERROR;
  svn_error_t* err = svn_error_create(errorcode, NULL, NULL);
  const char* const msg = known_exception_to_cstring(err->pool);
  if (msg)
    err->message = apr_psprintf(err->pool, _("Java exception: %s"), msg);
  else
    err->message = _("Java exception");

  
  /* ### TODO: Use apr_pool_userdata_set() on the pool we just created
               for the error chain to keep track of the actual Java
               exception while the error is inside Subversion.

               Once the error chain re-enters JavaHL we can check
               if there is a true exception that we can add to the chain.

               If the error is cleared in Subversion (which may happen
               during composing error chains, etc.) the cleanup handler
               handles properly releasing the exception.

    apr_status_t
    apr_pool_userdata_set(const void *data,
                          const char *key,
                          apr_status_t (*cleanup)(void *),
                          apr_pool_t *pool)
   */
  return err;
}

/**
 * Create a Java string from a native UTF-8 string.
 * @param txt   native UTF-8 string
 * @return the Java string. It is a local reference, which should be deleted
 *         as soon a possible
 */
jstring JNIUtil::makeJString(const char *txt)
{
  if (txt == NULL)
    // A NULL pointer is equates to a null java.lang.String.
    return NULL;

  JNIEnv *env = getEnv();
  return env->NewStringUTF(txt);
}

/**
 * Initialite the log file.
 * @param level the log level
 * @param the name of the log file
 */
void JNIUtil::initLogFile(int level, jstring path)
{
  // lock this operation
  JNICriticalSection cs(*g_logMutex);
  if (g_logLevel > noLog) // if the log file has been opened
    g_logStream.close();

  // remember the log level
  g_logLevel = level;
  JNIStringHolder myPath(path);
  if (g_logLevel > noLog) // if a new log file is needed
    {
      // open it
      g_logStream.open(myPath, std::ios::app);
    }
}

/**
 * Returns the current log level.
 * @return the log level
 */
int JNIUtil::getLogLevel()
{
  return g_logLevel;
}

/**
 * Write a message to the log file if needed.
 * @param the log message
 */
void JNIUtil::logMessage(const char *message)
{
  // lock the log file
  JNICriticalSection cs(*g_logMutex);
  g_logStream << message << std::endl;
}

/**
 * Create a java.util.Date object from an apr time.
 * @param time  the apr time
 * @return the java.util.Date. This is a local reference.  Delete as
 *         soon as possible
 */
jobject JNIUtil::createDate(apr_time_t time)
{
  jlong javatime = time /1000;
  JNIEnv *env = getEnv();
  jclass clazz = env->FindClass("java/util/Date");
  if (isJavaExceptionThrown())
    return NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>", "(J)V");
      if (isJavaExceptionThrown())
        return NULL;
    }

  jobject ret = env->NewObject(clazz, mid, javatime);
  if (isJavaExceptionThrown())
    return NULL;

  env->DeleteLocalRef(clazz);

  return ret;
}

apr_time_t
JNIUtil::getDate(jobject jdate)
{
  JNIEnv *env = getEnv();
  jclass clazz = env->FindClass("java/util/Date");
  if (isJavaExceptionThrown())
    return 0;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "getTime", "()J");
      if (isJavaExceptionThrown())
        return 0;
    }

  jlong jmillis = env->CallLongMethod(jdate, mid);
  if (isJavaExceptionThrown())
    return 0;

  env->DeleteLocalRef(clazz);

  return jmillis * 1000;
}

/**
 * Create a Java byte array from an array of characters.
 * @param data      the character array
 * @param length    the number of characters in the array
 */
jbyteArray JNIUtil::makeJByteArray(const void *data, int length)
{
  // a NULL will create no Java array
  if (!data)
    return NULL;

  JNIEnv *env = getEnv();

  // Allocate the Java array.
  jbyteArray ret = env->NewByteArray(length);
  if (isJavaExceptionThrown() || ret == NULL)
      return NULL;

  // Access the bytes.
  jbyte *retdata = env->GetByteArrayElements(ret, NULL);
  if (isJavaExceptionThrown())
    return NULL;

  // Copy the bytes.
  memcpy(retdata, data, length);

  // Release the bytes.
  env->ReleaseByteArrayElements(ret, retdata, 0);
  if (isJavaExceptionThrown())
    return NULL;

  return ret;
}

/**
 * Create a Java byte array from an svn_string_t.
 * @param str       the string
 */
jbyteArray JNIUtil::makeJByteArray(const svn_string_t *str)
{
  // a NULL will create no Java array
  if (!str)
    return NULL;

  return JNIUtil::makeJByteArray(str->data, static_cast<int>(str->len));
}

/**
 * Throw a Java NullPointerException.  Used when input parameters
 * which should not be null are that.
 *
 * @param message   the name of the parameter that is null
 */
void JNIUtil::throwNullPointerException(const char *message)
{
  if (getLogLevel() >= errorLog)
    logMessage("NullPointerException thrown");

  JNIEnv *env = getEnv();
  jclass clazz = env->FindClass("java/lang/NullPointerException");
  if (isJavaExceptionThrown())
    return;

  env->ThrowNew(clazz, message);
}

svn_error_t *JNIUtil::preprocessPath(const char *&path, apr_pool_t *pool)
{
  /* URLs and wc-paths get treated differently. */
  if (svn_path_is_url(path))
    {
      /* No need to canonicalize a URL's case or path separators. */

      /* Convert to URI. */
      path = svn_path_uri_from_iri(path, pool);

      /* Auto-escape some ASCII characters. */
      path = svn_path_uri_autoescape(path, pool);

      /* The above doesn't guarantee a valid URI. */
      if (! svn_path_is_uri_safe(path))
        return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                                 _("URL '%s' is not properly URI-encoded"),
                                 path);

      /* Verify that no backpaths are present in the URL. */
      if (svn_path_is_backpath_present(path))
        return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                                 _("URL '%s' contains a '..' element"),
                                 path);

      /* strip any trailing '/' */
      path = svn_uri_canonicalize(path, pool);
    }
  else  /* not a url, so treat as a path */
    {
      /* Normalize path to subversion internal style */

      /* ### In Subversion < 1.6 this method on Windows actually tried
         to lookup the path on disk to fix possible invalid casings in
         the passed path. (An extremely expensive operation; especially
         on network drives).

         This 'feature'is now removed as it penalizes every correct
         path passed, and also breaks behavior of e.g.
           'svn status .' returns '!' file, because there is only a "File"
             on disk.
            But when you then call 'svn status file', you get '? File'.

         As JavaHL is designed to be platform independent I assume users
         don't want this broken behavior on non round-trippable paths, nor
         the performance penalty.
       */

      path = svn_dirent_internal_style(path, pool);

      /* For kicks and giggles, let's absolutize it. */
      SVN_ERR(svn_dirent_get_absolute(&path, path, pool));
    }

  return NULL;
}

/* Tag to use on the apr_pool_t to store a WrappedException reference */
static const char *WrapExceptionTag = "org.apache.subversion.JavaHL.svnerror";

class WrappedException
{
  JNIEnv *m_env;
  jthrowable m_exception;
#ifdef SVN_DEBUG
  bool m_fetched;
#endif
public:
  WrappedException(JNIEnv *env)
  {
    m_env = env;

    // Fetch exception inside local frame
    jthrowable exceptionObj = env->ExceptionOccurred();

    // Now clear exception status
    env->ExceptionClear();

    // As adding a reference in exception state fails
    m_exception = static_cast<jthrowable>(env->NewGlobalRef(exceptionObj));

#ifdef SVN_DEBUG
    m_fetched = false;
#endif
  }

  static jthrowable get_exception(apr_pool_t *pool)
  {
      void *data;
      if (! apr_pool_userdata_get(&data, WrapExceptionTag, pool))
      {
          WrappedException *we = reinterpret_cast<WrappedException *>(data);

          if (we)
          {
#ifdef SVN_DEBUG
              we->m_fetched = TRUE;
#endif
              // Create reference in local frame, as the pool will be cleared
              return static_cast<jthrowable>(
                            we->m_env->NewLocalRef(we->m_exception));
          }
      }
      return NULL;
  }

private:
  ~WrappedException()
  {
#ifdef SVN_DEBUG
      if (!m_fetched)
          SVN_DBG(("Cleared svn_error_t * before Java exception was fetched"));
#endif
      m_env->DeleteGlobalRef(m_exception);
  }
public:
  static apr_status_t cleanup(void *data)
  {
    WrappedException *we = reinterpret_cast<WrappedException *>(data);

    delete we;
    return APR_SUCCESS;
  }
};

svn_error_t* JNIUtil::wrapJavaException()
{
  if (!isExceptionThrown())
    return SVN_NO_ERROR;

  svn_error_t *err = svn_error_create(SVN_ERR_JAVAHL_WRAPPED, NULL,
                                      "Wrapped Java Exception");
  apr_pool_userdata_set(new WrappedException(getEnv()), WrapExceptionTag,
                        WrappedException::cleanup, err->pool);
  return err;
}

jthrowable JNIUtil::unwrapJavaException(const svn_error_t *err)
{
    if (!err)
        return NULL;
    return
        WrappedException::get_exception(err->pool);
}
