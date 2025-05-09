// -*- C++ -*-
//
// Package:     FWCore/Framework
// Class  :     EDConsumerBase
//
// Implementation:
//     [Notes on implementation]
//
// Original Author:  Chris Jones
//         Created:  Tue, 02 Apr 2013 21:36:06 GMT
//

// system include files
#include <algorithm>
#include <cstring>
#include <set>
#include <string_view>

// user include files
#include "DataFormats/Provenance/interface/BranchType.h"
#include "FWCore/Framework/interface/EDConsumerBase.h"
#include "FWCore/Framework/interface/ConsumesCollector.h"
#include "FWCore/Framework/interface/ESRecordsToProductResolverIndices.h"
#include "FWCore/Framework/interface/ComponentDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ServiceRegistry/interface/ModuleConsumesInfo.h"
#include "FWCore/ServiceRegistry/interface/ModuleConsumesESInfo.h"
#include "FWCore/Utilities/interface/Likely.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "DataFormats/Provenance/interface/ProductResolverIndexHelper.h"
#include "DataFormats/Provenance/interface/ProductRegistry.h"

using namespace edm;

namespace {
  std::vector<char> makeEmptyTokenLabels() { return std::vector<char>{'\0'}; }
}  // namespace

EDConsumerBase::EDConsumerBase()
    : m_tokenLabels{makeEmptyTokenLabels()},
      esDataThatCanBeDeletedEarly_(std::make_unique<ESDataThatCanBeDeletedEarly>()),
      frozen_(false),
      containsCurrentProcessAlias_(false) {}

EDConsumerBase::~EDConsumerBase() noexcept(false) {}

//
// member functions
//
ConsumesCollector EDConsumerBase::consumesCollector() {
  ConsumesCollector c{this};
  return c;
}

static const edm::InputTag kWasEmpty("@EmptyLabel@");

edm::InputTag const& EDConsumerBase::checkIfEmpty(edm::InputTag const& iTag) {
  if (iTag.label().empty()) {
    return kWasEmpty;
  }
  return iTag;
}

unsigned int EDConsumerBase::recordConsumes(BranchType iBranch,
                                            TypeToGet const& iType,
                                            edm::InputTag const& iTag,
                                            bool iAlwaysGets) {
  if (frozen_) {
    throwConsumesCallAfterFrozen(iType, iTag);
  }

  unsigned int index = m_tokenInfo.size();

  bool skipCurrentProcess = iTag.willSkipCurrentProcess();

  const size_t labelSize = iTag.label().size();
  const size_t productInstanceSize = iTag.instance().size();
  unsigned int labelStart = m_tokenLabels.size();
  unsigned short delta1 = labelSize + 1;
  unsigned short delta2 = labelSize + 2 + productInstanceSize;
  m_tokenInfo.emplace_back(TokenLookupInfo{iType.type(), ProductResolverIndexInvalid, skipCurrentProcess, iBranch},
                           iAlwaysGets,
                           LabelPlacement{labelStart, delta1, delta2},
                           iType.kind());

  const size_t additionalSize = skipCurrentProcess ? labelSize + productInstanceSize + 3
                                                   : labelSize + productInstanceSize + iTag.process().size() + 3;

  m_tokenLabels.reserve(m_tokenLabels.size() + additionalSize);
  {
    const std::string& m = iTag.label();
    m_tokenLabels.insert(m_tokenLabels.end(), m.begin(), m.end());
    m_tokenLabels.push_back('\0');
  }
  {
    const std::string& m = iTag.instance();
    m_tokenLabels.insert(m_tokenLabels.end(), m.begin(), m.end());
    m_tokenLabels.push_back('\0');
  }
  {
    const std::string& m = iTag.process();
    if (m == InputTag::kCurrentProcess) {
      containsCurrentProcessAlias_ = true;
    }
    if (!skipCurrentProcess) {
      m_tokenLabels.insert(m_tokenLabels.end(), m.begin(), m.end());
      m_tokenLabels.push_back('\0');
    } else {
      m_tokenLabels.push_back('\0');
    }
  }
  return index;
}

void EDConsumerBase::extendUpdateLookup(BranchType, ProductResolverIndexHelper const&) {}

void EDConsumerBase::updateLookup(BranchType iBranchType,
                                  ProductResolverIndexHelper const& iHelper,
                                  bool iPrefetchMayGet) {
  frozen_ = true;
  assert(!containsCurrentProcessAlias_);
  extendUpdateLookup(iBranchType, iHelper);
  {
    auto itKind = m_tokenInfo.begin<kKind>();
    auto itLabels = m_tokenInfo.begin<kLabels>();
    for (auto itInfo = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); itInfo != itEnd;
         ++itInfo, ++itKind, ++itLabels) {
      if (itInfo->m_branchType == iBranchType) {
        const unsigned int labelStart = itLabels->m_startOfModuleLabel;
        const char* moduleLabel = &(m_tokenLabels[labelStart]);
        itInfo->m_index = ProductResolverIndexAndSkipBit(iHelper.index(*itKind,
                                                                       itInfo->m_type,
                                                                       moduleLabel,
                                                                       moduleLabel + itLabels->m_deltaToProductInstance,
                                                                       moduleLabel + itLabels->m_deltaToProcessName),
                                                         itInfo->m_index.skipCurrentProcess());
      }
    }
  }

  m_tokenInfo.shrink_to_fit();

  itemsToGet(iBranchType, itemsToGetFromBranch_[iBranchType]);
  if (iPrefetchMayGet) {
    itemsMayGet(iBranchType, itemsToGetFromBranch_[iBranchType]);
  }
}

void EDConsumerBase::updateLookup(eventsetup::ESRecordsToProductResolverIndices const& iPI) {
  // temporarily unfreeze to allow late EventSetup consumes registration
  frozen_ = false;
  registerLateConsumes(iPI);
  frozen_ = true;

  unsigned int index = 0;
  for (auto it = esTokenLookupInfoContainer().begin<kESLookupInfo>();
       it != esTokenLookupInfoContainer().end<kESLookupInfo>();
       ++it, ++index) {
    auto indexInRecord = iPI.indexInRecord(it->m_record, it->m_key);
    if (indexInRecord != ESResolverIndex::noResolverConfigured()) {
      const char* componentName = &(m_tokenLabels[it->m_startOfComponentName]);
      if (*componentName) {
        auto component = iPI.component(it->m_record, it->m_key);
        if (component->label_.empty()) {
          if (component->type_ != componentName) {
            indexInRecord = ESResolverIndex::moduleLabelDoesNotMatch();
          }
        } else if (component->label_ != componentName) {
          indexInRecord = ESResolverIndex::moduleLabelDoesNotMatch();
        }
      }
    }
    esDataThatCanBeDeletedEarly_->esTokenLookupInfoContainer_.get<kESResolverIndex>(index) = indexInRecord;

    int negIndex = -1 * (index + 1);
    for (auto& items : esItemsToGetFromTransition_) {
      for (auto& itemIndex : items) {
        if (itemIndex.value() == negIndex) {
          itemIndex = indexInRecord;
          ESResolverIndexContainer::size_type transitionIndex = &items - &esItemsToGetFromTransition_.front();
          std::vector<ESResolverIndex>::size_type indexToItemInTransition = &itemIndex - &items.front();
          esRecordsToGetFromTransition_[transitionIndex][indexToItemInTransition] = iPI.recordIndexFor(it->m_record);
          esDataThatCanBeDeletedEarly_->consumesIndexConverter_.emplace_back(transitionIndex, indexToItemInTransition);
          negIndex = 1;
          break;
        }
      }
      if (negIndex > 0) {
        break;
      }
    }
  }
}

void EDConsumerBase::releaseMemoryPostLookupSignal() { esDataThatCanBeDeletedEarly_.reset(); }

std::tuple<ESTokenIndex, char const*> EDConsumerBase::recordESConsumes(
    Transition iTrans,
    eventsetup::EventSetupRecordKey const& iRecord,
    eventsetup::heterocontainer::HCTypeTag const& iDataType,
    edm::ESInputTag const& iTag) {
  if (frozen_) {
    throwESConsumesCallAfterFrozen(iRecord, iDataType, iTag);
  }

  //m_tokenLabels first entry is a null. Since most ES data requests have
  // empty labels we will assume we can reuse the first entry
  unsigned int startOfComponentName = 0;
  if (not iTag.module().empty()) {
    startOfComponentName = m_tokenLabels.size();

    m_tokenLabels.reserve(m_tokenLabels.size() + iTag.module().size() + 1);
    {
      const std::string& m = iTag.module();
      m_tokenLabels.insert(m_tokenLabels.end(), m.begin(), m.end());
      m_tokenLabels.push_back('\0');
    }
  }

  auto index = static_cast<ESResolverIndex::Value_t>(esTokenLookupInfoContainer().size());
  esDataThatCanBeDeletedEarly_->esTokenLookupInfoContainer_.emplace_back(
      ESTokenLookupInfo{iRecord, eventsetup::DataKey{iDataType, iTag.data().c_str()}, startOfComponentName},
      ESResolverIndex{-1});
  if (iTrans >= edm::Transition::NumberOfEventSetupTransitions) {
    throwESConsumesInProcessBlock();
  }
  auto indexForToken = esItemsToGetFromTransition_[static_cast<unsigned int>(iTrans)].size();
  esItemsToGetFromTransition_[static_cast<unsigned int>(iTrans)].emplace_back(-1 * (index + 1));
  esRecordsToGetFromTransition_[static_cast<unsigned int>(iTrans)].emplace_back();
  return {ESTokenIndex{static_cast<ESTokenIndex::Value_t>(indexForToken)},
          esTokenLookupInfoContainer().get<kESLookupInfo>(index).m_key.name().value()};
}

//
// const member functions
//
ProductResolverIndexAndSkipBit EDConsumerBase::indexFrom(EDGetToken iToken,
                                                         BranchType iBranch,
                                                         TypeID const& iType) const {
  if (UNLIKELY(iToken.index() >= m_tokenInfo.size())) {
    throwBadToken(iType, iToken);
  }
  const auto& info = m_tokenInfo.get<kLookupInfo>(iToken.index());
  if (LIKELY(iBranch == info.m_branchType)) {
    if (LIKELY(iType == info.m_type)) {
      return info.m_index;
    } else {
      throwTypeMismatch(iType, iToken);
    }
  } else {
    throwBranchMismatch(iBranch, iToken);
  }
  return ProductResolverIndexAndSkipBit(edm::ProductResolverIndexInvalid, false);
}

ProductResolverIndexAndSkipBit EDConsumerBase::uncheckedIndexFrom(EDGetToken iToken) const {
  return m_tokenInfo.get<kLookupInfo>(iToken.index()).m_index;
}

void EDConsumerBase::itemsToGet(BranchType iBranch, std::vector<ProductResolverIndexAndSkipBit>& oIndices) const {
  //how many are we adding?
  unsigned int count = 0;
  {
    auto itAlwaysGet = m_tokenInfo.begin<kAlwaysGets>();
    for (auto it = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); it != itEnd;
         ++it, ++itAlwaysGet) {
      if (iBranch == it->m_branchType) {
        if (it->m_index.productResolverIndex() != ProductResolverIndexInvalid) {
          if (*itAlwaysGet) {
            ++count;
          }
        }
      }
    }
  }
  oIndices.reserve(oIndices.size() + count);
  {
    auto itAlwaysGet = m_tokenInfo.begin<kAlwaysGets>();
    for (auto it = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); it != itEnd;
         ++it, ++itAlwaysGet) {
      if (iBranch == it->m_branchType) {
        if (it->m_index.productResolverIndex() != ProductResolverIndexInvalid) {
          if (*itAlwaysGet) {
            oIndices.push_back(it->m_index);
          }
        }
      }
    }
  }
}

void EDConsumerBase::itemsMayGet(BranchType iBranch, std::vector<ProductResolverIndexAndSkipBit>& oIndices) const {
  //how many are we adding?
  unsigned int count = 0;
  {
    auto itAlwaysGet = m_tokenInfo.begin<kAlwaysGets>();
    for (auto it = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); it != itEnd;
         ++it, ++itAlwaysGet) {
      if (iBranch == it->m_branchType) {
        if (it->m_index.productResolverIndex() != ProductResolverIndexInvalid) {
          if (not *itAlwaysGet) {
            ++count;
          }
        }
      }
    }
  }
  oIndices.reserve(oIndices.size() + count);
  {
    auto itAlwaysGet = m_tokenInfo.begin<kAlwaysGets>();
    for (auto it = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); it != itEnd;
         ++it, ++itAlwaysGet) {
      if (iBranch == it->m_branchType) {
        if (it->m_index.productResolverIndex() != ProductResolverIndexInvalid) {
          if (not *itAlwaysGet) {
            oIndices.push_back(it->m_index);
          }
        }
      }
    }
  }
}

void EDConsumerBase::labelsForToken(EDGetToken iToken, Labels& oLabels) const {
  unsigned int index = iToken.index();
  auto labels = m_tokenInfo.get<kLabels>(index);
  unsigned int start = labels.m_startOfModuleLabel;
  oLabels.module = &(m_tokenLabels[start]);
  oLabels.productInstance = oLabels.module + labels.m_deltaToProductInstance;
  oLabels.process = oLabels.module + labels.m_deltaToProcessName;
}

bool EDConsumerBase::registeredToConsume(ProductResolverIndex iIndex,
                                         bool skipCurrentProcess,
                                         BranchType iBranch) const {
  for (auto it = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); it != itEnd; ++it) {
    if (it->m_index.productResolverIndex() == iIndex and it->m_index.skipCurrentProcess() == skipCurrentProcess and
        it->m_branchType == iBranch) {
      return true;
    }
  }
  return false;
}

void EDConsumerBase::throwTypeMismatch(edm::TypeID const& iType, EDGetToken iToken) const {
  throw cms::Exception("TypeMismatch") << "A get using a EDGetToken used the C++ type '" << iType.className()
                                       << "' but the consumes call was for type '"
                                       << m_tokenInfo.get<kLookupInfo>(iToken.index()).m_type.className()
                                       << "'.\n Please modify either the consumes or get call so the types match.";
}
void EDConsumerBase::throwBranchMismatch(BranchType iBranch, EDGetToken iToken) const {
  throw cms::Exception("BranchTypeMismatch")
      << "A get using a EDGetToken was done in " << BranchTypeToString(iBranch) << " but the consumes call was for "
      << BranchTypeToString(m_tokenInfo.get<kLookupInfo>(iToken.index()).m_branchType)
      << ".\n Please modify the consumes call to use the correct branch type.";
}

void EDConsumerBase::throwBadToken(edm::TypeID const& iType, EDGetToken iToken) const {
  if (iToken.isUninitialized()) {
    throw cms::Exception("BadToken") << "A get using a EDGetToken with the C++ type '" << iType.className()
                                     << "' was made using an uninitialized token.\n Please check that the variable is "
                                        "being initialized from a 'consumes' call.";
  }
  throw cms::Exception("BadToken")
      << "A get using a EDGetToken with the C++ type '" << iType.className() << "' was made using a token with a value "
      << iToken.index()
      << " which is beyond the range used by this module.\n Please check that the variable is being initialized from a "
         "'consumes' call from this module.\n You can not share EDGetToken values between modules.";
}

void EDConsumerBase::throwConsumesCallAfterFrozen(TypeToGet const& typeToGet, InputTag const& inputTag) const {
  throw cms::Exception("LogicError") << "A module declared it consumes a product after its constructor.\n"
                                     << "This must be done in the contructor\n"
                                     << "The product type was: " << typeToGet.type() << "\n"
                                     << "and " << inputTag << "\n";
}

void EDConsumerBase::throwESConsumesCallAfterFrozen(eventsetup::EventSetupRecordKey const& iRecord,
                                                    eventsetup::heterocontainer::HCTypeTag const& iDataType,
                                                    edm::ESInputTag const& iTag) const {
  throw cms::Exception("LogicError") << "A module declared it consumes an EventSetup product after its constructor.\n"
                                     << "This must be done in the contructor\n"
                                     << "The product type was: " << iDataType.name() << " in record "
                                     << iRecord.type().name() << "\n"
                                     << "and ESInputTag was " << iTag << "\n";
}

void EDConsumerBase::throwESConsumesInProcessBlock() const {
  throw cms::Exception("LogicError")
      << "A module declared it consumes an EventSetup product during a ProcessBlock transition.\n"
      << "EventSetup products can only be consumed in Event, Lumi, or Run transitions.\n";
}

void EDConsumerBase::doSelectInputProcessBlocks(ProductRegistry const&, ProcessBlockHelperBase const&) {}

namespace {
  struct CharStarComp {
    bool operator()(const char* iLHS, const char* iRHS) const { return strcmp(iLHS, iRHS) < 0; }
  };
}  // namespace

namespace {
  void insertFoundModuleLabel(edm::KindOfType consumedTypeKind,
                              edm::TypeID consumedType,
                              const char* consumedModuleLabel,
                              const char* consumedProductInstance,
                              std::vector<ModuleDescription const*>& modules,
                              std::set<std::string>& alreadyFound,
                              std::map<std::string, ModuleDescription const*> const& labelsToDesc,
                              ProductRegistry const& preg) {
    // Convert from label string to module description, eliminate duplicates,
    // then insert into the vector of modules
    if (auto it = labelsToDesc.find(consumedModuleLabel); it != labelsToDesc.end()) {
      if (alreadyFound.insert(consumedModuleLabel).second) {
        modules.push_back(it->second);
      }
      return;
    }
    // Deal with EDAlias's by converting to the original module label first
    if (auto aliasToModuleLabels =
            preg.aliasToModules(consumedTypeKind, consumedType, consumedModuleLabel, consumedProductInstance);
        not aliasToModuleLabels.empty()) {
      bool foundInLabelsToDesc = false;
      for (auto const& label : aliasToModuleLabels) {
        if (auto it = labelsToDesc.find(label); it != labelsToDesc.end()) {
          if (alreadyFound.insert(label).second) {
            modules.push_back(it->second);
          }
          foundInLabelsToDesc = true;
        } else {
          if (label == "source") {
            foundInLabelsToDesc = true;
          }
        }
      }
      if (foundInLabelsToDesc) {
        return;
      }
    }
    // Ignore the source products, we are only interested in module products.
    // As far as I know, it should never be anything else so throw if something
    // unknown gets passed in.
    if (std::string_view(consumedModuleLabel) != "source") {
      throw cms::Exception("EDConsumerBase", "insertFoundModuleLabel")
          << "Couldn't find ModuleDescription for the consumed product type: '" << consumedType.className()
          << "' module label: '" << consumedModuleLabel << "' product instance name: '" << consumedProductInstance
          << "'";
    }
  }
}  // namespace

void EDConsumerBase::modulesWhoseProductsAreConsumed(
    std::array<std::vector<ModuleDescription const*>*, NumBranchTypes>& modulesAll,
    ProductRegistry const& preg,
    std::map<std::string, ModuleDescription const*> const& labelsToDesc,
    std::string const& processName) const {
  std::set<std::string> alreadyFound;

  auto itKind = m_tokenInfo.begin<kKind>();
  auto itLabels = m_tokenInfo.begin<kLabels>();
  for (auto itInfo = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); itInfo != itEnd;
       ++itInfo, ++itKind, ++itLabels) {
    ProductResolverIndexHelper const& helper = *preg.productLookup(itInfo->m_branchType);
    std::vector<ModuleDescription const*>& modules = *modulesAll[itInfo->m_branchType];

    const unsigned int labelStart = itLabels->m_startOfModuleLabel;
    const char* const consumedModuleLabel = &(m_tokenLabels[labelStart]);
    const char* const consumedProductInstance = consumedModuleLabel + itLabels->m_deltaToProductInstance;
    const char* const consumedProcessName = consumedModuleLabel + itLabels->m_deltaToProcessName;

    if (not itInfo->m_index.skipCurrentProcess()) {
      assert(*consumedModuleLabel != '\0');  // consumesMany used to create empty labels before we removed consumesMany
      if (*consumedProcessName != '\0') {    // process name is specified in consumes call
        if (helper.index(*itKind, itInfo->m_type, consumedModuleLabel, consumedProductInstance, consumedProcessName) !=
            ProductResolverIndexInvalid) {
          if (processName == consumedProcessName) {
            insertFoundModuleLabel(*itKind,
                                   itInfo->m_type,
                                   consumedModuleLabel,
                                   consumedProductInstance,
                                   modules,
                                   alreadyFound,
                                   labelsToDesc,
                                   preg);
          }
        }
      } else {  // process name was empty
        auto matches = helper.relatedIndexes(*itKind, itInfo->m_type, consumedModuleLabel, consumedProductInstance);
        for (unsigned int j = 0; j < matches.numberOfMatches(); ++j) {
          if (processName == matches.processName(j)) {
            insertFoundModuleLabel(*itKind,
                                   itInfo->m_type,
                                   consumedModuleLabel,
                                   consumedProductInstance,
                                   modules,
                                   alreadyFound,
                                   labelsToDesc,
                                   preg);
          }
        }
      }
    }
  }
}

void EDConsumerBase::esModulesWhoseProductsAreConsumed(
    std::array<std::vector<eventsetup::ComponentDescription const*>*, kNumberOfEventSetupTransitions>& esModules,
    eventsetup::ESRecordsToProductResolverIndices const& iPI) const {
  std::array<std::set<std::string>, kNumberOfEventSetupTransitions> alreadyFound;

  unsigned int index = 0;
  auto itResolver = esTokenLookupInfoContainer().begin<kESResolverIndex>();
  for (auto it = esTokenLookupInfoContainer().begin<kESLookupInfo>();
       it != esTokenLookupInfoContainer().end<kESLookupInfo>();
       ++it, ++itResolver, ++index) {
    auto [componentDescription, produceMethodID] = iPI.componentAndProduceMethodID(it->m_record, *itResolver);
    if (componentDescription) {
      std::string const& moduleLabel =
          componentDescription->label_.empty() ? componentDescription->type_ : componentDescription->label_;
      ESResolverIndexContainer::size_type transitionIndex = consumesIndexConverter()[index].first;
      if (alreadyFound[transitionIndex].insert(moduleLabel).second) {
        esModules[transitionIndex]->push_back(componentDescription);
      }
    }
  }
}

void EDConsumerBase::convertCurrentProcessAlias(std::string const& processName) {
  frozen_ = true;

  if (containsCurrentProcessAlias_) {
    containsCurrentProcessAlias_ = false;

    auto newTokenLabels = makeEmptyTokenLabels();

    // first calculate the size of the new vector and reserve memory for it
    std::vector<char>::size_type newSize = newTokenLabels.size();
    std::string newProcessName;
    for (auto iter = m_tokenInfo.begin<kLabels>(), itEnd = m_tokenInfo.end<kLabels>(); iter != itEnd; ++iter) {
      newProcessName = &m_tokenLabels[iter->m_startOfModuleLabel + iter->m_deltaToProcessName];
      if (newProcessName == InputTag::kCurrentProcess) {
        newProcessName = processName;
      }
      newSize += (iter->m_deltaToProcessName + newProcessName.size() + 1);
    }
    newTokenLabels.reserve(newSize);

    unsigned int newStartOfModuleLabel = newTokenLabels.size();
    for (auto iter = m_tokenInfo.begin<kLabels>(), itEnd = m_tokenInfo.end<kLabels>(); iter != itEnd; ++iter) {
      unsigned int startOfModuleLabel = iter->m_startOfModuleLabel;
      unsigned short deltaToProcessName = iter->m_deltaToProcessName;

      iter->m_startOfModuleLabel = newStartOfModuleLabel;

      newProcessName = &m_tokenLabels[startOfModuleLabel + deltaToProcessName];
      if (newProcessName == InputTag::kCurrentProcess) {
        newProcessName = processName;
      }

      newStartOfModuleLabel += (deltaToProcessName + newProcessName.size() + 1);

      // Copy in both the module label and instance, they are the same
      newTokenLabels.insert(newTokenLabels.end(),
                            m_tokenLabels.begin() + startOfModuleLabel,
                            m_tokenLabels.begin() + (startOfModuleLabel + deltaToProcessName));

      newTokenLabels.insert(newTokenLabels.end(), newProcessName.begin(), newProcessName.end());
      newTokenLabels.push_back('\0');
    }
    m_tokenLabels = std::move(newTokenLabels);
  }
}

std::vector<ModuleConsumesInfo> EDConsumerBase::moduleConsumesInfos() const {
  std::vector<ModuleConsumesInfo> result;
  auto itAlways = m_tokenInfo.begin<kAlwaysGets>();
  auto itKind = m_tokenInfo.begin<kKind>();
  auto itLabels = m_tokenInfo.begin<kLabels>();
  for (auto itInfo = m_tokenInfo.begin<kLookupInfo>(), itEnd = m_tokenInfo.end<kLookupInfo>(); itInfo != itEnd;
       ++itInfo, ++itKind, ++itLabels, ++itAlways) {
    const unsigned int labelStart = itLabels->m_startOfModuleLabel;
    const char* consumedModuleLabel = &(m_tokenLabels[labelStart]);
    const char* consumedInstance = consumedModuleLabel + itLabels->m_deltaToProductInstance;
    const char* consumedProcessName = consumedModuleLabel + itLabels->m_deltaToProcessName;

    assert(*consumedModuleLabel != '\0');

    // Just copy the information into the ModuleConsumesInfo data structure
    result.emplace_back(itInfo->m_type,
                        consumedModuleLabel,
                        consumedInstance,
                        consumedProcessName,
                        itInfo->m_branchType,
                        *itKind,
                        *itAlways,
                        itInfo->m_index.skipCurrentProcess());
  }
  return result;
}

std::vector<ModuleConsumesESInfo> EDConsumerBase::moduleConsumesESInfos(
    eventsetup::ESRecordsToProductResolverIndices const& iPI) const {
  std::vector<ModuleConsumesESInfo> result;

  ModuleConsumesESInfo info;

  unsigned int index = 0;
  auto itResolver = esTokenLookupInfoContainer().begin<kESResolverIndex>();
  for (auto it = esTokenLookupInfoContainer().begin<kESLookupInfo>();
       it != esTokenLookupInfoContainer().end<kESLookupInfo>();
       ++it, ++itResolver, ++index) {
    info.eventSetupRecordType_ = it->m_record.name();
    info.productType_ = it->m_key.type().name();
    info.productLabel_ = it->m_key.name().value();
    info.requestedModuleLabel_ = &(m_tokenLabels[it->m_startOfComponentName]);
    info.transitionOfConsumer_ = static_cast<Transition>(consumesIndexConverter()[index].first);
    info.moduleLabelMismatch_ = *itResolver == ESResolverIndex::moduleLabelDoesNotMatch();

    // Initial values used in the case where there isn't an EventSetup
    // module to produce the requested data. Test whether moduleType
    // is empty to identify this case because it will be empty if and
    // only if this is true.
    info.moduleType_ = {};
    info.moduleLabel_ = {};
    info.produceMethodIDOfProducer_ = 0;
    info.isSource_ = false;
    info.isLooper_ = false;

    auto [componentDescription, produceMethodID] = iPI.componentAndProduceMethodID(it->m_record, *itResolver);
    if (componentDescription) {
      info.moduleType_ = componentDescription->type_;
      info.moduleLabel_ =
          componentDescription->label_.empty() ? componentDescription->type_ : componentDescription->label_;
      info.produceMethodIDOfProducer_ = produceMethodID;
      info.isSource_ = componentDescription->isSource_;
      info.isLooper_ = componentDescription->isLooper_;
    }
    result.push_back(info);
  }
  return result;
}
