#ifndef LLVM_TOOLS_GOLD_HELPERFUNCTIONS_H
#define LLVM_TOOLS_GOLD_HELPERFUNCTIONS_H

#include "llvm/Pass.h"
#include "llvm/LinkAllPasses.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include <map>
#include <string>

/// functions copied from BackendUtil.cpp
static void addAddDiscriminatorsPass(const llvm::PassManagerBuilder &Builder,
                                     llvm::legacy::PassManagerBase &PM) {
  PM.add(llvm::createAddDiscriminatorsPass());
}

static llvm::Pass*
createTTIPass(llvm::TargetMachine& TM) {
  return llvm::createTargetTransformInfoWrapperPass(TM.getTargetIRAnalysis());
}


typedef llvm::Pass* (*sd_pass_factory)();

template<class T>
static T* createPassObject() { return new T(); }

typedef std::map<std::string, sd_pass_factory> passDBType;
passDBType passDB;

typedef std::set<std::string> analysisDBType;
analysisDBType analysisDB;

#define SD_INSERT_PASS_MAP(MAP, NAME, FUN) \
  MAP[std::string(NAME)] = (sd_pass_factory) FUN

#define SD_INSERT_ANALYSIS_MAP(SET, NAME) \
  SET.insert(std::string(NAME))

static void
fillPasses() {
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-assumption-cache-tracker");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-block-freq");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-branch-prob");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-domtree");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-inline-cost");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-loop-accesses");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-memdep");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-scalar-evolution");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-targetlibinfo");
  SD_INSERT_ANALYSIS_MAP(analysisDB, "-tti"); // TargetTransformInfoWrapperPass

  SD_INSERT_PASS_MAP(passDB, "-adce", llvm::createAggressiveDCEPass);
  SD_INSERT_PASS_MAP(passDB, "-alignment-from-assumptions", llvm::createAlignmentFromAssumptionsPass);
  SD_INSERT_PASS_MAP(passDB, "-argpromotion", llvm::createArgumentPromotionPass);
  SD_INSERT_PASS_MAP(passDB, "-barrier", llvm::createBarrierNoopPass);
  SD_INSERT_PASS_MAP(passDB, "-basicaa", llvm::createBasicAliasAnalysisPass);
  //SD_INSERT_PASS_MAP(passDB, "-basiccg", NULL); // yet to be written
  SD_INSERT_PASS_MAP(passDB, "-bdce", llvm::createBitTrackingDCEPass);
  SD_INSERT_PASS_MAP(passDB, "-constmerge", llvm::createConstantMergePass);
  SD_INSERT_PASS_MAP(passDB, "-correlated-propagation", llvm::createCorrelatedValuePropagationPass);
  SD_INSERT_PASS_MAP(passDB, "-deadargelim", llvm::createDeadArgEliminationPass);
  SD_INSERT_PASS_MAP(passDB, "-dse", llvm::createDeadStoreEliminationPass);
  SD_INSERT_PASS_MAP(passDB, "-early-cse", llvm::createEarlyCSEPass);
  SD_INSERT_PASS_MAP(passDB, "-float2int", llvm::createFloat2IntPass);
  SD_INSERT_PASS_MAP(passDB, "-functionattrs", llvm::createFunctionAttrsPass);
  SD_INSERT_PASS_MAP(passDB, "-globaldce", llvm::createGlobalDCEPass);
  SD_INSERT_PASS_MAP(passDB, "-globalopt", llvm::createGlobalOptimizerPass);
  SD_INSERT_PASS_MAP(passDB, "-gvn", llvm::createGVNPass);
  SD_INSERT_PASS_MAP(passDB, "-indvars", llvm::createIndVarSimplifyPass);
  SD_INSERT_PASS_MAP(passDB, "-inline", llvm::createFunctionInliningPass);
  SD_INSERT_PASS_MAP(passDB, "-instcombine", llvm::createInstructionCombiningPass);
  SD_INSERT_PASS_MAP(passDB, "-instsimplify", llvm::createInstructionSimplifierPass);
  SD_INSERT_PASS_MAP(passDB, "-ipsccp", llvm::createIPSCCPPass);
  SD_INSERT_PASS_MAP(passDB, "-jump-threading", llvm::createJumpThreadingPass);
  SD_INSERT_PASS_MAP(passDB, "-lazy-value-info", llvm::createLazyValueInfoPass);
  SD_INSERT_PASS_MAP(passDB, "-lcssa", llvm::createLCSSAPass);
  SD_INSERT_PASS_MAP(passDB, "-licm", llvm::createLICMPass);
  SD_INSERT_PASS_MAP(passDB, "-loop-deletion", llvm::createLoopDeletionPass);
  SD_INSERT_PASS_MAP(passDB, "-loop-idiom", llvm::createLoopIdiomPass);
  SD_INSERT_PASS_MAP(passDB, "-loop-rotate", llvm::createLoopRotatePass);
  SD_INSERT_PASS_MAP(passDB, "-loop-simplify", llvm::createLoopSimplifyPass);
  SD_INSERT_PASS_MAP(passDB, "-loop-unroll", llvm::createLoopUnrollPass);
  SD_INSERT_PASS_MAP(passDB, "-loop-unswitch", llvm::createLoopUnswitchPass);
  SD_INSERT_PASS_MAP(passDB, "-lower-expect", llvm::createLowerExpectIntrinsicPass);
  SD_INSERT_PASS_MAP(passDB, "-memcpyopt", llvm::createMemCpyOptPass);
  SD_INSERT_PASS_MAP(passDB, "-mergefunc", llvm::createMergeFunctionsPass);
  SD_INSERT_PASS_MAP(passDB, "-no-aa", llvm::createNoAAPass);
  SD_INSERT_PASS_MAP(passDB, "-prune-eh", llvm::createPruneEHPass);
  SD_INSERT_PASS_MAP(passDB, "-sccp", llvm::createSCCPPass);
  SD_INSERT_PASS_MAP(passDB, "-scev-aa", llvm::createScalarEvolutionAliasAnalysisPass);
  SD_INSERT_PASS_MAP(passDB, "-scoped-noalias", llvm::createScopedNoAliasAAPass);
  SD_INSERT_PASS_MAP(passDB, "-simplifycfg", llvm::createCFGSimplificationPass);
  SD_INSERT_PASS_MAP(passDB, "-slp-vectorizer", llvm::createSLPVectorizerPass);
  SD_INSERT_PASS_MAP(passDB, "-sroa", llvm::createSROAPass);
  SD_INSERT_PASS_MAP(passDB, "-strip-dead-prototypes", llvm::createStripDeadPrototypesPass);
  SD_INSERT_PASS_MAP(passDB, "-tailcallelim", llvm::createTailCallEliminationPass);
  SD_INSERT_PASS_MAP(passDB, "-tbaa", llvm::createTypeBasedAliasAnalysisPass);
  SD_INSERT_PASS_MAP(passDB, "-verify", llvm::createVerifierPass);
}


std::string funPasses[] = {
  "-tti", "-no-aa", "-tbaa", "-scoped-noalias", "-assumption-cache-tracker",
  "-targetlibinfo", "-basicaa", "-verify", "-simplifycfg", "-domtree",
  "-sroa", "-early-cse", "-lower-expect"
};

unsigned funPassesLength = sizeof(funPasses) / sizeof(std::string);

std::string modulePasses[] = {
  "-targetlibinfo", "-tti", "-no-aa", "-tbaa", "-scoped-noalias", "-assumption-cache-tracker",
  "-basicaa", "-ipsccp", "-globalopt", "-deadargelim", "-domtree", "-instcombine", "-simplifycfg",
  "-basiccg", "-prune-eh", "-inline-cost", "-inline", "-functionattrs", "-argpromotion", "-sroa",
  "-domtree", "-early-cse", "-lazy-value-info", "-jump-threading", "-correlated-propagation",
  "-simplifycfg", "-domtree", "-instcombine", "-tailcallelim", "-simplifycfg", "-reassociate", "-domtree",
  "-loops", "-loop-simplify", "-lcssa", "-loop-rotate", "-licm", "-loop-unswitch", "-instcombine",
  "-scalar-evolution", "-loop-simplify", "-lcssa", "-indvars", "-loop-idiom", "-loop-deletion",
  "-loop-unroll", "-memdep", "-mldst-motion", "-domtree", "-memdep", "-gvn", "-memdep", "-memcpyopt",
  "-sccp", "-domtree", "-bdce", "-instcombine", "-lazy-value-info", "-jump-threading",
  "-correlated-propagation", "-domtree", "-memdep", "-dse", "-loops", "-loop-simplify", "-lcssa",
  "-licm", "-adce", "-simplifycfg", "-domtree", "-instcombine", "-barrier", "-float2int", "-domtree",
  "-loops", "-loop-simplify", "-lcssa", "-loop-rotate", "-branch-prob", "-block-freq", "-scalar-evolution",
  "-loop-accesses", "-loop-vectorize", "-instcombine", "-scalar-evolution", "-slp-vectorizer",
  "-simplifycfg", "-domtree", "-instcombine", "-loops", "-loop-simplify", "-lcssa", "-scalar-evolution",
  "-loop-unroll", "-instsimplify", "-loop-simplify", "-lcssa", "-licm", "-scalar-evolution",
  "-alignment-from-assumptions", "-strip-dead-prototypes", "-globaldce", "-constmerge", "-verify"
};
unsigned modulePassesLength = sizeof(modulePasses) / sizeof(std::string);

llvm::legacy::PassManager *
getSDPasses(llvm::TargetMachine &TM) {
  llvm::legacy::PassManager* PM = new llvm::legacy::PassManager();
  PM->add(createTTIPass(TM));

  PM->add(llvm::createSDModulePass());
  PM->add(llvm::createChangeConstantPass());

  PM->add(llvm::createVerifierPass());

  return PM;
}

llvm::legacy::FunctionPassManager *
getPerFunctionPasses(llvm::Module& TheModule, llvm::TargetMachine &TM) {
  llvm::legacy::FunctionPassManager* FPM = new legacy::FunctionPassManager(&TheModule);
  FPM->add(createTTIPass(TM));

  passDBType::iterator itr;
  for (unsigned i = 0; i < funPassesLength; ++i) {
    const std::string* passName = &(funPasses[i]);

    if (analysisDB.find(*passName) == analysisDB.end())
      continue; // this is an analysis pass, move on

    if ((itr = passDB.find(*passName)) != passDB.end())
      FPM->add((itr->second)());
  }

  return FPM;
}

llvm::legacy::PassManager *
getPerModulePasses(llvm::TargetMachine &TM) {
  llvm::legacy::PassManager* PM = new legacy::PassManager();
  PM->add(createTTIPass(TM));

  passDBType::iterator itr;
  for (unsigned i = 0; i < modulePassesLength; ++i) {
    const std::string* passName = &(modulePasses[i]);

    if (analysisDB.find(*passName) == analysisDB.end())
      continue; // this is an analysis pass, move on

    else if ((itr = passDB.find(*passName)) != passDB.end())
      PM->add((itr->second)());
  }

  return PM;
}

#endif

