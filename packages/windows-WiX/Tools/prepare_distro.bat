cd ..\..\WixDialog
call build.bat
cd ..\..\Tools\
perl set_version.pl -a
perl mk_svndoc.pl
perl update_build_files.pl
