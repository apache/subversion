Attribute VB_Name = "Startup"
'
' ====================================================================
' Copyright (c) 2000 CollabNet.  All rights reserved.
'
' This software is licensed as described in the file COPYING, which
' you should have received as part of this distribution.  The terms
' are also available at http://subversion.tigris.org/license-1.html.
' If newer versions of this license are posted there, you may use a
' newer version instead, at your option.
' ====================================================================
'
Option Explicit

Public fMainForm As frmMain
Public g_sComputerName As String
Public g_sListViewPath As String
Public g_SVN_WC As New SVNWorkingCopy


Sub Main()
    Dim fLogin As New frmLogin
    fLogin.Show vbModal
    If Not fLogin.OK Then
        'Login Failed so exit app
        End
    End If
    Unload fLogin

    frmSplash.Show
    frmSplash.Refresh
    g_sComputerName = GetComputerName
    Set fMainForm = New frmMain
    Load fMainForm
    Unload frmSplash

    fMainForm.Show
End Sub
Private Function RemoveResName(s As String) As String
    If InStr(1, s, "-") > 0 Then
        RemoveResName = Left(s, InStr(1, s, "-") - 1)
    Else
        RemoveResName = s
    End If
End Function
Sub LoadResStrings(frm As Form)
    On Error Resume Next

    Dim ctl As Control
    Dim obj As Object
    Dim fnt As Object
    Dim sCtlType As String
    Dim nVal As Integer


    'set the form's caption
'    frm.Caption = LoadResString(CInt(RemoveResName(frm.Tag)))

    'set the font
    Set fnt = frm.Font
    fnt.Name = LoadResString(1078)
    fnt.Size = CInt(LoadResString(1079))

    'set the controls' captions using the caption
    'property for menu items and the Tag property
    'for all other controls
    For Each ctl In frm.Controls
        Set ctl.Font = fnt
        sCtlType = TypeName(ctl)
        If sCtlType = "Label" Then
            ctl.Caption = LoadResString(CInt(RemoveResName(ctl.Tag)))
        ElseIf sCtlType = "Menu" Then
            ctl.Caption = LoadResString(CInt(RemoveResName(ctl.Caption)))
        ElseIf sCtlType = "TabStrip" Then
            For Each obj In ctl.Tabs
                obj.Caption = LoadResString(CInt(RemoveResName(obj.Tag)))
                obj.ToolTipText = LoadResString(CInt(RemoveResName(obj.ToolTipText)))
            Next
        ElseIf sCtlType = "Toolbar" Then
            For Each obj In ctl.Buttons
                obj.ToolTipText = LoadResString(CInt(RemoveResName(obj.ToolTipText)))
            Next
        ElseIf sCtlType = "ListView" Then
            For Each obj In ctl.ColumnHeaders
                obj.Text = LoadResString(CInt(RemoveResName(obj.Tag)))
            Next
        Else
            nVal = 0
            nVal = Val(ctl.Tag)
            If nVal > 0 Then ctl.Caption = LoadResString(nVal)
            nVal = 0
            nVal = Val(ctl.ToolTipText)
            If nVal > 0 Then ctl.ToolTipText = LoadResString(nVal)
        End If
    Next
End Sub

