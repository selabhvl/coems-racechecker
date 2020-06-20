//===-------- worklist2.cpp - part of the static analysis for counting locks, threads and global variables in c program --------===//
//
// Define another worklist to calculate function calls in function CFG
// 
//Author: ld, 2018-02-05,2018-02-21
//===----------------------------------------------------------------------===//

#ifndef FF_WORKLIST
#define FF_WORKLIST

#include <cassert>

#include "llvm/Config/llvm-config.h"
#if LLVM_VERSION_MAJOR < 4
#error "Unsupported LLVM version -- older versions may or may not compile."
#endif

#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopInfo.h" 
#include "llvm/IR/Dominators.h"

#include "st_utilize.cpp"
#include "st_valueSetMap.cpp"
//Debug
//#define USEDEBUG

/* #ifdef USEDEBUG
#define Debug( x ) errs() << x
#else
#define Debug( x ) 
#endif */

//#define INSTRU_DEBUGINFO

using namespace llvm;

class FFWorkList {
	
struct funstru {
	std::string name;
        int num;
	std::set<Instruction*> caller;		
};

typedef std::map<Value *, funstru> funMap;
// threadFuns : function -> it caller instructions
typedef std::map<Function *, std::set<Instruction*>> funcallstruMap;

typedef std::map<Value *, funcallstruMap> funtreeMap;

public:
	
	/* The pointer to the current module */
	Module *theM;
	
	/* The set of the recursive functions in the module */
	std::set<Function*> RecurFuns; 	
	
	/// the set of Store or Load instructions that should be traced 
	/// 1 Store or Load instruction
	/// 2 the pointer variable declaration that are accessed by the Store or Load instruction
	/// 3 the global var or function arg the above variable pointer gets address from,and the pos string
	ST_ValueSetMap<Instruction *, Value *, ST_ValueUtilize::MemVarPos_def> MemoryAccessInstructions;
	
	void printMemoryAccessInfo(std::string fname);
	
	///list of global Vars of {VarTypeStru}, including PTHREAD_VARDECLARE and LOCK_VARDECLARE
	ST_ValueUtilize::VarTypeList_def globalVarList;  
	
	///list of global mutex Locks initialized using PTHREAD_MUTEX_INITIALIZER 	
	ST_ValueUtilize::VarTypeList_def globalMutexsList;  
	
	/// the set variables that are pthreads or mutex locks
	std::set<Value*> globalPthreadOrMutexVars;	
	
	/* The functions called by "pthread_create" 
	 * Function -> caller instructions -> the variables passed to the functions
	 * Function -> Instruction -> Value
	*/
	ST_ValueSetMap<Function *, Instruction *, Value *> ThreadRunFuns;	
	
	void FFWorkListIni(Module &M, bool setMemAccess); 
	
	std::set<Function*> getCalledFunctions(Function* fun);
	void printInfo();
	void SetRecurFuns(std::set<Function*> funs); 
	
	Function* entryFunction;
	
private:
	
	/* General utilization class */
	ST_ValueUtilize Vutilize;	
	
	struct var_start_end_def {
	  Value* var; // the var	
	  unsigned start; // pos start
	  unsigned end;	  // pos end
	};
	
	/// the set variables that are accessed by the set of Store or Load instructions in MemoryAccessInstructions
	std::vector<var_start_end_def> MemoryAccessVars;	
	
	/// threadFuns : function -> it caller instructions
	funMap threadFuns; 
	
	void doSetMemoryAccessInstrs();
	
	///typedef std::map<Value *, SmallVector<funcallstru, 10>> funtreeMap;
	/// function -> its called functions -> the called from instructions 
	funtreeMap threadFuntree;	
	
};


void FFWorkList::FFWorkListIni(Module &M, bool setMemAccess) {	

	theM = &M;	
	
	/// we make a map (threadFuns) of functions that have the pthread_create instructions.
	for (Module::iterator f = M.begin(); f != M.end(); ++f) {
        if (f->empty()) continue;	
		Function* fun = &*f; 	
		for (Function::iterator bb = f->begin(), be = f->end(); bb != be; ++bb) {		
			for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
				Instruction* ins = &(*ii);
				
				if( isa<CallInst>(ins) && Vutilize.isCalledNoEmpty(ins)){
					CallInst* instr = dyn_cast<CallInst>(ins);

					if (instr->getCalledFunction()->getName().str()== Vutilize.INS_PTHREADCREATE) {	
						threadFuns[fun] = {fun->getName(),1,std::set<Instruction*>()};
			
						Function * callfun =  Vutilize.getPthreadCreateCalled(instr);						
						ThreadRunFuns.add(callfun,instr);
						
					}
				}
				
			}
		} 		
	}
	
	/// We add to ThreadRunFuns the functions it called.
	bool addNew = false;
	do {
		addNew = false;	
		for (auto &fun : ThreadRunFuns.get()) {
		  // assert(fun != NULL);
			if (fun == NULL || fun->empty()) continue;
			for (Function::iterator bb = fun->begin(), be = fun->end(); bb != be; ++bb) {		
				for (BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
					Instruction* ins = &(*ii);					
					if( isa<CallInst>(ins)){
						Function* cfun = dyn_cast<CallInst>(ins)->getCalledFunction();
                                                if (cfun == NULL) {
							/* Sometimes we see calls to %-registers. */
							continue;
						}
						if (cfun->empty()) continue;						
						if (ThreadRunFuns.find(cfun)) continue;
						ThreadRunFuns.add(cfun,ins);
						//errs()<< "The new function to add is : " << cfun->getName() << "\n";  
						addNew = true;
					}
					
				}
			}
		}
	} while (addNew); 	 
	
	/// We add all the functions and the instructions call them
	addNew = false;
	do {
		addNew = false;	
		for(funMap::iterator p = threadFuns.begin(); p!=threadFuns.end(); ++p){
			
			Function *F = dyn_cast<Function>(p->first);		
			funstru nd = p->second;
			//Debug( "The function name is : " << nd.name << "\n");  

			/// This function is called from the following instructions
			for (Value::user_iterator i = F->user_begin(), e = F->user_end(); i != e; ++i){				
				if (Instruction *Inst = dyn_cast<Instruction>(*i)) {
					nd.caller.insert(Inst);
					threadFuns[p->first] = {nd.name,nd.num,nd.caller};
					
					Value *newF = dyn_cast<Value>(Inst->getParent()->getParent());
					if(threadFuns.find(newF) == threadFuns.end()) {
						threadFuns[newF] = {newF->getName(),1,std::set<Instruction*>()};
						addNew = true;
						//Debug( "The new function to add is : " << newF->getName() << "\n");  
					}
				}  
			  
			}			
		}
		
	} while (addNew); 

	/// convert threadFuns : a map of functions to the instructions calling the functions.
	///   to threadFuntree : a map of functions to the called functions and the calling instructions
	for(funMap::iterator p = threadFuns.begin(); p!=threadFuns.end(); ++p){		
		Function *Fto = dyn_cast<Function>(p->first);		
		funstru nd = p->second;	
		
		for (std::set<Instruction*>::iterator it = nd.caller.begin(); it != nd.caller.end(); ++it){  
			
			Instruction* instFrom = *it;
			Value* FunFrom = dyn_cast<Value>(instFrom->getParent()->getParent());
			
			funcallstruMap cnd;
			std::set<Instruction*> iis;
			
			if(threadFuntree.find(FunFrom) != threadFuntree.end()) {
				cnd = threadFuntree[FunFrom];
				/// go through the vect to find *Fto
				if(cnd.find(Fto) != cnd.end()){
					iis = cnd[Fto];
				}
			}
			
			iis.insert(instFrom);
			cnd[Fto] = iis;
			
			threadFuntree[FunFrom] = cnd;			
			//Debug( "The caller instructions is : " << *instFrom << "\n");;  
		} 	
	
	}

	/// find if the Recursive functions and put to the RecurFuns 
	RecurFuns.clear();
	for(funMap::iterator p = threadFuns.begin(); p!=threadFuns.end(); ++p){			
		std::set<Function*> temp; 
		temp.clear();
		Function *F = dyn_cast<Function>(p->first);
		
		//errs()<< "The function to test is : " << F->getName() << "\n";
		//errs()<< "The calls of the fun is : " << Vutilize.printCalledby(F) << "\n";
				
		if(Vutilize.isFunRecursive(F,temp)) RecurFuns.insert(F);	
	}
	
	///Deal with global variable declarations, output all no constant global variables to globalVarList 
	// TODO: We MUST NOT distinguish between global and local variables in general in the analysis!
	globalVarList = Vutilize.doListGlobalVar(M);	
	
	///get a list of the global declare locks that have been static initialized using PTHREAD_MUTEX_INITIALIZER 
	globalMutexsList = Vutilize.getMutexList(&globalVarList);	
	
	/// get the set of global pthread_t and mutex locks
	globalPthreadOrMutexVars.clear();	
	ST_ValueUtilize::VarTypeStru tyst;
	for(auto p = globalVarList.begin(); p!=globalVarList.end(); ++p){	
		tyst = *p;
		if (tyst.tyName == Vutilize.LOCK_VARDECLARE || tyst.tyName == Vutilize.PTHREAD_VARDECLARE) {
			globalPthreadOrMutexVars.insert(tyst.var);
		}
	}	
	
	/// get the set of Store or Load instructions that should be traced  
	/// and the variables into MemoryAccessInstructions
	if (setMemAccess) doSetMemoryAccessInstrs();	
}	

/// fname == "", print out all 
void FFWorkList::printMemoryAccessInfo(std::string fname){
	
 	for (Module::iterator f = theM->begin(); f != theM->end(); ++f) {
        if (f->empty()) continue;	
		
		if (fname!="" && f->getName().str() != fname) continue;
		
		errs()<< "\n The Function is :" << f->getName().str() << "\n\n";		
		for (Instruction * vvv : MemoryAccessInstructions.getAllT1()) {
			//if (vvv->getParent()->getParent() == &*f) {	
				errs()<< "The  instructions are :" << Vutilize.printTextInfo(vvv) << "\n";
				for (Value * vt2 : MemoryAccessInstructions.get(vvv)) {
					for (ST_ValueUtilize::MemVarPos_def vt3 : MemoryAccessInstructions.get(vvv,vt2)) {	
							errs()<< "The  mem address are :" << Vutilize.printTextInfo(vt3.first) << "\n";
							errs()<< "The  mem address pos are :" << vt3.second << "\n\n";
					}					
				}				

			//}
		}	
	}	
	
	return;	
}

/// get the set of Store or Load instructions that should be traced  
/// and the variables into MemoryAccessInstructions
void FFWorkList::doSetMemoryAccessInstrs(){
	
	MemoryAccessVars.clear();
	
	const DataLayout &DL = theM->getDataLayout();
	
	for (auto &f : ThreadRunFuns.get()) {
                // assert (f != NULL);
                if (f == NULL || f->empty()) continue;	
		if (!Vutilize.hasCaller(&*f)) continue; 	
		
		//debug control 
		//if (f->getName().str()!="f") continue;		
		//errs()<< "\n The Function is :" << f->getName().str() << "\n\n";	
		
		DominatorTree DT = DominatorTree(*f);
			
		for (auto &BB : *f) {
			for (auto &Inst : BB) {	
				if (Vutilize.isTraceLoadStoreInstr(&Inst, DL )) {
					
					Value *mem = isa<StoreInst>(Inst)
						? dyn_cast<StoreInst>(&Inst)->getPointerOperand()
						: dyn_cast<LoadInst>(&Inst)->getPointerOperand();
					
					//Value*  basemem = mem->stripInBoundsOffsets();	
					//basemem = GetUnderlyingObject(basemem, DL);	
						
					if(globalPthreadOrMutexVars.find(mem) == globalPthreadOrMutexVars.end()) { //not find
					
						std::string pos = "";						
						Value* memdef = Vutilize.chasePointerDef(mem, &pos);
						
						if (memdef == NULL || memdef == nullptr) continue;
						
						//MemoryAccessVars.insert(memdef);	
						
						#ifdef INSTRU_DEBUGINFO
						errs()<< "\n The instruction goto check is :" << Vutilize.printTextInfo(&Inst) << "\n";	
						errs()<< "The mem def is :" << Vutilize.printTextInfo(memdef) << "\n";
						errs()<< "pos is :" << pos << "\n\n";
						#endif	
						
						//the pos has been set in chasePointerDef, and then we set it again in getPointerAddressFrom 
						//is it double setting
						//pos = "";						
						
						std::set<ST_ValueUtilize::MemVarPos_def> vfggset = Vutilize.getPointerAddressFrom(memdef,pos,&Inst,DT);
							
						for (ST_ValueUtilize::MemVarPos_def vfgg_p: vfggset){							
							
							Value* vfgg = vfgg_p.first;
							pos = vfgg_p.second;
							
							if (vfgg == NULL || vfgg==nullptr) continue;
							
							//debug info
							#ifdef INSTRU_DEBUGINFO
							errs()<< "\n VVV The instruction is :" << Vutilize.printTextInfo(&Inst) << "\n";
							errs()<< "   VVV The global alias var is  :" << Vutilize.printTextInfo(vfgg) << "\n";
							errs()<< "   VVV The global alias var pos is :" << pos << "\n";		
							errs()<< "   VVV The global alias var type  is  :" << Vutilize.printTypeInfo(vfgg) << "\n";
							errs()<< "   VVV The global alias var offset is :" << Vutilize.getOffsetToPos(vfgg->getType(),pos,DL) << "\n";		
							errs()<< "   VVV The mem access is Captured  :" << PointerMayBeCaptured(memdef, true, true) << "\n\n";			
							#endif
						
							MemoryAccessInstructions.add(&Inst,memdef,vfgg_p);
							
							var_start_end_def vse;
							vse.var = vfgg;
							vse.start = Vutilize.getOffsetToPos(vfgg->getType(),pos,DL);
							bool ispp = Vutilize.isPointerToPointer(vfgg,pos);
							vse.end = ispp ? (unsigned)DL.getPointerTypeSize(vfgg->getType()) : (unsigned)DL.getTypeAllocSize(vfgg->getType());
							vse.end = vse.start + vse.end;
							MemoryAccessVars.push_back(vse);
						}
					}
				}	
			}
	    }
	}

	/// add the load and save instructions from functions other than that called by pthread_create	
	std::set<Function*> ptst = ThreadRunFuns.get();		
	for (Module::iterator f = theM->begin(); f != theM->end(); ++f) {
        if (f->empty()) continue;
		if (!Vutilize.hasCaller(&*f)) continue;
		
		Function * fun = &*f;	
		
		DominatorTree DT = DominatorTree(*f);
		
		//debug control info
		//if (fun->getName().str()!="f") continue;		
		//errs()<< "The functions is :" << fun->getName()<< " : "<< Vutilize.getValueID(fun)<<"\n";
		
		if(ptst.find(fun) == ptst.end()) { //not found
			for (auto &BB : *fun) {
				for (auto &Inst : BB) {	
					if (Vutilize.isTraceLoadStoreInstr(&Inst, DL )) {
						Value *mem = isa<StoreInst>(Inst)
							? dyn_cast<StoreInst>(&Inst)->getPointerOperand()
							: dyn_cast<LoadInst>(&Inst)->getPointerOperand();

						//mem = mem->stripInBoundsOffsets();					
						//mem = GetUnderlyingObject(mem, DL);

						if(globalPthreadOrMutexVars.find(mem) == globalPthreadOrMutexVars.end()) { //not find
						
							std::string pos = "";						
							Value* memdef = Vutilize.chasePointerDef(mem, &pos);
							
							if (memdef == NULL || memdef == nullptr) continue;
				
							std::set<ST_ValueUtilize::MemVarPos_def> vfggset = Vutilize.getPointerAddressFrom(memdef,pos,&Inst,DT);
								
							for (ST_ValueUtilize::MemVarPos_def vfgg_p: vfggset){							
								
								Value* vfgg = vfgg_p.first;
								pos = vfgg_p.second;
								
								if (vfgg == NULL || vfgg==nullptr) continue;
								
								unsigned vfst = Vutilize.getOffsetToPos(vfgg->getType(),pos,DL);
								bool ispp = Vutilize.isPointerToPointer(vfgg,pos);
								unsigned vfed = ispp ? (unsigned)DL.getPointerTypeSize(vfgg->getType()) : (unsigned)DL.getTypeAllocSize(vfgg->getType());
								vfed = vfst + vfed;
								
								for (var_start_end_def vse : MemoryAccessVars ) {
									if (vse.var == vfgg){
										if((vfst>=vse.start && vfst<=vse.end) ||
											(vfed>=vse.start && vfed<=vse.end)){
											MemoryAccessInstructions.add(&Inst,memdef,vfgg_p);
											
											#ifdef INSTRU_DEBUGINFO
											errs()<< "\n insert ass instruction is :" << Vutilize.printTextInfo(&Inst) << "\n";
											errs()<< "insert ass meme address is :" << Vutilize.printTextInfo(vfgg) << "\n";
											errs()<< "insert ass meme address pos is :" << vfgg_p.second << "\n\n";							
											#endif												
										}
									}									
								}
							}
						}
					}	
				}
			}	
		}
	}
} //end doSetMemoryAccessInstrs

///get a set of functions called by the "fun"
std::set<Function*> FFWorkList::getCalledFunctions(Function* fun) {	
	Value* FunFrom = dyn_cast<Value>(fun);
	
	std::set<Function*> funset;
	
	if(threadFuntree.find(FunFrom) != threadFuntree.end()) {
		funcallstruMap ndto = threadFuntree[FunFrom];
		for(funcallstruMap::iterator pp = ndto.begin(); pp!=ndto.end(); ++pp){
			funset.insert(pp->first);
		}
	}
	return funset;
}

void FFWorkList::printInfo() {	

	errs() << "\n Program name : " << theM->getName() << "\n"  << "\n"; 

	for(funtreeMap::iterator p = threadFuntree.begin(); p!=threadFuntree.end(); ++p){	
		Value *fun = p->first;		
		errs() <<  "  Function : " << fun->getName() <<  "\n"; 		
		funcallstruMap cnd = p->second;
		for(funcallstruMap::iterator pp = cnd.begin(); pp!=cnd.end(); ++pp){
			errs() <<  "     called  : " << pp->first->getName() <<  "\n";
		}		

	}	 
}	

#endif
