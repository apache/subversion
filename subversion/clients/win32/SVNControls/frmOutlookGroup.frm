VERSION 5.00
Object = "*\ASGrid.vbp"
Begin VB.Form frmOutlookGroup 
   BorderStyle     =   3  'Fixed Dialog
   Caption         =   "frmGroupBy"
   ClientHeight    =   3528
   ClientLeft      =   3516
   ClientTop       =   3288
   ClientWidth     =   6144
   BeginProperty Font 
      Name            =   "Tahoma"
      Size            =   8.4
      Charset         =   0
      Weight          =   400
      Underline       =   0   'False
      Italic          =   0   'False
      Strikethrough   =   0   'False
   EndProperty
   Icon            =   "frmOutlookGroup.frx":0000
   LinkTopic       =   "Form1"
   MaxButton       =   0   'False
   MinButton       =   0   'False
   ScaleHeight     =   3528
   ScaleWidth      =   6144
   ShowInTaskbar   =   0   'False
   Begin VB.ComboBox cboOrder 
      Height          =   300
      Left            =   4740
      Style           =   2  'Dropdown List
      TabIndex        =   4
      Top             =   3000
      Visible         =   0   'False
      Width           =   1875
   End
   Begin VB.ComboBox cboField 
      Height          =   300
      Left            =   4740
      Style           =   2  'Dropdown List
      TabIndex        =   5
      Top             =   2640
      Visible         =   0   'False
      Width           =   1875
   End
   Begin VB.CommandButton cmdMoveDown 
      Caption         =   "u"
      Enabled         =   0   'False
      BeginProperty Font 
         Name            =   "Marlett"
         Size            =   11.4
         Charset         =   2
         Weight          =   500
         Underline       =   0   'False
         Italic          =   0   'False
         Strikethrough   =   0   'False
      EndProperty
      Height          =   375
      Left            =   4860
      TabIndex        =   3
      Top             =   1860
      Width           =   1215
   End
   Begin VB.CommandButton cmdMoveUp 
      Caption         =   "t"
      Enabled         =   0   'False
      BeginProperty Font 
         Name            =   "Marlett"
         Size            =   11.4
         Charset         =   2
         Weight          =   500
         Underline       =   0   'False
         Italic          =   0   'False
         Strikethrough   =   0   'False
      EndProperty
      Height          =   375
      Left            =   4860
      TabIndex        =   2
      Top             =   1440
      Width           =   1215
   End
   Begin VB.CommandButton cmdCancel 
      Cancel          =   -1  'True
      Caption         =   "Cancel"
      Height          =   375
      Left            =   4860
      TabIndex        =   1
      Top             =   540
      Width           =   1215
   End
   Begin VB.CommandButton cmdOK 
      Caption         =   "OK"
      Default         =   -1  'True
      Height          =   375
      Left            =   4860
      TabIndex        =   0
      Top             =   120
      Width           =   1215
   End
   Begin SVNControls.SGrid grdGroupBy 
      Height          =   3252
      Left            =   120
      TabIndex        =   6
      Top             =   120
      Width           =   4572
      _ExtentX        =   8065
      _ExtentY        =   5736
      BackgroundPictureHeight=   0
      BackgroundPictureWidth=   0
      BeginProperty Font {0BE35203-8F91-11CE-9DE3-00AA004BB851} 
         Name            =   "Tahoma"
         Size            =   8.4
         Charset         =   0
         Weight          =   400
         Underline       =   0   'False
         Italic          =   0   'False
         Strikethrough   =   0   'False
      EndProperty
      DisableIcons    =   -1  'True
   End
End
Attribute VB_Name = "frmOutlookGroup"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

Private m_bCancel As Boolean
Private m_sFieldList() As String
Private m_sFieldKey() As String
Private m_iFieldCount As Long
Private m_iSelCount As Long
Private m_sSelKey() As String
Private m_eSelOrder() As cShellSortOrderCOnstants

Public Property Get SelectionCount() As Long
   SelectionCount = m_iSelCount
End Property
Public Property Get SelectedKey(ByVal iIndex As Long) As String
   SelectedKey = m_sSelKey(iIndex)
End Property
Public Property Get SelectedOrder(ByVal iIndex As Long) As cShellSortOrderCOnstants
   SelectedOrder = m_eSelOrder(iIndex)
End Property

Public Sub AddField(ByVal sField As String, ByVal sKey As String)
   m_iFieldCount = m_iFieldCount + 1
   ReDim Preserve m_sFieldList(1 To m_iFieldCount) As String
   ReDim Preserve m_sFieldKey(1 To m_iFieldCount) As String
   m_sFieldList(m_iFieldCount) = sField
   m_sFieldKey(m_iFieldCount) = sKey
End Sub

Public Property Get Cancelled() As Boolean
   Cancelled = m_bCancel
End Property

Private Sub cboField_Click()
Dim i As Long
Dim lLastRow As Long
Dim lNC As Long
   '
   If cboField.Visible Then
      If cboField.ListIndex = 0 Then
         ' We've set this to (none).
         ' if this isn't the only visible row then swap all
         ' subsequent rows up one and make the last one
         ' invisible:
         
         For i = cboField.Tag + 1 To grdGroupBy.Rows
            If grdGroupBy.RowVisible(i) Then
               grdGroupBy.CellText(i - 1, 1) = grdGroupBy.CellText(i, 1)
               grdGroupBy.CellText(i - 1, 2) = grdGroupBy.CellText(i, 2)
               lLastRow = i
            End If
         Next i
         If (lLastRow = 0) Then lLastRow = grdGroupBy.Rows
         grdGroupBy.CellText(lLastRow, 1) = "(none)"
         grdGroupBy.CellForeColor(lLastRow, 1) = vbButtonFace
         For i = 1 To grdGroupBy.Rows
            If grdGroupBy.CellText(i, 1) = "(none)" Then
               lNC = lNC + 1
               grdGroupBy.CellForeColor(i, 1) = vbButtonFace
            End If
            If lNC > 1 Then
               grdGroupBy.RowVisible(i) = False
            End If
         Next i
      Else
         i = CLng(cboField.Tag)
         grdGroupBy.CellText(i, 1) = cboField.List(cboField.ListIndex)
         grdGroupBy.CellText(i, 3) = m_sFieldKey(cboField.ItemData(cboField.ListIndex))
         grdGroupBy.CellForeColor(i, 1) = vbWindowText
         If i < grdGroupBy.Rows Then
            i = i + 1
            If Not grdGroupBy.RowVisible(i) Then
               grdGroupBy.RowVisible(i) = True
               grdGroupBy.CellText(i, 1) = "(none)"
               grdGroupBy.CellForeColor(i, 1) = vbButtonFace
               grdGroupBy.CellText(i, 2) = "Ascending"
            End If
         End If
      End If
      cboField.Visible = False
      grdGroupBy.SetFocus
   End If
End Sub

Private Sub cboField_LostFocus()
   cboField.Visible = False
   grdGroupBy.CancelEdit
   grdGroupBy.SetFocus
End Sub

Private Sub cboOrder_Click()
   If cboOrder.Visible Then
      grdGroupBy.CellText(cboOrder.Tag, 2) = cboOrder.List(cboOrder.ListIndex)
      cboOrder.Visible = False
      grdGroupBy.SetFocus
   End If
End Sub

Private Sub cboOrder_LostFocus()
   cboOrder.Visible = False
   grdGroupBy.CancelEdit
   grdGroupBy.SetFocus

End Sub

Private Sub cmdCancel_Click()
   Unload Me
End Sub

Private Sub cmdOK_Click()
Dim i As Long
   m_bCancel = False
   For i = 1 To grdGroupBy.Rows
      If grdGroupBy.RowVisible(i) Then
         If grdGroupBy.CellText(i, 1) <> "(none)" Then
            m_iSelCount = m_iSelCount + 1
            ReDim Preserve m_sSelKey(1 To m_iSelCount) As String
            m_sSelKey(m_iSelCount) = grdGroupBy.CellText(i, 3)
            ReDim Preserve m_eSelOrder(1 To m_iSelCount) As cShellSortOrderCOnstants
            If (grdGroupBy.CellText(i, 2) = "Descending") Then
               m_eSelOrder(m_iSelCount) = CCLOrderDescending
            Else
               m_eSelOrder(m_iSelCount) = CCLOrderAscending
            End If
         End If
      End If
   Next i
   Unload Me
End Sub

Private Sub Form_Initialize()
   m_iFieldCount = 1
   ReDim m_sFieldList(1 To 1) As String
   m_sFieldList(1) = "(none)"
End Sub

Private Sub Form_Load()
Dim i As Long
   m_bCancel = True
   With grdGroupBy
      .Editable = True
            
      .AddColumn "field", "Field", , , grdGroupBy.Width \ (Screen.TwipsPerPixelX * 2) - 10
      .AddColumn "order", "Order", , , grdGroupBy.Width \ (Screen.TwipsPerPixelX * 2) - 10
      .AddColumn "key", , , , , False
      
      .GridLines = True
      .HeaderButtons = False
      .BorderStyle = ecgBorderStyle3dThin
   
      For i = 1 To 3
         .AddRow , , (i = 1)
         .CellText(i, 1) = m_sFieldList(1)
         .CellText(i, 2) = "Ascending"
         .CellForeColor(i, 1) = vbButtonFace
      Next i
   End With
   cboOrder.AddItem "Ascending"
   cboOrder.AddItem "Descending"
End Sub

Private Sub grdGroupBy_RequestEdit(ByVal lRow As Long, ByVal lCol As Long, ByVal iKeyAscii As Integer, bCancel As Boolean)
Dim lLeft As Long, lTop As Long, lWidth As Long, lHeight As Long
Dim i As Long, j As Long, k As Long
Dim iExcCount As Long, sExc() As String, bExclude As Boolean
Dim iListIndex As Long

   grdGroupBy.CellBoundary lRow, lCol, lLeft, lTop, lWidth, lHeight
   If (lCol = 1) Then
      cboField.Clear
      For i = 1 To grdGroupBy.Rows
         If i <> lRow Then
            If grdGroupBy.RowVisible(i) Then
               iExcCount = iExcCount + 1
               ReDim Preserve sExc(1 To iExcCount) As String
               sExc(iExcCount) = grdGroupBy.CellText(i, 1)
            End If
         End If
      Next i
      For j = 1 To m_iFieldCount
         If j > 1 Then
            bExclude = False
            For k = 1 To iExcCount
               If m_sFieldList(j) = sExc(k) Then
                  bExclude = True
               End If
            Next k
         End If
         If Not bExclude Then
            cboField.AddItem m_sFieldList(j)
            cboField.ItemData(cboField.NewIndex) = j
            If m_sFieldList(j) = grdGroupBy.CellText(lRow, lCol) Then
               iListIndex = cboField.NewIndex
            End If
         End If
      Next j
      cboField.Tag = lRow
      cboField.Move lLeft, lTop, lWidth
      cboField.ListIndex = iListIndex
      cboField.Visible = True
      cboField.ZOrder
      cboField.SetFocus
   Else
      cboOrder.Move lLeft, lTop, lWidth
      For i = 0 To cboOrder.ListCount - 1
         If cboOrder.List(i) = grdGroupBy.CellText(lRow, lCol) Then
            cboOrder.ListIndex = i
            Exit For
         End If
      Next i
      cboOrder.Tag = lRow
      cboOrder.Visible = True
      cboOrder.ZOrder
      cboOrder.SetFocus
   End If
End Sub

Private Sub grdGroupBy_SelectionChange(ByVal lRow As Long, ByVal lCol As Long)
Dim lLastRow As Long
Dim iRow As Long
   For iRow = 1 To grdGroupBy.Rows
      If grdGroupBy.RowVisible(iRow) Then
         lLastRow = iRow
      End If
   Next iRow
   cmdMoveUp.Enabled = (lRow > 1) And (lRow < lLastRow)
   cmdMoveDown.Enabled = (lRow < lLastRow)
End Sub
