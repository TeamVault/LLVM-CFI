#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/SafeDispatch.h"
#include "llvm/Transforms/IPO/SafeDispatchCHA.h"
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

namespace llvm {

  /**
   * Module pass for the SafeDispatch Gold Plugin
   */
  class SDLayoutBuilder : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid
    // variable definitions
    typedef SDBuildCHA::vtbl_t         vtbl_t;
    typedef SDBuildCHA::vtbl_name_t    vtbl_name_t;
    typedef SDBuildCHA::order_t        order_t;
    typedef SDBuildCHA::roots_t        roots_t;
    typedef SDBuildCHA::range_t        range_t;
    typedef std::pair<Constant*, uint64_t>  mem_range_t;
    typedef std::map<vtbl_t, std::vector<uint64_t>>         new_layout_inds_t;
    typedef std::map<vtbl_t, std::map<uint64_t, uint64_t>>  new_layout_inds_map_t;
    typedef std::pair<vtbl_t, uint64_t>       					    interleaving_t;
    typedef std::list<interleaving_t>                       interleaving_list_t;
    typedef std::vector<interleaving_t>                     interleaving_vec_t;
    typedef std::map<vtbl_name_t, interleaving_list_t>      interleaving_map_t;
    typedef std::map<vtbl_t, Constant*>                     vtbl_start_map_t;
    typedef std::map<vtbl_name_t, GlobalVariable*>          cloud_start_map_t;
    typedef std::map<vtbl_t, std::vector<range_t> >         range_map_t;
    typedef std::map<vtbl_t, std::vector<mem_range_t> >     mem_range_map_t;
    typedef std::map<vtbl_t, uint64_t>                      pad_map_t;

    new_layout_inds_t newLayoutInds;                   // (vtbl,ind) -> [new ind inside interleaved vtbl]
    interleaving_map_t interleavingMap;                // root -> new layouts map
    vtbl_start_map_t newVTableStartAddrMap;            // Starting addresses of all new vtables
    cloud_start_map_t cloudStartMap;                   // Mapping from new vtable names to their corresponding cloud starts
    std::map<vtbl_name_t, unsigned> alignmentMap;
    vtbl_t dummyVtable;
    range_map_t rangeMap;                             // Map of ranges for vptrs in terms of preorder indices
    mem_range_map_t memRangeMap;
    pad_map_t prePadMap;
    bool interleave;

    SDLayoutBuilder(bool interl = false) : ModulePass(ID), interleave(interl) {
      std::cerr << "SDLayoutBuilder(" << interl << ")\n";
      initializeSDLayoutBuilderPass(*PassRegistry::getPassRegistry());
      dummyVtable = vtbl_t("DUMMY_VTBL", 0);
    }

    virtual ~SDLayoutBuilder() { }

    bool runOnModule(Module &M) {
      sd_print("Started build layout\n");
      cha = &getAnalysis<SDBuildCHA>();

      buildNewLayouts(M);
      assert(verifyNewLayouts(M));
      sd_print("Finished building layout\n");
      return 1;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<SDBuildCHA>();
      AU.addPreserved<SDBuildCHA>();
    }

    virtual void clearAnalysisResults();
    virtual void buildNewLayouts(Module &M);
    virtual bool verifyNewLayouts(Module &M);
    virtual void removeOldLayouts(Module &M);

    virtual int64_t translateVtblInd(vtbl_t vtbl, int64_t offset, bool isRelative);
    /**
     * Get the start of the valid range for vptrs for a (potentially non-primary) vtable.
     * In practice we are always interested in primary vtables here.
     */
    llvm::Constant* getVTableRangeStart(const vtbl_t& vtbl);


    bool hasMemRange(const vtbl_t& vtbl);
    const std::vector<mem_range_t> &getMemRange(const vtbl_t& vtbl);
  private:
    /**
     * New starting address point inside the interleaved vtable
     */
    uint64_t newVtblAddressPoint(const vtbl_name_t& name);

    /**
     * Get the vtable address of the class in the interleaved scheme,
     * and insert the necessary instructions before the given instruction
     */
    Value* newVtblAddress(Module& M, const vtbl_name_t& name, Instruction* inst);
    Constant* newVtblAddressConst(Module& M, const vtbl_t& vtbl);

    /**
     * Order and pad the cloud given by the root element.
     */
    void orderCloud(vtbl_name_t& vtbl);
    /**
     * Interleave and pad the cloud given by the root element.
     */
    void interleaveCloud(vtbl_name_t& vtbl);

    /**
     * Calculate the new layout indices for each vtable inside the given cloud
     */
    void calculateNewLayoutInds(vtbl_name_t& vtbl);

    void calculateVPtrRanges(Module& M, vtbl_name_t& vtbl);
    void calculateVPtrRangesHelper(const vtbl_t& vtbl, std::map<vtbl_t, uint64_t> &indMap);
    void verifyVPtrRanges(vtbl_name_t& vtbl);

    /**
     * Interleave the actual vtable elements inside the cloud and
     * create a new global variable
     */
    void createNewVTable(Module& M, vtbl_name_t& vtbl);

    /**
     * This method is used for filling the both (negative and positive) parts of an
     * interleaved vtable of a cloud.
     *
     * @param part        : A list reference to record the <vtbl_t, element index> pairs
     * @param order       : A list that contains the preorder traversal
     * @param positiveOff : true if we're filling the positive (function pointers) part
     */
    void fillVtablePart(interleaving_list_t& part, const order_t& order, bool positiveOff);

    /**
     * These functions and variables used to deal with duplication
     * of the vthunks in the vtables
     */
    unsigned vcallMDId;
    std::set<Function*> vthunksToRemove;

    void createThunkFunctions(Module&, const vtbl_name_t& rootName);
    Function* getVthunkFunction(Constant* vtblElement);

    SDBuildCHA *cha;
  };

}
