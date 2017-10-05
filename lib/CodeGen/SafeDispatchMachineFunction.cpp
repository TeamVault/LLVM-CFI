#include "llvm/CodeGen/SafeDispatchMachineFunction.h"

using namespace llvm;

bool SDMachineFunction::runOnMachineFunction(MachineFunction &MF)  {
  // Enable SDMachineFunction pass?
  if (MF.getMMI().getModule()->getNamedMetadata("SD_emit_return_labels") == nullptr)
    return false;

  sdLog::log() << "Running SDMachineFunction pass: "<< MF.getName() << "\n";

  const TargetInstrInfo* TII = MF.getSubtarget().getInstrInfo();
  //We would get NamedMetadata like this:
  //const auto &M = MF.getMMI().getModule();
  //const auto &MD = M->getNamedMetadata("sd.class_info._ZTV1A");
  //MD->dump();

  for (auto &MBB: MF) {
    for (auto &MI : MBB) {
      if (MI.isCall()) {
        // Try to find our annotation.
        if (MI.getDebugLoc()) {
          std::string debugLocString = debugLocToString(MI.getDebugLoc());

          if (!processVirtualCallSite(debugLocString, MI, MBB, TII)) {
            if (!processStaticCallSite(debugLocString, MI, MBB, TII)) {
              processUnknownCallSite(debugLocString, MI, MBB, TII);
            }
          }
        } else {
          std::string NoDebugLoc = "N/A";
          processUnknownCallSite(NoDebugLoc, MI, MBB, TII);
        }
      }
    }
  }

  sdLog::log() << "Finished SDMachineFunction pass: "<< MF.getName() << "\n";
  return true;
}

bool SDMachineFunction::processVirtualCallSite(std::string &DebugLocString,
                                               MachineInstr &MI,
                                               MachineBasicBlock &MBB,
                                               const TargetInstrInfo *TII) {
  auto FunctionNameIt = CallSiteDebugLocVirtual.find(DebugLocString);
  if (FunctionNameIt == CallSiteDebugLocVirtual.end()) {
    return false;
  }

  auto FunctionName = FunctionNameIt->second;
  sdLog::log() << "Machine CallInst (@" << DebugLocString << ")"
               << " in " << MBB.getParent()->getName()
               << " is virtual Caller for " << FunctionName << "\n";

  auto range = CallSiteRange.find(DebugLocString);
  if (range == CallSiteRange.end()) {
    sdLog::errs() << DebugLocString << " has not Range!";
    return false;
  }
  int64_t min = range->second.first;
  int64_t width = range->second.second - min;


  RangeWidths.push_back(width);
  TII->insertNoop(MBB, MI.getNextNode());
  MI.getNextNode()->operands_begin()[3].setImm(width | 0x80000);
  TII->insertNoop(MBB, MI.getNextNode());
  MI.getNextNode()->operands_begin()[3].setImm(min | 0x80000);

  ++NumberOfVirtual;
  return true;
}

bool SDMachineFunction::processStaticCallSite(std::string &DebugLocString,
                                              MachineInstr &MI,
                                              MachineBasicBlock &MBB,
                                              const TargetInstrInfo *TII) {
  auto FunctionNameIt = CallSiteDebugLocStatic.find(DebugLocString);
  if (FunctionNameIt == CallSiteDebugLocStatic.end()) {
    return false;
  }

  auto FunctionName = FunctionNameIt->second;
  sdLog::log() << "Machine CallInst (@" << DebugLocString << ")"
               << " in " << MBB.getParent()->getName()
               << " is static Caller for " << FunctionName << "\n";

  if (FunctionName == "__INDIRECT__") {
    TII->insertNoop(MBB, MI.getNextNode());
    MI.getNextNode()->operands_begin()[3].setImm(indirectID);

    ++NumberOfIndirect;
    return true;
  }

  if (FunctionName == "__TAIL__") {
    TII->insertNoop(MBB, MI.getNextNode());
    MI.getNextNode()->operands_begin()[3].setImm(tailID);

    ++NumberOfTail;
    return true;
  }

  auto IDIterator = FunctionIDMap.find(FunctionName);
  if (IDIterator == FunctionIDMap.end()) {
    sdLog::errs() << FunctionName << " has not ID!";
    return false;
  }

  int64_t ID = IDIterator->second;
  TII->insertNoop(MBB, MI.getNextNode());
  MI.getNextNode()->operands_begin()[3].setImm(ID | 0x80000);

  ++NumberOfStaticDirect;
  return true;
}

bool SDMachineFunction::processUnknownCallSite(std::string &DebugLocString,
                                               MachineInstr &MI,
                                               MachineBasicBlock &MBB,
                                               const TargetInstrInfo *TII) {
  // Filter out std function and external function calls.
  if (MI.getNumOperands() > 0 && !MI.getOperand(0).isGlobal()) {
    TII->insertNoop(MBB, MI.getNextNode());
    MI.getNextNode()->operands_begin()[3].setImm(unknownID);

    sdLog::warn() << "Machine CallInst (@" << DebugLocString << ")"
                 << " in " << MBB.getParent()->getName()
                 << " is an unknown Caller! \n";

    ++NumberOfUnknown;
    return true;
  }
  return false;
}

std::string SDMachineFunction::debugLocToString(const DebugLoc &Loc) {
  assert(Loc);

  std::stringstream Stream;
  auto *Scope = cast<MDScope>(Loc.getScope());
  Stream << Scope->getFilename().str() << ":" << Loc.getLine() << ":" << Loc.getCol();
  return Stream.str();
};

void SDMachineFunction::loadVirtualCallSiteData() {
  //TODO MATT: delete file
  std::ifstream InputFile("./_SD_CallSites");
  std::string InputLine;
  std::string DebugLoc, ClassName, PreciseName, FunctionName;
  std::string MinStr, MaxStr;


  while (std::getline(InputFile, InputLine)) {
    std::stringstream LineStream(InputLine);

    std::getline(LineStream, DebugLoc, ',');
    std::getline(LineStream, ClassName, ',');
    std::getline(LineStream, PreciseName, ',');
    std::getline(LineStream, FunctionName, ',');
    std::getline(LineStream, MinStr, ',');
    LineStream >> MaxStr;
    int min = std::stoi(MinStr);
    int max = std::stoi(MaxStr);
    CallSiteDebugLocVirtual[DebugLoc] = FunctionName;
    CallSiteRange[DebugLoc] = {min, max};
  }
}

void SDMachineFunction::loadStaticCallSiteData() {
  //TODO MATT: delete file
  std::ifstream InputFile("./_SD_CallSitesStatic");
  std::string InputLine;
  std::string DebugLoc, FunctionName;

  while (std::getline(InputFile, InputLine)) {
    std::stringstream LineStream(InputLine);

    std::getline(LineStream, DebugLoc, ',');
    LineStream >> FunctionName;
    CallSiteDebugLocStatic[DebugLoc] = FunctionName;
  }
}

void SDMachineFunction::loadStaticFunctionIDData() {
  //TODO MATT: delete file
  std::ifstream InputFile("./_SD_FunctionIDMap");
  std::string InputLine;
  std::string FunctionName, IDString;

  while (std::getline(InputFile, InputLine)) {
    std::stringstream LineStream(InputLine);

    std::getline(LineStream, FunctionName, ',');
    LineStream >> IDString;
    int ID = std::stoi(IDString);
    FunctionIDMap[FunctionName] = ID;
  }
}

void SDMachineFunction::analyse() {
  uint64_t sum = 0;
  if (!RangeWidths.empty()) {
    for (int i : RangeWidths) {
      sum += i;
    }
    double avg = double(sum) / RangeWidths.size();
    sdLog::stream() << "AVG RANGE: " << avg << "\n";
    sdLog::stream() << "TOTAL RANGES: " << RangeWidths.size() << "\n";
  }
}


char SDMachineFunction::ID = 0;

INITIALIZE_PASS(SDMachineFunction, "sdMachinePass", "Get frontend infos.", false, true)

FunctionPass* llvm::createSDMachineFunctionPass() {
  return new SDMachineFunction();
}