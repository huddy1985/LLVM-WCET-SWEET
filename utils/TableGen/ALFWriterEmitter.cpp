#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include "CodeGenRegisters.h"
#include "CodeGenTarget.h"
#include "CodeGenInstruction.h"
#include "CodeGenDAGPatterns.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"

#include <vector>

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "alfwriter-emitter"

namespace {
struct ALFConditionFlag {
	Record *TheDef;
	unsigned Bitposition;
	Record *Reg;

	ALFConditionFlag() : TheDef(nullptr), Bitposition(0), Reg(nullptr)
	{
	}

	ALFConditionFlag(Record *R) : TheDef(R)
	{
		assert(R && "Record is a nullptr!");
		Bitposition = R->getValueAsInt("Bitposition");
		Reg = R->getValueAsDef("Reg");
	}
	virtual ~ALFConditionFlag() { };
};
struct ALFWriter
{
	Record *TheDef;

	ALFConditionFlag Nflag;
	ALFConditionFlag Zflag;
	ALFConditionFlag Cflag;
	ALFConditionFlag Vflag;
public:
	ALFWriter() : TheDef(nullptr), Nflag(nullptr), Zflag(nullptr), Cflag(nullptr), Vflag(nullptr)
	{

	}

	ALFWriter(Record *R) : TheDef(R)
	{
		assert(R && "Record is a nullptr!");
		Nflag = R->getValueAsDef("Nflag");
		Zflag = R->getValueAsDef("Zflag");
		Cflag = R->getValueAsDef("Cflag");
		Vflag = R->getValueAsDef("Vflag");
	}
	virtual ~ALFWriter() { };

private:
	/* data */
};
} // end anonymous namespace

namespace {
struct ALFDAGOperator
{
public:
	TreePatternNode *Operator;
	string name;

	ALFDAGOperator(TreePatternNode *_op) : Operator(_op), name(_op->getOperator()->getName())
	{
	}
	virtual ~ALFDAGOperator () {};

private:
	/* data */
};
struct ALFDAGLeaf
{
public:
	TreePatternNode *leaf;
	unsigned MIindex;

	ALFDAGLeaf(TreePatternNode *_leaf, unsigned MIindex) : leaf(_leaf), MIindex(MIindex)
	{
	}
	virtual ~ALFDAGLeaf () { };

private:
	/* data */
};
struct ALFInstructionInfo
{
public:
	vector<ALFDAGOperator> operators;
	vector<ALFDAGLeaf> leafs;
	const CodeGenInstruction *I;
	CodeGenDAGPatterns &CGDP;

	ALFInstructionInfo(const CodeGenInstruction *_I, CodeGenDAGPatterns &_CGDP,
			vector<TreePatternNode*> _operators,
			vector<TreePatternNode*> _leafs,
			vector<int> indexesForMI)
		: I(_I), CGDP(_CGDP)
	{
		for (auto op : _operators) 
			operators.push_back(ALFDAGOperator(op));
		assert((indexesForMI.size() == _leafs.size()) && "MI indexes and leaf vectors have different sizes!");
		unsigned counter = 0;
		for (auto leaf : _leafs)  {
			leafs.push_back(ALFDAGLeaf(leaf, indexesForMI[counter]));
			counter++;
		}
	}
	virtual ~ALFInstructionInfo () { }

	bool hasPattern(vector<string> pattern)
	{
		// check if the operators have the given pattern.
		// e.g. (set (add ) ) , compare set and add
		if (!I)
			return false;
		auto I = operators.begin();
		if (I == operators.end())
			return false;
		for (auto s : pattern) {
			if (I == operators.end())
				break;
			if (s != I++->name)
				return false;
		}
		return true;
	}

	void printCaseComments(raw_ostream &O)
	{
			// Print some comments first for each instr
			const DAGInstruction &daginst = CGDP.getInstruction(I->TheDef);
			auto treepattern = daginst.getPattern();
			// print the full Pattern field
			if (treepattern) {
				O << "      // ";
				treepattern->print(O);
				O << "\n";
			}
			// print the operands of the MI
			O << "      //MI operands: ";
			for (unsigned i = 0, e = I->Operands.size(); i != e; ++i) {
				O << I->Operands[i].Name << " ";
			}
			O << "\n";
			// print the indexes of the $test items in the MI operands
			O << "      //Indexes of $.. in the MachineInstruction's operands: ";
			for (auto l : leafs) {
				O << to_string(l.MIindex) << " ";
			}
			O << "\n";
			// print the names of the DAG operators like set, st
			O << "      //operatorNames: ";
			for (auto op : operators) {
				O << op.name << " ";
			}
			O << "\n";
			// print complex leafs if any
			O << "      //complexleafs: ";
			for (auto l : leafs) {
				const ComplexPattern *cp = l.leaf->getComplexPatternInfo(CGDP);
				if (cp) {
					O << cp->getRecord()->getName() << " ";
				}
			}
			O << "\n";
	}

private:
	/* data */
};
}

namespace {
class Pattern2ALFMapping {
// variables
protected:
	shared_ptr<ALFInstructionInfo> info;
	const ALFWriter &writer;

// public functions
public:
	Pattern2ALFMapping(shared_ptr<ALFInstructionInfo> _info, const ALFWriter &_writer) : info(_info), writer(_writer)
	{
	}

	virtual ~Pattern2ALFMapping() { };

	virtual void printALFText(raw_ostream &O) = 0;
	virtual bool canRUN() = 0;

// functions
protected:
	void handleDefaultOperand(raw_ostream &O, string returnVariable, ALFDAGLeaf &leaf)
	{
		// ASSUMPTIONS: mcop exists, as well as ctx, TRI

		// check if ComplexPattern
		if (auto cp = leaf.leaf->getComplexPatternInfo(info->CGDP)) {
			Record *cpR = cp->getRecord(); 
			if (!cpR->getValueAsString("ALFCustomMethod").empty()) {
				O << "      " << returnVariable << " = " << cpR->getValueAsString("ALFCustomMethod") << "(MI, alfbb, ctx, label);"<< "\n";
				return; //done!
			} else {
				// complexnode not handled, add an undefined
				O << "        " << returnVariable << " = ctx->undefined(32);\n";
			}
		} else {
			// else check on MI operand what to do
			O << "      if (MI.getOperand(" << leaf.MIindex << ").isReg()) {\n";
			O << "        " << returnVariable << " = ctx->load(32, TRI->getName(MI.getOperand(" << leaf.MIindex << ").getReg()));\n";
			O << "      } else if (MI.getOperand(" << leaf.MIindex << ").isImm()) {\n";
			O << "        " << returnVariable << " = ctx->dec_unsigned(32, MI.getOperand(" << leaf.MIindex << ").getImm());\n";
			O << "      } else {\n";
			O << "        " << returnVariable << " = ctx->undefined(32);\n";
			O << "      }\n";
		}
	}
};
} // end anonymous namespace

namespace {
class SETPattern : public Pattern2ALFMapping {
public:
	SETPattern(shared_ptr<ALFInstructionInfo> _info, const ALFWriter &_writer) : Pattern2ALFMapping(_info, _writer)
	{ }

	virtual ~SETPattern() { };

	void printALFText(raw_ostream &O)
	{
		if (!canRUN())
			return;

		if (info->hasPattern({"set"})) { 
			O << "      ALFStatement *statement = 0;\n";
			O << "      std::string targetReg;\n";
			O << "      SExpr *op1, *op2, *output, *output_carry;\n";
			O << "      SExpr *stor = 0;\n";
			O << "      op1 = op2 = output = output_carry = 0;\n";


			// one argument
			if (info->hasPattern({"set", "imm"})) {
				// assume the first index is a register, and assume the second index is an immediate
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				handleDefaultOperand(O, "op1", info->leafs[1]);

				O << "      output = op1;\n";

				O << "      stor = ctx->store(ctx->address(targetReg), op1);\n";

			// two arguments
			} else if (info->hasPattern({"set", "add"})) {
				// assume the first index is a register,
				// and assume the second and third index are registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				handleDefaultOperand(O, "op1", info->leafs[1]);
				handleDefaultOperand(O, "op2", info->leafs[2]);

				O << "      output = ctx->add(32, op1, op2, 0);\n";
				O << "      output_carry = ctx->c_add(32, op1, op2, 0);\n";

				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";

			} else if (info->hasPattern({"set", "sub"})) {
				// assume the first index is a register,
				// and assume the second and third index are registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				handleDefaultOperand(O, "op1", info->leafs[1]);
				handleDefaultOperand(O, "op2", info->leafs[2]);

				O << "      output = ctx->sub(32, op1, op2, 1);\n";
				O << "      output_carry = ctx->c_sub(32, op1, op2, 1);\n";

				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";

			} else if (info->hasPattern({"set", "ld"})) {
				// assume the first index is a target register,
				// and assume the second index is registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				handleDefaultOperand(O, "op1", info->leafs[1]);

				O << "      SExpr *load = ctx->load(32, op1);\n";
				O << "      stor = ctx->store(ctx->address(targetReg), load);\n";
			} else if (info->hasPattern({"set", "shl"})) {
				// assume the first index is a target register,
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				// and assume the second index is registers or immediates
				handleDefaultOperand(O, "op1", info->leafs[1]);
				// and assume the third index is registers or immediates
				handleDefaultOperand(O, "op2", info->leafs[2]);

				O << "      output = ctx->l_shift(32, 32, op1, op2);\n";
				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";
			} else if (info->hasPattern({"set", "sra"})) {
				// assume the first index is a target register,
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				// and assume the second index is registers or immediates
				handleDefaultOperand(O, "op1", info->leafs[1]);
				// and assume the third index is registers or immediates
				handleDefaultOperand(O, "op2", info->leafs[2]);

				O << "      output = ctx->r_shift_a(32, 32, op1, op2);\n";
				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";
			} else if (info->hasPattern({"set", "mul"})) {
				// assume the first index is a target register,
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				// and assume the second index is registers or immediates
				handleDefaultOperand(O, "op1", info->leafs[1]);
				// and assume the third index is registers or immediates
				handleDefaultOperand(O, "op2", info->leafs[2]);

				O << "      SExpr *temp = ctx->s_mul(32, 32, op1, op2);\n";
				O << "      output = ctx->select(64, 0, 31, temp);\n";
				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";
			} else if (info->hasPattern({"set", "xor"})) {
				// assume the first index is a target register,
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				// and assume the second index is registers or immediates
				handleDefaultOperand(O, "op1", info->leafs[1]);

				// if there is no 3th argument we asssume this is (set not .. ), or ( set xor .. -1 )
				if (info->leafs.size() != 3) {
					O << "      op2 = ctx->dec_signed(32, -1);\n";
				} else {
					handleDefaultOperand(O, "op2", info->leafs[2]);
				}

				O << "      output = ctx->xor_(32, op1, op2);\n";
				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";
			} else if (info->hasPattern({"set", "adde"})) {
				// assume the first index is a register,
				// and assume the second and third index are registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				handleDefaultOperand(O, "op1", info->leafs[1]);
				handleDefaultOperand(O, "op2", info->leafs[2]);

				unsigned CarrybitPos = writer.Cflag.Bitposition;
				string CarryRegName = writer.Cflag.Reg->getName();
				O << "      SExpr *loadC = ctx->select(32, " << CarrybitPos <<  ", " << CarrybitPos << ", ctx->load(32, \"" << CarryRegName << "\"));\n";
				O << "      output = ctx->add2(32, op1, op2, loadC);\n";
				O << "      output_carry = ctx->c_add2(32, op1, op2, loadC);\n";

				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";

			} else if (info->hasPattern({"set", "or"})) {
				// assume the first index is a register,
				// and assume the second and third index are registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				handleDefaultOperand(O, "op1", info->leafs[1]);
				handleDefaultOperand(O, "op2", info->leafs[2]);

				O << "      output = ctx->or_(32, op1, op2);\n";

				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";
			} else if (info->hasPattern({"set", "and"})) {
				// assume the first index is a register,
				// and assume the second and third index are registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << info->leafs[0].MIindex << ").getReg());\n";

				handleDefaultOperand(O, "op1", info->leafs[1]);
				handleDefaultOperand(O, "op2", info->leafs[2]);

				O << "      output = ctx->and_(32, op1, op2);\n";

				O << "      stor = ctx->store(ctx->address(targetReg), output);\n";
			} else {
				O << "      goto default_label;\n";
			}

			/* "bool " << TargetName << "ALFWriter::printInstructionALF(const MachineInstr &MI); */
			
			O << "      statement = alfbb.addStatement(label, comment, stor);\n";
			O << "      SExpr *store_condflags;\n";
			O << "      if (shouldSetCondFlags(MI)" << ") {\n";
			O << "        store_condflags = calcNZCV(ctx, op1, op2, output, output_carry);\n";
			O << "        if (store_condflags) {\n";
			O << "          alfbb.addStatement(label + \"_NZCV\", comment, store_condflags);\n";
			O << "        }\n";
			O << "      }\n";
		}
	}

	bool canRUN()
	{
		if (info->hasPattern({"set", "imm"})) { 
			return true;
		} else if (info->hasPattern({"set", "add"})) { 
			return true;
		} else if (info->hasPattern({"set", "sub"}) && info->leafs.size() >= 3) { 
			return true;
		} else if (info->hasPattern({"set", "ld"})) { 
			return true;
		} else if (info->hasPattern({"set", "shl"})) { 
			return true;
		} else if (info->hasPattern({"set", "sra"}) && info->leafs.size() >= 3) { 
			return true;
		} else if (info->hasPattern({"set", "mul"})) { 
			return true;
		} else if (info->hasPattern({"set", "xor"})) { 
			return true;
		} else if (info->hasPattern({"set", "adde"})) { 
			return true;
		} else if (info->hasPattern({"set", "or"})) { 
			return true;
		} else if (info->hasPattern({"set", "and"}) && info->leafs.size() >= 3) { 
			return true;
		}
		return false;
	}

private:
	/* data */
};
} // end anonymous namespace

namespace {
class STPattern : public Pattern2ALFMapping {
public:
	STPattern(shared_ptr<ALFInstructionInfo> _info, const ALFWriter &_writer) : Pattern2ALFMapping(_info, _writer) { }

	virtual ~STPattern() { }

	void printALFText(raw_ostream &O)
	{
		if (!canRUN())
			return;
		// assume the first 
		O << "      ALFStatement *statement;\n";
		O << "      SExpr *value, *address;\n";

		handleDefaultOperand(O, "value", info->leafs[0]);

		handleDefaultOperand(O, "address", info->leafs[1]);

		/* O << "      SExpr *bytes_to_bits = ctx->select(64, 0, 31, ctx->u_mul(32, 32, byteaddress, ctx->dec_unsigned(32, 8)));\n"; */

		O << "      SExpr *stor = ctx->store(address, value);\n";
		O << "      statement = alfbb.addStatement(label, comment, stor);\n";
	}

	bool canRUN()
	{
		if (info->hasPattern({"st"}))
			return true;
		return false;
	}

private:
	/* data */
};
} // end anonymous namespace

namespace {
class BRPattern : public Pattern2ALFMapping {
public:
	BRPattern(shared_ptr<ALFInstructionInfo> _info, const ALFWriter &_writer) : Pattern2ALFMapping(_info, _writer) { }

	virtual ~BRPattern() { }

	void printALFText(raw_ostream &O)
	{
		if (!canRUN())
			return;
		O << "      ALFStatement *statement;\n";
		// br has one operand, a target BB
		//
		O << "      auto jumpBB = MI.getOperand(0).getMBB();\n";
	    O << "      auto jumpFunction = jumpBB->getParent();\n";
        O << "      string jumpLabel = string(jumpFunction->getName()) + \":BB#\" + std::to_string(jumpBB->getNumber());\n";
		O << "      SExpr *jump = ctx->jump(jumpLabel);\n";
		O << "      alfbb.addStatement(label, comment, jump);\n";
	}

	bool canRUN()
	{
		if (info->hasPattern({"br"}))
			return true;
		return false;
	}

private:
	/* data */
};
} // end anonymous namespace

namespace {
class ALFWriterEmitter {
	RecordKeeper &Records;
	CodeGenTarget Target;
	vector<const CodeGenInstruction *> NumberedInstructions;
	CodeGenDAGPatterns CGDP;
	ALFWriter alfwriter;


public:
	ALFWriterEmitter(RecordKeeper &R) : Records(R), Target(R), CGDP(Records), alfwriter(ALFWriter(Target.getALFWriter()))
	{
	}

	void run(raw_ostream &O)
	{
		outputCortexM3InstrsTEST();
		/* outputCortexM3AssemblerPredicatesTEST(O); */
		/* outputCortexM3PredicatesTEST(O); */

		/* outputCortexM0AssemblerPredicatesTEST(O); */
		outputCortexM0InstrsTEST();

		/* outputALFRegisterDefinitionsTEST(O); */
		/* outputALFInstrMapping(O); */


		O << "#ifdef GET_ALF_HEADERS\n";
		O << "#undef GET_ALF_HEADERS\n";
		O << 
			"static SExpr *calcNZCV(ALFContext *ctx,\n"
			"       SExpr *op1, SExpr *op2,\n"
			"       SExpr *output, SExpr *output_carry);\n";

		O << "#else\n";

		outputRegDefALF(O);
		outputcalc_NZCV(O);

		outputPrintInstructionALF(O);
		O << "#endif // GET_ALF_HEADERS\n";
	}

private:
	void findTreePatternLeafs(TreePatternNode *n, std::vector<TreePatternNode *> &output)
	{
		if (!n)
			return;

		if (!n->getName().empty())
			output.push_back(n);

		if (!n->isLeaf()) {
			if (n->getNumChildren() != 0) {
				for (unsigned i = 0, e = n->getNumChildren(); i != e; ++i) {
					findTreePatternLeafs(n->getChild(i), output);
				}
			}
		}
	}

	void findTreePatternOperators(TreePatternNode *n, std::vector<TreePatternNode *> &output)
	{
		if (!n)
			return;

		if (!n->isLeaf()) {
			if (n->getOperator())
				output.push_back(n);

			if (n->getNumChildren() != 0) {
				for (unsigned i = 0, e = n->getNumChildren(); i != e; ++i) {
					findTreePatternOperators(n->getChild(i), output);
				}
			}
		}
	}

	shared_ptr<ALFInstructionInfo> buildALFInstructionInfo(const CodeGenInstruction *I)
	{
		vector<int> indexesForMI;
		vector<TreePatternNode*> operators;
		vector<TreePatternNode*> leafs;
		// find the Machineinstr. indexes of the $named values of the Pattern field.
		// example:
		//
		// dag OutOperandList = (outs tGPR:$Rd, s_cc_out:$s);
		// dag InOperandList = (ins tGPR:$Rm, imm0_7:$imm3, pred:$p);
		// list<dag> Pattern = [(set tGPR:$Rd, (add tGPR:$Rm, imm0_7:$imm3))];
		//
		// The machineinstr will have the operands:
		// 	tGPR:$Rd, s_cc_out:$s, tGPR:$Rm, imm0_7:$imm3, pred:$p
		//
		// output: 
		// Rd:0
		// Rm:2
		// imm0_7:3
		//
		const DAGInstruction &daginst = CGDP.getInstruction(I->TheDef);
		const std::string &InstName = I->TheDef->getName().str();
		auto treepattern = daginst.getPattern();

		// opsMap[index_of_pattern_$operand] = index_of_machineinstr_operand
		if (treepattern) {
			auto tpn = treepattern->getOnlyTree();
			if (tpn) {
				std::vector<string> leafNames;
				findTreePatternLeafs(tpn, leafs);
				for (auto tpn : leafs) {
					leafNames.push_back(tpn->getName());
				}

				findTreePatternOperators(tpn, operators);
				for (unsigned i = 0, e = I->Operands.size(); i != e; ++i) {
					string op = I->Operands[i].Name;
				}

				// loop through the full set of operands, find the index of the pattern name
				for (unsigned j = 0; j < leafNames.size(); j++) {
					for (unsigned i = 0, e = I->Operands.size(); i != e; ++i) {
						string op = I->Operands[i].Name;
						if (op == leafNames[j]) {
							indexesForMI.push_back(i);
						}
					}
				}
			}
		}
		return make_shared<ALFInstructionInfo>(I, CGDP, operators, leafs, indexesForMI);
	}

	void outputRegDefALF(raw_ostream &O)
	{
		const std::string &TargetName = Target.getName();

		O <<
			"/// regDefALF - This method is automatically generated by tablegen\n"
			"/// from the instruction set description.\n"
			"void " << TargetName << "ALFWriter::regDefALF(ALFBuilder &b) {\n";

		CodeGenRegBank &RegBank = Target.getRegBank();
		RegBank.computeDerivedInfo();

		for (unsigned i = 0, e = RegBank.getNumNativeRegUnits(); i != e; ++i) {
			ArrayRef<const CodeGenRegister*> Roots = RegBank.getRegUnit(i).getRoots();
			assert(!Roots.empty() && "All regunits must have a root register.");
			assert(Roots.size() <= 2 && "More than two roots not supported yet.");

			const CodeGenRegister *Reg = Roots.front();

			// get the first one, although there could be two root regs.. (?)
			const StringRef &regName = Roots.front()->getName();
			int64_t size = 0;

			// determine offset of the register classes of Reg:
			for (const auto &RC : RegBank.getRegClasses()) {
				if (RC.getDef() && RC.contains(Reg)) {
					size = std::max(size, RC.getDef()->getValueAsInt("Alignment"));
				}
			}

			// skip is zero regwidth
			if (size == 0)
				continue;

			O << "  b.addFrame(\"" << regName << "\", " << size << ", InternalFrame);\n";
		}
		// add frame representing the memory
		O << "  b.addInfiniteFrame(\"mem\", InternalFrame);\n";

		O << "}\n\n";
	}

	void outputcalc_NZCV(raw_ostream &O) 
	{
		const std::string &TargetName = Target.getName();

/* const MachineInstr &MI, ALFStatementGroup &alfbb, ALFContext *ctx, string label */

		O <<
			"/// regDefALF - This method is automatically generated by tablegen\n"
			"/// from the instruction set description.\n"
			"static SExpr *calcNZCV(ALFContext *ctx,\n"
			"       SExpr *op1, SExpr *op2,\n"
			"       SExpr *output, SExpr *output_carry) {\n"
			"  if (output == nullptr)\nreturn nullptr;\n"
			"  if (ctx == nullptr)\nreturn nullptr;\n";

		string VflagRegName = alfwriter.Vflag.Reg->getName();
		unsigned VflagBitPos = alfwriter.Vflag.Bitposition;

		O << "  SExpr *V = ctx->select(32, " << VflagBitPos << ", " << VflagBitPos << ", ctx->load(32, \"" << VflagRegName << "\"));\n";
		O << "  if (op1 && op2) {\n";
		// overflow if pos - neg = pos
		// overflow if neg - pos = neg
		O << "    SExpr *arg1_pos = ctx->s_ge(32, op1, ctx->dec_unsigned(32, 0));\n";
		O << "    SExpr *arg2_pos = ctx->s_ge(32, op2, ctx->dec_unsigned(32, 0));\n";
		O << "    SExpr *output_pos = ctx->s_ge(32, output, ctx->dec_unsigned(32, 0));\n";
		O << "    SExpr *arg1_neg = ctx->s_lt(32, op1, ctx->dec_unsigned(32, 0));\n";
		O << "    SExpr *arg2_neg = ctx->s_lt(32, op2, ctx->dec_unsigned(32, 0));\n";
		O << "    SExpr *output_neg = ctx->s_lt(32, output, ctx->dec_unsigned(32, 0));\n";
		O << "    V = ctx->or_(1,\n";
		O << "      ctx->and_(1, ctx->and_(1, arg1_pos, arg2_neg), output_pos),\n";
		O << "      ctx->and_(1, ctx->and_(1, arg1_neg, arg2_pos), output_neg)\n";
		O << "    );\n";
		O << "  }\n";

		string CflagRegName = alfwriter.Cflag.Reg->getName();
		unsigned CflagBitPos = alfwriter.Cflag.Bitposition;

		O << "  if (output_carry == nullptr) {\n";
		O << "    output_carry = ctx->select(32, " << CflagBitPos << ", " << CflagBitPos << ", ctx->load(32, \"" << CflagRegName << "\"));\n";
		O << "  }\n";

		O << "  SExpr *expr_nzcv = ctx->conc(4, 28, \n";
		O << "    ctx->conc(2, 2, \n";
		O << "      ctx->conc(1, 1, \n";
		O << "        ctx->s_lt(32, output, ctx->dec_unsigned(32, 0)),\n";
		O << "        ctx->eq(32, output, ctx->dec_unsigned(32, 0))\n";
		O << "      ),\n";
		O << "      ctx->conc(1, 1,\n"; 
		O << "        output_carry,\n";
		O << "        V\n";
		O << "        )\n";
		O << "      ),\n";
		O << "      ctx->dec_unsigned(28, 0)\n";
		O << "  );\n";
		O << "  return ctx->store(ctx->address(\"CPSR\"), expr_nzcv);\n";

		// TODO what flags, what regs
		// 	==> tablegen

		O << "}\n\n";
	}

	bool make_case(raw_ostream &O, shared_ptr<ALFInstructionInfo> info)
	{
		// check for some is* flags in th CGI
		// if isReturn is set make a return statement
		if (info->I->isReturn) {
			O << "      alfbb.addStatement(MI.getParent()->getParent()->getName() + string(\":debugmarker\"), \"marker for reading values at the end\", ctx->null());\n";
			O << "      alfbb.addStatement(label, comment, ctx->ret());\n";
			return true; // stop here
		}

		vector<shared_ptr<Pattern2ALFMapping>> ALFmappings =
		{ 
			std::make_shared<SETPattern>(info, alfwriter),
			std::make_shared<STPattern>(info, alfwriter),
			std::make_shared<BRPattern>(info, alfwriter),
		};

		for (auto alfm : ALFmappings) {
			if (alfm->canRUN()) {
				alfm->printALFText(O);
				return true;
			}
		}

		// hmm do we have a custom SDNode operator function ? (ALFCustomMethod)
		for (auto alfop : info->operators) {
			Record *Op = alfop.Operator->getOperator();
			if (Op) {
				if (Op->isSubClassOf("SDNode") && !Op->getValueAsString("ALFCustomMethod").empty()) {
					O << "      " << Op->getValueAsString("ALFCustomMethod") << "(MI, alfbb, ctx, label);"<< "\n";
					return true;
				}
			}
		}

		return false;
	}

	void outputPrintInstructionALF(raw_ostream &O)
	{
		const std::string &TargetName = Target.getName();

		// Get the instruction numbering.
		NumberedInstructions = Target.getInstructionsByEnumValue();

		O <<
			"/// printInstructionALF - This method is automatically generated by tablegen\n"
			"/// from the instruction set description.\n"
			"void " << TargetName << "ALFWriter::printInstructionALF(const MachineInstr &MI, ALFStatementGroup &alfbb, ALFContext *ctx, string label) {\n";

		O << "  const unsigned opcode = MI.getOpcode();\n";
		O << "  const TargetInstrInfo *TII = MI.getParent()->getParent()->getSubtarget().getInstrInfo();\n";
		O << "  const TargetRegisterInfo *TRI = MI.getParent()->getParent()->getSubtarget().getRegisterInfo();\n";
		// user later in the code for custommethodafterset

		O << "  string tmp;\n";
		O << "  raw_string_ostream comment_ss(tmp);\n";
		O << "  MI.print(comment_ss);\n";
		O << "  string comment = comment_ss.str();\n";

	  	O << "  switch (opcode) {\n";
		for (unsigned i = 0, e = NumberedInstructions.size(); i != e; ++i) {
			const CodeGenInstruction *I = NumberedInstructions[i];

			const std::string &InstName = I->TheDef->getName().str();
			O << "    case " << I->Namespace << "::" << InstName << ": {\n";

			// collect information about the Pattern field: 
			// operators (set, add.. ),  leafs ( $Rn, $Rm)
			// and indexes of the leafs in the operands of the MI

			shared_ptr<ALFInstructionInfo> info = buildALFInstructionInfo(I);

			// print some comments for this instruction
			info->printCaseComments(O);
			// after printing comments, generate the code for each case

			// call the special function if it is not empty
			Record *R = I->TheDef; 
			if (!R->getValueAsString("ALFCustomMethod").empty()) {
				O << "      " << R->getValueAsString("ALFCustomMethod") << "(MI, alfbb, ctx, label);\n";
			} else if (!info->operators.empty() && // else try to do something with operators
					make_case(O, info)) {
			} else { // else jump to the end
				O << "      goto default_label;\n";
			}

			O << "      break;\n";
			O << "    }\n";
		}

		// Default case: unhandled opcode
		O << "    default: {\n";
		O << "      default_label:\n";
		O << "        alfbb.addStatement(label, comment, ctx->null());\n";
		O << "    }\n";
		O << "  }\n";

		O << "}\n\n";
	}
















	// OLD


	void outputALFRegisterDefinitionsTEST(raw_ostream &O)
	{
		CodeGenRegBank &RegBank = Target.getRegBank();
		RegBank.computeDerivedInfo();

		for (unsigned i = 0, e = RegBank.getNumNativeRegUnits(); i != e; ++i) {
			ArrayRef<const CodeGenRegister*> Roots = RegBank.getRegUnit(i).getRoots();
			assert(!Roots.empty() && "All regunits must have a root register.");
			assert(Roots.size() <= 2 && "More than two roots not supported yet.");

			const CodeGenRegister *Reg = Roots.front();

			// get the first one, although there could be two root regs.. (?)
			const StringRef &regName = getQualifiedName(Roots.front()->TheDef);
			int64_t size = 0;

			// determine offset of the register classes of Reg:
			for (const auto &RC : RegBank.getRegClasses()) {
				if (RC.getDef() && RC.contains(Reg)) {
					size = std::max(size, RC.getDef()->getValueAsInt("Alignment"));
				}
			}

			// skip is zero regwidth
			if (size == 0)
				continue;

			O << regName << "  ";

			// print the register classes 
			if (RegBank.getRegClasses().empty())
				O << "\n";
			else {
				for (const auto &RC : RegBank.getRegClasses()) {
					if (RC.getDef() && RC.contains(Reg)) {
						O << RC.getName() << ", ";
					}
				}
				O << "\n";
			}
		}
	}

	void outputCortexM3InstrsTEST()
	{
		DEBUG(errs() << "Printing CortexM3 Instructions:\n");
		CodeGenTarget Target(Records);
		std::vector<Record*> Insts = Records.getAllDerivedDefinitions("Instruction");

		// derived by hand by looking at the cortex-m3 subtarget def
		std::vector<std::string> lookingFor {
			"HasV7",
				"HasV6T2",
				"HasV8MBaseline",
				"HasV6M",
				"HasV6",
				"HasV5TE",
				"HasV5T",
				"HasV4T",
				"HasV6K",
				"IsThumb",
				"IsThumb2",
				"HasDB",
				"HasDivide",
				"IsMClass",
		};

		// predicates with empty AssemblerCondString that can be skipped
		std::vector<std::string> canBeIgnored {
			"DontUseFusedMAC",
				"DontUseMovt",
				"DontUseNEONForFP",
				"DontUseNaClTrap",
				"DontUseVMOVSR",
				"GenExecuteOnly",
				"HasFastVDUP32",
				"HasFastVGETLNi32",
				"HasSlowVDUP32",
				"HasSlowVGETLNi32",
				"HasZCZ",
				"IsBE",
				"IsLE",
				"IsMachO",
				"IsNaCl",
				"IsNotMachO",
				"IsNotWindows",
				"IsThumb1Only",
				"IsWindows",
				"NoHonorSignDependentRounding",
				"NoV4T",
				"NoV6",
				"NoV6K",
				"NoV6T2",
				"NoVFP",
				"UseFPVMLx",
				"UseFusedMAC",
				"UseMovt",
				"UseMulOps",
				"UseNEONForFP",
				"UseVMOVSR",
		};

		// Construct all cases statement for each opcode
		for (std::vector<Record*>::iterator IC = Insts.begin(), EC = Insts.end();
				IC != EC; ++IC) {
			Record *R = *IC;
			if (R->getValueAsString("Namespace") == "TargetOpcode" ||
					R->getValueAsBit("isPseudo"))
				continue;

			const std::string &InstName = R->getName().str();
			const auto &preds = R->getValueAsListOfDefs( StringLiteral("Predicates") );


			bool allFound = true;
			for (const Record *pred : preds) {
				auto result = std::find(lookingFor.begin(), lookingFor.end(), pred->getName());

				/* check if pred is canBeIgnored */
				if (std::end(canBeIgnored) != std::find(canBeIgnored.begin(), canBeIgnored.end(), pred->getName()))
					continue;

				if (result != std::end(lookingFor)) {
					/* O << "  > " << pred->getName() << "\n"; */
					/* O << "     " << InstName << ": " << pred->getName() << "\n"; */
				} else 
					allFound = false;
			}

			if (allFound)
				DEBUG(errs() << InstName << "\n");

			/* O << "case " << InstName << ": {\n"; */

		}
	}

	void outputCortexM3PredicatesTEST(raw_ostream &O)
	{
		CodeGenTarget Target(Records);
		std::vector<Record*> Insts = Records.getAllDerivedDefinitions("Predicate");

		// Construct all cases statement for each opcode
		for (std::vector<Record*>::iterator IC = Insts.begin(), EC = Insts.end();
				IC != EC; ++IC) {
			Record *R = *IC;

			const std::string &InstName = R->getName().str();
			std::string AsmCondString = R->getValueAsString("AssemblerCondString");

			/* print all */ 
			/* O << InstName << ": " << (AsmCondString.empty() ? "empty" : AsmCondString) << "\n"; */
			/* print empty */ 
			if (AsmCondString.empty()) {
				O << InstName << "\n";
			}
		}
	}

	void outputCortexM3AssemblerPredicatesTEST(raw_ostream &O)
	{
		CodeGenTarget Target(Records);
		std::vector<Record*> Insts = Records.getAllDerivedDefinitions("AssemblerPredicate");
		std::vector<std::string> lookingFor {
			"ProcM3",
				"ARMv7m",
				"HasV7Ops",
				"HasV6T2Ops",
				"HasV8MBaseLineOps",
				"HasV6MOps",
				"HasV6Ops",
				"HasV5TEOps",
				"HasV5TOps",
				"HasV4TOps",
				"HasV6KOps",
				"FeatureThumb2",
				"FeatureNoARM",
				"ModeThumb",
				"FeatureDB",
				"FeatureHWDiv",
				"FeatureMClass",
		};

		// Construct all cases statement for each opcode
		for (std::vector<Record*>::iterator IC = Insts.begin(), EC = Insts.end();
				IC != EC; ++IC) {
			Record *R = *IC;

			const std::string &InstName = R->getName().str();
			std::string AsmCondString = R->getValueAsString("AssemblerCondString");

			// AsmCondString has syntax [!]F(,[!]F)*
			SmallVector<StringRef, 4> Ops;
			SplitString(AsmCondString, Ops, ",");
			assert(!Ops.empty() && "AssemblerCondString cannot be empty");

			O << R->getName() << ": ";
			for (StringRef str : Ops) {
				O << str << ", ";
			}
			O << "\n";
		}
	}

	void outputCortexM0AssemblerPredicatesTEST(raw_ostream &O)
	{
		CodeGenTarget Target(Records);
		std::vector<Record*> Insts = Records.getAllDerivedDefinitions("AssemblerPredicate");
		std::vector<std::string> lookingFor {
			"HasV6MOps",
				"HasV6Ops",
				"HasV5TEOps",
				"HasV5TOps",
				"HasV4TOps",
				"FeatureNoARM",
				"ModeThumb",
				"FeatureDB",
				"FeatureMClass",
		};

		// Construct all cases statement for each opcode
		for (std::vector<Record*>::iterator IC = Insts.begin(), EC = Insts.end();
				IC != EC; ++IC) {
			Record *R = *IC;

			const std::string &InstName = R->getName().str();
			std::string AsmCondString = R->getValueAsString("AssemblerCondString");

			// AsmCondString has syntax [!]F(,[!]F)*
			SmallVector<StringRef, 4> Ops;
			SplitString(AsmCondString, Ops, ",");
			assert(!Ops.empty() && "AssemblerCondString cannot be empty");

			O << R->getName() << ": ";
			for (StringRef str : Ops) {
				O << str << ", ";
			}
			O << "\n";
		}
	}

	void outputCortexM0InstrsTEST()
	{
		DEBUG(errs() << "Printing CortexM0 Instructions:\n");

		CodeGenTarget Target(Records);
		std::vector<Record*> Insts = Records.getAllDerivedDefinitions("Instruction");

		// derived by hand by looking at the cortex-m3 subtarget def
		std::vector<std::string> lookingFor {
			"HasV6M",
				"HasV6",
				"HasV5TE",
				"HasV5T",
				"HasV4T",
				"IsThumb",
				"HasDB",
				"IsMClass",
		};

		// predicates with empty AssemblerCondString that can be skipped
		std::vector<std::string> canBeIgnored {
			"DontUseFusedMAC",
				"DontUseMovt",
				"DontUseNEONForFP",
				"DontUseNaClTrap",
				"DontUseVMOVSR",
				"GenExecuteOnly",
				"HasFastVDUP32",
				"HasFastVGETLNi32",
				"HasSlowVDUP32",
				"HasSlowVGETLNi32",
				"HasZCZ",
				"IsBE",
				"IsLE",
				"IsMachO",
				"IsNaCl",
				"IsNotMachO",
				"IsNotWindows",
				"IsThumb1Only",
				"IsWindows",
				"NoHonorSignDependentRounding",
				"NoV4T",
				"NoV6",
				"NoV6K",
				"NoV6T2",
				"NoVFP",
				"UseFPVMLx",
				"UseFusedMAC",
				"UseMovt",
				"UseMulOps",
				"UseNEONForFP",
				"UseVMOVSR",
		};

		// Construct all cases statement for each opcode
		for (std::vector<Record*>::iterator IC = Insts.begin(), EC = Insts.end();
				IC != EC; ++IC) {
			Record *R = *IC;
			if (R->getValueAsString("Namespace") == "TargetOpcode" ||
					R->getValueAsBit("isPseudo"))
				continue;

			const std::string &InstName = R->getName().str();
			const auto &preds = R->getValueAsListOfDefs( StringLiteral("Predicates") );


			bool allFound = true;
			for (const Record *pred : preds) {
				auto result = std::find(lookingFor.begin(), lookingFor.end(), pred->getName());

				/* check if pred is canBeIgnored */
				if (std::end(canBeIgnored) != std::find(canBeIgnored.begin(), canBeIgnored.end(), pred->getName()))
					continue;

				if (result != std::end(lookingFor)) {
					/* O << "  > " << pred->getName() << "\n"; */
					/* O << "     " << InstName << ": " << pred->getName() << "\n"; */
				} else 
					allFound = false;
			}

			if (allFound)
				DEBUG(errs() << InstName << "\n");

			/* O << "case " << InstName << ": {\n"; */

		}
	}
};
} // end anonymous namespace


namespace llvm {

	void EmitALFWriter(RecordKeeper &RK, raw_ostream &OS)
	{
		emitSourceFileHeader("ALF Writer Source Fragment", OS);
		ALFWriterEmitter(RK).run(OS);
	}

}
