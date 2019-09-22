//#include <node.h>
#include <nan.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <concurrent_queue.h>
#include <concurrent_vector.h>
#include <concurrent_unordered_map.h>
#include <functional>

#include <chrono>
#include <sstream>

#include "v8-util.h"

#define ADIASSERT(exp) assert(exp)

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace v8;

using std::atomic;
using std::thread;
using std::mutex;
using std::lock_guard;
using std::condition_variable;
using Concurrency::concurrent_queue;
using Concurrency::concurrent_vector;
using Concurrency::concurrent_unordered_map;


// Stays in scope the entire time the addon is loaded.
Persistent<Object> persist;
v8::Persistent<v8::Context> gContext;

//typedef const std::function<void()> TWorkerFunc;

typedef std::thread::id TThreadKey;

class ThreadInfo;

static std::mutex gCoutMutex; //prevent garbled output on cout


class WorkerAction
   {
   public:

      WorkerAction()
         {
         }

      WorkerAction(const std::function<void(ThreadInfo* threadInfo)> &workerFunc, const std::function<void()> &mainFunc) :
         mInWorkerFunc(workerFunc), mInMainFunc(mainFunc)
         {
         }

      std::function<void(ThreadInfo* threadInfo)> mInWorkerFunc;
      std::function<void()> mInMainFunc;
   };


struct ThreadInfo
   {
   ThreadInfo(Isolate *isolate) :
      mThreadId(std::this_thread::get_id()),
      mIsolate(isolate),
      //mContext(isolate, isolate->GetCurrentContext()),
      mIsWaiting(false),
      mRecentlyUsed(false),
      mIndex(-1)
      {
      if(isolate)
         mContext.Reset(isolate, isolate->GetCurrentContext());
      }

   //Returns true if waiting or recently used flag is not set.
   bool ShouldBeUsed() //resets the recently used flag
      {
      //bool shouldUse = IsWaiting() || !mRecentlyUsed;
      bool shouldUse = !mRecentlyUsed;
      if (!shouldUse)
         mRecentlyUsed = false;
      return shouldUse;
      }

   void SetWaiting(bool waiting = true)
      {
      mIsWaiting = waiting;
      }

   void SetRecentlyUsed()
      {
      mRecentlyUsed = true;
      }

   bool IsWaiting() const
      {
      return mIsWaiting;
      }

   bool Wake()
      {
         {
         std::lock_guard<std::mutex> lock(mMutex);
         SetWaiting(false);
         //CreateInterThreadNotifierIfNeeded();
         }
      mCondition.notify_one();
      return true;
      }


   void CreateInterThreadNotifierIfNeeded();

   void Wait()
      {
      std::unique_lock<std::mutex> lock(mMutex);
      SetWaiting(true);
      mCondition.wait_for(lock, 300ms, [&] {return !IsWaiting(); });
      }


   void push(WorkerAction &&ac)
      {
      //if (!mInterThreadNotifier)
      //   CreateInterThreadNotifierIfNeeded();
      mWorkerActionQueue.push(ac);
      }

   std::mutex mMutex;
   std::condition_variable mCondition;

   //std::unique_ptr<ADINode::InterthreadNotifierLambda> mInterThreadNotifier;

   TThreadKey mThreadId;
   Isolate *mIsolate;
   Persistent<Context> mContext;

   typedef concurrent_queue<WorkerAction> TWorkerActionQueue;
   TWorkerActionQueue mWorkerActionQueue;

   //Persistent<v8::SharedArrayBuffer> expected_sab;
   std::atomic<bool> mIsWaiting;
   std::atomic<bool> mRecentlyUsed;

   uint32_t mIndex;
   };


typedef std::vector<Isolate*> TIsolates;

typedef std::unique_ptr<ThreadInfo> TThreadInfoPtr;


typedef concurrent_unordered_map< TThreadKey, TThreadInfoPtr, std::hash<std::thread::id>> TWorkerThreadsMap;
typedef concurrent_vector<TThreadInfoPtr> TWorkerThreadsVector;
//typedef std::vector<ThreadInfo> TWorkerThreadsVector;

TWorkerThreadsVector gWorkersVec;

class WebWorkerThreads
   {
   public:
      WebWorkerThreads() : mRover(0), mNWorkers(0)
         {
         }

      uint32_t Add(TThreadInfoPtr &&infoPtr)
         {
         std::lock_guard<mutex> lock(mMutex);
         auto threadInfo = infoPtr.get();
         mVector.push_back(std::move(infoPtr));
         threadInfo->mIndex = mNWorkers++;
         #ifdef ADIDEBUG
         mUseCount.push_back(0);
         #endif
         return threadInfo->mIndex;
         }

      ThreadInfo* Find(thread::id threadId)
         {
         for (uint32_t i(0), iEnd(mNWorkers); i < iEnd; ++i)
            {
            ThreadInfo* aWebWorker = mVector[i].get();
            if (aWebWorker->mThreadId == threadId)
               {
               #ifdef ADIDEBUG
               mUseCount[i]++;
               #endif
               return aWebWorker;
               }
            }
         return nullptr;
         }

      ThreadInfo* Find(Isolate *isolate)
         {
         for (uint32_t i(0), iEnd(mNWorkers); i < iEnd; ++i)
            {
            ThreadInfo* aWebWorker = mVector[i].get();
            if (aWebWorker->mIsolate == isolate)
               {
               #ifdef ADIDEBUG
               mUseCount[i]++;
               #endif
               return aWebWorker;
               }
            }
         return nullptr;
         }

      int Size()
         {
         return mVector.size();
         }

      void QueueAction(WorkerAction &&action, Isolate *isolate)
         {
         ThreadInfo* webWorker = nullptr;

         if (!isolate)
            {
            //Has no preferred thread (probably a new or changed calculation).
            //Find a thread with an empty queue. Prefer least recently used thread.
            uint32_t nWorkers = mNWorkers;
            uint32_t nToCheck = 2 * nWorkers;
            //Note: ShouldBeUsed() resets the recently used flag
            for (uint32_t i = 0; !mVector[mRover]->ShouldBeUsed() && i < nToCheck; ++i)
               if (++mRover >= nWorkers)
                  mRover = 0;
            webWorker = mVector[mRover].get();
            #ifdef ADIDEBUG
            mUseCount[mRover]++;
            #endif
            }
         else
            {
            //Find the worker with the correct thread id
            webWorker = Find(isolate);
            }

         if (!webWorker)
            {
            ADIASSERT(0);
            //if (!mNWorkers)
            //   Nan::ThrowError("Quark: No web worker threads present.");
            //else
            //   Nan::ThrowError("Quark: Unexpected web worker thread id");
            return;
            }
         webWorker->push(std::move(action));

         if (webWorker->IsWaiting())
            webWorker->Wake();
         }

      void QueueActionOnOrdinaryThread(WorkerAction &&action)
         {
         ThreadInfo* webWorker = nullptr;

         //Has no preferred thread (probably a new or changed calculation).
         //Find a thread with an empty queue. Prefer least recently used thread.
         uint32_t nWorkers = mNWorkers;
         uint32_t nToCheck = 2 * nWorkers;
         //Note: ShouldBeUsed() resets the recently used flag
         for (uint32_t i = 0; !mVector[mRover]->ShouldBeUsed() && i < nToCheck; ++i)
            if (++mRover >= nWorkers)
               mRover = 0;
         webWorker = mVector[mRover].get();
         #ifdef ADIDEBUG
         mUseCount[mRover]++;
         #endif
         if (!webWorker)
            {
            ADIASSERT(0);
            //if (!mNWorkers)
            //   Nan::ThrowError("Quark: No web worker threads present.");
            //else
            //   Nan::ThrowError("Quark: Unexpected web worker thread id");
            return;
            }
         webWorker->push(std::move(action));

         if (webWorker->IsWaiting())
            webWorker->Wake();
         }


      ThreadInfo *at(uint32_t i)
         {
         if (i < mVector.size())
            return mVector.at(i).get();
         return nullptr;
         }

   private:
      std::mutex mMutex;
      TWorkerThreadsVector mVector;
      atomic<uint32_t> mNWorkers;
      atomic<uint32_t> mRover;
      atomic<int> mInFlight;

      #ifdef ADIDEBUG
      std::vector<int> mUseCount; //Diagnostics only
      #endif
   };

typedef WebWorkerThreads TWorkerThreads;

WebWorkerThreads gWebWorkers;

std::once_flag gInitOrdinaryThread;
static std::unique_ptr<std::thread> gAnOrdinaryThread;

TWorkerThreads gOrdinaryThreads;

void mutate(Isolate * isolate) {
   while (true)
      {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::cerr << "Worker thread trying to enter isolate" << std::endl;
      v8::Locker locker(isolate);
      isolate->Enter();
      std::cerr << "Worker thread has entered isolate" << std::endl;
      // we need this to create local handles, since this
      // function is NOT called by Node.js
      {
      v8::HandleScope handleScope(isolate);

      //auto context = isolate->GetCurrentContext();
      auto context = Local<Context>::New(isolate, gContext);
      v8::Context::Scope context_scope{ context };

      Local<String> key = String::NewFromUtf8(isolate, "x");
      auto target = Local<Object>::New(isolate, persist);
      Local<Object> targetObj = target->ToObject(isolate);
      Local<Value> numJs;
      if (!targetObj.IsEmpty() || targetObj->IsObject())
         {
         if (targetObj->Get(context, key).ToLocal(&numJs))//>Get(context, key)->NumberValue();
            {
            auto current = numJs->NumberValue(context).FromMaybe(0);
            auto numJs = Number::New(isolate, current + 42);
            targetObj->Set(context, key, numJs);
            }
         }
      }

      isolate->Exit();

      // Note, the locker will go out of scope here, so the thread
      // will leave the isolate (release the lock)
      }
   }

void enterIso0FromOrdinaryThread(const FunctionCallbackInfo<Value>& args)
   {
   //First spin up an ordinary thread with a task queue if none present
   std::call_once(gInitOrdinaryThread, []()
      {
      ThreadInfo *ordinaryWorker = nullptr;
      gAnOrdinaryThread.reset(new thread([]()
         {
         auto ordinaryWorker = std::make_unique<ThreadInfo>(/*isolate*/nullptr);
         auto ordinaryWorkerWeak = ordinaryWorker.get();
         gOrdinaryThreads.Add(std::move(ordinaryWorker));

         //wait for work
         for (;;)
            {
            ordinaryWorkerWeak->Wait();
            WorkerAction action;
            while (ordinaryWorkerWeak->mWorkerActionQueue.try_pop(action))
               {
                  {
                  //std::unique_lock<std::mutex> lock(mMutex);
                  ordinaryWorkerWeak->SetWaiting(false);
                  ordinaryWorkerWeak->SetRecentlyUsed(); //We are doing real work
                  }

               try
                  {
                  if (action.mInWorkerFunc)
                     action.mInWorkerFunc(ordinaryWorkerWeak);
                  }
               catch (std::exception& err)
                  {
                  //action.setLastError(err);
                  }

               //if (workerPtr->mInterThreadNotifier)
               //   {
               //   workerPtr->mInterThreadNotifier->PostNotification([action]()
               //      {
               //      if (action.mInMainFunc)
               //         action.mInMainFunc();
               //      });
               //   }
               }
            }
         }));
      std::cout << "Started ordinary thread: "<< std::this_thread::get_id() << std::endl;
      });

   //store the JS script string in c++ string.
   Isolate * isolate = args.GetIsolate();
   std::string script;
   v8::HandleScope handleScope(isolate);
   if (args[0]->IsString())
      {
      v8::String::Utf8Value str(isolate, args[0]);
      script = *str;
      }

   //Queue a lambda function to run the script on an Isolate grabbed from a worker thread
   gOrdinaryThreads.QueueActionOnOrdinaryThread(WorkerAction([script](ThreadInfo *info)
      {
      std::ostringstream os;
      os << "throw Error('Exception from isolate " << info->mIndex << " on thread " << std::this_thread::get_id() << "');" << std::endl;

      auto isolate = Isolate::GetCurrent();

      //Enter isolate 0
      auto worker0 = gWebWorkers.at(0);
      auto isolate0 = worker0->mIsolate;
         { //Do some JS work
         Locker locker(isolate0); //This will block if isolate0 is busy!
         auto t1 = high_resolution_clock::now();
         Isolate::Scope isoScope(isolate0); //Enter Isolate.
         v8::HandleScope handleScope(isolate0);
         auto context = Local<Context>::New(isolate0, worker0->mContext);
         v8::Context::Scope context_scope{ context };
         auto t2 = high_resolution_clock::now();
         auto switchTime = duration_cast<duration<double>>(t2 - t1);

         TryCatch tryCatch(isolate0);

         auto result = ADI::CompileRun(script.c_str());
         if (!result.IsEmpty() && result->IsString())
            {
            v8::String::Utf8Value msg(isolate0, result->ToString(isolate0));
            std::cout<<*msg<<std::endl;
            }

         if (tryCatch.HasCaught())
            {
            v8::String::Utf8Value msg(isolate0, tryCatch.Message()->Get());
            std::cerr << *msg << std::endl;
            }

         std::ostringstream os;
         os << "Entering isolate (" << worker0->mIndex << ") on std::thread " << std::this_thread::get_id() << " took " << switchTime.count() << "secs." << std::endl;
         std::cout << os.str() << std::endl;

         }
      }, nullptr));
   }


void onWorkerStart(const FunctionCallbackInfo<Value> &args)
   {
   Isolate * isolate = args.GetIsolate();

   bool lockerActive = Locker::IsActive();
   bool locked = Locker::IsLocked(isolate);

   //TThreadKey id = std::this_thread::get_id();

   auto threadInfoPtr = std::make_unique<ThreadInfo>(isolate);
   uint32_t index = gWebWorkers.Add(std::move(threadInfoPtr));

   std::lock_guard<mutex> lock(gCoutMutex);
   std::cout <<"Worker "<<index <<" is thread "<<std::this_thread::get_id() << " and isolate "<<isolate<<std::endl;

   args.GetReturnValue().Set(Int32::New(isolate, index));
   }

void waitForTask(const FunctionCallbackInfo<Value> &args)
   {
   Isolate * isolate = args.GetIsolate();

   auto workerPtr = gWebWorkers.Find(isolate);
   if (!workerPtr)
      {
      //Nan::ThrowError("Quark: Unexpected web worker thread id");
      std::cerr << "Quark: Unexpected web worker isolate" << std::endl;
      ADIASSERT(0);
      return;
      }

   {
   isolate->Exit();
   v8::Unlocker unlocker(isolate); //Let other threads use this isolate

                                   // let worker execute for a second
                                   //std::this_thread::sleep_for(std::chrono::seconds(1));
   workerPtr->Wait();
   }
   // now that the unlocker is destroyed, re-enter.
   isolate->Enter();
   bool locked = Locker::IsLocked(isolate);
   //Context::Scope context_scope{ context };
   v8::HandleScope handleScope(isolate);

   bool hasContext0 = !v8::Isolate::GetCurrent()->GetCurrentContext().IsEmpty();

   //auto context = isolate->GetCurrentContext();
   auto context = Local<Context>::New(isolate, workerPtr->mContext);
   v8::Context::Scope context_scope{ context };

   bool hasContext1 = !v8::Isolate::GetCurrent()->GetCurrentContext().IsEmpty();


   WorkerAction action;
   while (workerPtr->mWorkerActionQueue.try_pop(action))
      {
         {
         //std::unique_lock<std::mutex> lock(mMutex);
         workerPtr->SetWaiting(false);
         workerPtr->SetRecentlyUsed(); //We are doing real work
         }

      try
         {
         if (action.mInWorkerFunc)
            action.mInWorkerFunc(workerPtr);
         }
      catch (std::exception& err)
         {
         //action.setLastError(err);
         }

      //if (workerPtr->mInterThreadNotifier)
      //   {
      //   workerPtr->mInterThreadNotifier->PostNotification([action]()
      //      {
      //      if (action.mInMainFunc)
      //         action.mInMainFunc();
      //      });
      //   }
      }
   }

void queWorkerAction(const FunctionCallbackInfo<Value> &args)
   {
   Isolate * isolate = args.GetIsolate();
   std::string script;
   {
   v8::HandleScope handleScope(isolate);
   if (args[0]->IsString())
      {
      v8::String::Utf8Value str(isolate, args[0]);
      script = *str;
      }
   }

   gWebWorkers.QueueAction(WorkerAction([script](ThreadInfo *info)
      {
      auto isolate = Isolate::GetCurrent();

      auto scriptJs = ADI::v8_compile(script.c_str());
      if (!scriptJs.IsEmpty())
         {
         TryCatch tryCatch(isolate);

         auto result = scriptJs->Run(isolate->GetCurrentContext());
         Local<Value> resultStr;
         if (result.ToLocal(&resultStr) && resultStr->IsString())
            {
            v8::String::Utf8Value msg(isolate, resultStr->ToString(isolate));
            std::cout << *msg << std::endl;
            }

         if (tryCatch.HasCaught())
            {
            v8::String::Utf8Value msg(isolate, tryCatch.Message()->Get());
            std::cerr << *msg << std::endl;
            }
         //Now run in other Isolate but still on this thread
         auto otherWorker = gWebWorkers.at(1 - info->mIndex);
         if (otherWorker)
            {
            auto t1 = high_resolution_clock::now();
            isolate->Exit();
            auto otherIso = otherWorker->mIsolate;
            {
            v8::Unlocker unlocker(isolate);
               {
               Locker locker(otherIso);
               otherIso->Enter();
               v8::HandleScope handleScope(otherIso);

               //auto context = isolate->GetCurrentContext();
               auto context = Local<Context>::New(otherIso, otherWorker->mContext);
               v8::Context::Scope context_scope{ context };

               auto t2 = high_resolution_clock::now();
               auto switchTime = duration_cast<duration<double>>(t2 - t1);


               TryCatch tryCatch(otherIso);

               std::cout << "\nSwitching to other isolate (" << otherWorker->mIndex << ") on thread " << std::this_thread::get_id() << ". Switch took " << switchTime.count() << "secs." << std::endl;

               auto result = ADI::CompileRun(script.c_str());
               if (!result.IsEmpty() && result->IsString())
                  {
                  v8::String::Utf8Value msg(otherIso, result->ToString(otherIso));
                  std::cout << *msg << std::endl;
                  }

               if (tryCatch.HasCaught())
                  {
                  v8::String::Utf8Value msg(otherIso, tryCatch.Message()->Get());
                  std::cerr << *msg << std::endl;
                  }
               }
            }
            // now that the unlocker is destroyed, re-enter original worker's isolate
            isolate->Enter();
            }
         }
      //ADI::CompileRun(os.str().c_str());
      //ADI::CompileRun(script.c_str());
      }, nullptr), nullptr);

   }


NODE_MODULE_INIT()
   {
   //init
   NODE_SET_METHOD(exports, "onWorkerStart", onWorkerStart);
   NODE_SET_METHOD(exports, "waitForTask", waitForTask);
   NODE_SET_METHOD(exports, "queWorkerAction", queWorkerAction);
   NODE_SET_METHOD(exports, "enterIso0FromOrdinaryThread", enterIso0FromOrdinaryThread);
   }
