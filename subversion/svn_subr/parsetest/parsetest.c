
/* Testing svn_parse() */

#include <stdio.h>
#include "svn_parse.h"

int
main ()
{
  ap_hash_t *configdata;
  ap_pool_t *pool;
  svn_error_t *error;

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


  /* Parse the file "./configfile" */

  error = 
    svn_parse (&configdata, svn_string_create ("configfile", pool), pool);

  if (error) {
    svn_handle_error (error);
  }


  /* Print out our configdata uber-hash */

  svn_uberhash_print (configdata, stdout);


  /* If we were an application using libsvn_svr, we would now pass
     this uber-hash to svn_init() to get a `svn_policies_t' structure.
     We would then use this structure for all our wrappered filesystem
     calls.  */

  /* Clean up our memory pool and apr */
  ap_destroy_pool (pool);
  ap_terminate ();

  printf ("Test complete, exiting cleanly.\n\n");
  return 0;
}
