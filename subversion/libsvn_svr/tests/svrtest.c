
/* Testing basic Subversion server stuff. */

#include <stdio.h>
#include "svn_svr.h"
#include "svn_parse.h"

int
main ()
{
  ap_hash_t *configdata;
  ap_pool_t *pool;
  svn_svr_policies_t *policy;

  /* Initialize APR (Apache pools) */
  if (ap_initialize () != APR_SUCCESS)
    {
      printf ("ap_initialize() failed.\n");
      exit (1);
    }
  if (ap_create_pool (&pool, NULL) != APR_SUCCESS)
    {
      printf ("ap_create_pool() failed.\n");
      exit (1);
    }


  /* Parse the file "./testpolicy.conf" */

  configdata = svn_parse (svn_string_create ("testpolicy.conf", pool), pool);


  /* Print out our configdata uber-hash 
     svn_uberhash_print (configdata, stdout); */


  /* If we were an application using libsvn_svr, we would now pass
     this uber-hash to svn_init() to get a `svn_policies_t' structure.
     We would then use this structure for all our wrappered filesystem
     calls.  */

  policy = svn_svr_init (configdata, pool);



  /* Clean up our memory pool and apr */
  ap_destroy_pool (pool);
  ap_terminate ();

  printf ("Test complete, exiting cleanly.\n\n");
  return 0;
}




