candle.exe -dinfoRtf=${uiextension.wixlib.dir.src}\Pre.rtf -dpostRtf=${uiextension.wixlib.dir.src}\Post.rtf WixUI_Subversion.wxs Infodlg.wxs WelcomeDlgSv.wxs PostDlg.wxs
lit.exe -out WixUI_Subversion.wixlib WixUI_Subversion.wixobj infodlg.wixobj WelcomeDlgSv.wixobj PostDlg.wixobj
