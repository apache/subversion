/*
 * global configuration of the java / subversion binding
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

#ifndef SVN_JNI_GLOBAL_H
#define SVN_JNI_GLOBAL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*** Defines ***/
#define SVN_JNI__SUBVERSION_EXCEPTION \
"org/tigris/subversion/SubversionException"
#define SVN_JNI__ERROR_CREATE_STRINGBUF "error while creating stringbuf_t"
#define SVN_JNI__ERROR_CLIENT_STATUS "error in svn_client_status()"

#define SVN_JNI__DEBUG_PTR(ptr) \
{ \
fprintf(stderr, #ptr "="); \
if( ptr==NULL ) { fprintf(stderr, "NULL;"); } \
else { fprintf(stderr, "%x;", ptr); } \
}
#define SVN_JNI__DEBUG_BOOL(bool) \
{ fprintf(stderr, #bool "="); \
if(bool) { fprintf(stderr, "TRUE;"); } \
else { fprintf(stderr, "FALSE;"); } \
}
#define SVN_JNI__DEBUG_STR(str) \
{ fprintf(stderr, #str "="); \
if(str==NULL) { fprintf(stderr, "NULL;"); } \
else { fprintf(stderr, "'%s';", (char*)str); } \
}
#define SVN_JNI__DEBUG_DEC(dec) \
{ fprintf(stderr, #dec "=%d;", dec); }
#define SVN_JNI__DEBUG_LONG(dec) \
{ fprintf(stderr, #dec "=%ld;", dec); }

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
