//===-------- st_utilize.cpp - part of the static analysis for counting locks, threads and global variables in c program --------===//
//
// Define the common utilization functions
// 
//Author: ld, 2018-02-05
//===----------------------------------------------------------------------===//

#ifndef ST_UTILIZATION
#define ST_UTILIZATION

#include <cassert>
#include <string>
#include <map>
#include <set>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <vector> 

#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DebugInfoMetadata.h"

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/ProfileData/InstrProf.h"

#include "llvm/Analysis/CaptureTracking.h"

//Debug
//#define INSTRU_DEBUGINFO

//#define USEDEBUG
/* #ifdef USEDEBUG
	#define Debug( x ) errs() << x
#else
	#define Debug( x ) 
#endif */


/// the instant of the class: ST_ValueUtilize Vutilize;

using namespace llvm;

class ST_ValueUtilize {

public:

	/// Variable and its type information
	typedef struct VarTypeStru_def {
		bool isPointer = false; /// is a pointer type;
		Value* var; 			/// the variable pointer
		std::string varName="";  
		Type * ty; 				/// the current variable Type		
		std::string tyName=""; /// such "int","float", "double", "pthread_mutex_t"
		unsigned bitwide = 0;  	
		Type * baseTy; 			/// the base Type		
		std::string baseTyName; /// such as name of the structure  
		std::string pos="";		/// location of the current Ty in the baseTy, such as "0 3 2" means [0][3][2] 
        int num=0;		  		/// numbers of elements, 0 if not a array or vector
	} VarTypeStru;
	
	typedef std::list<VarTypeStru> VarTypeList_def;		
	typedef std::pair<Value*, std::string> MemVarPos_def;
	typedef struct wklist_one_def_stru {
		Value* var; 
		std::string pos;
		Instruction* usein;
	} wklist_one_def;	

	typedef SmallVector<wklist_one_def, 18> wklist_def;
	typedef SmallVector<wklist_def,6> m_wklist_def;

	// global vars, static vars and extern  vars	
		
	/// define constant identifier string for counting pthreads and locks
	const std::string PTHREAD_PRINTSTR = "__pthreadCounter_the_value";
	const std::string PTHREAD_PRINTFUNSTR = "__pthreadFunCounter_the_value";
	const std::string LOCK_PRINTSTR = "__lockmthreadCounter_the_value";
	
	const std::string PTHREAD_PRINTSTR_WL = "__pthreadCounter_wl_the_value";
	const std::string LOCK_PRINTSTR_WL = "__lockmthreadCounter_wl_the_value";	
	
	const std::string LOCK_VARDECLARE = "pthread_mutex_t";
	const std::string LOCK_CONDIVARDECLARE = "union.pthread_cond_t";
	const std::string LOCK_FULLVARDECLARE = "%union.pthread_mutex_t";
	const std::string LOCK_VARDECLAREINI = "zeroinitializer";
	const std::string PTHREAD_VARNM = "__pthreadCounter";
	const std::string LOCK_VARNM = "_lockmthreadCounter";
	
	const std::string LOCK_VARDECLARESTART = "pthread_mutex";
	const std::string LOCK_FULLVARDECLARESTART = "union.pthread_mutex";
	
	const std::string PTHREAD_VARDECLARE = "pthread_t";
	
	const std::string INS_LOCKINI = "pthread_mutex_init";
	const std::string INS_LOCKINI2 = "PTHREAD_MUTEX_INITIALIZER";
	
	const std::string INS_PTHREADCREATE = "pthread_create";
	const std::string LLVM_MEMSETSTART = "llvm.memset.";												 

	
	/// get interger from conatant Value
	int getInt32Value(Value* gv);
	
	std::string get_ValueID(Value* val);
	
	/// find the declaration of a Value in debug information
	const MDNode* findDbgVar(Value* V, Function* F);
	
	///get the name of a local variable by debug information
	StringRef getOriginalName(Value* V, Function* F);
	
	/// with the left val at top
	SmallVector<int, 6> push_posStr(std::string posStr);
	
	//bool sortInstruction(Instruction * lhs, Instruction * rhs);
	
	std::string pop_posStr(SmallVector<int, 6> posVect);
	
	/// with the left val at bottom
	SmallVector<int,5> getVectorfromPos(std::string posstr);
	
	unsigned getOffsetToPos(Type* Agg, std::string pos,const DataLayout &DL);
	
	///find the definition of a Value
	Value* chasePointerDef(Value* ptr, std::string* pos); 	
	
	/// if sin happen before usein in DT
	bool isDominates(Instruction* sin, Instruction* usein,DominatorTree &DT);
	
	/// if two instruction has ordering relations 
	bool hasOrderRelation(Instruction* sin, Instruction* usein);
	
	/// a set of help functions to print out information
	std::string printTextInfo(Value* gv);
	std::string printTypeInfo(Value* gv);
	std::string printTypeInfo(Type* gv);
	
	std::string printCalledby(Function* fun);
	
	std::string printUses(Value* val);
	std::string printUsers(Value* val);	
	std::string printOperands(Value* val);	
	
	///get the function of the pthread_create called
	Function * getPthreadCreateCalled(Instruction* instr);	
	
	/// is the function (except the main)called by other functions, not an orphan 
	bool  hasCaller(Function* fun);

	///get the argument of the pthread_create called with
	Value * getPthreadCreateCallArg(Instruction* instr);

	bool isGetElementPtrConstantExpr(Value* exp);	
	
	void lookforUsers(Value* vv, std::string pos, Instruction* usein, DominatorTree &DT, wklist_def * wklist, m_wklist_def * m_wklist, SmallVector<Instruction*,6> * useinlist);

	void getAllPredecessors(BasicBlock * blk, std::set<BasicBlock *> * pres);
	
	//if a value inside the parameters of a function
	bool isInFunArgs(Value* def, Function* F);	
	
	/// If a string start with another string 
	bool startsWith(std::string mainStr, std::string toMatch);	
	
	wklist_one_def getwkone(Value* var, std::string pos, Instruction * usein);	
	
	std::set<Value*> do_union(std::set<Value*> vs1,std::set<Value*> vs2);
	
	std::string getLastOperName(Instruction* is);
	std::string getOperandName(Instruction* is,int p);
	
	SmallVector<BasicBlock *, 10> getFinalBBlocks(Function &F);	

	Value* getPointerOperand(Instruction &Inst);
	
	///find a pointer get its address from a global var or a function parameter
	std::set<ST_ValueUtilize::MemVarPos_def> getPointerAddressFrom(Value* def, std::string defpos,Instruction* usein, DominatorTree &DT);
	
	std::string getInstructionOpName(Instruction* is);
	
	///if it is a function call to, for example: "printf", INS_PTHREADCREATE
	bool isCallto(Value* is,std::string nm);
	
	/// return true if the vv been pushed
	bool pushToWklist(Value* vv, std::string pos,Instruction* usein, DominatorTree &DT, wklist_def * wklist);
	
	bool isGlobalVarInitializ(Value * gv);
	
	/// if it is an access to virtual table objects (address point)
	bool isVtableAccess(Instruction *I);
	
	bool isExternal(Value * val);
	//bool isAtomic(Instruction *I); /// commented because change of function names from llvm 4 to llvm 5 
	
	/// if the instruction followed by a icmp and then a br instruction
	/// input: the instruction inst
	/// output: the icmp instruction and the no error branch
	std::pair<Instruction*, BasicBlock*> getCompNoErrBr(Instruction* inst);
	
	///inert a prinf instruction to print a integer at the end of the function 
	void insertPrinfInt(Function &F, std::string formatStr, int prnInt);	
    Function * getPrintF(IRBuilder<> &Builder, Module *M);
    void createPrintF(IRBuilder<> &Builder,
				  std::string Format,
				  ArrayRef<Value *> Values,
				  Module *M);

				  std::string printCalls(Function* fun);
	
	/// is it the instruction accessing the memory we want monitor
	bool isTraceLoadStoreInstr(Instruction *I, const DataLayout &DL );	
	
	/// is the memory address we are going to test data races
	bool isMemoryFromAddress(Value *Addr);	
	
	/// if it is a constant var 
	bool isConstantDataAddr(Value *Addr);	
	
	///get the type index by the pos string ,such as "0 2 4", always strat with "0"
	Type* getTypeAtPos(Type* Agg, std::string pos);

	BasicBlock::iterator getLastInsertPoint(BasicBlock* LBlock);
	
	VarTypeStru getFromTypeInfo(std::string s);	
	
	VarTypeStru analyseType(Value* val);
	VarTypeStru analyseType(Type* val);
	
	/// is a LOCK_FULLVARDECLARE in the definition of the Value	
	bool isPthreadMutex(Value * val);
	
	/// is a local static initialized mutex lock	
	bool isLocalStaticIniLock(Instruction * inst);
	
	/// is the called instruction called no empty function
	bool isCalledNoEmpty(Value* V);
	
	/// is the function recursive
	/// input the function, a empty set of function
	bool isFunRecursive(Function * fun,std::set<Function*> calledfuns);
	
	// if the vaule V in pos is a pointer of pointer
	bool isPointerToPointer(Value* V);	
	bool isPointerToPointer(Value* V,std::string pos);
	
	// if the vaule V in pos is a pointer 
	bool isToPointer(Value* V,std::string pos);	
	
	/// get the target of the instruction working on, for example: a global variable.
	/// input: for call instruction, the first operand,
	/// output: first - the variable declaration it working on,  second - the pos string if there is is a getelementptr 
	MemVarPos_def getInstrWorkOn(Value* val);
	
	VarTypeList_def getMutexList(VarTypeList_def * gvList);	
	
	/// get all Global Variables
	VarTypeList_def doListGlobalVar(Module &m);
	
	void doAnalyseType(Type* ty, Type* baseTy, Value* var, std::string posStr, VarTypeList_def * varVect);
	
	std::string getTypeName(Type* ty);
	
	int getTypeNum(Type* ty);	
	
private:

	void analyseSingleType(Type* ty, VarTypeStru * tystru);
	
};

///like i32* getelementptr inbounds ([6 x i32], [6 x i32]* @garr, i64 0, i64 4)
bool ST_ValueUtilize::isGetElementPtrConstantExpr(Value* exp) {

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(exp)) 
		if (CE->getOpcode() == Instruction::GetElementPtr) 
			return true;
	return false;	
}

/// If a string start with another string 
bool ST_ValueUtilize::startsWith(std::string mainStr, std::string toMatch){
	// std::string::find returns 0 if toMatch is found at starting
	if(mainStr.find(toMatch) == 0)
		return true;
	else
		return false;
}

/// find the declaration of a Value in debug information
const MDNode* ST_ValueUtilize::findDbgVar(Value* V, Function* F) {
  //for (const_inst_iterator Iter = inst_begin(F), End = inst_end(F); Iter!= End; ++Iter) {
  for (Function::iterator bb = F->begin(), be = F->end(); bb != be; ++bb) {		
		for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
			//Instruction* I = &(*ii);	  
	  
			const Instruction* I = &*ii;
			if (const DbgDeclareInst* DbgDeclare = dyn_cast<DbgDeclareInst>(I)) {
			  if (DbgDeclare->getAddress() == V) return DbgDeclare->getVariable();
			} else if (const DbgValueInst* DbgValue = dyn_cast<DbgValueInst>(I)) {
			  if (DbgValue->getValue() == V) return DbgValue->getVariable();
			}
		}	
  }
  return NULL;
}


///get the name of a local variable by debug information
StringRef ST_ValueUtilize::getOriginalName(Value* V,Function* F) {

  const MDNode* Var = findDbgVar(V, F);  
  std::string tnm = get_ValueID(V);
  if (!Var) return tnm;
  if (const DIVariable* dvar = dyn_cast<DIVariable>(Var)) {
	return dvar->getName();
  }

  return tnm;
}

 	

std::set<Value*> ST_ValueUtilize::do_union(std::set<Value*> vs1,std::set<Value*> vs2) {
	std::set<Value*> vs = vs1;
	vs.insert(vs2.begin(), vs2.end());	
	return vs;
}

ST_ValueUtilize::wklist_one_def ST_ValueUtilize::getwkone(Value* var, std::string pos, Instruction * usein){
	wklist_one_def wk;
	
	wk.var = var;
	wk.pos = pos;
	wk.usein = usein;
	
	return wk;
}	

int ST_ValueUtilize::getInt32Value(Value* gv) {	
	std::string s = printTextInfo(gv);
	std::string deli = " ";
	int res = -1;
	size_t pos;
	if ((pos = s.find(deli)) != std::string::npos) {
		std::string tp = s.substr(0,pos);
		std::string vl = s.erase(0,pos+deli.length());
		if (tp=="i32" || tp=="i64" || tp=="i8" || tp=="i16" || tp=="i128"){
			res = atoi(vl.c_str());
		}	
	}	
	return res;	
}	

SmallVector<int, 6> ST_ValueUtilize::push_posStr(std::string posStr){
	SmallVector<int, 6> rst;

	std::string deli = " ";
	size_t pos=0;
	if ((pos = posStr.find(deli)) != std::string::npos) {
		std::string tp = posStr.substr(0,pos);
		posStr.erase(0,pos+deli.length());
		rst.push_back(atoi(tp.c_str()));
	}	
	return rst;
}

std::string ST_ValueUtilize::pop_posStr(SmallVector<int, 6> posVect){
	std::string rst = "";

	while(!posVect.empty()){
		int v = posVect.pop_back_val();
		rst = std::to_string(v)+" "+rst;
	}	
	return rst;
}	

void ST_ValueUtilize::getAllPredecessors(BasicBlock * blk, std::set<BasicBlock *> * pres) {
	for (auto it = pred_begin(blk), et = pred_end(blk); it != et; ++it) {
		BasicBlock* pb = *it;
		if(pres->find(pb) == pres->end()) {
			pres->insert(pb);
			getAllPredecessors(pb,pres);
		}	
	}	
	return;
}	

/// if it is a constant var 
bool ST_ValueUtilize::isConstantDataAddr(Value *Addr) {
  /// If this is a GEP, just analyze its pointer operand.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Addr))
    Addr = GEP->getPointerOperand();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    if (GV->isConstant()) {
      return true;
    }
  } else if (LoadInst *L = dyn_cast<LoadInst>(Addr)) {
    if (isVtableAccess(L)) {
      return true;
    }
  }
  return false;
}

///get the function of the pthread_create called
/* TODO: operand is not always a function, can really be anything, in which case
   you'll just get a NULL here from the cast.*/
Function * ST_ValueUtilize::getPthreadCreateCalled(Instruction* ins){

	if(CallInst* instr = dyn_cast<CallInst>(ins)){
		if (instr->getCalledFunction()->getName().str()== INS_PTHREADCREATE) {
			return dyn_cast<Function>(instr->getOperand(2));
		}		
	}	
	return NULL;
}

///get the argument of the pthread_create called with
Value * ST_ValueUtilize::getPthreadCreateCallArg(Instruction* ins){

	if(CallInst* instr = dyn_cast<CallInst>(ins)){
		if (instr->getCalledFunction()->getName().str()== INS_PTHREADCREATE) {
			return instr->getOperand(3);
		}		
	}
	
	return NULL;
}
 
/// is the memory address we are going to test data races
bool ST_ValueUtilize::isMemoryFromAddress(Value *Addr) {
	
  /// Peel off GEPs and BitCasts.
  Addr = Addr->stripInBoundsOffsets();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
/*     if (GV->hasSection()) {
      StringRef SectionName = GV->getSection();
      /// Check if the global is in the PGO counters section.
      if (SectionName.endswith(getInstrProfCountersSectionName(false)))
        return false;
    } */

    /// Check if the global is private gcov data.
    if (GV->getName().startswith("__llvm_gcov") ||
        GV->getName().startswith("__llvm_gcda"))
      return false;
  }

  /// If it is from different address spaces
  if (Addr) {
    Type *PtrTy = cast<PointerType>(Addr->getType()->getScalarType());
    if (PtrTy->getPointerAddressSpace() != 0)
      return false;
  }

  return true;
}

Value* ST_ValueUtilize::getPointerOperand(Instruction &Inst) {

  if (LoadInst *Load = dyn_cast<LoadInst>(&Inst))

    return Load->getPointerOperand();

  else if (StoreInst *Store = dyn_cast<StoreInst>(&Inst))

    return Store->getPointerOperand();

  else if (GetElementPtrInst *Gep = dyn_cast<GetElementPtrInst>(&Inst))

    return Gep->getPointerOperand();

  return nullptr;

}

/// if it is an access to virtual table objects 
bool ST_ValueUtilize::isVtableAccess(Instruction *I) {
  if (MDNode *Tag = I->getMetadata(LLVMContext::MD_tbaa))
    return Tag->isTBAAVtableAccess();
  return false;
}

std::string ST_ValueUtilize::printTextInfo(Value* gv) {	
	if (gv==0 || gv==NULL || gv==nullptr) return "";
	std::string Str;
	raw_string_ostream OSS(Str);
	gv->print(OSS);
	OSS.flush();
	return Str;	
}	

std::string ST_ValueUtilize::get_ValueID(Value* val) {

	return std::to_string((uintptr_t)val);	
}

std::string ST_ValueUtilize::printTypeInfo(Value* gv) {	
	std::string Str="";
	if (gv==0 || gv == NULL || gv == nullptr) return Str;
	raw_string_ostream OSS(Str);
	gv->getType()->print(OSS);
	OSS.flush();
	return Str;	
}

std::string ST_ValueUtilize::printTypeInfo(Type* ty) {	
	std::string Str="";
	if (ty==0 || ty == NULL || ty == nullptr) return Str;
	raw_string_ostream OSS(Str);
	ty->print(OSS);
	OSS.flush();
	return Str;	
}

std::string ST_ValueUtilize::getLastOperName(Instruction* is) {	
	std::string Str = "";
	
	Value *val = is->getOperand(is->getNumOperands()-1);
	if (val->hasName()) Str = val->getName().str();

	return Str;	
}

/* TODO: Probably better to use existing llvm::UnifyFunctionExitNodes:
   http://llvm.org/doxygen/structllvm_1_1UnifyFunctionExitNodes.html
   Logic is described in http://llvm.org/doxygen/UnifyFunctionExitNodes_8cpp_source.html
   -- I don't see the code below checking for Return-instructions.
*/
SmallVector<BasicBlock *, 10> ST_ValueUtilize::getFinalBBlocks(Function &F){

	SmallVector<BasicBlock *, 10> fBBList;
	
	for (Function::iterator bb = F.begin(), be = F.end(); bb != be; ++bb) {
	  BasicBlock* blk = &*bb;
	  if (succ_begin(blk) == succ_end(blk) && (pred_begin(blk)!= pred_end(blk) || F.size()==1)){
		  fBBList.push_back(blk);	
	  }
	} 

	if (fBBList.empty()) {
		Function::iterator vii = F.end();
		while (fBBList.empty()) {
			vii--;
			BasicBlock* Bl = &(*vii);	
			if (pred_begin(Bl)!= pred_end(Bl)){
				fBBList.push_back(Bl);
			} else if (Bl == &F.getEntryBlock()) {
				fBBList.push_back(Bl);
			}	
		}
	}		
	return fBBList;	
}

BasicBlock::iterator ST_ValueUtilize::getLastInsertPoint(BasicBlock* LBlock){
	Instruction* lastin = LBlock->getTerminator();		
			
	if ((isa<UnreachableInst>(lastin))) {
			lastin = lastin->getPrevNode();	
	}	
		
	return lastin->getIterator();

    /// another version
/* 	BasicBlock::iterator lstii = LBlock->end();
	lstii--;
	
	Instruction* pinst = &(*lstii);
	//Debug( "The instruction to insert before is  : " << Vutilize.printTextInfo(pinst) <<  "\n");			
	
	///if the instruction is  UnreachableInst or terminator, go one instruction up
	if ((isa<UnreachableInst>(pinst) || pinst->isTerminator())&&  lstii != LBlock->begin() ) {
		lstii--;				
		//Debug( "Now instruction to insert before is  : " << Vutilize.printTextInfo(&(*lstii)) <<  "\n");
	}			
	
	return lstii;    	
 */	
}	

void ST_ValueUtilize::insertPrinfInt(Function &F, std::string formatStr, int prnInt){

	static Value * FormatString;
	static std::string fStr;
	
	Module* mod = F.getParent();
	
	SmallVector<BasicBlock *, 10> finalBBLst = getFinalBBlocks(F);
	
	/// insert the printf instruction
	while (!finalBBLst.empty()) {
		
		BasicBlock* LBlock = finalBBLst.pop_back_val();	
		
		//Debug( "Block printf go through : " << printTextInfo(LBlock).substr(0,20) <<  "\n"); 		
 		IRBuilder<> builder(LBlock);
		
		if (fStr != formatStr){
			fStr = formatStr;
			FormatString = builder.CreateGlobalStringPtr(formatStr+" = %d \n");
		}		
		
		builder.SetInsertPoint(LBlock,getLastInsertPoint(LBlock));
		
		std::vector<Value *> printf_args;
		printf_args.push_back(FormatString);
		printf_args.push_back(builder.getInt32(prnInt));		
		
		builder.CreateCall(getPrintF(builder, mod), printf_args); 	
		
		printf_args.clear();
	}	
	finalBBLst.clear();
	
	return;
}

Function * ST_ValueUtilize::getPrintF(IRBuilder<> &Builder, Module *M) {

	const char *Name = "printf";
	Function *F = M->getFunction(Name);

	if (!F) {
		GlobalValue::LinkageTypes Linkage = Function::ExternalLinkage;
		FunctionType *Ty = FunctionType::get(Builder.getInt32Ty(), true);
		F = Function::Create(Ty, Linkage, Name, M);
	}
	return F;
}

void ST_ValueUtilize::createPrintF(IRBuilder<> &Builder,
				  std::string Format,
				  ArrayRef<Value *> Values,
				  Module *M) {
	Value *FormatString = Builder.CreateGlobalStringPtr(Format);
	std::vector<Value *> Arguments;

	Arguments.push_back(FormatString);
	Arguments.insert(Arguments.end(), Values.begin(), Values.end());
	Builder.CreateCall(getPrintF(Builder, M), Arguments);
}


/// for example, "add" for binop instruction
std::string ST_ValueUtilize::getInstructionOpName(Instruction* is) {	
	return is->getOpcodeName();
}

std::string ST_ValueUtilize::getOperandName(Instruction* is,int p) {	
	std::string Str = "";
	
	if (p== -1) {
		Str = getLastOperName(is);
	} else if (p >=0 && (unsigned)p < is->getNumOperands()) {	
		Value *val = is->getOperand(p);
		if (val->hasName()) Str = val->getName().str();
	}
	return Str;	
}

///if it is a function call to, for example: "printf", INS_PTHREADCREATE
bool ST_ValueUtilize::isCallto(Value* ins,std::string nm){	
	bool rst = false;
	
	if( isa<CallInst>(ins)){
		CallInst* instr = dyn_cast<CallInst>(ins);
		if (getLastOperName(instr)==nm) rst = true;
	}
	return rst;	
}

//if a value inside the parameters of a function
bool ST_ValueUtilize::isInFunArgs(Value* def, Function* F) {
	bool rst = false;
	
	for(Function::arg_iterator ai = F->arg_begin(), ae = F->arg_end(); ai != ae; ++ai){
		//Argument * ar = &*ai;
		if (def==&*ai){
			rst = true;
			break;
		}   
    } 
	return rst;
}

bool ST_ValueUtilize::isPointerToPointer(Value* V) {
	return isPointerToPointer(V,"");
}	

/// if sin happen before usein in DT
/// if sin and usein are not odering, return false
bool ST_ValueUtilize::isDominates(Instruction* sin, Instruction* usein,DominatorTree &DT) {
	BasicBlock * useinblk = usein->getParent();
	BasicBlock * sinblk = sin->getParent();
	
	if (useinblk==sinblk){ /// in same block
		return DT.dominates(sin, usein);
	} else {
		std::set<BasicBlock *> pres;
		getAllPredecessors(useinblk,&pres);
		
		if(pres.find(sinblk) == pres.end()) {
			return false;
		} else 
			return true;
	}
}

/// if two instruction has ordering relations 
bool ST_ValueUtilize::hasOrderRelation(Instruction* sin, Instruction* usein) {
	BasicBlock * useinblk = usein->getParent();
	BasicBlock * sinblk = sin->getParent();
	
	if (useinblk==sinblk){ /// in same block
		return true;
	} else {
		std::set<BasicBlock *> useinpres;
		getAllPredecessors(useinblk,&useinpres);
		
		std::set<BasicBlock *> sinpres;
		getAllPredecessors(sinblk,&sinpres);
		
		if(useinpres.find(sinblk) == useinpres.end() && sinpres.find(useinblk) == sinpres.end()) {
			return false;
		} else 
			return true;	
	}
}

/// return true if the vv been pushed
bool ST_ValueUtilize::pushToWklist(Value* vv, std::string pos,Instruction* usein, DominatorTree &DT, wklist_def * wklist){
		
	if (vv==NULL || vv==nullptr) return false;
	if (isa<StoreInst>(vv) ){
		Instruction* sin =  dyn_cast<Instruction>(vv);		
		if (!isDominates(sin, usein,DT)) { // sin not happen before usein				
			//errs()<< "The sin is not before usein :" << printTextInfo(sin)  << "\n";				
			return false;	
		}		
	}	
	///start to push it
	wklist->push_back(getwkone(vv,pos,usein));
		
	return true;
}

/// look for user instructions of vv, that happen before usein, and in the same function of usein
void ST_ValueUtilize::lookforUsers(Value* vv, std::string pos,Instruction* usein, DominatorTree &DT, wklist_def * wklist, m_wklist_def * m_wklist, SmallVector<Instruction*,6> * useinlist){
	
	SmallVector<Instruction*,6> uslst;
	
	// all the users in uslst	
	for (Value::user_iterator i = vv->user_begin(), e = vv->user_end(); i != e; ++i){
		Value *vvt = *i;
		
		if (isa<Instruction>(vvt)){
			Instruction* vvto = dyn_cast<Instruction>(vvt); 
			Function * vfun = vvto->getParent()->getParent();
			
			Function * fun = usein->getParent()->getParent();
			
			if (fun == vfun){
				if (isa<StoreInst>(vvto) ){	
					#ifdef INSTRU_DEBUGINFO
					errs()<< "The mem def store user :" << printTextInfo(vvto)  << "\n";
					#endif

					if (isDominates(vvto, usein,DT)) uslst.push_back(dyn_cast<StoreInst>(vvto));
					
				} else if (isa<GetElementPtrInst>(vvto)){
					
					if (isDominates(vvto, usein,DT)) uslst.push_back(dyn_cast<Instruction>(vvto));
					#ifdef INSTRU_DEBUGINFO
					errs()<< "The mem def GetElementPtrInst user :" << printTextInfo(vvto)  << "\n";
					#endif
				}
			}
		}		
	}
	
	SmallVector<Instruction*,6> uslst2;	
	SmallVector<Instruction*,6> uslst3;
	
	while (!uslst.empty()) {
		Instruction* sin =  uslst.pop_back_val();
	
		/// if sin is the newest, put it into uslst2
		bool hasafter = false;
	
		for (SmallVector<Instruction*,6>::iterator ii = uslst.begin(); ii != uslst.end(); ++ii) {			
			Instruction* sss =  *ii;	
			if (isDominates(sin,sss, DT)) { // sss is after sin
				hasafter = true;
				break;
			}			
		}
		if (!hasafter){
			uslst2.push_back(sin);
		} else uslst3.push_back(sin); // all the others in uslst3
	}
	
	if (uslst2.empty()) {
		return;
	}
	
	while (!uslst2.empty()) {
		Instruction* sin =  uslst2.pop_back_val();
		
		SmallVector<Instruction*,6> uslst4;
		uslst4.push_back(sin);

		for(size_t it=0;it!=uslst3.size();++it){ 
			Instruction * sss = uslst3[it];
			if (sss==0 || sss==NULL || sss==nullptr) continue;
			if (isDominates(sss,sin, DT)) { // sss is before sin
				uslst4.push_back(sss);
				uslst3[it] = NULL;
			}
		} 
		
		int num = uslst4.size();
		int uper = num;		
		while (uper > 1){
			for(int it=0;it<uper-1;++it) {
				if (isDominates(uslst4[it],uslst4[it+1], DT)) {
					Instruction* tmp = uslst4[it];
					uslst4[it] = uslst4[it+1];
					uslst4[it+1] = tmp;
				}
			}			
			uper--;
		}

		#ifdef INSTRU_DEBUGINFO	
		for (int it=0;it<num;++it){
			errs()<< "The content in uslst4 333 is :" << printTextInfo(uslst4[it])  << "\n";	
		}
		#endif
		
		wklist_def t_worklist = *wklist;	
		
		while (!uslst4.empty())	{
			Instruction* tmp = uslst4.pop_back_val();
			t_worklist.push_back(getwkone(tmp,pos,usein));
		}
		
		m_wklist->push_back(t_worklist);		
		useinlist->push_back(usein);		
	}	

	(void)useinlist->pop_back_val();	
	*wklist = m_wklist->pop_back_val();	
	
	return;
}	

/// if the vaule V in pos is a pointer 
bool ST_ValueUtilize::isToPointer(Value* V,std::string pos) {
    Type* T = V->getType();
	
	Type* Tpos;
	if (T->isPointerTy()){
		return true;
	}	
	Tpos = getTypeAtPos(T,pos);
    return Tpos->isPointerTy();	
}

// if the value V in pos is a pointer of pointer
bool ST_ValueUtilize::isPointerToPointer(Value* V,std::string pos) {
    Type* T = V->getType();
	
	Type* Tpos;
	if (T->isPointerTy()){
		Tpos = getTypeAtPos(T->getPointerElementType(),pos);
		return Tpos->isPointerTy();
	}	
	Tpos = getTypeAtPos(T,pos);

    return Tpos->isPointerTy() && Tpos->getContainedType(0)->isPointerTy();	
}

/// is the called instruction called no empty function
bool ST_ValueUtilize::isCalledNoEmpty(Value* V) {
	bool rst = false;
	if (isa<CallInst>(V)){
		Function* cfun = dyn_cast<CallInst>(V)->getCalledFunction(); 	
		if (cfun==0 || cfun==NULL || cfun == nullptr) return rst;
		//if (cfun->empty()) return rst;
		rst = true;
	}
	return rst;
}

///get the type index by the pos string ,such as "0 2 4", always start with "0"
Type* ST_ValueUtilize::getTypeAtPos(Type* Agg, std::string pos) {
	
	if (pos == "" || pos == "0") return Agg;	
	if (!isa<CompositeType>(Agg)) return Agg;
	
	SmallVector<int,5> posvector;
	posvector = getVectorfromPos(pos);
	
	//delete first 0
	if (!posvector.empty()){ 
		(void)posvector.pop_back_val();
	}
	while (!posvector.empty()){	
		unsigned Index = (unsigned)posvector.pop_back_val();

		CompositeType *CT = dyn_cast<CompositeType>(Agg);
		if (!CT) return nullptr;
		if (!CT->indexValid(Index)) return nullptr;
		Agg = CT->getTypeAtIndex(Index);
	}	
	return Agg;
}

///get the offset in bytes to the index by the pos string ,such as "0 2 4", always strat with "0"
unsigned ST_ValueUtilize::getOffsetToPos(Type* Agg, std::string pos,const DataLayout &DL) {
	
	if (pos == "" || pos == "0") return 0;	
	
	if (Agg->isPointerTy())
			Agg = Agg->getPointerElementType();
	
	if (!isa<CompositeType>(Agg)) return 0;
	
	Type *Int32Ty = Type::getInt32Ty(Agg->getContext());
	
	SmallVector<int,5> posvector;
	posvector = getVectorfromPos(pos);
	
	SmallVector<Value *, 8> Indices;
	while (!posvector.empty()){	
		unsigned Index = (unsigned)posvector.pop_back_val();		
		Indices.push_back(ConstantInt::get(Int32Ty, Index));
	}
	
	unsigned Offset = (unsigned)DL.getIndexedOffsetInType(Agg, Indices);
	
	return Offset;
}

SmallVector<int,5> ST_ValueUtilize::getVectorfromPos(std::string posstr) {
	SmallVector<int,5> rst;
	std::string deli = " ";
	size_t pos =0;
	while ((pos = posstr.find(deli)) != std::string::npos) {
		std::string tp = posstr.substr(0,pos);
		rst.push_back(atoi(tp.c_str()));
		posstr = posstr.erase(0,pos+deli.length());
	}	
	rst.push_back(atoi(posstr.c_str()));
	
	//return rst;
	SmallVector<int,5> rrst;
	while (!rst.empty()){
		rrst.push_back(rst.pop_back_val());
	}	
	return rrst;
}
 
///find a pointer get its address from, a global var or a function parameter
///from local declaration {def,pos} trace back to global var or a function parameter
std::set<ST_ValueUtilize::MemVarPos_def> ST_ValueUtilize::getPointerAddressFrom(Value* def, std::string defpos,Instruction* pusein, DominatorTree &DT) {
	
	std::set<MemVarPos_def> m_rst;

	m_wklist_def m_wklist;
	wklist_def wklist;
	SmallVector<Instruction*,6> useinlist;
	
	/// we do not care about the operation on pointer of pointer ?
	/// add by 18-2-20
	if (def->getType()->isPointerTy()){
		Type* Tpos = getTypeAtPos(def->getType()->getPointerElementType(),defpos);
		bool isdefpt = Tpos->isPointerTy() && Tpos->getPointerElementType()->isPointerTy();
		//errs()<< "Is the def pointer of pointer :" << isdefpt  << " : " << printTextInfo(def) << "\n";
		if (isdefpt) return m_rst;
	}
	/// end add by 18-2-20

	wklist.push_back(getwkone(def,defpos,pusein));
	
	std::set<Value *> wkedset;
	
	Instruction* usein = pusein;
	
	do {		
		MemVarPos_def rst = std::make_pair(nullptr,"");
	
		while (!wklist.empty()) {

		wklist_one_def wkdef = wklist.pop_back_val();
			
			Value* vv = wkdef.var;
			std::string pos = wkdef.pos;	
			usein = wkdef.usein;

			MemVarPos_def vvdef = std::make_pair(vv,pos);	
			
			// new change
			//if (wkedset.find(vv)!=wkedset.end()) continue;  /// we have worked on the mem def
			
			#ifdef INSTRU_DEBUGINFO
			errs()<< "\n The current vv XXXXXXX is :" << printTextInfo(vv) << "\n";
			errs()<< "The current vv XXXXXXX pos is :" << pos << "\n";			
			errs()<< "The current usein XXXXXXX is :" << printTextInfo(usein) << "\n\n";
			
			for (wklist_one_def wk : wklist) {
				
				errs()<< "The wklist vv in wklist  is :" << printTextInfo(wk.var) << "\n";
				errs()<< "The wklist in wklis pos is :" << wk.pos << "\n";			
				errs()<< "The wklist usein in wklis is :" << printTextInfo(wk.usein) << "\n";			
			
			}
			errs()<< " \n";			
			#endif
			
			if(isa<Instruction>(vv)){	
				if (!isDominates(dyn_cast<Instruction>(vv), usein,DT)) { // not vv older than usein
					#ifdef INSTRU_DEBUGINFO
					errs()<< "we skip the instruction :" << printTextInfo(vv) << "\n";
					#endif
					continue;	
				}
			}
			
			if (isa<StoreInst>(vv) ){	
				#ifdef INSTRU_DEBUGINFO
				//store from
				Value *vval0 = dyn_cast<Instruction>(vv)->getOperand(0); 
				Type *vtype0 = vval0->getType();				
				if (vtype0->getPointerElementType()->isPointerTy()) {					
					errs()<< "Store from pointer of pointer vv XXXXXXX:" << printTextInfo(vv) << "\n";				
				}				
				errs()<< "we push the store from instruction XXXXXXX:" << printTextInfo(vval0) << "\n";					
				#endif
				
				wklist.push_back(getwkone(dyn_cast<Instruction>(vv)->getOperand(0),pos,dyn_cast<Instruction>(vv)));

			}else if (isa<GlobalValue>(vv)) {
				
				bool isgvp = isPointerToPointer(vv,pos);	

				// should we use the ini instruction to judge ?
				int pp = isa<StoreInst>(pusein)? 1 : 0;				
				bool isuip = isPointerToPointer(pusein->getOperand(pp),pos);	
				
				//int pp = isa<StoreInst>(usein)? 1 : 0;				
				//bool isuip = isPointerToPointer(usein->getOperand(pp),pos);	
				
				#ifdef INSTRU_DEBUGINFO
				errs()<< "Global pointer found ispp :" << isgvp << "\n";	
				errs()<< "Global pointer found ispp2 :" << isuip << "\n";	
				
				errs()<< "The Global var type is :" << printTypeInfo(vv) << "\n";	
				errs()<< "The usein oper type is :" << printTypeInfo(pusein->getOperand(pp)) << "\n";
				#endif
				
				if (isgvp && (!isuip)){
					lookforUsers(vv, pos, usein, DT, &wklist,&m_wklist,&useinlist);
				}else {	
					rst = vvdef;				
					break;					
				}			
			//} else if (isInFunArgs(vv,F)){
			}else if (isa<Argument>(vv) && isToPointer(vv,pos)){	
				rst = vvdef;
				break;				
			} else if (isa<LoadInst>(vv)){

				wklist.push_back(getwkone(dyn_cast<Instruction>(vv)->getOperand(0),pos,dyn_cast<Instruction>(vv)));
				
				lookforUsers(vv, pos, usein, DT, &wklist,&m_wklist,&useinlist);
				
			} else if (isa<SelectInst>(vv)){
				Instruction* ins = dyn_cast<Instruction>(vv);
				int ind = 0;
				Value *tyval = NULL;
				for ( auto &op : ins->operands()) {
					tyval = op.get();
					if(ind==1){
						pushToWklist(tyval, pos,usein, DT, &wklist);
					}else if(ind>1){
						wklist_def t_worklist = wklist;	
						if (pushToWklist(tyval, pos,usein, DT, &t_worklist)){
							m_wklist.push_back(t_worklist);
							useinlist.push_back(usein);
						}
					}	
					ind++;
				}
			}else if (isa<PHINode>(vv)){
				PHINode *PN = dyn_cast<PHINode>(vv);				
				int numArgs = PN->getNumIncomingValues();
				
				for (int idx = 0; idx < numArgs; idx++) {
					Value *tyval = PN->getIncomingValue(idx);				
					if(idx==0){
						pushToWklist(tyval, pos,usein, DT, &wklist);
					}else if(idx>0){
						wklist_def t_worklist = wklist;	
						if (pushToWklist(tyval, pos,usein, DT, &t_worklist)){
							m_wklist.push_back(t_worklist);
							useinlist.push_back(usein);
						}
					}				
				
				}
			}else if (isa<AllocaInst>(vv)){
				
				bool ispp = isPointerToPointer(vv,pos);
				
				if (ispp){
					lookforUsers(vv, pos, usein, DT, &wklist,&m_wklist,&useinlist);
				} else { //to a local variable	
					break;
				}
				
			} if (isa<BitCastInst>(vv)){
				Value* ldfrom =  dyn_cast<BitCastInst>(vv)->getOperand(0);
				
				wklist.push_back(getwkone(ldfrom,pos,dyn_cast<Instruction>(vv)));
			
				
			} else if (isa<GetElementPtrInst>(vv) || isGetElementPtrConstantExpr(vv)){

				MemVarPos_def tgvar = getInstrWorkOn(vv);
				
				#ifdef INSTRU_DEBUGINFO
				errs()<< "The tgvar vv is :" << printTextInfo(tgvar.first)  << "\n";
				errs()<< "The tgvar new pos is :" << tgvar.second  << "\n";
				errs()<< "The pos is :" << pos  << "\n";
				#endif
				
				//do some things with pos
				std::string deli = "0 ";
				std::string newpos = tgvar.second;
				
				if (newpos.find(deli)!=0 && pos.find(deli)==0){	
					SmallVector<int, 6> pvect =  push_posStr(pos);
					int lp = pvect.pop_back_val()+atoi(newpos.c_str());
					pvect.push_back(lp);
					pos = pop_posStr(pvect);
				}else if(newpos.find(deli)==0 && pos.find(deli)==0){
					pos = newpos;
				}else {
 				if (pos == "" || pos =="0") {
					pos = tgvar.second;
				}else {
					/// remove "0" from pos
					//std::string deli = "0 ";
					size_t p;
					if ((p = pos.find(deli)) != std::string::npos) {
						pos = pos.erase(0,p+deli.length());
						pos = tgvar.second+" "+pos;
					}else {
						if ((p = tgvar.second.find(deli)) != std::string::npos) {
							
							std::string sss = tgvar.second;
							std::string ss = sss.erase(0,sss.rfind(" "));
							sss = tgvar.second;
							int np = atoi(pos.c_str())+atoi(ss.c_str());
							pos = sss.substr(0,sss.length()-ss.length())+" "+std::to_string(np);
						}else{
							int np = atoi(pos.c_str())+atoi(tgvar.second.c_str());
							pos = std::to_string(np);
						}	
					}	
		
				} 
				}
				
				wklist.push_back(getwkone(tgvar.first,pos,dyn_cast<Instruction>(vv)));
				
				///new add ?
 				if (isa<GetElementPtrInst>(vv)){
					lookforUsers(vv, pos, usein, DT, &wklist,&m_wklist,&useinlist);
				
				} 				
				#ifdef INSTRU_DEBUGINFO
				errs()<< "The new pos is BBBBBB :" << pos  << "\n";
				#endif
			}			
			
			
			
/* 				
				/// in which case, "0 1" could because "0 1 2"
 				if (pos == "" || pos =="0") {
					pos = tgvar.second;
				}else {
					/// remove "0" from pos
					std::string deli = "0 ";
					size_t p;
					if ((p = pos.find(deli)) != std::string::npos) { //found
						pos = pos.erase(0,p+deli.length());
						pos = tgvar.second+" "+pos;
					}else {
						if ((p = tgvar.second.find(deli)) != std::string::npos) {
							
							std::string sss = tgvar.second;
							std::string ss = sss.erase(0,sss.rfind(" "));
							sss = tgvar.second;
							int np = atoi(pos.c_str())+atoi(ss.c_str());
							pos = sss.substr(0,sss.length()-ss.length())+" "+std::to_string(np);
						}else{
							int np = atoi(pos.c_str())+atoi(tgvar.second.c_str());
							pos = std::to_string(np);
						}	
					}	
					//pos = tgvar.second+" "+pos;					
				} 
				
				//in which case, we need to calca the pos ??
				//pos = tgvar.second;
				/// ????
				
				
				wklist.push_back(std::make_pair(tgvar.first,pos));
				
				#ifdef INSTRU_DEBUGINFO
				errs()<< "The new pos is BBBBBB :" << pos  << "\n";
				#endif
				
				
			} else if (isGetElementPtrConstantExpr(vv)){	///definition of global arrows  
				MemVarPos_def tgvar = getInstrWorkOn(vv);
				wklist.push_back(tgvar);
			} */
			
			if(isa<Instruction>(vv) && (!isa<AllocaInst>(vv)) ){
			//new add
			//if(isa<Instruction>(vv) && (!isa<AllocaInst>(vv)) && (!isa<GetElementPtrInst>(vv)) ){	
				usein = dyn_cast<Instruction>(vv); 
			}
			//new add
			//if(isa<Instruction>(vv) && (!isa<AllocaInst>(vv)) ){
			//	wkedset.insert(vv);
			//}
			
			//if (isa<GetElementPtrInst>(vv)){
				wkedset.insert(vv);
			//}	
		}
		
		if (rst.first != NULL)
			m_rst.insert(rst);

		if (m_wklist.empty()) break;
		
		wklist = m_wklist.pop_back_val();
		
		usein = useinlist.pop_back_val();

	} while (true);		
	
	return m_rst;
}

//find the definition of a Value
Value* ST_ValueUtilize::chasePointerDef(Value* ptr,std::string* pos) {
	Value * rst = NULL;
	if (isa<LoadInst>(ptr)){		
		Value* ldfrom =  dyn_cast<LoadInst>(ptr)->getOperand(0);
		rst = chasePointerDef(ldfrom,pos);

	} else if (isa<GetElementPtrInst>(ptr) || isGetElementPtrConstantExpr(ptr)){			

		MemVarPos_def tgvar = getInstrWorkOn(ptr);	
		
		*pos = tgvar.second;
		rst = tgvar.first;
	} if (isa<BitCastInst>(ptr)){
		Value* ldfrom =  dyn_cast<BitCastInst>(ptr)->getOperand(0);
		rst = chasePointerDef(ldfrom,pos);	
	} else if (isa<AllocaInst>(ptr) || isa<SelectInst>(ptr) || isa<GlobalValue>(ptr) || isa<Argument>(ptr)) {	   // a local variable	
		rst = ptr;
	} 	
	return rst;
}

/// is it the instruction accessing the memory we want monitor
bool ST_ValueUtilize::isTraceLoadStoreInstr(Instruction *I, const DataLayout &DL ){
	
	bool rst = false;
	
	if( !(isa<StoreInst>(I) || isa<LoadInst>(I)))
		return rst;
	
	if (StoreInst *Store = dyn_cast<StoreInst>(I)) {
	    Value *gv = Store->getPointerOperand();
		if ((!isMemoryFromAddress(gv)) || isExternal(gv)){
		} else rst=true;		
	} else if (LoadInst *Load = cast<LoadInst>(I)) {
		Value *gv = Load->getPointerOperand();
		if ((!isMemoryFromAddress(gv)) || isConstantDataAddr(gv) || isExternal(gv)){
		} else rst=true; 
	}
	Value *Addr = isa<StoreInst>(*I)
		? cast<StoreInst>(I)->getPointerOperand()
		: cast<LoadInst>(I)->getPointerOperand();
	if (isa<AllocaInst>(GetUnderlyingObject(Addr, DL)) &&
		!PointerMayBeCaptured(Addr, true, true)) {
	  // The variable is addressable but not captured, so it cannot be
	  // referenced from a different thread and participate in a data race
	  // (see llvm/Analysis/CaptureTracking.h for details).
		rst=false;	 
	}
	return rst;		
}

/// if the instruction followed by a icmp and then a br instruction
/// input: the instruction inst
/// output: the icmp instruction and the no error branch
std::pair<Instruction*, BasicBlock*> ST_ValueUtilize::getCompNoErrBr(Instruction* inst){

	Instruction* cmpinst = NULL;
	BasicBlock * noErrBr = NULL;
	
	for (Value::user_iterator i = inst->user_begin(), e = inst->user_end(); i != e; ++i){
		if (ICmpInst *cmpi = dyn_cast<ICmpInst>(*i)) {
			for (Value::user_iterator j = cmpi->user_begin(), e = cmpi->user_end(); j != e; ++j){
				if (BranchInst  *bri = dyn_cast<BranchInst >(*j)) {
					
					int cmpNum = getInt32Value(cmpi->getOperand(1));
					//std::string cmpop = cmpi->getOpcodeName();
					BasicBlock * trueBr = dyn_cast<BasicBlock>(bri->getOperand(2));
					BasicBlock * falseBr = dyn_cast<BasicBlock>(bri->getOperand(1));
					
					enum CmpInst::Predicate pre = cmpi->getUnsignedPredicate(); 
					if (cmpNum == 0) { // = 0 : true branch, <> 0 , >0 : false branch  
						switch (pre) {
							case CmpInst::ICMP_NE: {
								cmpinst = cmpi;
								noErrBr = falseBr;
								break;
							}
							case CmpInst::ICMP_EQ: {
								cmpinst = cmpi;
								noErrBr = trueBr;
								break;
							}
							case CmpInst::ICMP_UGT: {
								cmpinst = cmpi;
								noErrBr = falseBr;
								break;
							}
							default: {
								break;
							}	
						}
					} else if (cmpNum == 3 && pre == CmpInst::ICMP_EQ) { // error number, = 3 : false branch  
						cmpinst = cmpi;
						noErrBr = falseBr;
					}
					break;
				}
			}
		}
	}

	return std::make_pair(cmpinst,noErrBr);
}	

/// check if there is a "common" in the declaration of the GlobalVar
bool ST_ValueUtilize::isGlobalVarInitializ(Value * gv){
	
	bool res = true;
	
	std::string s = printTextInfo(gv);
	std::string deli = "= common ";
	
	if (s.find(deli) != std::string::npos ) res = false;

	return res;		
}

/// show all callers and callee of a function 
std::string  ST_ValueUtilize::printCalls(Function* fun){

	std::string Str;
	raw_string_ostream OSS(Str);
	
	std::string sss = fun->getName().str();	
	OSS << "The function is : " << sss << "\n";
	
 	OSS << "    calling : " << "\n"; 
	for (Value::use_iterator i = fun->use_begin(), e = fun->use_end(); i != e; ++i){				
		OSS << " : "<< printTextInfo(*i) << "\n";	
		
		if (Function *cfun = dyn_cast<Function>(*i)) {
			OSS << "    calling : " << cfun->getName().str() << "\n";  
		}  
	} 
	
	OSS << "\n";
	OSS.flush();
	return Str;		

}

/// show all callers and callee of a function 
std::string  ST_ValueUtilize::printCalledby(Function* fun){

	std::string Str;
	raw_string_ostream OSS(Str);
	
	std::string sss = fun->getName().str();	
	OSS << "The function is : " << sss << "\n";

	for (Value::user_iterator i = fun->user_begin(), e = fun->user_end(); i != e; ++i){				
		if (CallInst *cin = dyn_cast<CallInst>(*i)) {
			OSS << "  called by instruction : "<< printTextInfo(*i) << "\n";	
			Function * newfun = dyn_cast<Function>(cin->getParent()->getParent());
			if (Function* fun2 = dyn_cast<Function>(newfun)) {
				OSS <<"    called by function: " << fun2->getName().str() << "\n";  
			} 
		}
	}		
	OSS << "\n";
	OSS.flush();
	return Str;	
}

/// is the function called by other functions, not an orphan 
bool  ST_ValueUtilize::hasCaller(Function* fun){

	if (fun->getName()=="main") return true;
	for (Value::user_iterator i = fun->user_begin(), e = fun->user_end(); i != e; ++i){
		if( isa<CallInst>(*i))
			return true;
	}	
	return false;
}

/// get the target of the instruction working on, for example: a global variable.
/// input: for call instruction, the first operand,
/// output: first - the variable declaration it working on,  second - the pos string if there is is a getelementptr 
ST_ValueUtilize::MemVarPos_def ST_ValueUtilize::getInstrWorkOn(Value* val){
	
	Value* vdel = val;
	std::string pos = "";
	
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(vdel)) {
		if (CE->getOpcode() == Instruction::GetElementPtr) {
			vdel = CE->getOperand(0);
			for (unsigned i=1;i < CE->getNumOperands(); i++)	{
				
				std::string pp = std::to_string(getInt32Value(CE->getOperand(i))); 
				if (pos==""){
					pos = pp;
				} else {
					pos = pos + " " + pp;
				}
			}		
		}
	} else if (isa<GetElementPtrInst>(vdel)){
		GetElementPtrInst* rrr = dyn_cast<GetElementPtrInst>(vdel); 
		vdel = rrr->getOperand(0);
		for (unsigned i=1;i < rrr->getNumOperands(); i++)	{
			std::string pp = std::to_string(getInt32Value(rrr->getOperand(i))); 
			if (pos==""){
				pos = pp;
			} else {
				pos = pos + " " + pp;
			}
		}		
	}else {
		pos = "";
    }		

	return std::make_pair(vdel,pos);
}

/// print the operands of a User
std::string ST_ValueUtilize::printOperands(Value* val){

	std::string Str;
	raw_string_ostream OSS(Str);

	std::string nm = "";
	if (val->hasName())
		nm = val->getName().str();
	
	OSS << "The Value Name is : " << nm << "\n";	
	
	if( isa<Instruction>(val)){
		Instruction* ins = dyn_cast<Instruction>(val); 
		
		Value *tyval = NULL;
		int ind = 0;
		for ( auto &op : ins->operands()) {
			tyval = op.get();
			OSS << " index : " << ind << " : "<< printTextInfo(tyval) << "\n";
			ind++;
		}		
		
	} if( isa<GlobalVariable>(val)){
		
		GlobalVariable* ins = dyn_cast<GlobalVariable>(val); 
		
		Value *tyval = NULL;
		int ind = 0;
		for ( auto &op : ins->operands()) {
			tyval = op.get();
			OSS << " index : " << ind << " : "<< printTextInfo(tyval) << "\n";
			ind++;
		}		
	} else if( isa<User>(val)){
		User* ins = dyn_cast<User>(val); 
		
		Value *tyval = NULL;
		int ind = 0;
		for ( auto &op : ins->operands()) {
			tyval = op.get();
			OSS << " index : " << ind << " : "<< printTextInfo(tyval) << "\n";
			ind++;
		}		
	
	}
	
	OSS << "\n";
	OSS.flush();
	return Str;		
}

std::string  ST_ValueUtilize::printUses(Value* val){
	
	std::string Str;
	raw_string_ostream OSS(Str);		

	std::string sss = val->getName().str();

	OSS << "The Value is : " << sss << "\n";
	
	for (Value::use_iterator i = val->use_begin(), e = val->use_end(); i != e; ++i){
		Value *vv = *i;
		std::string s = printTextInfo(vv);
		OSS << s << "\n";
	}
		
	OSS << "\n";
	OSS.flush();
	return Str;	
}

std::string  ST_ValueUtilize::printUsers(Value* val){
	
	std::string Str;
	raw_string_ostream OSS(Str);		

	std::string sss = val->getName().str();	
	OSS << "The Value is : " << sss << "\n";
	
	for (Value::user_iterator i = val->user_begin(), e = val->user_end(); i != e; ++i){
		Value *vv = *i;
		std::string s = printTextInfo(vv);
		OSS << s << "\n";
	}

	OSS << "\n";
	OSS.flush();
	return Str;	
}

ST_ValueUtilize::VarTypeStru ST_ValueUtilize::analyseType(Value* val){
	
    VarTypeStru tystru = analyseType(val->getType());
	
	if (val->hasName()) {
		tystru.varName = val->getName().str();
	} 
	
	return tystru;	
}		

/// from the list of all global variables,
///get a list of the global declare locks that have been static initialized using PTHREAD_MUTEX_INITIALIZER 
ST_ValueUtilize::VarTypeList_def ST_ValueUtilize::getMutexList(ST_ValueUtilize::VarTypeList_def * gvList){
	
	VarTypeList_def mutexsList;
	VarTypeStru tyst;
	
	for(auto p = gvList->begin(); p!=gvList->end(); ++p){	
		tyst = *p;
		if (tyst.tyName == LOCK_VARDECLARE) {
			if (isGlobalVarInitializ(tyst.var)) {
				mutexsList.push_back(tyst);
				//Debug( "Global locks:: Name :" << tyst.varName <<" baseTyName :" << tyst.baseTyName <<" Pointer :" << tyst.isPointer  <<" tyName :" << tyst.tyName << "  bitwide:" << tyst.bitwide << "  num:" <<tyst.num <<"  pos:" <<tyst.pos <<"\n\n"); 
			} 
			//Debug( "Global locks:: Name :" << tyst.varName <<" baseTyName :" << tyst.baseTyName <<" Pointer :" << tyst.isPointer  <<" tyName :" << tyst.tyName << "  bitwide:" << tyst.bitwide << "  num:" <<tyst.num <<"  pos:" <<tyst.pos <<"\n\n"); 
			
		}
	}
	return mutexsList;
}

///Deal with global variable declarations, output the result 
ST_ValueUtilize::VarTypeList_def ST_ValueUtilize::doListGlobalVar(Module &m) {
	
	VarTypeList_def globalVarList;

    for (llvm::Module::global_iterator ii = m.global_begin(); ii != m.global_end(); ++ii) {		
		GlobalVariable * gv = &(*ii);
		if( isa<llvm::GlobalObject >(gv)){			
			if (isConstantDataAddr(gv) || isExternal(gv) || gv->getName().startswith("__llvm_gcov") ||
					gv->getName().startswith("__llvm_gcda")) {
				continue;				
			}else{				
				//errs()<< "The global var :" << printTextInfo(gv) << "\n";
				//errs()<< "The global var :" << gv->getName()<< "\n";
				
				Value * varVal = gv->getOperand(0);
				Type * varType = varVal->getType();
				
				std::string tynm = getTypeName(varType);				
				bool startlkother = startsWith(tynm,LOCK_VARDECLARESTART) || startsWith(tynm,LOCK_FULLVARDECLARESTART);
				
				if (startlkother && tynm !=LOCK_VARDECLARE && tynm !=LOCK_CONDIVARDECLARE){
					continue;
				}	
				
				doAnalyseType(varType, varType,gv, "0", &globalVarList);
			}
		 }
	}

	/// find if a global variable is a pthread_t
	/// and mark it tyName as PTHREAD_VARDECLARE in globalVarList
	for (Module::iterator f = m.begin(); f != m.end(); ++f) {
        if (f->empty()) continue;	
		//Function* fun = &*f; 	
		for (Function::iterator bb = f->begin(), be = f->end(); bb != be; ++bb) {		
			for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
				Instruction* ins = &(*ii);				
				if( isa<CallInst>(ins) || isa<InvokeInst>(ins)){
					CallInst* instr = dyn_cast<CallInst>(ins);

					if (getLastOperName(instr)== INS_PTHREADCREATE) {
						Value* vdel = instr->getOperand(0);			
						MemVarPos_def tgvar = getInstrWorkOn(vdel);	
						
						if (tgvar.second=="") tgvar.second="0";

						///go through globalVarList to check if it is a Vutilize.PTHREAD_VARDECLARE
						VarTypeStru tyst;
						for(auto p = globalVarList.begin(); p!=globalVarList.end(); ++p){			
							tyst = *p;
							if (tyst.var == tgvar.first && tyst.pos == tgvar.second) {
								(*p).tyName = PTHREAD_VARDECLARE;
								//errs() << "We find the PTHREAD_VARDECLARE var :" << tyst.varName <<"\n\n"; 
							}
						}
					}
				}			
			}
		} 		
	}	

	return globalVarList; 
} 

///Is it a external declaration
bool ST_ValueUtilize::isExternal(Value * val){
	std::string vardef = printTextInfo(val); 

	if (vardef.find("= external") == std::string::npos) {
		return false;		
	}else{	
		return true;
	}	
}

/// commented because change of function names from llvm 4 to llvm 5 
/* bool ST_ValueUtilize::isAtomic(Instruction *I) {
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->isAtomic() && LI->getSynchScope() == CrossThread;
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->isAtomic() && SI->getSynchScope() == CrossThread;
  if (isa<AtomicRMWInst>(I))
    return true;
  if (isa<AtomicCmpXchgInst>(I))
    return true;
  if (isa<FenceInst>(I))
    return true;
  return false;
} */

/// is a LOCK_FULLVARDECLARE in the definition of the Value	
bool ST_ValueUtilize::isPthreadMutex(Value * val){
	std::string def = printTextInfo(val); 

	if (def.find(LOCK_FULLVARDECLARE) == std::string::npos) {
		return false;		
	}else{	
		return true;
	}	
}

/// is a local static initialized mutex lock	
bool ST_ValueUtilize::isLocalStaticIniLock(Instruction * inst){
	
	if (AllocaInst* ainst = dyn_cast<AllocaInst>(inst)) {	
		std::string tynm = printTypeInfo(ainst);
		if (startsWith(tynm,LOCK_FULLVARDECLARE)){
			for (Value::user_iterator i = ainst->user_begin(), e = ainst->user_end(); i != e; ++i){
				Value *vv = *i;
				if(BitCastInst* binst = dyn_cast<BitCastInst>(vv)){
					for (Value::user_iterator j = binst->user_begin(), ej = binst->user_end(); j != ej; ++j){
						Value *bvv = *j;	
						if(CallInst* callinstr = dyn_cast<CallInst>(bvv)){
							if (startsWith(getLastOperName(callinstr),LLVM_MEMSETSTART)) {
								//errs()<< "mem set llvm :" << printTextInfo(callinstr)  << "\n";
								return true;
							}	
						}	
					}
				}	
			}						
		}	
	
	}	

	return false;
}



/// is the function recursive
/// input the function, a empty set of function
/* TODO: There's already LLVM's Function::doesNotRecurse, but we really want "don't know" separately. */
bool ST_ValueUtilize::isFunRecursive(Function * fun,std::set<Function*> calledfuns){
	bool rst = false;
	
	//debug info
/* 	errs()<< "Testing : " << fun->getName() << "\n";
	for ( auto ff : calledfuns ) {
		errs()<< "    the called set  : " << ff->getName() << "\n";
	} */
	//end debug info	
	
	if(calledfuns.find(fun) == calledfuns.end()) { //not find
		for (Value::user_iterator i = fun->user_begin(), e = fun->user_end(); i != e; ++i){				
			if (CallInst *cin = dyn_cast<CallInst>(*i)) {				
				//Function * newfun = cin->getCalledFunction();
				Function * newfun = dyn_cast<Function>(cin->getParent()->getParent());
				assert (!newfun->empty());
				if ( ! newfun->empty()) {	
					calledfuns.insert(fun);
					/* This does not seem right. Assume:
                                            foo() {
                                              bar();
					      foo();
					    }
					  Q: isFunRecursive(bar,{})?
					  A: Code will find CallSite in foo(), start analysing foo(), and notice that foo() is recursive,
					     hence break and report true for bar().
					*/
					rst = isFunRecursive(newfun,calledfuns);
					if (rst) break;
				}
			}  			
		}
	} else 
		rst = true;
	return rst;
}

/// analysis the global variable definition
/// output the result to parameter varVect
void ST_ValueUtilize::doAnalyseType(Type* ty, Type* baseTy, Value* var, std::string posStr, VarTypeList_def * varVect){
	
    //errs()<< "The type we working on :" << printTypeInfo(ty) <<  "  and number :" << num <<"\n";	
	std::string tynm = getTypeName(ty);
	
	if (!ty->isAggregateType() || tynm ==LOCK_VARDECLARE || tynm ==LOCK_CONDIVARDECLARE){
		
		//errs()<< "going to have a new pushed  :" << "\n";			
		VarTypeStru tystru;
		
		if (var->hasName())
			tystru.varName = var->getName().str();

		tystru.baseTy = baseTy;
		tystru.baseTyName = getTypeName(baseTy);
		
		tystru.isPointer = ty->isPointerTy();
		
		//errs()<< "going to have a new pushed  2 :" << tystru.baseTyName << "\n";	
		
		tystru.ty = ty;
		tystru.var = var;
		
		if (ty->isPointerTy()) {	
			Type* cty =  ty->getPointerElementType();
			tystru.tyName = getTypeName(cty);
			tystru.bitwide= cty->getPrimitiveSizeInBits();
		} else {
			tystru.tyName = getTypeName(ty);
			tystru.bitwide= ty->getPrimitiveSizeInBits();
		}	
		
		//errs()<< "going to have a new pushed  3 :" << tystru.tyName << "\n";	
		
		tystru.pos = posStr;
		
		std::string s = printTypeInfo(ty);
		//errs()<< "  The type defing :" <<tystru.tyName << "  and def :" << s << "\n";
		//errs()<< "Going to push :" <<tystru.tyName << "  and number :" << num << "\n";	
	
		varVect->push_back(tystru);

	    //errs()<< "it is The pushed  :" << printTextInfo() "\n";
		//errs()<< "it is The Aggregate  :" << "\n";

    }else if (ty->isArrayTy() ) {	
		
		//debug
		//std::string tydef2 = printTypeInfo(ty->getArrayElementType());		
		//errs() <<  "The Array type : " << tydef2 << "\n" ;  
		
		Type* eTy = ty->getArrayElementType();			
		int num = int(ty->getArrayNumElements());
		
		for (int i=0;i<num;i++){
			std::string newpos = posStr+" "+std::to_string(i);
			doAnalyseType(eTy, baseTy, var, newpos, varVect);		
		}
	
	} else if (ty->isVectorTy()) {	
		
		//debug
		//std::string tydef2 = printTypeInfo(ty->getArrayElementType());		
		//errs() <<  "The Vector type : " << tydef2 << "\n" ; 

		Type* eTy = ty->getVectorElementType();			
		int num = int(ty->getVectorNumElements());		
		
		for (int i=0;i<num;i++){
			std::string newpos = posStr+" "+std::to_string(i);
			doAnalyseType(eTy, baseTy, var, newpos, varVect);		
		}

	} else {
		
		int ct = 0;
		
		//errs()<< "The agg name :" << getTypeName(ty) << "  and number :" << getTypeNum(ty) << "\n";	
		
		for (Type::subtype_iterator i = ty->subtype_begin(), e = ty->subtype_end(); i != e; ++i){
			Type *cty = *i;
			
			std::string newpos = posStr+" "+std::to_string(ct);
			
			//errs()<< "Going to analysis :" << getTypeName(cty) << "  and number :" << newnum << "\n";
			
			doAnalyseType(cty, baseTy, var, newpos, varVect);
	
			ct++;
		}
    }	
	return;
}

/// get the variable type name
/// now it only "int", "float", "double", or LOCK_VARDECLARE
std::string ST_ValueUtilize::getTypeName(Type* ty) {
	
	std::string nm = "";
	if (ty->isIntegerTy()) {		
		nm = "int";
	} else if (ty->isFloatTy()) {
		nm = "float";	
	} else if (ty->isDoubleTy()) {
		nm = "double";	
	} else if (ty->isStructTy()) {
		nm = ty->getStructName().str();		
		/// replace "union.pthread_mutex_t" with LOCK_VARDECLARE
		if (nm == "union.pthread_mutex_t") 
			nm = LOCK_VARDECLARE;
	}	
	return nm;
}

int ST_ValueUtilize::getTypeNum(Type* ty) {
	int num = 1;
	if (ty->isArrayTy() ) {	
		num = int(ty->getArrayNumElements());
	} else if (ty->isVectorTy()) {
		num = ty->getVectorNumElements();
	}	
	
	return num;
}	

ST_ValueUtilize::VarTypeStru ST_ValueUtilize::analyseType(Type* ty){
	
    VarTypeStru tystru;
	tystru.ty = ty;	
	
	if (ty->isArrayTy() ) {	
		
		//debug
		//std::string tydef2 = printTypeInfo(ty->getArrayElementType());		
		//errs() <<  "The Array type : " << tydef2 << "\n" ;  
		
		tystru.baseTy = ty->getArrayElementType();			
		tystru.num = int(ty->getArrayNumElements());
		analyseSingleType(ty->getArrayElementType(), &tystru);
		
		
	} else if (ty->isVectorTy()) {	
		
		//debug
		//std::string tydef2 = printTypeInfo(ty->getArrayElementType());		
		//errs() <<  "The Vector type : " << tydef2 << "\n" ;  
		
		tystru.baseTy = ty->getVectorElementType();		
		tystru.num = ty->getVectorNumElements();
		analyseSingleType(ty->getVectorElementType(), &tystru);

	} else if (ty->isPointerTy()) {	
		
		//debug
		//std::string tydef2 = printTypeInfo(ty->getArrayElementType());		
		//errs() <<  "The Vector type : " << tydef2 << "\n" ;  
		
		Type* cty =  ty->getPointerElementType();
		tystru = analyseType(cty);
		tystru.isPointer = true;

	}else {
		analyseSingleType(ty, &tystru);		
	}
	
	return tystru;	
}

void ST_ValueUtilize::analyseSingleType(Type* ty, VarTypeStru * tystru){	
	
	if (ty->isIntegerTy()) {
		
		tystru->tyName = "int";
		tystru->bitwide= ty->getPrimitiveSizeInBits();	
		
		//getIntegerBitWidth () 
		
	} else if (ty->isFloatTy()) {

		tystru->tyName = "float";
		tystru->bitwide =  ty->getPrimitiveSizeInBits();	
	
	} else if (ty->isDoubleTy()) {

		tystru->tyName = "double";
		tystru->bitwide =  ty->getPrimitiveSizeInBits();	
	
	} else if (ty->isStructTy()) {
		tystru->tyName = ty->getStructName().str();
		
		/// replace "union.pthread_mutex_t" with LOCK_VARDECLARE
		if (tystru->tyName == "union.pthread_mutex_t") 
			tystru->tyName = LOCK_VARDECLARE;
	
	}else {
		
		std::string tydef = printTypeInfo(ty);
		
		VarTypeStru ts = getFromTypeInfo(tydef);
		tystru->tyName = ts.tyName;		
	}
	
	return;
}

ST_ValueUtilize::VarTypeStru ST_ValueUtilize::getFromTypeInfo(std::string s) {
	
	VarTypeStru tystru;
	
	int res = 0;	
	std::string resty = s;

	std::string deli1 = "[";
	std::string deli2 = "]";
	std::string deli3 = " x";
	std::string deli4 = " = type {";
	
	size_t pos1 = 0;
	size_t pos2 = 0;
	size_t pos3 = 0;
	
	if ((pos1 = s.find(deli1)) != std::string::npos) {		
		s.erase(0,pos1+deli1.length());
		if ((pos2 = s.find(deli2)) != std::string::npos) {
			std::string token = s.substr(0,pos2);
			if ((pos3 = token.find(deli3)) != std::string::npos) {
				std::string toknum = token.substr(0,pos3);			
				res = atoi(toknum.c_str());				
				if (res < 1) res = 1;
				resty = token.erase(0,pos3+deli3.length());
			}
		}
	} else 	if ((pos1 = s.find(deli4)) != std::string::npos) {
		resty = s.substr(0,pos1);
    } 		
	
	tystru.num = res;
	tystru.tyName = resty;
	
	return tystru;		
}

  
#endif
