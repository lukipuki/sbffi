#include <iostream>
#include <node.h>

#ifdef __GNUC__
#define UNLIKELY(expr) __builtin_expect(!!(expr), 0)
#else
#define UNLIKELY(expr) expr
#endif
#define CHECK(expr)                                                           \
  do {                                                                        \
    if (UNLIKELY(!(expr))) {                                                  \
      std::cout << #expr << "\n";                                             \
      abort();                                                            \
    }                                                                         \
  } while (0)
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_LT(a, b) CHECK((a) < (b))

#include <v8-fast-api-calls.h>
#include <dyncall.h>
#include "sbffi_common.h"

using namespace v8;

namespace sbffi {

typedef struct fn_sig {
  DCCallVM * vm;
  void * fn;
  fn_type return_type;
  size_t argc;
  fn_type argv[];
} fn_sig;

uint8_t * callBuffer;

void js_setCallBuffer(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  auto buf = args[0].As<ArrayBuffer>();
  callBuffer = (uint8_t *)buf->GetContents().Data();
}

#define call_to_buf(_1, dcTyp, dcFn, _4, _5, _6) void call_##dcFn(DCCallVM * vm, DCpointer funcptr) {\
  dcTyp * retVal = (dcTyp*)(callBuffer + 8);\
  *retVal = dcFn(vm, funcptr);\
}
call_types_except_void(call_to_buf)

void makeFnCall() {
  fn_sig * sig = *(fn_sig **)callBuffer;
  uint8_t * origOffset = callBuffer;
  uint8_t * offset = callBuffer + sizeof(fn_sig *);
  void (*callFn)(DCCallVM *, DCpointer);
  switch (sig->return_type) {
    case fn_type_void:
      callFn = &dcCallVoid;
      break;
#define js_call_ret_case(enumTyp, typ, callFunc, _4, _5, _6) \
    case enumTyp: \
      callFn = &call_##callFunc;\
      offset += sizeof(typ);\
      break;
    call_types_except_void(js_call_ret_case)
    default:
      abort();
  }

  for (size_t i = 0; i < sig->argc; i++) {
    fn_type typ = sig->argv[i];
    switch (typ) {
      case fn_type_void:
        abort();
#define js_call_arg_case(enumTyp, argTyp, _3, argFunc, _5, _6) \
      case enumTyp:\
        argFunc(sig->vm, *(argTyp*)offset);\
        offset += sizeof(argTyp);\
        break;
      call_types_except_void(js_call_arg_case)
      default:
        abort();
    }
  }

  callFn(sig->vm, (DCpointer)sig->fn);
  dcReset(sig->vm);

  return;
}

void js_slowcall(const FunctionCallbackInfo<Value>& args) {
  makeFnCall();
}

void js_fastcall() {
  makeFnCall();
}

void Initialize(Local<Object> exports) {
  Isolate* isolate = exports->GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  auto cfunc = CFunction::Make(js_fastcall);
  auto funcTmpl = v8::FunctionTemplate::New(isolate,
      js_slowcall,
      Local<Value>(),
      Local<v8::Signature>(),
      0,
      v8::ConstructorBehavior::kThrow,
      v8::SideEffectType::kHasNoSideEffect,
      &cfunc);
  auto func = funcTmpl
    ->GetFunction(context)
    .ToLocalChecked();
  exports->Set(context, String::NewFromUtf8(isolate, "call").ToLocalChecked(), func);
  NODE_SET_METHOD(exports, "setCallBuffer", js_setCallBuffer);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Initialize)

}
