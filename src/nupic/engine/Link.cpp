/* ---------------------------------------------------------------------
 * Numenta Platform for Intelligent Computing (NuPIC)
 * Copyright (C) 2013-2017, Numenta, Inc.  Unless you have an agreement
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
 * ---------------------------------------------------------------------
 */

/** @file
 * Implementation of the Link class
 */
#include <cstring> // memcpy,memset
#include <nupic/engine/Input.hpp>
#include <nupic/engine/Link.hpp>
#include <nupic/engine/LinkPolicy.hpp>
#include <nupic/engine/LinkPolicyFactory.hpp>
#include <nupic/engine/Output.hpp>
#include <nupic/engine/Region.hpp>
#include <nupic/ntypes/Array.hpp>
#include <nupic/types/BasicType.hpp>
#include <nupic/utils/Log.hpp>

// Set this to true when debugging to enable handy debug-level logging of data
// moving through the links, including the delayed link transitions.
#define _LINK_DEBUG false

namespace nupic {

  Link::Link(const std::string &linkType, const std::string &linkParams,
             const std::string &srcRegionName, const std::string &destRegionName,
             const std::string &srcOutputName, const std::string &destInputName,
             const size_t propagationDelay)
      : srcBuffer_(0) {
    commonConstructorInit_(linkType, linkParams, srcRegionName, destRegionName,
                           srcOutputName, destInputName, propagationDelay);
  }

  Link::Link(const std::string &linkType, const std::string &linkParams,
             Output *srcOutput, Input *destInput, const size_t propagationDelay)
      : srcBuffer_(0) {
    commonConstructorInit_(linkType, linkParams, srcOutput->getRegion().getName(),
                           destInput->getRegion().getName(), srcOutput->getName(),
                           destInput->getName(), propagationDelay);

    connectToNetwork(srcOutput, destInput);
    // Note -- link is not usable until we set the destOffset, which happens at
    // initialization time
  }

  Link::Link() : srcBuffer_(0) {
    destOffset_ = 0;
    srcOffset_ = 0;
    srcSize_ = 0;
    src_ = nullptr;
    dest_ = nullptr;
    initialized_ = false;
    impl_ = nullptr;
  }

  void Link::commonConstructorInit_(const std::string &linkType,
                                    const std::string &linkParams,
                                    const std::string &srcRegionName,
                                    const std::string &destRegionName,
                                    const std::string &srcOutputName,
                                    const std::string &destInputName,
                                    const size_t propagationDelay) {
    linkType_ = linkType;
    linkParams_ = linkParams;
    srcRegionName_ = srcRegionName;
    srcOutputName_ = srcOutputName;
    destRegionName_ = destRegionName;
    destInputName_ = destInputName;
    propagationDelay_ = propagationDelay;
    destOffset_ = 0;
    srcOffset_ = 0;
    srcSize_ = 0;
    src_ = nullptr;
    dest_ = nullptr;
    initialized_ = false;

    impl_ = LinkPolicyFactory().createLinkPolicy(linkType, linkParams, this);
  }

  Link::~Link() { if (impl_) delete impl_; }

  void Link::initPropagationDelayBuffer_(size_t propagationDelay,
                                         NTA_BasicType dataElementType,
                                         size_t dataElementCount) {
    if (srcBuffer_.capacity() != 0 || !propagationDelay) {
      // Already initialized(e.g., as result of deserialization); or a 0-delay
      // link, which doesn't use buffering.
      return;
    }

    // Establish capacity for the requested delay data elements
    srcBuffer_.set_capacity(propagationDelay);

    // Initialize delay data elements
    for (size_t i = 0; i < propagationDelay; i++) {
      Array arrayTemplate(dataElementType);

      srcBuffer_.push_back(arrayTemplate);

      // Allocate 0-initialized data for current element
      srcBuffer_[i].allocateBuffer(dataElementCount);
      srcBuffer_[i].zeroBuffer();
    }
  }

  void Link::initialize(size_t destinationOffset) {
    // Make sure all information is specified and
    // consistent. Unless there is a NuPIC implementation
    // error, all these checks are guaranteed to pass
    // because of the way the network is constructed
    // and initialized.

    // Make sure we have been attached to a real network
    NTA_CHECK(src_ != nullptr) << "Link::initialize() and src_ Output object not set.";
    NTA_CHECK(dest_ != nullptr) << "Link::initialize() and dest_ Input object not set.";

    // Confirm that our dimensions are consistent with the
    // dimensions of the regions we're connecting.
    const Dimensions &srcD = getSrcDimensions();
    const Dimensions &destD = getDestDimensions();
    NTA_CHECK(!srcD.isUnspecified());
    NTA_CHECK(!destD.isUnspecified());

    Dimensions oneD;
    oneD.push_back(1);

    if (src_->isRegionLevel()) {
      Dimensions d;
      for (size_t i = 0; i < src_->getRegion().getDimensions().size(); i++) {
        d.push_back(1);
      }

      NTA_CHECK(srcD.isDontcare() || srcD == d);
    } else if (src_->getRegion().getDimensions() == oneD) {
      Dimensions d;
      for (size_t i = 0; i < srcD.size(); i++) {
        d.push_back(1);
      }
      NTA_CHECK(srcD.isDontcare() || srcD == d);
    } else {
      NTA_CHECK(srcD.isDontcare() || srcD == src_->getRegion().getDimensions());
    }

    if (dest_->isRegionLevel()) {
      Dimensions d;
      for (size_t i = 0; i < dest_->getRegion().getDimensions().size(); i++) {
        d.push_back(1);
      }

      NTA_CHECK(destD.isDontcare() || destD.isOnes());
    } else if (dest_->getRegion().getDimensions() == oneD) {
      Dimensions d;
      for (size_t i = 0; i < destD.size(); i++) {
        d.push_back(1);
      }
      NTA_CHECK(destD.isDontcare() || destD == d);
    } else {
      NTA_CHECK(destD.isDontcare() ||
                destD == dest_->getRegion().getDimensions());
    }

    destOffset_ = destinationOffset;
    impl_->initialize();

    // ---
    // Initialize the propagation delay buffer
    // But skip it if it already has something in it from deserialize().
    // ---
    if (srcBuffer_.size() == 0)
      initPropagationDelayBuffer_(propagationDelay_, src_->getData().getType(),
                                  src_->getData().getCount());

    initialized_ = true;
  }

  void Link::setSrcDimensions(Dimensions &dims) {
    NTA_CHECK(src_ != nullptr && dest_ != nullptr)
        << "Link::setSrcDimensions() can only be called on a connected link";

    size_t nodeElementCount = src_->getNodeOutputElementCount();
    if (nodeElementCount == 0) {
      nodeElementCount =
          src_->getRegion().getNodeOutputElementCount(src_->getName());
    }
    impl_->setNodeOutputElementCount(nodeElementCount);

    impl_->setSrcDimensions(dims);
  }

  void Link::setDestDimensions(Dimensions &dims) {
    NTA_CHECK(src_ != nullptr && dest_ != nullptr)
        << "Link::setDestDimensions() can only be called on a connected link";

    size_t nodeElementCount = src_->getNodeOutputElementCount();
    if (nodeElementCount == 0) {
      nodeElementCount =
          src_->getRegion().getNodeOutputElementCount(src_->getName());
    }
    impl_->setNodeOutputElementCount(nodeElementCount);

    impl_->setDestDimensions(dims);
  }

  const Dimensions &Link::getSrcDimensions() const {
    return impl_->getSrcDimensions();
  };

  const Dimensions &Link::getDestDimensions() const {
    return impl_->getDestDimensions();
  };

  // Return constructor params
  const std::string &Link::getLinkType() const { return linkType_; }

  const std::string &Link::getLinkParams() const { return linkParams_; }

  const std::string &Link::getSrcRegionName() const { return srcRegionName_; }

  const std::string &Link::getSrcOutputName() const { return srcOutputName_; }

  const std::string &Link::getDestRegionName() const { return destRegionName_; }

  const std::string &Link::getDestInputName() const { return destInputName_; }

  std::string Link::getMoniker() const {
    std::stringstream ss;
    ss << getSrcRegionName() << "." << getSrcOutputName() << "-->"
       << getDestRegionName() << "." << getDestInputName();
    return ss.str();
  }

  const std::string Link::toString() const {
    std::stringstream ss;
    ss << "[" << getSrcRegionName() << "." << getSrcOutputName();
    if (src_) {
      ss << " (region dims: " << src_->getRegion().getDimensions().toString()
         << ") ";
    }
    ss << " to " << getDestRegionName() << "." << getDestInputName();
    if (dest_) {
      ss << " (region dims: " << dest_->getRegion().getDimensions().toString()
         << ") ";
    }
    ss << " type: " << linkType_ << "]";
    return ss.str();
  }

  void Link::connectToNetwork(Output *src, Input *dest) {
    NTA_CHECK(src != nullptr);
    NTA_CHECK(dest != nullptr);

    src_ = src;
    dest_ = dest;
  }

  // The methods below only work on connected links.
  Output &Link::getSrc() const

  {
    NTA_CHECK(src_ != nullptr)
        << "Link::getSrc() can only be called on a connected link";
    return *src_;
  }

  Input &Link::getDest() const {
    NTA_CHECK(dest_ != nullptr)
        << "Link::getDest() can only be called on a connected link";
    return *dest_;
  }

  void Link::buildSplitterMap(Input::SplitterMap &splitter) {
    // The link policy generates a splitter map
    // at the element level.  Here we convert it
    // to a full splitter map
    //
    // if protoSplitter[destNode][x] == srcElement for some x
    // means that the output srcElement is sent to destNode

    Input::SplitterMap protoSplitter;
    protoSplitter.resize(splitter.size());
    size_t nodeElementCount = src_->getNodeOutputElementCount();
    impl_->setNodeOutputElementCount(nodeElementCount);
    impl_->buildProtoSplitterMap(protoSplitter);

    for (size_t destNode = 0; destNode < splitter.size(); destNode++) {
      // convert proto-splitter values into real
      // splitter values;
      for (auto &elem : protoSplitter[destNode]) {
        size_t srcElement = elem;
        size_t elementOffset = srcElement + destOffset_;
        splitter[destNode].push_back(elementOffset);
      }
    }
  }

  void Link::compute() {
    NTA_CHECK(initialized_);

    if (propagationDelay_) {
      NTA_CHECK(!srcBuffer_.empty());
    }

    // Copy data from source to destination. For delayed links, will copy from
    // head of circular queue; otherwise directly from source.
    const Array &src = propagationDelay_ ? srcBuffer_[0] : src_->getData();

    const Array &dest = dest_->getData();

    NTA_BasicType srctype = src.getType();
    size_t typeSize = BasicType::getSize(srctype);
    size_t srcSize = src.getCount() * typeSize;
    size_t destByteOffset = destOffset_ * typeSize;
    NTA_BasicType dsttype = dest.getType();

    if (_LINK_DEBUG) {
      NTA_DEBUG << "Link::compute: " << getMoniker() << "; copying to dest input"
                << "; delay=" << propagationDelay_ << "; " << src.getCount()
                << " elements=" << src;
    }

    ::memcpy((char *)(dest.getBuffer()) + destByteOffset, src.getBuffer(),
             srcSize);
  }

  void Link::shiftBufferedData() {
    if (!propagationDelay_) {
      // Source buffering is not used in 0-delay links
      return;
    }

    // A delayed link's circular buffer should always be at capacity, because
    // it starts out full in link initialization and we always append the new
    // source value after shifting out the head.
    NTA_CHECK(srcBuffer_.full());

    // Pop head of circular queue

    if (_LINK_DEBUG) {
      NTA_DEBUG << "Link::shiftBufferedData: " << getMoniker()
                << "; popping head; " << srcBuffer_[0].getCount()
                << " elements=" << srcBuffer_[0];
    }

    srcBuffer_.pop_front();

    // Append the current src value to circular queue

    const Array &srcArray = src_->getData();
    size_t elementCount = srcArray.getCount();
    auto elementType = srcArray.getType();

    if (_LINK_DEBUG) {
      NTA_DEBUG << "Link::shiftBufferedData: " << getMoniker()
                << "; appending src to circular buffer; " << elementCount
                << " elements=" << srcArray;

      NTA_DEBUG << "Link::shiftBufferedData: " << getMoniker()
                << "; num arrays in circular buffer before append; "
                << srcBuffer_.size() << "; capacity=" << srcBuffer_.capacity();
    }

    Array array(elementType);
    srcBuffer_.push_back(array);

    auto &lastElement = srcBuffer_.back();
    lastElement.allocateBuffer(elementCount);
    ::memcpy(lastElement.getBuffer(), srcArray.getBuffer(),
             elementCount * BasicType::getSize(elementType));

    if (_LINK_DEBUG) {
      NTA_DEBUG << "Link::shiftBufferedData: " << getMoniker()
                << "; circular buffer head after append is: "
                << srcBuffer_[0].getCount() << " elements=" << srcBuffer_[0];
    }
  }

  void Link::serialize(YAML::Emitter& out) {
    size_t srcCount = ((!src_) ? (size_t)0 : src_->getData().getCount());
    NTA_BasicType srcType =
        ((!src_) ? BasicType::parse(getLinkType()) : src_->getData().getType());
    out << YAML::BeginMap;
    out << YAML::Key << "linkType" << YAML::Value << getLinkType();
    out << YAML::Key << "params" << YAML::Value << getLinkParams();
    out << YAML::Key << "srcRegion" << YAML::Value << getSrcRegionName();
    out << YAML::Key << "srcOutput" << YAML::Value << getSrcOutputName();
    out << YAML::Key << "destRegion" << YAML::Value << getDestRegionName();
    out << YAML::Key << "destInput" << YAML::Value << getDestInputName();
    out << YAML::Key << "propagationDelay" << YAML::Value << propagationDelay_;
    out << YAML::Key << "circularBuffer" << YAML::Value << YAML::BeginSeq;
    if (propagationDelay_ > 0) {
      // we need to capture the circularBuffer used for propagationDelay
      boost::circular_buffer<Array>::iterator itr;
      for (auto itr = srcBuffer_.begin(); itr != srcBuffer_.end(); itr++) {
        Array &buf = *itr;
        buf.serialize(out);
      } // end for
    }
    out << YAML::EndSeq;


    out << YAML::EndMap;
  }

  void Link::deserialize(const YAML::Node &link) {
    // Each link is a map -- extract the 9 values in the map
    // The "circularBuffer" element is a two dimentional array only present if
    // propogationDelay > 0.
    NTA_CHECK(link.Type() == YAML::NodeType::Map)
        << "Invalid network structure file -- bad link (not a map)";
    NTA_CHECK(link.size() == 8)
        << "Invalid network structure file -- bad link (wrong size)";

    YAML::Node node;

    // 1. type
    node = link["linkType"];
    NTA_CHECK(node.IsScalar()) << "Invalid network structure file -- link does "
                                  "not have a 'linkType' field.";
    std::string linkType = node.as<std::string>();

    // 2. params
    node = link["params"];
    NTA_CHECK(node.IsScalar()) << "Invalid network structure file -- link does "
                                  "not have a 'params' field.";
    std::string linkParams = node.as<std::string>();

    // 3. srcRegion (name)
    node = link["srcRegion"];
    NTA_CHECK(node.IsScalar()) << "Invalid network structure file -- link does "
                                  "not have a 'srcRegion' field.";
    std::string srcRegionName = node.as<std::string>();

    // 4. srcOutput
    node = link["srcOutput"];
    NTA_CHECK(node.IsScalar()) << "Invalid network structure file -- link does "
                                   "not have a 'srcOutput' field";
    std::string srcOutputName = node.as<std::string>();

    // 5. destRegion
    node = link["destRegion"];
    NTA_CHECK(node.IsScalar()) << "Invalid network structure file -- link does "
                                   "not have a 'destRegion' field.";
    std::string destRegionName = node.as<std::string>();

    // 6. destInput
    node = link["destInput"];
    NTA_CHECK(node.IsScalar()) << "Invalid network structure file -- link does "
                                  "not have a 'destInput' field.";
    std::string destInputName = node.as<std::string>();


    // 7. propogationDelay (number of source buffers in the circular buffer)
    node = link["propagationDelay"];
    NTA_CHECK(node.IsScalar()) << "Invalid network structure file -- link does "
                                  "not have a 'propagationDelay' field.";
    size_t propagationDelay = node.as<size_t>();

    // fill in the data for the Link object and create the linkPolicy.
    commonConstructorInit_(linkType, linkParams, srcRegionName, destRegionName,
                           srcOutputName, destInputName, propagationDelay);

    // 8. circular Buffer
    node = link["circularBuffer"];
    NTA_CHECK(node.IsSequence()) << " Invalid network structure file-- link does not have a 'circularBuffer' field.";
    // Create a set of empty Array objects in the circularBuffer.
    // The contents of each array will be replaced by the deserialization
    // so the initial size or type does not matter.  Initialize it a dumby type and size.
    // if no propagationDelay (value = 0) then there should be an empty sequence.
    initPropagationDelayBuffer_(propagationDelay, NTA_BasicType_Byte, 1);
    size_t idx = 0;
    for (const auto &valiter : node) {
      NTA_CHECK(idx < propagationDelay_)
          << "Invalid network structure file -- link has too many buffers in 'circularBuffer'.";
      srcBuffer_[idx++].deserialize(valiter);
    }
    NTA_CHECK(idx == propagationDelay_) << "Invalid network structure file -- "
                                          "link has too few buffers in "
                                          "'circularBuffer'.";

  }

  std::ostream &operator<<(std::ostream &f, const Link &link) {
    f << "<Link>\n";
    f << "  <type>" << link.getLinkType() << "</type>\n";
    f << "  <params>" << link.getLinkParams() << "</params>\n";
    f << "  <srcRegion>" << link.getSrcRegionName() << "</srcRegion>\n";
    f << "  <destRegion>" << link.getDestRegionName() << "</destRegion>\n";
    f << "  <srcOutput>" << link.getSrcOutputName() << "</srcOutput>\n";
    f << "  <destInput>" << link.getDestInputName() << "</destInput>\n";
    f << "  <propagationDelay>" << link.getPropagationDelay()
      << "</propagationDelay>\n";
    f << "</Link>\n";
    return f;
  }

} // namespace nupic
