#include "RaContext.h"
#include "JNIUtil.h"
#include "Prompter.h"

#define STRING_RETURN_SIGNATURE "()Ljava/lang/String;"

RaContext::RaContext(jobject contextHolder, SVN::Pool &pool, jobject jconfig)
    : RaSharedContext(pool), m_raCallbacks(NULL)
{
  /*
   * Extract config properties
   */
  JNIEnv *env = JNIUtil::getEnv();

  static jmethodID midUsername = 0;
  static jmethodID midPassword = 0;
  static jmethodID midConfigDirectory = 0;
  static jmethodID midPrompt = 0;

  if (midUsername == 0 || midPassword == 0 || midConfigDirectory == 0
      || midPrompt == 0)
    {
      jclass clazz = env->FindClass(JAVA_PACKAGE"/ra/ISVNRaConfig");
      if (JNIUtil::isJavaExceptionThrown())
        return;

      midUsername = env->GetMethodID(clazz, "getUsername",
          STRING_RETURN_SIGNATURE);
      if (JNIUtil::isJavaExceptionThrown() || midUsername == 0)
        return;

      midPassword = env->GetMethodID(clazz, "getPassword",
          STRING_RETURN_SIGNATURE);
      if (JNIUtil::isJavaExceptionThrown() || midPassword == 0)
        return;

      midConfigDirectory = env->GetMethodID(clazz, "getConfigDirectory",
          STRING_RETURN_SIGNATURE);
      if (JNIUtil::isJavaExceptionThrown() || midConfigDirectory == 0)
        return;

      midPrompt = env->GetMethodID(clazz, "getPrompt",
          "()Lorg/apache/subversion/javahl/callback/UserPasswordCallback;");
      if (JNIUtil::isJavaExceptionThrown() || midPrompt == 0)
        return;

      env->DeleteLocalRef(clazz);
    }

  jstring jusername = (jstring) env->CallObjectMethod(jconfig, midUsername);
  if (JNIUtil::isExceptionThrown())
    return;

  if (jusername != NULL)
    {
      JNIStringHolder usernameStr(jusername);
      if (JNIUtil::isExceptionThrown())
        {
          return;
        }

      username(usernameStr);

      JNIUtil::getEnv()->DeleteLocalRef(jusername);
    }

  jstring jpassword = (jstring) env->CallObjectMethod(jconfig, midPassword);
  if (JNIUtil::isExceptionThrown())
    return;

  if (jpassword != NULL)
    {
      JNIStringHolder passwordStr(jpassword);
      if (JNIUtil::isExceptionThrown())
        {
          return;
        }

      password(passwordStr);

      JNIUtil::getEnv()->DeleteLocalRef(jpassword);
    }

  jstring jconfigDirectory = (jstring) env->CallObjectMethod(jconfig,
      midConfigDirectory);
  if (JNIUtil::isExceptionThrown())
    return;

  JNIStringHolder configDirectory(jconfigDirectory);
  if (JNIUtil::isExceptionThrown())
    {
      return;
    }

  setConfigDirectory(configDirectory);

  JNIUtil::getEnv()->DeleteLocalRef(jconfigDirectory);

  jobject jprompter = env->CallObjectMethod(jconfig, midPrompt);
  if (JNIUtil::isExceptionThrown())
    return;

  if (jprompter != NULL)
    {
      Prompter *prompter = Prompter::makeCPrompter(jprompter);
      if (JNIUtil::isExceptionThrown())
        return;

      setPrompt(prompter);
      JNIUtil::getEnv()->DeleteLocalRef(jprompter);
    }

  /*
   * Attach session context java object
   */
  static jfieldID ctxFieldID = 0;
  attachJavaObject(contextHolder, "L"JAVA_PACKAGE"/ra/RaContext;",
      "sessionContext", &ctxFieldID);

  /*
   * Setup callbacks
   */
  SVN_JNI_ERR(svn_ra_create_callbacks(&m_raCallbacks, m_pool->getPool()), );

  m_raCallbacks->auth_baton = getAuthBaton(pool);
  m_raCallbacks->cancel_func = checkCancel;
  m_raCallbacks->get_client_string = clientName;
  m_raCallbacks->progress_baton = m_jctx;
  m_raCallbacks->progress_func = progress;

  /*
   * JNI RA layer does not work with WC so all WC callbacks are set to NULL
   */
  m_raCallbacks->get_wc_prop = NULL;
  m_raCallbacks->invalidate_wc_props = NULL;
  m_raCallbacks->push_wc_prop = NULL;
  m_raCallbacks->set_wc_prop = NULL;

  /*
   * Don't set deprecated callback
   */
  m_raCallbacks->open_tmp_file = NULL;
}

RaContext::~RaContext()
{
}

void *
RaContext::getCallbackBaton()
{
  return this;
}

svn_ra_callbacks2_t *
RaContext::getCallbacks()
{
  return m_raCallbacks;
}
