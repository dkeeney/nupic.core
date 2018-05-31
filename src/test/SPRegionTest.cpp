/* ---------------------------------------------------------------------
 * Numenta Platform for Intelligent Computing (NuPIC)
 * Copyright (C) 2018, Numenta, Inc.  Unless you have an agreement
 * with Numenta, Inc., for a separate license for this software code, the
 * following terms and conditions apply:
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Affero Public License for more details.
 *
 * You should have received a copy of the GNU Affero Public License
 * along with this program.  If not, see http://www.gnu.org/licenses.
 *
 * http://numenta.org/licenses/
 
 * Author: David Keeney, April, 2018
 * ---------------------------------------------------------------------
 */
 
/*---------------------------------------------------------------------
  * This is a test for memory leaks using the Network API
  *---------------------------------------------------------------------
  */


#include <nupic/engine/NuPIC.hpp>
#include <nupic/engine/Network.hpp>
#include <nupic/engine/Region.hpp>
#include <nupic/engine/Spec.hpp>
#include <nupic/engine/Input.hpp>
#include <nupic/engine/Output.hpp>
#include <nupic/engine/Link.hpp>
#include <nupic/engine/RegisteredRegionImpl.hpp>
#include <nupic/ntypes/Dimensions.hpp>
#include <nupic/ntypes/Array.hpp>
#include <nupic/ntypes/ArrayRef.hpp>
#include <nupic/types/Exception.hpp>
#include <nupic/os/OS.hpp> // memory leak detection
#include <nupic/os/Env.hpp>
#include <nupic/os/Path.hpp>
#include <nupic/os/Timer.hpp>
#include <nupic/engine/YAMLUtils.hpp>
#include <nupic/regions/SPRegion.hpp>


#include <string>
#include <vector>
#include <cmath> // fabs/abs
#include <cstdlib> // exit
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <streambuf>

#include "yaml-cpp/yaml.h"

bool ignore_negative_tests = false;
#define SHOULDFAIL(statement) \
  { \
    if (!ignore_negative_tests) \
    { \
      bool caughtException = false; \
      try { \
        statement; \
      } catch(std::exception& ) { \
        caughtException = true; \
        if(verbose) std::cout << "Caught exception as expected: " # statement "" << std::endl;  \
      } \
      if (!caughtException) { \
        NTA_THROW << "Operation '" #statement "' did not fail as expected"; \
        fail++; \
      } \
    } \
  }

// The following string should contain a valid expected Spec - manually verified. 
#define EXPECTED_SPEC_COUNT  32  // The number of parameters expected in the SPRegion Spec
#define EXPECTED_SPEC " -- need to fill this in -- ";


using namespace nupic;

class MemoryMonitor;

/************* MemoryLeakTest Class *****************************/
class MemoryLeakTest
{
public:
  MemoryLeakTest() {
    verbose = false;
    fail = false;
    Real64 sensedValues[] = { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 0.0 };
    for (int i = 0; i < 10; i++) { sensedValues_[i] = sensedValues[i]; }
    iter_ = 0;
  }
  ~MemoryLeakTest() {}

  int realmain(bool leakTest);

  void testSpecAndParameters(Region_Ptr_t region);
  void testInputOutputAccess(Region_Ptr_t region);
  void testCppLinking(std::string linkPolicy, std::string linkParams);
  void testSerialization();
  void testRun();

  // some utilities
  bool compareRegionArrays(Region_Ptr_t region1, Region_Ptr_t region2, std::string parameter, NTA_BasicType type);

  // verbose == true turns on extra output that is useful for
  // debugging the test (e.g. when the SPRegion::compute() algorithm changes)
  bool verbose;
  int fail;
  YAML::Node modelParams;
  Real64 sensedValues_[10];
  int iter_;

};



// Call this after SPRegion initialization with all default parameters.
void MemoryLeakTest::testSpecAndParameters(Region_Ptr_t region)
{

  // check spec cache
  const Spec* ns = region->getSpec();
  if (ns != region->getSpec()) {
    std::cout << "Failure: First and second fetches of the spec gives different addresses. It was not cached." << "" << std::endl;
    fail++;
  }

  // check Spec content
  std::string specStr = EXPECTED_SPEC;
  if (specStr != ns->toString()) {
    std::cout << "Failure: spec not as expcted." << std::endl;
    std::cout << "Expected Spec is:\n" << specStr << "" << std::endl;
    std::cout << "Actual Spec is:\n" << ns->toString() << "" << std::endl;
    fail++;
  }
  else {
    if (verbose) std::cout << "Spec is:\n" << ns->toString() << "" << std::endl;
  }

  // Make sure the number of parameters matches.
  size_t expectedSpecCount = EXPECTED_SPEC_COUNT;
  size_t specCount = ns->parameters.getCount();
  if (specCount != expectedSpecCount) {
    std::cout << "Failure: Unexpected number of parameters. Expected=" << expectedSpecCount << ", Actual=" << specCount << "" << std::endl;
    fail++;
  }

  // Look through the Spec and look for every instance of a parameter.  get/set/verify each parameter
  for (size_t i = 0; i < specCount; i++) {
    std::pair<string, ParameterSpec> p = ns->parameters.getByIndex(i);
    string name = p.first;

    if(verbose)std::cout << "Parameter \"" << name << "\"" << std::endl;
    try {
      if (p.second.count == 1) {
        switch (p.second.dataType) {
        case NTA_BasicType_UInt32:
        {
          SHOULDFAIL(region->getParameterInt32(name));

          // check the getter.
          UInt32 v = region->getParameterUInt32(name);
          UInt32 d = std::stoul(p.second.defaultValue, nullptr, 0);
          if (v != d) {
            std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match default. Expected=" << d << ", Actual=" << v << "" << std::endl;
            fail++;
          }

          // check the setter.
          if (p.second.accessMode == ParameterSpec::ReadWriteAccess) {
            region->setParameterUInt32(name, 0xFFFFFFFF);  // set max value
            v = region->getParameterUInt32(name);
            if (v != 0xFFFFFFFF) {
              std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match UInt32 max. Expected=" << 0xFFFFFFFF << ", Actual=" << v << "" << std::endl;
              fail++;
            }
            region->setParameterUInt32(name, d);  // return to default value.
          }
          break;
        }

        case NTA_BasicType_Int32:
        {
          SHOULDFAIL(region->getParameterUInt32(name));

          // check the getter.
          UInt32 v = region->getParameterInt32(name);
          UInt32 d = std::stoul(p.second.defaultValue, nullptr, 0);
          if (v != d) {
            std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match default. Expected=" << d << ", Actual=" << v << "" << std::endl;
            fail++;
          }

          // check the setter.
          if (p.second.accessMode == ParameterSpec::ReadWriteAccess) {
            region->setParameterInt32(name, 0x7FFFFFFF);  // set max value
            v = region->getParameterInt32(name);
            if (v != 0x7FFFFFFF) {
              std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match Int32 max. Expected=" << 0x7FFFFFFF << ", Actual=" << v << "" << std::endl;
              fail++;
            }
            region->setParameterInt32(name, d);  // return to default value.
          }
          break;
        }

        case NTA_BasicType_Real32:
        {
          SHOULDFAIL(region->getParameterUInt32(name));

          // check the getter.
          Real32 v = region->getParameterReal32(name);
          Real32 d = std::strtof(p.second.defaultValue.c_str(), nullptr);
          if (v != d) {
            std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match default. Expected=" << d << ", Actual=" << v << "" << std::endl;
            fail++;
          }

          // check the setter.
          if (p.second.accessMode == ParameterSpec::ReadWriteAccess) {
            region->setParameterReal32(name, FLT_MAX);  // set max value
            v = region->getParameterReal32(name);
            if (v != FLT_MAX) {
              std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match float max. Expected=" << FLT_MAX << ", Actual=" << v << "" << std::endl;
              fail++;
            }
            region->setParameterReal32(name, d);  // return to default value.
          }
          break;
        }

        case NTA_BasicType_Bool:
        {
          SHOULDFAIL(region->getParameterUInt32(name));

          // check the getter.
          bool v = region->getParameterBool(name);
          bool d = std::strtol(p.second.defaultValue.c_str(), nullptr, 0);
          if (v != d) {
            std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match default. Expected=" << d << ", Actual=" << v << "" << std::endl;
            fail++;
          }

          // check the setter.
          if (p.second.accessMode == ParameterSpec::ReadWriteAccess) {
            region->setParameterBool(name, !d);  // change value
            v = region->getParameterBool(name);
            if (v != !d) {
              std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match changed value. Expected=" << (!d) << ", Actual=" << v << "" << std::endl;
              fail++;
            }
            region->setParameterBool(name, d);  // return to default value.
          }
          break;
        }

        default:
          std::cout << "Failure: Parameter \"" << name << "\" Invalid data type.  found " << p.second.dataType << "" << std::endl;
          fail++;
          break;
        } // end switch
      }
      else {



        // Array types
        switch (p.second.dataType) {
        case NTA_BasicType_Byte:
        {
          std::string v = region->getParameterString(name);
          std::string d = p.second.defaultValue;
          if (v != d) {
            std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match default. Expected=" << d << ", Actual=" << v << "" << std::endl;
            fail++;
          }

          // check the setter.
          if (p.second.accessMode == ParameterSpec::ReadWriteAccess) {
            region->setParameterString(name, "XXX");  // change value
            v = region->getParameterString(name);
            if (v != "XXX") {
              std::cout << "Failure: Parameter \"" << name << "\" Actual value does not match changed value. Expected=" << "XXX" << ", Actual=" << v << "" << std::endl;
              fail++;
            }
            region->setParameterString(name, d);  // return to default value.
          }
          break;

        }

        default:
          std::cout << "Failure: Parameter \"" << name << "\" Invalid data type.  found " << p.second.dataType << "" << std::endl;
          fail++;
          break;
        } // end switch
      }
    }
    catch (nupic::Exception& ex) {
      std::cout << "Failure: Exception while processing parameter " << name << ":  " << ex.getFilename() << "(" << ex.getLineNumber() << ") " << ex.getMessage() << "" << std::endl;

      fail++;
    }
    catch (std::exception& e) {
      std::cout << "Failure: Exception while processing parameter " << name << ":  " << e.what() << "" << std::endl;

      fail++;
    }
  }
}

// Make sure we can access the Inputs and Outputs
// Note: this does not confirm if the right output is given
//       for a specific input.  That should be handled in
//       the SpatialPoller algorithm unit test.
void MemoryLeakTest::testInputOutputAccess(Region_Ptr_t region)
{

  SHOULDFAIL(region->getOutputData("doesnotexist"));
  SHOULDFAIL(region->getInputData("doesnotexist"));

  // --- input/output access for region (C++ SPRegion) ---
  const Spec* ns = region->getSpec();
  if (ns->outputs.getByName("bottomUpOut").dataType != NTA_BasicType_Real32) {
    std::cout << "Failure: Output type for \"bottomUpOut\" Invalid data type. Should be Real32, found " << ns->outputs.getByName("bottomUpOut").dataType << "" << std::endl;
    fail++;
  }
  if (ns->inputs.getByName("bottomUpIn").dataType != NTA_BasicType_Real32) {
    std::cout << "Failure: Input type for \"bottomUpIn\" Invalid data type. Should be Real32, found " << ns->inputs.getByName("bottomUpIn").dataType << "" << std::endl;
    fail++;
  }

  ArrayRef output = region->getOutputData("bottomUpOut");
  if (verbose) std::cout << "Element count in bottomUpOut is " << output.getCount() << "" << std::endl;
  Real32 *data_actualOut = (Real32*)output.getBuffer();
  ArrayRef input = region->getInputData("bottomUpIn");
  if (verbose) std::cout << "Element count in bottomUpIn is " << input.getCount() << "" << std::endl;
  Real32 *data_actualIn = (Real32*)input.getBuffer();

  if (input.getCount() == 0 || data_actualIn == nullptr) {
    std::cout << "Failure: butomUpIn array buffer not valid. "  << std::endl;
    fail++;
  }

  if (output.getCount() == 0 || data_actualOut == nullptr) {
    std::cout << "Failure: butomUpOut array buffer not valid. " << std::endl;
    fail++;
  }
}


void MemoryLeakTest::testRun()
{


  if (verbose) std::cout << "Creating network..." << std::endl;
  Network n;

  size_t regionCntBefore = n.getRegions().getCount();

  if (verbose) std::cout << "Adding a built-in SPRegion region..." << std::endl;
  Region_Ptr_t region1 = n.addRegion("region1", "SPRegion", "");
  size_t regionCntAfter = n.getRegions().getCount();
  if (regionCntBefore + 1 != regionCntAfter) {
    std::cout << "Failure:  Expected number of regions to increase by one.  " << "" << std::endl;
    std::cout << "Region count before adding any regions is " << regionCntBefore << " and after adding built-in region, it is " << regionCntAfter << "" << std::endl;
    fail++;
  }

  if (region1->getType() != "SPRegion") {
    std::cout << "Failure: Expected type for region1 to be \"SPRegion\" but type is: " << region1->getType() << "" << std::endl;
    fail++;
  }


  // make sure the custom region registration works for CPP.
  // We will just use the same SPRegion class but it could be a subclass or some different custom class.
  if (verbose) std::cout << "Adding a custom-built SPRegion region..." << std::endl;
  n.registerCPPRegion("SPRegionCustom", new RegisteredRegionImpl<SPRegion>());
  Region_Ptr_t region2 = n.addRegion("region2", "SPRegionCustom", "");
  regionCntAfter = n.getRegions().getCount();
  if (regionCntBefore + 2 != regionCntAfter) {
    std::cout << "Failure:  Expected number of regions to increase by one.  " << "" << std::endl;
    std::cout << "Region count before adding any regions is " << regionCntBefore << " and after adding custom region, it is " << regionCntAfter << "" << std::endl;
    fail = true;
  }
  if (region2->getType() != "SPRegionCustom") {
    std::cout << "Failure: Expected type for region2 to be \"SPRegionCustom\" but type is: " << region2->getType() << "" << std::endl;
    fail = true;
  }
  n.removeRegion("region2");
  n.unregisterCPPRegion("SPRegionCustom");



  // compute() should fail because network has not been initialized
  SHOULDFAIL(n.run(1));
  SHOULDFAIL(region1->compute());

  // should fail because network can't be initialized without dimensions
  SHOULDFAIL(n.initialize());

  if (verbose) std::cout << "Setting dimensions of region1..." << std::endl;
  Dimensions d;
  d.push_back(4);
  d.push_back(4);
  region1->setDimensions(d);

  // should work this time now that dimensions are set.
  if (verbose) std::cout << "Initializing again..." << std::endl;
  n.initialize();

  // verify that all parameters are working.
  testSpecAndParameters(region1);

  // execute one iteration of the region
  region1->compute();

  // now execute 9 more iterations of the region.
  n.run(9);

  testInputOutputAccess(region1);
}



void MemoryLeakTest::testCppLinking(std::string linkPolicy, std::string linkParams)
{
  if (verbose) std::cout << "test linking." << std::endl;

  Network net;

  // Make sure we can feed data from some other region to our SPRegion.
  // To test this we will hook up the ScalarSensor to our SPRegion
  // The ScalerSensor uses the ScalerEncoder to encode data into an SDR.
  Region_Ptr_t region1 = net.addRegion("region1", "ScalarSensor", "");
  Region_Ptr_t region2 = net.addRegion("region2", "SPRegion", "");
  net.link("region1", "region2", linkPolicy, linkParams);

  if (verbose) std::cout << "Initialize should fail..." << std::endl;
  SHOULDFAIL(net.initialize());

  std::cout << "Setting region1 dims" << std::endl;
  Dimensions r1dims;
  r1dims.push_back(6);
  r1dims.push_back(4);
  region1->setDimensions(r1dims);

  std::cout << "Initialize should now succeed" << std::endl;
  net.initialize();

  const Dimensions& r2dims = region2->getDimensions();
  NTA_CHECK(r2dims.size() == 2) << " actual dims: " << r2dims.toString();
  NTA_CHECK(r2dims[0] == 3) << " actual dims: " << r2dims.toString();
  NTA_CHECK(r2dims[1] == 2) << " actual dims: " << r2dims.toString();

  SHOULDFAIL(region2->setDimensions(r1dims));

  // execute the ScalarSensor to create the input for our SPRegion.
  ArrayRef r1OutputArray = region1->getOutputData("Encoded value");
  region1->setParameterReal64("sensedValue", sensedValues_[iter_++]);    // give it an input value
  region1->compute();
  if (verbose) std::cout << "Checking region1 output after first iteration..." << std::endl;
  Real64 *buffer1 = (Real64*) r1OutputArray.getBuffer();

  for (size_t i = 0; i < r1OutputArray.getCount(); i++)
  {
    if (verbose)  std::cout << "  " << i << "    " << buffer1[i] << "" << std::endl;
  }

  region2->prepareInputs();
  ArrayRef r2InputArray = region2->getInputData("bottomUpIn");
  std::cout << "Region 2 input for first iteration:" << std::endl;
  if (r1OutputArray.getCount() != r2InputArray.getCount()) {
    std::cout << "Failure: Buffer length different. Output from encoder is " << r1OutputArray.getCount() << ", input to SPRegion is " << r2InputArray.getCount() << "." << std::endl;
    fail++;
  }
  else {
    Real64 *buffer2 = (Real64*)r2InputArray.getBuffer();
    if (verbose) std::cout << "Data being fed into SPRegion:" << std::endl;
    for (size_t i = 0; i < r2InputArray.getCount(); i++)
    {
      if (verbose) std::cout << "  " << i << "    " << buffer2[i] << "" << std::endl;
      if (buffer2[i] != buffer1[i]) {
        std::cout << "Failure: Buffer content different. Element " << i << " of Output from encoder is " << buffer1[i] << ", input to SPRegion is " << buffer2[i] << "." << std::endl;
        fail++;
        break;
      }
    }
  }

  // execute the SPRegion and check that it has output.
  region2->compute();
  ArrayRef r2OutputArray = region2->getOutputData("bottomUpOut");
  if (r2InputArray.getCount() != r2OutputArray.getCount()) {
    std::cout << "Failure: Buffer length different. input to SPRegion is " << r2InputArray.getCount() << ", Output from SPRegion is " << r2OutputArray.getCount() << "." << std::endl;
    fail++;
  }


}


void MemoryLeakTest::testSerialization()
{
  const char *goodparams = "{int32Param: 1234, real64Param: 23.1}";
  //  badparams contains a non-existent parameter
  const char *badparams = "{int32Param: 1234, real64Param: 23.1, badParam: 4}";

  if (verbose) std::cout << "test serialization." << std::endl;

  // use default parameters the first time
  Network* net1 = new Network();
  Network* net2 = nullptr;
  Network* net3 = nullptr;

  try {

    if (verbose) std::cout << "Setup first network and save it" << std::endl;
    Region_Ptr_t region1 = net1->addRegion("region", "SPRegion", "");
    Dimensions r1dims;
    r1dims.push_back(6);
    r1dims.push_back(6);
    region1->setDimensions(r1dims);
    net1->initialize();
    region1->compute();
    testSpecAndParameters(region1);   // make sure all default parameters are still set

    net1->save("spRegionTest.nta");

    if (verbose) std::cout << "Restore into a second network and compare." << std::endl;
    net2 = new Network("spRegionTest.nta");

    Region_Ptr_t region2 = net2->getRegions().getByName("region");
    if (region2->getType() != "SPRegion") {
      std::cout << "Failure: Restored region does not have the right type.  Expected SPRegion, found " << region2->getType() << "" << std::endl;
      fail++;
    }
    testSpecAndParameters(region2);   // make sure all default parameters are still set

    // compare internal states.
    if (region1->getParameterUInt32("columnCount") != region2->getParameterUInt32("columnCount")) {
      std::cout << "Failure: columnCount changed after restore: expected " << region1->getParameterUInt32("columnCount") << " got " << region2->getParameterUInt32("columnCount") << "" << std::endl;
      fail++;
    }
    if (region1->getParameterUInt32("inputWidth") != region2->getParameterUInt32("inputWidth")) {
      std::cout << "Failure: inputWidth changed after restore: expected " << region1->getParameterUInt32("inputWidth") << " got " << region2->getParameterUInt32("inputWidth") << "" << std::endl;
      fail++;
    }
    compareRegionArrays(region1, region2, "spInputNonZeros", NTA_BasicType_UInt32);
    compareRegionArrays(region1, region2, "spOutputNonZeros", NTA_BasicType_UInt32);
    compareRegionArrays(region1, region2, "spOverlapDistribution", NTA_BasicType_Real32);
    compareRegionArrays(region1, region2, "denseOutput", NTA_BasicType_Real32);

    if (region1->getParameterString("sparseCoincidenceMatrix") != region2->getParameterString("sparseCoincidenceMatrix")) {
      std::cout << "Failure: sparseCoincidenceMatrix changed after restore: expected \""
        << region1->getParameterString("sparseCoincidenceMatrix")
        << "\" got \"" << region2->getParameterString("sparseCoincidenceMatrix") << "\"." << std::endl;
      fail++;
    }

    // can we continue with execution?  See if we get any exceptions.
    region2->compute();

    // Change some parameters and see if they are retained after a restore.
    region2->setParameterUInt32("globalInhibition", 1);
    region2->setParameterUInt32("numActiveColumnsPerInhArea", 40);
    region2->setParameterReal32("localAreaDensity", -1.0);
    region2->setParameterReal32("potentialPct", 0.85);
    region2->setParameterReal32("synPermConnected", 0.1);
    region2->setParameterReal32("synPermActiveInc", 0.04);
    region2->setParameterReal32("synPermInactiveDec", 0.005);
    region2->setParameterReal32("boostStrength", 3.0);
    region2->compute();

    net2->save("spRegionTest.nta");

    if (verbose) std::cout << "Restore into a third network and compare changed parameters." << std::endl;
    net3 = new Network("spRegionTest.nta");
    Region_Ptr_t region3 = net3->getRegions().getByName("region");
    if (region3->getType() != "SPRegion") {
      std::cout << "Failure: Restored region does not have the right type.  Expected \"SPRegion\", found \"" << region3->getType() << "\"." << std::endl;
      fail++;
    }

    if (region3->getParameterUInt32("globalInhibition") != 1) {
      std::cout << "Failure: globalInhibition changed after restore: expected 1 got " << region3->getParameterUInt32("globalInhibition") << "" << std::endl;
      fail++;
    }
    if (region3->getParameterUInt32("numActiveColumnsPerInhArea") != 40) {
      std::cout << "Failure: numActiveColumnsPerInhArea changed after restore: expected 40 got " << region3->getParameterUInt32("numActiveColumnsPerInhArea") << "" << std::endl;
      fail++;
    }
    if (region3->getParameterReal32("localAreaDensity") != -1.0) {
      std::cout << "Failure: localAreaDensity changed after restore: expected -1.0 got " << region3->getParameterReal32("localAreaDensity") << "" << std::endl;
      fail++;
    }
    if (region3->getParameterReal32("potentialPct") != 0.85) {
      std::cout << "Failure: potentialPct changed after restore: expected 0.85 got " << region3->getParameterReal32("potentialPct") << "" << std::endl;
      fail++;
    }
    if (region3->getParameterReal32("synPermConnected") != 0.1) {
      std::cout << "Failure: synPermConnected changed after restore: expected 0.1 got " << region3->getParameterReal32("synPermConnected") << "" << std::endl;
      fail++;
    }
    if (region3->getParameterReal32("synPermActiveInc") != 0.04) {
      std::cout << "Failure: synPermActiveInc changed after restore: expected 0.04 got " << region3->getParameterReal32("synPermActiveInc") << "" << std::endl;
      fail++;
    }
    if (region3->getParameterReal32("synPermInactiveDec") != 0.005) {
      std::cout << "Failure: synPermInactiveDec changed after restore: expected 0.005 got " << region3->getParameterReal32("synPermInactiveDec") << "" << std::endl;
      fail++;
    }
    if (region3->getParameterReal32("boostStrength") != 3.0) {
      std::cout << "Failure: boostStrength changed after restore: expected 3.0 got " << region3->getParameterReal32("boostStrength") << "" << std::endl;
      fail++;
    }
  }
  catch (nupic::Exception& ex) {
    std::cout << "Failure: Exception: " << ex.getFilename() << "(" << ex.getLineNumber() << ") " << ex.getMessage() << "" << std::endl;
    fail++;
  }
  catch (std::exception& e) {
    std::cout << "Failure: Exception: " << e.what() << "" << std::endl;
    fail++;
  }

  if (net1 != nullptr) { delete net1; }
  if (net2 != nullptr) { delete net2; }
  if (net3 != nullptr) { delete net3; }

}



int MemoryLeakTest::realmain(bool leakTest)
{

  // Read in the modelParams
  //std::ifstream t("modelParams.yaml");
  //std::string yamlstring((std::istreambuf_iterator<char>(t)),  std::istreambuf_iterator<char>());
  //modelParams = YAML::Load(yamlstring);
  try {
    testRun();
  }
  catch (nupic::Exception& ex) {
    std::cout << "Failure: Exception in testRun: " << ex.getFilename() << "(" << ex.getLineNumber() << ") " << ex.getMessage() << "" << std::endl;
    fail++;
  }

  try {
    testCppLinking("TestFanIn2", "");
  }
  catch (nupic::Exception& ex) {
    std::cout << "Failure: Exception in testCppLinking: " << ex.getFilename() << "(" << ex.getLineNumber() << ") " << ex.getMessage() << "" << std::endl;
    fail++;
  }

  try {
    testCppLinking("UniformLink","{mapping: in, rfSize: [2]}");
  }
  catch (nupic::Exception& ex) {
    std::cout << "Failure: Exception in testCppLinking: " << ex.getFilename() << "(" << ex.getLineNumber() << ") " << ex.getMessage() << "" << std::endl;
    fail++;
  }

  try {
    testSerialization();
  }
  catch (Exception& ex) {
    std::cout << "Failure: Exception in testSerialization: " << ex.getFilename() << "(" << ex.getLineNumber() << ") " << ex.getMessage() << "" << std::endl;
    fail++;
  }


  if (fail)
    std::cout << "Done -- " << fail << " tests failed." << std::endl;
  else
    std::cout << "Done -- ALL TESTS PASSED" << std::endl;

  return fail;
}

// a utility function to compare two parameter arrays
bool MemoryLeakTest::compareRegionArrays(Region_Ptr_t region1, Region_Ptr_t region2, std::string parameter, NTA_BasicType type)
{
  UInt32 * buf1;
  UInt32 * buf2;
  Real32 * buf3;
  Real32 * buf4;
  Array array1(type);
  Array array2(type);
  region1->getParameterArray(parameter, array1);
  region2->getParameterArray(parameter, array2);

  size_t len1 = array1.getCount();
  if (len1 != array2.getCount()) {
    std::cout << "Failure: Arrays for parameter " << parameter << " are not the same length after restore." << std::endl;
    fail++;
  }
  switch (type)
  {
  case NTA_BasicType_UInt32:
    buf1 = (UInt32*)array1.getBuffer();
    buf2 = (UInt32*)array2.getBuffer();
    for (int i = 0; i < len1; i++) {
      if (buf1[i] != buf2[i]) {
        std::cout << "Failure: Array elements for parameter " << parameter << "[" << i << "] are not the sameafter restore." << std::endl;
        fail++;
        break;
      }
    }
    break;

  case NTA_BasicType_Real32:
    buf3 = (Real32*)array1.getBuffer();
    buf4 = (Real32*)array2.getBuffer();
    for (int i = 0; i < len1; i++) {
      if (buf3[i] != buf4[i]) {
        std::cout << "Failure: Array elements for parameter " << parameter << "[" << i << "] are not the sameafter restore." << std::endl;
        fail++;
        break;
      }
    }
    break;
  } // end switch
  return false;
}


/************* MemoryMonitor *****************************/
class MemoryMonitor
{
public:
  MemoryMonitor()
  {
#if defined(NTA_OS_WINDOWS)
    // takes longer to settle down on win32
    memoryLeakStartIter = 6000;
#else
    memoryLeakStartIter = 150;
#endif
    memoryLeakDeltaIterCheck = 10;
    initial_vmem = 0;
    initial_rmem = 0;
    current_vmem = 0;
    current_rmem = 0;

    OS::getProcessMemoryUsage(initial_vmem, initial_rmem);
  }

  ~MemoryMonitor()
  {
    if (hasMemoryLeaks())
    {
      NTA_DEBUG
        << "Memory leaks detected. "
        << "Real Memory: " << diff_rmem
        << ", Virtual Memory: " << diff_vmem;
    }
  }

  size_t getMinCount() {
    size_t minCount = memoryLeakStartIter + 5 * memoryLeakDeltaIterCheck;
    return minCount;
  }

  void update()
  {
    OS::getProcessMemoryUsage(current_vmem, current_rmem);
    diff_vmem = current_vmem - initial_vmem;
    diff_rmem = current_rmem - initial_rmem;
  }

  bool hasMemoryLeaks()
  {
    update();
    return diff_vmem > 0 || diff_rmem > 0;
  }

  // memory leak detection
  // we check even prior to the initial tracking iteration, because the act
  // of checking potentially modifies our memory usage
  void memoryLeakCheck(size_t iteration)
  {
    if ((iteration % memoryLeakDeltaIterCheck) == 0)
    {
      OS::getProcessMemoryUsage(current_rmem, current_vmem);
      if (iteration == memoryLeakStartIter)
      {
        initial_rmem = current_rmem;
        initial_vmem = current_vmem;
      }
      std::cout << "Memory usage: " << current_vmem << " (virtual) "
        << current_rmem << " (real) at iteration " << iteration << std::endl;

      if (iteration >= memoryLeakStartIter)
      {
        if (current_vmem > initial_vmem || current_rmem > initial_rmem)
        {
          std::cout << "Tracked memory usage (iteration "
            << memoryLeakStartIter << "): " << initial_vmem
            << " (virtual) " << initial_rmem << " (real)" << std::endl;
          throw std::runtime_error("Memory leak detected");
        }
      }
    }
  }

private:
  size_t initial_vmem;
  size_t initial_rmem;
  size_t current_vmem;
  size_t current_rmem;
  size_t diff_rmem;
  size_t diff_vmem;

  size_t memoryLeakStartIter;  // Start checking memory usage after this many iterations. 
  size_t memoryLeakDeltaIterCheck;  // This determines how frequently we check. 

};


/************* main *****************************/

int main(int argc, char *argv[])
{
  
  /* 
   * Without arguments, this program is a simple end-to-end demo
   * of NuPIC 2 functionality, used as a developer tool (when 
   * we add a feature, we add it to this program. 
   *
   * Runtime argument:  Number of times to run test.
   * With an integer argument N, runs the same test N times
   * and requires that memory use stay constant -- it can't
   * grow by even one byte. 
   */

  // TODO: real argument parsing
  // Optional arg is number of iterations to do. 
  NTA_CHECK(argc == 1 || argc == 2);
  size_t count = 1;
  if (argc == 2)
  {
    std::stringstream ss(argv[1]);
    ss >> count;
  }

  MemoryMonitor mm;
  MemoryLeakTest test;
  int fail = 0;


  if (count > 1 && count < mm.getMinCount())
  {
    std::cout << "Run count of " << count << " specified\n";
    std::cout << "When run in leak detection mode, count must be at least " << mm.getMinCount() << "\n";
    ::exit(1);
  }
   

  try {
    for (size_t i = 0;(!fail &&  i < count); i++)
    {
      NuPIC::init();
      fail = test.realmain(count > 1);
      NuPIC::shutdown();
      mm.memoryLeakCheck(i);
    }

  } catch (nupic::Exception& e) {
    std::cout 
      << "Exception: " << e.getMessage() 
      << " at: " << e.getFilename() << "(" << e.getLineNumber() << ")"
      << std::endl;
    return 1;

  } catch (std::exception& e) {
    std::cout << "Exception: " << e.what() << "" << std::endl;
    return 1;
  }
  catch (...) {
    std::cout << "\nTest is exiting because an unknown exception was thrown" << std::endl;
    return 1;
  }
  if (count > 20)
    std::cout << "Memory leak check passed -- " << count << " iterations" << std::endl;
  return (fail > 0);
}
