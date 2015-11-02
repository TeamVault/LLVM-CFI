#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Transforms/IPO/SafeDispatchLog.h"

#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <list>
#include <vector>
#include <set>
#include <map>
#include <math.h>
#include <algorithm>
#include <deque>

#include <iostream>

// you have to modify the following files for each additional LLVM pass
// 1. IPO.h and IPO.cpp
// 2. LinkAllPasses.h
// 3. InitializePasses.h

namespace llvm {
  class SDLayoutBuilder;
  /**
   * Module pass for the SafeDispatch Gold Plugin
   */
  class SDBuildCHA : public ModulePass {
    friend class SDLayoutBuilder;
  public:
    static char ID; // Pass identification, replacement for typeid

    SDBuildCHA() : ModulePass(ID) {
      std::cerr << "Creating SDBuildCHA pass!\n";
      initializeSDBuildCHAPass(*PassRegistry::getPassRegistry());

    }

    virtual ~SDBuildCHA() {
      sd_print("deleting SDBuildCHA pass\n");
    }

    // variable definitions
    typedef std::string                                     vtbl_name_t;
    typedef std::pair<vtbl_name_t, uint64_t>                vtbl_t;
    typedef std::set<vtbl_t>                                cloud_map_children_t;
    typedef std::map<vtbl_t, cloud_map_children_t>          cloud_map_t;
    typedef std::set<vtbl_name_t>                           roots_t;
    typedef std::map<vtbl_name_t, std::vector<uint64_t>>    addrpt_map_t;
    typedef std::pair<uint64_t, uint64_t>                   range_t;
    typedef std::map<vtbl_name_t, std::vector<range_t>>     range_map_t;
    typedef std::map<vtbl_t, vtbl_name_t>                   ancestor_map_t;
    typedef std::vector<vtbl_t>                             order_t;
    typedef std::map<vtbl_name_t, std::vector<vtbl_name_t>> subvtbl_map_t;
    typedef std::map<vtbl_name_t, ConstantArray*>           oldvtbl_map_t;

    cloud_map_t cloudMap;                              // (vtbl,ind) -> set<(vtbl,ind)>
    roots_t roots;                                     // set<vtbl>
    subvtbl_map_t subObjNameMap;                       // vtbl -> [vtbl]
    addrpt_map_t addrPtMap;                            // vtbl -> [addr pt]
    range_map_t rangeMap;                              // vtbl -> [(start,end)]
    ancestor_map_t ancestorMap;                        // (vtbl,ind) -> root vtbl
    oldvtbl_map_t oldVTables;                          // vtbl -> &[vtable element]
    std::map<vtbl_t, uint32_t> cloudSizeMap;      // vtbl -> # vtables derived from (vtbl,0)
    std::set<vtbl_name_t> undefinedVTables;            // contains dynamic classes that don't have vtables defined

    // these should match the structs defined at SafeDispatchVtblMD.h
    struct nmd_sub_t {
      uint64_t order;
      vtbl_name_t parentName;
      uint64_t start; // range boundaries are inclusive
      uint64_t end;
      uint64_t addressPoint;
    };

    struct nmd_t {
      vtbl_name_t className;
      std::vector<nmd_sub_t> subVTables;
    };

    /**
     * 1. a. Iterate NamedMDNodes to build CHA forest F.
     *       => map<pair<vtbl,ind>, vector<pair<vtbl,ind>>>
     *    b. Take note of the roots of the forest.
     *       => set<vtbl>
     *    c. Keep the original address point map
     *       => map<vtbl, vector<int>>
     *    d. Keep the original sub-vtable ranges
     *       => map<vtbl, vector<int>>
     *    e. Calculate which sub-vtable belongs to which cloud.
     *       => map<pair<vtbl,ind>, vtbl>
     */
    bool runOnModule(Module &M) {
      sd_print("Started building CHA\n");

      vcallMDId = M.getMDKindID(SD_MD_VCALL);

      buildClouds(M);          // part 1
      std::cerr << "Undefined vtables: \n";
      for (auto i : undefinedVTables) {
        std::cerr << i << "\n";
      }
      //printClouds();
      sd_print("Finished building CHA\n");

      return roots.size() > 0;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }

    void clearAnalysisResults();

    /**
     * Calculates the vtable order number given the index relative to
     * the beginning of the vtable
     */
    unsigned getVTableOrder(const vtbl_name_t& vtbl, uint64_t ind);

    bool isUndefined(const vtbl_name_t &vtbl) {
      return undefinedVTables.find(vtbl) != undefinedVTables.end();
    }

    bool isUndefined(const vtbl_t &vtbl) {
      return isUndefined(vtbl.first);
    }

    bool isDefined(const vtbl_t &vtbl) {
      return !isUndefined(vtbl);
    }

  private:

    /**
     * Reads the NamedMDNodes in the given module and creates the class hierarchy
     */
    void buildClouds(Module &M);

    void printClouds();

    /**
     * These functions and variables used to deal with duplication
     * of the vthunks in the vtables
     */
    unsigned vcallMDId;
    std::set<Function*> vthunksToRemove;

  public:
    /**
     * Recursive function that calculates the number of deriving sub-vtables of each
     * primary vtable
     */
    uint32_t calculateChildrenCounts(const vtbl_t& vtbl);


    /**
     * Extract the vtable info from the metadata and put it into a struct
     */
    std::vector<nmd_t> static extractMetadata(NamedMDNode* md);
    /**
     * Return a list that contains the preorder traversal of the tree
     * starting from the given node
     */
    order_t preorder(const vtbl_t& root);
    void preorderHelper(order_t& nodes, const vtbl_t& root);

    /**
     * Return the number of vtables in a given primary vtable's cloud(including
     * the vtable itself). This is effectively the width of the range in which
     * the vtable pointer must lie in.
     */
    int64_t getCloudSize(const vtbl_name_t& vtbl);
    /**
     * Get the start of the valid range for vptrs for a (potentially non-primary) vtable.
     * In practice we are always interested in primary vtables here.
     */
    vtbl_t getFirstDefinedChild(const vtbl_t &vtbl);
    bool hasFirstDefinedChild(const vtbl_t &vtbl);
    bool knowsAbout(const vtbl_t &vtbl); // Have we ever seen md about this vtable?

    int64_t getSubVTableIndex(const vtbl_name_t& derived, const vtbl_name_t &base);
  };

}
