VERSION 5.00
Object = "{F9043C88-F6F2-101A-A3C9-08002B2F49FB}#1.2#0"; "comdlg32.ocx"
Object = "{831FDD16-0C5C-11D2-A9FC-0000F8754DA1}#2.0#0"; "MSCOMCTL.OCX"
Object = "{396F7AC0-A0DD-11D3-93EC-00C0DFE7442A}#1.0#0"; "vbalIml6.ocx"
Object = "{4D4325C0-BD54-473C-8CE8-3C856739E161}#1.1#0"; "SVNControls.ocx"
Begin VB.Form frmMain 
   Caption         =   "WinSVN"
   ClientHeight    =   6780
   ClientLeft      =   132
   ClientTop       =   1032
   ClientWidth     =   6264
   LinkTopic       =   "Form1"
   ScaleHeight     =   6780
   ScaleWidth      =   6264
   StartUpPosition =   3  'Windows Default
   Begin vbalIml6.vbalImageList imlSGrid 
      Left            =   5640
      Top             =   1440
      _ExtentX        =   762
      _ExtentY        =   762
      ColourDepth     =   8
      Size            =   9400
      Images          =   "frmMain.frx":0000
      KeyCount        =   10
      Keys            =   "BINARYÿCONFLICTÿIGNORED_FILEÿMISSING_DIRÿMISSING_FILEÿMODIFIED_BINARYÿMODIFIED_NORMALÿNORMALÿPENDING_DELÿUNKNOWN"
   End
   Begin SVNControls.SGrid grdFiles 
      Height          =   4812
      Left            =   2160
      TabIndex        =   6
      Top             =   840
      Width           =   3132
      _ExtentX        =   5525
      _ExtentY        =   8488
      BackgroundPictureHeight=   0
      BackgroundPictureWidth=   0
      BeginProperty Font {0BE35203-8F91-11CE-9DE3-00AA004BB851} 
         Name            =   "MS Sans Serif"
         Size            =   7.8
         Charset         =   0
         Weight          =   400
         Underline       =   0   'False
         Italic          =   0   'False
         Strikethrough   =   0   'False
      EndProperty
      DisableIcons    =   -1  'True
   End
   Begin MSComctlLib.ImageList imlTreeView 
      Left            =   1320
      Top             =   2520
      _ExtentX        =   804
      _ExtentY        =   804
      BackColor       =   -2147483643
      ImageWidth      =   20
      ImageHeight     =   20
      MaskColor       =   12632256
      UseMaskColor    =   0   'False
      _Version        =   393216
      BeginProperty Images {2C247F25-8591-11D1-B16A-00C0F0283628} 
         NumListImages   =   9
         BeginProperty ListImage1 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":24D8
            Key             =   "REMOVABLE"
         EndProperty
         BeginProperty ListImage2 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2632
            Key             =   "NETDRIVE"
         EndProperty
         BeginProperty ListImage3 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2744
            Key             =   "HD"
         EndProperty
         BeginProperty ListImage4 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2856
            Key             =   "CD"
         EndProperty
         BeginProperty ListImage5 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2968
            Key             =   "CLOSED"
         EndProperty
         BeginProperty ListImage6 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2A7A
            Key             =   "OPEN"
         EndProperty
         BeginProperty ListImage7 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2B8C
            Key             =   "COMPUTER"
         EndProperty
         BeginProperty ListImage8 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2C9E
            Key             =   "SVNCLOSED"
         EndProperty
         BeginProperty ListImage9 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2DF8
            Key             =   "SVNOPEN"
         EndProperty
      EndProperty
   End
   Begin VB.PictureBox picSplitter 
      BackColor       =   &H00808080&
      BorderStyle     =   0  'None
      FillColor       =   &H00808080&
      Height          =   4800
      Left            =   5400
      ScaleHeight     =   2084.849
      ScaleMode       =   0  'User
      ScaleWidth      =   468
      TabIndex        =   5
      Top             =   705
      Visible         =   0   'False
      Width           =   72
   End
   Begin MSComDlg.CommonDialog dlgCommonDialog 
      Left            =   1440
      Top             =   1920
      _ExtentX        =   677
      _ExtentY        =   677
      _Version        =   393216
   End
   Begin MSComctlLib.StatusBar sbStatusBar 
      Align           =   2  'Align Bottom
      Height          =   264
      Left            =   0
      TabIndex        =   0
      Top             =   6516
      Width           =   6264
      _ExtentX        =   11049
      _ExtentY        =   466
      _Version        =   393216
      BeginProperty Panels {8E3867A5-8586-11D1-B16A-00C0F0283628} 
         NumPanels       =   3
         BeginProperty Panel1 {8E3867AB-8586-11D1-B16A-00C0F0283628} 
            AutoSize        =   1
            Object.Width           =   5440
            Text            =   "Status"
            TextSave        =   "Status"
         EndProperty
         BeginProperty Panel2 {8E3867AB-8586-11D1-B16A-00C0F0283628} 
            Style           =   6
            AutoSize        =   2
            TextSave        =   "2/5/2001"
         EndProperty
         BeginProperty Panel3 {8E3867AB-8586-11D1-B16A-00C0F0283628} 
            Style           =   5
            AutoSize        =   2
            TextSave        =   "12:45 AM"
         EndProperty
      EndProperty
   End
   Begin MSComctlLib.ImageList imlToolbarIcons 
      Left            =   2520
      Top             =   120
      _ExtentX        =   804
      _ExtentY        =   804
      BackColor       =   -2147483643
      ImageWidth      =   32
      ImageHeight     =   32
      MaskColor       =   12632256
      _Version        =   393216
      BeginProperty Images {2C247F25-8591-11D1-B16A-00C0F0283628} 
         NumListImages   =   2
         BeginProperty ListImage1 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":2F52
            Key             =   ""
         EndProperty
         BeginProperty ListImage2 {2C247F27-8591-11D1-B16A-00C0F0283628} 
            Picture         =   "frmMain.frx":33A4
            Key             =   ""
         EndProperty
      EndProperty
   End
   Begin MSComctlLib.Toolbar tbToolBar 
      Align           =   1  'Align Top
      Height          =   528
      Left            =   0
      TabIndex        =   1
      Top             =   0
      Width           =   6264
      _ExtentX        =   11049
      _ExtentY        =   931
      ButtonWidth     =   826
      ButtonHeight    =   804
      Appearance      =   1
      ImageList       =   "imlToolbarIcons"
      _Version        =   393216
      BeginProperty Buttons {66833FE8-8583-11D1-B16A-00C0F0283628} 
         NumButtons      =   2
         BeginProperty Button1 {66833FEA-8583-11D1-B16A-00C0F0283628} 
            Key             =   "Connect"
            Object.ToolTipText     =   "1040"
            ImageIndex      =   1
         EndProperty
         BeginProperty Button2 {66833FEA-8583-11D1-B16A-00C0F0283628} 
            Key             =   "Disconnect"
            ImageIndex      =   2
         EndProperty
      EndProperty
      Begin MSComctlLib.ImageList ilComboBox 
         Left            =   3840
         Top             =   120
         _ExtentX        =   804
         _ExtentY        =   804
         BackColor       =   -2147483643
         ImageWidth      =   20
         ImageHeight     =   20
         MaskColor       =   12632256
         UseMaskColor    =   0   'False
         _Version        =   393216
         BeginProperty Images {2C247F25-8591-11D1-B16A-00C0F0283628} 
            NumListImages   =   1
            BeginProperty ListImage1 {2C247F27-8591-11D1-B16A-00C0F0283628} 
               Picture         =   "frmMain.frx":37F6
               Key             =   ""
            EndProperty
         EndProperty
      End
   End
   Begin MSComctlLib.TreeView tvTreeView 
      Height          =   4788
      Left            =   0
      TabIndex        =   4
      Top             =   840
      Width           =   2016
      _ExtentX        =   3556
      _ExtentY        =   8446
      _Version        =   393217
      Indentation     =   353
      LabelEdit       =   1
      LineStyle       =   1
      Sorted          =   -1  'True
      Style           =   7
      FullRowSelect   =   -1  'True
      HotTracking     =   -1  'True
      ImageList       =   "imlTreeView"
      Appearance      =   1
   End
   Begin VB.PictureBox picTitles 
      Align           =   1  'Align Top
      Appearance      =   0  'Flat
      BorderStyle     =   0  'None
      ForeColor       =   &H80000008&
      Height          =   396
      Left            =   0
      ScaleHeight     =   396
      ScaleWidth      =   6264
      TabIndex        =   2
      TabStop         =   0   'False
      Top             =   528
      Width           =   6264
      Begin MSComctlLib.ImageCombo cmbDir 
         Height          =   300
         Left            =   0
         TabIndex        =   7
         Top             =   0
         Width           =   2052
         _ExtentX        =   3620
         _ExtentY        =   529
         _Version        =   393216
         ForeColor       =   -2147483640
         BackColor       =   -2147483643
         ImageList       =   "ilComboBox"
      End
      Begin VB.Label lblTitle 
         BorderStyle     =   1  'Fixed Single
         Caption         =   " ListView:"
         Height          =   276
         Index           =   1
         Left            =   2076
         TabIndex        =   3
         Tag             =   "1045"
         Top             =   12
         Width           =   3216
      End
   End
   Begin VB.Image imgSplitter 
      Height          =   4788
      Left            =   1968
      MousePointer    =   99  'Custom
      Top             =   840
      Width           =   156
   End
   Begin VB.Menu mnuFile 
      Caption         =   "1000 - File"
      Begin VB.Menu mnuFileOpen 
         Caption         =   "1001 - Open"
      End
      Begin VB.Menu mnuFileFind 
         Caption         =   "1002 - Find"
      End
      Begin VB.Menu mnuFileBar0 
         Caption         =   "-"
      End
      Begin VB.Menu mnuFileSendTo 
         Caption         =   "1003 - SendTo"
      End
      Begin VB.Menu mnuFileBar1 
         Caption         =   "-"
      End
      Begin VB.Menu mnuFileNew 
         Caption         =   "1004 - New"
         Shortcut        =   ^N
      End
      Begin VB.Menu mnuFileBar2 
         Caption         =   "-"
      End
      Begin VB.Menu mnuFileDelete 
         Caption         =   "1005 - Delete"
      End
      Begin VB.Menu mnuFileRename 
         Caption         =   "1006 - Rename"
      End
      Begin VB.Menu mnuFileProperties 
         Caption         =   "1007 - Properties"
      End
      Begin VB.Menu mnuFileBar3 
         Caption         =   "-"
      End
      Begin VB.Menu mnuFileMRU 
         Caption         =   ""
         Index           =   1
         Visible         =   0   'False
      End
      Begin VB.Menu mnuFileMRU 
         Caption         =   ""
         Index           =   2
         Visible         =   0   'False
      End
      Begin VB.Menu mnuFileMRU 
         Caption         =   ""
         Index           =   3
         Visible         =   0   'False
      End
      Begin VB.Menu mnuFileBar4 
         Caption         =   "-"
         Visible         =   0   'False
      End
      Begin VB.Menu mnuFileBar5 
         Caption         =   "-"
      End
      Begin VB.Menu mnuFileClose 
         Caption         =   "1008 - Close"
      End
   End
   Begin VB.Menu mnuEdit 
      Caption         =   "1009 - Edit"
      Begin VB.Menu mnuEditUndo 
         Caption         =   "1010 - Undo"
      End
      Begin VB.Menu mnuEditBar0 
         Caption         =   "-"
      End
      Begin VB.Menu mnuEditCut 
         Caption         =   "1011 - Cut"
         Shortcut        =   ^X
      End
      Begin VB.Menu mnuEditCopy 
         Caption         =   "1012 - Copy"
         Shortcut        =   ^C
      End
      Begin VB.Menu mnuEditPaste 
         Caption         =   "1013 - Paste"
         Shortcut        =   ^V
      End
      Begin VB.Menu mnuEditPasteSpecial 
         Caption         =   "1014 - PasteSpecial"
      End
      Begin VB.Menu mnuEditBar1 
         Caption         =   "-"
      End
      Begin VB.Menu mnuEditSelectAll 
         Caption         =   "1015 - Select All"
         Shortcut        =   ^A
      End
      Begin VB.Menu mnuEditInvertSelection 
         Caption         =   "1016 - InvertSelection"
      End
   End
   Begin VB.Menu mnuView 
      Caption         =   "1017 - View"
      Begin VB.Menu mnuViewToolbar 
         Caption         =   "1018 - Toolbar"
         Checked         =   -1  'True
      End
      Begin VB.Menu mnuViewStatusBar 
         Caption         =   "1019 -  StatusBar"
         Checked         =   -1  'True
      End
      Begin VB.Menu mnuViewBar2 
         Caption         =   "-"
      End
      Begin VB.Menu mnuViewRefresh 
         Caption         =   "1025 - Refresh"
      End
      Begin VB.Menu mnuViewOptions 
         Caption         =   "1026 - Options"
      End
      Begin VB.Menu mnuViewWebBrowser 
         Caption         =   "1027 - WebBrowser"
      End
   End
   Begin VB.Menu mnuTools 
      Caption         =   "1028 - Tools"
      Begin VB.Menu mnuToolsOptions 
         Caption         =   "1029 - Options"
      End
   End
   Begin VB.Menu mnuWindow 
      Caption         =   "1030 - Window"
      WindowList      =   -1  'True
      Begin VB.Menu mnuWindowNewWindow 
         Caption         =   "1031 - New"
      End
      Begin VB.Menu mnuWindowBar0 
         Caption         =   "-"
      End
      Begin VB.Menu mnuWindowCascade 
         Caption         =   "1032 - Cascade"
      End
      Begin VB.Menu mnuWindowTileHorizontal 
         Caption         =   "1033 - Tile Horiz"
      End
      Begin VB.Menu mnuWindowTileVertical 
         Caption         =   "1034 - Tile Vert"
      End
      Begin VB.Menu mnuWindowArrangeIcons 
         Caption         =   "1035 - ArrangeIcons"
      End
   End
   Begin VB.Menu mnuHelp 
      Caption         =   "1036 - Help"
      Begin VB.Menu mnuHelpContents 
         Caption         =   "1037"
      End
      Begin VB.Menu mnuHelpSearchForHelpOn 
         Caption         =   "1038"
      End
      Begin VB.Menu mnuHelpBar0 
         Caption         =   "-"
      End
      Begin VB.Menu mnuHelpAbout 
         Caption         =   "1039"
      End
   End
End
Attribute VB_Name = "frmMain"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
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

Private Declare Function OSWinHelp% Lib "user32" Alias "WinHelpA" (ByVal hwnd&, ByVal HelpFile$, ByVal wCommand%, dwData As Any)
Public g_oLVH As grdHelper
Public g_oTVH As ExplorerViewHelpers
Public g_oCDH As cmbDirHelper

Dim mbMoving As Boolean
Const sglSplitLimit = 500



Private Sub Form_Load()
    Dim oItem As ComboItem
    
    LoadResStrings Me
    imgSplitter.MouseIcon = LoadResPicture(101, vbResCursor)
    
    Set g_oLVH = New grdHelper
    Set g_oTVH = New ExplorerViewHelpers
    Set g_oCDH = New cmbDirHelper

    
    Me.Left = GetSetting(App.Title, "Settings", "MainLeft", 1000)
    Me.Top = GetSetting(App.Title, "Settings", "MainTop", 1000)
    Me.Width = GetSetting(App.Title, "Settings", "MainWidth", 6500)
    Me.Height = GetSetting(App.Title, "Settings", "MainHeight", 6500)
    
    g_oTVH.InitTreeView
    
    Set oItem = cmbDir.ComboItems.Add(, "___COMPUTER", "My Computer", 1)
    oItem.Selected = True
End Sub
Private Sub Form_Unload(Cancel As Integer)
    Dim i As Integer

    'close all sub forms
    For i = Forms.Count - 1 To 1 Step -1
        Unload Forms(i)
    Next
    If Me.WindowState <> vbMinimized Then
        SaveSetting App.Title, "Settings", "MainLeft", Me.Left
        SaveSetting App.Title, "Settings", "MainTop", Me.Top
        SaveSetting App.Title, "Settings", "MainWidth", Me.Width
        SaveSetting App.Title, "Settings", "MainHeight", Me.Height
    End If
End Sub

Private Sub Form_Resize()
    On Error Resume Next
    If Me.Width < 3000 Then Me.Width = 3000
    SizeControls imgSplitter.Left
End Sub

Private Sub imgSplitter_MouseDown(Button As Integer, Shift As Integer, X As Single, Y As Single)
    With imgSplitter
        picSplitter.Move .Left, .Top, .Width \ 2, .Height - 20
    End With
    picSplitter.Visible = True
    mbMoving = True
End Sub

Private Sub imgSplitter_MouseMove(Button As Integer, Shift As Integer, X As Single, Y As Single)
    Dim sglPos As Single
    
    If mbMoving Then
        sglPos = X + imgSplitter.Left
        If sglPos < sglSplitLimit Then
            picSplitter.Left = sglSplitLimit
        ElseIf sglPos > Me.Width - sglSplitLimit Then
            picSplitter.Left = Me.Width - sglSplitLimit
        Else
            picSplitter.Left = sglPos
        End If
    End If
End Sub

Private Sub imgSplitter_MouseUp(Button As Integer, Shift As Integer, X As Single, Y As Single)
    SizeControls picSplitter.Left
    picSplitter.Visible = False
    mbMoving = False
End Sub

'Private Sub tvTreeView_DragDrop(Source As Control, X As Single, Y As Single)
'    If Source = imgSplitter Then
'        SizeControls X
'    End If
'End Sub

Sub SizeControls(X As Single)
    'On Error Resume Next
    grdFiles.Redraw = False
    
    ' set the width
    If X < 1500 Then X = 1500
    If X > (Me.Width - 1500) Then X = Me.Width - 1500
    tvTreeView.Width = X
    imgSplitter.Left = X
    grdFiles.Left = X + 40
    grdFiles.Width = Me.Width - (tvTreeView.Width + 140)
    cmbDir.Width = tvTreeView.Width
    lblTitle(1).Left = grdFiles.Left + 20
    lblTitle(1).Width = grdFiles.Width - 40
    lblTitle(1).Height = cmbDir.Height

    ' set the top
    If tbToolBar.Visible Then
        tvTreeView.Top = tbToolBar.Height + picTitles.Height
    Else
        tvTreeView.Top = picTitles.Height
    End If

    grdFiles.Top = tvTreeView.Top

    ' set the height
    If sbStatusBar.Visible Then
        tvTreeView.Height = Me.ScaleHeight - (picTitles.Top + picTitles.Height + sbStatusBar.Height)
    Else
        tvTreeView.Height = Me.ScaleHeight - (picTitles.Top + picTitles.Height)
    End If

    grdFiles.Height = tvTreeView.Height
    imgSplitter.Top = tvTreeView.Top
    imgSplitter.Height = tvTreeView.Height
    ' Dragging the splitter would show the grid not
    ' redrawing appropriatly, so force it to update.
    grdFiles.Redraw = True
End Sub

Private Sub tbToolBar_ButtonClick(ByVal Button As MSComctlLib.Button)
    On Error Resume Next
    Select Case Button.Key
    'Todo: Blah
    End Select
End Sub

Private Sub mnuHelpAbout_Click()
    frmAbout.Show vbModal, Me
End Sub

Private Sub mnuHelpSearchForHelpOn_Click()
    Dim nRet As Integer

    'if there is no helpfile for this project display a message to the user
    'you can set the HelpFile for your application in the
    'Project Properties dialog
    If Len(App.HelpFile) = 0 Then
        MsgBox "Unable to display Help Contents. There is no Help associated with this project.", vbInformation, Me.Caption
    Else
        On Error Resume Next
        nRet = OSWinHelp(Me.hwnd, App.HelpFile, 261, 0)
        If Err Then
            MsgBox Err.Description
        End If
    End If

End Sub

Private Sub mnuHelpContents_Click()
    Dim nRet As Integer

    'if there is no helpfile for this project display a message to the user
    'you can set the HelpFile for your application in the
    'Project Properties dialog
    If Len(App.HelpFile) = 0 Then
        MsgBox "Unable to display Help Contents. There is no Help associated with this project.", vbInformation, Me.Caption
    Else
        On Error Resume Next
        nRet = OSWinHelp(Me.hwnd, App.HelpFile, 3, 0)
        If Err Then
            MsgBox Err.Description
        End If
    End If

End Sub

Private Sub mnuWindowTileVertical_Click()
    'ToDo: Add 'mnuWindowTileVertical_Click' code.
    MsgBox "Add 'mnuWindowTileVertical_Click' code."
End Sub

Private Sub mnuWindowTileHorizontal_Click()
    'ToDo: Add 'mnuWindowTileHorizontal_Click' code.
    MsgBox "Add 'mnuWindowTileHorizontal_Click' code."
End Sub

Private Sub mnuWindowCascade_Click()
    'ToDo: Add 'mnuWindowCascade_Click' code.
    MsgBox "Add 'mnuWindowCascade_Click' code."
End Sub

Private Sub mnuWindowNewWindow_Click()
    'ToDo: Add 'mnuWindowNewWindow_Click' code.
    MsgBox "Add 'mnuWindowNewWindow_Click' code."
End Sub

Private Sub mnuToolsOptions_Click()
    frmOptions.Show vbModal, Me
End Sub

Private Sub mnuViewWebBrowser_Click()
    'ToDo: Add 'mnuViewWebBrowser_Click' code.
    MsgBox "Add 'mnuViewWebBrowser_Click' code."
End Sub

Private Sub mnuViewOptions_Click()
    frmOptions.Show vbModal, Me
End Sub

Private Sub mnuViewRefresh_Click()
    g_oLVH.PopulateSGrid g_sListViewPath
End Sub

Private Sub mnuViewStatusBar_Click()
    mnuViewStatusBar.Checked = Not mnuViewStatusBar.Checked
    sbStatusBar.Visible = mnuViewStatusBar.Checked
    SizeControls imgSplitter.Left
End Sub

Private Sub mnuViewToolbar_Click()
    mnuViewToolbar.Checked = Not mnuViewToolbar.Checked
    tbToolBar.Visible = mnuViewToolbar.Checked
    SizeControls imgSplitter.Left
End Sub

Private Sub mnuEditInvertSelection_Click()
    'ToDo: Add 'mnuEditInvertSelection_Click' code.
    MsgBox "Add 'mnuEditInvertSelection_Click' code."
End Sub

Private Sub mnuEditSelectAll_Click()
    'ToDo: Add 'mnuEditSelectAll_Click' code.
    MsgBox "Add 'mnuEditSelectAll_Click' code."
End Sub

Private Sub mnuEditPasteSpecial_Click()
    'ToDo: Add 'mnuEditPasteSpecial_Click' code.
    MsgBox "Add 'mnuEditPasteSpecial_Click' code."
End Sub

Private Sub mnuEditPaste_Click()
    'ToDo: Add 'mnuEditPaste_Click' code.
    MsgBox "Add 'mnuEditPaste_Click' code."
End Sub

Private Sub mnuEditCopy_Click()
    'ToDo: Add 'mnuEditCopy_Click' code.
    MsgBox "Add 'mnuEditCopy_Click' code."
End Sub

Private Sub mnuEditCut_Click()
    'ToDo: Add 'mnuEditCut_Click' code.
    MsgBox "Add 'mnuEditCut_Click' code."
End Sub

Private Sub mnuEditUndo_Click()
    'ToDo: Add 'mnuEditUndo_Click' code.
    MsgBox "Add 'mnuEditUndo_Click' code."
End Sub

Private Sub mnuFileClose_Click()
    'unload the form
    Unload Me

End Sub

Private Sub mnuFileProperties_Click()
    'ToDo: Add 'mnuFileProperties_Click' code.
    MsgBox "Add 'mnuFileProperties_Click' code."
End Sub

Private Sub mnuFileRename_Click()
    'ToDo: Add 'mnuFileRename_Click' code.
    MsgBox "Add 'mnuFileRename_Click' code."
End Sub

Private Sub mnuFileDelete_Click()
    'ToDo: Add 'mnuFileDelete_Click' code.
    MsgBox "Add 'mnuFileDelete_Click' code."
End Sub

Private Sub mnuFileNew_Click()
    'ToDo: Add 'mnuFileNew_Click' code.
    MsgBox "Add 'mnuFileNew_Click' code."
End Sub

Private Sub mnuFileSendTo_Click()
    'ToDo: Add 'mnuFileSendTo_Click' code.
    MsgBox "Add 'mnuFileSendTo_Click' code."
End Sub

Private Sub mnuFileFind_Click()
    'ToDo: Add 'mnuFileFind_Click' code.
    MsgBox "Add 'mnuFileFind_Click' code."
End Sub

Private Sub mnuFileOpen_Click()
    Dim sFile As String


    With dlgCommonDialog
        .DialogTitle = "Open"
        .CancelError = False
        'ToDo: set the flags and attributes of the common dialog control
        .Filter = "All Files (*.*)|*.*"
        .ShowOpen
        If Len(.FileName) = 0 Then
            Exit Sub
        End If
        sFile = .FileName
    End With
    'ToDo: add code to process the opened file

End Sub


