
/* Testing svn_parse() */

#include <svn_parse.h>
#include <stdio.h>

int
main ()
{
  ap_hash_t *configdata;
  ap_pool_t *pool;
  svn_string_t *myfile;

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

  myfile = svn_string_create ("configfile", pool);

  configdata = svn_parse (myfile, pool);


  printf ("Test complete, exiting cleanly.\n\n");
  return 0;
}
