#include "CommandProcessorTests.h"
#include "Conf.h"
#include "UpgradeCommandTest.h"
#include "NetworkConfigCommandTest.h"
#include "MockConf.h"
#include "Headcrab.h"
#include "Crowbar.h"
#include "libconf/Conf.h"
#include "boost/lexical_cast.hpp"
#include "CommandReply.pb.h"
#include "MockCommandProcessor.h"
#include "MockUpgradeCommand.h"
#include "CommandRequest.pb.h"
#include "MockProcessManagerCommand.h"
#include "CommandFailedException.h"
#include "QosmosDpiTest.h"
#include "RebootCommand.h"
#include "NtpConfigCommand.h"
#include "RebootCommandTest.h"
#include "MockShutdownCommand.h"
#include <g2loglevels.hpp>
#include "g2log.hpp"
#include "RestartSyslogCommandTest.h"
#include "NetInterfaceMsg.pb.h"
#include "ShutdownMsg.pb.h"
#include "MockTestCommand.h"
#ifdef LR_DEBUG

TEST_F(CommandProcessorTests, ExecuteForkTests) {
   protoMsg::CommandRequest requestMsg;
   requestMsg.set_type(protoMsg::CommandRequest_CommandType_TEST);
   requestMsg.set_async(true);
   std::shared_ptr<Command> holdMe(MockTestCommand::Construct(requestMsg));
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   std::shared_ptr<std::atomic<bool> > threadRefSet = std::make_shared<std::atomic<bool> >(false);

   unsigned int count(0);
   std::weak_ptr<Command> weakCommand(holdMe);
   Command::ExecuteFork(holdMe, conf, threadRefSet);

   while (!*threadRefSet && !zctx_interrupted && count++ < 1000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   ASSERT_TRUE(1000 > count); // the thread ownership shouldn't take longer than a second
   EXPECT_TRUE(holdMe.use_count() > 1); // the thread has at least incremented the count by one (possibly 2)
   count = 0;
   while (!std::dynamic_pointer_cast<MockTestCommand>(holdMe)->Finished() && !zctx_interrupted && count++ < 1000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   ASSERT_TRUE(1000 > count); // the completion shouldn't take longer than a second
   EXPECT_TRUE(std::dynamic_pointer_cast<MockTestCommand>(holdMe)->GetResult().success());
   EXPECT_TRUE(std::dynamic_pointer_cast<MockTestCommand>(holdMe)->GetResult().result() == "TestCommand");
   ASSERT_TRUE(std::dynamic_pointer_cast<MockTestCommand>(holdMe)->GetResult().has_completed());
   EXPECT_TRUE(std::dynamic_pointer_cast<MockTestCommand>(holdMe)->GetResult().completed());
   EXPECT_TRUE(holdMe.use_count() == 2); // the reference count stays +1 till GetStatus succeeds
   protoMsg::CommandReply reply = Command::GetStatus(weakCommand);
   EXPECT_TRUE(reply.success() == std::dynamic_pointer_cast<MockTestCommand>(holdMe)->GetResult().success());
   EXPECT_TRUE(reply.result() == std::dynamic_pointer_cast<MockTestCommand>(holdMe)->GetResult().result());
   EXPECT_TRUE(reply.completed() == std::dynamic_pointer_cast<MockTestCommand>(holdMe)->GetResult().completed());
   EXPECT_TRUE(holdMe.use_count() == 1); // the thread has dropped all but one reference
}

TEST_F(CommandProcessorTests, GetStatusTests) {
   protoMsg::CommandRequest requestMsg;
   requestMsg.set_type(protoMsg::CommandRequest_CommandType_TEST);
   requestMsg.set_async(true);
   std::shared_ptr<Command> holdMe(MockTestCommand::Construct(requestMsg));
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);

   std::weak_ptr<Command> weakCommand(holdMe);

   protoMsg::CommandReply reply = Command::GetStatus(weakCommand);
   EXPECT_FALSE(reply.success());
   EXPECT_FALSE(reply.has_completed());

   holdMe.reset();

   reply = Command::GetStatus(weakCommand);
   EXPECT_TRUE(reply.success());
   EXPECT_TRUE(reply.has_completed());
   EXPECT_TRUE(reply.completed());
   EXPECT_TRUE("Result Already Sent" == reply.result());
}

TEST_F(CommandProcessorTests, StartAQuickAsyncCommandAndGetStatusAlwaysFails) {

   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   testProcessor.ChangeRegistration(protoMsg::CommandRequest_CommandType_TEST, MockTestCommandAlwaysFails::Construct);

   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   protoMsg::CommandRequest requestMsg;
   unsigned int count(0);
   std::string reply;
   protoMsg::CommandReply realReply;
   protoMsg::CommandReply replyMsg;
   for (int i = 0; i < 100; i++) {
      requestMsg.set_type(protoMsg::CommandRequest_CommandType_TEST);
      requestMsg.set_async(true);
      sender.Swing(requestMsg.SerializeAsString());
      sender.BlockForKill(reply);
      EXPECT_FALSE(reply.empty());

      replyMsg.ParseFromString(reply);
      EXPECT_TRUE(replyMsg.success());
      count = 0;

      requestMsg.set_type(::protoMsg::CommandRequest_CommandType_COMMAND_STATUS);
      requestMsg.set_async(false);
      requestMsg.set_stringargone(replyMsg.result());
      do {

         requestMsg.set_stringargone(replyMsg.result());
         sender.Swing(requestMsg.SerializeAsString());
         std::string reply;
         sender.BlockForKill(reply);
         EXPECT_FALSE(reply.empty());
         realReply.ParseFromString(reply);
         if (realReply.has_completed() && realReply.completed()) {
            break;
         } else {
            EXPECT_TRUE(realReply.result() == "Command running");
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (!zctx_interrupted && count++ < 100);
      EXPECT_TRUE(realReply.result() == "TestCommandFails");
      EXPECT_FALSE(realReply.success());
   }
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_TRUE(realReply.success());
   EXPECT_TRUE(realReply.result() == "Result Already Sent");
   std::this_thread::sleep_for(std::chrono::milliseconds(1001));
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_FALSE(realReply.success());
   EXPECT_TRUE(realReply.result() == "Command Not Found");

   raise(SIGTERM);
}

TEST_F(CommandProcessorTests, StartAQuickAsyncCommandAndGetStatusForcedKill) {

   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   testProcessor.ChangeRegistration(protoMsg::CommandRequest_CommandType_TEST, MockTestCommandRunsForever::Construct);

   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   protoMsg::CommandRequest requestMsg;
   unsigned int count(0);
   std::string reply;
   protoMsg::CommandReply realReply;
   protoMsg::CommandReply replyMsg;
   requestMsg.set_type(protoMsg::CommandRequest_CommandType_TEST);
   requestMsg.set_async(true);
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());

   replyMsg.ParseFromString(reply);
   EXPECT_TRUE(replyMsg.success());
   count = 0;

   requestMsg.set_type(::protoMsg::CommandRequest_CommandType_COMMAND_STATUS);
   requestMsg.set_async(false);
   requestMsg.set_stringargone(replyMsg.result());
   do {
      requestMsg.set_stringargone(replyMsg.result());
      sender.Swing(requestMsg.SerializeAsString());
      std::string reply;
      sender.BlockForKill(reply);
      EXPECT_FALSE(reply.empty());
      realReply.ParseFromString(reply);
      if (realReply.has_completed() && realReply.completed()) {
         break;
      } else {
         EXPECT_TRUE(realReply.result() == "Command running");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   } while (!zctx_interrupted && count++ < 100);
   testProcessor.timeout = 1;
   std::this_thread::sleep_for(std::chrono::milliseconds(2001));
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_FALSE(realReply.success());
   EXPECT_TRUE(realReply.result() == "Command Not Found");

   raise(SIGTERM);
}

TEST_F(CommandProcessorTests, StartAQuickAsyncCommandAndGetStatusDontGetStatus) {

   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   testProcessor.ChangeRegistration(protoMsg::CommandRequest_CommandType_TEST, MockTestCommand::Construct);

   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   protoMsg::CommandRequest requestMsg;
   unsigned int count(0);
   std::string reply;
   protoMsg::CommandReply realReply;
   protoMsg::CommandReply replyMsg;
   requestMsg.set_type(protoMsg::CommandRequest_CommandType_TEST);
   requestMsg.set_async(true);
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());

   replyMsg.ParseFromString(reply);
   EXPECT_TRUE(replyMsg.success());
   count = 0;

   requestMsg.set_type(::protoMsg::CommandRequest_CommandType_COMMAND_STATUS);
   requestMsg.set_async(false);
   requestMsg.set_stringargone(replyMsg.result());
   testProcessor.timeout = 0;
   std::this_thread::sleep_for(std::chrono::milliseconds(2001));
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_FALSE(realReply.success());
   EXPECT_TRUE(realReply.result() == "Command Not Found") << " : " << realReply.result();
   raise(SIGTERM);
}

TEST_F(CommandProcessorTests, StartAQuickAsyncCommandAndGetStatusExitApp) {

   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   testProcessor.ChangeRegistration(protoMsg::CommandRequest_CommandType_TEST, MockTestCommandRunsForever::Construct);

   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   protoMsg::CommandRequest requestMsg;
   unsigned int count(0);
   std::string reply;
   protoMsg::CommandReply realReply;
   protoMsg::CommandReply replyMsg;
   requestMsg.set_type(protoMsg::CommandRequest_CommandType_TEST);
   requestMsg.set_async(true);
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());

   replyMsg.ParseFromString(reply);
   EXPECT_TRUE(replyMsg.success());
   raise(SIGTERM);
   std::this_thread::sleep_for(std::chrono::milliseconds(1001));
}
TEST_F(CommandProcessorTests, StartAQuickAsyncCommandAndGetStatus) {

   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   testProcessor.ChangeRegistration(protoMsg::CommandRequest_CommandType_TEST, MockTestCommand::Construct);

   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   protoMsg::CommandRequest requestMsg;
   unsigned int count(0);
   std::string reply;
   protoMsg::CommandReply realReply;
   protoMsg::CommandReply replyMsg;
   for (int i = 0; i < 100; i++) {
      requestMsg.set_type(protoMsg::CommandRequest_CommandType_TEST);
      requestMsg.set_async(true);
      sender.Swing(requestMsg.SerializeAsString());
      sender.BlockForKill(reply);
      EXPECT_FALSE(reply.empty());

      replyMsg.ParseFromString(reply);
      EXPECT_TRUE(replyMsg.success());
      count = 0;

      requestMsg.set_type(::protoMsg::CommandRequest_CommandType_COMMAND_STATUS);
      requestMsg.set_async(false);
      requestMsg.set_stringargone(replyMsg.result());
      do {

         requestMsg.set_stringargone(replyMsg.result());
         sender.Swing(requestMsg.SerializeAsString());
         std::string reply;
         sender.BlockForKill(reply);
         EXPECT_FALSE(reply.empty());
         realReply.ParseFromString(reply);
         if (realReply.has_completed() && realReply.completed()) {
            break;
         } else {
            EXPECT_TRUE(realReply.result() == "Command running");
         }
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (!zctx_interrupted && count++ < 100);
      EXPECT_TRUE(realReply.result() == "TestCommand");
      EXPECT_TRUE(realReply.success());
   }
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_TRUE(realReply.success());
   EXPECT_TRUE(realReply.result() == "Result Already Sent");
   std::this_thread::sleep_for(std::chrono::milliseconds(1001));
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_FALSE(realReply.success());
   EXPECT_TRUE(realReply.result() == "Command Not Found");

   raise(SIGTERM);
}

TEST_F(CommandProcessorTests, CommandStatusFailureTests) {
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   std::string reply;
   protoMsg::CommandReply realReply;
   protoMsg::CommandRequest requestMsg;

   requestMsg.set_type(::protoMsg::CommandRequest_CommandType_COMMAND_STATUS);
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_FALSE(realReply.success());
   EXPECT_TRUE("Invalid Status Request, No ID" == realReply.result());
   requestMsg.set_stringargone("abc123");
   requestMsg.set_async(true);
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_FALSE(realReply.success());
   EXPECT_TRUE("Invalid Status Request, Cannot Process Asynchronously" == realReply.result());
   requestMsg.set_async(false);
   sender.Swing(requestMsg.SerializeAsString());
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   realReply.ParseFromString(reply);
   EXPECT_FALSE(realReply.success());
   EXPECT_TRUE("Command Not Found" == realReply.result());

   raise(SIGTERM);
}
#endif

TEST_F(CommandProcessorTests, ConstructAndInitializeFail) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mCommandQueue = "invalid";
   CommandProcessor testProcessor(conf);
   EXPECT_FALSE(testProcessor.Initialize());
   protoMsg::CommandRequest requestMsg;

#endif
}

TEST_F(CommandProcessorTests, ConstructAndInitialize) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   CommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   raise(SIGTERM);
#endif
}

TEST_F(CommandProcessorTests, ConstructAndInitializeCheckRegistrations) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   EXPECT_EQ(UpgradeCommand::Construct, testProcessor.CheckRegistration(protoMsg::CommandRequest_CommandType_UPGRADE));
   EXPECT_EQ(RestartSyslogCommand::Construct, testProcessor.CheckRegistration(protoMsg::CommandRequest_CommandType_SYSLOG_RESTART));
   EXPECT_EQ(RebootCommand::Construct, testProcessor.CheckRegistration(protoMsg::CommandRequest_CommandType_REBOOT));
   EXPECT_EQ(NetworkConfigCommand::Construct, testProcessor.CheckRegistration(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG));
   EXPECT_EQ(NtpConfigCommand::Construct, testProcessor.CheckRegistration(protoMsg::CommandRequest_CommandType_NTP_CONFIG));
   EXPECT_EQ(ShutdownCommand::Construct, testProcessor.CheckRegistration(protoMsg::CommandRequest_CommandType_SHUTDOWN));
   EXPECT_EQ(ConfigRequestCommand::Construct, testProcessor.CheckRegistration(protoMsg::CommandRequest_CommandType_CONFIG_REQUEST));
   raise(SIGTERM);
#endif
}

TEST_F(CommandProcessorTests, InvalidCommandSendReceive) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   CommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   std::this_thread::sleep_for(std::chrono::milliseconds(1000));
   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   std::string requestMsg("ABC123");
   sender.Swing(requestMsg);
   std::string reply;
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   protoMsg::CommandReply replyMsg;
   replyMsg.ParseFromString(reply);
   EXPECT_FALSE(replyMsg.success());
   raise(SIGTERM);
#endif
}

TEST_F(CommandProcessorTests, CommandSendReceive) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   testProcessor.ChangeRegistration(protoMsg::CommandRequest_CommandType_UPGRADE, MockUpgradeCommand::Construct);
   std::this_thread::sleep_for(std::chrono::milliseconds(1000));
   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   protoMsg::CommandRequest requestMsg;
   requestMsg.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   sender.Swing(requestMsg.SerializeAsString());
   std::string reply;
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   protoMsg::CommandReply replyMsg;
   replyMsg.ParseFromString(reply);
   EXPECT_TRUE(replyMsg.success());
   raise(SIGTERM);
#endif
}

//Upgrade commands

TEST_F(CommandProcessorTests, UpgradeCommandInit) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandExecSuccess) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = upg.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
#endif
}

TEST_F(CommandProcessorTests, DynamicUpgradeCommandExecSuccess) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest* upg = new UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = upg->Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
   } catch (...) {
      exception = true;
   }
   delete upg;
   ASSERT_FALSE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailReturnCodeCreatePassPhrase) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.CreatePassPhraseFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif 
}

TEST_F(CommandProcessorTests, UpgradeCommandFailSuccessCreatePassPhrase) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.CreatePassPhraseFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailReturnCodeDecryptFile) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.DecryptFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailSuccessDecryptFile) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.DecryptFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailReturnCodeRenameDecryptedFile) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.RenameDecryptedFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailSuccessRenameDecryptedFile) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.RenameDecryptedFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailReturnCodeUntarFile) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.UntarFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailSuccessUntarFile) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.UntarFile();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailReturnRunUpgradeScript) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.RunUpgradeScript();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailSuccessRunUpgradeScript) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.RunUpgradeScript();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailReturnCleanUploadDir) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.CleanUploadDir();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailSuccessCleanUploadDir) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      upg.CleanUploadDir();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, UpgradeCommandFailInitProcessManager) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->setInit(false);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_UPGRADE);
   cmd.set_stringargone("filename");
   UpgradeCommandTest upg = UpgradeCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = upg.Execute(conf);
      ASSERT_FALSE(reply.success());

   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_FALSE(exception);
#endif
}

//REBOOT COMMANDS

TEST_F(CommandProcessorTests, RebootCommandExecSuccess) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RebootCommandTest reboot(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = reboot.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
      EXPECT_TRUE(processManager->mRunCommand == "/sbin/init");
      EXPECT_TRUE(processManager->mRunArgs == " 6");
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
#endif
}

TEST_F(CommandProcessorTests, ShutdownCommandExecSuccess) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_SHUTDOWN);
   MockShutdownCommand shutdown = MockShutdownCommand(cmd, processManager);
   MockShutdownCommand::callRealShutdownCommand = true;
   bool exception = false;
   try {
      protoMsg::CommandReply reply = shutdown.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
      EXPECT_EQ(processManager->mRunCommand, "/sbin/init");
      EXPECT_EQ(processManager->mRunArgs, " 0");
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
#endif
}

//
//  The Mock Shutdown sets a flag if the DoTheShutdown gets triggered. 
//  As long as the Mock is not tampered with and the ChangeRegistration below is in order
//  this test will NOT shutdown your PC, only fake it. 
//
//  If you tamper with the details mentioned, all bets are OFF!
//

TEST_F(CommandProcessorTests, PseudoShutdown) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mCommandQueue = "tcp://127.0.0.1:";
   conf.mCommandQueue += boost::lexical_cast<std::string>(rand() % 1000 + 20000);
   MockCommandProcessor testProcessor(conf);
   EXPECT_TRUE(testProcessor.Initialize());
   LOG(INFO) << "Executing Real command with real Processor but with Mocked Shutdown function";
   // NEVER CHANGE the LINE below. If it is set to true your PC will shut down
   MockShutdownCommand::callRealShutdownCommand = false;
   MockShutdownCommand::wasShutdownCalled = false;


   testProcessor.ChangeRegistration(protoMsg::CommandRequest_CommandType_SHUTDOWN, MockShutdownCommand::FatalAndDangerousConstruct);
   std::this_thread::sleep_for(std::chrono::milliseconds(1000));
   Crowbar sender(conf.getCommandQueue());
   ASSERT_TRUE(sender.Wield());
   protoMsg::CommandRequest requestMsg;
   requestMsg.set_type(protoMsg::CommandRequest_CommandType_SHUTDOWN);

   protoMsg::ShutdownMsg shutdown;
   shutdown.set_now(true);
   requestMsg.set_stringargone(shutdown.SerializeAsString());
   sender.Swing(requestMsg.SerializeAsString());
   std::string reply;
   sender.BlockForKill(reply);
   EXPECT_FALSE(reply.empty());
   protoMsg::CommandReply replyMsg;
   replyMsg.ParseFromString(reply);
   EXPECT_TRUE(replyMsg.success());
   EXPECT_TRUE(MockShutdownCommand::wasShutdownCalled);
   raise(SIGTERM);
#endif
}



//
//TEST_F(CommandProcessorTests, ShutdownSystem) {
//#ifdef LR_DEBUG
//   protoMsg::CommandRequest cmd;
//   const MockConf conf;
//   cmd.set_type(protoMsg::CommandRequest_CommandType_SHUTDOWN);
//   ProcessManager* manager = new ProcessManager(conf);
//   ASSERT_TRUE(manager->Initialize());
//   MockShutdownCommand command(cmd,manager);
//   command.DoTheShutdown();
//   
//#endif
//}

TEST_F(CommandProcessorTests, RebootCommandFailReturnDoTheUpgrade) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RebootCommandTest reboot(cmd, processManager);
   bool exception = false;
   try {
      reboot.DoTheReboot();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

TEST_F(CommandProcessorTests, RebootCommandFailSuccessDoTheUpgrade) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RebootCommandTest reboot(cmd, processManager);
   bool exception = false;
   try {
      reboot.DoTheReboot();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
#endif
}

// Syslog Restart Commands

TEST_F(CommandProcessorTests, RestartSyslogCommandExecSuccess) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RestartSyslogCommandTest reboot = RestartSyslogCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = reboot.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);

#endif
}

TEST_F(CommandProcessorTests, RestartSyslogCommandExecSuccess_UDP) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mSyslogConfName = "/tmp/test.nm.rsyslog.conf"; // dummy file. won't be created
   conf.mSyslogProtocol = false; // udp
   conf.mSyslogAgentIp = "123.123.123";
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RestartSyslogCommandTest reboot = RestartSyslogCommandTest(cmd, processManager);
   bool exception = false;
   try {
      reboot.UpdateSyslog(conf);
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   auto cmdArgs = processManager->getRunArgs();

   std::string expected = {"-e \"\n\n\\$SystemLogRateLimitInterval 1 \n"};
   expected.append("\\$SystemLogRateLimitBurst 20000 \n\n");
   expected.append("local4.* @123.123.123:1234\" > /tmp/test.nm.rsyslog.conf");
   EXPECT_TRUE(cmdArgs == expected) << "\ncmdArgs:\t" << cmdArgs << "\nexpected:\t" << expected;
#endif
}

TEST_F(CommandProcessorTests, RestartSyslogCommandExecSuccess_TCP) {
#ifdef LR_DEBUG
   MockConf conf;
   conf.mSyslogConfName = "/tmp/test.nm.rsyslog.conf"; // dummy file. won't be created
   conf.mSyslogProtocol = true; // "TCP";
   conf.mSyslogAgentIp = "123.123.123";
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RestartSyslogCommandTest reboot = RestartSyslogCommandTest(cmd, processManager);
   bool exception = false;
   try {
      reboot.UpdateSyslog(conf);
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   auto cmdArgs = processManager->getRunArgs();
   std::string expected = {"-e \"\n\n\\$SystemLogRateLimitInterval 1 \n"};
   expected.append("\\$SystemLogRateLimitBurst 20000 \n\n");
   expected.append("\\$WorkDirectory /var/lib/rsyslog # where to place spool files\n");
   expected.append("\\$ActionQueueType LinkedList   # use asynchronous processing\n");
   expected.append("\\$ActionQueueFileName LR_SIEM  # unique name prefix for spool files\n");
   expected.append("\\$ActionResumeRetryCount -1    # infinite retries if host is down\n");
   expected.append("\\$ActionQueueMaxDiskSpace 1g   # 1gb space limit (use as much as possible)\n");
   expected.append("\\$ActionQueueSaveOnShutdown on # save messages to disk on shutdown\n");
   expected.append("local4.* @@123.123.123:1234\" > /tmp/test.nm.rsyslog.conf");
   EXPECT_TRUE(cmdArgs == expected) << "\ncmdArgs:\t" << cmdArgs << "\nexpected:\t" << expected;
  #endif
}

TEST_F(CommandProcessorTests, RestartSyslogCommandTestFailedUpdate) {
#ifdef LR_DEBUG
   MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RestartSyslogCommandTest reboot = RestartSyslogCommandTest(cmd, processManager);
   bool exception = false;
   try {
      reboot.UpdateSyslog(conf);
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   
#endif
}


TEST_F(CommandProcessorTests, RestartSyslogCommandTestFailRestart) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RestartSyslogCommandTest reboot = RestartSyslogCommandTest(cmd, processManager);
   bool exception = false;
   try {
      reboot.Restart();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);

#endif
}

TEST_F(CommandProcessorTests, RestartSyslogCommandTestFailSuccessRestart) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_REBOOT);
   RestartSyslogCommandTest reboot = RestartSyslogCommandTest(cmd, processManager);
   bool exception = false;
   try {
      reboot.Restart();
   } catch (CommandFailedException e) {
      exception = true;
   }
   ASSERT_TRUE(exception);

#endif
}

// Network Config commands

TEST_F(CommandProcessorTests, NetworkConfigCommandInit) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);

#endif 
}

TEST_F(CommandProcessorTests, NetworkConfigCommandExecSuccess) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::DHCP);
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = ncct.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);

#endif
}

TEST_F(CommandProcessorTests, DynamicNetworkConfigCommandExecSuccess) {
#ifdef LR_DEBUG
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::DHCP);
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest* ncct = new NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = ncct->Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
   } catch (...) {
      exception = true;
   }
   delete ncct;
   ASSERT_FALSE(exception);


}

TEST_F(CommandProcessorTests, NetworkConfigCommandBadInterfaceMsg) {

   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   cmd.set_stringargone("BadInterfaceMsg");
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = ncct.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_FALSE(reply.success());
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);


}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailInitProcessManager) {

   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->setInit(false);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::DHCP);
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = ncct.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_FALSE(reply.success());
   } catch (...) {
      exception = true;
   }

   ASSERT_FALSE(exception);


}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailInterfaceMethodNotSet) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = ncct.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_FALSE(reply.success());
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);

}

TEST_F(CommandProcessorTests, NetworkConfigCommandSetStaticIpSuccess) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("Success!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_ipaddress("192.168.1.1");
   interfaceConfig.set_netmask("255.255.255.0");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      protoMsg::CommandReply reply = ncct.Execute(conf);
      LOG(DEBUG) << "Success: " << reply.success() << " result: " << reply.result();
      ASSERT_TRUE(reply.success());
   } catch (...) {
      exception = true;
   }

   ASSERT_FALSE(exception);
}

TEST_F(CommandProcessorTests, NetworkConfigFailIfcfgInterfaceAllowedEth1) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("eth1");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   ncct.setStatFail();
   bool exception = false;
   try {
      ncct.IfcfgInterfaceAllowed();
   } catch (...) {
      exception = true;
   }

   ASSERT_TRUE(exception);
}

TEST_F(CommandProcessorTests, NetworkConfigFailIfcfgInterfaceAllowedEm2) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("em2");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   ncct.setStatFail();
   bool exception = false;
   try {
      ncct.IfcfgInterfaceAllowed();
   } catch (...) {
      exception = true;
   }

   ASSERT_TRUE(exception);
}

TEST_F(CommandProcessorTests, NetworkConfigFailIfcfgInterfaceAllowedEth2) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("eth2");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   ncct.setStatFail();
   bool exception = false;
   try {
      ncct.IfcfgInterfaceAllowed();
   } catch (...) {
      exception = true;
   }

   ASSERT_TRUE(exception);
}

TEST_F(CommandProcessorTests, NetworkConfigFailIfcfgInterfaceAllowedEm3) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("em3");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   ncct.setStatFail();
   bool exception = false;
   try {
      ncct.IfcfgInterfaceAllowed();
   } catch (...) {
      exception = true;
   }

   ASSERT_TRUE(exception);
}

TEST_F(CommandProcessorTests, NetworkConfigFailIfcfgFileExists) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("NoIface");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   ncct.setStatFail();
   bool exception = false;
   try {
      ncct.IfcfgFileExists();
   } catch (...) {
      exception = true;
   }

   ASSERT_TRUE(exception);
}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeBackupIfcfgFile) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("NoIface");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.BackupIfcfgFile();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/cat", processManager->getRunCommand());
   ASSERT_EQ("\"/etc/sysconfig/network-scripts/ifcfg-NoIface\" > "
           "\"/etc/sysconfig/network-scripts/bkup-ifcfg-NoIface\"",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessBackupIfcfgFile) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("NoIface");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.BackupIfcfgFile();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/cat", processManager->getRunCommand());
   ASSERT_EQ("\"/etc/sysconfig/network-scripts/ifcfg-NoIface\" > "
           "\"/etc/sysconfig/network-scripts/bkup-ifcfg-NoIface\"",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeRestoreIfcfgFile) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("NoIface");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.RestoreIfcfgFile();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/cat", processManager->getRunCommand());
   ASSERT_EQ("\"/etc/sysconfig/network-scripts/bkup-ifcfg-NoIface\" > "
           "\"/etc/sysconfig/network-scripts/ifcfg-NoIface\"",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessRestoreIfcfgFile) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("NoIface");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.RestoreIfcfgFile();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/cat", processManager->getRunCommand());
   ASSERT_EQ("\"/etc/sysconfig/network-scripts/bkup-ifcfg-NoIface\" > "
           "\"/etc/sysconfig/network-scripts/ifcfg-NoIface\"",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeResetIfcfgFile) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("NoIface");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.ResetIfcfgFile();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/usr/bin/perl", processManager->getRunCommand());
   ASSERT_EQ("-ni -e 'print unless /BOOTPROTO|IPADDR|NETMASK|GATEWAY|NETWORK|"
           "NM_CONTROLLED|ONBOOT|DNS1|DNS2|PEERDNS|DOMAIN|BOARDCAST/i' "
           "\"/etc/sysconfig/network-scripts/ifcfg-NoIface\"",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessResetIfcfgFile) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("NoIface");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.ResetIfcfgFile();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/usr/bin/perl", processManager->getRunCommand());
   ASSERT_EQ("-ni -e 'print unless /BOOTPROTO|IPADDR|NETMASK|GATEWAY|NETWORK|"
           "NM_CONTROLLED|ONBOOT|DNS1|DNS2|PEERDNS|DOMAIN|BOARDCAST/i' "
           "\"/etc/sysconfig/network-scripts/ifcfg-NoIface\"",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddBootProto) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::DHCP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddBootProto("dhcp");
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"BOOTPROTO=dhcp\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddBootProto) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddBootProto("none");
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"BOOTPROTO=none\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailAddIpAddrNotDefined) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddIpAddr();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailAddIpAddrEmptyString) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_ipaddress("");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddIpAddr();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddIpAddr) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_ipaddress("192.168.1.1");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddIpAddr();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"IPADDR=192.168.1.1\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddIpAddr) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_ipaddress("192.168.1.1");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddIpAddr();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"IPADDR=192.168.1.1\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandAddNetmaskNotDefined) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddNetmask();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandAddNetmaskEmptyString) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_netmask("");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddNetmask();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddNetmask) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_netmask("255.255.255.0");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddNetmask();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"NETMASK=255.255.255.0\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddNetmask) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_netmask("255.255.255.0");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddNetmask();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"NETMASK=255.255.255.0\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailAddGatewayNotDefined) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddGateway();
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddGateway) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_gateway("192.168.1.100");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddGateway();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"GATEWAY=192.168.1.100\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddGateway) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_gateway("192.168.1.100");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddGateway();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"GATEWAY=192.168.1.100\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandAddGatewayEmptyString) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_gateway("");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddGateway();
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddDnsServers) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("192.168.1.10");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDnsServers();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DNS1=192.168.1.10\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddDnsServers) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("192.168.1.10,192.168.1.11");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDnsServers();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DNS1=192.168.1.10\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandAddDnsServersEmptyString) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDnsServers();
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddDns1) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("192.168.1.10,192.168.1.11");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDns1();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DNS1=192.168.1.10\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddDns1) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("192.168.1.10");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDns1();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DNS1=192.168.1.10\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddDns2) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("192.168.1.10,192.168.1.11");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDns2();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DNS2=192.168.1.11\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddDns2) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("192.168.1.10,192.168.1.11");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDns2();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DNS2=192.168.1.11\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddDomain) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_searchdomains("schq.secious.com");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDomain();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DOMAIN=schq.secious.com\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddDomain) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_searchdomains("schq.secious.com");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDomain();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"DOMAIN=schq.secious.com\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandAddDomainEmptyString) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_searchdomains("");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddDomain();
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
   ASSERT_EQ("", processManager->getRunCommand());
   ASSERT_EQ("", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandIgnoreReturnCodeInterfaceDown) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.InterfaceDown();
   } catch (...) {
      exception = true;
   }
   ASSERT_FALSE(exception);
   ASSERT_EQ("/sbin/ifdown", processManager->getRunCommand());
   ASSERT_EQ("ethx boot --force", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandIgnoreSuccessInterfaceDown) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.InterfaceDown();
   } catch (...) {
      exception = true;
   }
   EXPECT_EQ(processManager->mCountNumberOfRuns, 3); // 3x ifup
   ASSERT_FALSE(exception);
   ASSERT_EQ("/sbin/ifdown", processManager->getRunCommand());
   ASSERT_EQ("ethx boot --force", processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandIgnoreReturnCodeInterfaceUp) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.InterfaceUp();
   } catch (...) {
      exception = true;
   }
   EXPECT_EQ(processManager->mCountNumberOfRuns, 3); // 3x  ifup
   ASSERT_FALSE(exception);
   ASSERT_EQ("/sbin/ifup", processManager->getRunCommand());
   ASSERT_EQ("ethx boot --force", processManager->getRunArgs());
}

TEST_F(CommandProcessorTests, NetworkConfigCommandIgnoreSuccessInterfaceUp) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.InterfaceUp();
   } catch (...) {
      exception = true;
   }
   EXPECT_EQ(processManager->mCountNumberOfRuns, 3); // 3x ifup
   ASSERT_FALSE(exception);
   ASSERT_EQ("/sbin/ifup", processManager->getRunCommand());
   ASSERT_EQ("ethx boot --force", processManager->getRunArgs());
}

TEST_F(CommandProcessorTests, NetworkConfigCommandStaticNoExtraRetriesOnSuccessfulInterfaceUp) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("eth0");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.InterfaceUp();
   } catch (...) {
      exception = true;
   }
   EXPECT_EQ(processManager->mCountNumberOfRuns, 1); // 1 ifup
   ASSERT_FALSE(exception);
   ASSERT_EQ("/sbin/ifup", processManager->getRunCommand());
   ASSERT_EQ("eth0 boot --force", processManager->getRunArgs());
}

TEST_F(CommandProcessorTests, NetworkConfigCommandDhcpNoExtraRetriesOnSuccessfulInterfaceUp) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(0);
   processManager->SetResult("");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::DHCP);
   interfaceConfig.set_interface("eth0");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.InterfaceUp();
   } catch (...) {
      exception = true;
   }
   EXPECT_EQ(processManager->mCountNumberOfRuns, 1); // 1 ifup
   ASSERT_FALSE(exception);
   ASSERT_EQ("/sbin/ifup", processManager->getRunCommand());
   ASSERT_EQ("eth0 boot --force", processManager->getRunArgs());
}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddOnBoot) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddOnBoot();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"ONBOOT=yes\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddOnBoot) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddOnBoot();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"ONBOOT=yes\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddNmControlled) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddNmControlled();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"NM_CONTROLLED=no\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddNmControlled) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddNmControlled();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"NM_CONTROLLED=no\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailReturnCodeAddPeerDns) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   // No DNS Servers or Search Domains set, which causes PEERDNS=no on output
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddPeerDns();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"PEERDNS=no\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandDnsServerEmptyStringSearchDomainNo) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("");
   // DNS Server is empty string and no Search Domains set, which causes PEERDNS=no on output
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddPeerDns();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"PEERDNS=no\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandDnsServerNoSearchDomainEmptyString) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_searchdomains("");
   // No DNS Server and Search Domains is empty string, which causes PEERDNS=no on output
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddPeerDns();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"PEERDNS=no\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandDnsServerSearchDomainEmptyStrings) {
   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(true);
   processManager->SetReturnCode(1);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   interfaceConfig.set_dnsservers("");
   interfaceConfig.set_searchdomains("");
   // DNS Server and Search Domains are both empty strings, which causes PEERDNS=no on output
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddPeerDns();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"PEERDNS=no\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

}

TEST_F(CommandProcessorTests, NetworkConfigCommandFailSuccessAddPeerDns) {

   const MockConf conf;
   MockProcessManagerCommand* processManager = new MockProcessManagerCommand(conf);
   processManager->SetSuccess(false);
   processManager->SetReturnCode(0);
   processManager->SetResult("Failed!");
   protoMsg::CommandRequest cmd;
   cmd.set_type(protoMsg::CommandRequest_CommandType_NETWORK_CONFIG);
   protoMsg::NetInterface interfaceConfig;
   interfaceConfig.set_method(protoMsg::STATICIP);
   interfaceConfig.set_interface("ethx");
   // Set a DNS Servers, which causes PEERDNS=yes on output
   interfaceConfig.set_dnsservers("192.168.1.10,192.168.1.11");
   interfaceConfig.set_searchdomains("");
   cmd.set_stringargone(interfaceConfig.SerializeAsString());
   NetworkConfigCommandTest ncct = NetworkConfigCommandTest(cmd, processManager);
   bool exception = false;
   try {
      ncct.AddPeerDns();
   } catch (...) {
      exception = true;
   }
   ASSERT_TRUE(exception);
   ASSERT_EQ("/bin/echo", processManager->getRunCommand());
   ASSERT_EQ("\"PEERDNS=yes\" >> /etc/sysconfig/network-scripts/ifcfg-ethx",
           processManager->getRunArgs());

#endif
}

