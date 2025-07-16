#ifndef CondFormats_HGCalObjects_interface_HGCalMappingCellTriggerParameterIndex_h
#define CondFormats_HGCalObjects_interface_HGCalMappingCellTriggerParameterIndex_h

#include <iostream>
#include <cstdint>
#include <vector>
#include <numeric>
#include "DataFormats/HGCalDigi/interface/HGCalElectronicsId.h"
#include "CondFormats/Serialization/interface/Serializable.h"
#include "CondFormats/HGCalObjects/interface/HGCalDenseIndexerBase.h"

/**
   @short utility class to assign dense readout trigger cell indexing
 */
class HGCalMappingCellIndexerTrigger {
public:
  // typedef HGCalDenseIndexerBase WaferDenseIndexerBase;

  HGCalMappingCellIndexerTrigger() = default;

  /**
     adds to map of type codes (= module types) to handle and updatest the max. number of eRx 
   */
  void processNewCell(std::string typecode, uint16_t ROC, uint16_t trLink,uint16_t trCell) {
    // Skip if trLink, trCell not connected
    if (trLink == uint16_t(-1) || trCell == uint16_t(-1)) return; 
    //assign index to this typecode and resize the max vectors
    if (typeCodeIndexer_.count(typecode) == 0) {
      typeCodeIndexer_[typecode] = typeCodeIndexer_.size();
      maxROC_.resize(typeCodeIndexer_.size(), 0);
      maxTrLink_.resize(typeCodeIndexer_.size(), 0);
      maxTCPerLink_.resize(typeCodeIndexer_.size(), 0);
    }

    size_t idx = typeCodeIndexer_[typecode];

    /*High density modules have links {0, 2} and low {0, 1, 2, 3} so to make it work I need to divide by 2*/
    if (typecode[1]=='H') trLink/=2; 

    maxROC_[idx] = std::max(maxROC_[idx], static_cast<uint16_t>(ROC+1));
    maxTrLink_[idx] = std::max(maxTrLink_[idx], static_cast<uint16_t>(trLink+1));
    maxTCPerLink_[idx] = std::max(maxTCPerLink_[idx], static_cast<uint16_t>(trCell+1));
  }

  /**
     @short process the current list of type codes handled and updates the dense indexers
  */
  void update() {
    uint32_t n = typeCodeIndexer_.size();
    offsets_ = std::vector<uint32_t>(n, 0);
    di_ = std::vector<HGCalDenseIndexerBase>(n, HGCalDenseIndexerBase(3)); /* The indices are {ROC, trLink, TC}*/
    for (uint32_t idx = 0; idx < n; idx++) {
      uint16_t maxROCs = maxROC_[idx];
      uint16_t maxLinks = maxTrLink_[idx];
      uint16_t maxTCPerLink = maxTCPerLink_[idx];
      di_[idx].updateRanges({{maxROCs, maxLinks, maxTCPerLink}});
      if (idx < n - 1)
        offsets_[idx + 1] = di_[idx].getMaxIndex();
    }

    //accumulate the offsets in the array
    std::partial_sum(offsets_.begin(), offsets_.end(), offsets_.begin());
  }

  /**
     @short gets index given typecode string
   */
  size_t getEnumFromTypecode(std::string typecode) const {
    auto it = typeCodeIndexer_.find(typecode);
    if (it == typeCodeIndexer_.end())
      throw cms::Exception("ValueError") << " unable to find typecode=" << typecode << " in cell indexer";
    return it->second;
  }

  /**
     @short checks if there is a typecode corresponding to an index
   */
  std::string getTypecodeFromEnum(size_t idx) const {
    for (const auto& it : typeCodeIndexer_)
      if (it.second == idx)
        return it.first;
    throw cms::Exception("ValueError") << " unable to find typecode corresponding to idx=" << idx;
  }

  /**
     @short returns the dense indexer for a typecode
   */
  HGCalDenseIndexerBase getDenseIndexFor(std::string typecode) const {
    return getDenseIndexerFor(getEnumFromTypecode(typecode));
  }

  /**
     @short returns the dense indexer for a given internal index
  */
  HGCalDenseIndexerBase getDenseIndexerFor(size_t idx) const {
    if (idx >= di_.size())
      throw cms::Exception("ValueError") << " index requested for cell dense indexer (i=" << idx
                                         << ") is larger than allocated";
    return di_[idx];
  }

  /**
    @short builders for the dense index
  */
  uint16_t denseIndex(std::string typecode, uint32_t ROC, uint32_t trLink, uint32_t trCell) const {
    return denseIndex(getEnumFromTypecode(typecode), ROC, trLink, trCell);
  }
  uint32_t denseIndex(size_t idx, uint32_t ROC, uint32_t trLink, uint32_t trCell) const {
    return di_[idx].denseIndex({{ROC,trLink,trCell}}) + offsets_[idx];
  }
    /* OLD ##########
    uint32_t denseIndex(std::string typecode, uint32_t chip, uint32_t erx, uint32_t seq) const {
      return denseIndex(getEnumFromTypecode(typecode), chip, erx, seq);
    }
    uint32_t denseIndex(std::string typecode, uint32_t erx, uint32_t seq) const {
      return denseIndex(getEnumFromTypecode(typecode), erx, seq);
    }
    uint32_t denseIndex(size_t idx, uint32_t chip, uint32_t half, uint32_t seq) const {
      uint16_t erx = chip * maxHalfPerROC_ + half;
      return denseIndex(idx, erx, seq);
    }
    uint32_t denseIndex(size_t idx, uint32_t erx, uint32_t seq) const {
      return di_[idx].denseIndex({{erx, seq}}) + offsets_[idx];
    }
    ############## */

  /**
     @short decodes the dense index code
  */
  /* Only for Data, Trigger does not have elecId (?) ###########
  uint32_t elecIdFromIndex(uint32_t rtn, std::string typecode) const {
    return elecIdFromIndex(rtn, getEnumFromTypecode(typecode));
  }
  uint32_t elecIdFromIndex(uint32_t rtn, size_t idx) const {
    if (idx >= di_.size())
      throw cms::Exception("ValueError") << " index requested for cell dense indexer (i=" << idx
                                         << ") is larger than allocated";
    rtn -= offsets_[idx];
    auto rtn_codes = di_[idx].unpackDenseIndex(rtn); // {}
    return HGCalElectronicsId(false, 0, 0, 0, rtn_codes[0], rtn_codes[1]).raw();
  }
  ################################*/

  /**
     @short returns the max. dense index expected
   */
  uint32_t maxDenseIndex() const {
    size_t i = maxTrLink_.size();
    if (i == 0)
      return 0;
    return offsets_.back() + maxTrLink_.back() * maxTCPerLink_.back();
  }

  /**
     @short gets the number of words (cells) for a given typecode 
     TODO:(?) For the partials it will give a number rounded to the closest multiple of 16/8 if M[L/H],
     ask if this needs correcting or if it is acceptable
     e.g.: ML-T has 22 TCs but this will return 32 or MH-T has 19 but it will return 24
  */
  size_t getNWordsExpectedFor(std::string typecode) const {
    auto it = getEnumFromTypecode(typecode);
    return getNWordsExpectedFor(it);
  }
  size_t getNWordsExpectedFor(size_t typecodeidx) const { return getDenseIndexerFor(typecodeidx).getMaxIndex(); }

  /**
     @short gets the number of Trigger Links for a given typecode
  */
  size_t getNTrLinkExpectedFor(std::string typecode) const {
    auto it = getEnumFromTypecode(typecode);
    return getNTrLinkExpectedFor(it);
  }
  size_t getNTrLinkExpectedFor(size_t typecodeidx) const { return maxTrLink_[typecodeidx]*maxROC_[typecodeidx]; }

  constexpr static char maxHalfPerROC_ = 2;
  //constexpr static uint16_t maxChPerErx_ = 37;  //36 channels + 1 calib
  constexpr static uint16_t maxTCPerTrLink_ = 4; // 4 TC per TrLink

  std::map<std::string, size_t> typeCodeIndexer_;
  std::vector<uint16_t> maxROC_;
  std::vector<uint16_t> maxTrLink_;
  std::vector<uint16_t> maxTCPerLink_;
  std::vector<uint32_t> offsets_;
  std::vector<HGCalDenseIndexerBase> di_;

  ~HGCalMappingCellIndexerTrigger() {}

  COND_SERIALIZABLE;
};

#endif