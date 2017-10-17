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

namespace {

class ALFWriterEmitter {
	RecordKeeper &Records;
	CodeGenTarget Target;
	vector<const CodeGenInstruction *> NumberedInstructions;
	CodeGenDAGPatterns CGDP;

	// Info needed for register
	struct ALFReg {
		const CodeGenRegister* reg;
		unsigned int bitwidth;
	};

	vector<ALFReg> RootRegs;


public:
	ALFWriterEmitter(RecordKeeper &R) : Records(R), Target(R), CGDP(Records)
	{
	}

	void run(raw_ostream &O)
	{
		/* outputCortexM3InstrsTEST(O); */
		/* outputCortexM3AssemblerPredicatesTEST(O); */
		/* outputCortexM3PredicatesTEST(O); */

		/* outputCortexM0AssemblerPredicatesTEST(O); */
		/* outputCortexM0InstrsTEST(O); */

		/* outputALFRegisterDefinitionsTEST(O); */
		/* outputALFInstrMapping(O); */

		outputRegDefALF(O);
		outputPrintInstructionALF(O);
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

	void findMachineInstrIndexes_ForPattern(const CodeGenInstruction *I, vector<int> &indexesForMI, vector<TreePatternNode*> &operators, vector<TreePatternNode*> &leafs)
	{
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
				for (int j = 0; j < leafNames.size(); j++) {
					for (unsigned i = 0, e = I->Operands.size(); i != e; ++i) {
						string op = I->Operands[i].Name;
						if (op == leafNames[j]) {
							indexesForMI.push_back(i);
						}
					}
				}
			}
		}
	}

	void outputRegDefALF(raw_ostream &O)
	{
		O <<
			"/// regDefALF - This method is automatically generated by tablegen\n"
			"/// from the instruction set description.\n"
			"void ARMALFWriter::regDefALF(ALFBuilder &b) {\n";

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

	void handleDefaultOperand(raw_ostream &O, string returnVariable, unsigned int MIindex, TreePatternNode *leaf)
	{
		// ASSUMPTIONS: mcop exists, as well as ctx, TRI

		// check if ComplexPattern
		if (auto cp = leaf->getComplexPatternInfo(CGDP)) {
			Record *cpR = cp->getRecord(); 
			if (!cpR->getValueAsString("ALFCustomMethod").empty())
				O << "      " << returnVariable << " = " << cpR->getValueAsString("ALFCustomMethod") << "(ctx, TRI, MI);"<< "\n";
		} else { // else check on MI operand what to do
			O << "      if (MI.getOperand(" << MIindex << ").isReg()) {\n";
			O << "        " << returnVariable << " = ctx->load(32, TRI->getName(MI.getOperand(" << MIindex << ").getReg()));\n";
			O << "      } else if (MI.getOperand(" << MIindex << ").isImm()) {\n";
			O << "        " << returnVariable << " = ctx->dec_unsigned(32, MI.getOperand(" << MIindex << ").getImm());\n";
			O << "      }\n";
		}
	}
	
	bool make_case(raw_ostream &O, const CodeGenInstruction *I, std::vector<int> indexesForMI, vector<TreePatternNode*> operators, vector<TreePatternNode*> leafs)
	{
		// collect names of the operators
		vector<string> operatorNames;
		for (auto tpn : operators) {
			operatorNames.push_back(tpn->getOperator()->getName());
		}

		// check for some is* flags in th CGI
		// if isReturn is set make a return statement
		if (I->isReturn) {
			O << "      alfbb.addStatement(label, TII->getName(MI.getOpcode()), ctx->ret());\n";
			return true; // stop here
		}

		// do something for set ... 
		if (operatorNames.size() >= 2 &&
				operatorNames[0] == "set") { 
			O << "      ALFStatement *statement;\n";
			O << "      std::string targetReg;\n";
			O << "      SExpr *op1, *op2;\n";

			// one argument
			if (operatorNames[1] == "imm") {
				// assume the first index is a register, and assume the second index is an immediate
				O << "      targetReg = TRI->getName(MI.getOperand(" << indexesForMI[0] << ").getReg());\n";

				handleDefaultOperand(O, "op1", indexesForMI[1], leafs[1]);

				O << "      SExpr *stor = ctx->store(ctx->address(targetReg), op1);\n";
				O << "      statement = alfbb.addStatement(label, TII->getName(MI.getOpcode()), stor);\n";

			// two arguments
			} else if (operatorNames[1] == "add") {
				// assume the first index is a register,
				// and assume the second and third index are registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << indexesForMI[0] << ").getReg());\n";

				handleDefaultOperand(O, "op1", indexesForMI[1], leafs[1]);
				handleDefaultOperand(O, "op2", indexesForMI[2], leafs[2]);

				O << "      SExpr *expr = ctx->add(32, op1, op2, 0);\n";

				O << "      SExpr *stor = ctx->store(ctx->address(targetReg), expr);\n";
				O << "      statement = alfbb.addStatement(label, TII->getName(MI.getOpcode()), stor);\n";

			} else if (operatorNames[1] == "ld") {
				// assume the first index is a target register,
				// and assume the second index is registers or immediates
				O << "      targetReg = TRI->getName(MI.getOperand(" << indexesForMI[0] << ").getReg());\n";

				handleDefaultOperand(O, "op1", indexesForMI[1], leafs[1]);

				O << "      SExpr *load = ctx->load(32, op1);\n";
				O << "      SExpr *stor = ctx->store(ctx->address(targetReg), load);\n";
				O << "      statement = alfbb.addStatement(label, TII->getName(MI.getOpcode()), stor);\n";
			} else {
				O << "      goto default_label;\n";
			}

			// write some information of the operands to Text for the customCodeAfterSET method
			//
		/* O << "  struct OperandInfo {\n"; */
		/* O << "    std::string RecName;\n"; */
		/* O << "    std::string Name;\n"; */
		/* O << "    std::string OperandType;\n"; */
		/* O << "    std::string MIOperandNo;\n"; */
		/* O << "  };\n"; */

			/* O << "      vector<OperandInfo> opinfo;\n"; */

			/* for (unsigned i = 0, e = I->Operands.size(); i != e; ++i) { */
			/* 	auto op = I->Operands[i]; */
			/* 	O << "      opinfo.RecName;\n"; */
			/* 	O << "      opinfo.Name;\n"; */
			/* 	O << "      opinfo.OperandType;\n"; */
			/* 	O << "      opinfo.MIOperandNo;\n"; */
			/* 	dbgs() << "name: " << op.Rec->getName().str() << "\n"; */
			/* 	dbgs() << "symname: " << op.Name << "\n"; */
			/* 	dbgs() << "type: " << op.OperandType << "\n"; */
			/* 	dbgs() << "MIOperandNo: " << op.MIOperandNo << "\n"; */
			/* } */
			/* dbgs() << "\n"; */

			O << "      vector<SExpr *> ops = { op1, op2 };\n";
			O << "      customCodeAfterSET(alfbb, ctx, statement, MI, targetReg, ops);\n";
			return true;
		}
		// do something for st 
		else if (operatorNames.size() >= 1 &&
				operatorNames[0] == "st") { 
			// assume the first 
			O << "      ALFStatement *statement;\n";
			O << "      SExpr *address;\n";

			O << "      SExpr *storValue = ctx->load(32, TRI->getName(MI.getOperand(" << indexesForMI[0] << ").getReg()));\n";

			handleDefaultOperand(O, "address", indexesForMI[1], leafs[1]);

			O << "      SExpr *stor = ctx->store(address, storValue);\n";
			O << "      statement = alfbb.addStatement(label, TII->getName(MI.getOpcode()), stor);\n";
			return true;
		}
		return false;
	}

	void outputPrintInstructionALF(raw_ostream &O)
	{
		// Get the instruction numbering.
		NumberedInstructions = Target.getInstructionsByEnumValue();

		O <<
			"/// printInstructionALF - This method is automatically generated by tablegen\n"
			"/// from the instruction set description.\n"
			"void ARMALFWriter::printInstructionALF(const MachineInstr &MI, ALFStatementGroup &alfbb, ALFContext *ctx, string label) {\n";

		O << "  const unsigned opcode = MI.getOpcode();\n";
		O << "  const TargetInstrInfo *TII = MI.getParent()->getParent()->getSubtarget().getInstrInfo();\n";
		O << "  const TargetRegisterInfo *TRI = MI.getParent()->getParent()->getSubtarget().getRegisterInfo();\n";
		// user later in the code for custommethodafterset
		O << "  struct OperandInfo {\n";
		O << "    std::string RecName;\n";
		O << "    std::string Name;\n";
		O << "    std::string OperandType;\n";
		O << "    unsigned MIOperandNo;\n";
		O << "  };\n";

		O << "  switch (opcode) {\n";

		for (unsigned i = 0, e = NumberedInstructions.size(); i != e; ++i) {
			const CodeGenInstruction *I = NumberedInstructions[i];

			const std::string &InstName = I->TheDef->getName().str();
			O << "    case " << I->Namespace << "::" << InstName << ": {\n";

			// collect information about the Pattern field: 
			// operators (set, add.. ),  leafs ( $Rn, $Rm)
			// and indexes of the leafs in the operands of the MI

			std::vector<int> indexesForMI;
			vector<TreePatternNode*> operators, leafs;
			findMachineInstrIndexes_ForPattern(I, indexesForMI, operators, leafs);

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
			O << "      //indexesForMI: ";
			for (auto i : indexesForMI) {
				O << to_string(i) << " ";
			}
			O << "\n";
			// print the names of the DAG operators like set, st
			O << "      //operatorNames: ";
			for (auto tpn : operators) {
				O << tpn->getOperator()->getName() << " ";
			}
			O << "\n";
			// print complex leafs if any
			O << "      //complexleafs: ";
			for (auto tpn : leafs) {
				const ComplexPattern *cp = tpn->getComplexPatternInfo(CGDP);
				if (cp) {
					O << cp->getRecord()->getName() << " ";
				}
			}
			O << "\n";


			// after printing comments, generate the code for each case

			// call the special function if it is not empty
			Record *R = I->TheDef; 
			if (!R->getValueAsString("ALFCustomMethod").empty()) {
				O << "      " << R->getValueAsString("ALFCustomMethod") << "(MI, alfbb, ctx, label);\n";
			} else if (!operators.empty() && // else try to do something with operators
					make_case(O, I, indexesForMI, operators, leafs)) {
			} else { // else jump to the end
				O << "      goto default_label;\n";
			}

			O << "      break;\n";
			O << "    }\n";
		}

		// Default case: unhandled opcode
		O << "    default: {\n";
		O << "      default_label:\n";
		O << "        alfbb.addStatement(label, TII->getName(MI.getOpcode()), ctx->null());\n";
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

	void outputCortexM3InstrsTEST(raw_ostream &O)
	{
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
				O << InstName << "\n";

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

	void outputCortexM0InstrsTEST(raw_ostream &O)
	{
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
				O << InstName << "\n";

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
