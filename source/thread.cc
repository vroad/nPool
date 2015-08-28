#include "thread.h"

// C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// custom source
#include "file_manager.h"
#include "json_utility.h"
#include "utilities.h"
#include "callback_queue.h"
#include "isolate_context.h"

#include <mutex>

// file loader and hash (npool.cc)
static FileManager *fileManager = &(FileManager::GetInstance());

// callback queue
static CallbackQueue *callbackQueue = &(CallbackQueue::GetInstance());
static std::mutex removedIsolatesMutex;
static std::vector<Isolate*> removedIsolates;

void* Thread::ThreadInit()
{
    // allocate memory for thread context
    THREAD_CONTEXT* threadContext = (THREAD_CONTEXT*)malloc(sizeof(THREAD_CONTEXT));
    memset(threadContext, 0, sizeof(THREAD_CONTEXT));

    // create and initialize async watcher
    threadContext->uvAsync = (uv_async_t*)malloc(sizeof(uv_async_t));
    memset(threadContext->uvAsync, 0, sizeof(uv_async_t));
    uv_async_init(uv_default_loop(), threadContext->uvAsync, Thread::uvAsyncCallback);
    threadContext->uvAsync->close_cb = Thread::uvCloseCallback;

    // create thread isolate
    threadContext->threadIsolate = Isolate::New();

    // create module map
    threadContext->moduleMap = new ThreadModuleMap();

    return (void*)threadContext;
}

void Thread::ThreadPostInit(void* threadContext)
{
    // thread context
    THREAD_CONTEXT* thisContext = (THREAD_CONTEXT*)threadContext;

    // get reference to thread isolate
    Isolate* isolate = thisContext->threadIsolate;
    {
        // lock the isolate
        Locker myLocker(isolate);

        // enter the isolate
        isolate->Enter();

        // create a stack-allocated handle-scope
        Nan::HandleScope scope;

        // store reference to a new persistent context
        Local<Context> isolateContext = Nan::New<Context>();
        thisContext->threadJSContext = isolateContext;

        // enter thread specific context
        isolateContext->Enter();

        // create global context
        Local<Object> globalContext = Nan::GetCurrentContext()->Global();
        IsolateContext::CreateGlobalContext(globalContext);

        // create module context
        IsolateContext::CreateModuleContext(globalContext, NULL);

        // exit thread specific context
        isolateContext->Exit();
    }

    // leave the isolate
    isolate->Exit();
}

void Thread::ThreadDestroy(void* threadContext)
{
    // thread context
    THREAD_CONTEXT* thisContext = (THREAD_CONTEXT*)threadContext;

    // get reference to thread isolate
    Isolate* isolate = thisContext->threadIsolate;
    {
        // lock the isolate
        Locker myLocker(isolate);

        // enter the isolate
        isolate->Enter();

        // clean-up the worker modules
        for(ThreadModuleMap::iterator it = thisContext->moduleMap->begin(); it != thisContext->moduleMap->end(); ++it)
        {
            PersistentWrap* pWrap = it->second;
            pWrap->Unref();
            #if (NODE_MODULE_VERSION > 0x000B)
            pWrap->persistent().Reset();
            #else
            NanDisposePersistent(pWrap->handle_);
            #endif
            delete pWrap;
        }
        thisContext->moduleMap->clear();

        // dispose of js context
        thisContext->threadJSContext.Reset();
    }

    // exit the isolate
    isolate->Exit();

    // Dispose of the isolate
    //((Isolate *)(thisContext->threadIsolate))->Dispose();
    {
        std::lock_guard<std::mutex> lock(removedIsolatesMutex);
        removedIsolates.push_back(isolate);
    }

    // release the module map
    delete thisContext->moduleMap;

    //uv_unref((uv_handle_t*)thisContext->uvAsync);
    uv_close((uv_handle_t*)thisContext->uvAsync, ((uv_async_t*)thisContext->uvAsync)->close_cb);

    // release the thread context memory
    free(threadContext);
}

void Thread::DestroyIsolates()
{
    std::lock_guard<std::mutex> lock(removedIsolatesMutex);
    for (size_t i = 0; i < removedIsolates.size(); ++i)
        removedIsolates[i]->Dispose();
    removedIsolates.clear();
}

THREAD_WORK_ITEM* Thread::BuildWorkItem(Handle<Object> v8Object)
{
    // work item to be returned
    THREAD_WORK_ITEM *workItem = NULL;

    // exception catcher
    TryCatch tryCatch;

    // get all the properties from the object
    Local<Number> workId = v8Object->Get(Nan::New<String>("workId").ToLocalChecked())->ToUint32();
    Local<Number> fileKey = v8Object->Get(Nan::New<String>("fileKey").ToLocalChecked())->ToUint32();
    Local<String> workFunction = v8Object->Get(Nan::New<String>("workFunction").ToLocalChecked())->ToString();
    Local<Object> workParam = v8Object->Get(Nan::New<String>("workParam").ToLocalChecked())->ToObject();
    Local<Object> callbackContext = v8Object->Get(Nan::New<String>("callbackContext").ToLocalChecked())->ToObject();
    Handle<Function> callbackFunction = Handle<Function>::Cast(v8Object->Get(Nan::New<String>("callbackFunction").ToLocalChecked()));

    // determine if the object is valid
    bool isInvalidWorkObject = (workId.IsEmpty() ||
                                fileKey.IsEmpty() ||
                                workFunction.IsEmpty() ||
                                workParam.IsEmpty() ||
                                callbackContext == Nan::Undefined() || callbackContext.IsEmpty() ||
                                callbackFunction.IsEmpty());

    // ensure there weren't any exceptions and properties were valid
    if((isInvalidWorkObject == false) || !(tryCatch.HasCaught()))
    {
        // return value
        workItem = (THREAD_WORK_ITEM*)malloc(sizeof(THREAD_WORK_ITEM));
        memset(workItem, 0, sizeof(THREAD_WORK_ITEM));

        // workId
        workItem->workId = workId->Value();

        // fileKey
        workItem->fileKey = fileKey->Value();

        // workFunction
        workItem->workFunction = Utilities::CreateCharBuffer(workFunction);

        // generate JSON c str of param object
        workItem->workParam = createDataFromValue(workParam);

        // callback context
        workItem->callbackContext = callbackContext;

        // callback function
        workItem->callbackFunction = callbackFunction;

        // register external memory
        int bytesAlloc = /*(workItem->workParam->length() + 1)*/ + strlen(workItem->workFunction);
        Nan::AdjustExternalMemory(bytesAlloc);
    }

    return workItem;
}

void Thread::QueueWorkItem(TASK_QUEUE_DATA *taskQueue, THREAD_WORK_ITEM *workItem)
{
    // reference to task queue item to be added
    TASK_QUEUE_ITEM     *taskQueueItem = 0;

    // create task queue item object
    taskQueueItem = (TASK_QUEUE_ITEM*)malloc(sizeof(TASK_QUEUE_ITEM));
    memset(taskQueueItem, 0, sizeof(TASK_QUEUE_ITEM));

    // set the data size
    taskQueueItem->dataSize = sizeof(THREAD_WORK_ITEM);

    // store reference to work item
    taskQueueItem->taskItemData = (void*)workItem;

    // set the task item work function
    taskQueueItem->taskItemFunction = Thread::WorkItemFunction;

    // set the task item callback function
    taskQueueItem->taskItemCallback = Thread::WorkItemCallback;

    // set the task item id
    taskQueueItem->taskId = workItem->workId;

    // add the task to the queue
    AddTaskToQueue(taskQueue, taskQueueItem);
}

void* Thread::WorkItemFunction(TASK_QUEUE_WORK_DATA *taskData, void *threadContext, void *threadWorkItem)
{
    //fprintf(stdout, "[%u] Thread::WorkItemFunction\n", SyncGetThreadId());

    // thread context
    THREAD_CONTEXT* thisContext = (THREAD_CONTEXT*)threadContext;

    // thread work item
    THREAD_WORK_ITEM* workItem = (THREAD_WORK_ITEM*)threadWorkItem;

    // get reference to thread isolate
    Isolate* isolate = thisContext->threadIsolate;
    {
        // Lock the isolate
        Locker myLocker(isolate);

        // Enter the isolate
        isolate->Enter();

        // Create a stack-allocated handle scope.
        Nan::HandleScope scope;

        // enter thread specific context
        Local<Context> isolateContext = Nan::New<Context>(thisContext->threadJSContext);
        isolateContext->Enter();

        // exception catcher
        TryCatch tryCatch;

        // get worker object
        Handle<Object> workerObject = Thread::GetWorkerObject(thisContext, workItem);

        // no errors getting the worker object
        if(workItem->isError == false)
        {
            // get work param
            Handle<Value> workParam = workItem->workParam->GetV8Value();

            // get worker function name
            Handle<Value> workerFunction = workerObject->Get(Nan::New<String>(workItem->workFunction).ToLocalChecked());

            // execute function and get work result
            Handle<Value> workResult = workerFunction.As<Function>()->Call(workerObject, 1, &workParam);

            // work failed to perform successfully
            if(workResult.IsEmpty() || tryCatch.HasCaught())
            {
                workItem->jsException = Utilities::HandleException(&tryCatch, true);
                workItem->isError = true;
            }
            // work performed successfully
            else
            {
                // strinigfy callback object
                workItem->callbackObject = createDataFromValue(workResult);
                workItem->isError = false;

                // register external memory
#if 0
                NanAdjustExternalMemory(workItem->callbackObject->length() + 1);
#endif
            }
        }

        // exit thread specific context
        isolateContext->Exit();
    }

    // leave the isolate
    isolate->Exit();

    // return the work item
    return workItem;
}

void Thread::WorkItemCallback(TASK_QUEUE_WORK_DATA *taskData, void *threadContext, void *threadWorkItem)
{
    //fprintf(stdout, "[%u] Thread::WorkItemCallback\n", SyncGetThreadId());

    // thread context
    THREAD_CONTEXT* thisContext = (THREAD_CONTEXT*)threadContext;

    // add work item to callback queue
    THREAD_WORK_ITEM* workItem = (THREAD_WORK_ITEM*)malloc(sizeof(THREAD_WORK_ITEM));
    memcpy(workItem, threadWorkItem, sizeof(THREAD_WORK_ITEM));
    callbackQueue->AddWorkItem(workItem);

    // async callback
    uv_async_t *uvAsync = (uv_async_t*)thisContext->uvAsync;
    uv_async_send(uvAsync);
}

void Thread::uvCloseCallback(uv_handle_t* handle)
{
    //fprintf(stdout, "[%u] Thread::uvCloseCallback - Async: %p\n", SyncGetThreadId(), handle);
    // cleanup the handle
    free(handle);
}

#if NODE_VERSION_AT_LEAST(0, 11, 13)
void Thread::uvAsyncCallback(uv_async_t* handle)
#else
void Thread::uvAsyncCallback(uv_async_t* handle, int status)
#endif
{
    //fprintf(stdout, "[%u] Thread::uvAsyncCallback - Async: %p\n", SyncGetThreadId(), handle);

    Nan::HandleScope scope;

    // process all work items awaiting callback
    THREAD_WORK_ITEM* workItem = 0;
    while((workItem = callbackQueue->GetWorkItem()) != 0)
    {
        Handle<Value> callbackObject = Nan::Null();
        Handle<Value> exceptionObject = Nan::Null();

        // parse exception if one is present
        if(workItem->isError == true)
        {
            exceptionObject = JsonUtility::Parse(**(workItem->jsException));
        }
        else
        {
            // parse stringified result
            callbackObject = workItem->callbackObject->GetV8Value();
        }

        //create arguments array
        const unsigned argc = 3;
        Handle<Value> argv[argc] = { callbackObject, Nan::New<Number>(workItem->workId), exceptionObject };

        // make callback on node thread
        Local<Function> callbackFunction = Nan::New<Function>(workItem->callbackFunction);
        callbackFunction->Call(Nan::New<Object>(workItem->callbackContext), argc, argv);

        // clean up memory and dispose of persistent references
        Thread::DisposeWorkItem(workItem, true);
    }
}

Handle<Object> Thread::GetWorkerObject(THREAD_CONTEXT* thisContext, THREAD_WORK_ITEM* workItem)
{
    Nan::EscapableHandleScope scope;

    // return variable and exception
    Handle<Object> workerObject;
    TryCatch tryCatch;

    // check module cache
    const FILE_INFO* workFileInfo = 0;
    if(thisContext->moduleMap->find(workItem->fileKey) == thisContext->moduleMap->end())
    {
        // get work file string
        workFileInfo = fileManager->GetFileInfo(workItem->fileKey);
    }

    // compile the object script if necessary
    if(workFileInfo != 0)
    {
        // update the context for file properties of work file
        Handle<Object> globalContext = Nan::New<Context>(thisContext->threadJSContext)->Global();
        IsolateContext::UpdateContextFileProperties(globalContext, workFileInfo);

        // compile the source code
        ScriptOrigin scriptOrigin(Nan::New<String>(workFileInfo->fileName).ToLocalChecked());
        Handle<Script> script = Script::Compile(Nan::New<String>(workFileInfo->fileBuffer).ToLocalChecked(), &scriptOrigin);

        // check for exception on compile
        if(script.IsEmpty() || tryCatch.HasCaught())
        {
            workItem->jsException = Utilities::HandleException(&tryCatch, true);
            workItem->isError = true;
        }
        // no exception
        else
        {
            // run the script to get the result
            Handle<Value> scriptResult = script->Run();

            // throw exception if script failed to run properly
            if(scriptResult.IsEmpty() || tryCatch.HasCaught())
            {
                workItem->jsException = Utilities::HandleException(&tryCatch, true);
                workItem->isError = true;
            }
            else
            {
                 // create object template in order to use object wrap
                Handle<ObjectTemplate> objectTemplate = Nan::New<ObjectTemplate>();
                objectTemplate->SetInternalFieldCount(1);
                workerObject = objectTemplate->NewInstance();

                Handle<Object> module = globalContext->Get(Nan::New<String>("module").ToLocalChecked())->ToObject();
                Handle<Value> exports = module->Get(Nan::New<String>("exports").ToLocalChecked());

                // copy the script result to the worker object
                if (exports->IsObject())
                    Utilities::CopyObject(
                        workerObject,
                        exports->ToObject());

                // cache the persistent object type for later use
                // wrap the object so it can be persisted
                PersistentWrap* objectWrap = new PersistentWrap();
                objectWrap->Wrap(workerObject);
                objectWrap->Ref();
                thisContext->moduleMap->insert(make_pair(
                    workItem->fileKey,
                    objectWrap));
            }
        }
    }
    // get the worker object from the cache
    else
    {
        // get the constructor function from cache
        workerObject = Nan::New<Object>(thisContext->moduleMap->find(workItem->fileKey)->second->persistent());
    }

    return scope.Escape(workerObject);
}

void Thread::DisposeWorkItem(THREAD_WORK_ITEM* workItem, bool freeWorkItem)
{
    // cleanup the work item data
    workItem->callbackContext.Reset();
    workItem->callbackFunction.Reset();

    // de-register memory
    int bytesToFree = /*(workItem->workParam->length() + 1) +*/ strlen(workItem->workFunction);
#if 0
    if(workItem->callbackObject != NULL)
    {
        bytesToFree += (workItem->callbackObject->length() + 1);
    }
#endif
    Nan::AdjustExternalMemory(-bytesToFree);

    // un-alloc the memory
    free(workItem->workFunction);
    delete workItem->workParam;
    if(workItem->callbackObject != NULL)
    {
        delete workItem->callbackObject;
    }
    if(workItem->isError == true)
    {
        delete workItem->jsException;
    }
    if(freeWorkItem == true)
    {
        free(workItem);
    }
}
