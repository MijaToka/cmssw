#include <unordered_map>

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "DataFormats/L1TParticleFlow/interface/PFCandidate.h"
#include "DataFormats/L1TParticleFlow/interface/PFCluster.h"

#include "L1Trigger/Phase2L1ParticleFlow/interface/deregionizer/deregionizer_input.h"
#include "L1Trigger/Phase2L1ParticleFlow/interface/deregionizer/deregionizer_ref.h"

class DeregionizerProducer : public edm::stream::EDProducer<> {
public:
  explicit DeregionizerProducer(const edm::ParameterSet &);
  ~DeregionizerProducer() override;
  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions);

private:
  edm::ParameterSet config_;
  edm::EDGetTokenT<l1t::PFCandidateRegionalOutput> token_;
  std::vector<edm::ParameterSet> linkConfigs_;
  const unsigned int nInputFramesPerBX_;
  l1ct::DeregionizerEmulator emulator_;
  l1ct::DeregionizerInput input_;
  std::vector<uint32_t> boardOrder_, nOutputFramesPerBX_, nPuppiFramesPerRegion_, nLinksPuppi_, nPuppiPerRegion_;
  std::vector<std::vector<uint32_t>> outputRegions_;
  const unsigned int tmuxFactor_ = 6;  // not really configurable in current architecture

  void produce(edm::Event &, const edm::EventSetup &) override;
  void hwToEdm_(const std::vector<l1ct::PuppiObjEmu> &hwOut, std::vector<l1t::PFCandidate> &edmOut) const;
  void setRefs_(l1t::PFCandidate &pf, const l1ct::PuppiObjEmu &p) const;
};

DeregionizerProducer::DeregionizerProducer(const edm::ParameterSet &iConfig)
    : config_(iConfig),
      token_(consumes<l1t::PFCandidateRegionalOutput>(iConfig.getParameter<edm::InputTag>("RegionalPuppiCands"))),
      linkConfigs_(iConfig.getParameter<std::vector<edm::ParameterSet>>("linkConfigs")),
      nInputFramesPerBX_(iConfig.getParameter<uint32_t>("nInputFramesPerBX")),
      emulator_(iConfig),
      input_(linkConfigs_) {
  produces<l1t::PFCandidateCollection>("Puppi");
  produces<l1t::PFCandidateCollection>("TruncatedPuppi");
}

DeregionizerProducer::~DeregionizerProducer() {}

void DeregionizerProducer::produce(edm::Event &iEvent, const edm::EventSetup &iSetup) {
  auto deregColl = std::make_unique<l1t::PFCandidateCollection>();
  auto truncColl = std::make_unique<l1t::PFCandidateCollection>();

  edm::Handle<l1t::PFCandidateRegionalOutput> src;

  iEvent.getByToken(token_, src);

  std::vector<l1ct::OutputRegion> outputRegions;
  std::vector<l1ct::PuppiObjEmu> hwOut;
  std::vector<l1t::PFCandidate> edmOut;
  std::vector<l1ct::PuppiObjEmu> hwTruncOut;
  std::vector<l1t::PFCandidate> edmTruncOut;

  LogDebug("DeregionizerProducer") << "\nRegional Puppi Candidates";
  for (unsigned int iReg = 0, nReg = src->nRegions(); iReg < nReg; ++iReg) {
    l1ct::OutputRegion tempOutputRegion;

    auto region = src->region(iReg);
    float eta = src->eta(iReg);
    float phi = src->phi(iReg);
    LogDebug("DeregionizerProducer") << "\nRegion " << iReg << "\n"
                                     << "Eta = " << eta << " and Phi = " << phi << "\n"
                                     << "###########";
    for (int i = 0, n = region.size(); i < n; ++i) {
      l1ct::PuppiObjEmu tempPuppi;
      const l1t::PFCandidate &cand = region[i];

      tempPuppi.initFromBits(cand.encodedPuppi64());
      tempPuppi.srcCand = &cand;
      tempOutputRegion.puppi.push_back(tempPuppi);
      LogDebug("DeregionizerProducer") << "pt[" << i << "] = " << tempOutputRegion.puppi.back().hwPt << ", eta[" << i
                                       << "] = " << tempOutputRegion.puppi.back().floatEta() << ", phi[" << i
                                       << "] = " << tempOutputRegion.puppi.back().floatPhi();
    }
    outputRegions.push_back(tempOutputRegion);
  }

  std::vector<std::vector<std::vector<l1ct::PuppiObjEmu>>> layer2In = input_.orderInputs(outputRegions);

  emulator_.run(layer2In, hwOut, hwTruncOut);

  DeregionizerProducer::hwToEdm_(hwOut, edmOut);
  DeregionizerProducer::hwToEdm_(hwTruncOut, edmTruncOut);

  deregColl->swap(edmOut);
  truncColl->swap(edmTruncOut);

  iEvent.put(std::move(deregColl), "Puppi");
  iEvent.put(std::move(truncColl), "TruncatedPuppi");
}

void DeregionizerProducer::hwToEdm_(const std::vector<l1ct::PuppiObjEmu> &hwOut,
                                    std::vector<l1t::PFCandidate> &edmOut) const {
  for (const auto &hwPuppi : hwOut) {
    l1t::PFCandidate::ParticleType type;
    float mass = 0.13f;
    if (hwPuppi.hwId.charged()) {
      if (hwPuppi.hwId.isMuon()) {
        type = l1t::PFCandidate::Muon;
        mass = 0.105;
      } else if (hwPuppi.hwId.isElectron()) {
        type = l1t::PFCandidate::Electron;
        mass = 0.005;
      } else
        type = l1t::PFCandidate::ChargedHadron;
    } else {
      type = hwPuppi.hwId.isPhoton() ? l1t::PFCandidate::Photon : l1t::PFCandidate::NeutralHadron;
      mass = hwPuppi.hwId.isPhoton() ? 0.0 : 0.5;
    }
    reco::Particle::PolarLorentzVector p4(hwPuppi.floatPt(), hwPuppi.floatEta(), hwPuppi.floatPhi(), mass);
    edmOut.emplace_back(
        type, hwPuppi.intCharge(), p4, hwPuppi.floatPuppiW(), hwPuppi.intPt(), hwPuppi.intEta(), hwPuppi.intPhi());
    if (hwPuppi.hwId.charged()) {
      edmOut.back().setZ0(hwPuppi.floatZ0());
      edmOut.back().setDxy(hwPuppi.floatDxy());
      edmOut.back().setHwZ0(hwPuppi.hwZ0());
      edmOut.back().setHwDxy(hwPuppi.hwDxy());
      edmOut.back().setHwTkQuality(hwPuppi.hwTkQuality());
    } else {
      edmOut.back().setHwPuppiWeight(hwPuppi.hwPuppiW());
      edmOut.back().setHwEmID(hwPuppi.hwEmID());
    }
    edmOut.back().setEncodedPuppi64(hwPuppi.pack().to_uint64());
    edmOut.back().setCaloPtr(hwPuppi.srcCand->caloPtr());
    edmOut.back().setPFTrack(hwPuppi.srcCand->pfTrack());
    edmOut.back().setMuon(hwPuppi.srcCand->muon());
  }
}

void DeregionizerProducer::fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("RegionalPuppiCands", edm::InputTag("l1tLayer1", "PuppiRegional"));
  desc.add<unsigned int>("nPuppiFinalBuffer", 128);
  desc.add<unsigned int>("nPuppiPerClk", 6);
  desc.add<unsigned int>("nPuppiFirstBuffers", 12);
  desc.add<unsigned int>("nPuppiSecondBuffers", 32);
  desc.add<unsigned int>("nPuppiThirdBuffers", 64);
  desc.add<unsigned int>("nInputFramesPerBX", 9);
  edm::ParameterSetDescription linkConfigDummyValidator;
  linkConfigDummyValidator.setAllowAnything();
  desc.addVPSet("linkConfigs", linkConfigDummyValidator);
  descriptions.add("DeregionizerProducer", desc);
}

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(DeregionizerProducer);
