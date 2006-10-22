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
 *
 * @file JNIUtil.cpp
 * @brief Implementation of the class JNIUtil
 */

#include "JNIUtil.h"
#include <locale.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_general.h>
#include <apr_lib.h>

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_path.h"
#include <apr_file_info.h>
#include "svn_private_config.h"
#ifdef WIN32
/* FIXME: We're using an internal APR header here, which means we
   have to build Subversion with APR sources. This being Win32-only,
   that should be fine for now, but a better solution must be found in
   combination with issue #850. */
extern "C" {
#include <arch/win32/apr_arch_utf8.h>
};
#endif

#include "SVNBase.h"
#include "JNIMutex.h"
#include "JNICriticalSection.h"
#include "JNIThreadData.h"
#include "JNIStringHolder.h"

// static members of JNIUtil are allocated here
apr_pool_t *JNIUtil::g_pool = NULL;
std::list<SVNBase*> JNIUtil::g_finalizedObjects;
JNIMutex *JNIUtil::g_finalizedObjectsMutex = NULL;
JNIMutex *JNIUtil::g_logMutex = NULL;
JNIMutex *JNIUtil::g_globalPoolMutext = NULL;
bool JNIUtil::g_initException;
bool JNIUtil::g_inInit;
JNIEnv *JNIUtil::g_initEnv;
char JNIUtil::g_initFormatBuffer[formatBufferSize];
int JNIUtil::g_logLevel = JNIUtil::noLog;
std::ofstream JNIUtil::g_logStream;

/**
 * initialize the environment for all requests
 * @param env   the JNI environment for this request
 */
bool JNIUtil::JNIInit(JNIEnv *env)
{
    // clear all standing exceptions.
    env->ExceptionClear();

    // remember the env paramater for the remainder of the request
    setEnv(env);

    // lock the list of finalized objects
    JNICriticalSection cs(*g_finalizedObjectsMutex) ;
    if(isExceptionThrown())
    {
        return false;
    }

    // delete all finalized, but not yet deleted objects
    for(std::list<SVNBase*>::iterator it = g_finalizedObjects.begin(); 
        it != g_finalizedObjects.end(); it++)
    {
        delete *it;
    }
    g_finalizedObjects.clear();

    return true;
}
/**
 * initialize the environment for all requests
 * @param env   the JNI environment for this request
 */
bool JNIUtil::JNIGlobalInit(JNIEnv *env)
{
	// this method has to be run only once during the run a 
    // programm
    static bool run = false;
    if(run) // already run
    {
		return true;
    }
    run = true;
    // do not run this part more than one time. 
    // this leaves a small time window when two threads create their first
    // SVNClient & SVNAdmin at the same time, but I do not see a better 
    // option without APR already initialized
    if(g_inInit)
    {
        return false;
    }
    g_inInit = true;
    g_initEnv = env;

    apr_status_t status;

    /* C programs default to the "C" locale. But because svn is supposed
       to be i18n-aware, it should inherit the default locale of its
       environment.  */
    if (!setlocale(LC_ALL, ""))
    {
        if (stderr)
        {
            const char *env_vars[] = { "LC_ALL", "LC_CTYPE", "LANG", NULL };
            const char **env_var = &env_vars[0], *env_val = NULL;
            while (*env_var)
            {
                env_val = getenv(*env_var);
                if (env_val && env_val[0])
                    break;
                ++env_var;
            }

            if (!*env_var)
            {
                /* Unlikely. Can setlocale fail if no env vars are set? */
                --env_var;
                env_val = "not set";
            }

            fprintf(stderr,
                  "%s: error: cannot set LC_ALL locale\n"
                  "%s: error: environment variable %s is %s\n"
                  "%s: error: please check that your locale name is correct\n",
                  "svnjavahl", "svnjavahl", *env_var, env_val, "svnjavahl");
        }
        return FALSE;
    }

    /* Initialize the APR subsystem, and register an atexit() function
       to Uninitialize that subsystem at program exit. */
    status = apr_initialize();
    if (status)
    {
        if (stderr)
        {
            char buf[1024];
            apr_strerror(status, buf, sizeof(buf) - 1);
            fprintf(stderr,
                  "%s: error: cannot initialize APR: %s\n",
                  "svnjavahl", buf);
        }
        return FALSE;
    }

    if (0 > atexit(apr_terminate))
    {
        if (stderr)
            fprintf(stderr,
                "%s: error: atexit registration failed\n",
                "svnjavahl");
        return FALSE;
    }

#ifdef ENABLE_NLS
#ifdef WIN32
    {
        WCHAR ucs2_path[MAX_PATH];
        char* utf8_path;
        const char* internal_path;
        apr_pool_t* pool;
        apr_status_t apr_err;
        unsigned int inwords, outbytes, outlength;

        apr_pool_create (&pool, 0);
        /* get dll name - our locale info will be in '../share/locale' */
        inwords = sizeof (ucs2_path) / sizeof(ucs2_path[0]);
        HINSTANCE moduleHandle = GetModuleHandle("libsvnjavahl-1");
        GetModuleFileNameW (moduleHandle, ucs2_path, inwords);
        inwords = lstrlenW (ucs2_path);
        outbytes = outlength = 3 * (inwords + 1);
        utf8_path = (char *)apr_palloc (pool, outlength);
        apr_err = apr_conv_ucs2_to_utf8 ((const apr_wchar_t *) ucs2_path,
                                         &inwords, utf8_path, &outbytes);
        if (!apr_err && (inwords > 0 || outbytes == 0))
          apr_err = APR_INCOMPLETE;
        if (apr_err)
        {
          if (stderr)
            fprintf (stderr, "Can't convert module path to UTF-8");
          return FALSE;
        }
        utf8_path[outlength - outbytes] = '\0';
        internal_path = svn_path_internal_style (utf8_path, pool);
        /* get base path name */
        internal_path = svn_path_dirname (internal_path, pool);
        internal_path = svn_path_join (internal_path, SVN_LOCALE_RELATIVE_PATH,
                                       pool);
        bindtextdomain (PACKAGE_NAME, internal_path);
        apr_pool_destroy (pool);
    }
#else
    bindtextdomain(PACKAGE_NAME, SVN_LOCALE_DIR);
#endif
    textdomain(PACKAGE_NAME);
#endif

    /* Create our top-level pool. */
    g_pool = svn_pool_create (NULL);

#if defined(WIN32) || defined(__CYGWIN__)
    /* See http://svn.collab.net/repos/svn/trunk/notes/asp-dot-net-hack.txt */
    /* ### This code really only needs to be invoked by consumers of
       ### the libsvn_wc library, which basically means SVNClient. */
    if (getenv ("SVN_ASP_DOT_NET_HACK"))
    {
        svn_error_t *err = svn_wc_set_adm_dir("_svn", g_pool);
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

    // build all mutexes
    g_finalizedObjectsMutex = new JNIMutex(g_pool);
    if(isExceptionThrown())
    {
        return false;
    }

    g_logMutex = new JNIMutex(g_pool);
    if(isExceptionThrown())
    {
        return false;
    }

    g_globalPoolMutext = new JNIMutex(g_pool);
    if(isExceptionThrown())
    {
        return false;
    }

    // initialized the thread local storage
    if(!JNIThreadData::initThreadData())
    {
        return false;
    }

    setEnv(env);
    if(isExceptionThrown())
    {
        return false;
    }

    g_initEnv = NULL;
    g_inInit = false;
    return true;
}
/**
 * returns the global (not request specific) pool
 * @return global pool
 */
apr_pool_t * JNIUtil::getPool()
{
    return g_pool;
}
/**
 * return the mutex securing the global pool
 * @return the mutex for the global pool
 */
JNIMutex *JNIUtil::getGlobalPoolMutex()
{
    return g_globalPoolMutext;
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
    {
        return;
    }
    env->ThrowNew(clazz, message);
    setExceptionThrown();
    env->DeleteLocalRef(clazz);
}
jstring JNIUtil::makeSVNErrorMessage(svn_error_t *err)
{
    if(err == NULL)
        return NULL;
    std::string buffer;
    assembleErrorMessage(err, 0, APR_SUCCESS, buffer);
    jstring jmessage = makeJString(buffer.c_str());
    return jmessage;
}
/**
 * process a svn error by wraping in into a ClientExpection
 * and throwing that
 * @param err   the error to by handled
 */
void JNIUtil::handleSVNError(svn_error_t *err)
{
    JNIEnv *env = getEnv();
    jclass clazz = env->FindClass(JAVA_PACKAGE"/ClientException");
    if(getLogLevel() >= exceptionLog)
    {
        JNICriticalSection cs(*g_logMutex);
        g_logStream << "Error SVN exception thrown message:<";
        g_logStream << err->message << "> file:<" << err->file <<"> apr-err:<";
        g_logStream << err->apr_err << ">" << std::endl;
    }
    if(isJavaExceptionThrown())
    {
        svn_error_clear(err);
        return;
    }

    std::string buffer;
    assembleErrorMessage(err, 0, APR_SUCCESS, buffer);
    jstring jmessage = makeJString(buffer.c_str());
    if(isJavaExceptionThrown())
    {
        svn_error_clear(err);
        return;
    }
    if(isJavaExceptionThrown())
    {
        svn_error_clear(err);
        return;
    }
    jstring jfile = makeJString(err->file);
    if(isJavaExceptionThrown())
    {
        svn_error_clear(err);
        return;
    }
    jmethodID mid = env->GetMethodID(clazz, "<init>", 
        "(Ljava/lang/String;Ljava/lang/String;I)V");
    if(isJavaExceptionThrown())
    {
        svn_error_clear(err);
        return;
    }
    jobject error = env->NewObject(clazz, mid, jmessage, jfile, 
        static_cast<jint>(err->apr_err));
    svn_error_clear(err);
    if(isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(clazz);
    if(isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(jmessage);
    if(isJavaExceptionThrown())
    {
        return;
    }
    env->DeleteLocalRef(jfile);
    if(isJavaExceptionThrown())
    {
        return;
    }
    env->Throw(static_cast<jthrowable>(error));
}

void JNIUtil::putFinalizedClient(SVNBase *object)
{
    enqueueForDeletion(object);
}

void JNIUtil::enqueueForDeletion(SVNBase *object)
{
    JNICriticalSection cs(*g_finalizedObjectsMutex);
    if (!isExceptionThrown())
    {
        g_finalizedObjects.push_back(object);
    }
}

/**
 * Handle an apr error (those are not expected) by throwing an error
 * @param error the apr error number
 * @param op the apr function returning the error
 */
void JNIUtil::handleAPRError(int error, const char *op)
{
    char *buffer = getFormatBuffer();
    if(buffer == NULL)
    {
        return;
    }
    apr_snprintf(buffer, formatBufferSize, 
        _("an error occurred in function %s with return value %d"),
        op, error);

    throwError(buffer);
}
/**
 * return is an exception has been detected
 * @return a exception has been detected
 */
bool JNIUtil::isExceptionThrown()
{
    if(g_inInit) // during init -> look in the global member
    {
        return g_initException;
    }

    // look in the thread local storage
    JNIThreadData *data = JNIThreadData::getThreadData();
    return data == NULL || data->m_exceptionThrown;
}
/**
 * store the JNI environment for this request in the thread local storage
 * @param env   the JNI environment
 */
void JNIUtil::setEnv(JNIEnv *env)
{
	JNIThreadData::pushNewThreadData();
    JNIThreadData *data = JNIThreadData::getThreadData();
    data->m_env = env;
    data->m_exceptionThrown = false;
}
/**
 * Return the JNI environment to use
 * @return the JNI environment
 */
JNIEnv * JNIUtil::getEnv()
{
    // during init -> look into the global variable
    if(g_inInit)
    {
        return g_initEnv;
    }

    // look in the thread local storage
    JNIThreadData *data = JNIThreadData::getThreadData();
    return data->m_env;
}
/**
 * check in a java exception has been thrown
 * @return is a java exception has been thrown
 */
bool JNIUtil::isJavaExceptionThrown()
{
    JNIEnv *env = getEnv();
    if(env->ExceptionCheck())
    {
        // retrieving the exception removes it
        // so we rethrow it here
        jthrowable exp = env->ExceptionOccurred();
        env->ExceptionDescribe();
        env->Throw(exp);
        env->DeleteLocalRef(exp);
        setExceptionThrown();
        return true;
    }
    return false;
}
/**
 * create a java string from a native UTF-8 string
 * @param txt   native UTF-8 string
 * @return the java string. It is a local reference, which should be deleted
 *         as soon a possible
 */ 
jstring JNIUtil::makeJString(const char *txt)
{
    if(txt == NULL) // NULL string can be converted to a null java string
    {
        return NULL;
    }
    JNIEnv *env = getEnv();
    jstring js = env->NewStringUTF(txt);
    return js;
}
/**
 * set the flag, that an exception has been thrown
 */
void JNIUtil::setExceptionThrown()
{
    // during init -> store in global variable
    if(g_inInit)
    {
        g_initException = true;
    }
    // store in thread local storage
    JNIThreadData *data = JNIThreadData::getThreadData();
    data->m_exceptionThrown = true;
}
/** 
 * initialite the log file
 * @param level the log level
 * @param the name of the log file
 */
void JNIUtil::initLogFile(int level, jstring path)
{
    // lock this operation
    JNICriticalSection cs(*g_logMutex);
    if(g_logLevel > noLog) // if the log file has been opened
    {
        g_logStream.close();
    }
    // remember the log level
    g_logLevel = level;
    JNIStringHolder myPath(path);
    if(g_logLevel > noLog) // if a new log file is needed
    {
        // open it
        g_logStream.open(myPath, std::ios::app);
    }
}
/**
 * Returns a buffer to format error messages
 * @return a buffer for formating error messages
 */
char * JNIUtil::getFormatBuffer()
{
    if(g_inInit) // during init -> use the global buffer
    {
        return g_initFormatBuffer;
    }
    // use the buffer in the thread local storage
    JNIThreadData *data = JNIThreadData::getThreadData();
    if(data == NULL) // if that does not exists -> use the global buffer
    {
        return g_initFormatBuffer;
    }
    return data->m_formatBuffer;
}
/**
 * Returns the current log level
 * @return the log level
 */
int JNIUtil::getLogLevel()
{
    return g_logLevel;
}
/**
 * write a message to the log file if needed
 * @param the log message
 */
void JNIUtil::logMessage(const char *message)
{
    // lock the log file
    JNICriticalSection cs(*g_logMutex);
    g_logStream << message << std::endl;
}
/**
 * create a java.util.Date object from an apr time
 * @param time  the apr time
 * @return the java.util.Date. This is a local reference. Delete as soon as 
 *          possible
 */
jobject JNIUtil::createDate(apr_time_t time)
{
    jlong javatime = time /1000;
    JNIEnv *env = getEnv();
    jclass clazz = env->FindClass("java/util/Date");
    if(isJavaExceptionThrown())
    {
        return NULL;
    }
    static jmethodID mid = 0;
    if(mid == 0)
    {
        mid = env->GetMethodID(clazz, "<init>", "(J)V");
        if(isJavaExceptionThrown())
        {
            return NULL;
        }
    }
    jobject ret = env->NewObject(clazz, mid, javatime);
    if(isJavaExceptionThrown())
    {
        return NULL;
    }
    env->DeleteLocalRef(clazz);
    if(isJavaExceptionThrown())
    {
        return NULL;
    }
    return ret;
}
/** 
 * Return the request pool. The request pool will be destroyed after each 
 * request (call) 
 * @return the pool to be used for this request
 */
Pool * JNIUtil::getRequestPool()
{
    return JNIThreadData::getThreadData()->m_requestPool;
}
/**
 * Set the request pool in thread local storage
 * @param pool  the request pool
 */
void JNIUtil::setRequestPool(Pool *pool)
{
    JNIThreadData::getThreadData()->m_requestPool = pool;
}

/**
 * create a java byte array from an array of characters.
 * @param data      the character array
 * @param length    the number of characters in the array
 */
jbyteArray JNIUtil::makeJByteArray(const signed char *data, int length)
{
    if(data == NULL || length == 0) // a NULL or empty will create no
                                    // java array
    {
        return NULL;
    }
    JNIEnv *env = getEnv();

    // allocate the java array
    jbyteArray ret = env->NewByteArray(length);
    if(isJavaExceptionThrown())
    {
        return NULL;
    }

    // access the bytes
    jbyte *retdata = env->GetByteArrayElements(ret, NULL);
    if(isJavaExceptionThrown())
    {
        return NULL;
    }

    // copy the bytes 
    memcpy(retdata, data, length);

    // release the bytes
    env->ReleaseByteArrayElements(ret, retdata, 0);
    if(isJavaExceptionThrown())
    {
        return NULL;
    }
    return ret;
}
/**
 * build the error message from the svn error into buffer. This method calls 
 * itselft recursivly for all the chained errors
 *
 * @param err               the subversion error
 * @param depth             the depth of the call, used for formating
 * @param parent_apr_err    the apr of the previous level, used for formating
 * @param buffer            the buffer where the formated error message will
 *                          be stored
 */
void JNIUtil::assembleErrorMessage(svn_error_t *err, int depth, 
                                   apr_status_t parent_apr_err, 
                                   std::string &buffer)
{
    // buffer for a single error message
    char errbuf[256];

    /* Pretty-print the error */
    /* Note: we can also log errors here someday. */

    /* When we're recursing, don't repeat the top-level message if its
       the same as before. */
    if (depth == 0 || err->apr_err != parent_apr_err)
    {
        /* Is this a Subversion-specific error code? */
        if ((err->apr_err > APR_OS_START_USEERR)
            && (err->apr_err <= APR_OS_START_CANONERR))
            buffer.append(svn_strerror (err->apr_err, errbuf, sizeof (errbuf)));
        /* Otherwise, this must be an APR error code. */
        else
            buffer.append(apr_strerror (err->apr_err, errbuf, sizeof (errbuf)));
        buffer.append("\n");
    }
    if (err->message)
        buffer.append(_("svn: ")).append(err->message).append("\n");

    if (err->child)
        assembleErrorMessage(err->child, depth + 1, err->apr_err, buffer);

}
/**
 * Throw a java NullPointerException. Used when input parameters which should
 * not be null are that.
 *
 * @param message   the name of the parameter that is null
 */
void JNIUtil::throwNullPointerException(const char *message)
{
    if (getLogLevel() >= errorLog)
    {
        logMessage("NullPointerException thrown");
    }
    JNIEnv *env = getEnv();
    jclass clazz = env->FindClass("java/lang/NullPointerException");
    if(isJavaExceptionThrown())
    {
        return;
    }
    env->ThrowNew(clazz, message);
    setExceptionThrown();
    env->DeleteLocalRef(clazz);
}
svn_error_t *JNIUtil::preprocessPath(const char *&path, apr_pool_t * pool)
{
  /* URLs and wc-paths get treated differently. */
  if (svn_path_is_url (path))
    {
      /* No need to canonicalize a URL's case or path separators. */

      /* Convert to URI. */
      path = svn_path_uri_from_iri (path, pool);
      /* Auto-escape some ASCII characters. */
      path = svn_path_uri_autoescape (path, pool);

      /* The above doesn't guarantee a valid URI. */
      if (! svn_path_is_uri_safe (path))
        return svn_error_createf (SVN_ERR_BAD_URL, 0,
                                  _("URL '%s' is not properly URI-encoded"),
                                  path);

      /* Verify that no backpaths are present in the URL. */
      if (svn_path_is_backpath_present (path))
        return svn_error_createf (SVN_ERR_BAD_URL, 0,
                                  _("URL '%s' contains a '..' element"),
                                  path);
      
      /* strip any trailing '/' */
      path = svn_path_canonicalize (path, pool);
    }
  else  /* not a url, so treat as a path */
    {
      const char *apr_target;
      char *truenamed_target; /* APR-encoded */
      apr_status_t apr_err;

      /* canonicalize case, and change all separators to '/'. */
      SVN_ERR (svn_path_cstring_from_utf8 (&apr_target, path,
                                           pool));
      apr_err = apr_filepath_merge (&truenamed_target, "", apr_target,
                                    APR_FILEPATH_TRUENAME, pool);

      if (!apr_err)
        /* We have a canonicalized APR-encoded target now. */
        apr_target = truenamed_target;
      else if (APR_STATUS_IS_ENOENT (apr_err))
        /* It's okay for the file to not exist, that just means we
           have to accept the case given to the client. We'll use
           the original APR-encoded target. */
        ;
      else
        return svn_error_createf (apr_err, NULL,
                                  _("Error resolving case of '%s'"),
                                  svn_path_local_style (path,
                                                        pool));

      /* convert back to UTF-8. */
      SVN_ERR (svn_path_cstring_to_utf8 (&path, apr_target, pool));
      path = svn_path_canonicalize (path, pool);

    }
  return NULL;
}
