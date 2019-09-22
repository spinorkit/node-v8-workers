#pragma once

#include <node.h>
//#include "adi-nan.h"
//#include <nan.h>

//#include "ADIString/StringUtil.h"


namespace ADI
{

inline v8::Local<v8::String> v8_str(const char* x)
   {
   return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), x,
      v8::NewStringType::kNormal)
      .ToLocalChecked();
   }


inline v8::Local<v8::Script> v8_compile(v8::Local<v8::String> x)
   {
   v8::Local<v8::Script> result;
   if (v8::Script::Compile(v8::Isolate::GetCurrent()->GetCurrentContext(), x)
      .ToLocal(&result))
      {
      return result;
      }
   return v8::Local<v8::Script>();
   }


inline v8::Local<v8::Script> v8_compile(const char* x)
   {
   return v8_compile(v8_str(x));
   }

//inline v8::Local<v8::Script> v8_compile(const ADI::String& x)
//   {
//   auto isolate = v8::Isolate::GetCurrent();
//   v8::Local<v8::Script> result;
//   if (v8::Script::Compile(isolate->GetCurrentContext(), ADI::NewFromADIString(isolate, x))
//      .ToLocal(&result))
//      {
//      return result;
//      }
//   return v8::Local<v8::Script>();
//   }



inline v8::Local<v8::Value> CompileRun(v8::Local<v8::String> source)
   {
   v8::Local<v8::Value> result;
   if (v8_compile(source)
      ->Run(v8::Isolate::GetCurrent()->GetCurrentContext())
      .ToLocal(&result))
      {
      return result;
      }
   return v8::Local<v8::Value>();
   }

// Helper functions that compile and run the source.
inline v8::Local<v8::Value> CompileRun(const char* source)
   {
   return CompileRun(v8_str(source));
   }

//inline v8::Local<v8::Value> CompileRun(const ADI::String &source)
//   {
//   return CompileRun(ADI::NewFromADIString(v8::Isolate::GetCurrent(), source));
//   }

inline v8::Local<v8::Value> CompileRun(
   v8::Local<v8::Context> context, v8::ScriptCompiler::Source* script_source,
   v8::ScriptCompiler::CompileOptions options)
   {
   v8::Local<v8::Value> result;
   if (v8::ScriptCompiler::Compile(context, script_source, options)
      .ToLocalChecked()
      ->Run(context)
      .ToLocal(&result))
      {
      return result;
      }
   return v8::Local<v8::Value>();
   }



}//namespace
