#ifndef FWCore_Common_ProcessBlockHelper_h
#define FWCore_Common_ProcessBlockHelper_h

/** \class edm::ProcessBlockHelper

\author W. David Dagenhart, created 15 September, 2020

Historical Note: The code related to ProcessBlock was originally written
to support SubProcesses being present in the job. The SubProcess feature
was removed from the CMS Framework in 2025. This support for SubProcesses
added complexity in the ProcessBlock code that is currently unnecessary.
For example, it contains counters that are never greater than one, vectors
that will never have more than one entry, the concept of the top ProcessBlockHelper
when there will only ever be one... There are a couple reasons we didn't clean
this up in 2025:

    1. We are considering implementing a new feature similar to SubProcess,
    but better. Hopefully, something that will be used.  The new feature might
    make use of the ability of the ProcessBlock code to handle multiple
    processes in the same job. We're not sure when this will happen or if
    the current version of the code will be useful, but we're keeping it just
    in case.

    2. The current version of the code works well when there are not any SubProcesses.
    Even when the Framework supported the feature, there usually were not any
    configured. The risk of breaking something while simplifying the code is
    significant. Also, it didn't seem worth the investment of expensive
    developer time to clean this up. We had more important things that needed
    to be done...

We might revisit this when the new feature is implemented or if we decide not
to implement it at all.

The class SubProcessBlockHelper was deleted in 2025, but it is still in
the git history and was the other class that inherited from ProcessBlockHelperBase.
*/

#include "DataFormats/Provenance/interface/ProvenanceFwd.h"
#include "FWCore/Common/interface/ProcessBlockHelperBase.h"

#include <set>
#include <string>
#include <vector>

namespace edm {

  class ProcessBlockHelper : public ProcessBlockHelperBase {
  public:
    ProcessBlockHelperBase const* topProcessBlockHelper() const final;
    std::vector<std::string> const& topProcessesWithProcessBlockProducts() const final;
    unsigned int nProcessesInFirstFile() const final;
    std::vector<std::vector<unsigned int>> const& processBlockCacheIndices() const final;
    std::vector<std::vector<unsigned int>> const& nEntries() const final;
    std::vector<unsigned int> const& cacheIndexVectorsPerFile() const final;
    std::vector<unsigned int> const& cacheEntriesPerFile() const final;
    unsigned int processBlockIndex(std::string const& processName, EventToProcessBlockIndexes const&) const final;
    unsigned int outerOffset() const final;

    bool initializedFromInput() const { return initializedFromInput_; }

    bool firstFileDropProcessesAndReorderStored(StoredProcessBlockHelper& storedProcessBlockHelper,
                                                std::set<std::string> const& processesToKeep,
                                                std::vector<unsigned int> const& nEntries,
                                                std::vector<unsigned int>& finalIndexToStoredIndex) const;

    bool dropProcessesAndReorderStored(StoredProcessBlockHelper& storedProcessBlockHelper,
                                       std::set<std::string> const& processesToKeep,
                                       std::vector<unsigned int> const& nEntries,
                                       std::vector<unsigned int>& finalIndexToStoredIndex,
                                       std::vector<std::string> const& firstFileFinalProcesses) const;

    void initializeFromPrimaryInput(StoredProcessBlockHelper const& storedProcessBlockHelper);

    void fillFromPrimaryInput(StoredProcessBlockHelper const& storedProcessBlockHelper,
                              std::vector<unsigned int> const& nEntries);

    void clearAfterOutputFilesClose();

  private:
    void dropProcessesAndReorderStoredImpl(StoredProcessBlockHelper& storedProcessBlockHelper,
                                           std::vector<std::string>& finalProcesses,
                                           std::vector<unsigned int> const& nEntries,
                                           std::vector<unsigned int> const& finalIndexToStoredIndex) const;

    void fillFromPrimaryInputWhenNotEmpty(std::vector<std::string> const& storedProcesses,
                                          std::vector<unsigned int> const& storedCacheIndices,
                                          std::vector<unsigned int> const& nEntries);

    void fillEntriesFromPrimaryInput(std::vector<unsigned int> nEntries);

    // A general comment about this class and its data members.
    // It was initially written to handle cases where all ProcessBlock
    // products from some process were dropped in a file after
    // the first input file but were present in the first input file.
    // At the moment this comment is being written, the file merging
    // rules do not allow this to happen and this situation never
    // occurs. However, this class intentionally maintains support
    // for this case, because we may find we need to change the file
    // merging requirements in the future. So there is support for
    // some indices to be invalid or other values to be zero even
    // though at the moment this should never occur.

    // Events hold an index into the outer vector
    // (an offset needs to added in the case of multiple input
    // files). The elements of the inner vector correspond to the
    // processes in processesWithProcessBlockProducts_ (exactly
    // 1 to 1 in the same order except it only includes those processes
    // from the input, if the current Process is
    // added, then it is added to the container of cache indices when
    // the output module makes its modified copy). The values inside
    // the inner vector are the cache indices into the cache vectors
    // contained by user modules. This cache order is the same as the
    // processing order of ProcessBlocks in the current process.
    // It might contain invalid cache index values.
    std::vector<std::vector<unsigned int>> processBlockCacheIndices_;

    // Number of entries per ProcessBlock TTree.
    // The outer vector has an element for each input file.
    // The inner vector elements correspond 1-to-1 with
    // processesWithProcessBlockProducts_ and in the same
    // order. This might contain zeroes.
    std::vector<std::vector<unsigned int>> nEntries_;

    // The index into the next two vectors is the input file index
    // which is based on the order input files are read
    // These can contain zeroes.
    std::vector<unsigned int> cacheIndexVectorsPerFile_;
    std::vector<unsigned int> cacheEntriesPerFile_;

    unsigned int nProcessesInFirstFile_ = 0;

    bool initializedFromInput_ = false;

    // Index of the first outer vector element in the cache indices
    // container that is associated with the current input file. If
    // it points to the end, then there were no cache indices in the
    // current input file.
    unsigned int outerOffset_ = 0;

    // Offset for the cacheIndex, counts all entries in
    // ProcessBlock TTree's in all input files seen so far.
    unsigned int cacheIndexOffset_ = 0;
  };
}  // namespace edm
#endif
