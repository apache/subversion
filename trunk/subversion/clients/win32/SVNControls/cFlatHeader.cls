VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
  Persistable = 0  'NotPersistable
  DataBindingBehavior = 0  'vbNone
  DataSourceBehavior  = 0  'vbNone
  MTSTransactionMode  = 0  'NotAnMTSObject
END
Attribute VB_Name = "cFlatHeader"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = True
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = False
Option Explicit

' ====================================================================================
' File:     cFlatHeader.cls
' Author:   SP McMahon
' Date:     15 August 1999
'
' Attach to the parent of any control containing a COMCTL32.DLL header
' control, and this routine will ensure the header paints in a flat style,
' like the header in DevStudio 6.
'
' Requires: SSUBTMR.DLL
'
' Date      Who
' 15/09/99  SPM
' First release.
'
' ------------------------------------------------------------------------------------
' vbAccelerator
' >> Advanced, free VB Source Code.
'
' http://vbaccelerator.com/
' mailto:steve@vbaccelerator.com
' ====================================================================================

Private Type POINTAPI
    x As Long
    y As Long
End Type

Private Type RECT
   Left As Long
   Top As Long
   Right As Long
   Bottom As Long
End Type

Private Type HD_ITEM
    mask As Long
    cxy As Long
    pszText As String
    hbm As Long
    cchTextMax As Long
    fmt As Long
    lParam As Long
    ' 4.70:
    iImage As Long
    iOrder As Long
End Type

Private Declare Function GetWindowRect Lib "user32" (ByVal hwnd As Long, lpRect As RECT) As Long
Private Declare Function GetClassName Lib "user32" Alias "GetClassNameA" (ByVal hwnd As Long, ByVal lpClassName As String, ByVal nMaxCount As Long) As Long
Private Declare Function FindWindowEx Lib "user32" Alias "FindWindowExA" (ByVal hWnd1 As Long, ByVal hWnd2 As Long, ByVal lpsz1 As String, ByVal lpsz2 As String) As Long
Private Declare Function IsWindow Lib "user32" (ByVal hwnd As Long) As Long
Private Declare Function SendMessageByLong Lib "user32" Alias "SendMessageA" (ByVal hwnd As Long, ByVal wMsg As Long, ByVal wParam As Long, ByVal lParam As Long) As Long
Private Declare Function SendMessage Lib "user32" Alias "SendMessageA" (ByVal hwnd As Long, ByVal wMsg As Long, ByVal wParam As Long, lParam As Any) As Long
Private Declare Function GetDC Lib "user32" (ByVal hwnd As Long) As Long
Private Declare Function ReleaseDC Lib "user32" (ByVal hwnd As Long, ByVal hdc As Long) As Long
Private Declare Function MoveToEx Lib "gdi32" (ByVal hdc As Long, ByVal x As Long, ByVal y As Long, lpPoint As POINTAPI) As Long
Private Declare Function LineTo Lib "gdi32" (ByVal hdc As Long, ByVal x As Long, ByVal y As Long) As Long
Private Declare Function GetPixel Lib "gdi32" (ByVal hdc As Long, ByVal x As Long, ByVal y As Long) As Long
Private Declare Function CreatePen Lib "gdi32" (ByVal nPenStyle As Long, ByVal nWidth As Long, ByVal crColor As Long) As Long
Private Declare Function Rectangle Lib "gdi32" (ByVal hdc As Long, ByVal X1 As Long, ByVal Y1 As Long, ByVal X2 As Long, ByVal Y2 As Long) As Long
Private Declare Function SetPixel Lib "gdi32" (ByVal hdc As Long, ByVal x As Long, ByVal y As Long, ByVal crColor As Long) As Long
Private Declare Function GetSysColor Lib "user32" (ByVal nIndex As Long) As Long
Private Declare Function SelectObject Lib "gdi32" (ByVal hdc As Long, ByVal hObject As Long) As Long
Private Declare Function DeleteObject Lib "gdi32" (ByVal hObject As Long) As Long
Private Declare Function OffsetRect Lib "user32" (lpRect As RECT, ByVal x As Long, ByVal y As Long) As Long
Private Declare Function ShowScrollBar Lib "user32" (ByVal hwnd As Long, ByVal wBar As Long, ByVal bShow As Long) As Long
Private Const SB_BOTH = 3

Private Const WC_HEADERA = "SysHeader32"
Private Const WC_HEADER = WC_HEADERA
Private Const HDM_FIRST = &H1200                    '// Header messages
Private Const HDM_GETITEMCOUNT = (HDM_FIRST + 0)
Private Const HDM_ORDERTOINDEX = (HDM_FIRST + 15)
Private Const HDM_GETITEMA = (HDM_FIRST + 3)
Private Const HDM_GETITEM = HDM_GETITEMA

Private Const HDI_WIDTH = &H1
Private Const HDI_HEIGHT = HDI_WIDTH
Private Const HDI_TEXT = &H2
Private Const HDI_FORMAT = &H4
Private Const HDI_LPARAM = &H8
Private Const HDI_BITMAP = &H10

Private Const WM_PAINT = &HF
Private Const PS_SOLID = 0

' VB6 header in ListView is a new class:
Private Const WC_HEADER_VB6 = "msvb_lib_header"

Implements ISubclass

Private m_hWnd As Long

Public Sub Attach(ByVal hWndA As Long)
Dim sClassName As String
Dim iPos As Long
Dim hWndP As Long

   Detach
   
   sClassName = String$(256, 0)
   GetClassName hWndA, sClassName, 255
   iPos = InStr(sClassName, Chr$(0))
   If Not iPos = 0 Then
      sClassName = Left$(sClassName, iPos - 1)
   End If
   If (Not sClassName = WC_HEADER) And (Not sClassName = WC_HEADER_VB6) Then
      hWndP = hWndA
      hWndA = FindWindowEx(hWndP, 0, WC_HEADER, "")
      If hWndA = 0 Then
         hWndA = FindWindowEx(hWndP, 0, WC_HEADER_VB6, "")
      End If
   End If
   If IsWindow(hWndA) Then
      m_hWnd = hWndA
      AttachMessage Me, m_hWnd, WM_PAINT
   Else
      Err.Raise 26020, App.EXEName & ".cFlatHeader", "Invalid Window Passed to cFlatHeader - no header control detected."
   End If
   
End Sub
Public Sub Detach()
   If Not m_hWnd = 0 Then
      DetachMessage Me, m_hWnd, WM_PAINT
      m_hWnd = 0
   End If
End Sub

Private Sub Class_Terminate()
   Detach
End Sub

Private Property Let ISubClass_MsgResponse(ByVal RHS As SSubTimer6.EMsgResponse)
   '
End Property

Private Property Get ISubClass_MsgResponse() As SSubTimer6.EMsgResponse
   ISubClass_MsgResponse = emrPreprocess
End Property

Private Function ISubClass_WindowProc(ByVal hwnd As Long, ByVal iMsg As Long, ByVal wParam As Long, ByVal lParam As Long) As Long
   If iMsg = WM_PAINT Then
            
      Dim tR As RECT
      Dim lC As Long
      Dim lColIndex As Long
      Dim i As Long
      Dim tHI As HD_ITEM
      Dim hdc As Long
      Dim tJunk As POINTAPI
      Dim lColor As Long
      Dim hPen As Long, hPenOld As Long
      Dim hPenFace As Long, hPenShadow As Long, hPenHighlight As Long
      Dim lX As Long, lXStart As Long
      
      GetWindowRect m_hWnd, tR
      OffsetRect tR, -tR.Left, -tR.Top
      
      hdc = GetDC(m_hWnd)
            
      lColor = GetSysColor(vb3DHighlight And &H1F&)
      hPenHighlight = CreatePen(PS_SOLID, 1, lColor)
      lColor = GetSysColor(vbButtonFace And &H1F&)
      hPenFace = CreatePen(PS_SOLID, 1, lColor)
      lColor = GetSysColor(vbButtonShadow And &H1F&)
      hPenShadow = CreatePen(PS_SOLID, 1, lColor)
            
      lC = SendMessageByLong(m_hWnd, HDM_GETITEMCOUNT, 0, 0)
      For i = 0 To lC - 1
         tHI.mask = HDI_WIDTH
         lColIndex = SendMessageByLong(m_hWnd, HDM_ORDERTOINDEX, i, 0)
         If SendMessage(m_hWnd, HDM_GETITEM, lColIndex, tHI) <> 0 Then
            lXStart = lX + 1
            lX = lX + tHI.cxy
            
            ' Draw over existing shadow with btn face:
            hPenOld = SelectObject(hdc, hPenFace)
            MoveToEx hdc, lXStart, tR.Bottom - 2, tJunk
            LineTo hdc, lX - 2, tR.Bottom - 2
            LineTo hdc, lX - 2, tR.Top
            SelectObject hdc, hPenOld
            
            ' Draw over existing black with shadow:
            If GetPixel(hdc, lXStart, tR.Top) = lColor Then
               ' Item is depressed!
               hPenOld = SelectObject(hdc, hPenHighlight)
            Else
               hPenOld = SelectObject(hdc, hPenShadow)
            End If
            MoveToEx hdc, lXStart - 1, tR.Bottom - 1, tJunk
            LineTo hdc, lX - 1, tR.Bottom - 1
            LineTo hdc, lX - 1, tR.Top - 1
            SelectObject hdc, hPenOld
            
         End If
      Next i
      
      If lX < tR.Right Then
         ' Draw over existing shadow with btn face:
         hPenOld = SelectObject(hdc, hPenFace)
         MoveToEx hdc, lX + 1, tR.Bottom - 2, tJunk
         LineTo hdc, tR.Right, tR.Bottom - 2
         SelectObject hdc, hPenOld
      End If
      
      ' Clear up objects:
      DeleteObject hPenFace
      DeleteObject hPenShadow
      DeleteObject hPenHighlight
      
      ReleaseDC m_hWnd, hdc
      
   End If

End Function

