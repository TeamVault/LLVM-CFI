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

// you have to modify the following 4 files for each additional LLVM pass
// 1. include/llvm/IPO.h
// 2. lib/Transforms/IPO/IPO.cpp
// 3. include/llvm/LinkAllPasses.h
// 4. include/llvm/InitializePasses.h
// 5. lib/Transforms/IPO/PassManagerBuilder.cpp

namespace llvm {

  /**
   * Module pass for the SafeDispatch Gold Plugin
   */
  class SDLayoutBuilder : public ModulePass {
  public:
    static char ID; // Pass identification, replacement for typeid
    // variable definitions
    typedef SDBuildCHA::vtbl_t                              vtbl_t;     //Paul: pair v table name and index
    typedef SDBuildCHA::vtbl_name_t                         vtbl_name_t;//Paul: v table name as a string
    typedef SDBuildCHA::order_t                             order_t;    //Paul: vector of pairs of vtbl_t (string and index)
    typedef SDBuildCHA::roots_t                             roots_t;    //Paul: set of vtbl_name_t (strings)
    typedef SDBuildCHA::range_t                             range_t;    //Paul: pair of uint64_t and uint64_t
    
    typedef std::pair<Constant*, uint64_t>                  mem_range_t;
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

    new_layout_inds_t newLayoutInds;                        // (vtbl,ind) -> [new ind inside interleaved vtbl]
    interleaving_map_t interleavingMap;                     // root -> new layouts map
    vtbl_start_map_t newVTableStartAddrMap;                 // Starting addresses of all new vtables
    cloud_start_map_t cloudStartMap;                        // Mapping from new vtable names to their corresponding cloud starts
    std::map<vtbl_name_t, unsigned> alignmentMap;
    vtbl_t dummyVtable;                                     // Paul: this v table is used for the interleaving
    range_map_t rangeMap;                                   // Map of ranges for vptrs in terms of preorder indices
    mem_range_map_t memRangeMap;                            // this is the memory range map for each of the nodes in a cloud
    pad_map_t prePadMap;
    bool interleave;                                        // this is a flag used to decide if we interleave or order the cloud 

    SDLayoutBuilder(bool interl = false) : ModulePass(ID), interleave(interl) {
      std::cerr << "SDLayoutBuilder(" << interl << ")\n";
      initializeSDLayoutBuilderPass(*PassRegistry::getPassRegistry());
      dummyVtable = vtbl_t("DUMMY_VTBL", 0); //this v tables are used during padding 
    }

    virtual ~SDLayoutBuilder() { }

    bool runOnModule(Module &M) {
      sd_print("\nP3. Started building layout ...\n");

      /**Paul:
      first, pass the results from the CHA pass
       to the SD Layout Builder pass inside the new cha variable*/
      cha = &getAnalysis<SDBuildCHA>();
      
      /* Paul:
      this builds the new layouts. The layouts will be stored 
      in the metadata of the GlobalVariables. 
      First, this will be
      removed and our data will be inserted there.
      The buildNewLayouts(M) method is located
      at the bottom of the SDLayoutBuilder class and it is the main
      driver of this pass. 
      The v tables area interleaved or ordered depending 
      on the used interleaving flag. 
      */
      
      buildNewLayouts(M);

      //after building the new layout verify them according to some imposed conditions 
      assert(verifyNewLayouts(M));

      sd_print("\nP3. Finished building layout ...\n");
      return 1;
    }
    
    /*Paul:
    this is used in order to pass info from CHA pass to this pass 
    */
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<SDBuildCHA>();
      AU.addPreserved<SDBuildCHA>();
    }
 
     /*Paul:
    build the analysis results of interleave and order*/
    virtual void buildNewLayouts(Module &M);
    
    /*Paul:
    verify the analysis results of interleave and order*/
    virtual bool verifyNewLayouts(Module &M);

    /*Paul:
    remove the analysis results of interleave and order*/
    virtual void removeOldLayouts(Module &M);
    
    /*Paul:
    clear the analysis results after we are done with building the new layouts*/
    virtual void clearAnalysisResults();
   

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
     * New Interleaving method 
     */
    void interleaveCloudNew(vtbl_name_t& vtbl);


    /**
     * Calculate the new layout indices for each vtable inside the given cloud
     */
    void calculateNewLayoutInds(vtbl_name_t& vtbl);

    /** Paul
     * Calculate the v pointer ranges
     */
    void calculateVPtrRanges(Module& M, vtbl_name_t& vtbl);
  
    /** Paul
     * helper for the above function
     */
    void calculateVPtrRangesHelper(const vtbl_t& vtbl, std::map<vtbl_t, uint64_t> &indMap);

     /** Paul
     * after calculating the ranges, see method above, these will be checked
     */
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
    
    /*Paul: 
     *cha stores the result of the CHA pass.
     The CHA pass is just responsible for collecting
     the v tables which are contained in the metadata. The CHA pass
     results will be passed to SDLayoutBuilder pass after
     CHA has finisched inside *char variable.
    */
    SDBuildCHA *cha; 
  };

}
