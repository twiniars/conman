/** Copyright (c) 2013, Jonathan Bohren, all rights reserved.
 * * This software is released under the BSD 3-clause license, for the details of
 * * this license, please see LICENSE.txt at the root of this repository.
 * */

#include <string>
#include <vector>
#include <iterator>

#include <rtt/os/startstop.h>

#include <ocl/DeploymentComponent.hpp>
#include <ocl/TaskBrowser.hpp>
#include <ocl/LoggingService.hpp>
#include <rtt/Logger.hpp>
#include <rtt/deployment/ComponentLoader.hpp>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>

#include <conman/conman.h>
#include <conman/scheme.h>
#include <conman/hook.h>

#include <boost/assign/std/vector.hpp>
using namespace boost::assign;

#include <gtest/gtest.h>
#include <gmock/gmock.h>
using ::testing::ElementsAre;

std::vector<std::string> enable_Order;
std::vector<std::string> disable_Order;

class InvalidBlock : public RTT::TaskContext {
public:
  InvalidBlock(const std::string &name) : RTT::TaskContext(name) { }
};

class ValidBlock : public RTT::TaskContext {
public:
  ValidBlock(const std::string &name) : RTT::TaskContext(name) {
    conman_hook_ = conman::Hook::GetHook(this);
  }
  boost::shared_ptr<conman::Hook> conman_hook_;
};

class IOBlock : public RTT::TaskContext {
public:
  RTT::InputPort<double> in;
  RTT::InputPort<double> in_ex;

  RTT::OutputPort<double> out1;
  RTT::OutputPort<double> out2;

  IOBlock(const std::string &name) : RTT::TaskContext(name) {
    this->addPort("in",in);
    this->addPort("in_ex",in_ex);

    this->addPort("out1",out1);
    this->addPort("out2",out2);

    conman_hook_ = conman::Hook::GetHook(this);
    conman_hook_->setInputExclusivity("in_ex",conman::Exclusivity::EXCLUSIVE);
  }
  bool startHook() {
    enable_Order += this->getName();
    return true;
  }
  void stopHook() {
    disable_Order += this->getName();
  }
  boost::shared_ptr<conman::Hook> conman_hook_;
};

class SchemeTest : public ::testing::Test {
protected:
  SchemeTest() : scheme("Scheme") { }

  conman::Scheme scheme;
};

class TopoTest : public SchemeTest {
public:
  IOBlock iob1;
  IOBlock iob2;
  IOBlock iob3;
  IOBlock iob4;
  IOBlock iob5;
  
  // Expected cycles
  std::vector<std::string> c1;

  TopoTest() : SchemeTest(),
    iob1("iob1"),
    iob2("iob2"),
    iob3("iob3"),
    iob4("iob4"),
    iob5("iob5")
  {
    // Expected cycle
    c1 += "iob1", "iob2", "iob3", "iob4", "iob5";
  }

  void AddBlocks() {
    scheme.addBlock(&iob1);
    scheme.addBlock(&iob2);
    scheme.addBlock(&iob3);
    scheme.addBlock(&iob4);
    scheme.addBlock(&iob5);
  }

  void ConnectBlocksAcyclic() {
    iob1.out1.connectTo(&iob2.in);
    iob2.out2.connectTo(&iob3.in);
    iob3.out1.connectTo(&iob4.in);
    iob4.out1.connectTo(&iob5.in);
  }

  void ConnectBlocksCyclic() {
    iob5.out1.connectTo(&iob1.in);
  }

  void PrintCycles(std::vector<std::vector<std::string> > &cycles) {
    std::cerr<<"cycles: "<<std::endl;
    for(size_t i=0; i<cycles.size(); i++) {
      std::cerr<<" [";
      for(size_t v=0; v < cycles[i].size(); v++) {
        std::cerr<<" "<<cycles[i][v];
      }
      std::cerr<<" ]"<<std::endl;
    }
  }
};

TEST_F(TopoTest, EnableOrder) {
  //setup blocks, connected 1 -> 2 -> 3 -> 4 -> 5 -latched-> 1
  ConnectBlocksAcyclic();
  ConnectBlocksCyclic();
  AddBlocks();
  scheme.latchConnections("iob5","iob1",true);
  std::vector<std::string> execution_order;
  scheme.getExecutionOrder(execution_order);
  
  scheme.start();
  std::vector<std::string> &ptr_blocks = execution_order;

  EXPECT_TRUE(scheme.enableBlocks(ptr_blocks, true, true));
  EXPECT_THAT(enable_Order, ElementsAre("iob1", "iob2", "iob3", "iob4", "iob5"));

  EXPECT_TRUE(scheme.disableBlocks(ptr_blocks, true));
  scheme.stop();
  enable_Order.clear();
  disable_Order.clear();
}

TEST_F(TopoTest, DisableOrder) {
  //setup blocks, connected 1 -> 2 -> 3 -> 4 -> 5 -latched-> 1
  ConnectBlocksAcyclic();
  ConnectBlocksCyclic();
  AddBlocks();
  scheme.latchConnections("iob5","iob1",true);
  std::vector<std::string> execution_order;
  scheme.getExecutionOrder(execution_order);
  
  scheme.start();
  std::vector<std::string> &ptr_blocks = execution_order;

  EXPECT_TRUE(scheme.enableBlocks(ptr_blocks, true, true));

  EXPECT_TRUE(scheme.disableBlocks(ptr_blocks, true));
  EXPECT_THAT(disable_Order, ElementsAre("iob1", "iob2", "iob3", "iob4", "iob5"));

  scheme.stop();
  enable_Order.clear();
  disable_Order.clear();
}

TEST_F(TopoTest, TopoEnable) {
  //setup blocks, connected 1 -> 2 -> 3 -> 4 -> 5 -latched-> 1
  ConnectBlocksAcyclic();
  ConnectBlocksCyclic();
  AddBlocks();
  scheme.latchConnections("iob5","iob1",true);
  std::vector<std::string> execution_order;
  scheme.getExecutionOrder(execution_order);
   
  scheme.start();
  std::vector<std::string> &ptr_blocks = execution_order;

  EXPECT_TRUE(scheme.enableBlocksTopo(ptr_blocks, true, true));
  EXPECT_THAT(enable_Order, ElementsAre("iob1", "iob2", "iob3", "iob4", "iob5"));

  EXPECT_TRUE(scheme.disableBlocks(ptr_blocks, true));

  scheme.stop();
  enable_Order.clear();
  disable_Order.clear();
}

TEST_F(TopoTest, TopoEnableRand) {
  //setup blocks, connected 1 -> 2 -> 3 -> 4 -> 5 -latched-> 1
  ConnectBlocksAcyclic();
  ConnectBlocksCyclic();
  AddBlocks();
  scheme.latchConnections("iob5","iob1",true);
  std::vector<std::string> execution_order;
  scheme.getExecutionOrder(execution_order);

  scheme.start();

  std::vector<std::string> random_order;
  random_order += "io4", "io1", "io5", "io3", "io2";
  std::vector<std::string> &ptr_blocks = random_order; 

  EXPECT_TRUE(scheme.enableBlocksTopo(ptr_blocks, true, true));
  //EXPECT_THAT(enable_Order, ElementsAre("iob1", "iob2", "iob3", "iob4", "iob5"));
  
  //EXPECT_TRUE(scheme.disableBlocks(ptr_blocks, true));

  scheme.stop();
  enable_Order.clear();
  disable_Order.clear();
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  // Initialize Orocos
  __os_init(argc, argv);

  RTT::Logger::log().setStdStream(std::cerr);
  RTT::Logger::log().mayLogStdOut(true);
  //RTT::Logger::log().setLogLevel(RTT::Logger::Info);

  // Import conman plugin
  RTT::ComponentLoader::Instance()->import("conman", "" );
  
  return RUN_ALL_TESTS();
}
