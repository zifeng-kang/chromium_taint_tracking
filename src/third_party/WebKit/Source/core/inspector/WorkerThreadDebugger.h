/*
 * Copyright (c) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WorkerThreadDebugger_h
#define WorkerThreadDebugger_h

#include "core/CoreExport.h"
#include "core/inspector/ThreadDebugger.h"

namespace blink {

class ConsoleMessage;
class ErrorEvent;
class SourceLocation;
class WorkerThread;

class CORE_EXPORT WorkerThreadDebugger final : public ThreadDebugger {
    WTF_MAKE_NONCOPYABLE(WorkerThreadDebugger);
public:
    explicit WorkerThreadDebugger(WorkerThread*, v8::Isolate*);
    ~WorkerThreadDebugger() override;

    static WorkerThreadDebugger* from(v8::Isolate*);
    void reportConsoleMessage(ExecutionContext*, MessageSource, MessageLevel, const String& message, SourceLocation*) override;
    int contextGroupId(ExecutionContext*) override;

    int contextGroupId();
    void contextCreated(v8::Local<v8::Context>);
    void contextWillBeDestroyed(v8::Local<v8::Context>);
    void exceptionThrown(ErrorEvent*);
    unsigned promiseRejected(v8::Local<v8::Context>, const String16& errorMessage, v8::Local<v8::Value> exception, std::unique_ptr<SourceLocation>);

    // V8InspectorClient implementation.
    void runMessageLoopOnPause(int contextGroupId) override;
    void quitMessageLoopOnPause() override;
    void muteMetrics(int contextGroupId) override;
    void unmuteMetrics(int contextGroupId) override;
    v8::Local<v8::Context> ensureDefaultContextInGroup(int contextGroupId) override;
    void beginEnsureAllContextsInGroup(int contextGroupId) override;
    void endEnsureAllContextsInGroup(int contextGroupId) override;
    bool canExecuteScripts(int contextGroupId) override;
    void resumeStartup(int contextGroupId) override;

    v8::MaybeLocal<v8::Value> memoryInfo(v8::Isolate*, v8::Local<v8::Context>) override;
    void consoleAPIMessage(int contextGroupId, V8ConsoleAPIType, const String16& message, const String16& url, unsigned lineNumber, unsigned columnNumber, V8StackTrace*) override;

private:
    WorkerThread* m_workerThread;
};

} // namespace blink

#endif // WorkerThreadDebugger_h