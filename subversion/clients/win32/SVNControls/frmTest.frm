VERSION 5.00
Object = "{396F7AC0-A0DD-11D3-93EC-00C0DFE7442A}#1.0#0"; "vbalIml6.ocx"
Object = "{831FDD16-0C5C-11D2-A9FC-0000F8754DA1}#2.0#0"; "MSCOMCTL.OCX"
Object = "{4D4325C0-BD54-473C-8CE8-3C856739E161}#1.0#0"; "SVNControls.ocx"
Begin VB.Form frmDemo 
   Caption         =   "SGrid Demonstrator"
   ClientHeight    =   7608
   ClientLeft      =   888
   ClientTop       =   888
   ClientWidth     =   9204
   BeginProperty Font 
      Name            =   "Tahoma"
      Size            =   8.4
      Charset         =   0
      Weight          =   400
      Underline       =   0   'False
      Italic          =   0   'False
      Strikethrough   =   0   'False
   EndProperty
   Icon            =   "frmTest.frx":0000
   LinkTopic       =   "Form1"
   ScaleHeight     =   7608
   ScaleWidth      =   9204
   Begin SVNControls.SGrid grdThis 
      Height          =   5412
      Left            =   120
      TabIndex        =   28
      Top             =   240
      Width           =   5292
      _ExtentX        =   9335
      _ExtentY        =   9546
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
      Begin VB.TextBox txtEdit 
         Height          =   300
         Left            =   2160
         TabIndex        =   29
         Text            =   "Text1"
         Top             =   1200
         Width           =   972
      End
   End
   Begin MSComctlLib.ListView ListView1 
      Height          =   492
      Left            =   5640
      TabIndex        =   27
      Top             =   1320
      Width           =   492
      _ExtentX        =   868
      _ExtentY        =   868
      LabelWrap       =   -1  'True
      HideSelection   =   -1  'True
      _Version        =   393217
      ForeColor       =   -2147483640
      BackColor       =   -2147483643
      BorderStyle     =   1
      Appearance      =   1
      NumItems        =   2
      BeginProperty ColumnHeader(1) {BDD1F052-858B-11D1-B16A-00C0F0283628} 
         Key             =   "Blah"
         Text            =   "Blah"
         Object.Width           =   2540
      EndProperty
      BeginProperty ColumnHeader(2) {BDD1F052-858B-11D1-B16A-00C0F0283628} 
         SubItemIndex    =   1
         Key             =   "Blah2"
         Text            =   "Blah2"
         Object.Width           =   2540
      EndProperty
   End
   Begin vbalIml6.vbalImageList ilsIcons 
      Left            =   5760
      Top             =   180
      _ExtentX        =   762
      _ExtentY        =   762
      IconSizeX       =   24
      IconSizeY       =   24
      ColourDepth     =   24
      Size            =   50856
      Images          =   "frmTest.frx":0442
      KeyCount        =   26
      Keys            =   "ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ"
   End
   Begin VB.PictureBox picBackground 
      Height          =   1515
      Left            =   4560
      Picture         =   "frmTest.frx":CB0A
      ScaleHeight     =   1464
      ScaleWidth      =   1524
      TabIndex        =   24
      TabStop         =   0   'False
      Top             =   2520
      Visible         =   0   'False
      Width           =   1575
   End
   Begin VB.PictureBox picStatus 
      Align           =   2  'Align Bottom
      Height          =   315
      Left            =   0
      ScaleHeight     =   264
      ScaleWidth      =   9156
      TabIndex        =   23
      TabStop         =   0   'False
      Top             =   7290
      Width           =   9204
   End
   Begin VB.Frame fraOptions 
      Height          =   7095
      Left            =   6300
      TabIndex        =   21
      Top             =   60
      Width           =   2175
      Begin VB.CheckBox chkFlatHeader 
         Appearance      =   0  'Flat
         Caption         =   "&Flat Header"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   26
         Top             =   1980
         Value           =   1  'Checked
         Width           =   1935
      End
      Begin VB.CommandButton cmdGetSel 
         Caption         =   "&Selected"
         Height          =   435
         Left            =   1200
         TabIndex        =   25
         Top             =   780
         Width           =   915
      End
      Begin VB.CheckBox chkDrawFocusRect 
         Appearance      =   0  'Flat
         Caption         =   "Dra&w Focus Rectangle"
         ForeColor       =   &H80000008&
         Height          =   255
         Left            =   60
         TabIndex        =   10
         Top             =   4020
         Value           =   1  'Checked
         Width           =   1935
      End
      Begin VB.CheckBox chkHighlightSelectedIcons 
         Appearance      =   0  'Flat
         Caption         =   "Highlight Selected Ico&ns"
         ForeColor       =   &H80000008&
         Height          =   315
         Left            =   60
         TabIndex        =   9
         Top             =   3660
         Value           =   1  'Checked
         Width           =   1935
      End
      Begin VB.CommandButton cmdAutoRowHeight 
         Caption         =   "Fit &Heights"
         Height          =   375
         Left            =   1080
         TabIndex        =   18
         Top             =   6120
         Width           =   975
      End
      Begin VB.CheckBox chkEnabled 
         Appearance      =   0  'Flat
         Caption         =   "E&nabled"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   4
         Top             =   2220
         Value           =   1  'Checked
         Width           =   1935
      End
      Begin VB.CheckBox chkEditable 
         Appearance      =   0  'Flat
         Caption         =   "&Editable"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   5
         Top             =   2580
         Width           =   1815
      End
      Begin VB.CommandButton cmdRemoveCol 
         Caption         =   "&Del Col..."
         Height          =   375
         Left            =   1080
         TabIndex        =   20
         Top             =   6540
         Width           =   975
      End
      Begin VB.CommandButton cmdAddCol 
         Caption         =   "&Add Col..."
         Height          =   375
         Left            =   60
         TabIndex        =   19
         Top             =   6540
         Width           =   975
      End
      Begin VB.CheckBox chkRnd 
         Appearance      =   0  'Flat
         Caption         =   "Ran&dom Row Heights"
         ForeColor       =   &H80000008&
         Height          =   255
         Left            =   60
         TabIndex        =   17
         Top             =   5880
         Value           =   1  'Checked
         Width           =   1935
      End
      Begin VB.CheckBox chkCol4 
         Appearance      =   0  'Flat
         Caption         =   "Date Column &Visible"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   8
         Top             =   3300
         Width           =   1995
      End
      Begin VB.TextBox txtRows 
         Height          =   285
         Left            =   60
         TabIndex        =   16
         Text            =   "50"
         Top             =   5580
         Width           =   2010
      End
      Begin VB.CommandButton cmdRepopulate 
         Caption         =   "&Repopulate"
         Height          =   375
         Left            =   1080
         TabIndex        =   15
         Top             =   5160
         Width           =   975
      End
      Begin VB.CommandButton cmdEmpty 
         Caption         =   "&Clear"
         Height          =   375
         Left            =   60
         TabIndex        =   14
         Top             =   5160
         Width           =   975
      End
      Begin VB.CheckBox chkHeaderButtons 
         Appearance      =   0  'Flat
         Caption         =   "Header Bu&ttons"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   3
         Top             =   1740
         Value           =   1  'Checked
         Width           =   1935
      End
      Begin VB.CheckBox chkHeader 
         Appearance      =   0  'Flat
         Caption         =   "&Header"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   2
         Top             =   1500
         Value           =   1  'Checked
         Width           =   1935
      End
      Begin VB.CheckBox chkItalic 
         Appearance      =   0  'Flat
         Caption         =   "&Italic"
         ForeColor       =   &H80000008&
         Height          =   255
         Left            =   60
         TabIndex        =   12
         Top             =   4680
         Width           =   975
      End
      Begin VB.CheckBox chkBold 
         Appearance      =   0  'Flat
         Caption         =   "&Bold"
         ForeColor       =   &H80000008&
         Height          =   255
         Left            =   60
         TabIndex        =   11
         Top             =   4440
         Width           =   975
      End
      Begin VB.CheckBox chkBackground 
         Appearance      =   0  'Flat
         Caption         =   "&Background Bitmap"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   6
         Top             =   2820
         Width           =   1815
      End
      Begin VB.CommandButton cmdCellText 
         Caption         =   "&Cell Text..."
         Height          =   375
         Left            =   1080
         TabIndex        =   13
         Top             =   4500
         Width           =   975
      End
      Begin VB.CheckBox chkVisible 
         Appearance      =   0  'Flat
         Caption         =   "Show &Odd Rows only"
         ForeColor       =   &H80000008&
         Height          =   195
         Left            =   60
         TabIndex        =   7
         Top             =   3060
         Width           =   1995
      End
      Begin VB.CheckBox chkOptions 
         Appearance      =   0  'Flat
         Caption         =   "&Grid-Lines"
         ForeColor       =   &H80000008&
         Height          =   255
         Index           =   2
         Left            =   60
         TabIndex        =   1
         Top             =   1260
         Width           =   1995
      End
      Begin VB.CheckBox chkOptions 
         Appearance      =   0  'Flat
         Caption         =   "&Row Mode"
         ForeColor       =   &H80000008&
         Height          =   255
         Index           =   1
         Left            =   60
         TabIndex        =   0
         Top             =   1020
         Width           =   1995
      End
      Begin VB.Image Image2 
         Height          =   384
         Left            =   1080
         Picture         =   "frmTest.frx":D366
         Top             =   180
         Width           =   780
      End
      Begin VB.Image Image1 
         Height          =   384
         Left            =   120
         Picture         =   "frmTest.frx":D985
         Top             =   180
         Width           =   768
      End
      Begin VB.Label Label1 
         BackColor       =   &H00000000&
         BorderStyle     =   1  'Fixed Single
         Caption         =   "Label1"
         Height          =   615
         Left            =   60
         TabIndex        =   22
         Top             =   120
         Width           =   2055
      End
   End
   Begin VB.Menu mnuDemoTOP 
      Caption         =   "&Demo"
      Begin VB.Menu mnuDemo 
         Caption         =   "&Outlook Style..."
         Index           =   0
      End
      Begin VB.Menu mnuDemo 
         Caption         =   "&Matrix..."
         Index           =   1
      End
      Begin VB.Menu mnuDemo 
         Caption         =   "&Virtual Grid..."
         Index           =   2
      End
      Begin VB.Menu mnuDemo 
         Caption         =   "-"
         Index           =   3
      End
      Begin VB.Menu mnuDemo 
         Caption         =   "E&xit"
         Index           =   4
      End
   End
End
Attribute VB_Name = "frmDemo"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

' ======================================================================================
' Name:     vbAcceleratorGrid Control Demo
' Author:   Steve McMahon (steve@dogma.demon.co.uk)
' Date:     22 December 1998
'
' Requires: SSubTmr.DLL
'           vbalGrid.OCX
'
' Copyright © 1998-1999 Steve McMahon for vbAccelerator
' --------------------------------------------------------------------------------------
' Visit vbAccelerator - advanced free source code for VB programmers
' http://vbaccelerator.com
' --------------------------------------------------------------------------------------
'
' Demonstrates the features of the vbAccelerator grid control.
'
' Features:
'
'  * Drag-drop columns
'  * Visible or invisible columns
'  * Row height can be set independently for each row
'  * MS Common Controls or vbAccelerator ImageList support
'  * Up to two icons per cell (e.g. a check box and a standard icon)
'  * Indent text within any cell
'  * Many cell text formatting options including multi-line text
'  * Show/Hide rows to allow filtering options
'  * Show/Hide columns
'  * Scroll bars implemented using true API scroll bars, and support flat/encarta style
'  * Up to 2 billion rows and columns (although practically about 20,000 is the limit)
'  * Full row sorting by up to three columns at once, allows sorting by icon, text,
'    date/time or number.
'  * Autosize columns
'
' FREE SOURCE CODE - ENJOY!
' ======================================================================================


Private m_sStatus As String
Private m_iValue As Long
Private m_iMax As Long

Private Declare Function GetWindowLong Lib "user32" Alias "GetWindowLongA" (ByVal hwnd As Long, ByVal nIndex As Long) As Long
Private Declare Function SetWindowLong Lib "user32" Alias "SetWindowLongA" (ByVal hwnd As Long, ByVal nIndex As Long, ByVal dwNewLong As Long) As Long
Private Const GWL_EXSTYLE = (-20)
Private Const WS_EX_WINDOWEDGE = &H100
Private Const WS_EX_CLIENTEDGE = &H200
Private Const WS_EX_STATICEDGE = &H20000
Private Declare Function SetWindowPos Lib "user32" (ByVal hwnd As Long, ByVal hWndInsertAfter As Long, ByVal x As Long, ByVal y As Long, ByVal cx As Long, ByVal cy As Long, ByVal wFlags As Long) As Long
Private Enum ESetWindowPosStyles
    SWP_SHOWWINDOW = &H40
    SWP_HIDEWINDOW = &H80
    SWP_FRAMECHANGED = &H20 ' The frame changed: send WM_NCCALCSIZE
    SWP_NOACTIVATE = &H10
    SWP_NOCOPYBITS = &H100
    SWP_NOMOVE = &H2
    SWP_NOOWNERZORDER = &H200 ' Don't do owner Z ordering
    SWP_NOREDRAW = &H8
    SWP_NOREPOSITION = SWP_NOOWNERZORDER
    SWP_NOSIZE = &H1
    SWP_NOZORDER = &H4
    SWP_DRAWFRAME = SWP_FRAMECHANGED
    HWND_NOTOPMOST = -2
End Enum

Private Sub TestVeryLongText()
Dim sOut As String
Dim i As Long
   For i = 1 To 4096
      If Rnd < 0.2 Then
         sOut = sOut & " "
      Else
         sOut = sOut & Chr$(Rnd * 26 + Asc("A"))
      End If
   Next i
   grdThis.CellText(1, 5) = sOut
   
   ' test visible...
   grdThis.Redraw = False
   grdThis.CellSelected(48, 2) = True
   grdThis.Redraw = True
   
End Sub


Private Sub ThinBorder(ByVal hwnd As Long, ByVal bState As Boolean)
Dim lStyle As Long
   ' Thin border:
   lStyle = GetWindowLong(hwnd, GWL_EXSTYLE)
   If bState Then
      lStyle = lStyle And Not WS_EX_CLIENTEDGE Or WS_EX_STATICEDGE
   Else
      lStyle = lStyle Or WS_EX_CLIENTEDGE And Not WS_EX_STATICEDGE
   End If
   SetWindowLong hwnd, GWL_EXSTYLE, lStyle
   ' Make the style 'take':
   SetWindowPos hwnd, 0, 0, 0, 0, 0, SWP_NOACTIVATE Or SWP_NOZORDER Or SWP_FRAMECHANGED Or SWP_NOSIZE Or SWP_NOMOVE

End Sub

Private Function DrawStatus()
   picStatus.Cls
   If (m_iValue <> 0) Then
      picStatus.Line (0, 0)-(picStatus.ScaleWidth * m_iValue \ m_iMax, picStatus.ScaleHeight), RGB(0, 0, &H80), BF
      picStatus.ForeColor = &HFFFFFF
   Else
      picStatus.ForeColor = vbWindowText
   End If
   If (m_sStatus <> "") Then
      picStatus.CurrentX = 4 * Screen.TwipsPerPixelX
      picStatus.CurrentY = 2 * Screen.TwipsPerPixelY
      picStatus.Print m_sStatus
   End If
End Function
Public Property Let Max(ByVal iMax As Long)
   m_iMax = iMax
   DrawStatus
End Property
Public Property Let Value(ByVal iValue As Long)
   m_iValue = iValue
   DrawStatus
End Property
Public Property Get Value() As Long
   Value = m_iValue
End Property
Public Property Let Status(ByVal sText As String)
   m_sStatus = sText
   DrawStatus
End Property

Private Sub pPopulate()
Dim lRow As Long, lCol As Long, lIndent As Long
      
   Dim sFnt2 As New StdFont
   sFnt2.Name = "Times New Roman"
   sFnt2.Bold = True
   sFnt2.Size = 12
   
   With grdThis
      .DefaultRowHeight = 24
      .Redraw = False
      .Rows = CLng(txtRows.Text)
      Max = .Rows
      For lRow = 1 To .Rows
         If (chkRnd.Value = Checked) Then
            .RowHeight(lRow) = Rnd * 48 + 16
         End If
         For lCol = 1 To .Columns
            If (.ColumnKey(lCol) = "file") Or (.ColumnKey(lCol) = "col8") Then
               .CellDetails lRow, lCol, , , Rnd * (ilsIcons.ImageCount - 1)
            ElseIf (.ColumnKey(lCol) = "date") Then
               .CellDetails lRow, lCol, DateSerial(Year(Now) + Rnd * 8 - 1, Rnd * 12, Rnd * 31)
            ElseIf (.ColumnKey(lCol) = "col5") Then
               ' Icons + text
               If (lRow Mod 2) = 0 Then
                  lIndent = 24
               Else
                  lIndent = 0
               End If
               .CellDetails lRow, lCol, "This is a longer piece of text which can wrap onto a second line if the default cell format is changed so the DT_SINGLELINE option is removed. Test ampersands: Autos & Auto Parts.", DT_LEFT Or DT_MODIFYSTRING Or DT_WORDBREAK Or DT_END_ELLIPSIS, Rnd * ilsIcons.ImageCount - 1, , , , lIndent
            ElseIf .ColumnKey(lCol) = "size" Then
                .CellDetails lRow, lCol, "Row" & lRow & ",Col" & lCol, , Rnd * (ilsIcons.ImageCount - 1), , , , , Rnd * (ilsIcons.ImageCount - 1)
            Else
               ' Text:
               .CellDetails lRow, lCol, "Row" & lRow & ",Col" & lCol
            End If
            
            ' Demonstrating multiple forecolor, backcolor and fonts for cells
            If (lRow Mod 42) = 0 Then
               .CellFont(lRow, lCol) = sFnt2
            ElseIf (lRow Mod 35) = 0 Then
               If (lCol = 4) Then
                  .CellBackColor(lRow, lCol) = &HCC9966
               Else
                  .CellBackColor(lRow, lCol) = &HEECC99
               End If
            ElseIf (lRow Mod 10) = 0 Then
               .CellForeColor(lRow, lCol) = &HFF&
            End If
            
         Next lCol
         If (lRow Mod 10) = 0 Then
            Value = Value + 10
            Status = lRow & " of " & .Rows
         End If
      Next lRow
      Value = 0
      .Redraw = True
   End With
   
End Sub


Private Sub chkBackground_Click()
   If chkBackground.Value = Checked Then
      Set grdThis.BackgroundPicture = picBackground.Picture
      ' work around vb bug for JPG and GIF - picture is 2 pixels larger than expected
      grdThis.BackgroundPictureHeight = grdThis.BackgroundPictureHeight - 3
   Else
      Set grdThis.BackgroundPicture = Nothing
   End If
End Sub

Private Sub chkBold_Click()
Dim sFnt As New StdFont
   If (chkBold.Tag = "") Then
      With grdThis.CellFont(grdThis.SelectedRow, grdThis.SelectedCol)
         sFnt.Name = .Name
         sFnt.Size = .Size
         sFnt.Bold = (chkBold.Value = Checked)
         sFnt.Italic = (chkItalic.Value = Checked)
         grdThis.CellFont(grdThis.SelectedRow, grdThis.SelectedCol) = sFnt
      End With
   Else
      chkBold.Tag = ""
   End If
End Sub

Private Sub chkCol4_Click()
   grdThis.ColumnVisible("date") = (chkCol4.Value = Checked)
End Sub

Private Sub chkDrawFocusRect_Click()
   grdThis.DrawFocusRectangle = (chkDrawFocusRect.Value = Checked)
   grdThis.Draw
End Sub

Private Sub chkEditable_Click()
   grdThis.Editable = (chkEditable = Checked)
End Sub

Private Sub chkEnabled_Click()
   grdThis.Enabled = (chkEnabled.Value = Checked)
End Sub

Private Sub chkFlatHeader_Click()
   grdThis.HeaderFlat = (chkFlatHeader.Value = Checked)
End Sub

Private Sub chkHeader_Click()
   grdThis.Header = (chkHeader.Value = Checked)
End Sub

Private Sub chkHeaderButtons_Click()
   grdThis.HeaderButtons = (chkHeaderButtons.Value = Checked)
End Sub

Private Sub chkHighlightSelectedIcons_Click()
   grdThis.HighlightSelectedIcons = (chkHighlightSelectedIcons.Value = Checked)
   grdThis.Draw
End Sub

Private Sub chkItalic_Click()
   chkBold_Click
End Sub

Private Sub chkOptions_Click(Index As Integer)
   Select Case Index
   Case 0
      grdThis.MultiSelect = -1 * chkOptions(Index).Value
   Case 1
      grdThis.RowMode = -1 * chkOptions(Index).Value
   Case 2
      grdThis.GridLines = -1 * chkOptions(Index).Value
   End Select
End Sub

Private Sub chkVisible_Click()
Dim bS As Boolean
Dim lRow As Long
   bS = (chkVisible.Value = Unchecked)
   With grdThis
      .Redraw = False
      For lRow = 1 To .Rows
         If (lRow Mod 2) = 0 Then
            .RowVisible(lRow) = bS
         End If
      Next lRow
      .Redraw = True
   End With
End Sub

Private Sub cmdAddCol_Click()
Static s_iItem As Long
   If s_iItem = 0 Then
      s_iItem = grdThis.Columns
   End If
   With grdThis
      .AddColumn "New" & s_iItem, "New:" & s_iItem
   End With
End Sub

Private Sub cmdAutoRowHeight_Click()
Dim lRow As Long
   Screen.MousePointer = vbHourglass
   With grdThis
      .Redraw = False
      For lRow = 1 To .Rows
         .AutoHeightRow lRow
      Next lRow
      .Redraw = True
   End With
   Screen.MousePointer = vbDefault
End Sub

Private Sub cmdCellText_Click()
Dim sText As String
Dim sI As String
Dim iCol As Long

   If (grdThis.RowMode) Then
      iCol = 5
   Else
      iCol = grdThis.SelectedCol
   End If
   sText = grdThis.CellText(grdThis.SelectedRow, iCol)
   sI = InputBox$("Enter text", , sText)
   If (sI <> "") Then
      grdThis.CellText(grdThis.SelectedRow, iCol) = sI
   End If
End Sub


Private Sub cmdEmpty_Click()
   grdThis.Clear
End Sub

Private Sub cmdGetSel_Click()
Dim iRow As Long, iCol As Long
   With grdThis
      For iRow = 1 To .Rows
         If .RowMode Then
            If .CellSelected(iRow, 1) Then
               Debug.Print "SELECTED:" & iRow
            End If
         Else
            For iCol = 1 To .Columns
               If .CellSelected(iRow, iCol) Then
                  Debug.Print "SELECTED:" & iRow, iCol
               End If
            Next iCol
         End If
      Next iRow
   End With
End Sub

Private Sub cmdRemoveCol_Click()
Dim iCol As Long
Dim sKey As String
Dim sI As String
Dim sDefault As String
   If (grdThis.Columns > 0) Then
      For iCol = 1 To grdThis.Columns
         sKey = sKey & grdThis.ColumnKey(iCol) & ","
      Next iCol
      sKey = Left$(sKey, Len(sKey) - 1)
      sI = InputBox$("Enter column to delete" & vbCrLf & vbCrLf & "Available columns: " & sKey, , grdThis.ColumnKey(1))
      If (sI <> "") Then
         grdThis.RemoveColumn sI
      End If
   Else
      MsgBox "No columns to delete.", vbInformation
   End If
End Sub

Private Sub cmdRepopulate_Click()
   pPopulate
End Sub

Private Sub Form_Load()
   
   ThinBorder picStatus.hwnd, True
   
   Me.Show
   Me.Refresh
   
   With grdThis
      ' Turn redraw off for speed:
      .Redraw = False
      
      .ImageList = ilsIcons.hIml
      .AddColumn "file", "Name", , , 32, , , , False
      .AddColumn "size", "Size", , , 48
      .AddColumn "type", "Type"
      .AddColumn "date", "Modified", , , 64, False, , , , "Long Date"
      .AddColumn "col5", "Col 5", , , 196
      .AddColumn "col6", "Col 6"
      .AddColumn "col7", "Col 7"
      .AddColumn "col8", "Col 8"
      .AddColumn "col9", "Col 9"
      .AddColumn "col10", "Col 10"
      .AddColumn "col11", "Col 11"
      .SetHeaders
      .KeySearchColumn = .ColumnIndex("size")
      .HighlightSelectedIcons = False
      pPopulate
      
      ' Ensure the grid will draw!
      .Redraw = True
      
   End With
End Sub

Private Sub Form_Resize()
Dim lSize As Long
Dim lHeight As Long
On Error Resume Next
   lHeight = Me.ScaleHeight - picStatus.Height - 4 * Screen.TwipsPerPixelY
   lSize = fraOptions.Width + grdThis.Left
   grdThis.Move 2 * Screen.TwipsPerPixelX, 2 * Screen.TwipsPerPixelY, Me.ScaleWidth - grdThis.Left * 2 - lSize, lHeight
   fraOptions.Move Me.ScaleWidth - lSize, grdThis.Top - 6 * Screen.TwipsPerPixelY, fraOptions.Width, lHeight + 6 * Screen.TwipsPerPixelY
   picStatus.Move grdThis.Left, Me.ScaleHeight - picStatus.Height - Screen.TwipsPerPixelY, Me.ScaleWidth - grdThis.Left * 2
End Sub

Private Sub grdThis_CancelEdit()
   txtEdit.Visible = False
End Sub

Private Sub grdThis_ColumnClick(ByVal lCol As Long)
Dim sTag As String
Dim i As Long
      
   With grdThis.SortObject
      .Clear
      .SortColumn(1) = lCol
   
      sTag = grdThis.ColumnTag(lCol)
      If (sTag = "") Then
         sTag = "DESC"
         .SortOrder(1) = CCLOrderAscending
      Else
         sTag = ""
         .SortOrder(1) = CCLOrderDescending
      End If
      grdThis.ColumnTag(lCol) = sTag
   
      Select Case grdThis.ColumnKey(lCol)
      Case "file", "col8"
         ' sort by icon:
         .SortType(1) = CCLSortIcon
      Case "date"
         ' sort by date:
         .SortType(1) = CCLSortDate
      Case Else
         ' sort by text:
         .SortType(1) = CCLSortString
      End Select
   End With
   Screen.MousePointer = vbHourglass
   grdThis.Sort
   Screen.MousePointer = vbDefault
   
End Sub

Private Sub grdThis_ColumnWidthChanging(ByVal lCol As Long, ByVal lWidth As Long, bCancel As Boolean)
   ' If column 1 then prevent size change;
   If (grdThis.ColumnKey(lCol) = "file") Then
      bCancel = True
   End If

End Sub

Private Sub grdThis_HeaderRightClick(ByVal x As Single, ByVal y As Single)
    Debug.Print "HitTest: " & grdThis.HeaderColumnHitTest(x, y)
End Sub

Private Sub grdThis_MouseDown(Button As Integer, Shift As Integer, x As Single, y As Single, bDoDefault As Boolean)
   Shift = vbCtrlMask
End Sub

Private Sub grdThis_RequestEdit(ByVal lRow As Long, ByVal lCol As Long, ByVal iKeyAscii As Integer, bCancel As Boolean)
Dim lLeft As Long, lTop As Long, lWidth As Long, lHeight As Long
Dim sText As String
   grdThis.CellBoundary lRow, lCol, lLeft, lTop, lWidth, lHeight
   If Not IsMissing(grdThis.CellText(lRow, lCol)) Then
      sText = grdThis.CellFormattedText(lRow, lCol)
   Else
      sText = ""
   End If
   If (iKeyAscii <> 0) Then
      sText = Chr$(iKeyAscii) & sText
      txtEdit.Text = sText
      txtEdit.SelStart = 1
      txtEdit.SelLength = Len(sText)
   Else
      txtEdit.Text = sText
      txtEdit.SelStart = 0
      txtEdit.SelLength = Len(sText)
   End If
   Set txtEdit.Font = grdThis.CellFont(lRow, lCol)
   If grdThis.CellBackColor(lRow, lCol) = -1 Then
      txtEdit.BackColor = grdThis.BackColor
   Else
      txtEdit.BackColor = grdThis.CellBackColor(lRow, lCol)
   End If
   txtEdit.Move lLeft, lTop, lWidth, lHeight
   txtEdit.Visible = True
   txtEdit.ZOrder
   txtEdit.SetFocus
End Sub

Private Sub grdThis_SelectionChange(ByVal lRow As Long, ByVal lCol As Long)
   Status = "Selected: " & lRow & "," & lCol
   chkBold.Tag = "CODE"
   chkBold.Value = Abs(grdThis.CellFont(lRow, lCol).Bold)
   chkBold.Tag = ""
   chkItalic.Tag = "CODE"
   chkItalic.Value = Abs(grdThis.CellFont(lRow, lCol).Italic)
   chkItalic.Tag = ""
End Sub

Private Sub mnuDemo_Click(Index As Integer)
   Select Case Index
   Case 0
      frmOutlookDemo.Show
   Case 1
      frmMatrixDemo.Show
   Case 2
      frmVirtual.Show
   Case 4
      Unload Me
   End Select
End Sub


Private Sub txtEdit_KeyDown(KeyCode As Integer, Shift As Integer)
   If (KeyCode = vbKeyReturn) Then
      ' Commit edit
      grdThis.CellText(grdThis.SelectedRow, grdThis.SelectedCol) = txtEdit.Text
      grdThis.SetFocus
   ElseIf (KeyCode = vbKeyEscape) Then
      ' Cancel edit
      grdThis.SetFocus
   End If
End Sub

Private Sub txtEdit_LostFocus()
   grdThis.CancelEdit
End Sub
