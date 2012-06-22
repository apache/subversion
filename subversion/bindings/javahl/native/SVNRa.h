#ifndef SVNRa_H
#define SVNRa_H

#include <jni.h>

#include "svn_ra.h"

#include "SVNBase.h"
#include "RaContext.h"
#include "Revision.h"

#include <set>

class SVNEditor;

/*
 * This class wraps Ra based operations from svn_ra.h
 */
class SVNRa: public SVNBase
{
  public:
    static SVNRa *getCppObject(jobject jthis);
    SVNRa(jobject *, jstring jurl, jstring juuid, jobject jconfig);
    ~SVNRa();

    jlong getLatestRevision();

    svn_revnum_t getDatedRev(apr_time_t time);
    jobject getLocks(const char *path, svn_depth_t depth);
    jobject checkPath(const char *path, Revision &revision);

    virtual void dispose(jobject jthis);

  private:
    svn_ra_session_t * m_session;

    RaContext * m_context;
};

#endif //SVNRa_H
