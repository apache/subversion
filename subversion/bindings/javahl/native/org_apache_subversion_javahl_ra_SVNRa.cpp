#include "../include/org_apache_subversion_javahl_ra_SVNRa.h"

#include "JNIStackElement.h"
#include "JNIUtil.h"
#include "Prompter.h"
#include "SVNRa.h"

#include "svn_private_config.h"


JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_ra_SVNRa_finalize
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNRa, finalize);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  if (ras != NULL)
    ras->finalize();
}

JNIEXPORT void JNICALL Java_org_apache_subversion_javahl_ra_SVNRa_dispose
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNRa, dispose);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  if (ras != NULL)
    ras->dispose(jthis);
}

JNIEXPORT jlong JNICALL Java_org_apache_subversion_javahl_ra_SVNRa_getLatestRevision
(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNRa, getLatestRevision);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getLatestRevision();
}
