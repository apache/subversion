lib_LTLIBRARIES = libsvnjavahl.la

libsvnjavahl_la_LIBADD = -lstdc++ @SVN_LIBS@ 

libsvnjavahl_la_SOURCES=SVNClient.cpp JNICriticalSection.cpp JNIMutex.cpp JNIStackElement.cpp JNIStringHolder.cpp JNIThreadData.cpp JNIUtil.cpp JNIByteArray.cpp Notify.cpp Path.cpp Pool.cpp Prompter.cpp Revision.cpp Targets.cpp org_tigris_subversion_javahl_SVNClient.cpp libsvnjavahl.la.c

BUILT_SOURCES = org_tigris_subversion_javahl_SVNClient.h \
		org_tigris_subversion_javahl_SVNClient_LogLevel.h \
		org_tigris_subversion_javahl_NodeKind.h \
		org_tigris_subversion_javahl_Notify_Action.h \
		org_tigris_subversion_javahl_Notify_Status.h \
		org_tigris_subversion_javahl_Revision.h \
		org_tigris_subversion_javahl_Revision_Kind.h \
		org_tigris_subversion_javahl_Status_Kind.h

EXTRA_DIST = svnjavahl.dsw svnjavahl.dsp 

noinst_HEADERS = SVNClient.h JNICriticalSection.h JNIMutex.h JNIStackElement.h JNIStringHolder.h JNIThreadData.h JNIUtil.h JNIByteArray.h Notify.h Path.h Pool.h Prompter.h Revision.h Targets.h version.h

JNI_CLS_DIR=../cls/org/tigris/subversion/javahl
JNI_PACKAGE=org.tigris.subversion.javahl
JAVAH=@JDK@/bin/javah -force -verbose -classpath ../cls

org_tigris_subversion_javahl_SVNClient.h: $(JNI_CLS_DIR)/SVNClient.class 
	$(JAVAH) $(JNI_PACKAGE).SVNClient

org_tigris_subversion_javahl_SVNClient_LogLevel.h: $(JNI_CLS_DIR)/SVNClient$$LogLevel.class 
	$(JAVAH) $(JNI_PACKAGE).SVNClient\$$LogLevel

org_tigris_subversion_javahl_NodeKind.h: $(JNI_CLS_DIR)/NodeKind.class 
	$(JAVAH) $(JNI_PACKAGE).NodeKind

org_tigris_subversion_javahl_Notify_Action.h: $(JNI_CLS_DIR)/Notify$$Action.class 
	$(JAVAH) $(JNI_PACKAGE).Notify\$$Action

org_tigris_subversion_javahl_Notify_Status.h: $(JNI_CLS_DIR)/Notify$$Status.class 
	$(JAVAH) $(JNI_PACKAGE).Notify\$$Status

org_tigris_subversion_javahl_Revision.h: $(JNI_CLS_DIR)/Revision.class 
	$(JAVAH) $(JNI_PACKAGE).Revision

org_tigris_subversion_javahl_Revision_Kind.h: $(JNI_CLS_DIR)/Revision$$Kind.class 
	$(JAVAH) $(JNI_PACKAGE).Revision\$$Kind

org_tigris_subversion_javahl_Status_Kind.h: $(JNI_CLS_DIR)/Status$$Kind.class 
	$(JAVAH) $(JNI_PACKAGE).Status\$$Kind

