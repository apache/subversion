
COMps.dll: dlldata.obj COM_p.obj COM_i.obj
	link /dll /out:COMps.dll /def:COMps.def /entry:DllMain dlldata.obj COM_p.obj COM_i.obj \
		kernel32.lib rpcndr.lib rpcns4.lib rpcrt4.lib oleaut32.lib uuid.lib \

.c.obj:
	cl /c /Ox /DWIN32 /D_WIN32_WINNT=0x0400 /DREGISTER_PROXY_DLL \
		$<

clean:
	@del COMps.dll
	@del COMps.lib
	@del COMps.exp
	@del dlldata.obj
	@del COM_p.obj
	@del COM_i.obj
