//===-------------------------- RangeAnalysis.cpp -------------------------===//
//===-----Performs the Range analysis of the variables of the function-----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file contains a pass that performs range analysis. The objective of
// the range analysis is to map each integer variable in the program to the
// range of possible values that it might assume through out the program
// execution. Ideally this range should be as constrained as possible, so that
// an optimizing compiler could learn more information about each variable.
// However, the range analysis must be conservative, that it, it will only
// constraint the range of a variable if it can prove that it is safe to do so.
// As an example, consider the program:
//
// i = read();
// if (i < 10) {
//   print (i + 1);
// else {
//   print(i - 1);
// }
//
// In this program we know, from the conditional test, that the value of i in
// the true side of the branch is in the range [-INF, 9], and in the false side
// is in the range [10, +INF].
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_RANGEANALYSIS_RANGEANALYSIS_H_
#define LLVM_TRANSFORMS_RANGEANALYSIS_RANGEANALYSIS_H_

#include "llvm/Function.h"
#include "llvm/Pass.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ConstantRange.h"
//#include "llvm/Transforms/Utils/SSI.h"
#include <deque>
#include <stack>
#include <set>


namespace llvm {

class RangeAnalysis: public FunctionPass {

public:
	static char ID; // Pass identification, replacement for typeid
	RangeAnalysis() : FunctionPass(ID) {}
	void getAnalysisUsage(AnalysisUsage &AU) const;
	bool runOnFunction(Function &F);

private:
	/// Gets the maximum bit width of the operands in
	/// the instructions of the function.
	/// This function is necessary because the class APInt only supports
	/// binary operations on operands that have the same number of bits; thus,
	/// all the APInts that we allocate to process the function will have the
	/// maximum bit size.
	/// The complexity of this function is linear on the number of operands
	/// used in the function.
	void getMaxBitWidth(const Function& F);

  /// Iterates through all BasicBlocks. If the Terminator Instruction
	/// uses an Comparator Instruction, all operands of this comparator
	/// are sent to be transformed to e-SSA. Only Instruction operands are
	/// transformed.
	void createESSA(Function &F);

}; // end of class RangeAnalysis


/// In our range analysis pass we have to perform operations on ranges all the
/// time. LLVM has a class to perform operations on ranges: the class
/// Range. However, the class Range doesn't serve very well
/// for our purposes because we need to perform operations with big numbers
/// (MIN_INT, MAX_INT) a lot of times, without allow these numbers wrap around.
/// And the class Range allows that. So, I'm writing this class to
/// perform operations on ranges, considering these big numbers and without
/// allow them wrap around.
/// The interface of this class is very similar to LLVM's ConstantRange class.
/// TODO: probably, a better idea would be perform our range analysis
/// considering the ranges symbolically, letting them wrap around, 
/// as ConstantRange considers, but it would require big changes 
/// in our algorithm.
class Range {
private:
  APInt l;    // The lower bound of the range.
  APInt u;    // The upper bound of the range.
  bool isEmpty;

public:
  Range();

  Range(APInt lb, APInt ub, bool isEmpty);

  ~Range();

  inline APInt getLower() const {return l;}

  inline APInt getUpper() const {return u;}

  inline void setLower(const APInt& newl) {this->l = newl;}

  inline void setUpper(const APInt& newu) {this->u = newu;}

  inline void setEmptySet(bool isEmptySet) {this->isEmpty = isEmptySet;}

  inline bool isEmptySet() const {return isEmpty;}

  bool isMaxRange() const;

  void print(raw_ostream& OS) const;

  Range add(const Range& other);

  Range sub(const Range& other);

  Range mul(const Range& other);

  Range udiv(const Range& other);

  Range sdiv(const Range& other);

  Range urem(const Range& other);

  Range srem(const Range& other);

  Range shl(const Range& other);

  Range lshr(const Range& other);

  Range ashr(const Range& other);

  Range And(const Range& other);

  Range Or(const Range& other);

  Range Xor(const Range& other);

  Range truncate(unsigned bitwidht) const;

  Range sextOrTrunc(unsigned bitwidht) const;

  Range zextOrTrunc(unsigned bitwidht) const;

  Range intersectWith(const Range& other) const;

  Range unionWith(const Range& other) const;

  bool operator==(const Range& other) const;

  bool operator!=(const Range& other) const;

};


/// This class represents a program variable.
class VarNode {
private:
	// The program variable which is represented.
	const Value* V;
	// A Range associated to the variable, that is,
	// its interval inferred by the analysis.
	Range interval;

public:
	/// The ctor.
	VarNode(const Value* V);

	/// The dtor.
	~VarNode();

	/// Initializes the value of the node.
	void init();

	/// Returns the range of the variable represented by this node.
  inline Range getRange() const {return interval;}

  /// Returns the variable represented by this node.
  inline const Value *getValue() const {return V;}

  /// Changes the status of the variable represented by this node.
  inline void setRange(const Range& newInterval) {
 		this->interval = newInterval;
  }

  /// Pretty print.
  void print(raw_ostream& OS) const;
};


enum IntervalId {
	BasicIntervalId,
	SymbIntervalId
};

/// This class represents a basic interval of values. This class could inherit
/// from LLVM's Range class, since it *is a Range*. However,
/// LLVM's Range class doesn't have a virtual constructor =/.
class BasicInterval {
private:
	Range range;
public:
	/// The ctor
	BasicInterval(const Range& range);

	/// The ctor
	BasicInterval(const APInt& l, const APInt& u);

	/// The ctor
	BasicInterval();

	/// The dtor
	virtual ~BasicInterval(); // This is a base class.

	// Methods for RTTI
	virtual IntervalId getValueId() const {return BasicIntervalId;}
	
	static inline bool classof(BasicInterval const *) {return true;}

	/// Returns the range of this interval.
	inline const Range& getRange() const {return this->range;}

	/// Sets the range of this interval to another range.
	inline void setRange(const Range& newRange) {
		this->range = newRange;
	}

  /// Pretty print.
  virtual void print(raw_ostream& OS) const;
};

/// This is an interval that contains a symbolic limit, which is
/// given by the bounds of a program name, e.g.: [-inf, ub(b) + 1].
class SymbInterval : public BasicInterval {
private:
	// The bound. It is a node which limits the interval of this range.
	const Value* bound;

	// The predicate of the operation in which this interval takes part.
	// It is useful to know how we can constrain this interval
	// after we fix the intersects.
	CmpInst::Predicate pred;

public:
	// The ctor.
	SymbInterval(const Range& range, const Value* bound, CmpInst::Predicate pred);

	/// The dtor.
	~SymbInterval();	
	
	// Methods for RTTI
	virtual IntervalId getValueId() const {return SymbIntervalId;}
	
	static inline bool classof(SymbInterval const *) {return true;}

	static inline bool classof(BasicInterval const *BI) {
		return BI->getValueId() == SymbIntervalId;
	}

	/// Returns the opcode of the operation that create this interval.
	inline CmpInst::Predicate getOperation() const {return this->pred;}

	/// Returns the node which is the bound of this interval.
	const Value* getBound() const {return this->bound;}

	/// Replace symbolic intervals with hard-wired constants.
	Range fixIntersects(VarNode* bound, VarNode* sink);

  /// Prints the content of the interval.
  void print(raw_ostream& OS) const;
};


enum OperationId {
	UnaryOpId,
	BinaryOpId,
	PhiOpId,
	ControlDepId
};

/// This class represents a generic operation in our analysis.
class BasicOp {
private:
	// We do not want people creating objects of this class.
	void operator=(const BasicOp&);

  BasicOp(const BasicOp&);

	// The range of the operation. Each operation has a range associated to it.
	// This range is obtained by inspecting the branches in the source program
	// and extracting its condition and intervals.
	BasicInterval* intersect;

	// The target of the operation, that is, the node which
	// will store the result of the operation.
	VarNode* sink;

protected:
    /// We do not want people creating objects of this class,
    /// but we want to inherit from it.
    BasicOp(BasicInterval* intersect, VarNode* sink);

public:
  	/// The dtor. Its virtual because this is a base class.
    virtual ~BasicOp();
    
    // Methods for RTTI
    virtual OperationId getValueId() const = 0;
    
    static inline bool classof(BasicOp const *) {return true;}

    /// Given the input of the operation and the operation that will be
    /// performed, evaluates the result of the operation.
    virtual Range eval() const = 0;

    /// Replace symbolic intervals with hard-wired constants.
    void fixIntersects(VarNode* V);

    /// Returns the range of the operation.
    inline BasicInterval* getIntersect() const {return intersect;}

    /// Changes the interval of the operation.
    inline void setIntersect(const Range& newIntersect) {
    	this->intersect->setRange(newIntersect);
    }

    /// Returns the target of the operation, that is,
    /// where the result will be stored.
    inline const VarNode* getSink() const {return sink;}

    /// Returns the target of the operation, that is,
    /// where the result will be stored.
    inline VarNode* getSink() {return sink;}

    /// Prints the content of the operation.
    virtual void print(raw_ostream& OS) const = 0;
};


/// A constraint like sink = operation(source) \intersec [l, u]
class UnaryOp : public BasicOp {
private:
	// The source node of the operation.
	VarNode* source;

	// The opcode of the operation.
	unsigned int opcode;

	/// Computes the interval of the sink based on the interval of the sources,
	/// the operation and the interval associated to the operation.
	Range eval() const;

public:
	/// The ctor.
  UnaryOp(BasicInterval* intersect,
      		VarNode* sink,
      		VarNode* source,
      		unsigned int opcode);

  /// The dtor.
  ~UnaryOp();
	
	// Methods for RTTI
	virtual OperationId getValueId() const {return UnaryOpId;}
	
	static inline bool classof(UnaryOp const *) {return true;}

	static inline bool classof(BasicOp const *BO) {
		return BO->getValueId() == UnaryOpId || BO->getValueId() == PhiOpId;
	}

  /// Return the opcode of the operation.
  inline unsigned int getOpcode() const {return opcode;}

	/// Returns the source of the operation.
	VarNode *getSource() const {return source;}

  /// Prints the content of the operation. I didn't it an operator overload
  /// because I had problems to access the members of the class outside it.
  void print(raw_ostream& OS) const;
};

// Specific type of BasicOp used in Nuutila
class ControlDep : public BasicOp {
private:
	VarNode *source;
	
	Range eval() const;
	    
  void print(raw_ostream& OS) const;
	
public:
	ControlDep(VarNode* sink, VarNode *source);
	
	~ControlDep();
	
	// Methods for RTTI
	virtual OperationId getValueId() const {return ControlDepId;}
	
	static inline bool classof(ControlDep const *) {return true;}
	
  static inline bool classof(BasicOp const *BO) {
		return BO->getValueId() == ControlDepId;
	}
	
	/// Returns the source of the operation.
	VarNode *getSource() const {return source;}
};

/// A constraint like sink = phi(src1, src2, ..., srcN)
class PhiOp : public UnaryOp {
private:

	/// Computes the interval of the sink based on the interval of the sources,
	/// the operation and the interval associated to the operation.
	Range eval() const;

public:
	/// The ctor.
  PhiOp(BasicInterval* intersect,
      	  VarNode* sink,
      	  VarNode* source,
      	  unsigned int opcode);

	/// The dtor.
	~PhiOp();
	
	// Methods for RTTI
	virtual OperationId getValueId() const {return PhiOpId;}
	
	static inline bool classof(PhiOp const *) {
		return true;
	}

	static inline bool classof(UnaryOp const *UO) {
		return UO->getValueId() == PhiOpId;
	}

	static inline bool classof(BasicOp const *BO) {
		return BO->getValueId() == PhiOpId;
	}

  /// Prints the content of the operation. I didn't it an operator overload
  /// because I had problems to access the members of the class outside it.
  void print(raw_ostream& OS) const;
};


/// A constraint like sink = source1 operation source2 intersect [l, u].
class BinaryOp : public BasicOp {
private:
	// The first operand.
	VarNode* source1;

	// The second operand.
	VarNode* source2;

	// The opcode of the operation.
	unsigned int opcode;

	/// Computes the interval of the sink based on the interval of the sources,
	/// the operation and the interval associated to the operation.
	Range eval() const;

public:
	// The ctor.
    BinaryOp(BasicInterval* intersect,
        		 VarNode* sink,
        		 VarNode* source1,
        		 VarNode* source2,
        		 unsigned int opcode);

	/// The dtor.
	~BinaryOp();
	
	// Methods for RTTI
	virtual OperationId getValueId() const {return BinaryOpId;}
	
	static inline bool classof(BinaryOp const *) {
		return true;
	}

	static inline bool classof(BasicOp const *BO) {
		return BO->getValueId() == BinaryOpId;
	}

  /// Return the opcode of the operation.
  inline unsigned int getOpcode() const {return opcode;}

  /// Returns the first operand of this operation.
  inline VarNode *getSource1() const {return source1;}

  /// Returns the second operand of this operation.
  inline VarNode *getSource2() const {return source2;}

  /// Prints the content of the operation. I didn't it an operator overload
  /// because I had problems to access the members of the class outside it.
  void print(raw_ostream& OS) const;

};

/// This class is used to store the intersections that we get in the branches.
/// I decided to write it because I think it is better to have an objetc 
/// to store these information than create a lot of maps 
/// in the ConstraintGraph class.
class ValueBranchMap {
private:
	const Value* V;
	const BasicBlock* BBTrue;
	const BasicBlock* BBFalse;
	BasicInterval* ItvT;
	BasicInterval* ItvF;

public:
	ValueBranchMap(const Value* V,
		      		   const BasicBlock* BBTrue,
		      		   const BasicBlock* BBFalse,
		      		   BasicInterval* ItvT,
		      		   BasicInterval* ItvF);

	~ValueBranchMap();

  /// Get the "false side" of the branch
  inline const BasicBlock *getBBFalse() const {
		return BBFalse;
	}

  /// Get the "true side" of the branch
	inline const BasicBlock *getBBTrue() const {
		return BBTrue;
	}

  /// Get the interval associated to the true side of the branch
	inline BasicInterval *getItvT() const {
		return ItvT;
	}

  /// Get the interval associated to the false side of the branch
	inline BasicInterval *getItvF() const {
		return ItvF;
	}

  /// Get the value associated to the branch.
	inline const Value *getV() const {
		return V;
	}

  /// Change the interval associated to the true side of the branch
	inline void setItvT(BasicInterval *Itv) {
		this->ItvT = Itv;
	}

  /// Change the interval associated to the false side of the branch
	inline void setItvF(BasicInterval *Itv) {
		this->ItvF = Itv;
	}

};

// The VarNodes type.
typedef DenseMap<const Value*, VarNode*> VarNodes;

// The Operations type.
typedef SmallPtrSet<BasicOp*, 64> GenOprs;

// A map from variables to the operations where these variables are used.
typedef DenseMap<const Value*, SmallPtrSet<BasicOp*, 8> > UseMap;

// A map from variables to the operations where these 
// variables are present as bounds
typedef DenseMap<const Value*, SmallPtrSet<BasicOp*, 8> > SymbMap;

// A map from varnodes to the operation in which this variable is defined
typedef DenseMap<VarNode*, BasicOp*> DefMap;

typedef DenseMap<const Value*, ValueBranchMap> ValuesBranchMap;

/// This class represents our constraint graph. This graph is used to
/// perform all computations in our analysis.
class ConstraintGraph {
private:

	// The variables of the source program and the nodes which represent them.
	VarNodes* vars;

	// The operations of the source program and the nodes which represent them.
	GenOprs* oprs;

	// A map from variables to the operations where these variables are used.
	UseMap* useMap;
	
	// A map from variables to the operations where these 
  // variables are present as bounds
	SymbMap* symbMap;

  // This data structure is used to store intervals, basic blocks and intervals
  // obtained in the branches.
	ValuesBranchMap* valuesBranchMap;

	/// Adds a BinaryOp in the graph.
	void addBinaryOp(const Instruction* I);

	/// Adds a PhiOp in the graph.
	void addPhiOp(const PHINode* Phi);

	/// Takes an intruction and creates an operation.
	void buildOperations(const Instruction* I);

	void buildValueBranchMap(const Function& F);

  // Perform the widening and narrowing operations
	void update(std::set<const Value*>& actv, bool (*meet)(BasicOp* op));


	//void update(const UseMap &compUseMap,
  //	    SmallPtrSet<const Value*, 32>& actv, bool (*meet)(BasicOp* op));

public:

	/// I'm doing this because I want to use this analysis in an
	/// inter-procedural pass. So, I have to receive these data structures as
	// parameters.
	ConstraintGraph(VarNodes *varNodes, GenOprs *genOprs, UseMap *usemap,
        					ValuesBranchMap *valuesBranchMap);

	/// The dtor.
	~ConstraintGraph();

	/// Adds a VarNode in the graph.
	VarNode* addVarNode(const Value* V);

	/// Adds an UnaryOp to the graph.
	void addUnaryOp(VarNode* sink, VarNode* source);

	/// Adds an UnaryOp to the graph.
	void addUnaryOp(const Instruction* I);

	/// Iterates through all instructions in the function and builds the graph.
	void buildGraph(const Function& F);

	void buildSymbolicIntersectMap();

	UseMap buildUseMap(const SmallPtrSet<VarNode*, 32> &component);
	
	void propagateToNextSCC(const SmallPtrSet<VarNode*, 32> &component);

	/// Finds the intervals of the variables in the graph.
	void findIntervals(const Function& F); // FIXME: Remove the parameter.

	/// Releases the memory used by the graph.
	void clear();

	/// Prints the content of the graph in dot format. For more informations
	/// about the dot format, see: http://www.graphviz.org/pdf/dotguide.pdf
	void print(const Function& F, raw_ostream& OS) const;

	/// Allow easy printing of graphs from the debugger.
	void dump(const Function& F) const {print(F, dbgs()); dbgs() << '\n'; };
};


class Nuutila {
public:
	VarNodes *variables;
	int index;
	DenseMap<Value*, int> dfs;
	DenseMap<Value*, Value*> root;
	SmallPtrSet<Value*, 32> inComponent;
	DenseMap<Value*, SmallPtrSet<VarNode*, 32> > components;
	std::deque<Value*> worklist;
	
public:
	Nuutila(VarNodes *varNodes, UseMap *useMap, SymbMap *symbMap);
	void addControlDependenceEdges(SymbMap *symbMap, UseMap *useMap, VarNodes* vars);
	void delControlDependenceEdges(UseMap *useMap);
	void visit(Value *V, std::stack<Value*> &stack, UseMap *useMap);
	
	typedef std::deque<Value*>::reverse_iterator iterator;
	
	iterator begin() {return worklist.rbegin();}
	iterator end() {return worklist.rend();}
};


} // end namespace

#endif /* LLVM_TRANSFORMS_RANGEANALYSIS_RANGEANALYSIS_H_ */

