@ECHO OFF
SET SetupFiles=setup.0 setup.msg svn-setup-1.bin svn-setup.exe
<%tv_7zip_exe%> a -t7z -ms -mx setup.7z %SetupFiles%
copy /b <%tv_7zip_sfx%> + 7z.conf + setup.7z svn-<%tv_version%>-r<%tv_release%>-setup.exe
del %SetupFiles% setup.7z
