#include "../include/org_apache_subversion_javahl_ra_SVNRa.h"

#include "JNIStackElement.h"
#include "JNIUtil.h"
#include "Prompter.h"
#include "SVNRa.h"
#include "Revision.h"
#include "EnumMapper.h"

#include "svn_private_config.h"

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_ra_SVNRa_finalize(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNRa, finalize);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  if (ras != NULL)
    ras->finalize();
}

JNIEXPORT void JNICALL
Java_org_apache_subversion_javahl_ra_SVNRa_dispose(JNIEnv *env, jobject jthis)
{
  JNIEntry(SVNRa, dispose);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  if (ras != NULL)
    ras->dispose(jthis);
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_ra_SVNRa_getLatestRevision(JNIEnv *env,
                                                             jobject jthis)
{
  JNIEntry(SVNRa, getLatestRevision);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getLatestRevision();
}

JNIEXPORT jlong JNICALL
Java_org_apache_subversion_javahl_ra_SVNRa_getDatedRevision
(JNIEnv *env, jobject jthis, jobject jdate)
{
  JNIEntry(SVNRa, getDatedRevision);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, SVN_INVALID_REVNUM);

  return ras->getDatedRev(jdate);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_ra_SVNRa_getLocks
(JNIEnv *env, jobject jthis, jstring jpath, jobject jdepth)
{
  JNIEntry(SVNRa, getLocks);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->getLocks(jpath, jdepth);
}

JNIEXPORT jobject JNICALL
Java_org_apache_subversion_javahl_ra_SVNRa_checkPath
(JNIEnv *env, jobject jthis, jstring jpath, jobject jrevision)
{
  JNIEntry(SVNReposAccess, checkPath);
  SVNRa *ras = SVNRa::getCppObject(jthis);
  CPPADDR_NULL_PTR(ras, NULL);

  return ras->checkPath(jpath, jrevision);
}
