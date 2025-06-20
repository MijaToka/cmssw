#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/SourceFactory.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/ESProducer.h"
#include "FWCore/Framework/interface/ESProducts.h"
#include "FWCore/Framework/interface/EventSetupRecordIntervalFinder.h"
#include "FWCore/ParameterSet/interface/FileInPath.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/do_nothing_deleter.h"
#include "CondFormats/DataRecord/interface/HGCalElectronicsMappingRcd.h"

#include "CondFormats/HGCalObjects/interface/HGCalMappingModuleIndexerTrigger.h"
#include "CondFormats/HGCalObjects/interface/HGCalMappingCellIndexerTrigger.h"
#include "CondFormats/HGCalObjects/interface/HGCalMappingParameterHost.h"

#include "DataFormats/HGCalDigi/interface/HGCalElectronicsId.h"
#include "DataFormats/ForwardDetId/interface/HGCSiliconDetId.h"
#include "DataFormats/ForwardDetId/interface/HGCScintillatorDetId.h"
#include "Geometry/HGCalMapping/interface/HGCalMappingTools.h"
#include <regex>  // regular expression

/**
   @short plugin parses the module/cell locator files to produce the indexer records
 */
class HGCalMappingTriggerESProducer : public edm::ESProducer, public edm::EventSetupRecordIntervalFinder {
public:
  explicit HGCalMappingTriggerESProducer(const edm::ParameterSet& iConfig) {
    //parse the files and hold the list of entities in memory
    for (auto v : {"modules", "si", "sipm"}) {
      edm::FileInPath fip = iConfig.getParameter<edm::FileInPath>(v);
      hgcal::mappingtools::HGCalEntityList pmap;
      pmap.buildFrom(fip.fullPath());
      parsedMaps_[v] = pmap;
    }

    setWhatProduced(this, &HGCalMappingTriggerESProducer::produceCellMapIndexer);
    setWhatProduced(this, &HGCalMappingTriggerESProducer::produceModuleMapIndexer);

    findingRecord<HGCalElectronicsMappingRcd>();

    prepareCellMapperIndexer();
    prepareModuleMapperIndexer();
  }

  std::shared_ptr<HGCalMappingModuleIndexerTrigger> produceModuleMapIndexer(const HGCalElectronicsMappingRcd&) {
    return std::shared_ptr<HGCalMappingModuleIndexerTrigger>(&modIndexer_, edm::do_nothing_deleter());
  }

  std::shared_ptr<HGCalMappingCellIndexerTrigger> produceCellMapIndexer(const HGCalElectronicsMappingRcd&) {
    return std::shared_ptr<HGCalMappingCellIndexerTrigger>(&cellIndexer_, edm::do_nothing_deleter());
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::FileInPath>("modules")->setComment("module locator file");
    desc.add<edm::FileInPath>("si")->setComment("file containing the mapping of the readout cells in Si modules");
    desc.add<edm::FileInPath>("sipm")->setComment(
        "file containing the mapping of the readout cells in SiPM-on-tile modules");
    descriptions.addWithDefaultLabel(desc);
  }

private:
  void setIntervalFor(const edm::eventsetup::EventSetupRecordKey&,
                      const edm::IOVSyncValue&,
                      edm::ValidityInterval& oValidity) override {
    oValidity = edm::ValidityInterval(edm::IOVSyncValue::beginOfTime(), edm::IOVSyncValue::endOfTime());
  }

  void prepareCellMapperIndexer();
  void prepareModuleMapperIndexer();

  std::map<std::string, hgcal::mappingtools::HGCalEntityList> parsedMaps_;
  HGCalMappingCellIndexerTrigger cellIndexer_;
  HGCalMappingModuleIndexerTrigger modIndexer_;
};

//
void HGCalMappingTriggerESProducer::prepareCellMapperIndexer() {
  for (auto v : {"si", "sipm"}) {
    auto& pmap = parsedMaps_[v];
    const auto& entities = pmap.getEntries();
    for (auto row : entities) {
      std::string typecode = pmap.getAttr("Typecode", row);
      int ROC = pmap.getIntAttr("ROC", row);
      int trLink = pmap.getIntAttr("TrLink", row);
      int trCell = pmap.getIntAttr("TrCell", row);
      cellIndexer_.processNewCell(typecode, ROC, trLink, trCell);
    }
  }

  // all {hex,tile}board types are loaded finalize the mapping indexer
  cellIndexer_.update();
}

//
void HGCalMappingTriggerESProducer::prepareModuleMapperIndexer() {
  //default values to assign in case module type has not yet been mapped
  //a high density module (max possible) will be assigned so that the mapping doesn't block
  auto defaultTypeCodeIdx = cellIndexer_.getEnumFromTypecode("MH-F");
  auto typecodeidx = defaultTypeCodeIdx;
  auto defaultNTrLinks = cellIndexer_.getNTrLinkExpectedFor(defaultTypeCodeIdx);
  auto nTrLinks = defaultNTrLinks;
  auto defaultTypeNTCs = cellIndexer_.getNWordsExpectedFor(defaultTypeCodeIdx);
  auto nwords = defaultTypeNTCs;

  auto& pmap = parsedMaps_["modules"];
  auto& entities = pmap.getEntries();
  for (auto row : entities) {
    std::string typecode = pmap.getAttr("typecode", row);  // module type code
    std::string wtypecode;                                 // wafer type code

    // match module type code to regular expression pattern (MM-TTTT-LL-NNNN)
    // see https://edms.cern.ch/ui/#!master/navigator/document?D:101059405:101148061:subDocs
    //const std::regex typecode_regex("([MX])([LH])-([FTBLR5])([123])([WPC])-([A-Z]{2})-([0-9]{3,4})"); // MM-TTTT-LL-NNNN
    const std::regex typecode_regex("(([MX])([LH])-([FTBLR5])).*");  // MM-T*
    std::smatch typecode_match;                                      // match object for string objects
    bool matched = std::regex_match(typecode, typecode_match, typecode_regex);
    if (matched) {
      wtypecode = typecode_match[1].str();  // wafer type following MM-T pattern, e.g. "MH-F"
    } else {
      edm::LogWarning("HGCalMappingIndexESSource")
          << "Could not match module type code to expected pattern: " << typecode;
    }

    try {
      typecodeidx = cellIndexer_.getEnumFromTypecode(wtypecode);
      nTrLinks = cellIndexer_.getNTrLinkExpectedFor(wtypecode);
      nwords = cellIndexer_.getNWordsExpectedFor(wtypecode);
    } catch (cms::Exception& e) {
      int plane = pmap.getIntAttr("plane", row);
      int u = pmap.getIntAttr("u", row);
      int v = pmap.getIntAttr("v", row);
      edm::LogWarning("HGCalMappingTriggerESProducer") << "Exception caught decoding index for typecode=" << typecode
                                                << " @ plane=" << plane << " u=" << u << " v=" << v << "\n"
                                                << e.what() << "\n"
                                                << "===> will assign default (MH-F) which may be inefficient";
      typecodeidx = defaultTypeCodeIdx;
      nTrLinks = defaultNTrLinks;
      nwords = defaultTypeNTCs;
    }

    int fedid = pmap.getIntAttr("fedid", row);
    //int captureblockidx = pmap.getIntAttr("captureblockidx", row);
    int econtidx = pmap.getIntAttr("econtidx", row);
    modIndexer_.processNewModule(fedid, econtidx, typecodeidx, nTrLinks, nwords, typecode);
  }

  modIndexer_.finalize();
}

DEFINE_FWK_EVENTSETUP_SOURCE(HGCalMappingTriggerESProducer);