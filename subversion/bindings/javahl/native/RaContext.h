#ifndef RACONTEXT_H_
#define RACONTEXT_H_

#include "svn_ra.h"

#include "RaSharedContext.h"

class RaContext : public RaSharedContext
{
  public:
    RaContext(jobject contextHolder, SVN::Pool &pool, jobject jconfig);
    virtual ~RaContext();
    void * getCallbackBaton();
    svn_ra_callbacks2_t * getCallbacks();

  private:
    svn_ra_callbacks2_t * m_raCallbacks;
};

#endif /* RACONTEXT_H_ */
