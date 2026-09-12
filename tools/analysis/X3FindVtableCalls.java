// Locate indirect COM vtable calls by slot displacement and report the calling
// context (containing function plus the immediate PUSH arguments that precede
// the call). This is an analysis tool: it prints addresses and integer operands
// only, never decompiler output, so it is safe to commit.
//
// X3AP.exe is an MSVC /O2 build that materializes the vtable entry in a
// register before calling it:
//     MOV EAX,[ESI]          ; load vtable pointer
//     MOV EDX,[EAX + 0x114]  ; load slot
//     PUSH ... ; PUSH ...
//     CALL EDX
// so the scanner handles both `CALL dword ptr [REG + disp]` and the two-step
// `MOV REG,[REG2 + disp]` / `CALL REG` form, resolving the register definition
// by walking backwards within the containing function.
//
// Usage:
//   -postScript X3FindVtableCalls.java <outfile> <disp> [<disp> ...]
// where each <disp> is a hexadecimal vtable byte offset, e.g. 114 10C 5C.
//
// Output lines per hit:
//   HIT disp=0x114 call=<addr> func=<entry> load=<addr> pushes=[imm,...]
// The "pushes" list walks backwards over at most 32 instructions and records
// every PUSH operand, most recent first. For stdcall COM methods arguments are
// pushed right-to-left, so the last declared argument appears first here and
// the `this` pointer is pushed last (it appears first only when the compiler
// uses ECX/thiscall-style loads, which D3D9 stdcall methods do not).
// @category X3
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

public class X3FindVtableCalls extends GhidraScript {

    private static final int PUSH_LOOKBACK = 32;
    private static final int DEF_LOOKBACK = 16;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            throw new IllegalArgumentException("Output path and at least one hex displacement required");
        }
        List<Long> wanted = new ArrayList<>();
        for (int i = 1; i < args.length; ++i) {
            wanted.add(Long.parseLong(args[i].replaceFirst("^0[xX]", ""), 16));
        }

        int hits = 0;
        try (PrintWriter out = new PrintWriter(args[0])) {
            out.println("# X3FindVtableCalls program=" + currentProgram.getName() + " displacements=" + wanted.size());
            InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
            while (instructions.hasNext()) {
                if (monitor.isCancelled()) {
                    break;
                }
                Instruction call = instructions.next();
                if (!"CALL".equalsIgnoreCase(call.getMnemonicString())) {
                    continue;
                }
                Long displacement = directDisplacement(call);
                Instruction load = null;
                if (displacement == null) {
                    Register target = soleRegister(call);
                    if (target == null) {
                        continue;
                    }
                    load = definingLoad(call, target);
                    if (load == null) {
                        continue;
                    }
                    displacement = memoryDisplacement(load);
                }
                if (displacement == null || !wanted.contains(displacement)) {
                    continue;
                }
                ++hits;
                Function owner = getFunctionContaining(call.getAddress());
                StringBuilder line = new StringBuilder();
                line.append("HIT disp=0x").append(Long.toHexString(displacement));
                line.append(" call=").append(call.getAddress());
                line.append(" func=").append(owner == null ? "unknown" : owner.getEntryPoint().toString());
                line.append(" load=").append(load == null ? "inline" : load.getAddress().toString());
                line.append(" pushes=").append(precedingPushes(call));
                out.println(line);
            }
            out.println("# total hits=" + hits);
        }
        println("X3FindVtableCalls: " + hits + " hit(s) written to " + args[0]);
    }

    /** `CALL dword ptr [REG + disp]` -> disp. */
    private Long directDisplacement(Instruction call) {
        return call.getNumOperands() < 1 ? null : registerPlusScalar(call.getOpObjects(0));
    }

    /** `MOV REG,[REG2 + disp]` -> disp (operand 1 is the memory source). */
    private Long memoryDisplacement(Instruction load) {
        return load.getNumOperands() < 2 ? null : registerPlusScalar(load.getOpObjects(1));
    }

    private Long registerPlusScalar(Object[] operands) {
        boolean hasRegister = false;
        Long scalar = null;
        for (Object operand : operands) {
            if (operand instanceof Register) {
                hasRegister = true;
            } else if (operand instanceof Scalar) {
                scalar = ((Scalar) operand).getUnsignedValue();
            }
        }
        return hasRegister && scalar != null ? scalar : null;
    }

    /** `CALL REG` -> REG, else null. */
    private Register soleRegister(Instruction call) {
        if (call.getNumOperands() != 1) {
            return null;
        }
        Object[] operands = call.getOpObjects(0);
        return operands.length == 1 && operands[0] instanceof Register ? (Register) operands[0] : null;
    }

    /** Nearest preceding MOV that writes `target` from a register+displacement memory operand. */
    private Instruction definingLoad(Instruction call, Register target) {
        Function callOwner = getFunctionContaining(call.getAddress());
        Instruction cursor = call;
        for (int i = 0; i < DEF_LOOKBACK; ++i) {
            cursor = cursor.getPrevious();
            if (cursor == null || leavesFunction(cursor, callOwner)) {
                return null;
            }
            if (!"MOV".equalsIgnoreCase(cursor.getMnemonicString()) || cursor.getNumOperands() < 2) {
                continue;
            }
            Object[] destination = cursor.getOpObjects(0);
            if (destination.length != 1 || !(destination[0] instanceof Register)) {
                continue;
            }
            if (!target.equals(destination[0])) {
                continue;
            }
            return cursor;
        }
        return null;
    }

    private boolean leavesFunction(Instruction instruction, Function callOwner) {
        Function owner = getFunctionContaining(instruction.getAddress());
        return owner != null && callOwner != null && !owner.equals(callOwner);
    }

    /** Collects PUSH operands preceding the call, nearest first. */
    private String precedingPushes(Instruction call) {
        Function callOwner = getFunctionContaining(call.getAddress());
        List<String> found = new ArrayList<>();
        Instruction cursor = call;
        for (int i = 0; i < PUSH_LOOKBACK; ++i) {
            cursor = cursor.getPrevious();
            if (cursor == null || leavesFunction(cursor, callOwner)) {
                break;
            }
            String mnemonic = cursor.getMnemonicString();
            if (!"PUSH".equalsIgnoreCase(mnemonic)) {
                if ("CALL".equalsIgnoreCase(mnemonic) || "RET".equalsIgnoreCase(mnemonic)) {
                    break; // a nested call consumed the stack; earlier pushes are not ours
                }
                continue;
            }
            Object[] operands = cursor.getOpObjects(0);
            if (operands.length == 1 && operands[0] instanceof Scalar) {
                found.add("0x" + Long.toHexString(((Scalar) operands[0]).getUnsignedValue()));
            } else {
                StringBuilder rendered = new StringBuilder();
                for (Object operand : operands) {
                    if (rendered.length() > 0) {
                        rendered.append('+');
                    }
                    rendered.append(operand.toString());
                }
                found.add("[" + rendered + "]");
            }
        }
        return found.toString();
    }
}
