// -*- mode: c++; tab-width: 4; indent-tabs-mode: t; eval: (progn (c-set-style "stroustrup") (c-set-offset 'innamespace 0) (c-set-offset 'inextern-lang 0)); -*-
// vi:set ts=4 sts=4 sw=4 noet :
// Copyright 2014 The pyRASMUS development team
// 
// This file is part of pyRASMUS.
// 
// pyRASMUS is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
// 
// pyRASMUS is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with pyRASMUS.  If not, see <http://www.gnu.org/licenses/>
#include "lexer.hh"
#include <frontend/AST.hh>
#include "code.hh"
#include "error.hh"
#include <frontend/visitor.hh>
#include "llvmCodeGen.hh"
#include <sstream>
#include <iostream>
#include <memory>
#include <unordered_map>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Analysis/Passes.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/PassManager.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/IR/Instructions.h>

#include <llvm/Transforms/Scalar.h>
#include <llvm/Analysis/Passes.h>


namespace {
using namespace llvm;
using namespace rasmus::frontend;

template<int ...> struct seq {};
	template<int N, int ...S> struct gens : gens<N-1, N-1, S...> {};
	template<int ...S> struct gens<0, S...>{ typedef seq<S...> type; };
	

struct BorrowedLLVMVal {
public:
	llvm::Value * value;
	llvm::Value * type;
	BorrowedLLVMVal(): value(nullptr), type(nullptr) {};
	BorrowedLLVMVal(llvm::Value * value): value(value), type(nullptr) {}
	BorrowedLLVMVal(llvm::Value * value, llvm::Value * type): value(value), type(type) {}
};

struct LLVMVal {
public:
	llvm::Value * value;
	llvm::Value * type;
	bool owned;

	LLVMVal(): value(nullptr), type(nullptr), owned(false) {}
	LLVMVal(const BorrowedLLVMVal & v): value(v.value), type(v.type), owned(false) {} 
	
	LLVMVal(OwnedLLVMVal && v): value(v.value), type(v.type), owned(true) {
		v.value=nullptr; v.type=nullptr;
	}

	LLVMVal(LLVMVal && v): value(v.value), type(v.type), owned(v.owned) {
		v.value=nullptr;
		v.type=nullptr;
		v.owned=false;
	}
	
	LLVMVal & operator=(LLVMVal && v) {
		if (value || type || owned) ICE("Set of tainted LLVMVal");
		value=v.value;
		type=v.type;
		owned=v.owned;
		v.value=nullptr;
		v.type=nullptr;
		v.owned=false;
		return *this;
	}
	
	
	LLVMVal(const LLVMVal & o) = delete;
	LLVMVal & operator=(const LLVMVal & o) = delete;
	
	~LLVMVal() {
		if (!std::uncaught_exception() && (value || type || owned)) ICE("LLVMValue not propperly disposed of");
	}
};

class CodeGen: public LLVMCodeGen, public VisitorCRTP<CodeGen, LLVMVal> {
public:
	size_t uid=0;
	std::shared_ptr<Error> error;
	std::shared_ptr<Code> code;
	Module * module;
	IRBuilder<> builder;
	FunctionPassManager fpm;
	bool dumpRawFunctions;
	bool dumpOptFunctions;

	llvm::Type * voidType = llvm::Type::getVoidTy(getGlobalContext());
	llvm::Type * int8Type = llvm::Type::getInt8Ty(getGlobalContext());
	llvm::Type * int16Type = llvm::Type::getInt16Ty(getGlobalContext());
	llvm::Type * int32Type = llvm::Type::getInt32Ty(getGlobalContext());
	llvm::Type * int64Type = llvm::Type::getInt64Ty(getGlobalContext());
	llvm::PointerType * pointerType(llvm::Type * t) {return PointerType::getUnqual(t);}
	llvm::PointerType * voidPtrType = pointerType(int8Type);
	
	llvm::StructType * structType(std::string name, 
								  std::initializer_list<llvm::Type *> types) {
		return StructType::create(getGlobalContext(), ArrayRef<llvm::Type * >(types.begin(), types.end()), name);
	}
	
	llvm::FunctionType * functionType(llvm::Type * ret, std::initializer_list<llvm::Type *> args) {
		return FunctionType::get(ret, ArrayRef<llvm::Type * >(args.begin(), args.end()), false);
	}
	
	llvm::FunctionType * dtorType = functionType(voidType, {voidPtrType});;
	llvm::StructType * anyRetType = structType("AnyRet", {int64Type, int8Type});
	llvm::StructType * tupleEntryType = structType("TupleEntry", {pointerType(int8Type), int64Type, int8Type});
	llvm::StructType * funcBase = structType("FuncBase", {int32Type, int16Type, int16Type, voidPtrType, voidPtrType} );;
	llvm::StructType * objectBaseType = structType("ObjectBase", {int32Type, int16Type});

	
	CodeGen(std::shared_ptr<Error> error, std::shared_ptr<Code> code,
			llvm::Module * module, bool dumpRawFunctions,
			bool dumpOptFunctions): error(error), code(code), module(module),
									builder(getGlobalContext()), fpm(module), 
									dumpRawFunctions(dumpRawFunctions),
									dumpOptFunctions(dumpOptFunctions) {
		fpm.add(new DataLayout(module));
		fpm.add(createBasicAliasAnalysisPass());
		fpm.add(createInstructionCombiningPass());
		fpm.add(createReassociatePass());
		//fpm.add(createGVNPass());
		fpm.add(createCFGSimplificationPass());
		fpm.doInitialization();

		std::vector< std::pair<std::string, FunctionType *> > fs =
		{
			{"rm_print", functionType(voidType, {int8Type, int64Type})},
			{"rm_free", functionType(voidType, {voidPtrType})},
			{"rm_concatText", functionType(voidPtrType, {voidPtrType, voidPtrType})},
			{"rm_getConstText", functionType(voidPtrType, {pointerType(int8Type)})},
			{"rm_emitTypeError", functionType(voidType, {int32Type, int32Type, int8Type, int8Type})},
			{"rm_emitArgCntError", functionType(voidType, {int32Type, int32Type, int16Type, int16Type})},
			{"rm_unionRel", functionType(voidPtrType, {voidPtrType, voidPtrType})},
			{"rm_joinRel", functionType(voidPtrType, {voidPtrType, voidPtrType})},
			{"rm_diffRel", functionType(voidPtrType, {voidPtrType, voidPtrType})},
			{"rm_loadRel", functionType(voidPtrType, {pointerType(int8Type)})},
			{"rm_saveRel", functionType(voidType, {voidPtrType, pointerType(int8Type)})},
			{"rm_maxRel", functionType(int64Type, {voidPtrType, pointerType(int8Type)})},
			{"rm_minRel", functionType(int64Type, {voidPtrType, pointerType(int8Type)})},
			{"rm_addRel", functionType(int64Type, {voidPtrType, pointerType(int8Type)})},
			{"rm_multRel", functionType(int64Type, {voidPtrType, pointerType(int8Type)})},
			{"rm_countRel", functionType(int64Type, {voidPtrType, pointerType(int8Type)})},
			{"rm_selectRel", functionType(voidPtrType, {voidPtrType, voidPtrType})},
			{"rm_projectPlusRel", functionType(voidPtrType, {voidPtrType, int32Type, pointerType(pointerType(int8Type))})},
			{"rm_renameRel", functionType(voidPtrType, {voidPtrType, int32Type, pointerType(pointerType(int8Type))})},
			{"rm_projectMinusRel", functionType(voidPtrType, {voidPtrType, int32Type, pointerType(pointerType(int8Type))})},
			{"rm_substrText", functionType(voidPtrType, {voidPtrType, int64Type, int64Type})},
			{"rm_createFunction", functionType(voidPtrType, {int32Type})},
			{"rm_loadGlobalAny", functionType(voidType, {int32Type, pointerType(anyRetType)})},
			{"rm_saveGlobalAny", functionType(voidType, {int32Type, int64Type, int8Type})},
			{"rm_length", functionType(int64Type, {voidPtrType})},
			{"rm_tupEntry", functionType(voidType, {voidPtrType, pointerType(int8Type), pointerType(anyRetType)})},
			{"rm_createTup", functionType(voidPtrType, {int32Type, pointerType(tupleEntryType)})},
			{"rm_extendTup", functionType(voidPtrType, {voidPtrType, voidPtrType})},
			{"rm_tupRemove", functionType(voidPtrType, {voidPtrType, pointerType(int8Type)})},
			{"rm_tupHasEntry", functionType(int8Type, {voidPtrType, pointerType(int8Type)})},
			{"rm_relHasEntry", functionType(int8Type, {voidPtrType, pointerType(int8Type)})},
			{"rm_equalText", functionType(int8Type, {voidPtrType, voidPtrType})},
			{"rm_equalRel", functionType(int8Type, {voidPtrType, voidPtrType})},
			{"rm_equalTup", functionType(int8Type, {voidPtrType, voidPtrType})},
			{"rm_createRel", functionType(voidPtrType, {voidPtrType})}
		};

		for(auto p: fs)
			stdlib[p.first] = Function::Create(p.second, Function::ExternalLinkage, p.first, module);
	}
	
	Function * getStdlibFunc(std::string name) {
		auto x=stdlib.find(name);
		if (x == stdlib.end()) ICE("Unknow stdlib function", name);
		return x->second;
	}

	llvm::Type * llvmType(::Type t) {
		switch (t) {
		case TBool: return int8Type;
		case TInt: return int64Type;
		case TFunc: return pointerType(funcBase);
		case TText: 
		case TRel: 
			return voidPtrType;
		case TAny: return anyRetType;
		default:
			ICE("llvmType - Unhandled type ", t);
		}
	}
	
	llvm::Constant * int8(uint8_t value) {	return llvm::ConstantInt::get(int8Type, value);}
	llvm::Constant * int16(uint16_t value) {return llvm::ConstantInt::get(int16Type, value);}
	llvm::Constant * int32(uint32_t value) {return llvm::ConstantInt::get(int32Type, value);}
	llvm::Constant * int64(int64_t value) {return llvm::ConstantInt::get(int64Type, value);}

	llvm::Constant * undefInt = int64(std::numeric_limits<int64_t>::min());
	
	llvm::Value * typeRepr(::Type t) {
		return int8(t);
	}
	
	llvm::FunctionType * funcType(uint16_t argc) {
		std::vector<llvm::Type *> t;
		t.push_back(pointerType(funcBase));
		t.push_back(pointerType(anyRetType));
		for (size_t i=0; i < argc; ++i) {
			t.push_back(int64Type);
			t.push_back(int8Type);
		}
		return llvm::FunctionType::get(voidType, t, false);
	}


	Function * getFunction() {
		return builder.GetInsertBlock()->getParent();
	}

	BasicBlock * newBlock() {
		std::stringstream ss;
		ss << "b" << uid++;
		return BasicBlock::Create(getGlobalContext(), ss.str(), getFunction());
	}

	void forgetOwnership(LLVMVal & val) {
		val.value=nullptr;
		val.type=nullptr;
		val.owned=false;
	}

	void forgetOwnership(OwnedLLVMVal & val) {
		val.value=nullptr;
		val.type=nullptr;
	}

	OwnedLLVMVal stealOwnership(BorrowedLLVMVal val) {
		return OwnedLLVMVal(val.value, val.type);
	}

	BorrowedLLVMVal borrow(const LLVMVal & v) {
		return BorrowedLLVMVal(v.value, v.type);
	}

	BorrowedLLVMVal borrow(const OwnedLLVMVal & v) {
		return BorrowedLLVMVal(v.value, v.type);
	}

	OwnedLLVMVal takeOwnership(LLVMVal val, ::Type type) {
 		OwnedLLVMVal value(val.value, val.type);
		bool owned=val.owned;
		forgetOwnership(val);
		if (owned) return value;
		switch (type) {
		case TInt:
		case TBool:
			break;
		case TFunc:
		case TRel:
		case TText:
		{
			Value * v = builder.CreatePointerCast(value.value, pointerType(objectBaseType));
			Value * rc = builder.CreateAdd(builder.CreateLoad(builder.CreateConstGEP2_32(v, 0, 0)), int32(1));
			builder.CreateStore(rc, builder.CreateConstGEP2_32(v, 0, 0));
			break;
		}
		case TAny:
		{
			BasicBlock * b1 = newBlock();
			BasicBlock * b2 = newBlock();
			builder.CreateCondBr(builder.CreateICmpUGE(value.type, int8((int)TText)), b1, b2);
			builder.SetInsertPoint(b1);
			Value * p = builder.CreateIntToPtr(value.value, voidPtrType);
			Value * v = builder.CreatePointerCast(p, pointerType(objectBaseType));
			Value * rc = builder.CreateAdd(builder.CreateLoad(builder.CreateConstGEP2_32(v, 0, 0)), int32(1));
			builder.CreateStore(rc, builder.CreateConstGEP2_32(v, 0, 0));
			builder.CreateBr(b2);
			builder.SetInsertPoint(b2);
			break;
		}
		default:
			ICE("Unhandled type in takeOwnership", type);
		}
		return value;
	}
	
	OwnedLLVMVal takeOwnership(BorrowedLLVMVal value, ::Type type) {
		return takeOwnership(LLVMVal(value), type);
	}

	void disown(OwnedLLVMVal & value, ::Type type) {
		switch (type) {
		case TInt:
		case TBool:
			break;
		case TFunc:
		case TRel:
		case TText:
		case TTup:
		{
			
			//TODO CHECK FOR NULLPTR
			BasicBlock * b1 = newBlock();
			BasicBlock * b2 = newBlock();
			Value * v = builder.CreatePointerCast(value.value, pointerType(objectBaseType));
			Value * rc = builder.CreateSub(builder.CreateLoad(builder.CreateConstGEP2_32(v, 0, 0)), int32(1));
			builder.CreateStore(rc, builder.CreateConstGEP2_32(v, 0, 0));
			builder.CreateCondBr(builder.CreateICmpEQ(rc, int32(0)), b1, b2);
			builder.SetInsertPoint(b1);
			builder.CreateCall(getStdlibFunc("rm_free"), builder.CreatePointerCast(value.value, voidPtrType));
			builder.CreateBr(b2);
			builder.SetInsertPoint(b2);
		}
			break;
		case TAny:
		{
			//TODO CHECK FOR NULLPTR
			BasicBlock * b1 = newBlock();
			BasicBlock * b2 = newBlock();
			BasicBlock * b3 = newBlock();
			builder.CreateCondBr(builder.CreateICmpUGE(value.type, int8((int)TText)), b1, b3);
			builder.SetInsertPoint(b1);
			Value * p = builder.CreateIntToPtr(value.value, voidPtrType);
			Value * v = builder.CreatePointerCast(p, pointerType(objectBaseType));
			Value * rc = builder.CreateSub(builder.CreateLoad(builder.CreateConstGEP2_32(v, 0, 0)), int32(1));
			builder.CreateStore(rc, builder.CreateConstGEP2_32(v, 0, 0));
			builder.CreateCondBr(builder.CreateICmpEQ(rc, int32(0)), b2, b3);
			builder.SetInsertPoint(b2);
			builder.CreateCall(getStdlibFunc("rm_free"), p);
			builder.CreateBr(b3);
			builder.SetInsertPoint(b3);
			break;
		}
		default:
			ICE("Unhandled type in abandon", type);
		}
		forgetOwnership(value);
	}

	void disown(LLVMVal & value, ::Type type) {
		if (value.owned) {
			OwnedLLVMVal v(value.value, value.type);
			disown(v, type);
		}
		forgetOwnership(value);
	}


	BorrowedLLVMVal getUndef(::Type t) {
		switch(t) {
		case TInt:
			return BorrowedLLVMVal(undefInt);
		case TBool:
			return BorrowedLLVMVal(int8(2));
		case TAny:
			return BorrowedLLVMVal(undefInt, typeRepr(TInt));
		case TFunc:
			return BorrowedLLVMVal(llvm::Constant::getNullValue(pointerType(funcBase)));
		case TText:
			return extGlobalObject("undef_text");
		case TRel:
		case TTup:
		default:
			ICE("Unhandled undef", t);
		}
	}

	BorrowedLLVMVal cast(BorrowedLLVMVal value, ::Type tfrom, ::Type tto, NodePtr node) {
		if (tfrom == tto) return BorrowedLLVMVal(value.value, value.type);
		if (tto == TAny) {
			switch(tfrom) {
			case TInt:
				return BorrowedLLVMVal(value.value, typeRepr(tfrom));
			case TBool:
				return BorrowedLLVMVal(builder.CreateZExt(value.value, int64Type), typeRepr(tfrom));
			case TText:
			case TFunc:
			case TRel:
			case TTup:
				return BorrowedLLVMVal(builder.CreatePtrToInt(value.value, int64Type), typeRepr(tfrom));
            default:
                ICE("Unhandled type", tfrom);
			};
		}

		if (tfrom == TAny) {
			BasicBlock * fblock = newBlock();
			BasicBlock * nblock = newBlock();
			builder.CreateCondBr(builder.CreateICmpEQ(value.type, typeRepr(tto)), nblock, fblock);
			builder.SetInsertPoint(fblock);
			builder.CreateCall4(getStdlibFunc("rm_emitTypeError"),
								int32(node->charRange.lo), int32(node->charRange.hi),
								value.type,
								typeRepr(tto));
			builder.CreateUnreachable();
			builder.SetInsertPoint(nblock);
			switch (tto) {
			case TInt:
				return BorrowedLLVMVal(value.value);
			case TBool:
				return BorrowedLLVMVal(builder.CreateTruncOrBitCast(value.value, llvmType(tto)));
			case TText:
			case TFunc:
			case TRel:
			case TTup:
				return BorrowedLLVMVal(builder.CreateIntToPtr(value.value, voidPtrType));
			default:
				ICE("Unhandled type", tto);
			}
		}
		ICE("Unhandled type", tfrom);
	}
	
	LLVMVal cast(LLVMVal value, ::Type tfrom, ::Type tto, NodePtr node) {
		BorrowedLLVMVal s=cast(borrow(value), tfrom, tto, node);
		value.value = s.value;
		value.type = s.type;
		return value;
	}

	OwnedLLVMVal cast(OwnedLLVMVal value, ::Type tfrom, ::Type tto, NodePtr node) {
		BorrowedLLVMVal s=cast(borrow(value), tfrom, tto, node);
		value.value = s.value;
		value.type = s.type;
		return value;
	}

	
	LLVMVal castVisit(NodePtr node, ::Type tto) {
		return cast(visitNode(node), node->type, tto, node);
	}
	
	void finishFunction(Function * func) {
		llvm::verifyFunction(*func);
		if (dumpRawFunctions) func->dump();
		fpm.run(*func);
		if (dumpOptFunctions) func->dump();
	}

	BorrowedLLVMVal loadValue(Value * v, std::initializer_list<int> gep, ::Type type) {
		std::vector<Value *> GEP;
		for (int x: gep) GEP.push_back(int32(x));
		switch (type) {
		case TInt:
		case TBool:
		case TText:
		case TFunc:
		case TRel:
			return BorrowedLLVMVal(builder.CreateLoad(builder.CreateGEP(v, GEP)));
		case TAny:
		{
			GEP.push_back(int32(0));
			Value * value=builder.CreateGEP(v, GEP);
			GEP.pop_back();
			GEP.push_back(int32(1));
			Value * type=builder.CreateGEP(v, GEP);
			return BorrowedLLVMVal(builder.CreateLoad(value), builder.CreateLoad(type));
		}
		default:
			ICE("Unhandled type", type);
		}
	}

	void saveValue(Value * dst, std::initializer_list<int> gep, LLVMVal _v, ::Type type) {
		OwnedLLVMVal v=takeOwnership(std::move(_v), type);
		std::vector<Value *> GEP;
		for (int x: gep) GEP.push_back(int32(x));
		switch (type) {
		case TInt:
		case TBool:
		case TText:
		case TFunc:
		case TRel:
			builder.CreateStore(v.value, builder.CreateGEP(dst, GEP));
			break;
		case TAny:
		{
			GEP.push_back(int32(0));
			builder.CreateStore(v.value, builder.CreateGEP(dst, GEP));
			GEP.pop_back();
			GEP.push_back(int32(1));
			builder.CreateStore(v.type, builder.CreateGEP(dst, GEP));
			break;
		}
		default:
			ICE("Unhandled type", type);
		}
		forgetOwnership(v);
	}

	Value * globalString(std::string s) {
		Constant * c = ConstantDataArray::getString(getGlobalContext(), s);
		GlobalVariable * gv = new GlobalVariable(*module,
												 c->getType(),
												 true,
												 llvm::GlobalValue::PrivateLinkage,
												 c);
		return builder.CreateConstGEP2_32(gv, 0, 0);
	}
	
	Value * globalString(Token t) {
		return globalString(t.getText(code));
	}
	
	BorrowedLLVMVal extGlobalObject(const char * name) {
		if (module->getGlobalVariable(name)) return BorrowedLLVMVal(module->getGlobalVariable(name));
		return BorrowedLLVMVal(new GlobalVariable(*module,
												  int8Type,
												  true,
												  llvm::GlobalValue::ExternalLinkage,
												  nullptr,
												  name));
	}

	LLVMVal visit(std::shared_ptr<VariableExp> node) {
		if (NodePtr store = node->store.lock()) {
			if (store->nodeType != NodeType::AssignmentExp) return borrow(store->llvmVal);
			std::shared_ptr<AssignmentExp> st=std::static_pointer_cast<AssignmentExp>(store);
			
			if (st->globalId == NOT_GLOBAL)
				return borrow(store->llvmVal);
			
			Value * rv = builder.CreateAlloca(anyRetType);

			builder.CreateCall2(getStdlibFunc("rm_loadGlobalAny"),
								int32(st->globalId),
								rv);

			return cast(BorrowedLLVMVal(builder.CreateLoad(builder.CreateConstGEP2_32(rv, 0, 0)), 
										builder.CreateLoad(builder.CreateConstGEP2_32(rv, 0, 1))),
						TAny, node->type, node);
			
		} else {
			return OwnedLLVMVal(builder.CreateCall(
									getStdlibFunc("rm_loadRel"),
									globalString(node->nameToken)));
		}
	} 
	
	LLVMVal visit(std::shared_ptr<AssignmentExp> node) {
		LLVMVal val = castVisit(node->valueExp, node->type);
		if (node->globalId == NOT_GLOBAL) {
			node->llvmVal=takeOwnership(borrow(val), node->type);
			return val;
		}

		{
			BorrowedLLVMVal v2 = cast(borrow(val), node->type, TAny, node);
			builder.CreateCall3(getStdlibFunc("rm_saveGlobalAny"), 
								int32(node->globalId),
								v2.value,
								v2.type);
		}

		switch (node->type) {
		case TRel:
		{
			builder.CreateCall2(getStdlibFunc("rm_saveRel"), 
								builder.CreateIntToPtr(val.value, voidPtrType),
								globalString(node->nameToken));
			break;
		}
		case TAny:
		{
			std::stringstream ss1;
			ss1 << "store_rel_" << uid++;
			BasicBlock * sblock = BasicBlock::Create(getGlobalContext(), ss1.str(), getFunction());
			
			std::stringstream ss2;
			ss2 << "continue_" << uid++;
			BasicBlock * cblock = BasicBlock::Create(getGlobalContext(), ss2.str(), getFunction());
			
			
			builder.CreateCondBr(builder.CreateICmpEQ(val.type, typeRepr(TRel)), sblock, cblock);
			builder.SetInsertPoint(sblock);
			builder.CreateCall2(getStdlibFunc("rm_saveRel"), 
								builder.CreateIntToPtr(val.value, voidPtrType),
								globalString(node->nameToken));
			builder.CreateBr(cblock);
			builder.SetInsertPoint(cblock);
			break;
		}
		default:
			break;
		}
		return val;
	}

	LLVMVal visit(std::shared_ptr<IfExp> node) {
		// The following code is no good with ownership or anytype
		Value * value=nullptr;
		Value * type=nullptr;
		switch (node->type) {
		case TText:
		case TRel:
		case TTup:
			value = builder.CreateAlloca(voidPtrType);
			break;
		case TBool:
			value = builder.CreateAlloca(int8Type);
			break;
		case TInt:
			value = builder.CreateAlloca(int64Type);
			break;
		case TAny:
			value = builder.CreateAlloca(int64Type);
			type = builder.CreateAlloca(int8Type);
			break;
		default:
			ICE("Unhandled", node->type);
		}


		BasicBlock * end = newBlock();
		bool done=false;

		for (auto choice: node->choices) {
			LLVMVal cond = castVisit(choice->condition, TBool);
		 	llvm::ConstantInt * ci = dyn_cast<llvm::ConstantInt>(cond.value);
		 	if (done || (ci && ci->isZeroValue())) {
		 		error->reportWarning("Branch never taken", choice->arrowToken);
				forgetOwnership(cond);
		 		continue;
		 	}

			if (ci && ci->equalsInt(3)) {
				LLVMVal v=takeOwnership(castVisit(choice->value, node->type), node->type);
				builder.CreateStore(v.value, value);
				if (type) builder.CreateStore(v.type, type);
				forgetOwnership(v);
				builder.CreateBr(end);
				done=true;
			} else {
				BasicBlock * b1 = newBlock();		
				BasicBlock * b2 = newBlock();
				builder.CreateCondBr(builder.CreateICmpEQ(cond.value, int8(3)), b1, b2);
				
				builder.SetInsertPoint(b1);
				LLVMVal v=takeOwnership(castVisit(choice->value, node->type), node->type);
				builder.CreateStore(v.value, value);
				if (type) builder.CreateStore(v.type, type);
				forgetOwnership(v);
				builder.CreateBr(end);
				
				builder.SetInsertPoint(b2);
			}
			forgetOwnership(cond);
		}

		if (!done) {
			LLVMVal v=takeOwnership(getUndef(node->type), node->type);
			builder.CreateStore(v.value, value);
			if (type) builder.CreateStore(v.type, type);
			forgetOwnership(v);			
			builder.CreateBr(end);
		}
		builder.SetInsertPoint(end);
	
		return OwnedLLVMVal(builder.CreateLoad(value), type?builder.CreateLoad(type):nullptr);
	}

	LLVMVal visit(std::shared_ptr<ForallExp>) {
		ICE("ForallExp");
	}


	LLVMVal visit(std::shared_ptr<FuncExp> node) {
		// Create function type
		FunctionType * ft = funcType(node->args.size());

		std::vector<llvm::Type *> captureContent = {
			int32Type, //Refcount
			int16Type, //Type
			int16Type, //Number Of arguments
			pointerType(dtorType), //Dtor
			pointerType(ft), //Function ptr
		};
		for (auto cap: node->captures)
			captureContent.push_back(llvmType(cap->type));
		
		StructType * captureType = StructType::create(getGlobalContext(), captureContent);
		assert(captureType->isSized());
		
		//Cache currentFunction
		auto old_ip = builder.saveIP();

		Function * dtor=nullptr;
		
		// Create dtor 
		{
			std::stringstream ss;
			ss << "f_dtor" << uid;
			dtor = Function::Create(dtorType, Function::InternalLinkage, ss.str(), module);
			BasicBlock * block = BasicBlock::Create(getGlobalContext(), "entry", dtor);
			builder.SetInsertPoint(block);
			auto & al = dtor->getArgumentList();
			auto self = al.begin();
			self->setName("self");
			Value * selfv = builder.CreatePointerCast(self, pointerType(captureType));

			//go through the capture list and abandon all
			for (size_t i=0; i < node->captures.size(); ++i) {
				OwnedLLVMVal val(stealOwnership(loadValue(selfv, {0, 5+(int)i}, node->captures[i]->type)));
				disown(val, node->captures[i]->type);
			}

			builder.CreateRetVoid();

			finishFunction(dtor);
		}
		
		Function * function=nullptr;
		
		// Create function inner
		{
			std::stringstream ss;
			ss << "f" << uid++;
			function = Function::Create(ft, Function::InternalLinkage, ss.str(), module);
			BasicBlock * block = BasicBlock::Create(getGlobalContext(), "entry", function);
			builder.SetInsertPoint(block);
			// Setup args		
			auto & al = function->getArgumentList();   
			auto arg = al.begin();
			auto self = &(*arg);
			++arg;
			auto ret = &(*arg);
			++arg;
			
			self->setName("self");
			ret->setName("ret");
			Value * selfv = builder.CreatePointerCast(self, pointerType(captureType));
			for (auto a: node->args) {
				Value * v = &(*arg);
				++arg;
				Value * t = &(*arg);
				++arg;
				a->llvmVal = stealOwnership(cast(BorrowedLLVMVal(v, t), TAny, a->type, a));
			}

			for (size_t i=0; i < node->captures.size(); ++i)
				node->captures[i]->llvmVal = stealOwnership(loadValue(selfv, {0, 5+(int)i}, node->captures[i]->type));
        
			// Build function code
			OwnedLLVMVal x = cast(
				takeOwnership(visitNode(node->body), node->body->type), node->body->type, TAny, node->body);
			
			for (auto a: node->args)
				forgetOwnership(a->llvmVal);
			
			for (auto c: node->captures)
				forgetOwnership(c->llvmVal);

			builder.CreateStore(x.value, builder.CreateConstGEP2_32(ret, 0, 0));
			builder.CreateStore(x.type, builder.CreateConstGEP2_32(ret, 0, 1));
			builder.CreateRetVoid();
			forgetOwnership(x);
			finishFunction(function);
		}

		// Revert state
		builder.restoreIP(old_ip);
		Constant* AllocSize = ConstantExpr::getSizeOf(captureType);
		AllocSize = ConstantExpr::getTruncOrBitCast(AllocSize, int32Type);
		auto pp = builder.CreateCall(getStdlibFunc("rm_createFunction"), AllocSize);
		auto p = builder.CreatePointerCast(pp, pointerType(captureType));
		builder.CreateStore(int32(1), builder.CreateConstGEP2_32(p, 0, 0)); //RefCount
		builder.CreateStore(int16(node->args.size()), builder.CreateConstGEP2_32(p, 0, 2)); //Argc
		builder.CreateStore(dtor, builder.CreateConstGEP2_32(p, 0, 3)); //Dtor
		builder.CreateStore(function, builder.CreateConstGEP2_32(p, 0, 4)); //Fptr
		
        // Store captures
		for (size_t i=0; i < node->captures.size(); ++i) {
			NodePtr n(node->captures[i]->store);
			assert(n);
			saveValue(p, {0, 5+(int)i}, borrow(n->llvmVal), node->captures[i]->type);;
		}

		return OwnedLLVMVal(p);
	}
	
	LLVMVal visit(std::shared_ptr<TupExp> exp) {
		std::vector<LLVMVal> values;
		Value * entries = builder.CreateAlloca(tupleEntryType, int32(exp->items.size()) );
		for (size_t i=0; i < exp->items.size(); ++i) {
			auto item = exp->items[i];
			values.push_back( visitNode(item->exp) );
			BorrowedLLVMVal v = cast(borrow(values.back()), item->exp->type, TAny, item->exp);
			
			builder.CreateStore(globalString(item->nameToken), builder.CreateConstGEP2_32(entries, i, 0)); //name
			builder.CreateStore(v.value, builder.CreateConstGEP2_32(entries, i, 1)); //value
			builder.CreateStore(v.type, builder.CreateConstGEP2_32(entries, i, 2)); //type
		}

		OwnedLLVMVal r(builder.CreateCall2(getStdlibFunc("rm_createTup"), int32(exp->items.size()), entries));
		
		for (size_t i=0; i < exp->items.size(); ++i)
			disown(values[i], exp->items[i]->exp->type);
		return LLVMVal(std::move(r));
	}

	LLVMVal visit(std::shared_ptr<BlockExp> node) {
		for (auto v: node->vals)
			v->exp->llvmVal = takeOwnership(visitNode(v->exp), v->exp->type);

		// This could be a reference to one of the vals, so we take ownership
		OwnedLLVMVal r = takeOwnership(visitNode(node->inExp), node->inExp->type); 
		
		for (auto v: node->vals) 
			disown(v->exp->llvmVal, v->exp->type);
		
		return LLVMVal(std::move(r));
	}




	LLVMVal visit(std::shared_ptr<BuiltInExp> node) {
		switch (node->nameToken.id) {
		case TK_PRINT:
		{
			LLVMVal v=castVisit(node->args[0], TAny);
			builder.CreateCall2(getStdlibFunc("rm_print"), v.type, v.value);
			disown(v, TAny);
			return OwnedLLVMVal(int8(1));
		}
		case TK_HAS:
			if (node->args[0]->type == TTup) {
				LLVMVal v=castVisit(node->args[0], TTup);
				OwnedLLVMVal r(builder.CreateCall2(
								   getStdlibFunc("rm_tupHasEntry"), 
								   v.value, 
								   globalString(std::static_pointer_cast<VariableExp>(node->args[1])->nameToken.getText(code))));
				disown(v, TTup);
				return LLVMVal(std::move(r));
			} else if (node->args[0]->type == TRel) {
				LLVMVal v=castVisit(node->args[0], TRel);
				OwnedLLVMVal r(builder.CreateCall2(
								   getStdlibFunc("rm_relHasEntry"), 
								   v.value, 
								   globalString(std::static_pointer_cast<VariableExp>(node->args[1])->nameToken.getText(code))));
				disown(v, TRel);
				return LLVMVal(std::move(r));
			} else if (node->args[0]->type == TAny) {
				ICE("Dynamic switching not implemented");
			} else
				ICE("Unexpected type", node->args[0]->type);
			break;
		case TK_MAX:
		{	
			LLVMVal v=castVisit(node->args[0], TRel);
			OwnedLLVMVal r(builder.CreateCall2(
								   getStdlibFunc("rm_maxRel"), 
								   v.value, 
								   globalString(std::static_pointer_cast<VariableExp>(node->args[1])->nameToken.getText(code))));
			disown(v, TRel);
			return LLVMVal(std::move(r));
		}
		case TK_MIN:
		{	
			LLVMVal v=castVisit(node->args[0], TRel);
			OwnedLLVMVal r(builder.CreateCall2(
								   getStdlibFunc("rm_minRel"), 
								   v.value, 
								   globalString(std::static_pointer_cast<VariableExp>(node->args[1])->nameToken.getText(code))));
			disown(v, TRel);
			return LLVMVal(std::move(r));
		}
		case TK_ADD:
		{	
			LLVMVal v=castVisit(node->args[0], TRel);
			OwnedLLVMVal r(builder.CreateCall2(
								   getStdlibFunc("rm_addRel"), 
								   v.value, 
								   globalString(std::static_pointer_cast<VariableExp>(node->args[1])->nameToken.getText(code))));
			disown(v, TRel);
			return LLVMVal(std::move(r));
		}
		case TK_MULT:
		{	
			LLVMVal v=castVisit(node->args[0], TRel);
			OwnedLLVMVal r(builder.CreateCall2(
								   getStdlibFunc("rm_multRel"), 
								   v.value, 
								   globalString(std::static_pointer_cast<VariableExp>(node->args[1])->nameToken.getText(code))));
			disown(v, TRel);
			return LLVMVal(std::move(r));
		}
		case TK_COUNT:
		{	
			LLVMVal v=castVisit(node->args[0], TRel);
			OwnedLLVMVal r(builder.CreateCall2(
								   getStdlibFunc("rm_countRel"), 
								   v.value, 
								   globalString(std::static_pointer_cast<VariableExp>(node->args[1])->nameToken.getText(code))));
			disown(v, TRel);
			return LLVMVal(std::move(r));
		}
		default:
			ICE("BuildIn not implemented", node->nameToken.id);
		}
	}

	LLVMVal visit(std::shared_ptr<ConstantExp> node) {
		switch (node->valueToken.id) {
		case TK_FALSE:
			return OwnedLLVMVal(int8(0));
        case TK_TRUE:
			return OwnedLLVMVal(int8(3));
        case TK_STDBOOL:
			return getUndef(TBool);
		case TK_INT:
			return OwnedLLVMVal(int64(atol(node->valueToken.getText(code).c_str())));
		case TK_TEXT: {
			std::string text=node->valueToken.getText(code);
			return OwnedLLVMVal( builder.CreateCall(
									 getStdlibFunc("rm_getConstText"),
									 globalString(text.substr(1, text.size()-2))));
		}
		case TK_STDTEXT:
			return getUndef(TText);
        case TK_STDINT:
			return getUndef(TInt);
        case TK_ZERO:
			return extGlobalObject("zero_rel");
		case TK_ONE:
			return extGlobalObject("one_rel");
		default:
			ICE("No codegen for", node->valueToken.id);
			break;		
		}
	}
	
	LLVMVal visit(std::shared_ptr<UnaryOpExp> node) {
		switch (node->opToken.id) {
		case TK_NOT: {
			LLVMVal v=castVisit(node->exp, TBool);
			LLVMVal r=cast(OwnedLLVMVal(
							   builder.CreateSelect(
								   builder.CreateICmpEQ(v.value, int8(2)),
								   int8(2),
								   builder.CreateSub(int8(3), v.value))), TBool, node->type, node);
			disown(v, TBool);
			return r;
		}
		case TK_MINUS:
		{
			LLVMVal v=castVisit(node->exp, TInt);
			LLVMVal r=cast(OwnedLLVMVal(builder.CreateNeg(v.value)), TInt, node->type, node);
			disown(v, TInt);
			return r;
		}
		default:
			ICE("Unhandled operator", node->opToken.id);
		}
	}

	LLVMVal visit(std::shared_ptr<RelExp> exp) {
		LLVMVal v = castVisit(exp->exp, TTup);
		LLVMVal v2 = OwnedLLVMVal(builder.CreateCall(getStdlibFunc("rm_createRel"), v.value));
		disown(v, TTup);
		return v2;
	}

	LLVMVal visit(std::shared_ptr<LenExp> exp) {
		LLVMVal v=castVisit(exp->exp, TAny);
		LLVMVal v2=OwnedLLVMVal(builder.CreateCall(
									getStdlibFunc("rm_length"), 
									builder.CreateIntToPtr(v.value, voidPtrType)));
		
		disown(v, TAny);
		return v2;
	}

    LLVMVal visit(std::shared_ptr<FuncInvocationExp> node) {
        FunctionType * ft = funcType(node->args.size());
		
		LLVMVal cap=castVisit(node->funcExp, TFunc);
		Value * capture = builder.CreatePointerCast(cap.value, pointerType(funcBase));
		Value * rv = builder.CreateAlloca(anyRetType);
		
		std::stringstream ss1;
		ss1 << "check_fail_" << uid++;
		BasicBlock * fblock = BasicBlock::Create(getGlobalContext(), ss1.str(), getFunction());
		
		std::stringstream ss2;
		ss2 << "check_succ_" << uid++;
		BasicBlock * nblock = BasicBlock::Create(getGlobalContext(), ss2.str(), getFunction());
		
		Value * argc = builder.CreateLoad(builder.CreateConstGEP2_32(capture, 0, 2));
		Value * margc = int16(node->args.size());
		builder.CreateCondBr(builder.CreateICmpEQ(argc, margc), nblock, fblock);
		
		builder.SetInsertPoint(fblock);

		builder.CreateCall4(getStdlibFunc("rm_emitArgCntError"), 
							int32(node->charRange.lo), 
							int32(node->charRange.hi),
							margc, 
							argc);
		builder.CreateUnreachable();

		builder.SetInsertPoint(nblock);

		std::vector<LLVMVal> ac;
		std::vector<Value *> args={capture, rv};
		for (auto arg: node->args) {
			ac.push_back(castVisit(arg, TAny));
			args.push_back(ac.back().value);
			args.push_back(ac.back().type);
		}

		Value * voidfp = builder.CreateLoad(builder.CreateConstGEP2_32(capture, 0, 4));
		Value * fp = builder.CreatePointerCast(voidfp, pointerType(ft));
		builder.CreateCall(fp, args);
		
		for (auto & a: ac)
			disown(a, TAny);
		disown(cap, TFunc);
		return OwnedLLVMVal(builder.CreateLoad(builder.CreateConstGEP2_32(rv, 0, 0)), 
							builder.CreateLoad(builder.CreateConstGEP2_32(rv, 0, 1)));
	}

	LLVMVal visit(std::shared_ptr<SubstringExp> node) {
		LLVMVal s = castVisit(node->stringExp, TText);
		LLVMVal f = castVisit(node->fromExp, TInt);
		LLVMVal t = castVisit(node->toExp, TInt);
		LLVMVal r = cast(OwnedLLVMVal(builder.CreateCall3(getStdlibFunc("rm_substrText"), s.value, f.value, t.value)), TText, node->type, node);
		disown(s, TText);
		disown(f, TInt);
		disown(t, TInt);
		return r;;
	}

	LLVMVal visit(std::shared_ptr<RenameExp> exp) {
		Value * names = builder.CreateAlloca(voidPtrType, int32(2*exp->renames.size()) );
		for (size_t i=0; i < exp->renames.size(); ++i) {
			builder.CreateStore(globalString(exp->renames[i]->fromNameToken), builder.CreateConstGEP1_32(names, 2*i));
			builder.CreateStore(globalString(exp->renames[i]->toNameToken), builder.CreateConstGEP1_32(names, 2*i+1));
		}
		
		LLVMVal rel=castVisit(exp->lhs, TRel);
		LLVMVal ret=OwnedLLVMVal(builder.CreateCall3(getStdlibFunc("rm_renameRel"), rel.value, int32(exp->renames.size()), names));
		disown(rel, TRel);
		return ret;

	}
        
	LLVMVal visit(std::shared_ptr<DotExp> node) {
		LLVMVal tup=castVisit(node->lhs, TTup);
		Value * rv = builder.CreateAlloca(anyRetType);
		builder.CreateCall3(getStdlibFunc("rm_tupEntry"), tup.value, globalString(node->nameToken), rv);
		
		OwnedLLVMVal r=cast(OwnedLLVMVal(builder.CreateLoad(builder.CreateConstGEP2_32(rv, 0, 0)), 
										 builder.CreateLoad(builder.CreateConstGEP2_32(rv, 0, 1))),
							TAny, node->type, node);
		disown(tup, TTup);
		return LLVMVal(std::move(r));
	}

	LLVMVal visit(std::shared_ptr<TupMinus> node) {
		LLVMVal tup=castVisit(node->lhs, TTup);

		OwnedLLVMVal ans(builder.CreateCall2(getStdlibFunc("rm_tupRemove"), tup.value, globalString(node->nameToken)));
		disown(tup, TTup);
		return LLVMVal(std::move(ans));
	}


	LLVMVal visit(std::shared_ptr<AtExp>) {
		ICE("At");
	}

	LLVMVal visit(std::shared_ptr<ProjectExp> exp) {
		Value * names = builder.CreateAlloca(voidPtrType, int32(exp->names.size()) );
		for (size_t i=0; i < exp->names.size(); ++i)
			builder.CreateStore(globalString(exp->names[i]), builder.CreateConstGEP1_32(names, i));
		
		LLVMVal rel=castVisit(exp->lhs, TRel);
		LLVMVal ret;
		switch (exp->projectionToken.id) {
		case TK_PROJECT_MINUS:
			ret=OwnedLLVMVal(builder.CreateCall3(getStdlibFunc("rm_projectMinusRel"), rel.value, int32(exp->names.size()), names));
			break;
		case TK_PROJECT_PLUS:
			ret=OwnedLLVMVal(builder.CreateCall3(getStdlibFunc("rm_projectPlusRel"), rel.value, int32(exp->names.size()), names));
			break;
		default:
			ICE("Bad project", exp->projectionToken.id);
		}
		disown(rel, TRel);
		return ret;
	}

	LLVMVal visit(std::shared_ptr<Choice>) {ICE("Choice");}
	LLVMVal visit(std::shared_ptr<FuncCaptureValue>) {ICE("FuncCaptureValue");}
	LLVMVal visit(std::shared_ptr<FuncArg>) {ICE("FuncArg");}
	LLVMVal visit(std::shared_ptr<TupItem>) {ICE("TupItem");}
	LLVMVal visit(std::shared_ptr<InvalidExp>) {ICE("InvalidExp");}
	LLVMVal visit(std::shared_ptr<Val>) {ICE("Val");}
	LLVMVal visit(std::shared_ptr<RenameItem>) {ICE("RenameItem");}

	template <typename T>
	struct bwh {
		typedef BorrowedLLVMVal t;
	};

	template <typename T>
	struct th {
		typedef ::Type t;
	};

	template <typename ...T>
	struct OpImpl {
		::Type ret;
		std::array<::Type, sizeof...(T)> types;
		std::function<LLVMVal(typename bwh<T>::t ...)> func;
	};

	template <typename ...T>
	OpImpl<typename th<T>::t...> dOp(
		::Type rtype, 
		std::function<LLVMVal(typename bwh<T>::t ...)> func, 
		T... types) {
		typedef OpImpl<typename th<T>::t...> rt; 
		return rt{rtype, {types...}, func};
	}


	template <typename ...T>
	OpImpl<typename th<T>::t...> dOp(
		::Type rtype, 
		std::function<LLVMVal(CodeGen&, typename bwh<T>::t ...)> func, 
		T... types) {
		typedef OpImpl<typename th<T>::t...> rt; 
		return rt{rtype, {types...}, 
				[func,this](BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) -> LLVMVal {
					return func(*this, lhs, rhs);
				}};
	}	

	template <int i>
	struct bwi {
		typedef BorrowedLLVMVal t;
	};

	template <int ...S>
	LLVMVal doCall(seq<S...>, 
				   std::function<LLVMVal(typename bwi<S>::t ...)> func,
				   std::array<LLVMVal, sizeof...(S)> & v) {
		return func(borrow(v[S])...);
	}

	template <typename ...T>
	LLVMVal opImp(std::shared_ptr<Node> node,
				  std::vector<OpImpl<typename th<T>::t...> > ops,
				  T ... c
		) {
		const int N=sizeof...(T);
		typedef OpImpl<typename th<T>::t...> oi_t;
		std::vector<oi_t> matches;

		std::array<::Type, N> types={c->type...};
		for (auto op: ops) {
			bool ok=true;
			for (size_t i=0; i < N; ++i) 
				ok = ok && (types[i] == op.types[i] || types[i] == TAny);
			if (!ok) continue;
			matches.push_back(op);
		}
	
		if (matches.size() == 0)
		 	ICE("Infeasible types in binopImpl, there is a bug in the typechecker!!");
			
		if (matches.size() == 1) {
		 	// Even though there might be any arguments
			// we have determined that this is the only possible alloweable call
		 	oi_t h = matches[0];

			std::array<std::shared_ptr<Node>, N> clds={std::static_pointer_cast<Node>(c)...};
			std::array<LLVMVal, N> values;
			for (size_t i=0; i < N; ++i)
				values[i] = castVisit(clds[i], h.types[i]);
			
			LLVMVal v=doCall(typename gens<N>::type(), h.func, values);;
			
			LLVMVal r(cast(std::move(v), h.ret, node->type, node));

			for (size_t i=0; i < N; ++i)
				disown(values[i], h.types[i]);
			
			return r;
		} 
		
		// There was more then one possible operator we want to call
		// So we have to determine on runtime which is the right
		ICE("Dynamic binops are not implemented");
	}

	  
	struct BinopHelp {
		::Type lhsType;
		::Type rhsType;
		::Type resType;
		std::function<LLVMVal(CodeGen&, BorrowedLLVMVal, BorrowedLLVMVal) > func;
	};
	
	LLVMVal binopImpl(std::shared_ptr<BinaryOpExp> node, std::initializer_list<BinopHelp> types) {
		std::vector<OpImpl<::Type, ::Type> > ops;
		for (BinopHelp h: types) {
			ops.push_back( dOp(h.resType, h.func, h.lhsType, h.rhsType) );
		}
		return opImp(node, ops, node->lhs, node->rhs);
	}
	
	LLVMVal binopAddInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSelect(builder.CreateICmpEQ(lhs.value, undefInt), undefInt,
								 builder.CreateSelect(builder.CreateICmpEQ(rhs.value, undefInt), undefInt,
													  builder.CreateAdd(lhs.value, rhs.value))));
	}

	LLVMVal binopMinusInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSelect(builder.CreateICmpEQ(lhs.value, undefInt), undefInt,
								 builder.CreateSelect(builder.CreateICmpEQ(rhs.value, undefInt), undefInt,
													  builder.CreateSub(lhs.value, rhs.value))));
	}

	LLVMVal binopMulInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSelect(builder.CreateICmpEQ(lhs.value, undefInt), undefInt,
								 builder.CreateSelect(builder.CreateICmpEQ(rhs.value, undefInt), undefInt,
													  builder.CreateMul(lhs.value, rhs.value))));
	}

	LLVMVal binopDivInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSelect(builder.CreateICmpEQ(lhs.value, undefInt), undefInt,
								 builder.CreateSelect(builder.CreateICmpEQ(rhs.value, undefInt), undefInt,
													  builder.CreateSelect(builder.CreateICmpEQ(rhs.value, int64(0)), undefInt,
																		   builder.CreateSDiv(lhs.value, rhs.value)))));
	}

	LLVMVal binopModInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSelect(builder.CreateICmpEQ(lhs.value, undefInt), undefInt,
								 builder.CreateSelect(builder.CreateICmpEQ(rhs.value, undefInt), undefInt,
													  builder.CreateSelect(builder.CreateICmpEQ(rhs.value, int64(0)), undefInt,
																		   builder.CreateSRem(lhs.value, rhs.value)))));
	}

	LLVMVal binopConcat(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_concatText"), lhs.value, rhs.value));
	}
	
	LLVMVal binopLessInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpSLT(lhs.value, rhs.value));
	}

	LLVMVal binopLessBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpULT(lhs.value, rhs.value));
	}

	LLVMVal binopLessEqualInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpSLE(lhs.value, rhs.value));
	}

	LLVMVal binopLessEqualBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpULE(lhs.value, rhs.value));
	}

	LLVMVal binopGreaterInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpSGT(lhs.value, rhs.value));
	}

	LLVMVal binopGreaterBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpUGT(lhs.value, rhs.value));
	}

	LLVMVal binopGreaterEqualInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpSGE(lhs.value, rhs.value));
	}

	LLVMVal binopGreaterEqualBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpUGE(lhs.value, rhs.value));
	}

	LLVMVal binopEqualInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpEQ(lhs.value, rhs.value));
	}

	LLVMVal binopEqualBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSelect(
				builder.CreateICmpEQ(lhs.value, int8(2)),
				int8(2),
				builder.CreateSelect(
					builder.CreateICmpEQ(rhs.value, int8(2)),
					int8(2),
					builder.CreateSelect(
						builder.CreateICmpEQ(lhs.value, rhs.value),
						int8(3),
						int8(0)))));
	}

	LLVMVal binopEqualText(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_equalText"), lhs.value, rhs.value));
	}

	LLVMVal binopEqualRel(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_equalRel"), lhs.value, rhs.value));
	}

	LLVMVal binopEqualTup(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_equalTup"), lhs.value, rhs.value));
	}


	LLVMVal binopDifferentInt(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateICmpNE(lhs.value, rhs.value));
	}

	LLVMVal binopDifferentBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSelect(
				builder.CreateICmpEQ(lhs.value, int8(2)),
				int8(2),
				builder.CreateSelect(
					builder.CreateICmpEQ(rhs.value, int8(2)),
					int8(2),
					builder.CreateSelect(
						builder.CreateICmpEQ(lhs.value, rhs.value),
						int8(0),
						int8(3)))));
	}

	LLVMVal binopDifferentText(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSub(int8(3), builder.CreateCall2(getStdlibFunc("rm_equalText"), lhs.value, rhs.value)));
	}

	LLVMVal binopDifferentRel(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSub(int8(3), builder.CreateCall2(getStdlibFunc("rm_equalRel"), lhs.value, rhs.value)));
	}

	LLVMVal binopDifferentTup(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(
			builder.CreateSub(int8(3), builder.CreateCall2(getStdlibFunc("rm_equalTup"), lhs.value, rhs.value)));
	}


	LLVMVal binopAndBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateAnd(lhs.value, rhs.value));
	}

	LLVMVal binopOrBool(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateOr(lhs.value, rhs.value));
	}

	LLVMVal binopUnionRel(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_unionRel"), lhs.value, rhs.value));
	}

	LLVMVal binopJoinRel(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_joinRel"), lhs.value, rhs.value));
	}

	LLVMVal binopDiffRel(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_diffRel"), lhs.value, rhs.value));
	}

	LLVMVal binopSelectRel(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_selectRel"), lhs.value, 
												builder.CreatePointerCast(rhs.value, voidPtrType)));
	}

	LLVMVal binopExtendTup(BorrowedLLVMVal lhs, BorrowedLLVMVal rhs) {
		return OwnedLLVMVal(builder.CreateCall2(getStdlibFunc("rm_extendTup"), lhs.value, rhs.value));
	}
	
	LLVMVal visit(std::shared_ptr<BinaryOpExp> node) {
		switch (node->opToken.id) {
		case TK_CONCAT:
			return binopImpl(node, { {TText, TText, TText, &CodeGen::binopConcat} });
		case TK_PLUS:
			return binopImpl(node, { 
					{TInt, TInt, TInt, &CodeGen::binopAddInt},
					{TRel, TRel, TRel, &CodeGen::binopUnionRel}
				});
		case TK_MUL:
			return binopImpl(node, {
					{TInt, TInt, TInt, &CodeGen::binopMulInt},
					{TRel, TRel, TRel, &CodeGen::binopJoinRel}
				});
		case TK_MINUS:
			return binopImpl(node, { 
					{TInt, TInt, TInt, &CodeGen::binopMinusInt},
					{TRel, TRel, TRel, &CodeGen::binopDiffRel}
				});
		case TK_DIV:
			return binopImpl(node, { {TInt, TInt, TInt, &CodeGen::binopDivInt} });
		case TK_MOD:
			return binopImpl(node, { {TInt, TInt, TInt, &CodeGen::binopModInt} });
		case TK_LESS:
			return binopImpl(node, { 
					{TInt, TInt, TBool, &CodeGen::binopLessInt} ,
					{TBool, TBool, TBool, &CodeGen::binopLessBool} ,
						});
		case TK_LESSEQUAL:
			return binopImpl(node, { 
					{TInt, TInt, TBool, &CodeGen::binopLessEqualInt} ,
					{TBool, TBool, TBool, &CodeGen::binopLessEqualBool} ,
						});
		case TK_GREATER:
			return binopImpl(node, { 
					{TInt, TInt, TBool, &CodeGen::binopGreaterInt} ,
					{TBool, TBool, TBool, &CodeGen::binopGreaterBool} ,
						});
		case TK_GREATEREQUAL:
			return binopImpl(node, { 
					{TInt, TInt, TBool, &CodeGen::binopGreaterEqualInt} ,
					{TBool, TBool, TBool, &CodeGen::binopGreaterEqualBool} ,
						});
		case TK_EQUAL:
			return binopImpl(node, { 
					{TInt, TInt, TBool, &CodeGen::binopEqualInt} ,
					{TBool, TBool, TBool, &CodeGen::binopEqualBool} ,
					{TRel, TRel, TBool, &CodeGen::binopEqualRel} ,
					{TTup, TTup, TBool, &CodeGen::binopEqualTup} ,
					{TText, TText, TBool, &CodeGen::binopEqualText} ,
						});
		case TK_DIFFERENT:
			return binopImpl(node, { 
					{TInt, TInt, TBool, &CodeGen::binopDifferentInt} ,
					{TBool, TBool, TBool, &CodeGen::binopDifferentBool} ,
					{TRel, TRel, TBool, &CodeGen::binopDifferentRel} ,
					{TTup, TTup, TBool, &CodeGen::binopDifferentTup} ,
					{TText, TText, TBool, &CodeGen::binopDifferentText} ,
						});
		case TK_AND:
			return binopImpl(node, { {TBool, TBool, TBool, &CodeGen::binopAndBool} });
		case TK_OR:
			return binopImpl(node, { {TBool, TBool, TBool, &CodeGen::binopOrBool} });
		case TK_QUESTION:
			return binopImpl(node, { {TRel, TFunc, TRel, &CodeGen::binopSelectRel} });
		case TK_OPEXTEND:
			return binopImpl(node, { {TTup, TTup, TTup, &CodeGen::binopExtendTup} });
		default: 
			ICE("Binop not implemented", node->opToken.id);
		}
	}
	
	LLVMVal visit(std::shared_ptr<SequenceExp> node) {
		::Type t=TInvalid;
		LLVMVal x;
		for(auto n: node->sequence) {
			disown(x, t);
			x = visitNode(n);
			t=n->type;
		}
		//TODO if I am not outer walk over all sequences again
		//to abandon any assignments
		return x;
	}
	
	llvm::Function * translate(NodePtr ast) override {
		size_t id=uid++;
		llvm::FunctionType * ft = FunctionType::get(llvm::Type::getVoidTy(getGlobalContext()), {}, false);
		std::stringstream ss;
		ss << "INNER_" << id;
		Function * function = Function::Create(ft, Function::ExternalLinkage, ss.str(), module);
		BasicBlock * block = BasicBlock::Create(getGlobalContext(), "entry", function);
		builder.SetInsertPoint(block);
		LLVMVal val=visitNode(ast);
		disown(val, ast->type);
		builder.CreateRetVoid();
		finishFunction(function);
		return function ;
	}
};

} //nameless namespace

namespace rasmus {
namespace frontend {

std::shared_ptr<LLVMCodeGen> makeLlvmCodeGen(
	std::shared_ptr<Error> error, std::shared_ptr<Code> code, llvm::Module * module,
	bool dumpRawFunctions, bool dumpOptFunctions) {
	return std::make_shared<CodeGen>(error, code, module, dumpRawFunctions, dumpOptFunctions);
} 

} //namespace frontend
} //namespace rasmus
