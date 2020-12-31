// Most of the MiniFFI class was taken from Ruby 1.8's Win32API.c,
// it's just as basic but should work fine for the moment

#include "fake-api.h"
#include <SDL.h>
#include <cstdint>

#include "binding-util.h"
#include "debugwriter.h"

#define _T_VOID 0
#define _T_NUMBER 1
#define _T_POINTER 2
#define _T_INTEGER 3
#define _T_BOOL 4

#ifndef __WIN32__
#define MINIFFI_MAX_ARGS 8l
#else
#define MINIFFI_MAX_ARGS 32l
#endif

#define INTEL_ASM ".intel_syntax noprefix\n"

// Might need to let MiniFFI.initialize set calling convention
// as an optional arg, this won't work with everything
// Maybe libffi would help out with this

// Only using 8 max args instead of 16 to reduce the time taken
// to set all those variables up and ease the eyesore, and I don't
// think there are many functions one would need to use that require
// that many arguments anyway

typedef struct {
  unsigned long params[MINIFFI_MAX_ARGS];
} MiniFFIFuncArgs;

#ifndef __WIN32__
// L O N G, but variables won't get set up correctly otherwise
// should allow for __fastcalls (macOS likes these) and whatever else
typedef PREFABI void *(*MINIFFI_FUNC)(unsigned long, unsigned long,
                                      unsigned long, unsigned long,
                                      unsigned long, unsigned long,
                                      unsigned long, unsigned long);
#else
typedef PREFABI void *(*MINIFFI_FUNC)(...);
#endif
// MiniFFI class, also named Win32API on Windows
// Uses LoadLibrary/GetProcAddress on Windows, dlopen/dlsym everywhere else

#if RAPI_FULL > 187
DEF_TYPE_CUSTOMFREE(MiniFFI, SDL_UnloadObject);
#else
DEF_ALLOCFUNC_CUSTOMFREE(MiniFFI, SDL_UnloadObject);
#endif

static void *MiniFFI_GetFunctionHandle(void *libhandle, const char *func) {
#ifdef MKXPZ_ESSENTIALS_DEBUG
#define CAPTURE(n)                                                             \
  if (!strcmp(#n, func))                                                       \
  return (void *)&MKXP_##n
  CAPTURE(GetCurrentThreadId);
  CAPTURE(GetWindowThreadProcessId);
  CAPTURE(FindWindowEx);
  CAPTURE(GetForegroundWindow);
  CAPTURE(GetClientRect);
  CAPTURE(GetCursorPos);
  CAPTURE(ScreenToClient);
  CAPTURE(SetWindowPos);
  CAPTURE(SetWindowTextA);
  CAPTURE(GetWindowRect);
  CAPTURE(GetKeyboardState);
#ifndef __WIN32__
  // Functions only needed on Linux and macOS go here
  CAPTURE(RtlMoveMemory);
  CAPTURE(LoadLibrary);
  CAPTURE(FreeLibrary);
  CAPTURE(GetAsyncKeyState);
  CAPTURE(GetSystemPowerStatus);
  CAPTURE(ShowWindow);
  CAPTURE(GetSystemMetrics);
  CAPTURE(SetCapture);
  CAPTURE(ReleaseCapture);
  CAPTURE(ShowCursor);
  CAPTURE(GetPrivateProfileString);
  CAPTURE(GetUserDefaultLangID);
  CAPTURE(GetUserName);
  CAPTURE(RegisterHotKey);
  CAPTURE(SetWindowLong);
#endif
#endif
  if (!libhandle)
    return 0;
  return SDL_LoadFunction(libhandle, func);
}

// MiniFFI.new(library, function[, imports[, exports]])
// Yields itself in blocks

RB_METHOD(MiniFFI_initialize) {
  VALUE libname, func, imports, exports;
  rb_scan_args(argc, argv, "22", &libname, &func, &imports, &exports);
  SafeStringValue(libname);
  SafeStringValue(func);
  void *hlib = SDL_LoadObject(RSTRING_PTR(libname));
  setPrivateData(self, hlib);
  void *hfunc = MiniFFI_GetFunctionHandle(hlib, RSTRING_PTR(func));
#ifdef __WIN32__
  if (hlib && !hfunc) {
    VALUE func_a = rb_str_new3(func);
    func_a = rb_str_cat(func_a, "A", 1);
    hfunc = SDL_LoadFunction(hlib, RSTRING_PTR(func_a));
  }
#endif
  if (!hfunc)
    rb_raise(rb_eRuntimeError, "%s", SDL_GetError());

  rb_iv_set(self, "_func", ULONG2NUM((unsigned long)hfunc));
  rb_iv_set(self, "_funcname", func);
  rb_iv_set(self, "_libname", libname);

  VALUE ary_imports = rb_ary_new();
  VALUE *entry;
  switch (TYPE(imports)) {
  case T_NIL:
    break;
  case T_ARRAY:
    entry = RARRAY_PTR(imports);
    for (int i = 0; i < RARRAY_LEN(imports); i++) {
      SafeStringValue(entry[i]);
      switch (*(char *)RSTRING_PTR(entry[i])) {
      case 'N':
      case 'n':
      case 'L':
      case 'l':
        rb_ary_push(ary_imports, INT2FIX(_T_NUMBER));
        break;

      case 'P':
      case 'p':
        rb_ary_push(ary_imports, INT2FIX(_T_POINTER));
        break;

      case 'I':
      case 'i':
        rb_ary_push(ary_imports, INT2FIX(_T_INTEGER));
        break;

      case 'B':
      case 'b':
        rb_ary_push(ary_imports, INT2FIX(_T_BOOL));
        break;
      }
    }
    break;
  default:
    SafeStringValue(imports);
    const char *s = RSTRING_PTR(imports);
    for (int i = 0; i < RSTRING_LEN(imports); i++) {
      switch (*s++) {
      case 'N':
      case 'n':
      case 'L':
      case 'l':
        rb_ary_push(ary_imports, INT2FIX(_T_NUMBER));
        break;

      case 'P':
      case 'p':
        rb_ary_push(ary_imports, INT2FIX(_T_POINTER));
        break;

      case 'I':
      case 'i':
        rb_ary_push(ary_imports, INT2FIX(_T_INTEGER));
        break;

      case 'B':
      case 'b':
        rb_ary_push(ary_imports, INT2FIX(_T_BOOL));
        break;
      }
    }
    break;
  }

  if (MINIFFI_MAX_ARGS < RARRAY_LEN(ary_imports))
    rb_raise(rb_eRuntimeError, "too many parameters: %ld/%ld\n",
             RARRAY_LEN(ary_imports), MINIFFI_MAX_ARGS);

  rb_iv_set(self, "_imports", ary_imports);
  int ex;
  if (NIL_P(exports)) {
    ex = _T_VOID;
  } else {
    SafeStringValue(exports);
    switch (*RSTRING_PTR(exports)) {
    case 'V':
    case 'v':
      ex = _T_VOID;
      break;

    case 'N':
    case 'n':
    case 'L':
    case 'l':
      ex = _T_NUMBER;
      break;

    case 'P':
    case 'p':
      ex = _T_POINTER;
      break;

    case 'I':
    case 'i':
      ex = _T_INTEGER;
      break;

    case 'B':
    case 'b':
      ex = _T_BOOL;
      break;
    }
  }
  rb_iv_set(self, "_exports", INT2FIX(ex));
  if (rb_block_given_p())
    rb_yield(self);
  return Qnil;
}

RB_METHOD(MiniFFI_call) {
  MiniFFIFuncArgs param;
#define params param.params
  VALUE func = rb_iv_get(self, "_func");
  VALUE own_imports = rb_iv_get(self, "_imports");
  VALUE own_exports = rb_iv_get(self, "_exports");
  MINIFFI_FUNC ApiFunction = (MINIFFI_FUNC)NUM2ULONG(func);
  VALUE args;
  int items = rb_scan_args(argc, argv, "0*", &args);
  int nimport = RARRAY_LEN(own_imports);
  if (items != nimport)
    rb_raise(rb_eRuntimeError,
             "wrong number of parameters: expected %d, got %d", nimport, items);

  for (int i = 0; i < nimport; i++) {
    VALUE str = rb_ary_entry(args, i);
    unsigned long lParam = 0;
    switch (FIX2INT(rb_ary_entry(own_imports, i))) {
    case _T_POINTER:
      if (NIL_P(str)) {
        lParam = 0;
      } else if (FIXNUM_P(str)) {
        lParam = NUM2ULONG(str);
      } else {
        StringValue(str);
        rb_str_modify(str);
        lParam = (unsigned long)RSTRING_PTR(str);
      }
      break;

    case _T_BOOL:
      lParam = RTEST(rb_ary_entry(args, i));
      break;

    case _T_INTEGER:
#if INTPTR_MAX == INT64_MAX
      lParam = NUM2UINT(rb_ary_entry(args, i)) & UINT32_MAX;
      break;
#endif
    case _T_NUMBER:
    default:
      lParam = NUM2ULONG(rb_ary_entry(args, i));
      break;
    }
    params[i] = lParam;
  }
#ifndef __WINDOWS__
  unsigned long ret =
      (unsigned long)ApiFunction(params[0], params[1], params[2], params[3],
                                 params[4], params[5], params[6], params[7]);

// Telling the compiler that the called function uses stdcall
// apparently doesn't work anymore, so assembly is used instead.
// Technically also allows for an unlimited number of arguments,
// but the above code does not
#else
  unsigned long ret = 0;
  void *old_esp = 0;

  asm volatile(INTEL_ASM

               "MiniFFI_call_asm:\n"
               "mov [edi], esp\n"
               "test ebx, ebx\n"
               "jz mffi_call_void\n"

               "shl ebx, 2\n"
               "mov ecx, ebx\n"

               "mffi_call_loop:\n"
               "sub ecx, 4\n"
               "mov ebx, [esi+ecx]\n"
               "push ebx\n"
               "test ecx, ecx\n"
               "jnz mffi_call_loop\n"

               "mffi_call_void:\n"
               "call edx\n"

               : "=a"(ret)
               : "b"(nimport), "S"(&param), "d"(ApiFunction), "D"(&old_esp)
               : "ecx");


  // If esp doesn't match, this was probably a cdecl and not a stdcall.
  // Move the stack pointer back to where it should be
  asm volatile(INTEL_ASM
               "mov edx, [edi]\n"
               "cmp edx, esp\n"
               "cmovne esp, edx"
               :
               : "D"(&old_esp)
               : "edx"
  );
#endif

  switch (FIX2INT(own_exports)) {
  case _T_NUMBER:
  case _T_INTEGER:
    return ULONG2NUM(ret);

  case _T_POINTER:
    return rb_str_new_cstr((char *)ret);

  case _T_BOOL:
    return rb_bool_new(ret);

  case _T_VOID:
  default:
    return ULONG2NUM(0);
  }
}

void MiniFFIBindingInit() {
  VALUE cMiniFFI = rb_define_class("MiniFFI", rb_cObject);
#if RAPI_FULL > 187
  rb_define_alloc_func(cMiniFFI, classAllocate<&MiniFFIType>);
#else
  rb_define_alloc_func(cMiniFFI, MiniFFIAllocate);
#endif
  _rb_define_method(cMiniFFI, "initialize", MiniFFI_initialize);
  _rb_define_method(cMiniFFI, "call", MiniFFI_call);
  rb_define_alias(cMiniFFI, "Call", "call");

  rb_define_const(rb_cObject, "Win32API", cMiniFFI);
}
