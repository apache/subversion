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

#include <svn_pools.h>
#include <svn_config.h>
#include "svn_path.h"
#include "svn_private_config.h"
#ifdef WIN32
/* FIXME: We're using an internal APR header here, which means we
   have to build Subversion with APR sources. This being Win32-only,
   that should be fine for now, but a better solution must be found in
   combination with issue #850. */
extern "C" {
#include "arch/win32/apr_arch_utf8.h"
};
#endif

#include "SVNBase.h"
#include "JNIMutex.h"
#include "JNICriticalSection.h"
#include "JNIThreadData.h"
#include "JNIStringHolder.h"

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

bool JNIUtil::JNIInit(JNIEnv *env)
{
	static bool run = false;
	if(run) 
	{
		env->ExceptionClear();
		setEnv(env);
		JNICriticalSection cs(*g_finalizedObjectsMutex) ;
		if(isExceptionThrown())
		{
			return false;
		}

		for(std::list<SVNBase*>::iterator it = g_finalizedObjects.begin(); it != g_finalizedObjects.end(); it++)
		{
			delete *it;
		}
		g_finalizedObjects.clear();

		return true;
	}
	run = true;
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
        /* get exe name - our locale info will be in '../share/locale' */
        inwords = sizeof (ucs2_path) / sizeof(ucs2_path[0]);
        HINSTANCE moduleHandle = GetModuleHandle("svnjavahl-1");
        GetModuleFileNameW (moduleHandle, ucs2_path, inwords);
        inwords = lstrlenW (ucs2_path);
        outbytes = outlength = 3 * (inwords + 1);
        utf8_path = (char *)apr_palloc (pool, outlength);
        apr_err = apr_conv_ucs2_to_utf8 (ucs2_path, &inwords,
                                         utf8_path, &outbytes);
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

	svn_error_t *err = svn_config_ensure (NULL, g_pool); // we use the default directory for config files
	if (err)
	{
		svn_pool_destroy (g_pool);
		handleSVNError(err);
		return false;
	}

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

apr_pool_t * JNIUtil::getPool()
{
	return g_pool;
}

JNIMutex *JNIUtil::getGlobalPoolMutex()
{
    return g_globalPoolMutext;
}

void JNIUtil::throwError(const char *message)
{
	if(getLogLevel() >= errorLog)
	{
		JNICriticalSection cs(*g_logMutex);
		g_logStream << "Error thrown <" << message << ">" << std::endl;
	}
	JNIEnv *env = getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/JNIError");
	if(isJavaExceptionThrown())
	{
		return;
	}
	env->ThrowNew(clazz, message);
	setExceptionThrown();
	env->DeleteLocalRef(clazz);
}

void JNIUtil::handleSVNError(svn_error_t *err)
{
	JNIEnv *env = getEnv();
	jclass clazz = env->FindClass(JAVA_PACKAGE"/ClientException");
	if(getLogLevel() >= exceptionLog)
	{
		JNICriticalSection cs(*g_logMutex);
		g_logStream << "Error SVN exception thrown message:<";
		g_logStream << err->message << "> file:<" << err->file << "> apr-err:<" << err->apr_err;
		g_logStream	<< ">" << std::endl;
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
	jmethodID mid = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;Ljava/lang/String;I)V");
	if(isJavaExceptionThrown())
	{
		svn_error_clear(err);
		return;
	}
	jobject error = env->NewObject(clazz, mid, jmessage, jfile, static_cast<jint>(err->apr_err));
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


void JNIUtil::putFinalizedClient(SVNBase *cl)
{
	if(getLogLevel() >= errorLog)
	{
		JNICriticalSection cs(*g_logMutex);
		g_logStream << "a client object was not disposed" << std::endl;
	}
	JNICriticalSection cs(*g_finalizedObjectsMutex);
	if(isExceptionThrown())
	{
		return;
	}

	g_finalizedObjects.push_back(cl);

}

void JNIUtil::handleAPRError(int error, const char *op)
{
	char *buffer = getFormatBuffer();
	if(buffer == NULL)
	{
		return;
	}
    apr_snprintf(buffer, formatBufferSize, _("an error occurred in function %s with return value %d"),
		op, error);

	throwError(buffer);
}

bool JNIUtil::isExceptionThrown()
{
	if(g_inInit)
	{
		return g_initException;
	}
	JNIThreadData *data = JNIThreadData::getThreadData();
	return data == NULL || data->m_exceptionThrown;
}

void JNIUtil::setEnv(JNIEnv *env)
{
	JNIThreadData *data = JNIThreadData::getThreadData();
	data->m_env = env;
	data->m_exceptionThrown = false;
}

JNIEnv * JNIUtil::getEnv()
{
	if(g_inInit)
	{
		return g_initEnv;
	}
	JNIThreadData *data = JNIThreadData::getThreadData();
	return data->m_env;
}

bool JNIUtil::isJavaExceptionThrown()
{
	JNIEnv *env = getEnv();
	if(env->ExceptionCheck())
	{
		jthrowable exp = env->ExceptionOccurred();
		env->ExceptionDescribe();
		env->Throw(exp);
		env->DeleteLocalRef(exp);
		return true;
	}
	return false;
}

jstring JNIUtil::makeJString(const char *txt)
{
	if(txt == NULL)
	{
		return NULL;
	}
	JNIEnv *env = getEnv();
	jstring js = env->NewStringUTF(txt);
	return js;
}

void JNIUtil::setExceptionThrown()
{
	if(g_inInit)
	{
		g_initException = true;
	}
	JNIThreadData *data = JNIThreadData::getThreadData();
	data->m_exceptionThrown = true;
}

void JNIUtil::initLogFile(int level, jstring path)
{
	JNICriticalSection cs(*g_logMutex);
	if(g_logLevel > noLog)
	{
		g_logStream.close();
	}
	g_logLevel = level;
	JNIStringHolder myPath(path);
	if(g_logLevel > noLog)
	{
		g_logStream.open(myPath, std::ios::app);
	}
}

char * JNIUtil::getFormatBuffer()
{
	if(g_inInit)
	{
		return g_initFormatBuffer;
	}
	JNIThreadData *data = JNIThreadData::getThreadData();
	if(data == NULL)
	{
		return g_initFormatBuffer;
	}
	return data->m_formatBuffer;
}

int JNIUtil::getLogLevel()
{
	return g_logLevel;
}

void JNIUtil::logMessage(const char *message)
{
	JNICriticalSection cs(*g_logMutex);
	g_logStream << message << std::endl;
}

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

Pool * JNIUtil::getRequestPool()
{
    return JNIThreadData::getThreadData()->m_requestPool;
}

void JNIUtil::setRequestPool(Pool *pool)
{
	JNIThreadData::getThreadData()->m_requestPool = pool;
}

jbyteArray JNIUtil::makeJByteArray(const signed char *data, int length)
{
	if(data == NULL || length == 0)
	{
		return NULL;
	}
	JNIEnv *env = getEnv();
	jbyteArray ret = env->NewByteArray(length);
	if(isJavaExceptionThrown())
	{
		return NULL;
	}
	jbyte *retdata = env->GetByteArrayElements(ret, NULL);
	if(isJavaExceptionThrown())
	{
		return NULL;
	}
	memcpy(retdata, data, length);
	env->ReleaseByteArrayElements(ret, retdata, 0);
	if(isJavaExceptionThrown())
	{
		return NULL;
	}
	return ret;
}

void JNIUtil::assembleErrorMessage(svn_error_t *err, int depth, apr_status_t parent_apr_err, std::string &buffer)
{
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

void JNIUtil::throwNullPointerException(const char *message)
{
	if(getLogLevel() >= errorLog)
	{
		JNICriticalSection cs(*g_logMutex);
		g_logStream << "NullPointerException thrown" << std::endl;
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
