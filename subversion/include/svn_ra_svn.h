/*
 * svn_ra_svn.h :  libsvn_ra_svn functions used by the server
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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




#ifndef SVN_RA_SVN_H
#define SVN_RA_SVN_H

#include <apr_network_io.h>
#include <svn_delta.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* The well-known svn port number.  Right now this is just a random
 * port in the private range; I am waiting for a real port
 * assignment. -ghudson */
#define SVN_RA_SVN_PORT 51662

/* A specialized form of SVN_ERR to deal with errors which occur in an
 * svn_ra_svn_command_handler.  An error returned with this macro will
 * be passed back to the other side of the connection.  Use this macro
 * when performing the requested operation; use the regular SVN_ERR
 * when performing I/O with the client. */
#define SVN_CMD_ERR(expr)                                     \
  do {                                                        \
    svn_error_t *svn_err__temp = (expr);                      \
    if (svn_err__temp)                                        \
      return svn_error_create(SVN_ERR_RA_SVN_CMD_ERR,         \
			      svn_err__temp, NULL);           \
  } while (0)

typedef struct svn_ra_svn_conn_st svn_ra_svn_conn_t;

/* Command handler, used by svn_ra_svn_handle_commands. */
typedef svn_error_t *(*svn_ra_svn_command_handler)(svn_ra_svn_conn_t *conn,
                                                   apr_pool_t *pool,
                                                   apr_array_header_t *params,
                                                   void *baton);

/* Command table, used by svn_ra_svn_handle_commands.  If TERMINATE
 * is set, command-handling will cease after command is processed. */
typedef struct {
  const char *cmdname;
  svn_ra_svn_command_handler handler;
  svn_boolean_t terminate;
} svn_ra_svn_cmd_entry_t;

/* Memory representation of an on-the-wire data item. */
typedef struct {
  enum {
    SVN_RA_SVN_NUMBER,
    SVN_RA_SVN_STRING,
    SVN_RA_SVN_WORD,
    SVN_RA_SVN_LIST
  } kind;
  union {
    apr_uint64_t number;
    svn_string_t *string;
    const char *word;
    apr_array_header_t *list;	/* Contains svn_ra_svn_item_ts. */
  } u;
} svn_ra_svn_item_t;

typedef svn_error_t *(*svn_ra_svn_edit_callback)(void *baton);

/* Initialize a connection structure for the given socket or
 * input/output files.  Either SOCK or IN_FILE/OUT_FILE must be set,
 * not both. */
svn_ra_svn_conn_t *svn_ra_svn_create_conn(apr_socket_t *sock,
                                          apr_file_t *in_file,
                                          apr_file_t *out_file,
                                          apr_pool_t *pool);

/* Write simple data items over the net.  Writes will be buffered until
 * the next read or flush. */
svn_error_t *svn_ra_svn_write_number(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     apr_uint64_t number);
svn_error_t *svn_ra_svn_write_string(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const svn_string_t *str);
svn_error_t *svn_ra_svn_write_cstring(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool, const char *s);
svn_error_t *svn_ra_svn_write_word(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   const char *word);

/* Begin or end a list.  As above, writes will be buffered. */
svn_error_t *svn_ra_svn_start_list(svn_ra_svn_conn_t *conn, apr_pool_t *pool);
svn_error_t *svn_ra_svn_end_list(svn_ra_svn_conn_t *conn, apr_pool_t *pool);

/* Flush the write buffer.  Normally this shouldn't be necessary,
 * since the write buffer is flushed when a read is attempted. */
svn_error_t *svn_ra_svn_flush(svn_ra_svn_conn_t *conn, apr_pool_t *pool);

/* Write a tuple, using a printf-like interface.  The format string may
 * contain:
 *
 *   Spec  Argument type         Item type
 *   ----  --------------------  ---------
 *   n     apr_uint64_t          Number
 *   r     svn_revnum_t          Number
 *   s     const svn_string_t *  String
 *   c     const char *          String
 *   w     const char *          Word
 *   b     svn_boolean_t         Word ("true" or "false")
 *   (                           Begin tuple
 *   )                           End tuple
 *   [                           Begin optional tuple
 *   ]                           End optional tuple
 *
 * Inside an optional tuple, 'r' values may be SVN_INVALID_REVNUM and
 * 's', 'c', and 'w' values may be NULL; in these cases no data will
 * be written.  Either all or none of the optional tuple values should
 * be valid.  Optional tuples may not be nested.
 */
svn_error_t *svn_ra_svn_write_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    const char *fmt, ...);

/* Read an item from the network into ITEM. */
svn_error_t *svn_ra_svn_read_item(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  svn_ra_svn_item_t **item);

/* Parse an array of svn_item_t structures as a tuple, using a
 * printf-like interface.  The format string may contain:
 *
 *   Spec  Argument type          Item type
 *   ----  --------------------   ---------
 *   n     apr_uint64_t *         Number
 *   r     svn_revnum_t *         Number
 *   s     svn_string_t **        String
 *   c     const char **          String
 *   w     const char **          Word
 *   b     svn_boolean_t *        Word ("true" or "false")
 *   l     apr_array_header_t **  List
 *   [                            Begin optional tuple
 *   ]                            End optional tuple
 *
 * If an optional tuple contains no data, 'r' values will be set to
 * SVN_INVALID_REVNUM and 's', 'c', 'w', and 'l' values will be set to
 * NULL.  'n' and 'b' may not appear inside an optional tuple
 * specification.  Optional tuples may not be nested.
 */
svn_error_t *svn_ra_svn_parse_tuple(apr_array_header_t *list,
                                    apr_pool_t *pool,
                                    const char *fmt, ...);

/* Read an item from the network and parse it as a tuple, using the
 * above format string notation. */
svn_error_t *svn_ra_svn_read_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   const char *fmt, ...);
svn_error_t *svn_ra_svn_read_cmd_response(svn_ra_svn_conn_t *conn,
                                          apr_pool_t *pool,
                                          const char *fmt, ...);

/* Accept commands over the network and handle them according to
 * COMMANDS.  Command handlers will be passed CONN, a subpool of POOL
 * (cleared after each command is handled), the parameters of the
 * command, and BATON.  Commands will be accepted until a terminating
 * command is received (a command with "terminate" set in the command
 * table).  Normally, this function will only halt and return an error
 * when a communications failure occurs, and will send other errors to
 * the remote connection as command failures.  If PASS_THROUGH_ERRORS
 * is set, all errors will be returned (after being sent to the remote
 * connection if appropriate). */
svn_error_t *svn_ra_svn_handle_commands(svn_ra_svn_conn_t *conn,
                                        apr_pool_t *pool,
                                        svn_ra_svn_cmd_entry_t *commands,
                                        void *baton,
                                        svn_boolean_t pass_through_errors);

/* Write a command or successful command response over the network,
 * using the same format string notation as svn_ra_svn_write_tuple. */
svn_error_t *svn_ra_svn_write_cmd(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                  const char *cmdname, const char *fmt, ...);
svn_error_t *svn_ra_svn_write_cmd_response(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool,
                                           const char *fmt, ...);

/* Write an unsuccessful command response over the network. */
svn_error_t *svn_ra_svn_write_cmd_failure(svn_ra_svn_conn_t *conn,
                                          apr_pool_t *pool, svn_error_t *err);

/* Set *EDITOR and *EDIT_BATON to an editor which will pass editing
 * operations over the network, using CONN and POOL.  Upon successful
 * completion of the edit, the editor will invoke CALLBACK with
 * CALLBACK_BATON as an argument. */
void svn_ra_svn_get_editor(const svn_delta_editor_t **editor,
                           void **edit_baton, svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool, svn_ra_svn_edit_callback callback,
                           void *callback_baton);

/* Receive edit commands over the network and use them to drive EDITOR
 * with EDIT_BATON.  On return, *ABORTED will be set if the edit was
 * aborted.  See the svn_ra_svn_handle_commands description for the
 * meaning of PASS_THROUGH_ERRORS. */
svn_error_t *svn_ra_svn_drive_editor(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const svn_delta_editor_t *editor,
                                     void *edit_baton,
                                     svn_boolean_t pass_through_errors,
                                     svn_boolean_t *aborted);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_SVN_H */
