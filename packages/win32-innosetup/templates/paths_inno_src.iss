; This is where you can let Inno Setup know where to find the files included in
; the setup.
; The paths can be relative to the "packages\win32-innosetup" path in the
; Subversion repository or they can be absolute (full path).
; This file must reside in the "packages\win32-innosetup" directory so copy it
; to there and edit it.
; This file is ignored by the Subversion repository when it resides in
; "packages\win32-innosetup". That's because the many people who want to make
; distros for Subversion have different preferences (compilers, sources, etc.).
;
; #############################################################################
; Edit this variables and make sure that all files are where they should be
; according to this file
;
; Misc files such as svn.chm goes here:
#define path_setup_in "in"
; This is where the final setup ends
#define path_setup_out "out"
; Path to ISX
#define path_isx "E:\inno\isx"

; Example - Downloaded files --------------------------------------------------
; Here is a example (commented) if you want to compile a setup from files
; already found at the Subversion site and other places when downloaded and
; extracted somwhere on a machine.
; You should edit the paths below the examples according to your system

;; Subversion stuff
;#define path_svnclient "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_svnadmin "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_svnlook "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_svnserve "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_svnversion "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_svndumpfilter "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_davsvn "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_authzsvn "C:\svn_innosrc\svn-win32-rXXXX"
;#define path_svnpath "tools\svnpath"
;; Berkeley stuff
;#define path_brkdb_bin "C:\svn_innosrc\db4-win32\bin"
;#define path_brkdb_lib "C:\svn_innosrc\db4-win32\lib"
;#define path_brkdb_inc "C:\svn_innosrc\db4-win32\include"
;#define path_brkdb_inc2 "C:\svn_innosrc\db4-win32\include"
;; Openssl
;#define path_ssl "C:\svn_innosrc\svn-win32-rXXXX"
;; Misc
;#define path_iconv "C:\svn_innosrc\svn-win32-rXXXX\iconv"

; Default setup - Edit this paths so they fits to your system #################
; Subversion stuff
#define path_svnclient "..\..\subversion\clients\cmdline\Release"
#define path_svnadmin "..\..\subversion\svnadmin\Release"
#define path_svnlook "..\..\subversion\svnlook\Release"
#define path_svnserve "..\..\subversion\svnserve\Release"
#define path_svnversion "..\..\subversion\svnversion\Release"
#define path_svndumpfilter "..\..\subversion\svndumpfilter\Release"
#define path_davsvn "..\..\subversion\mod_dav_svn\Release"
#define path_authzsvn "..\..\subversion\mod_authz_svn\Release"
#define path_svnpath "tools\svnpath"
; Berkeley stuff
#define path_brkdb "E:\src\db-4.0.14"
#define path_brkdb_bin "E:\src\db-4.0.14\build_win32\Release"
#define path_brkdb_lib "E:\src\db-4.0.14\build_win32\Release"
#define path_brkdb_inc "E:\src\db-4.0.14\build_win32"
#define path_brkdb_inc2 "E:\src\db-4.0.14\dbinc
; Openssl
#define path_ssl "E:\src\l\openssl-0.9.6g\out32dll"
; Misc
#define path_iconv "C:\path\to\the\.so\files"


