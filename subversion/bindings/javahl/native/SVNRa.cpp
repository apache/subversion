#include "JNIStringHolder.h"
#include "JNIUtil.h"

#include "svn_ra.h"

#include "SVNRa.h"

#include "svn_private_config.h"

#define JAVA_CLASS_SVN_RA JAVA_PACKAGE "/ra/SVNRa"

SVNRa *
SVNRa::getCppObject(jobject jthis)
{
  static jfieldID fid = 0;
  jlong cppAddr = SVNBase::findCppAddrForJObject(jthis, &fid,
      JAVA_CLASS_SVN_RA);
  return (cppAddr == 0 ? NULL : reinterpret_cast<SVNRa *>(cppAddr));
}

SVNRa::SVNRa(jobject *jthis_out, jstring jurl, jstring juuid, jobject jconfig)
{
  JNIEnv *env = JNIUtil::getEnv();

  JNIStringHolder url(jurl);
  if (JNIUtil::isExceptionThrown())
    {
      return;
    }

  JNIStringHolder uuid(juuid);
  if (JNIUtil::isExceptionThrown())
    {
      return;
    }

  // Create java session object
  jclass clazz = env->FindClass(JAVA_CLASS_SVN_RA);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(J)V");
      if (JNIUtil::isJavaExceptionThrown())
        return;
    }

  jlong cppAddr = this->getCppAddr();

  jobject jSVNRa = env->NewObject(clazz, ctor, cppAddr);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  *jthis_out = jSVNRa;

  m_context = new RaContext(jSVNRa, pool, jconfig);
  if (JNIUtil::isJavaExceptionThrown())
    return;

  //TODO: add corrected URL support
  SVN_JNI_ERR(
      svn_ra_open4(&m_session, NULL, url, uuid, m_context->getCallbacks(),
                   m_context->getCallbackBaton(), m_context->getConfigData(),
                   pool.getPool()),
      );
}

SVNRa::~SVNRa()
{
  if (m_context)
    {
      delete m_context;
    }
}

jlong
SVNRa::getLatestRevision()
{
  SVN::Pool subPool(pool);
  svn_revnum_t rev;

  SVN_JNI_ERR(svn_ra_get_latest_revnum(m_session, &rev, subPool.getPool()),
      SVN_INVALID_REVNUM);

  return rev;
}

void
SVNRa::dispose(jobject jthis)
{
  static jfieldID fid = 0;
  SVNBase::dispose(jthis, &fid, JAVA_CLASS_SVN_RA);
}
