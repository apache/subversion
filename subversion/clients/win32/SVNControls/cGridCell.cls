VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
  Persistable = 0  'NotPersistable
  DataBindingBehavior = 0  'vbNone
  DataSourceBehavior  = 0  'vbNone
  MTSTransactionMode  = 0  'NotAnMTSObject
END
Attribute VB_Name = "cGridCell"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = True
Option Explicit

Private m_lPtrGrid As Long
Private m_lRow As Long, m_lCol As Long

Private m_oFont As StdFont
Private m_eAlign As ECGTextAlignFlags
Private m_lIconIndex As Long
Private m_oBackColor As OLE_COLOR
Private m_oForeColor As OLE_COLOR
Private m_lIndent As Long
Private m_lExtraIconIndex As Long
Private m_vText As Variant
Private m_lItemData As Long
Private m_sKey As String

Friend Property Let Key(s As String)
    m_sKey = s
End Property
Public Property Get Key() As String
   Key = m_sKey
End Property

Public Property Get ItemData() As Long
   ItemData = m_lItemData
End Property
Public Property Let ItemData(ByVal lItemData As Long)
   m_lItemData = lItemData
   If (m_lPtrGrid <> 0) Then
      grd.CellItemData(m_lRow, m_lCol) = lItemData
   End If
End Property
Public Property Get TextAlign() As ECGTextAlignFlags
   TextAlign = m_eAlign
End Property
Public Property Let TextAlign(ByVal eAlign As ECGTextAlignFlags)
   m_eAlign = eAlign
   If (m_lPtrGrid <> 0) Then
      grd.CellTextAlign(m_lRow, m_lCol) = m_eAlign
   End If
End Property
Public Property Get Text() As Variant
   Text = m_vText
End Property
Public Property Let Text(ByVal vText As Variant)
   m_vText = vText
   If (m_lPtrGrid <> 0) Then
      grd.CellText(m_lRow, m_lCol) = vText
   End If
End Property

Public Property Get Font() As StdFont
   Set Font = m_oFont
End Property
Public Property Set Font(ByRef oFont As StdFont)
   Set m_oFont = oFont
   If (m_lPtrGrid <> 0) Then
      If (oFont Is Nothing) Then
         grd.CellDefaultFont m_lRow, m_lCol
      Else
         grd.CellFont(m_lRow, m_lCol) = oFont
      End If
   End If
End Property
Public Property Get IconIndex() As Long
   IconIndex = m_lIconIndex
End Property
Public Property Let IconIndex(ByVal lIconIndex As Long)
   m_lIconIndex = lIconIndex
   If (m_lPtrGrid <> 0) Then
      grd.CellIcon(m_lRow, m_lCol) = lIconIndex
   End If
End Property
Public Property Get ExtraIconIndex() As Long
   ExtraIconIndex = m_lExtraIconIndex
End Property
Public Property Let ExtraIconIndex(ByVal lExtraIconIndex As Long)
   m_lExtraIconIndex = lExtraIconIndex
   If (m_lPtrGrid <> 0) Then
      grd.CellExtraIcon(m_lRow, m_lCol) = lExtraIconIndex
   End If
End Property
Public Property Get Indent() As Long
   Indent = m_lIndent
End Property
Public Property Let Indent(ByVal lIndent As Long)
   m_lIndent = lIndent
   If (m_lPtrGrid <> 0) Then
      grd.CellIndent(m_lRow, m_lCol) = lIndent
   End If
End Property
Public Property Get BackColor() As OLE_COLOR
   BackColor = m_oBackColor
End Property
Public Property Let BackColor(ByVal oColor As OLE_COLOR)
   m_oBackColor = oColor
   If (m_lPtrGrid <> 0) Then
      grd.CellBackColor(m_lRow, m_lCol) = oColor
   End If
End Property
Public Sub DefaultBackColor()
   m_oBackColor = CLR_NONE
   If (m_lPtrGrid <> 0) Then
      grd.CellBackColor(m_lRow, m_lCol) = CLR_NONE
   End If
End Sub
Public Property Get ForeColor() As OLE_COLOR
   ForeColor = m_oForeColor
End Property
Public Property Let ForeColor(ByVal oColor As OLE_COLOR)
   m_oForeColor = oColor
   If (m_lPtrGrid <> 0) Then
      grd.CellForeColor(m_lRow, m_lCol) = oColor
   End If
End Property
Public Sub DefaultForeColor()
   m_oForeColor = CLR_NONE
   If (m_lPtrGrid <> 0) Then
      grd.CellForeColor(m_lRow, m_lCol) = CLR_NONE
   End If
End Sub
Private Property Get grd() As SGrid
Dim oTemp As SGrid
   If (m_lPtrGrid <> 0) Then
      Set grd = ObjectFromPtr(m_lPtrGrid)
   End If
End Property

Friend Sub Init(ByVal grdThis As SGrid, ByVal lRow As Long, ByVal lCol As Long)
   m_lRow = lRow
   m_lCol = lCol
   m_lPtrGrid = ObjPtr(grdThis)
End Sub

Private Sub Class_Initialize()
   debugmsg "cGridCell:Initialize"
   m_eAlign = DT_WORD_ELLIPSIS Or DT_SINGLELINE Or DT_VCENTER
   m_lIconIndex = -1
   m_oBackColor = CLR_NONE
   m_oForeColor = CLR_NONE
   m_lIndent = 0
   Set m_oFont = Nothing
   m_lExtraIconIndex = -1
   m_vText = Empty
End Sub

Private Sub Class_Terminate()
   m_lPtrGrid = 0
   debugmsg "cGridCell:Terminate"
End Sub
