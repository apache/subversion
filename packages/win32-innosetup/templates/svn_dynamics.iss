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
; Path to Inno Setup Executable
#define path_is "E:\Inno Setup4"

;Build defines ----------------------------------------------------------------
; Uncomment the defines if true
#define inc_dbgsyms
#define VC7

; You should edit the paths below the examples according to your system

;Subversion Core --------------------------------------------------------------
; Basic files
#define path_svnclient "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnadmin "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnlook "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnserve "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnversion "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svndumpfilter "C:\work\Subversion\binaries_svn\svn-win32\bin"
; httpd
#define path_davsvn "C:\work\Subversion\binaries_svn\svn-win32\httpd"
#define path_authzsvn "C:\work\Subversion\binaries_svn\svn-win32\httpd"
; Misc
#define path_msvcr70_dll "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_brkdb_dll "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnpath "tools\svnpath"
#define path_iconv "C:\work\Subversion\binaries_svn\svn-win32\iconv"
#define path_ssl "C:\work\Subversion\binaries_svn\svn-win32\bin"

;Debug symbols ----------------------------------------------------------------
; Basic files
#define path_svnclient_pdb "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnadmin_pdb "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnlook_pdb "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnserve_pdb "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svnversion_pdb "C:\work\Subversion\binaries_svn\svn-win32\bin"
#define path_svndumpfilter_pdb "C:\work\Subversion\binaries_svn\svn-win32\bin"
; httpd
#define path_davsvn_pdb "C:\work\Subversion\binaries_svn\svn-win32\httpd"
#define path_authzsvn_pdb "C:\work\Subversion\binaries_svn\svn-win32\httpd"
; Misc
#define path_iconv_pdb "C:\work\Subversion\binaries_svn\svn-win32\iconv"
#define path_libapr_pdb "C:\work\Subversion\binaries_svn\svn-win32\lib\apr"
#define path_libapu_pdb "C:\work\Subversion\binaries_svn\svn-win32\lib\apr-util"
#define path_py_libsvn_pdb "C:\work\Subversion\binaries_svn\svn-win32\python\libsvn"

;Development -------------------------------------------------------------------
#define path_dev_doc "C:\work\Subversion\binaries_svn\svn-win32\doc"
#define path_dev_inc "C:\work\Subversion\binaries_svn\svn-win32\include"
#define path_dev_inc_apr "C:\work\Subversion\binaries_svn\svn-win32\include\apr"
#define path_dev_inc_apu "C:\work\Subversion\binaries_svn\svn-win32\include\apr-util"
#define path_dev_lib "C:\work\Subversion\binaries_svn\svn-win32\lib"
#define path_dev_lib_apr "C:\work\Subversion\binaries_svn\svn-win32\lib\apr"
#define path_dev_lib_apu "C:\work\Subversion\binaries_svn\svn-win32\lib\apr-util"

;Python bindings
#define path_py_bind_libsvn "C:\work\Subversion\binaries_svn\python_bindings\libsvn"
#define path_py_bind_svn "C:\work\Subversion\binaries_svn\python_bindings\svn"

