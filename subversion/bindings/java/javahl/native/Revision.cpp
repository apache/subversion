/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file Revision.cpp
 * @brief Implementation of the class Revision
 */

#include "Revision.h"
#include "org_tigris_subversion_javahl_Revision_Kind.h"
#include "JNIUtil.h"
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
const svn_opt_revision_kind Revision::START = svn_opt_revision_unspecified;
const svn_opt_revision_kind Revision::HEAD = svn_opt_revision_head;

Revision::Revision (const svn_opt_revision_kind kind)
{
	m_revision.kind = kind;
	m_revision.value.number = 0;
}


Revision::Revision(jobject jthis)
{
	if(jthis == NULL)
	{
		m_revision.kind = svn_opt_revision_unspecified;
		m_revision.value.number = 0;
		return;
	}

	JNIEnv *env = JNIUtil::getEnv();
	static jfieldID fid = 0;
	if(fid == 0)
	{
		jclass clazz = env->FindClass(JAVA_PACKAGE"/Revision");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		fid = env->GetFieldID(clazz, "revKind", "I");
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
		env->DeleteLocalRef(clazz);
		if(JNIUtil::isJavaExceptionThrown())
		{
			return;
		}
	}
	jint jKind = env->GetIntField(jthis, fid);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
	m_revision.value.number = 0;

	switch(jKind)
	{
	case org_tigris_subversion_javahl_Revision_Kind_unspecified:
		m_revision.kind = svn_opt_revision_unspecified;
		break;
	case org_tigris_subversion_javahl_Revision_Kind_number:
		m_revision.kind = svn_opt_revision_number;
		{
			static jfieldID fidNum = 0;
			if(fidNum == 0)
			{
				jclass clazz = env->FindClass(JAVA_PACKAGE"/Revision$Number");
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
				fidNum = env->GetFieldID(clazz, "revNumber", "J");
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
				env->DeleteLocalRef(clazz);
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
			}
			jlong jNumber = env->GetLongField(jthis, fidNum);
			m_revision.value.number = jNumber;
		}
		break;
	case org_tigris_subversion_javahl_Revision_Kind_date:
		m_revision.kind = svn_opt_revision_date;
		{
			static jfieldID fidDate = 0;
			if(fidDate == 0)
			{
				jclass clazz = env->FindClass(JAVA_PACKAGE"/Revision$DateSpec");
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
				fidDate = env->GetFieldID(clazz, "revDate", "Ljava/util/Date;");
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
				env->DeleteLocalRef(clazz);
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
			}
			jobject jDate = env->GetObjectField(jthis, fidDate);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return;
			}

			static jmethodID mid = 0;
			if(mid == 0)
			{
				jclass clazz = env->FindClass("java/util/Date");
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
				fid = env->GetFieldID(clazz, "getTime", "()J");
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
				env->DeleteLocalRef(clazz);
				if(JNIUtil::isJavaExceptionThrown())
				{
					return;
				}
			}
			jlong jMillSec = env->CallLongMethod(jDate, mid);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return;
			}
			env->DeleteLocalRef(jDate);
			if(JNIUtil::isJavaExceptionThrown())
			{
				return;
			}
			m_revision.value.date = jMillSec * 1000;
		}
		break;
	case org_tigris_subversion_javahl_Revision_Kind_committed:
		m_revision.kind = svn_opt_revision_committed;
		break;
	case org_tigris_subversion_javahl_Revision_Kind_previous:
		m_revision.kind = svn_opt_revision_previous;
		break;
	case org_tigris_subversion_javahl_Revision_Kind_base:
		m_revision.kind = svn_opt_revision_base;
		break;
	case org_tigris_subversion_javahl_Revision_Kind_working:
		m_revision.kind = svn_opt_revision_working;
		break;
	case org_tigris_subversion_javahl_Revision_Kind_head:
		m_revision.kind = svn_opt_revision_head;
		break;
	}
	/*
	env->DeleteLocalRef(jthis);
	if(JNIUtil::isJavaExceptionThrown())
	{
		return;
	}
	*/
}

Revision::~Revision()
{

}
const svn_opt_revision_t *Revision::revision () const
{
    return &m_revision;
}
