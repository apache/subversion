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


#include <svn_svr.h>
#include <svn_parse.h>
#include <apr_hash.h>


/* This routine does any necessary server setup, so it must be called first!

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
      svn_handle_error (svn_create_error (result, FALSE, msg, pool));

      my_policies->pool = pool;
    }


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
            printf ("neato!  found the repos_aliases section.\n");
          }

        else if (svn_string_compare_2cstring ((svn_string_t *) key,
                                              "plugins"))
          {
            printf ("neato!  found the plugins section.\n");
          }

        else if (svn_string_compare_2cstring ((svn_string_t *) key,
                                              "security"))
          {
            printf ("neato!  found the security section.\n");
          }

        else
          {
            svn_string_t *msg = 
              svn_string_create ("svr_init(): warning: unknown section: ", 
                                 pool);
            svn_string_appendstr (msg, (svn_string_t *) key, pool);
            svn_handle_error (svn_create_error 
                              (SVN_ERR_UNRECOGNIZED_SECTION, FALSE,
                               msg, pool));            
          }
      }    
    /* store any repository aliases, */
    
    /* store any  general security policies, */
    
    /* use apr's DSO routines to load each server plugin;
       use dlsym to get a handle on the named init routine;
       call init_routine (&my_policies, dso_filename, pool); */
       
   
  }
  
  return my_policies;
}



/* Add a plugin structure to a server policy structure.
   Called by each plugin's init() routine. */

void
svn_svr_register_plugin (svn_svr_policies_t *policy,
                         svn_string_t *dso_filename,
                         svn_svr_plugin_t *new_plugin)
{
  /* just need to push the new plugin pointer onto the policy's
     array of plugin pointers.  */

  /* Store in policy->plugins hashtable : 
     KEY = dso_filename, val = new_plugin */

}








/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
