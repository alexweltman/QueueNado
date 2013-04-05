/* 
 * File:   ConfProcessorTests.h
 * Author: Ben Aldrich
 *
 * Created on September 28, 2012, 3:53 PM
 */

#pragma once

#include "gtest/gtest.h"
#include "ConfProcessor.h"
#include "boost/lexical_cast.hpp"
#include <csignal>

class ConfProcessorTests : public ::testing::Test {
public:

    ConfProcessorTests() {
    };
protected:
    std::string mWriteLocation;
    std::string mTestConf;

    virtual void SetUp() {
        int pid = getpid();
        mWriteLocation = "/tmp/test.yaml.";
        mWriteLocation += boost::lexical_cast<std::string > (pid);
        remove(mWriteLocation.c_str());
        mTestConf = "resources/test.yaml";

    };

    virtual void TearDown() {
       std::string syslogWriteLocation = mWriteLocation;
       syslogWriteLocation += ".Syslog";
       std::string qosmosWriteLocation = mWriteLocation;
       qosmosWriteLocation == ".Qosmos";
       remove(mWriteLocation.c_str());
       remove(syslogWriteLocation.c_str());
       remove(qosmosWriteLocation.c_str());
    };
    
    void StartTimedSection(const double expectedTimePerTransaction,const unsigned int totalTransactions) {
      t_expectedTime = expectedTimePerTransaction*totalTransactions * 1000000.0;
      t_totalTransactions = totalTransactions;
      gettimeofday(&t_startTime, NULL);
   }

   void EndTimedSection() {
      gettimeofday(&t_endTime, NULL);
      std::cout << "Expected Time: " << t_expectedTime / 1000000L << std::endl;
      t_elapsedUS = (t_endTime.tv_sec - t_startTime.tv_sec)*1000000L + (t_endTime.tv_usec - t_startTime.tv_usec);
      std::cout << std::dec << "Elapsed Time :" << t_elapsedUS / 1000000L << "." << std::setfill('0') << std::setw(6) << t_elapsedUS % 1000000 << "s" << std::endl;
      double totalTransactionsPS = (t_totalTransactions * 1.0) / (t_elapsedUS * 1.0 / 1000000.0);
      std::cout << "Transactions/second :" << totalTransactionsPS << std::endl;
      
   }

   bool TimedSectionPassed() {
      return (t_elapsedUS < t_expectedTime);
   }
   double t_expectedTime;
   time_t t_elapsedUS;
   timeval t_startTime;
   size_t t_packetSize;
   timeval t_endTime;
   unsigned int t_totalTransactions;
private:

};

