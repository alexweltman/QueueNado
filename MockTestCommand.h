#pragma once
#include "include/global.h"
#include "ProcessManager.h"
#include "Command.h"
#include "Conf.h"
#include <memory>

class MockTestCommand : public Command {
public:

   virtual ~MockTestCommand() {
   }

   virtual protoMsg::CommandReply Execute(const Conf& conf) {
      protoMsg::CommandReply reply;
      reply.set_success(mSuccess);
      reply.set_result(mResult);
      unsigned int randSleep = rand() % 10;
      std::this_thread::sleep_for(std::chrono::milliseconds(randSleep));
      return reply;
   }

   static std::shared_ptr<Command> Construct(const protoMsg::CommandRequest& request, const std::string& programName) {
      std::shared_ptr<Command> command(new MockTestCommand(request,programName));
      return command;
   }
   protoMsg::CommandReply GetResult() {
      return mAsyncResult;
   }
   bool Finished() {
      return mFinished;
   }
   bool mSuccess;
   std::string mResult;
   protoMsg::CommandRequest mRequest;
protected:

   MockTestCommand(const protoMsg::CommandRequest& request, const std::string& programName) : Command(programName),
   mResult("TestCommand"), mSuccess(true), mRequest(request) {

   }

};

class MockTestCommandAlwaysFails : public MockTestCommand {
public:
   virtual ~MockTestCommandAlwaysFails() {
   }
   static std::shared_ptr<Command> Construct(const protoMsg::CommandRequest& request, const std::string& programName) {
      std::shared_ptr<Command> command(new MockTestCommandAlwaysFails(request,programName));
      return command;
   }

protected:

   MockTestCommandAlwaysFails(const protoMsg::CommandRequest& request, const std::string& programName) : MockTestCommand(request,programName) {
      mResult="TestCommandFails";
      mSuccess=false;
      mRequest=request;
   }
};

class MockTestCommandRunsForever : public MockTestCommand {
public:
   virtual ~MockTestCommandRunsForever() {
   }
   static std::shared_ptr<Command> Construct(const protoMsg::CommandRequest& request, const std::string& programName) {
      std::shared_ptr<Command> command(new MockTestCommandRunsForever(request,programName));
      return command;
   }
   virtual protoMsg::CommandReply Execute(const Conf& conf) {
      protoMsg::CommandReply reply;
      reply.set_success(mSuccess);
      reply.set_result(mResult);
      unsigned int randSleep = 1000000;
      std::this_thread::sleep_for(std::chrono::milliseconds(randSleep));
      return reply;
   }
protected:

   MockTestCommandRunsForever(const protoMsg::CommandRequest& request, const std::string& programName) : MockTestCommand(request, programName) {

   }
};