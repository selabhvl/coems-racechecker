//===-------- st_valueSetMap.cpp - part of the static analysis for counting locks, threads and global variables in c program --------===//
//
// Define a Map->Map->Set data structure and operation
// 
//Author: ld, 2018-02-05
//===----------------------------------------------------------------------===//

#ifndef ST_VALUESETMAP
#define ST_VALUESETMAP


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

//Debug
//#define USEDEBUG

/* #ifdef USEDEBUG
	#define Debug( x ) errs() << x
#else
	#define Debug( x ) 
#endif */

using namespace llvm;


template <typename T1,typename T2, typename T3>
class ST_ValueSetMap {

public:
	
	typedef std::map<T2, std::set<T3>> VVMap;
	typedef std::map<T1, VVMap> VVVMap;

	VVVMap vSetMap;
	
	//ST_ValueSetMap();
	
	void add(T1 ov);	
	void add(T1 ov,T2 ov2);
	void add(T1 ov,T2 ov2, T3 vv);
	void add(T1 ov,T2 ov2,std::set<T3> vs);
	
	std::set<T1> get();
	std::set<T2> get(T1 ov);	
	std::set<T3> get(T1 ov,T2 ov2);
	
	std::set<T1> getAllT1();
	std::set<T2> getAllT2();
	std::set<T3> getAllT3();
	
	bool isFind(T1 ov,T2 ov2, T3 vv);
	
	bool find(T1 ov);

	
};

template <typename T1,typename T2, typename T3>
std::set<T3> ST_ValueSetMap<T1,T2,T3>::getAllT3() {	
	std::set<T3> emp;	
	for (const auto& pair1 : vSetMap) { 
		VVMap cnd = vSetMap[pair1.first];	
		for (const auto& pair2 : cnd) {
			std::set<T3> cnd2 = get(pair1.first,pair2.first);	
			emp.insert(cnd2.begin(), cnd2.end());	
		}
	}
	return emp;	
}

template <typename T1,typename T2, typename T3>
std::set<T2> ST_ValueSetMap<T1,T2,T3>::getAllT2() {	
	std::set<T2> emp;	
	for (const auto& pair1 : vSetMap) { 
		VVMap cnd = vSetMap[pair1.first];	
		for (const auto& pair2 : cnd) {
			emp.insert(pair2.first);
		}
	}
	return emp;	
}


template <typename T1,typename T2, typename T3>
std::set<T1> ST_ValueSetMap<T1,T2,T3>::getAllT1() {	
	return get();
}
	
template <typename T1,typename T2, typename T3>
std::set<T1> ST_ValueSetMap<T1,T2,T3>::get() {	
	std::set<T1> emp;
	for (const auto& pair : vSetMap) {
		emp.insert(pair.first);
	}
	return emp;
}

template <typename T1,typename T2, typename T3>
std::set<T2> ST_ValueSetMap<T1,T2,T3>::get(T1 ov) {	
	std::set<T2> emp;
	if(vSetMap.find(ov) != vSetMap.end()) {
		VVMap cnd = vSetMap[ov];	
		for (const auto& pair : cnd) {
			emp.insert(pair.first);
		}
	}
	return emp;
}

template <typename T1,typename T2, typename T3>
std::set<T3> ST_ValueSetMap<T1,T2,T3>::get(T1 ov,T2 ov2) {	
	std::set<T3> emp;
	if(vSetMap.find(ov) != vSetMap.end()) {
		VVMap cnd = vSetMap[ov];
		emp = cnd[ov2];	
	}
	return emp;
}

template <typename T1,typename T2, typename T3>
bool ST_ValueSetMap<T1,T2,T3>::isFind(T1 ov,T2 ov2,T3 vv) {
	
	bool res = false;
	
	std::set<T3> emp;	
	VVMap cnd ;
	
	if(vSetMap.find(ov) != vSetMap.end()) {
		cnd = vSetMap[ov];
		if(cnd.find(ov2) != cnd.end()) {
			emp = cnd[ov2];
			if (emp.find(vv) != emp.end()) 
				res = true;
		}
	}
	
	return res;	
}

template <typename T1,typename T2, typename T3>
bool ST_ValueSetMap<T1,T2,T3>::find(T1 ov) {	
	return !vSetMap[ov].empty();	
}

template <typename T1,typename T2, typename T3>
void ST_ValueSetMap<T1,T2,T3>::add(T1 ov) {	
	if(vSetMap.find(ov) == vSetMap.end()) {
		VVMap cnd;	
		vSetMap[ov] = cnd;
	}
}

template <typename T1,typename T2, typename T3>
void ST_ValueSetMap<T1,T2,T3>::add(T1 ov,T2 ov2) {
	
	std::set<T3> emp;
	VVMap cnd;
	
	if(vSetMap.find(ov) != vSetMap.end()) { // found
		cnd = vSetMap[ov];
		if(cnd.find(ov2) != cnd.end()) {
			return;
		} else {
			cnd[ov2] = emp;
		}	
	} else {
		cnd[ov2] = emp;
	}
	
	vSetMap[ov] = cnd;
	
}

template <typename T1,typename T2, typename T3>
void ST_ValueSetMap<T1,T2,T3>::add(T1 ov,T2 ov2,T3 vv) {
	
	std::set<T3> emp;
	VVMap cnd;
	
	if(vSetMap.find(ov) != vSetMap.end()) { //found ov
		cnd = vSetMap[ov];
		if(cnd.find(ov2) != cnd.end()) {
			emp = cnd[ov2];
			emp.insert(vv);
		} 
	}else emp.insert(vv);
		
	cnd[ov2] = emp;	
	
	vSetMap[ov] = cnd;
	
}

template <typename T1,typename T2, typename T3>
void ST_ValueSetMap<T1,T2,T3>::add(T1 ov,T2 ov2,std::set<T3> vs) {
	
	std::set<T3> emp;
	VVMap cnd;
	
	if(vSetMap.find(ov) != vSetMap.end()) {
		cnd = vSetMap[ov];
		if(cnd.find(ov2) != cnd.end()) {
			emp = cnd[ov2];
			emp.insert(vs.begin(), vs.end());
		}
	}
	cnd[ov2] = emp;	
	vSetMap[ov] = cnd;
	
}

  
#endif