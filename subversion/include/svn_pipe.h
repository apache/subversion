/*
 * svn_pipe.h: shared declarations for pipe interface
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING,
 * which you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_PIPE_H
#define SVN_PIPE_H

#include <apr_pools.h>
#include <apr_file_io.h>
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* An opaque handle to a pipe endpoint */
typedef struct svn_pipe_t svn_pipe_t;



/* Establish a pipe endpoint by executing the command stored
   in ARGV[0], passing it the parameters ARGV, using POOL for
   all memory allocations.  Store the pipe in *RESULT. */
svn_error_t *
svn_pipe_open(svn_pipe_t **result,
              const char * const *argv,
              apr_pool_t *pool);

/* Establish a pipe endpoint by attaching to INPUT and OUTPUT,
   using POOL for all memory allocations.  Store the pipe in *RESULT.
   This function is symmetrical to svn_pipe_open(); it is to be called
   by the process *established* by svn_pipe_open() in order to initialize
   its end of the pipe. */
svn_error_t *
svn_pipe_endpoint(svn_pipe_t **result,
                  apr_file_t *input,
                  apr_file_t *output,
                  apr_pool_t *pool);

/* Close a pipe endpoint, using POOL for all memory allocations. */
svn_error_t *
svn_pipe_close(svn_pipe_t *pipe,
               apr_pool_t *pool);

/* Write LENGTH bytes of DATA to the PIPE using POOL
   for all memory allocations. */
svn_error_t *
svn_pipe_write(svn_pipe_t *pipe,
               const char *data,
               apr_size_t length,
               apr_pool_t *pool);

/* Send LENGTH bytes of DATA down the PIPE *in a frame* using POOL
   for all memory allocations. */
svn_error_t *
svn_pipe_send(svn_pipe_t *pipe,
              const char *data,
              apr_size_t length,
              apr_pool_t *pool);


/* Receive a *framed message* from the PIPE, storing the contents
   in *DATA and the length in *LENGTH, using POOL for all
   memory allocations. */
svn_error_t *
svn_pipe_receive(svn_pipe_t *pipe,
                 char **data,
                 apr_size_t *length,
                 apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_PIPE_H */
