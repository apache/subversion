/* delta-combine-test.c -- test driver for delta combination
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <apr_general.h>
#include <apr_tables.h>
#include "svn_base64.h"
#include "svn_quoprint.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_string.h"


/* The test here is simple.  Given a string of N files:

   foreach file (files - 1)
     generate svndiff which converts file to file + 1;
     write diff to disk;
   
   combine the files - 1 diff data segments into a single diff data
   file;

   apply the One Diff to the first file, and hope it produces the
   last.
*/


#define INT_ERR(expr)                              \
  do {                                             \
    svn_error_t *svn_err__temp = (expr);           \
    if (svn_err__temp) {                           \
      svn_handle_error (svn_err__temp, stderr, 0); \
      return EXIT_FAILURE;                         \
    }                                              \
  } while (0)
      

/* Print a usage message for program PROG, and return ERRVAL. */
static int 
do_usage (const char *prog, 
          int errval)
{
  printf ("%s - testing svndiff delta combination\n", prog);
  printf ("usage: %s FILE1 FILE2 FILE3 [ FILE4 [ FILE5 ... ]]\n\n", prog);
  return errval;
}


/* Given a list of FILE_NAMES (of which there are NUM_FILES), generate
   a collection of (NUM_FILES - 1) temporary files containing svndiff
   data which describes the difference between each successive file in
   FILE_NAMES.  Allocate an array, TMP_FILES_P, and populate it with
   the names of the temporary files created in this process.  Use POOL
   for all allocations.  */
static svn_error_t *
generate_file_diffs (apr_array_header_t **tmp_files_p,
                     int num_files, 
                     const char **file_names,
                     apr_pool_t *pool)
{ 
  int i;
  apr_array_header_t *tmp_files;
  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_handler_t svndiff_handler;
  svn_stream_t *out_stream;
  void *svndiff_baton;

  /* Allocate an array in which to store our temporary filenames. */
  tmp_files = apr_array_make (pool, 2, sizeof (svn_stringbuf_t *));

  /* Loop over our arguments, generating diff data between each
     successive one and storing that diff data in a temporary file. */
  for (i = 0; i < num_files - 1; i++)
    {
      FILE *source_file;
      FILE *target_file;
      svn_stringbuf_t *tmpfile_name;
      apr_file_t *tmp_file;

      /* Open the two source files. */
      source_file = fopen (file_names[i], "rb");
      target_file = fopen (file_names[i + 1], "rb");

      /* Open the output file (a tmpfile whose name we want to
         remember!) */
      SVN_ERR (svn_io_open_unique_file (&tmp_file, &tmpfile_name,
                                        "svndiff", ".data", FALSE, pool));
      
      /* Create OUTSTREAM from TMPFILE. */
      out_stream = svn_stream_from_aprfile (tmp_file, pool);

      /* Create a TXDELTA_STREAM from the two files. */
      svn_txdelta (&txdelta_stream, 
                   svn_stream_from_stdio (source_file, pool),
                   svn_stream_from_stdio (target_file, pool), 
                   pool);
      
      
      /* Note that we want our txdelta's converted to svndiff data,
         and sent to OUTSTREAM.  */
      svn_txdelta_to_svndiff (out_stream, pool, 
                              &svndiff_handler, &svndiff_baton);

      /* Now do the conversion. */
      svn_txdelta_send_txstream (txdelta_stream, svndiff_handler,
                                 svndiff_baton, pool);

      /* Close all the files. */
      fclose (source_file);
      fclose (target_file);
      apr_file_close (tmp_file);

      /* ...but remember the tmpfile's name. */
      (*((svn_stringbuf_t **) apr_array_push (tmp_files))) = tmpfile_name;
    }

  *tmp_files_p = tmp_files;
  return SVN_NO_ERROR;
}


/* Given a array of SVNDIFF_FILES, combine them into a single file
   containing the combined svndiff delta data across the set of diffs.
   Return the name of the file which contains this combined delta data
   in *OUT_FILENAME, and use POOL for all allocations.  */
static svn_error_t *
do_delta_combination (const char **out_filename,
                      apr_array_header_t *svndiff_files,
                      apr_pool_t *pool)
{
  svn_stringbuf_t *target = ((svn_stringbuf_t **) (svndiff_files->elts))[0];


  /* ### TODO:  RIGHT HERE!  HERE YA GO!  THIS IS THE SPOT!!

     Right about ... HERE ... would be a good place to put some delta
     combination code.  See the docstring above for what should
     happen.  */
  *out_filename = apr_pstrdup (pool, target->data);
  return SVN_NO_ERROR;
}




/* Apply the svndiff data found in SVNDIFF_FILENAME to the source data
   found in SOURCE_FILENAME, and store the results in a temporary
   file, whose name is returned in *OUT_FILENAME.  Use POOL for all
   allocations.  */
static svn_error_t *
apply_svndiff_data (const char **out_filename,
                    const char *source_filename,
                    const char *svndiff_filename,
                    apr_pool_t *pool)
{
  FILE *source_file, *svndiff_file;
  apr_file_t *out_file;
  svn_stringbuf_t *unique_file;
  svn_txdelta_window_handler_t svndiff_handler;
  svn_stream_t *out_stream, *in_stream;
  void *svndiff_baton;
  svn_boolean_t streaming;

  /* Re-open the first file, the svndiff file, and a tmp-file for the
     diff-applied output. */
  source_file = fopen (source_filename, "rb");
  svndiff_file = fopen (svndiff_filename, "rb");
  SVN_ERR (svn_io_open_unique_file (&out_file, &unique_file,
                                    "svndiff", ".data", FALSE, pool));

  /* Store our returned filename. */
  *out_filename = unique_file->data;

  /* Get a handler/baton that will apply txdelta's to SOURCE_FILE, and
     place the results in OUT_FILE.  */
  svn_txdelta_apply (svn_stream_from_stdio (source_file, pool),
                     svn_stream_from_aprfile (out_file, pool),
                     pool, &svndiff_handler, &svndiff_baton);

  /* Make OUT_STREAM a writable stream that will parse svndiff data,
     calling a handler/baton for each window of that data. */
  out_stream = svn_txdelta_parse_svndiff (svndiff_handler, svndiff_baton, 
                                          TRUE, pool);

  /* Make IN_STREAM a readable stream based on the tmpfile which
     contains our combined delta data. */
  in_stream = svn_stream_from_stdio (svndiff_file, pool);
  
  /* Now, read from IN_STREAM and write to OUT_STREAM. */
  streaming = TRUE;
  while (streaming)
    {
      char buf[SVN_STREAM_CHUNK_SIZE];
      apr_size_t len = SVN_STREAM_CHUNK_SIZE;
      
      SVN_ERR (svn_stream_read (in_stream, buf, &len));
      if (len != SVN_STREAM_CHUNK_SIZE)
        streaming = FALSE;
      SVN_ERR (svn_stream_write (out_stream, buf, &len));
    }

  /* Close the two streams. */
  SVN_ERR (svn_stream_close (out_stream));
  SVN_ERR (svn_stream_close (in_stream));

  return SVN_NO_ERROR;
}


/* Verify that FILE1 and FILE2 definitely have different filesizes
   (stolen from libsvn_wc/questions.c). */
static svn_error_t *
filesizes_definitely_different_p (svn_boolean_t *different_p,
                                  const char *file1,
                                  const char *file2,
                                  apr_pool_t *pool)
{
  apr_finfo_t finfo1;
  apr_finfo_t finfo2;
  apr_status_t status;

  /* Stat both files */
  status = apr_stat (&finfo1, file1, APR_FINFO_MIN, pool);
  if (status)
    {
      /* If we got an error stat'ing a file, it could be because the
         file was removed... or who knows.  Whatever the case, we
         don't know if the filesizes are definitely different, so
         assume that they're not. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }

  status = apr_stat (&finfo2, file2, APR_FINFO_MIN, pool);
  if (status)
    {
      /* See previous comment. */
      *different_p = FALSE;
      return SVN_NO_ERROR;
    }


  /* Examine file sizes */
  if (finfo1.size == finfo2.size)
    *different_p = FALSE;
  else
    *different_p = TRUE;

  return SVN_NO_ERROR;
}

/* Compare the contents of FILE1 and FILE2, and report whether or not
   they are identical.  Use POOL for all allocations (this code was
   stolen from libsvn_wc/questions.c) */
static svn_error_t *
contents_identical_p (svn_boolean_t *identical_p,
                      const char *file1,
                      const char *file2,
                      apr_pool_t *pool)
{
  apr_status_t status;
  apr_size_t bytes_read1, bytes_read2;
  char buf1[BUFSIZ], buf2[BUFSIZ];
  apr_file_t *file1_h = NULL;
  apr_file_t *file2_h = NULL;

  status = apr_file_open (&file1_h, file1,
                          APR_READ, APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_open failed on `%s'", file1);

  status = apr_file_open (&file2_h, file2, APR_READ, 
                          APR_OS_DEFAULT, pool);
  if (status)
    return svn_error_createf
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_open failed on `%s'", file2);

  *identical_p = TRUE;  /* assume TRUE, until disproved below */
  while (!APR_STATUS_IS_EOF(status))
    {
      status = apr_file_read_full (file1_h, buf1, sizeof(buf1), &bytes_read1);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_file_read_full() failed on %s.", 
           file1);

      status = apr_file_read_full (file2_h, buf2, sizeof(buf2), &bytes_read2);
      if (status && !APR_STATUS_IS_EOF(status))
        return svn_error_createf
          (status, 0, NULL, pool,
           "contents_identical_p: apr_file_read_full() failed on %s.", 
           file2);
      
      if ((bytes_read1 != bytes_read2)
          || (memcmp (buf1, buf2, bytes_read1)))
        {
          *identical_p = FALSE;
          break;
        }
    }

  status = apr_file_close (file1_h);
  if (status)
    return svn_error_createf 
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_close failed on %s.", file1);

  status = apr_file_close (file2_h);
  if (status)
    return svn_error_createf 
      (status, 0, NULL, pool,
       "contents_identical_p: apr_file_close failed on %s.", file2);

  return SVN_NO_ERROR;
}


int
main (int argc, 
      const char **argv)
{
  apr_pool_t *pool;
  apr_array_header_t *tmp_files;
  const char *combined_diff_filename, *target_regen_filename;
  svn_boolean_t identical, different_sizes;

  /* We have needs, too, you know...like 3 arguments to the program! */
  if (argc < 4)
    return do_usage (argv[0], 1);
  
  /* Initialize APR and create our top-level SVN pool. */
  apr_initialize();
  pool = svn_pool_create (NULL);
  
  /* Generate the successive svndiffs between out input files. */
  INT_ERR (generate_file_diffs (&tmp_files, argc - 1, argv + 1, pool));


  /* WHOO-PAH!!  Here is where we do the delta combination, baby!  The
     result should be a single svndiff-containing file */
  INT_ERR (do_delta_combination (&combined_diff_filename,
                                 tmp_files,
                                 pool));

  
  /* And here, we need to apply our combined delta to our first file,
     and store the results in another tempfilee.  */
  INT_ERR (apply_svndiff_data (&target_regen_filename,
                               argv[1],
                               combined_diff_filename,
                               pool));

  /* Then, we compare the delta-d copied with the last file, and if
     they are exactly alike, we win!!  */
  INT_ERR (filesizes_definitely_different_p (&different_sizes,
                                             argv[1],
                                             target_regen_filename,
                                             pool));
  if (different_sizes)
    INT_ERR (svn_error_create (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                               "Application of combined delta corrupt"));

  INT_ERR (contents_identical_p (&identical, 
                                 argv[1],
                                 target_regen_filename,
                                 pool));
  if (! identical)
    INT_ERR (svn_error_create (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                               "Application of combined delta corrupt"));

  apr_terminate();
  return 0;
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
