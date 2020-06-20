#include <iostream>
#include <set>
#include <string>
#include <cstdlib>
#include <algorithm>

#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/ValueTracking.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"

#include "llvm/Support/raw_ostream.h"
#include <llvm/Support/PrettyStackTrace.h>
#include "llvm/Support/Signals.h"

#include "worklist2.cpp"
#include "st_utilize.cpp"
#include "st_valueSetMap.cpp"

// Stuff for command line options
#include <boost/program_options.hpp>

extern "C" {
  #include "instrumentation.h"
}

//#define INSTRU_DEBUGINFO

#define TRACE_LOCALVAR

namespace po = boost::program_options;
namespace {
  const size_t ERROR_IN_COMMAND_LINE = 1;
  const size_t SUCCESS = 0;
  const size_t ERROR_UNHANDLED_EXCEPTION = 2;
}

using namespace llvm;

struct Specification {
    std::vector<int> lines;
    std::vector<int> except_lines;
    std::vector<std::string> opcode;
    std::vector<std::string> except_opcode;
    std::vector<std::string> functions;
    std::vector<std::string> except_functions;
};

// name of instrumentation functions
const char *LOG_INSTR = "_instrumentation_log_instr";
const char *FLUSH = "_instrumentation_flush";
const char *INIT = "_instrumentation_init";

ST_ValueUtilize Vutilize;

// TODO: What does this data structure do?
FFWorkList fworklist;
bool loc_check_firsttime = true; // Warn once if debugging symbols missing.
bool trace_pthread_only = false; // Do not track calls into libraries apart from pthreads.
bool varname_resolution = true;  // TODO: This switch is a workaround for the non-termination in #25.

void initializeLogger(Module&, Function&, bool);
void normalizeTimestamps(Module&, Function&);
Constant* stringLit(Module&, const StringRef&);
// TODO: C++, no real need to thread Specification through everywhere.
void instrumentFunction(Module&, Function&, bool, bool, Specification);
void instrumentInstructions(Module&, Function&, bool, bool,Specification);
void declareLogFunctions(Module &);
Specification interpretCommandLine(po::variables_map);

///get the variable name
std::string getVarName(Value* vval,Function* F);

void instrumentModule(Module &m, bool doFlush, bool doNormalize, bool doInstruOp, Specification s) {	
  if (doInstruOp) {
    /* TODO: What is this data structure doing? [vs]
     */
    /* TODO: There's some non-termination here for some examples,
       switch to 'false' to disable name resolution and avoid this! */
    fworklist.FFWorkListIni(m, varname_resolution);
		
#ifdef INSTRU_DEBUGINFO
    fworklist.printMemoryAccessInfo("f");
#endif
  }
  declareLogFunctions(m);
  for (Module::iterator f = m.begin(); f != m.end(); ++f) {
    if (f->empty()) continue;

    instrumentFunction(m, *f, doFlush, doInstruOp,s);
    std::string fname = f->getName();
    if (fname == "main") {
      initializeLogger(m, *f, doNormalize);
    }
  }
}

void initializeLogger(Module &m, Function &f, bool doNormalize) {
    Type *intTy32 = Type::getInt32Ty(m.getContext());
    BasicBlock &entryBlock = f.getEntryBlock();
    Function *initFunction = m.getFunction(INIT);
    int param = doNormalize ? 1:0;
    std::vector<Value*> args = {ConstantInt::get(intTy32, param, true)};
    CallInst::Create(initFunction, args, "", entryBlock.getFirstNonPHI());
}

Constant* stringLit(Module& m, const StringRef& str) {
    LLVMContext& c = m.getContext();
    Constant* arr = ConstantDataArray::getString(c, str);
    GlobalVariable* arrVar =
        new GlobalVariable(m, arr->getType(), true, GlobalValue::PrivateLinkage, arr);
    IntegerType* int64Type = IntegerType::getInt64Ty(c);
    ConstantInt* zero = ConstantInt::getSigned(int64Type, 0);
    std::vector<Constant*> idx {zero, zero};
    Constant* ptr = ConstantExpr::getGetElementPtr(arr->getType(), arrVar, idx);
    return ptr;
}

void instrumentFunction(Module &m, Function &f, bool doFlush, bool doInstruOp, Specification s) {
    std::string sfunctionName = f.getName();
    if (!s.functions.empty()) {
      // Check if we're interested in that function or if we should ignore it.
        if ( std::find(s.functions.begin(), s.functions.end(), sfunctionName) == s.functions.end()) {
            return;
        }
    } else if (!s.except_functions.empty()) {
        if ( std::find(s.except_functions.begin(), s.except_functions.end(), sfunctionName) != s.except_functions.end()) {
            return;
        }
    }

    instrumentInstructions(m, f, doFlush, doInstruOp, s);
}

bool is_this_access_interesting(Instruction *i) {
  /* WAS: fworklist.MemoryAccessInstructions.find(vvv)
   * but actually needs a proper interface to the outside. Maybe the user
   * can specify a set of variables that he is interested in. Then, we can refine this here,
   * and probably also do things like "if you find THAT variable interesting, then maybe
   * also THIS potential alias here is relevant to you".
   */
   
  #ifdef TRACE_LOCALVAR
  return true;
  #endif
  return fworklist.MemoryAccessInstructions.find(i); 
}

void instrumentInstructions(Module& m, Function &f, bool doFlush, bool doInstruOp, Specification s) {
  Value *functionName = stringLit(m, f.getName());
  Function *logInstr = m.getFunction(LOG_INSTR);
  Function *flush = m.getFunction(FLUSH);

  const DataLayout &DL = m.getDataLayout();

  Instruction* vvv;

  for (auto& bb : f.getBasicBlockList()) {
    for (auto& instr : bb.getInstList()) {	

      /* Sometimes we may want to insert the instruction AFTER, not BEFORE the current instruction */
      bool insertBefore = true;

      /* We do a quick assessment if we can skip the current instruction or if we need to instrument it: */
      if (doInstruOp){
	vvv = &instr;
	if (!(isa<StoreInst>(vvv) || isa<LoadInst>(vvv) || isa<CallInst>(vvv) || 
	      isa<InvokeInst>(vvv)|| isa<ReturnInst>(vvv)|| isa<ResumeInst>(vvv)|| isa<CatchReturnInst>(vvv)|| isa<CleanupReturnInst>(vvv))) {
	  continue;
	}
        /* This check would exclude all calls to libc, like malloc etc.
           Probably there should be some black/whitelisting, because otherwise
           it gets to be A LOT... */
	if (isa<CallInst>(vvv) && Vutilize.isCalledNoEmpty(vvv)){
	  CallInst* cii = dyn_cast<CallInst>(vvv);
	  Function* cfun = cii->getCalledFunction(); 
	  std::string fnm = cfun->getName().str();
	  /* MacOS has a funky \01_-prefix */
	  if(!(fnm.find("pthread_") == 0 || (fnm.find(NAME_PTHREAD_JOIN) == 0))) {
	    if (trace_pthread_only && cfun->empty()) continue;
	  }
          /* Skip some build-ins */
	  if(fnm.find("llvm.") == 0) continue;
	}
	if (isa<StoreInst>(vvv) || isa<LoadInst>(vvv)){
	  if (!is_this_access_interesting(vvv)) {
	    continue;
	  }
	}				
      }
			
      std::string sinstrName = instr.getOpcodeName();
      if (!s.opcode.empty()) {
	if ( std::find(s.opcode.begin(), s.opcode.end(), sinstrName) == s.opcode.end()) {
	  continue;
	}
      } else if (!s.except_opcode.empty()) {
	if ( std::find(s.except_opcode.begin(), s.except_opcode.end(), sinstrName) != s.except_opcode.end()) {
	  continue;
	}
      }

      Value *instrName = stringLit(m, sinstrName);
      Type* intType = IntegerType::get(m.getContext(), 32);

      auto loc = instr.getDebugLoc();
			
      // only warn once (noted on LLVM-5) by checking the first load instruction.
      if (isa<LoadInst>(instr) && loc.get() == NULL && loc_check_firsttime) {
	loc_check_firsttime = false;
	errs() << "No debugging data, did you forget to compile with -g?\n";
      }
      int iline = -1, icol = -1;
      Value *line, *col;
      if (loc.get() != NULL && instr.hasMetadata()) {
	iline = loc.getLine();
	icol = loc.getCol();
      }
      line = ConstantInt::getSigned(intType, iline);
      col = ConstantInt::getSigned(intType, icol);

      if (!s.lines.empty()) {
	if ( std::find(s.lines.begin(), s.lines.end(), iline) == s.lines.end()) {
	  continue;
	}
      } else if (!s.except_lines.empty()) {
	if ( std::find(s.except_lines.begin(), s.except_lines.end(), iline) != s.except_lines.end()) {
	  continue;
	}
      }

      /* TODO: An OO-way of logging the data would be nice.
	 In 'args', we prepare the data for the instrumentation, which will be passed via varargs
	 in the generated code. Unfortunately, it's not able to call C functions with varargs from
	 C++. So for our own logging, we print all the data so that we can capture it. Can later be
	 changed to a database or something -- the current approach of quasi-CSV has the disadvantage
	 that we have a varying number of entries per line. [vs]
      */
      std::vector<Value *> args {instrName, functionName, line, col};
      std::cout << iline <<","<< icol << "," << sinstrName << "," << f.getName().str();
	    
      LLVMContext &c = m.getContext();
      Type *intTy32 = Type::getInt32Ty(c);

      /* This list needs to be in sync with the enum in instrumentation.c */
      Constant *stringType = ConstantInt::get(intTy32, STRING, true);
      Constant *pointerType = ConstantInt::get(intTy32, POINTER, true);
      Constant *int32Type = ConstantInt::get(intTy32, INT32, true);
      Constant *endType = ConstantInt::get(intTy32, END, true);
      Constant *functionCallType = ConstantInt::get(intTy32, FUNCTIONCALL, true);
      Constant *ptCallType = ConstantInt::get(intTy32, PT, true);
      Constant *globalVarType = ConstantInt::get(intTy32, GLOBALVAR, true);
      Constant *pointOfpointerType = ConstantInt::get(intTy32, POINTEROF, true);
      Constant *localVarType = ConstantInt::get(intTy32, LOCALVAR, true);

      if( isa<CallInst>(&instr)) {
				
	CallInst* valinstr = dyn_cast<CallInst>(&instr);
	Function *calledFunction = valinstr->getCalledFunction();
	/* Indirect? */
	if (calledFunction != NULL) {
	  std::string ty2= calledFunction->getName().str();
	  std::cout << "," << ty2;
			   
	  if (ty2 == "pthread_mutex_lock" || ty2 == "pthread_mutex_unlock" ){
	    args.push_back(ptCallType);
	    args.push_back(stringLit(m, ty2));
            /* Insert BEFORE unlocking. Otherwise we unlock, the other thread races us, grabs the lock,
               and our UNLOCK is not yet in the trace! */
	    insertBefore = ty2 == "pthread_mutex_unlock";
            args.push_back(ConstantInt::get(intTy32, ty2 == "pthread_mutex_lock" ? PT_LOCK : PT_UNLOCK, true));
					
	    Value *nmval = instr.getOperand(0);
					
	    // name
	    std::string nm = Vutilize.get_ValueID(nmval);
	    /* Why do we sometimes overwrite nm again? */
	    if (nmval->hasName()) {
	      nm = nmval->getName().str();
	    }

	    args.push_back(stringLit(m, nm));
	    std::cout << "," << nm;
	    args.push_back(nmval);
	    std::cout << "," << nmval;
	  } else if (ty2 == "pthread_mutex_init") {
	    args.push_back(ptCallType);
	    args.push_back(stringLit(m, ty2));
            args.push_back(ConstantInt::get(intTy32, PT_INIT, true));
	    /* TODO: Check if this is useful -- we'll still be missing static initializations, don't we? */
	    Value *mval = instr.getOperand(0);
					
	    // name
	    std::string nm = Vutilize.get_ValueID(mval);
	    if (mval->hasName()) {
	      /* nm overwritten? */
	      nm = mval->getName().str();
	    }	

	    args.push_back(stringLit(m, nm));
	    args.push_back(mval);
	  } else if (ty2 == NAME_PTHREAD_JOIN) {
	    args.push_back(ptCallType);
	    args.push_back(stringLit(m, ty2));
            args.push_back(ConstantInt::get(intTy32, PT_JOIN, true));
	    /* Log thread we're waiting for */
	    Value *mval = instr.getOperand(0);
	    args.push_back(mval);
	  } else if (ty2 == "pthread_create" ){
	    args.push_back(ptCallType);
	    args.push_back(stringLit(m, ty2));
            args.push_back(ConstantInt::get(intTy32, PT_CREATE, true));
					
	    /// the name of function invoked
	    /* [vs] I doubt that in general we have the NAME of the function statically available.
	       At runtime, we have of course the function-pointer/code address.
	    */
	    Function * cfun = Vutilize.getPthreadCreateCalled(valinstr);
	    /* TODO: operand can be anything */
	    if (cfun != NULL) {
	      std::string vfn = Vutilize.get_ValueID(cfun);
	      if (cfun->hasName()) {
		vfn = cfun->getName().str();
	      }
	      args.push_back(stringLit(m, vfn));
	      std::cout << "," << vfn;
	    } else {
	      /* XXX: the instrumenter is probably unhapy if you didn't push any args! */
	      args.push_back(stringLit(m, "(unknown)"));
	    }
	  } else {
	    args.push_back(functionCallType);
	    args.push_back(stringLit(m, ty2));
          }
	} else {
	  /* Indirect function call */
          // errs() << "flunking on indirect call in line " << iline << "\n";
	}
      } else if (isa<LoadInst>(&instr)) {
	// load from
	Value *vval = instr.getOperand(0);
	Type *vtype = vval->getType();

	std::string vfn = getVarName(vval,&f);

	/// the var name as localVarType
	args.push_back(localVarType);
	args.push_back(ConstantInt::get(intTy32, VA_R, true));
	args.push_back(stringLit(m, vfn));
	std::cout << "," << vfn;

	unsigned sz;
	/// the type and the val itself
	if (vtype->getPointerElementType()->isPointerTy()) {
	  /* TODO: Review [X] below */
	  //args.push_back(pointOfpointerType);
	  sz = DL.getPointerTypeSize(vtype);
	} else {
	  sz = DL.getTypeAllocSize(instr.getType());
	}
	args.push_back(stringLit(m, std::to_string(sz)));
	std::cout << "," << sz;

	args.push_back(pointerType);
	args.push_back(vval);
	std::cout << "," << POINTER << "," << vval;
      } else if (isa<StoreInst>(&instr)) {
	//store from
	Value *vval0 = instr.getOperand(0);
	Type *vtype0 = vval0->getType();

	//store to
	Value *vval = instr.getOperand(1);
	Type *vtype = vval->getType();

	std::string vfn = getVarName(vval,&f);

	/// the var name as localVarType
	args.push_back(localVarType);
        args.push_back(ConstantInt::get(intTy32, VA_W, true));
	args.push_back(stringLit(m, vfn));
	std::cout << "," << vfn;

	unsigned sz;
	if (vtype0->isPointerTy()) { /* Why is this "from", not "to"? Probably doesn't matter? */
	  sz = DL.getPointerTypeSize(vtype0);
	} else {
	  sz = DL.getTypeAllocSize(vtype0);
	}
	args.push_back(stringLit(m, std::to_string(sz)));
	std::cout << "," << sz;


	/// the type and the val itself
	if (vtype->getPointerElementType()->isPointerTy()) {

	  /* TODO: Review [X] above -- it was 'pointerType' for Loads? */
	  if (vtype0->isPointerTy()) {
	    args.push_back(pointerType);
	    std::cout << "," << POINTER << "," << vval;
	  } else {
	    args.push_back(pointOfpointerType);
	    std::cout << "," << POINTEROF << "," << vval;
	  }
	} else {
	  args.push_back(pointerType);
	  std::cout << "," << POINTER << "," << vval;
	}
	args.push_back(vval);
      }

      if (doInstruOp){
	// TODO: Not sure if there's a better way...we're testing this like the 3rd time now...
	if (isa<StoreInst>(&instr) || isa<LoadInst>(&instr)) { 

	  SmallVector<ST_ValueUtilize::MemVarPos_def,5> gvctor;
					
	  for (Value * vt2 : fworklist.MemoryAccessInstructions.get(vvv)) {
	    for (ST_ValueUtilize::MemVarPos_def vt3 : fworklist.MemoryAccessInstructions.get(vvv,vt2)) {
	      gvctor.push_back(vt3);
	    }
	  }
					
	  /// push in the args the number of possible global variables
	  args.push_back(globalVarType);
	  args.push_back(ConstantInt::get(intTy32, gvctor.size()));
	  std::cout << ",gvctor.size: " << gvctor.size();
					
	  while (!gvctor.empty()){	
	    ST_ValueUtilize::MemVarPos_def vt3 = gvctor.pop_back_val();
	    Value *vval = vt3.first;
	    std::string vfn = Vutilize.get_ValueID(vval);
	    if (vval->hasName()) {
	      vfn = vval->getName().str();
	    } 
	    // glabal var name
	    args.push_back(stringType);
	    args.push_back(stringLit(m, vfn));
	    /* We're inside a loop, so keep tags in the output fo now */
	    std::cout << ", vfn:" << STRING << ", " << vfn;

	    //global var offset 
	    args.push_back(int32Type);
	    unsigned pint;
	    if (vt3.second=="" || vt3.second=="0"){
	      pint = 0;
	    } else {
	      pint = Vutilize.getOffsetToPos(vval->getType(),vt3.second,DL);
	    }
	    args.push_back(ConstantInt::get(intTy32, pint));
	    std::cout << ",pint :" << INT32 << "," << pint;
	    //args.push_back(stringLit(m, vt3.second=="" ? "0" : vt3.second ));
							
	    // global var type and val (addr)						
	    // we do not care global var could be a pointer or not 						
	    args.push_back(pointerType); 
	    args.push_back(vval);
	    std::cout << ",vval:" << POINTER << "," << vval;
							
	    /* 							Type *vtype = vval->getType();
								if (vtype->getPointerElementType()->isPointerTy()) {                            
								args.push_back(pointOfpointerType); 
								args.push_back(vval);                            
								} else {
								args.push_back(pointerType); 
								args.push_back(vval); 
								}  */
							
	    //errs()<< "The  mem address are :" << Vutilize.printTextInfo(vt3.first) << "\n";
	    //errs()<< "The  mem address pos are :" << vt3.second << "\n\n";
	  }
	}
      } else {
	if (isa<StoreInst>(&instr) || isa<LoadInst>(&instr)) {
	  /// with no related global var, push 0
	  args.push_back(globalVarType);
	  args.push_back(ConstantInt::get(intTy32, 0));
	  std::cout << "," << 0;
	}
      } 
			
      args.push_back(endType);
      /* Done logging info, newline: */
      std::cout << std::endl;

      /* Check adapted from TSAN */
      Value *Addr = isa<StoreInst>(instr)
        ? cast<StoreInst>(&instr)->getPointerOperand()
	: isa<LoadInst>(instr) ? cast<LoadInst>(&instr)->getPointerOperand() : NULL;
      if (Addr != NULL && isa<AllocaInst>(GetUnderlyingObject(Addr, DL)) &&
	   !PointerMayBeCaptured(Addr, true, true)) {
	// The variable is addressable but not captured, so it cannot be
	// referenced from a different thread and participate in a data race
	// (see llvm/Analysis/CaptureTracking.h for details).
	/* Not captured! */
      } else {
	/* For the race validation, we are only interested in a limited set of instructions.
	 * Probably that can be done a bit erlier.
	 */
	if (isa<LoadInst>(&instr) || isa<StoreInst>(&instr) || isa<CallInst>(&instr)) {
	  CallInst * ci = CallInst::Create(logInstr, args, "", (Instruction *)NULL);
	  if (insertBefore) {
	    ci->insertBefore(&instr);
	  } else {
	    ci->insertAfter(&instr);
	  }

	  /* TODO: this is actually a bit silly, the flush could be done in the instrumentation, and not here as an additional instruction */
	  if (doFlush) {
	    std::vector<Value *> empty {};
	    CallInst::Create(flush, empty, "", (Instruction *)NULL)->insertAfter(ci);
	  }
	}
      }
    }
  }
}

std::string getOrig(Value* vval,Function* F) {
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(vval))
    return Vutilize.getOriginalName(GEP->getPointerOperand(),F).str();
  return Vutilize.getOriginalName(vval,F).str();
}

///get the variable name
std::string getVarName(Value* vval,Function* F) {
  if (vval->hasName()) return vval->getName().str();
  return getOrig(vval, F);
}

void declareLogFunctions(Module &m) {
    LLVMContext &c = m.getContext();
    Type *voidTy = Type::getVoidTy(c);
    Type *intTy32 = Type::getInt32Ty(c);
    Type *stringType = Type::getInt8PtrTy(c);

    FunctionType *logInstrType = FunctionType::get(voidTy, {stringType, stringType, intTy32, intTy32}, true);
    FunctionType *initFunctionType = FunctionType::get(voidTy, {intTy32}, false);
    FunctionType *flushType = FunctionType::get(intTy32, false);
    // insert functions to module
    m.getOrInsertFunction(LOG_INSTR, logInstrType);
    m.getOrInsertFunction(INIT, initFunctionType);
    m.getOrInsertFunction(FLUSH, flushType);
}

Specification interpretCommandLine(po::variables_map vm) {
    // Empty vectors in s will be ignored,
    // since there is no reason to ever pass such as command line arguments.
    // That is actually impossible at the moment anyways.
    Specification s;

    if (vm.count("lines")) {
        s.lines = vm["lines"].as< std::vector<int> >();
    }

    if (vm.count("except-lines")) {
        s.except_lines = vm["except-lines"].as< std::vector<int> >();
    }

    if (vm.count("opcode")) {
        s.opcode = vm["opcode"].as< std::vector<std::string> >();
    }

    if (vm.count("except-opcode")) {
        s.except_opcode = vm["except-opcode"].as< std::vector<std::string> >();
    }

    if (vm.count("functions")) {
        s.functions = vm["functions"].as< std::vector<std::string> >();
    }

    if (vm.count("except-functions")) {
        s.except_functions = vm["except-functions"].as< std::vector<std::string> >();
    }

    return s;
}
int main(int argc, char** argv) {
    bool doFlush = false;
    bool doNormalize = false;
    bool doInstruOp = false;
    const char *filename;
    Specification s;

    // Print full stack trace when crashed
    sys::PrintStackTraceOnErrorSignal(argv[0]);
    PrettyStackTraceProgram X(argc, argv);

    try {
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help,h", "produce help message")
            ("version,v", "print version info")
            ("flush,ff", "flush to memory")
	    ("partly-instruction,p", "instruction optimization")
            ("no-lib-calls", "do not log calls to library functions")
            ("no-varname-resolution", "do not resolve variable names in trace statically")
            ("normalize-timestamps,nt","Normalizes timestamps, always starting with 1")
            ("lines,l", po::value<std::vector<int> >()->multitoken(), "only log given lines")
            ("except-lines,el", po::value<std::vector<int> >()->multitoken(), "log everything except given lines")
            ("opcode,o", po::value<std::vector<std::string> >()->multitoken(), "only log given opcode")
            ("except-opcode,eo", po::value<std::vector<std::string> >()->multitoken(), "log everything except given opcode")
            ("functions,f", po::value<std::vector<std::string> >()->multitoken(), "only instrument given functions")
            ("except-functions,ef", po::value<std::vector<std::string> >()->multitoken(), "instrument everything except given function")
            ("input-file,i", po::value< std::vector<std::string> >(), "input file")
        ;

        // allows filename to be passed on without using --input-file
        po::positional_options_description p;
        p.add("input-file", -1);

        po::variables_map vm;
        try {
            po::store(po::command_line_parser(argc, argv).
                    options(desc).positional(p).run(), vm);
            po::notify(vm);

            if (vm.count("help")) {
                std::cout << "Usage: instrument <filename> [options]" << "\n";
                std::cout << desc << "\n";
                return 0;
            }

            if (vm.count("version")) {
                std::cout << "Version: " << VERSION << "\n";
                return 0;
            }

            if (vm.count("flush")) {
                doFlush = true;
            }
			
 	    if (vm.count("partly-instruction")) {
                doInstruOp = true;
            }

            if (vm.count("no-lib-calls")) {
                trace_pthread_only = true;
            }

            if (vm.count("no-varname-resolution")) {
                varname_resolution = false;
            }

            if (vm.count("normalize-timestamps")) {
                doNormalize = true;
            }
            if (vm.count("input-file")) {
                filename = vm["input-file"].as< std::vector<std::string> >()[0].c_str();
            } else {
                std::cerr << "Usage: instrument <filename> [options]" << "\n";
                std::cerr << desc << "\n";
                return ERROR_IN_COMMAND_LINE;
            }

            s = interpretCommandLine(vm);
        } catch (po::error& e) {
            std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
            std::cerr << desc << std::endl;
            return ERROR_IN_COMMAND_LINE;
        }

        LLVMContext c;
        SMDiagnostic err;
        std::unique_ptr<Module> m = parseIRFile(filename, err, c);

        if (!m) {
            err.print(argv[0], errs()); // TODO: messy
            return 1;
        }

        instrumentModule(*m, doFlush, doNormalize, doInstruOp, s);
		
        std::error_code ec;
        raw_fd_ostream out(filename, ec, llvm::sys::fs::F_None);
#if LLVM_VERSION_MAJOR < 7
        WriteBitcodeToFile(m.get(), out);
#else
        WriteBitcodeToFile(*m.get(), out);
#endif
        return SUCCESS;
    } catch (std::exception& e) {
        std::cerr   << "Unhandled Exception reached the top of main: "
                    << e.what() << ", application will now exit" << std::endl;
        return ERROR_UNHANDLED_EXCEPTION;
    }
}
