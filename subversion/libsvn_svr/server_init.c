/*
 * server_init.c :   parse server configuration file
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */


#include <apr_hash.h>
#include <apr_dso.h>
#include "svn_svr.h"
#include "svn_parse.h"


/* 
   svn_svr_load_plugin() : Utility to load/register a server plugin

   Input:    * a policy in which to register the plugin
             * a path to the shared library
             * name of the initialization routine in the plugin
            
   Returns:  error structure or SVN_SUCCESS

   ASSUMES that ap_dso_init() has already been called!

*/

svn_error_t *
svn_svr_load_plugin (svn_svr_policies_t *policy,
                     svn_string_t *path, 
                     (svn_error_t *) (* ) ())
{

}




/*  svn__svr_load_all_plugins :  NOT EXPORTED

    Loops through hash of plugins, loads each using APR's DSO
    routines.  Each plugin ultimately registers (appends) itself into
    the policy structure.  
*/

svn_error_t *
svn__svr_load_all_plugins (ap_hash_t *plugins, svn_svr_policies_t *policy)
{
  ap_hash_index_t *hash_index;
  void *key, *val;
  size_t keylen;
  

  /* Initialize the APR DSO mechanism*/
  ap_status_t result = ap_dso_init();

  if (result != APR_SUCCESS)
    {
      svn_string_t *msg = 
        svn_string_create 
        ("svr__load_plugins(): fatal: can't ap_dso_init() ", policy->pool);
      return (svn_create_error (result, SVN_NON_FATAL, msg, policy->pool));
    }

  /* Loop through the hash of plugins from configdata */

  for (hash_index = ap_hash_first (plugins);    /* get first hash entry */
       hash_index;                              /* NULL if out of entries */
       hash_index = ap_hash_next (hash_index))  /* get next hash entry */
    {
      /* call 
    }
}





/* 
   svn_svr_init()

   This routine does any necessary server setup, so it must be called first!

   Input:  a hash of hashes, containing all server-policy data.
    
   Returns: a pointer to a server policy structure.

   --> The hash of hashes can be created either manually, or by running
       svn_parse (filename), e.g.
        
                my_filename = svn_string_create ("/etc/svn.conf");
                my_policy = svn_svr_init (svr_parse (my_filename, p), p);

   --> The returned policy structure is a global context that must be
   passed to all server routines... don't lose it!

*/

svn_svr_policies_t *
svn_svr_init (ap_hash_t *configdata, ap_pool_t *pool)
{
  ap_status_t result;

  /* First, allocate a `policies' structure and all of its internal
     lists */

  svn_svr_policies_t *my_policies = 
    (svn_svr_policies_t *) ap_palloc (pool, sizeof(svn_svr_policies_t *));

  my_policies->repos_aliases = ap_make_hash (pool);
  my_policies->global_restrictions = ap_make_hash (pool);
  my_policies->plugins = ap_make_hash (pool);

  /* A policy structure has its own private memory pool, too, for
     miscellaneous useful things.  */

  result = ap_create_pool (& (my_policies->pool), NULL);

  if (result != APR_SUCCESS)
    {
      /* Can't create a private memory pool for the policy structure?
         Then just use the one that was passed in instead.  */
      svn_string_t *msg = 
        svn_string_create 
        ("svr_init(): warning: can't alloc pool for policy structure", pool);
      svn_handle_error (svn_create_error (result, SVN_NON_FATAL, msg, pool));

      my_policies->pool = pool;
    }

  /* Ben sez:  we need a debugging system here.  Let's get one quick. (TODO)
     i.e.  
            if (DEBUGLVL >= 2) {  printf...;  svn_uberhash_print(); }
  */
  svn_uberhash_print (configdata, stdout);

  /* Now walk through our Uberhash, just as we do in svn_uberhash_print(). */
  {
    ap_hash_index_t *hash_index;
    void *key, *val;
    size_t keylen;

    for (hash_index = ap_hash_first (configdata); /* get first hash entry */
         hash_index;                              /* NULL if out of entries */
         hash_index = ap_hash_next (hash_index))  /* get next hash entry */
      {
        /* Retrieve key and val from current hash entry */
        ap_hash_this (hash_index, &key, &keylen, &val);

        /* Figure out which `section' of svn.conf we're looking at */

        if (svn_string_compare_2cstring ((svn_string_t *) key,
                                         "repos_aliases"))
          {
            /* The "val" is a pointer to a hash full of repository
               aliases, alrady as we want them.  Just store this value
               in our policy structure! */

            printf ("svr_init(): got repository aliases.\n");
            my_policies->repos_aliases = (ap_hash_t *) val;
          }

        else if (svn_string_compare_2cstring ((svn_string_t *) key,
                                              "security"))
          {
            /* The "val" is a pointer to a hash full of security
               commands; again, we just store a pointer to this hash
               in our policy (the commands are interpreted elsewhere) */

            printf ("svr_init(): got security restrictions.\n");
            my_policies->global_restrictions = (ap_hash_t *) val;
          }

        else if (svn_string_compare_2cstring ((svn_string_t *) key,
                                              "plugins"))
          {
            /* The "val" is a pointer to a hash containing plugin
               libraries to load up.  We'll definitely do that here
               and now! */
            
            printf ("svr_init(): loading list of plugins...\n");
            
            svn__svr_load_all_plugins ((ap_hash_t *) val, my_policies);

          }

        else
          {
            svn_string_t *msg = 
              svn_string_create ("svr_init(): warning: unknown section: ", 
                                 pool);
            svn_string_appendstr (msg, (svn_string_t *) key, pool);
            svn_handle_error (svn_create_error 
                              (SVN_ERR_UNRECOGNIZED_SECTION, 
                               SVN_NON_FATAL,
                               msg, pool));            
          }
      }    /* for (hash_index...)  */
       
  } /* closing of Uberhash walk-through */
  
  return my_policies;
}



/* Add a plugin structure to a server policy structure.
   Called by each plugin's init() routine. */

svn_error_t *
svn_svr_register_plugin (svn_svr_policies_t *policy,
                         svn_svr_plugin_t *new_plugin)
{
  /* just need to push the new plugin pointer onto the policy's
     array of plugin pointers.  */

  /* Store in policy->plugins hashtable : 
     KEY = new_plugin->name, val = new_plugin */

  /* Hm... how would this routine fail? :)  */

  return SVN_SUCCESS;  /* success */
}








/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
