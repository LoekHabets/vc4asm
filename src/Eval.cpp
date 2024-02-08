/*
 * Eval.cpp
 *
 *  Created on: 16.11.2014
 *      Author: mueller
 */

#include "Eval.h"
#include "Inst.h"
#include "utils.h"
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <climits>


constexpr const struct EvalMSG Eval::MSG;


Eval::operate::operate(vector<exprEntry>& stack)
:	rhs(stack.back())
,	lhs(stack[stack.size()-2])
,	types((1 << rhs.Type) | !isUnary(lhs.Op) * (1 << lhs.Type))
{}

void Eval::operate::TypesFail()
{	if (isUnary(lhs.Op))
		throw MSG.UNARY_OP_WRONG_TYPE.toMsg(op2string(lhs.Op), type2string(rhs.Type));
	else
		throw MSG.BINARY_OP_WRONG_TYPES.toMsg(op2string(lhs.Op), type2string(lhs.Type), type2string(rhs.Type));
}

void Eval::operate::CheckInt()
{	if (types != 1<<V_INT)
		TypesFail();
}

void Eval::operate::CheckBool()
{	CheckInt();
	lhs.Type = V_INT;
}

void Eval::operate::PropFloat()
{	if (lhs.Type == V_INT)
	{	lhs.fValue = lhs.iValue;
		lhs.Type = V_FLOAT;
		types = 1<<V_FLOAT;
	}
	if (rhs.Type == V_INT)
	{	rhs.fValue = rhs.iValue;
		rhs.Type = V_FLOAT;
		types = 1<<V_FLOAT;
	}
}

void Eval::operate::CheckNumericPropFloat()
{	if (types & ~(1<<V_INT | 1<<V_FLOAT))
		TypesFail();
	if (types == (1<<V_INT | 1<<V_FLOAT))
		PropFloat();
}

int Eval::operate::Compare()
{	int ret;
	switch (types)
	{case 1<<V_INT | 1<<V_FLOAT:
		PropFloat();
	 case 1<<V_FLOAT:
		if (lhs.fValue < rhs.fValue)
			goto less;
		if (lhs.fValue > rhs.fValue)
			goto greater;
		if (lhs.fValue == rhs.fValue)
			goto equal;
		ret = INT_MIN; // Indeterminate
		goto end;
	 case 1<<V_INT:
		if (lhs.iValue < rhs.iValue)
			goto less;
		if (lhs.iValue > rhs.iValue)
			goto greater;
		goto equal;
	 case 1<<V_LDPES:
	 case 1<<V_LDPE:
	 case 1<<V_LDPEU:
	 case 1<<V_LABEL:
		if (lhs.iValue < rhs.iValue)
			goto less;
		if (lhs.iValue > rhs.iValue)
			goto greater;
		goto equal;
	 case 1<<V_REG:
		if (lhs.rValue.Type < rhs.rValue.Type)
			goto less;
		if (lhs.rValue.Type > rhs.rValue.Type)
			goto greater;
		if (lhs.rValue.Rotate < rhs.rValue.Rotate)
			goto less;
		if (lhs.rValue.Rotate > rhs.rValue.Rotate)
			goto greater;
		if (lhs.rValue.Num < rhs.rValue.Num)
			goto less;
		if (lhs.rValue.Num > rhs.rValue.Num)
			goto greater;
		if (lhs.rValue.Pack.Mode < rhs.rValue.Pack.Mode)
			goto less;
		if (lhs.rValue.Pack.Mode > rhs.rValue.Pack.Mode)
			goto less;
		goto equal;
	 default:
		if (lhs.Type > rhs.Type)
			goto greater;
	}
 less:
	ret = -1;
	goto end;
 greater:
	ret = 1;
	goto end;
 equal:
	ret = 0;
 end:
	lhs.Type = V_INT;
	return ret;
}

void Eval::operate::ApplyUnaryMathOp(double (*op)(double))
{	if (types & ~(1<<V_INT | 1<<V_FLOAT))
		TypesFail();
	PropFloat();
	lhs.fValue = op(rhs.fValue);
	lhs.Type = V_FLOAT;
}

bool Eval::operate::Apply(bool unary)
{	if ( precedence(lhs.Op) < precedence(rhs.Op)
		|| (unary && isUnary(lhs.Op)) )
		return true;

	switch (lhs.Op)
	{default:
		throw MSG.INTERNAL_ERROR.toMsg();
	 case BRO:
		switch (rhs.Op)
		{case EVAL:
			throw MSG.MISSING_CLOSING_BRACE.toMsg();
		 case BRC:
			lhs.Op = NOP; // closing brace cancels opening brace
			lhs.iValue = rhs.iValue;
			lhs.Type = rhs.Type;
			return false;
		 default:
			return true; // can't evaluate over opening brace
		}
	 case NOP:
		lhs = rhs;
		return false;
	 case NEG:
		lhs.Type = rhs.Type;
		switch (rhs.Type)
		{	uint32_t sh;
		 default:
			TypesFail();
		 case V_FLOAT:
			lhs.fValue = -rhs.fValue; break;
		 case V_INT:
			lhs.iValue = -rhs.iValue; break;
		 case V_LDPEU:
		 case V_LDPE:
			sh = (rhs.iValue & 0x55555555U) << 1;
			if (rhs.iValue & sh)
				throw MSG.CANNOT_NEGATE_PES3.toMsg();
			lhs.iValue = rhs.iValue | sh;
			lhs.Type = V_LDPES;
			break;
		 case V_LDPES:
			sh = (rhs.iValue & 0x55555555U) << 1;
			lhs.iValue = rhs.iValue | sh;
			if (lhs.iValue & sh)
				throw MSG.CANNOT_NEGATE_PESm2.toMsg();
			break;
		}
		break;
	 case NOT:
		switch (rhs.Type)
		{default:
			TypesFail();
		 case V_INT:
		 case V_LDPES:
		 case V_LDPE:
		 case V_LDPEU:
			lhs.iValue = ~rhs.iValue;
		}
		lhs.Type = rhs.Type;
		break;
	 case lNOT:
		CheckBool();
		lhs.iValue = !rhs.iValue;
		break;
	 case ABS:
		CheckNumericPropFloat();
		if (rhs.Type == V_FLOAT)
			lhs.fValue = fabs(rhs.fValue);
		else
			lhs.iValue = abs(rhs.iValue);
		lhs.Type = rhs.Type;
		break;
	 case CEIL:
		if (rhs.Type != V_FLOAT)
			TypesFail();
		lhs.iValue = (int64_t)ceil(rhs.fValue);
		lhs.Type = V_INT;
		break;
	 case FLOOR:
		if (rhs.Type != V_FLOAT)
			TypesFail();
		lhs.iValue = (int64_t)floor(rhs.fValue);
		lhs.Type = V_INT;
		break;
	 case SQRT:
		ApplyUnaryMathOp(&sqrt); break;
	 case EXP:
		ApplyUnaryMathOp(&exp); break;
	 case EXP2:
		ApplyUnaryMathOp(&exp2); break;
	 case EXP10:
		ApplyUnaryMathOp([](double val) { return pow(10., val); }); break;
	 case LOG:
		ApplyUnaryMathOp(&log); break;
	 case LOG2:
		ApplyUnaryMathOp([](double val) { return log(val) / M_LN2; }); break;
	 case LOG10:
		ApplyUnaryMathOp(&log10); break;
	 case COS:
		ApplyUnaryMathOp(&cos); break;
	 case SIN:
		ApplyUnaryMathOp(&sin); break;
	 case TAN:
		ApplyUnaryMathOp(&tan); break;
	 case ACOS:
		ApplyUnaryMathOp(&acos); break;
	 case ASIN:
		ApplyUnaryMathOp(&asin); break;
	 case ATAN:
		ApplyUnaryMathOp(&atan); break;
	 case COSH:
		ApplyUnaryMathOp(&cosh); break;
	 case SINH:
		ApplyUnaryMathOp(&sinh); break;
	 case TANH:
		ApplyUnaryMathOp(&tanh); break;
	 case ACOSH:
		ApplyUnaryMathOp(&acosh); break;
	 case ASINH:
		ApplyUnaryMathOp(&asinh); break;
	 case ATANH:
		ApplyUnaryMathOp(&atanh); break;
	 case ERF:
		ApplyUnaryMathOp(&erf); break;
	 case ERFC:
		ApplyUnaryMathOp(&erfc); break;
	 case POW:
		CheckNumericPropFloat();
		if (rhs.Type == V_FLOAT)
			lhs.fValue = pow(lhs.fValue, rhs.fValue);
		else
			lhs.iValue = (int64_t)floor(pow(lhs.iValue, rhs.iValue)+.5);
		break;
	 case MUL:
		CheckNumericPropFloat();
		if (rhs.Type == V_FLOAT)
			lhs.fValue *= rhs.fValue;
		else
			lhs.iValue *= rhs.iValue;
		break;
	 case DIV:
		CheckNumericPropFloat();
		if (rhs.Type == V_FLOAT)
			lhs.fValue /= rhs.fValue;
		else
			lhs.iValue /= rhs.iValue;
		break;
	 case MOD:
		CheckNumericPropFloat();
		if (rhs.Type == V_FLOAT)
			lhs.fValue = fmod(lhs.fValue, rhs.fValue);
		else
			lhs.iValue %= rhs.iValue;
		break;
	 case ADD:
		switch (types)
		{default:
			TypesFail();
		 case 1<<V_INT | 1<<V_FLOAT:
			PropFloat();
			lhs.Type = V_FLOAT;
		 case 1<<V_FLOAT:
			lhs.fValue += rhs.fValue; break;
		 case 1<<V_LABEL | 1<<V_INT:
			lhs.Type = V_LABEL;
		 case 1<<V_INT:
			lhs.iValue += rhs.iValue; break;
		 case 1<<V_REG | 1<<V_INT:
			if (rhs.Type == V_REG)
				swap((exprValue&)lhs, (exprValue&)rhs);
			rhs.iValue += lhs.rValue.Num;
		 regaddcont:
			if ( rhs.iValue < 0 || rhs.iValue > 63
				|| ((lhs.rValue.Type & R_SEMA) && (uint8_t)rhs.iValue > 15) )
				throw MSG.REGNO_OUT_OF_RANGE.toMsg(rhs.iValue);
			lhs.rValue.Num = (uint8_t)rhs.iValue;
			break;
		} break;
	 case SUB:
		switch (types)
		{default:
			TypesFail();
		 case 1<<V_INT | 1<<V_FLOAT:
			PropFloat();
			lhs.Type = V_FLOAT;
		 case 1<<V_FLOAT:
			lhs.fValue -= rhs.fValue; break;
		 case 1<<V_LABEL:
			lhs.Type = V_INT;
			lhs.iValue -= rhs.iValue; break;
		 case 1<<V_LABEL | 1<<V_INT:
			if (rhs.Type == V_LABEL)
				TypesFail();
		 case 1<<V_INT:
			lhs.iValue -= rhs.iValue; break;
		 case 1<<V_REG | 1<<V_INT:
			if (rhs.Type == V_REG)
				TypesFail();
			rhs.iValue = lhs.rValue.Num - rhs.iValue;
			goto regaddcont;
		} break;
	 case ASL:
	 case ROL32:
		switch (types)
		{case 1<<V_INT:
			if (lhs.Op == ROL32)
			{	unsigned s = rhs.iValue & 31;
				lhs.iValue = (uint32_t)lhs.iValue << s | (uint32_t)lhs.iValue >> (32-s);
			} else if (rhs.iValue < 0)
				lhs.iValue >>= -rhs.iValue;
			else
				lhs.iValue <<= rhs.iValue;
			break;
		 case 1<<V_FLOAT | 1<<V_INT:
			if (lhs.Type != V_FLOAT || lhs.Op != ASL)
				TypesFail();
			lhs.fValue = ldexp(lhs.fValue, rhs.iValue > 5000 ? 5000 : rhs.iValue < -5000 ? -5000 : (int)rhs.iValue);
			break;
		 case 1<<V_REG:
			if (lhs.rValue.Rotate || rhs.rValue.Num != 32+5 || rhs.rValue.Type != R_AB || rhs.rValue.Rotate || rhs.rValue.Pack)
				throw MSG.ROT_REGISTER_NOT_R5.toMsg();
			lhs.rValue.Rotate = 16;
			break;
		 case 1<<V_REG | 1<<V_INT:
			if (lhs.Type == V_REG)
			{	if (lhs.rValue.Rotate & ~0xf)
					throw MSG.ROT_REGISTER_AND_VALUE.toMsg();
				lhs.rValue.Rotate = (lhs.rValue.Rotate + rhs.iValue) & 0xf;
				break;
			}
		 default:
			TypesFail();
		} break;
	 case ASR:
	 case ROR32:
		switch (types)
		{case 1<<V_INT:
			if (lhs.Op == ROR32)
			{	unsigned s = rhs.iValue & 31;
				lhs.iValue = (uint32_t)lhs.iValue >> s | (uint32_t)lhs.iValue << (32-s);
			} else if (rhs.iValue < 0)
				lhs.iValue <<= -rhs.iValue;
			else
				lhs.iValue >>= rhs.iValue;
			break;
		 case 1<<V_FLOAT | 1<<V_INT:
			if (lhs.Type != V_FLOAT || lhs.Op != ASL)
				TypesFail();
			lhs.fValue = ldexp(lhs.fValue, rhs.iValue > 5000 ? -5000 : rhs.iValue < -5000 ? 5000 : -(int)rhs.iValue);
			break;
		 case 1<<V_REG:
			if (lhs.rValue.Rotate || rhs.rValue.Num != 32+5 || rhs.rValue.Type != R_AB || rhs.rValue.Rotate)
				throw MSG.ROT_REGISTER_NOT_R5.toMsg();
			lhs.rValue.Rotate = -16;
			break;
		 case 1<<V_REG | 1<<V_INT:
			if (lhs.Type == V_REG)
			{	if (lhs.rValue.Rotate & ~0xf)
					throw MSG.ROT_REGISTER_AND_VALUE.toMsg();
				lhs.rValue.Rotate = (lhs.rValue.Rotate - rhs.iValue) & 0xf;
				break;
			}
		 default:
			TypesFail();
		} break;
	 case SHL:
		CheckInt();
		lhs.iValue <<= rhs.iValue;
		break;
	 case SHR:
		CheckInt();
		(uint64_t&)lhs.iValue >>= rhs.iValue;
		break;
	 case ROL64:
		CheckInt();
		{	unsigned s = rhs.iValue & 63;
			lhs.iValue = lhs.iValue << s | (uint64_t)lhs.iValue >> (64-s);
		} break;
	 case ROR64:
		CheckInt();
		{	unsigned s = rhs.iValue & 63;
			lhs.iValue = (uint64_t)lhs.iValue >> s | lhs.iValue << (64-s);
		} break;
	 case GT:
		lhs.iValue = Compare() == 1; break;
	 case GE:
		lhs.iValue = Compare() >= 0; break;
	 case LT:
		lhs.iValue = Compare() == -1; break;
	 case LE:
		lhs.iValue = (unsigned)(Compare() + 1) <= 1; break; // take care of indeterminate
	 case CMP:
		lhs.iValue = Compare(); break;
	 case EQ:
		lhs.iValue = Compare() == 0; break;
	 case NE:
		lhs.iValue = Compare() != 0; break;
	 case IDNT:
		lhs.iValue = lhs == rhs;
		lhs.Type = V_INT;
		break;
	 case NIDNT:
		lhs.iValue = lhs != rhs;
		lhs.Type = V_INT;
		break;
	 case AND:
		if (types == 1<<V_REG)
		{	// Hack to mask registers
			lhs.rValue.Num &= rhs.rValue.Num;
			(uint8_t&)lhs.rValue.Type &= rhs.rValue.Type;
			lhs.rValue.Rotate &= rhs.rValue.Rotate;
			lhs.rValue.Pack.Mode &= rhs.rValue.Pack.Mode;
			break;
		}
		CheckInt();
		lhs.iValue &= rhs.iValue;
		break;
	 case XOR:
		CheckInt();
		lhs.iValue ^= rhs.iValue;
		break;
	 case XNOR:
		CheckInt();
		lhs.iValue = ~(lhs.iValue ^ rhs.iValue);
		break;
	 case OR:
		CheckInt();
		lhs.iValue |= rhs.iValue;
		break;
	 case lAND:
		CheckBool();
		lhs.iValue = lhs.iValue && rhs.iValue;
		break;
	 case lXOR:
		CheckBool();
		lhs.iValue = !lhs.iValue ^ !rhs.iValue;
		break;
	 case lXNOR:
		CheckBool();
		lhs.iValue = !(!lhs.iValue ^ !rhs.iValue);
		break;
	 case lOR:
		CheckBool();
		lhs.iValue = lhs.iValue || rhs.iValue;
		break;
	}
	lhs.Op = rhs.Op;
	return false;
}


bool Eval::partialEvaluate(bool unary)
{	while (true)
	{	if (Stack.size() <= 1)
		{	if (Stack.back().Op == BRC) // closing brace w/o opening brace, might not belong to our expression
			{	if (Stack.back().Type == V_NONE)
					throw MSG.MISSING_VALUE.toMsg();
				return false;
			}
			return true;
		}
		operate op(Stack);
		if (op.Apply(unary))
			return true;
		Stack.pop_back();
	}
}

bool Eval::PushOperator(mathOp op)
{	exprEntry& current = Stack.back();
	current.Op = op;
	if (isUnary(op))
	{	if (current.Type != V_NONE)
			throw MSG.UNARY_OP_AFTER_VALUE.toMsg(op2string(op));
	 unary:
		Stack.emplace_back();
		return true;
	} else
	{	if (current.Type == V_NONE)
			switch (op)
			{default:
				throw MSG.BINARY_OP_WO_LHS_VALUE.toMsg(op2string(op));
			 case ADD:
				current.Op = NOP;
				goto unary;
			 case SUB:
				current.Op = NEG;
				goto unary;
			 case BRC:;
			}
	}
	if (!partialEvaluate(false))
		return false;
	if (op != BRC)
		Stack.emplace_back();
	return true;
}

void Eval::PushValue(const exprValue& value)
{	exprEntry& current = Stack.back();
	if (current.Type != V_NONE)
		throw MSG.MISSING_OP_BEFORE_VALUE.toMsg(value.toString().c_str());
	(exprValue&)current = value; // assign slice
	partialEvaluate(true);
}

exprValue Eval::Evaluate()
{	exprEntry& current = Stack.back();
	if (current.Type == V_NONE)
	{	if ( Stack.size() == 2 && Stack.front().Op == NEG
			&& current.Op == NOP && current.Type == V_NONE ) // special handling for '-' as r39
		{	current.Type = V_REG;
			current.rValue.Num = Inst::R_NOP;
			current.rValue.Rotate = 0;
			current.rValue.Type = R_RWAB;
			current.rValue.Pack.reset();
			return current;
		}
		throw MSG.MISSING_VALUE.toMsg();
	}
	current.Op = EVAL;
	partialEvaluate(false);
	return Stack.front();
}

const char* Eval::op2string(mathOp op)
{	switch (op)
	{case BRO:  return "(";
	 case BRC:  return ")";
	 case NOP:
	 case ADD:  return "+";
	 case NEG:
	 case SUB:  return "-";
	 case NOT:  return "~";
	 case lNOT: return "!";
	 case ABS:  return "abs";
	 case CEIL: return "ceil";
	 case FLOOR:return "floor";
	 case SQRT: return "sqrt";
	 case EXP:  return "exp";
	 case EXP2: return "exp2";
	 case EXP10:return "exp10";
	 case LOG:  return "log";
	 case LOG2: return "log2";
	 case LOG10:return "log10";
	 case COS:  return "cos";
	 case SIN:  return "sin";
	 case TAN:  return "tan";
	 case ACOS: return "acos";
	 case ASIN: return "asin";
	 case ATAN: return "atan";
	 case COSH: return "cosh";
	 case SINH: return "sinh";
	 case TANH: return "tanh";
	 case ACOSH:return "acosh";
	 case ASINH:return "asinh";
	 case ATANH:return "atanh";
	 case ERF:  return "erf";
	 case ERFC: return "erfc";
	 case POW:  return "**";
	 case MUL:  return "*";
	 case DIV:  return "/";
	 case MOD:  return "%";
	 case ASL:  return "<<";
	 case ASR:  return ">>";
	 case SHL:  return "<<<";
	 case SHR:  return ">>>";
	 case ROL32:return "><<";
	 case ROR32:return ">><";
	 case ROL64:return "><<<";
	 case ROR64:return ">>><";
	 case GT:   return ">";
	 case GE:   return ">=";
	 case LT:   return "<";
	 case LE:   return "<=";
	 case EQ:   return "==";
	 case NE:   return "!=";
	 case IDNT: return "===";
	 case NIDNT:return "!==";
	 case CMP:  return "<=>";
	 case AND:  return "&";
	 case XOR:  return "^";
	 case XNOR: return "!^";
	 case OR:   return "|";
	 case lAND: return "&&";
	 case lXOR: return "^^";
	 case lXNOR:return "!^^";
	 case lOR:  return "||";
	 default:   return NULL;
	}
}
