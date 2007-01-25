/*
 * win32_crashrpt.c : provides information after a crash
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifdef WIN32

/*** Includes. ***/
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>

#include "svn_version.h"

#include "win32_crashrpt.h"
#include "win32_crashrpt_dll.h"

/*** Global variables ***/
HANDLE dbghelp_dll = INVALID_HANDLE_VALUE;

/* email address where the crash reports should be sent too. */
#define CRASHREPORT_EMAIL "svnbreakage@subversion.tigris.org"

#define DBGHELP_DLL "dbghelp.dll"

#define VERSION_DLL "version.dll"

#define LOGFILE_PREFIX "svn-crash-log"

/*** Code. ***/

/* write string to file, allow printf style formatting */
static void
write_to_file(FILE *file, const char *format, ...)
{
  va_list argptr;

  va_start(argptr, format);
  vfprintf(file, format, argptr);
  va_end(argptr);
}

/* Convert a wide-character string to utf-8. This function will create a buffer
 * large enough to hold the result string, the caller should free this buffer.
 * If the string can't be converted, NULL is returned.
 */
static char *
convert_wbcs_to_utf8(const wchar_t *str)
{
  size_t len = wcslen(str);
  char *utf8_str = malloc(sizeof(wchar_t) * len + 1);
  len = wcstombs(utf8_str, str, len);

  if (len == -1)
    return NULL;

  utf8_str[len] = '\0';

  return utf8_str;
}

/* Convert the exception code to a string */
static const char *
exception_string(int exception)
{
  #define EXCEPTION(x) case EXCEPTION_##x: return (#x);

  switch (exception)
    {
      EXCEPTION(ACCESS_VIOLATION)
      EXCEPTION(DATATYPE_MISALIGNMENT)
      EXCEPTION(BREAKPOINT)
      EXCEPTION(SINGLE_STEP)
      EXCEPTION(ARRAY_BOUNDS_EXCEEDED)
      EXCEPTION(FLT_DENORMAL_OPERAND)
      EXCEPTION(FLT_DIVIDE_BY_ZERO)
      EXCEPTION(FLT_INEXACT_RESULT)
      EXCEPTION(FLT_INVALID_OPERATION)
      EXCEPTION(FLT_OVERFLOW)
      EXCEPTION(FLT_STACK_CHECK)
      EXCEPTION(FLT_UNDERFLOW)
      EXCEPTION(INT_DIVIDE_BY_ZERO)
      EXCEPTION(INT_OVERFLOW)
      EXCEPTION(PRIV_INSTRUCTION)
      EXCEPTION(IN_PAGE_ERROR)
      EXCEPTION(ILLEGAL_INSTRUCTION)
      EXCEPTION(NONCONTINUABLE_EXCEPTION)
      EXCEPTION(STACK_OVERFLOW)
      EXCEPTION(INVALID_DISPOSITION)
      EXCEPTION(GUARD_PAGE)
      EXCEPTION(INVALID_HANDLE)

      default:
        return "UNKNOWN_ERROR";
    }
}

/* Write the minidump to file. The callback function will at the same time
   write the list of modules to the log file. */
static BOOL
write_minidump_file(const char *file, PEXCEPTION_POINTERS ptrs,
                    MINIDUMP_CALLBACK_ROUTINE module_callback,
                    void *data)
{
  /* open minidump file */
  HANDLE minidump_file = CreateFile(file, GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    NULL);

  if (minidump_file != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION expt_info;
      MINIDUMP_CALLBACK_INFORMATION dump_cb_info;

      expt_info.ThreadId = GetCurrentThreadId();
      expt_info.ExceptionPointers = ptrs;
      expt_info.ClientPointers = FALSE;

      dump_cb_info.CallbackRoutine = module_callback;
      dump_cb_info.CallbackParam = data;

      MiniDumpWriteDump_(GetCurrentProcess(),
                         GetCurrentProcessId(),
                         minidump_file,
                         MiniDumpNormal,
                         ptrs ? &expt_info : NULL,
                         NULL,
                         &dump_cb_info);

      CloseHandle(minidump_file);
      return TRUE;
    }

  return FALSE;
}

/* Write module information to the log file */
static BOOL CALLBACK
write_module_info_callback(void *data,
                 CONST PMINIDUMP_CALLBACK_INPUT callback_input,
                 PMINIDUMP_CALLBACK_OUTPUT callback_output)
{
  if (data != NULL &&
      callback_input != NULL &&
      callback_input->CallbackType == ModuleCallback)
    {
      FILE *log_file = (FILE *)data;
      MINIDUMP_MODULE_CALLBACK module = callback_input->Module;

      char *buf = convert_wbcs_to_utf8(module.FullPath);
      write_to_file(log_file, "0x%08x", module.BaseOfImage);
      write_to_file(log_file, "  %s", buf);
      free(buf);

      write_to_file(log_file, " (%d.%d.%d.%d, %d bytes)\n",
                              HIWORD(module.VersionInfo.dwFileVersionMS),
                              LOWORD(module.VersionInfo.dwFileVersionMS),
                              HIWORD(module.VersionInfo.dwFileVersionLS),
                              LOWORD(module.VersionInfo.dwFileVersionLS),
                              module.SizeOfImage);
    }

  return TRUE;
}

/* Write details about the current process, platform and the exception */
static void
write_process_info(EXCEPTION_RECORD *exception, CONTEXT *context,
                   FILE *log_file)
{
  OSVERSIONINFO oi;
  const char *cmd_line;

  /* write the command line */
  cmd_line = GetCommandLine();
  write_to_file(log_file,
                "Cmd line: %.65s\n", cmd_line);

  /* write the svn version number info. */
  write_to_file(log_file,
                "Version:  %s, compiled %s, %s\n",
                SVN_VERSION, __DATE__, __TIME__);

  /* write information about the OS */
  oi.dwOSVersionInfoSize = sizeof(oi);
  GetVersionEx(&oi);

  write_to_file(log_file,
                "Platform: Windows OS version %d.%d build %d %s\n\n",
                oi.dwMajorVersion, oi.dwMinorVersion, oi.dwBuildNumber,
                oi.szCSDVersion);

  /* write the exception code */
  write_to_file(log_file,
               "Exception: %s\n\n",
               exception_string(exception->ExceptionCode));

  /* write the register info. */
  write_to_file(log_file,
                "Registers:\n");
  write_to_file(log_file,
                "eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n",
                context->Eax, context->Ebx, context->Ecx,
                context->Edx, context->Esi, context->Edi);
  write_to_file(log_file,
                "eip=%08x esp=%08x ebp=%08x efl=%08x\n",
                context->Eip, context->Esp,
                context->Ebp, context->EFlags);
  write_to_file(log_file,
                "cd=%04x  ss=%04x  ds=%04x  es=%04x  fs=%04x  gs=%04x\n",
                context->SegCs, context->SegSs, context->SegDs,
                context->SegEs, context->SegFs, context->SegGs);
}

/* formats the value at address based on the specified basic type
 * (char, int, long ...). */
static void
format_basic_type(char *buf, DWORD basic_type, DWORD64 length, void *address)
{
  switch(length)
    {
      case 1:
        sprintf(buf, "%x", *(unsigned char *)address);
        break;
      case 2:
        sprintf(buf, "%x", *(unsigned short *)address);
        break;
      case 4:
        switch(basic_type)
          {
            case 2:  /* btChar */
              {
                if (!IsBadStringPtr(*(PSTR*)address, 32))
                  sprintf(buf, "\"%.31s\"", *(unsigned long *)address);
                else
                  sprintf(buf, "%x", *(unsigned long *)address);
              }
            case 6:  /* btInt */
              sprintf(buf, "%d", *(int *)address);
              break;
            case 8:  /* btFloat */
              sprintf(buf, "%f", *(float *)address);
              break;
            default:
              sprintf(buf, "%x", *(unsigned long *)address);
              break;
          }
        break;
      case 8:
        if (basic_type == 8) /* btFloat */
          sprintf(buf, "%lf", *(double *)address);
        else
          sprintf(buf, "%I64X", *(unsigned __int64 *)address);
        break;
    }
}

/* formats the value at address based on the type (pointer, user defined,
 * basic type). */
static void
format_value(char *value_str, DWORD64 mod_base, DWORD type, void *value_addr)
{
  DWORD tag;
  int ptr = 0;
  HANDLE proc = GetCurrentProcess();

  while (SymGetTypeInfo_(proc, mod_base, type, TI_GET_SYMTAG, &tag))
    {
      /* SymTagPointerType */
      if (tag == 14)
        {
          ptr++;
          SymGetTypeInfo_(proc, mod_base, type, TI_GET_TYPE, &type);
          continue;
        }
      break;
    }

  switch(tag)
    {
      case 11: /* SymTagUDT */
        {
          WCHAR *type_name_wbcs;
          if (SymGetTypeInfo_(proc, mod_base, type, TI_GET_SYMNAME,
                              &type_name_wbcs))
            {
              char *type_name = convert_wbcs_to_utf8(type_name_wbcs);
              LocalFree(type_name_wbcs);

              if (ptr == 0)
                sprintf(value_str, "(%s) 0x%08x",
                        type_name, (DWORD *)value_addr);
              else if (ptr == 1)
                sprintf(value_str, "(%s *) 0x%08x",
                        type_name, *(DWORD *)value_addr);
              else
                sprintf(value_str, "(%s **) 0x%08x",
                        type_name, (DWORD *)value_addr);

              free(type_name);
            }
        }
        break;
      case 16: /* SymTagBaseType */
        {
          DWORD bt;
          ULONG64 length;
          SymGetTypeInfo_(proc, mod_base, type, TI_GET_LENGTH, &length);

          /* print a char * as a string */
          if (ptr == 1 && length == 1)
            {
              sprintf(value_str, "0x%08x \"%s\"",
                      *(DWORD *)value_addr, (char *)*(DWORD*)value_addr);
              break;
            }
          if (ptr >= 1)
            {
              sprintf(value_str, "0x%08x", *(DWORD *)value_addr);
              break;
            }
          if (SymGetTypeInfo_(proc, mod_base, type, TI_GET_BASETYPE, &bt))
            {
              format_basic_type(value_str, bt, length, value_addr);
              break;
            }
        }
        break;
      case 12: /* SymTagEnum */
          sprintf(value_str, "%d", *(DWORD *)value_addr);
          break;
      case 13: /* SymTagFunctionType */
          sprintf(value_str, "0x%08x", *(DWORD *)value_addr);
          break;
      default: break;
    }
}

/* Internal structure used to pass some data to the enumerate symbols
 * callback */
typedef struct {
  STACKFRAME *stack_frame;
  FILE *log_file;
  int nr_of_frame;
  BOOL log_params;
} symbols_baton_t;

/* write the details of one parameter or local variable to the log file */
static BOOL WINAPI
write_var_values(PSYMBOL_INFO sym_info, ULONG sym_size, void *baton)
{
  static int last_nr_of_frame = 0;
  DWORD_PTR var_data = 0;    /* Will point to the variable's data in memory */
  STACKFRAME *stack_frame = ((symbols_baton_t*)baton)->stack_frame;
  FILE *log_file   = ((symbols_baton_t*)baton)->log_file;
  int nr_of_frame = ((symbols_baton_t*)baton)->nr_of_frame;
  BOOL log_params = ((symbols_baton_t*)baton)->log_params;
  char value_str[256] = "";

  /* get the variable's data */
  if (sym_info->Flags & SYMFLAG_REGREL)
    {
      var_data = stack_frame->AddrFrame.Offset;
      var_data += (DWORD_PTR)sym_info->Address;
    }
  else
    return FALSE;

  if (log_params == TRUE && sym_info->Flags & SYMFLAG_PARAMETER)
    {
      if (last_nr_of_frame == nr_of_frame)
        write_to_file(log_file, ", ", 2);
      else
        last_nr_of_frame = nr_of_frame;

      format_value((char *)value_str, sym_info->ModBase, sym_info->TypeIndex,
                   (void *)var_data);
      write_to_file(log_file, "%s=%s", sym_info->Name, value_str);
    }
  if (log_params == FALSE && sym_info->Flags & SYMFLAG_LOCAL)
    {
      format_value((char *)value_str, sym_info->ModBase, sym_info->TypeIndex,
                   (void *)var_data);
      write_to_file(log_file, "        %s = %s\n", sym_info->Name, value_str);
    }

  return TRUE;
}

/* write the details of one function to the log file */
static void
write_function_detail(STACKFRAME stack_frame, void *data)
{
  ULONG64 symbolBuffer[(sizeof(SYMBOL_INFO) +
    MAX_PATH*sizeof(TCHAR) +
    sizeof(ULONG64) - 1) /
    sizeof(ULONG64)];
  PSYMBOL_INFO pIHS = (PSYMBOL_INFO)symbolBuffer;
  DWORD64 func_disp=0 ;

  IMAGEHLP_STACK_FRAME ih_stack_frame;
  IMAGEHLP_LINE ih_line;
	DWORD line_disp=0;

  HANDLE proc = GetCurrentProcess();
  FILE *log_file = (FILE *)data;

  symbols_baton_t ensym;

  static int nr_of_frame = 0;

  nr_of_frame++;

  /* log the function name */
  pIHS->SizeOfStruct = sizeof(SYMBOL_INFO);
  pIHS->MaxNameLen = MAX_PATH;
  if (SymFromAddr_(proc, stack_frame.AddrPC.Offset, &func_disp, pIHS) == TRUE)
    {
      write_to_file(log_file,
                    "#%d  0x%08x in %.200s (",
                    nr_of_frame, stack_frame.AddrPC.Offset,  pIHS->Name);

      /* restrict symbol enumeration to this frame only */
      ih_stack_frame.InstructionOffset = stack_frame.AddrPC.Offset;
      SymSetContext_(proc, &ih_stack_frame, 0);

      ensym.log_file = log_file;
      ensym.stack_frame = &stack_frame;
      ensym.nr_of_frame = nr_of_frame;

      /* log all function parameters */
      ensym.log_params = TRUE;
      SymEnumSymbols_(proc, 0, 0, write_var_values, &ensym);

      write_to_file(log_file, ")", 1);
    }
  else
    {
      write_to_file(log_file,
                    "#%d  0x%08x in (unknown function)",
                    nr_of_frame, stack_frame.AddrPC.Offset);
    }

  /* find the source line for this function. */
  ih_line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
  if (SymGetLineFromAddr_(proc, stack_frame.AddrPC.Offset,
                          &line_disp, &ih_line) != 0)
    {
      write_to_file(log_file,
                    " at %s:%d\n", ih_line.FileName, ih_line.LineNumber);
    }
  else
    {
      write_to_file(log_file, "\n");
    }

  /* log all function local variables */
  ensym.log_params = FALSE;
  SymEnumSymbols_(proc, 0, 0, write_var_values, &ensym);
}

/* walk over the stack and log all relevant information to the log file */
static void
write_stacktrace(CONTEXT *context, FILE *log_file)
{
  HANDLE proc = GetCurrentProcess();
  STACKFRAME stack_frame;
  DWORD machine;
  CONTEXT ctx;
  int skip = 0, i = 0;

  /* The thread information - if not supplied. */
	if (context == NULL)
    {
      /* If no context is supplied, skip 1 frame */
      skip = 1;

  		ctx.ContextFlags = CONTEXT_FULL ;
  		if (GetThreadContext(GetCurrentThread(), &ctx))
		    context = &ctx;
	  }

	if (context == NULL)
    return;

  /* Write the stack trace */
  ZeroMemory(&stack_frame, sizeof(STACKFRAME));
  stack_frame.AddrPC.Mode = AddrModeFlat ;
  stack_frame.AddrStack.Mode   = AddrModeFlat ;
  stack_frame.AddrFrame.Mode   = AddrModeFlat ;
#if defined (_M_IX86)
  machine = IMAGE_FILE_MACHINE_I386 ;
  stack_frame.AddrPC.Offset    = context->Eip;
  stack_frame.AddrStack.Offset = context->Esp;
  stack_frame.AddrFrame.Offset = context->Ebp;
#elif defined (_M_AMD64)
  machine = IMAGE_FILE_MACHINE_AMD64 ;
  stack_frame.AddrPC.Offset    = context->Rip;
  stack_frame.AddrStack.Offset = context->Rsp;
  stack_frame.AddrFrame.Offset = context->Rbp;
#endif

  while (1)
    {
      if (! StackWalk_(machine, proc, GetCurrentThread(),
                      &stack_frame, context, NULL, SymFunctionTableAccess_,
                      SymGetModuleBase_, NULL))
        {
          break;
        }

      if (i >= skip)
        {
          /* Try to include symbolic information.
             Also check that the address is not zero. Sometimes StackWalk
             returns TRUE with a frame of zero. */
          if (stack_frame.AddrPC.Offset != 0)
            {
              write_function_detail(stack_frame, (void *)log_file);
            }
        }
      i++;
    }
}

/* Check if a debugger is attached to this process */
static BOOL
is_debugger_present()
{
  HANDLE kernel32_dll = LoadLibrary("kernel32.dll");
  BOOL result;

  ISDEBUGGERPRESENT IsDebuggerPresent_ =
          (ISDEBUGGERPRESENT)GetProcAddress(kernel32_dll, "IsDebuggerPresent");

  if (IsDebuggerPresent_ && IsDebuggerPresent_())
    result = TRUE;
  else
    result = FALSE;

  FreeLibrary(kernel32_dll);

  return result;
}

/* Match the version of dbghelp.dll with the minimum expected version */
static BOOL
check_dbghelp_version(WORD exp_major, WORD exp_minor, WORD exp_build, 
                      WORD exp_qfe)
{
  HANDLE version_dll = LoadLibrary(VERSION_DLL);
  GETFILEVERSIONINFOSIZE GetFileVersionInfoSize_ =
         (GETFILEVERSIONINFOSIZE)GetProcAddress(version_dll,
                                                "GetFileVersionInfoSizeA");
  GETFILEVERSIONINFO GetFileVersionInfo_ =
         (GETFILEVERSIONINFO)GetProcAddress(version_dll,
                                            "GetFileVersionInfoA");
  VERQUERYVALUE VerQueryValue_ =
         (VERQUERYVALUE)GetProcAddress(version_dll, "VerQueryValueA");

  DWORD version     = 0,
        exp_version = MAKELONG(MAKEWORD(exp_qfe, exp_build),
                               MAKEWORD(exp_minor, exp_major));
  DWORD h = 0;
  DWORD resource_size = GetFileVersionInfoSize_(DBGHELP_DLL, &h);

  if (resource_size)
    {
      void *resource_data = malloc(resource_size);
      if (GetFileVersionInfo_(DBGHELP_DLL, h, resource_size, 
                              resource_data) != FALSE)
        {
          void *buf = NULL;
          UINT len;
          if (VerQueryValue_(resource_data, "\\", &buf, &len))
            {
              VS_FIXEDFILEINFO *info = (VS_FIXEDFILEINFO*)buf;
              version = MAKELONG(MAKEWORD(LOWORD(info->dwFileVersionLS),
                                          HIWORD(info->dwFileVersionLS)),
                                 MAKEWORD(LOWORD(info->dwFileVersionMS),
                                          HIWORD(info->dwFileVersionMS)));
            }
        }
      free(resource_data);
    }

   FreeLibrary(version_dll);

   if (version >= exp_version)
     return TRUE;

   return FALSE;
}

/* Load the dbghelp.dll file, try to find a version that matches our
   requirements. */
static BOOL
load_dbghelp_dll()
{
  /* check version of the dll, should be at least 6.6.7.5 */
  if (check_dbghelp_version(6, 6, 7, 5) == FALSE)
    return FALSE;

  dbghelp_dll = LoadLibrary(DBGHELP_DLL);
  if (dbghelp_dll != INVALID_HANDLE_VALUE)
    {
      DWORD opts;

      /* load the functions */
      MiniDumpWriteDump_ =
           (MINIDUMPWRITEDUMP)GetProcAddress(dbghelp_dll, "MiniDumpWriteDump");
      SymInitialize_ =
           (SYMINITIALIZE)GetProcAddress(dbghelp_dll, "SymInitialize");
      SymSetOptions_ =
           (SYMSETOPTIONS)GetProcAddress(dbghelp_dll, "SymSetOptions");
      SymGetOptions_ =
           (SYMGETOPTIONS)GetProcAddress(dbghelp_dll, "SymGetOptions");
      SymCleanup_ =
           (SYMCLEANUP)GetProcAddress(dbghelp_dll, "SymCleanup");
      SymGetTypeInfo_ =
           (SYMGETTYPEINFO)GetProcAddress(dbghelp_dll, "SymGetTypeInfo");
      SymGetLineFromAddr_ =
           (SYMGETLINEFROMADDR)GetProcAddress(dbghelp_dll,
                                              "SymGetLineFromAddr");
      SymEnumSymbols_ =
           (SYMENUMSYMBOLS)GetProcAddress(dbghelp_dll, "SymEnumSymbols");
      SymSetContext_ =
           (SYMSETCONTEXT)GetProcAddress(dbghelp_dll, "SymSetContext");
      SymFromAddr_ = (SYMFROMADDR)GetProcAddress(dbghelp_dll, "SymFromAddr");
      StackWalk_ = (STACKWALK)GetProcAddress(dbghelp_dll, "StackWalk");
      SymFunctionTableAccess_ =
           (SYMFUNCTIONTABLEACCESS)GetProcAddress(dbghelp_dll,
                                                  "SymFunctionTableAccess");
      SymGetModuleBase_ =
           (SYMGETMODULEBASE)GetProcAddress(dbghelp_dll, "SymGetModuleBase");

      if (! (MiniDumpWriteDump_ &&
             SymInitialize_ && SymSetOptions_  && SymGetOptions_ &&
             SymCleanup_    && SymGetTypeInfo_ && SymGetLineFromAddr_ && 
             SymEnumSymbols_ && SymSetContext_ && SymFromAddr_ && StackWalk_ &&
             SymFunctionTableAccess_ && SymGetModuleBase_) )
        goto cleanup;

      /* initialize the symbol loading code */
      opts = SymGetOptions_();

      /* Set the 'load lines' option to retrieve line number information;
         set the Deferred Loads option to map the debug info in memory only
         when needed. */
      SymSetOptions_(opts | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);

      /* Initialize the debughlp DLL with the default path and automatic
         module enumeration (and loading of symbol tables) for this process.
       */
      SymInitialize_(GetCurrentProcess(), NULL, TRUE);

      return TRUE;
    }

cleanup:
  if (dbghelp_dll)
    FreeLibrary(dbghelp_dll);

  return FALSE;
}

/* Cleanup the dbghelp.dll library */
static void
cleanup_debughlp()
{
  SymCleanup_(GetCurrentProcess());

  FreeLibrary(dbghelp_dll);
}

/* Create a filename based on a prefix, the timestamp and an extension.
   check if the filename was already taken, retry 3 times. */
BOOL
get_temp_filename(char *filename, const char *prefix, const char *ext)
{
  char temp_dir[MAX_PATH - 14];
  int i;

  if (! GetTempPathA(MAX_PATH - 14, temp_dir))
    return FALSE;

  for (i = 0;i < 3;i++)
    {
      HANDLE file;
      time_t now;
      char time_str[64];

      time(&now);
      strftime(time_str, 64, "%Y%m%d%H%M%S", localtime(&now));
      sprintf(filename, "%s%s%s.%s", temp_dir, prefix, time_str, ext);

      file = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL, NULL);
      if (file != INVALID_HANDLE_VALUE)
        {
          CloseHandle(file);
          return TRUE;
        }
    }

   filename[0] = '\0';
   return FALSE;
}

/* unhandled exception callback set with SetUnhandledExceptionFilter() */
LONG WINAPI
svn_unhandled_exception_filter(PEXCEPTION_POINTERS ptrs)
{
  char dmp_filename[MAX_PATH];
  char log_filename[MAX_PATH];
  FILE *log_file;

  /* Check if the crash handler was already loaded (crash while handling the
     crash) */
  if (dbghelp_dll != INVALID_HANDLE_VALUE)
    return EXCEPTION_CONTINUE_SEARCH;

  /* don't log anything if we're running inside a debugger ... */
  if (is_debugger_present() == TRUE)
    return EXCEPTION_CONTINUE_SEARCH;

  /* ... or if we can't create the log files ... */
  if (get_temp_filename(dmp_filename, LOGFILE_PREFIX, "dmp") == FALSE ||
      get_temp_filename(log_filename, LOGFILE_PREFIX, "log") == FALSE)
    return EXCEPTION_CONTINUE_SEARCH;

  /* If we can't load a recent version of the dbghelp.dll, pass on this
     exception */
  if (load_dbghelp_dll() == FALSE)
    return EXCEPTION_CONTINUE_SEARCH;

  /* open log file */
  log_file = fopen(log_filename, "w+");

  /* write information about the process */
  write_to_file(log_file, "\nProcess info:\n");
  write_process_info(ptrs ? ptrs->ExceptionRecord : NULL,
                     ptrs ? ptrs->ContextRecord : NULL,
                     log_file);

  /* write the stacktrace, if available */
  write_to_file(log_file, "\nStacktrace:\n");
  write_stacktrace(ptrs ? ptrs->ContextRecord : NULL, log_file);

  /* write the minidump file and use the callback to write the list of modules
     to the log file */
  write_to_file(log_file, "\n\nLoaded modules:\n");
  write_minidump_file(dmp_filename, ptrs,
                      write_module_info_callback, (void *)log_file);

  fclose(log_file);

  cleanup_debughlp();

  /* inform the user */
  fprintf(stderr, "This application has halted due to an unexpected error.\n"\
                  "A crash report and minidump file were saved to disk, you"\
                  " can find them here:\n"\
                  "%s\n%s\n"\
                  "Please send the log file to %s to help us analyse\nand "\
                  "solve this problem.\n\n"\
                  "NOTE: The crash report and minidump files can contain some"\
                  " sensitive information\n(filenames, partial file content, "\
                  "usernames and passwords etc.)\n",
                  log_filename,
                  dmp_filename,
                  CRASHREPORT_EMAIL);

  /* terminate the application */
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif /* WIN32 */