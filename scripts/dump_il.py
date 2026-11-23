#!/usr/bin/env python3
"""Dump IL of Photon3Unity3D.dll handshake methods (EnetPeer/EnetProtocol)."""
import dnfile, struct, sys

DLL = "/home/z/my-project/toolchain/ref_jibo/PhotonAssets/Plugins/Photon3Unity3D.dll"
pe = dnfile.dnPE(DLL)
md = pe.net.mdtables

# --- opcode table (subset needed for these methods) ---
OPS = {}
def op(code, name, operand):
    OPS[code] = (name, operand)
# one-byte
for c, (n, o) in {
0x00:("nop",0),0x01:("break",0),0x02:("ldarg.0",0),0x03:("ldarg.1",0),0x04:("ldarg.2",0),
0x05:("ldarg.3",0),0x06:("ldloc.0",0),0x07:("ldloc.1",0),0x08:("ldloc.2",0),0x09:("ldloc.3",0),
0x0A:("stloc.0",0),0x0B:("stloc.1",0),0x0C:("stloc.2",0),0x0D:("stloc.3",0),
0x0E:("ldarg.s",1),0x0F:("ldarga.s",1),0x10:("starg.s",1),0x11:("ldloc.s",1),
0x12:("ldloca.s",1),0x13:("stloc.s",1),0x14:("ldnull",0),0x15:("ldc.i4.m1",0),
0x16:("ldc.i4.0",0),0x17:("ldc.i4.1",0),0x18:("ldc.i4.2",0),0x19:("ldc.i4.3",0),
0x1A:("ldc.i4.4",0),0x1B:("ldc.i4.5",0),0x1C:("ldc.i4.6",0),0x1D:("ldc.i4.7",0),
0x1E:("ldc.i4.8",0),0x1F:("ldc.i4.s",1),0x20:("ldc.i4",4),0x21:("ldc.i8",8),
0x22:("ldc.r4",4),0x23:("ldc.r8",8),0x25:("dup",0),0x26:("pop",0),0x27:("jmp",4),
0x28:("call",4),0x29:("calli",4),0x2A:("ret",0),0x2B:("br.s",1),0x2C:("brfalse.s",1),
0x2D:("brtrue.s",1),0x2E:("beq.s",1),0x2F:("bge.s",1),0x30:("bgt.s",1),0x31:("ble.s",1),
0x32:("blt.s",1),0x33:("bne.un.s",1),0x34:("bge.un.s",1),0x35:("bgt.un.s",1),
0x36:("ble.un.s",1),0x37:("blt.un.s",1),0x38:("br",4),0x39:("brfalse",4),0x3A:("brtrue",4),
0x3B:("beq",4),0x3C:("bge",4),0x3D:("bgt",4),0x3E:("ble",4),0x3F:("blt",4),
0x40:("bne.un",4),0x41:("bge.un",4),0x42:("bgt.un",4),0x43:("ble.un",4),0x44:("blt.un",4),
0x45:("switch",0),0x46:("ldind.i1",0),0x47:("ldind.u1",0),0x48:("ldind.i2",0),
0x49:("ldind.u2",0),0x4A:("ldind.i4",0),0x4B:("ldind.u4",0),0x4C:("ldind.i8",0),
0x4D:("ldind.i",0),0x4E:("ldind.r4",0),0x4F:("ldind.r8",0),0x50:("ldind.ref",0),
0x51:("stind.ref",0),0x52:("stind.i1",0),0x53:("stind.i2",0),0x54:("stind.i4",0),
0x55:("stind.i8",0),0x56:("stind.r4",0),0x57:("stind.r8",0),
0x58:("add",0),0x59:("sub",0),0x5A:("mul",0),0x5B:("div",0),0x5C:("div.un",0),
0x5D:("rem",0),0x5E:("rem.un",0),0x5F:("and",0),0x60:("or",0),0x61:("xor",0),
0x62:("shl",0),0x63:("shr",0),0x64:("shr.un",0),0x65:("neg",0),0x66:("not",0),
0x67:("conv.i1",0),0x68:("conv.i2",0),0x69:("conv.i4",0),0x6A:("conv.i8",0),
0x6B:("conv.r4",0),0x6C:("conv.r8",0),0x6D:("conv.u4",0),0x6E:("conv.u8",0),
0x6F:("callvirt",4),0x72:("ldstr",4),0x73:("newobj",4),0x74:("ldflda",4),
0x7B:("ldfld",4),0x7C:("ldflda2",4),0x7D:("stfld",4),0x7E:("ldsfld",4),0x80:("stsfld",4),
0x8C:("box",4),0x8D:("newarr",4),0x8E:("ldlen",0),0x8F:("ldelema",4),0x90:("ldelem.i1",0),
0x91:("ldelem.u1",0),0x92:("ldelem.i2",0),0x93:("ldelem.u2",0),0x94:("ldelem.i4",0),
0x95:("ldelem.u4",0),0x96:("ldelem.i8",0),0x97:("ldelem.r4",0),0x98:("ldelem.r8",0),
0x9A:("ldelem.ref",0),0x9B:("stelem.i",0),0x9C:("stelem.i1",0),0x9D:("stelem.i2",0),
0x9E:("stelem.i4",0),0x9F:("stelem.i8",0),0xA0:("stelem.r4",0),0xA1:("stelem.r8",0),
0xA2:("stelem.ref",0),0xA3:("ldelem",4),0xA4:("stelem",4),0xA5:("unbox.any",4),
0xD0:("ldtoken",4),0xDD:("leave",4),0xDE:("leave.s",1),
}.items():
    op(c, n, o)

# two-byte 0xFE prefixed
FE = {0x01:("ceq",0),0x02:("cgt",0),0x03:("cgt.un",0),0x04:("clt",0),0x05:("clt.un",0),
0x06:("ldftn",4),0x07:("ldvirtftn",4),0x09:("ldarg",2),0x0B:("ldloc",2),0x0D:("stloc",2),
0x0F:("endfilter",0),0x11:("unaligned",1),0x12:("volatile",0),0x13:("tail",0),0x14:("initobj",4),
0x15:("constrained",4),0x16:("cpblk",0),0x17:("initblk",0),0x19:("sizeof",4)}

def member_name(tok):
    try:
        tbl = (tok >> 24) & 0xFF
        idx = tok & 0xFFFFFF
        if tbl == 0x0A:  # MemberRef
            r = md.MemberRef.rows[idx - 1]
            cls = r.Class.row if hasattr(r, "Class") and r.Class else None
            clsname = str(cls.TypeName) if cls and hasattr(cls, "TypeName") else "?"
            return f"{clsname}::{str(r.Name)}"
        if tbl == 0x06:  # MethodDef
            r = md.MethodDef.rows[idx - 1]
            return f"mdef::{str(r.Name)}"
        if tbl == 0x04:  # Field
            r = md.Field.rows[idx - 1]
            return f"fld::{str(r.Name)}"
        if tbl == 0x01:  # TypeRef
            r = md.TypeRef.rows[idx - 1]
            return f"tyref::{str(r.TypeNamespace)}.{str(r.TypeName)}"
        if tbl == 0x02:
            r = md.TypeDef.rows[idx - 1]
            return f"tydef::{str(r.TypeName)}"
    except Exception as e:
        return f"tok0x{tok:08X}?{e}"
    return f"tok0x{tok:08X}"

def dump_method(rva):
    # method body: tiny (1 byte header) or fat
    rva_off = pe.get_offset_from_rva(rva)
    b0 = pe.__data__[rva_off]
    if (b0 & 3) == 2:
        code_size = b0 >> 2
        start = rva_off + 1
    else:
        flags_hf = struct.unpack_from("<H", pe.__data__, rva_off)[0]
        hsize = (flags_hf >> 12) * 4
        code_size = struct.unpack_from("<I", pe.__data__, rva_off + 4)[0]
        start = rva_off + hsize
    data = pe.__data__[start:start + code_size]
    out = []
    i = 0
    while i < len(data):
        c = data[i]
        if c == 0xFE:
            name, osize = FE.get(data[i+1], (f"fe.{data[i+1]:02x}", 0))
            i += 2
        else:
            name, osize = OPS.get(c, (f"op{c:02x}", 0))
            i += 1
        argtxt = ""
        if osize == 4 and i + 4 <= len(data):
            tok = struct.unpack_from("<I", data, i)[0]
            argtxt = member_name(tok)
            i += 4
        elif osize == 2 and i + 2 <= len(data):
            v = struct.unpack_from("<H", data, i)[0]
            argtxt = str(v)
            i += 2
        elif osize == 1 and i < len(data):
            argtxt = str(data[i]); i += 1
        elif osize == 8 and i + 8 <= len(data):
            argtxt = str(struct.unpack_from("<q", data, i)[0]); i += 8
        out.append(f"  {name} {argtxt}")
    return "\n".join(out)

target = sys.argv[1] if len(sys.argv) > 1 else "EnetPeer"
method_filter = sys.argv[2] if len(sys.argv) > 2 else None

for t in md.TypeDef.rows:
    if str(t.TypeName) != target:
        continue
    print(f"=== TYPE {t.TypeNamespace}.{t.TypeName} ===")
    for m in t.MethodList:
        r = m.row
        name = str(r.Name)
        if method_filter and method_filter.lower() not in name.lower():
            continue
        print(f"--- {name} ---")
        if r.Rva:
            try:
                print(dump_method(r.Rva))
            except Exception as e:
                print("  <error>", e)
        else:
            print("  <no body>")
