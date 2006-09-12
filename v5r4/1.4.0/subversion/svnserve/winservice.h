/*
 * winservice.h : Public definitions for Windows Service support
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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

#ifndef WINSERVICE_H
#define WINSERVICE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#ifdef WIN32

/* Connects to the Windows Service Control Manager and allows this
   process to run as a service.  This function can only succeed if the
   process was started by the SCM, not directly by a user.  After this
   call succeeds, the service should perform whatever work it needs to
   start the service, and then the service should call
   winservice_running() (if no errors occurred) or winservice_stop()
   (if something failed during startup). */
svn_error_t *winservice_start(void);

/* Notifies the SCM that the service is now running.  The caller must
   already have called winservice_start successfully. */
void winservice_running(void);

/* This function is called by the SCM in an arbitrary thread when the
   SCM wants the service to stop.  The implementation of this function
   can return immediately; all that is necessary is that the service
   eventually stop in response. */
void winservice_notify_stop(void);

/* Evaluates to TRUE if the SCM has requested that the service stop.
   This allows for the service to poll, in addition to being notified
   in the winservice_notify_stop callback. */
svn_boolean_t winservice_is_stopping(void);

#endif /* WIN32 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* WINSERVICE_H */
