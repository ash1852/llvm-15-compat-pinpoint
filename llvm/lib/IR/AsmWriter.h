//===-- llvm/IR/AsmWriter.h - Printing LLVM IR as an assembly file - C++ --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This files defines the interface for the AssemblyWriter class used to print
// LLVM IR and various helper classes that are used in printing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_ASMWRITER_H
#define LLVM_LIB_IR_ASMWRITER_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalIFunc.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

using OrderMap = MapVector<const Value *, unsigned>;

using UseListOrderMap = DenseMap<const Function *, MapVector<const Value *, std::vector<unsigned>>>;

class BasicBlock;
class Function;
class GlobalValue;
class Comdat;
class Module;
class NamedMDNode;
class Value;
class SlotTracker;

/// Create a new SlotTracker for a Module 
SlotTracker *createSlotTracker(const Module *M);

//===----------------------------------------------------------------------===//
// TypePrinting Class: Type printing machinery
//===----------------------------------------------------------------------===//


class TypePrinting {
public:
  TypePrinting(const Module *M = nullptr) : DeferredM(M) {}

  TypePrinting(const TypePrinting &) = delete;
  TypePrinting &operator=(const TypePrinting &) = delete;

  /// The named types that are used by the current module.
  TypeFinder &getNamedTypes();

  /// The numbered types, number to type mapping.
  std::vector<StructType *> &getNumberedTypes();

  bool empty();

  void print(Type *Ty, raw_ostream &OS);

  void printStructBody(StructType *Ty, raw_ostream &OS);

private:
  void incorporateTypes();

  /// A module to process lazily when needed. Set to nullptr as soon as used.
  const Module *DeferredM;

  TypeFinder NamedTypes;

  // The numbered types, along with their value.
  DenseMap<StructType *, unsigned> Type2Number;

  std::vector<StructType *> NumberedTypes;
};

struct AsmWriterContext {
  TypePrinting *TypePrinter = nullptr;
  SlotTracker *Machine = nullptr;
  const Module *Context = nullptr;

  AsmWriterContext(TypePrinting *TP, SlotTracker *ST, const Module *M = nullptr)
      : TypePrinter(TP), Machine(ST), Context(M) {}

  static AsmWriterContext &getEmpty() {
    static AsmWriterContext EmptyCtx(nullptr, nullptr);
    return EmptyCtx;
  }

  /// A callback that will be triggered when the underlying printer
  /// prints a Metadata as operand.
  virtual void onWriteMetadataAsOperand(const Metadata *) {}

  virtual ~AsmWriterContext() = default;
};

class SlotTracker : public AbstractSlotTrackerStorage {
public:
  /// ValueMap - A mapping of Values to slot numbers.
  using ValueMap = DenseMap<const Value *, unsigned>;

private:
  /// TheModule - The module for which we are holding slot numbers.
  const Module* TheModule;

  /// TheFunction - The function for which we are holding slot numbers.
  const Function* TheFunction = nullptr;
  bool FunctionProcessed = false;
  bool ShouldInitializeAllMetadata;

  std::function<void(AbstractSlotTrackerStorage *, const Module *, bool)>
      ProcessModuleHookFn;
  std::function<void(AbstractSlotTrackerStorage *, const Function *, bool)>
      ProcessFunctionHookFn;

  /// The summary index for which we are holding slot numbers.
  const ModuleSummaryIndex *TheIndex = nullptr;

  /// mMap - The slot map for the module level data.
  ValueMap mMap;
  unsigned mNext = 0;

  /// fMap - The slot map for the function level data.
  ValueMap fMap;
  unsigned fNext = 0;

  /// mdnMap - Map for MDNodes.
  DenseMap<const MDNode*, unsigned> mdnMap;
  unsigned mdnNext = 0;

  /// asMap - The slot map for attribute sets.
  DenseMap<AttributeSet, unsigned> asMap;
  unsigned asNext = 0;

  /// ModulePathMap - The slot map for Module paths used in the summary index.
  StringMap<unsigned> ModulePathMap;
  unsigned ModulePathNext = 0;

  /// GUIDMap - The slot map for GUIDs used in the summary index.
  DenseMap<GlobalValue::GUID, unsigned> GUIDMap;
  unsigned GUIDNext = 0;

  /// TypeIdMap - The slot map for type ids used in the summary index.
  StringMap<unsigned> TypeIdMap;
  unsigned TypeIdNext = 0;

public:
  /// Construct from a module.
  ///
  /// If \c ShouldInitializeAllMetadata, initializes all metadata in all
  /// functions, giving correct numbering for metadata referenced only from
  /// within a function (even if no functions have been initialized).
  explicit SlotTracker(const Module *M,
                       bool ShouldInitializeAllMetadata = false);

  /// Construct from a function, starting out in incorp state.
  ///
  /// If \c ShouldInitializeAllMetadata, initializes all metadata in all
  /// functions, giving correct numbering for metadata referenced only from
  /// within a function (even if no functions have been initialized).
  explicit SlotTracker(const Function *F,
                       bool ShouldInitializeAllMetadata = false);

  /// Construct from a module summary index.
  explicit SlotTracker(const ModuleSummaryIndex *Index);

  SlotTracker(const SlotTracker &) = delete;
  SlotTracker &operator=(const SlotTracker &) = delete;

  ~SlotTracker() = default;

  void setProcessHook(
      std::function<void(AbstractSlotTrackerStorage *, const Module *, bool)>);
  void setProcessHook(std::function<void(AbstractSlotTrackerStorage *,
                                         const Function *, bool)>);

  unsigned getNextMetadataSlot() override { return mdnNext; }

  void createMetadataSlot(const MDNode *N) override;

  /// Return the slot number of the specified value in it's type
  /// plane.  If something is not in the SlotTracker, return -1.
  int getLocalSlot(const Value *V);
  int getGlobalSlot(const GlobalValue *V);
  int getMetadataSlot(const MDNode *N) override;
  int getAttributeGroupSlot(AttributeSet AS);
  int getModulePathSlot(StringRef Path);
  int getGUIDSlot(GlobalValue::GUID GUID);
  int getTypeIdSlot(StringRef Id);

  /// If you'd like to deal with a function instead of just a module, use
  /// this method to get its data into the SlotTracker.
  void incorporateFunction(const Function *F) {
    TheFunction = F;
    FunctionProcessed = false;
  }

  const Function *getFunction() const { return TheFunction; }

  /// After calling incorporateFunction, use this method to remove the
  /// most recently incorporated function from the SlotTracker. This
  /// will reset the state of the machine back to just the module contents.
  void purgeFunction();

  /// MDNode map iterators.
  using mdn_iterator = DenseMap<const MDNode*, unsigned>::iterator;

  mdn_iterator mdn_begin() { return mdnMap.begin(); }
  mdn_iterator mdn_end() { return mdnMap.end(); }
  unsigned mdn_size() const { return mdnMap.size(); }
  bool mdn_empty() const { return mdnMap.empty(); }

  /// AttributeSet map iterators.
  using as_iterator = DenseMap<AttributeSet, unsigned>::iterator;

  as_iterator as_begin()   { return asMap.begin(); }
  as_iterator as_end()     { return asMap.end(); }
  unsigned as_size() const { return asMap.size(); }
  bool as_empty() const    { return asMap.empty(); }

  /// GUID map iterators.
  using guid_iterator = DenseMap<GlobalValue::GUID, unsigned>::iterator;

  /// These functions do the actual initialization.
  inline void initializeIfNeeded();
  int initializeIndexIfNeeded();

  // Implementation Details
private:
  /// CreateModuleSlot - Insert the specified GlobalValue* into the slot table.
  void CreateModuleSlot(const GlobalValue *V);

  /// CreateMetadataSlot - Insert the specified MDNode* into the slot table.
  void CreateMetadataSlot(const MDNode *N);

  /// CreateFunctionSlot - Insert the specified Value* into the slot table.
  void CreateFunctionSlot(const Value *V);

  /// Insert the specified AttributeSet into the slot table.
  void CreateAttributeSetSlot(AttributeSet AS);

  inline void CreateModulePathSlot(StringRef Path);
  void CreateGUIDSlot(GlobalValue::GUID GUID);
  void CreateTypeIdSlot(StringRef Id);

  /// Add all of the module level global variables (and their initializers)
  /// and function declarations, but not the contents of those functions.
  void processModule();
  // Returns number of allocated slots
  int processIndex();

  /// Add all of the functions arguments, basic blocks, and instructions.
  void processFunction();

  /// Add the metadata directly attached to a GlobalObject.
  void processGlobalObjectMetadata(const GlobalObject &GO);

  /// Add all of the metadata from a function.
  void processFunctionMetadata(const Function &F);

  /// Add all of the metadata from an instruction.
  void processInstructionMetadata(const Instruction &I);
};

class AssemblyWriter {
  formatted_raw_ostream &Out;
  const Module *TheModule = nullptr;
  const ModuleSummaryIndex *TheIndex = nullptr;
  std::unique_ptr<SlotTracker> SlotTrackerStorage;
  SlotTracker &Machine;
  TypePrinting TypePrinter;
  AssemblyAnnotationWriter *AnnotationWriter = nullptr;
  SetVector<const Comdat *> Comdats;
  bool IsForDebug;
  bool ShouldPreserveUseListOrder;
  UseListOrderMap UseListOrders;
  SmallVector<StringRef, 8> MDNames;
  /// Synchronization scope names registered with LLVMContext.
  SmallVector<StringRef, 8> SSNs;
  DenseMap<const GlobalValueSummary *, GlobalValue::GUID> SummaryToGUIDMap;

public:
  /// Construct an AssemblyWriter with an external SlotTracker
  AssemblyWriter(formatted_raw_ostream &o, SlotTracker &Mac, const Module *M,
                 AssemblyAnnotationWriter *AAW, bool IsForDebug,
                 bool ShouldPreserveUseListOrder = false);

  AssemblyWriter(formatted_raw_ostream &o, SlotTracker &Mac,
                 const ModuleSummaryIndex *Index, bool IsForDebug);

  AsmWriterContext getContext() {
    return AsmWriterContext(&TypePrinter, &Machine, TheModule);
  }

  void printMDNodeBody(const MDNode *MD);
  void printNamedMDNode(const NamedMDNode *NMD);

  void printModule(const Module *M);

  void writeOperand(const Value *Op, bool PrintType);
  void writeParamOperand(const Value *Operand, AttributeSet Attrs);
  void writeOperandBundles(const CallBase *Call);
  void writeSyncScope(const LLVMContext &Context,
                      SyncScope::ID SSID);
  void writeAtomic(const LLVMContext &Context,
                   AtomicOrdering Ordering,
                   SyncScope::ID SSID);
  void writeAtomicCmpXchg(const LLVMContext &Context,
                          AtomicOrdering SuccessOrdering,
                          AtomicOrdering FailureOrdering,
                          SyncScope::ID SSID);

  void writeAllMDNodes();
  void writeMDNode(unsigned Slot, const MDNode *Node);
  void writeAttribute(const Attribute &Attr, bool InAttrGroup = false);
  void writeAttributeSet(const AttributeSet &AttrSet, bool InAttrGroup = false);
  void writeAllAttributeGroups();

  void printTypeIdentities();
  void printGlobal(const GlobalVariable *GV);
  void printAlias(const GlobalAlias *GA);
  void printIFunc(const GlobalIFunc *GI);
  void printComdat(const Comdat *C);
  void printFunction(const Function *F);
  void printArgument(const Argument *FA, AttributeSet Attrs);
  void printBasicBlock(const BasicBlock *BB);
  void printInstructionLine(const Instruction &I);
  void printInstruction(const Instruction &I);

  void printUseListOrder(const Value *V, const std::vector<unsigned> &Shuffle);
  void printUseLists(const Function *F);

  void printModuleSummaryIndex();
  void printSummaryInfo(unsigned Slot, const ValueInfo &VI);
  void printSummary(const GlobalValueSummary &Summary);
  void printAliasSummary(const AliasSummary *AS);
  void printGlobalVarSummary(const GlobalVarSummary *GS);
  void printFunctionSummary(const FunctionSummary *FS);
  void printTypeIdSummary(const TypeIdSummary &TIS);
  void printTypeIdCompatibleVtableSummary(const TypeIdCompatibleVtableInfo &TI);
  void printTypeTestResolution(const TypeTestResolution &TTRes);
  void printArgs(const std::vector<uint64_t> &Args);
  void printWPDRes(const WholeProgramDevirtResolution &WPDRes);
  void printTypeIdInfo(const FunctionSummary::TypeIdInfo &TIDInfo);
  void printVFuncId(const FunctionSummary::VFuncId VFId);
  void
  printNonConstVCalls(const std::vector<FunctionSummary::VFuncId> &VCallList,
                      const char *Tag);
  void
  printConstVCalls(const std::vector<FunctionSummary::ConstVCall> &VCallList,
                   const char *Tag);

private:
  /// Print out metadata attachments.
  void printMetadataAttachments(
      const SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs,
      StringRef Separator);

  // printInfoComment - Print a little comment after the instruction indicating
  // which slot it occupies.
  void printInfoComment(const Value &V);

  // printGCRelocateComment - print comment after call to the gc.relocate
  // intrinsic indicating base and derived pointer names.
  void printGCRelocateComment(const GCRelocateInst &Relocate);
};

} // namespace llvm

#endif

