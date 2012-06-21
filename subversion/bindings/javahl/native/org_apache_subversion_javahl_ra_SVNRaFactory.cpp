#include "../include/org_apache_subversion_javahl_ra_SVNRaFactory.h"

#include "JNIStackElement.h"
#include "JNIUtil.h"
#include "JNIStringHolder.h"

#include "SVNRa.h"

#include "svn_private_config.h"

JNIEXPORT jobject JNICALL Java_org_apache_subversion_javahl_ra_SVNRaFactory_createRaSession
(JNIEnv *env, jclass jclass, jstring jurl, jstring juuid, jobject jconfig)
{
  //JNI macros need jthis but this is a static call
  jobject jthis = NULL;
  JNIEntry(SVNRaFactory, createRaSession);

  /*
   * Initialize ra layer if we have not done so yet
   */
  static bool initialized = false;
  if(!initialized)
    {
      SVN_JNI_ERR(svn_ra_initialize(JNIUtil::getPool()), NULL);
      initialized = true;
    }

  /*
   * Create Ra C++ object and return its java wrapper to the caller
   */
  jobject jSVNRa = NULL;

  SVNRa * raSesson = new SVNRa(&jSVNRa, jurl, juuid, jconfig);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  return jSVNRa;
}
