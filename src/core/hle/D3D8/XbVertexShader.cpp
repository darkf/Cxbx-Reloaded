// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2004 Aaron Robinson <caustik@caustik.com>
// *                Kingofc <kingofc@freenet.de>
// *
// *  All rights reserved
// *
// ******************************************************************
#define LOG_PREFIX CXBXR_MODULE::VTXSH

#define _DEBUG_TRACK_VS

#include "core\kernel\init\CxbxKrnl.h"
#include "core\kernel\support\Emu.h"
#include "core\hle\D3D8\Direct3D9\Direct3D9.h" // For g_Xbox_VertexShader_Handle
#include "core\hle\D3D8\XbVertexShader.h"

#include "XbD3D8Types.h" // For X_D3DVSDE_*
#include <sstream>
#include <regex>
#include <unordered_map>
#include <array>
#include <bitset>

#define DbgVshPrintf \
	LOG_CHECK_ENABLED(LOG_LEVEL::DEBUG) \
		if(g_bPrintfOn) printf

// ****************************************************************************
// * Vertex shader function recompiler
// ****************************************************************************

typedef enum _VSH_SWIZZLE
{
	SWIZZLE_X = 0,
	SWIZZLE_Y,
	SWIZZLE_Z,
	SWIZZLE_W
}
VSH_SWIZZLE;

typedef DWORD DxbxMask,
*PDxbxMask;

#define MASK_X 0x001
#define MASK_Y 0x002
#define MASK_Z 0x004
#define MASK_W 0x008
#define MASK_XYZ MASK_X | MASK_Y | MASK_Z
#define MASK_XYZW MASK_X | MASK_Y | MASK_Z | MASK_W

// Local types
typedef enum _VSH_FIELD_NAME
{
    FLD_ILU = 0,
    FLD_MAC,
    FLD_CONST,
    FLD_V,
    // Input A
    FLD_A_NEG,
    FLD_A_SWZ_X,
    FLD_A_SWZ_Y,
    FLD_A_SWZ_Z,
    FLD_A_SWZ_W,
    FLD_A_R,
    FLD_A_MUX,
    // Input B
    FLD_B_NEG,
    FLD_B_SWZ_X,
    FLD_B_SWZ_Y,
    FLD_B_SWZ_Z,
    FLD_B_SWZ_W,
    FLD_B_R,
    FLD_B_MUX,
    // Input C
    FLD_C_NEG,
    FLD_C_SWZ_X,
    FLD_C_SWZ_Y,
    FLD_C_SWZ_Z,
    FLD_C_SWZ_W,
    FLD_C_R_HIGH,
    FLD_C_R_LOW,
    FLD_C_MUX,
    // Output
    FLD_OUT_MAC_MASK_X,
    FLD_OUT_MAC_MASK_Y,
    FLD_OUT_MAC_MASK_Z,
    FLD_OUT_MAC_MASK_W,
    FLD_OUT_R,
    FLD_OUT_ILU_MASK_X,
    FLD_OUT_ILU_MASK_Y,
    FLD_OUT_ILU_MASK_Z,
    FLD_OUT_ILU_MASK_W,
    FLD_OUT_O_MASK_X,
    FLD_OUT_O_MASK_Y,
    FLD_OUT_O_MASK_Z,
    FLD_OUT_O_MASK_W,
    FLD_OUT_ORB,
    FLD_OUT_ADDRESS,
    FLD_OUT_MUX,
    // Relative addressing
    FLD_A0X,
    // Final instruction
    FLD_FINAL
}
VSH_FIELD_NAME;

typedef enum _VSH_OREG_NAME
{
	OREG_OPOS,    //  0
	OREG_UNUSED1, //  1
	OREG_UNUSED2, //  2
	OREG_OD0,     //  3
	OREG_OD1,     //  4
	OREG_OFOG,    //  5
	OREG_OPTS,    //  6
	OREG_OB0,     //  7
	OREG_OB1,     //  8
	OREG_OT0,     //  9
	OREG_OT1,     // 10
	OREG_OT2,     // 11
	OREG_OT3,     // 12
	OREG_UNUSED3, // 13
	OREG_UNUSED4, // 14
	OREG_A0X      // 15 - all values of the 4 bits are used
}
VSH_OREG_NAME;

typedef enum _VSH_OUTPUT_TYPE
{
    OUTPUT_C = 0,
    OUTPUT_O
}
VSH_OUTPUT_TYPE;

typedef enum _VSH_ARGUMENT_TYPE
{
    PARAM_UNKNOWN = 0,
    PARAM_R,          // Temporary (scRatch) registers
    PARAM_V,          // Vertex registers
    PARAM_C,          // Constant registers, set by SetVertexShaderConstant
    PARAM_O // = 0??
}
VSH_ARGUMENT_TYPE;

typedef VSH_ARGUMENT_TYPE VSH_PARAMETER_TYPE; // Alias, to indicate difference between a parameter and a generic argument

typedef enum _VSH_OUTPUT_MUX
{
    OMUX_MAC = 0,
    OMUX_ILU
}
VSH_OUTPUT_MUX;

typedef enum _VSH_IMD_OUTPUT_TYPE
{
    IMD_OUTPUT_C,
    IMD_OUTPUT_R,
    IMD_OUTPUT_O,
    IMD_OUTPUT_A0X
}
VSH_IMD_OUTPUT_TYPE;

// Dxbx note : ILU stands for 'Inverse Logic Unit' opcodes
typedef enum _VSH_ILU
{
    ILU_NOP = 0,
    ILU_MOV,
    ILU_RCP,
    ILU_RCC,
    ILU_RSQ,
    ILU_EXP,
    ILU_LOG,
    ILU_LIT // = 7 - all values of the 3 bits are used
}
VSH_ILU;

// Dxbx note : MAC stands for 'Multiply And Accumulate' opcodes
typedef enum _VSH_MAC
{
    MAC_NOP = 0,
    MAC_MOV,
    MAC_MUL,
    MAC_ADD,
    MAC_MAD,
    MAC_DP3,
    MAC_DPH,
    MAC_DP4,
    MAC_DST,
    MAC_MIN,
    MAC_MAX,
    MAC_SLT,
    MAC_SGE,
    MAC_ARL
	// ??? 14
	// ??? 15 - 2 values of the 4 bits are undefined
}
VSH_MAC;

typedef struct _VSH_PARAMETER
{
    VSH_PARAMETER_TYPE  ParameterType;   // Parameter type, R, V or C
    boolean             Neg;             // TRUE if negated, FALSE if not
    VSH_SWIZZLE         Swizzle[4];      // The four swizzles
    int16_t             Address;         // Register address
}
VSH_PARAMETER;

typedef struct _VSH_OUTPUT
{
    // Output register
    VSH_OUTPUT_MUX      OutputMux;       // MAC or ILU used as output
	VSH_OUTPUT_TYPE     OutputType;      // C or O
    boolean             OutputMask[4];
    int16_t             OutputAddress;
    // MAC output R register
    boolean             MACRMask[4];
    int16_t             MACRAddress;
    // ILU output R register
    boolean             ILURMask[4];
    int16_t             ILURAddress;
}
VSH_OUTPUT;

// The raw, parsed shader instruction (can be many combined [paired] instructions)
typedef struct _VSH_SHADER_INSTRUCTION
{
    VSH_ILU       ILU;
    VSH_MAC       MAC;
    VSH_OUTPUT    Output;
    VSH_PARAMETER A;
    VSH_PARAMETER B;
    VSH_PARAMETER C;
    boolean       a0x;
    boolean       Final;
}
VSH_SHADER_INSTRUCTION;

typedef enum _VSH_IMD_INSTRUCTION_TYPE
{
    IMD_MAC,
    IMD_ILU
}
VSH_IMD_INSTRUCTION_TYPE;

typedef struct _VSH_IMD_OUTPUT
{
    VSH_IMD_OUTPUT_TYPE Type;
    boolean             Mask[4];
    int16_t             Address;
}
VSH_IMD_OUTPUT;

typedef struct _VSH_IMD_PARAMETER
{
    boolean         Active;
    VSH_PARAMETER   Parameter;
	// There is only a single address register in Microsoft DirectX 8.0.
	// The address register, designated as a0.x, may be used as signed
	// integer offset in relative addressing into the constant register file.
	//     c[a0.x + n]
	boolean         IndexesWithA0_X;
}
VSH_IMD_PARAMETER;

typedef struct _VSH_INTERMEDIATE_FORMAT
{

    boolean                  IsCombined;
    VSH_IMD_INSTRUCTION_TYPE InstructionType;
    VSH_MAC                  MAC;
    VSH_ILU                  ILU;
    VSH_IMD_OUTPUT           Output;
    VSH_IMD_PARAMETER        Parameters[3];
}
VSH_INTERMEDIATE_FORMAT;

// Used for xvu spec definition
typedef struct _VSH_FIELDMAPPING
{
    VSH_FIELD_NAME  FieldName;
    uint8_t          SubToken;
    uint8_t          StartBit;
    uint8_t          BitLength;
}
VSH_FIELDMAPPING;

typedef struct _VSH_XBOX_SHADER
{
    XTL::X_VSH_SHADER_HEADER       ShaderHeader;
    uint16_t                IntermediateCount;
    VSH_INTERMEDIATE_FORMAT Intermediate[VSH_MAX_INTERMEDIATE_COUNT];
}
VSH_XBOX_SHADER;

// Local constants
static const VSH_FIELDMAPPING g_FieldMapping[] =
{
    // Field Name         DWORD BitPos BitSize
    {  FLD_ILU,              1,   25,     3 },
    {  FLD_MAC,              1,   21,     4 },
    {  FLD_CONST,            1,   13,     8 },
    {  FLD_V,                1,    9,     4 },
    // Input A
    {  FLD_A_NEG,            1,    8,     1 },
    {  FLD_A_SWZ_X,          1,    6,     2 },
    {  FLD_A_SWZ_Y,          1,    4,     2 },
    {  FLD_A_SWZ_Z,          1,    2,     2 },
    {  FLD_A_SWZ_W,          1,    0,     2 },
    {  FLD_A_R,              2,   28,     4 },
    {  FLD_A_MUX,            2,   26,     2 },
    // Input B
    {  FLD_B_NEG,            2,   25,     1 },
    {  FLD_B_SWZ_X,          2,   23,     2 },
    {  FLD_B_SWZ_Y,          2,   21,     2 },
    {  FLD_B_SWZ_Z,          2,   19,     2 },
    {  FLD_B_SWZ_W,          2,   17,     2 },
    {  FLD_B_R,              2,   13,     4 },
    {  FLD_B_MUX,            2,   11,     2 },
    // Input C
    {  FLD_C_NEG,            2,   10,     1 },
    {  FLD_C_SWZ_X,          2,    8,     2 },
    {  FLD_C_SWZ_Y,          2,    6,     2 },
    {  FLD_C_SWZ_Z,          2,    4,     2 },
    {  FLD_C_SWZ_W,          2,    2,     2 },
    {  FLD_C_R_HIGH,         2,    0,     2 },
    {  FLD_C_R_LOW,          3,   30,     2 },
    {  FLD_C_MUX,            3,   28,     2 },
    // Output
    {  FLD_OUT_MAC_MASK_X,   3,   27,     1 },
    {  FLD_OUT_MAC_MASK_Y,   3,   26,     1 },
    {  FLD_OUT_MAC_MASK_Z,   3,   25,     1 },
    {  FLD_OUT_MAC_MASK_W,   3,   24,     1 },
    {  FLD_OUT_R,            3,   20,     4 },
    {  FLD_OUT_ILU_MASK_X,   3,   19,     1 },
    {  FLD_OUT_ILU_MASK_Y,   3,   18,     1 },
    {  FLD_OUT_ILU_MASK_Z,   3,   17,     1 },
    {  FLD_OUT_ILU_MASK_W,   3,   16,     1 },
    {  FLD_OUT_O_MASK_X,     3,   15,     1 },
    {  FLD_OUT_O_MASK_Y,     3,   14,     1 },
    {  FLD_OUT_O_MASK_Z,     3,   13,     1 },
    {  FLD_OUT_O_MASK_W,     3,   12,     1 },
    {  FLD_OUT_ORB,          3,   11,     1 },
    {  FLD_OUT_ADDRESS,      3,    3,     8 },
    {  FLD_OUT_MUX,          3,    2,     1 },
	// Relative addressing
    {  FLD_A0X,              3,    1,     1 },
	// Final instruction
	{  FLD_FINAL,            3,    0,     1 }
};

static const char* OReg_Name[] =
{
    "oPos",
    "???",
    "???",
    "oD0",
    "oD1",
    "oFog",
    "oPts",
    "oB0",
    "oB1",
    "oT0",
    "oT1",
    "oT2",
    "oT3",
    "???",
    "???",
    "a0.x"
};

// TODO : Reinstate and use : std::array<bool, 16> RegVIsPresentInDeclaration;

/* TODO : map non-FVF Xbox vertex shader handle to CxbxVertexShader (a struct containing a host Xbox vertex shader handle and the original members)
std::unordered_map<DWORD, CxbxVertexShader> g_CxbxVertexShaders;

void CxbxUpdateVertexShader(DWORD XboxVertexShaderHandle)
{
	CxbxVertexShader &VertexShader = g_CxbxVertexShaders[XboxVertexShaderHandle];
}*/

static inline int IsInUse(const boolean *pMask)
{
    return (pMask[0] || pMask[1] || pMask[2] || pMask[3]);
}

static inline boolean HasMACR(VSH_SHADER_INSTRUCTION *pInstruction)
{
    return IsInUse(pInstruction->Output.MACRMask) && pInstruction->MAC != MAC_NOP;
}

static inline boolean HasMACO(VSH_SHADER_INSTRUCTION *pInstruction)
{
    return IsInUse(pInstruction->Output.OutputMask) &&
            pInstruction->Output.OutputMux == OMUX_MAC &&
            pInstruction->MAC != MAC_NOP;
}

static inline boolean HasMACARL(VSH_SHADER_INSTRUCTION *pInstruction)
{
    return /*!IsInUse(pInstruction->Output.OutputMask) &&
            pInstruction->Output.OutputMux == OMUX_MAC &&*/
            pInstruction->MAC == MAC_ARL;
}

static inline boolean HasILUR(VSH_SHADER_INSTRUCTION *pInstruction)
{
    return IsInUse(pInstruction->Output.ILURMask) && pInstruction->ILU != ILU_NOP;
}

static inline boolean HasILUO(VSH_SHADER_INSTRUCTION *pInstruction)
{
    return IsInUse(pInstruction->Output.OutputMask) &&
            pInstruction->Output.OutputMux == OMUX_ILU &&
            pInstruction->ILU != ILU_NOP;
}

// Retrieves a number of bits in the instruction token
static inline int VshGetFromToken(uint32_t *pShaderToken,
                                  uint8_t SubToken,
                                  uint8_t StartBit,
                                  uint8_t BitLength)
{
    return (pShaderToken[SubToken] >> StartBit) & ~(0xFFFFFFFF << BitLength);
}

// Converts the C register address to disassembly format
static inline int16_t ConvertCRegister(const int16_t CReg)
{
    return ((((CReg >> 5) & 7) - 3) * 32) + (CReg & 31);
}

uint8_t VshGetField(uint32_t         *pShaderToken,
                   VSH_FIELD_NAME FieldName)
{
    return (uint8_t)(VshGetFromToken(pShaderToken,
                                   g_FieldMapping[FieldName].SubToken,
                                   g_FieldMapping[FieldName].StartBit,
                                   g_FieldMapping[FieldName].BitLength));
}

static void VshParseInstruction(uint32_t               *pShaderToken,
                                VSH_SHADER_INSTRUCTION *pInstruction)
{
    // First get the instruction(s).
    pInstruction->ILU = (VSH_ILU)VshGetField(pShaderToken, FLD_ILU);
    pInstruction->MAC = (VSH_MAC)VshGetField(pShaderToken, FLD_MAC);

    // Get parameter A
    pInstruction->A.ParameterType = (VSH_PARAMETER_TYPE)VshGetField(pShaderToken, FLD_A_MUX);
    switch(pInstruction->A.ParameterType)
    {
    case PARAM_R:
        pInstruction->A.Address = VshGetField(pShaderToken, FLD_A_R);
        break;
    case PARAM_V:
        pInstruction->A.Address = VshGetField(pShaderToken, FLD_V);
        break;
    case PARAM_C:
        pInstruction->A.Address = ConvertCRegister(VshGetField(pShaderToken, FLD_CONST));
        break;
    default:
        EmuLog(LOG_LEVEL::WARNING, "Invalid instruction, parameter A type unknown %d", pInstruction->A.ParameterType);
        return;
    }
    pInstruction->A.Neg = VshGetField(pShaderToken, FLD_A_NEG);
    pInstruction->A.Swizzle[0] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_A_SWZ_X);
    pInstruction->A.Swizzle[1] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_A_SWZ_Y);
    pInstruction->A.Swizzle[2] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_A_SWZ_Z);
    pInstruction->A.Swizzle[3] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_A_SWZ_W);
    // Get parameter B
    pInstruction->B.ParameterType = (VSH_PARAMETER_TYPE)VshGetField(pShaderToken, FLD_B_MUX);
    switch(pInstruction->B.ParameterType)
    {
    case PARAM_R:
        pInstruction->B.Address = VshGetField(pShaderToken, FLD_B_R);
        break;
    case PARAM_V:
        pInstruction->B.Address = VshGetField(pShaderToken, FLD_V);
        break;
    case PARAM_C:
        pInstruction->B.Address = ConvertCRegister(VshGetField(pShaderToken, FLD_CONST));
        break;
    default:
        DbgVshPrintf("Invalid instruction, parameter B type unknown %d\n", pInstruction->B.ParameterType);
        return;
    }
    pInstruction->B.Neg = VshGetField(pShaderToken, FLD_B_NEG);
    pInstruction->B.Swizzle[0] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_B_SWZ_X);
    pInstruction->B.Swizzle[1] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_B_SWZ_Y);
    pInstruction->B.Swizzle[2] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_B_SWZ_Z);
    pInstruction->B.Swizzle[3] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_B_SWZ_W);
    // Get parameter C
    pInstruction->C.ParameterType = (VSH_PARAMETER_TYPE)VshGetField(pShaderToken, FLD_C_MUX);
    switch(pInstruction->C.ParameterType)
    {
    case PARAM_R:
        pInstruction->C.Address = VshGetField(pShaderToken, FLD_C_R_HIGH) << 2 |
                                  VshGetField(pShaderToken, FLD_C_R_LOW);
        break;
    case PARAM_V:
        pInstruction->C.Address = VshGetField(pShaderToken, FLD_V);
        break;
    case PARAM_C:
        pInstruction->C.Address = ConvertCRegister(VshGetField(pShaderToken, FLD_CONST));
        break;
    default:
        DbgVshPrintf("Invalid instruction, parameter C type unknown %d\n", pInstruction->C.ParameterType);
        return;
    }
    pInstruction->C.Neg = VshGetField(pShaderToken, FLD_C_NEG);
    pInstruction->C.Swizzle[0] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_C_SWZ_X);
    pInstruction->C.Swizzle[1] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_C_SWZ_Y);
    pInstruction->C.Swizzle[2] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_C_SWZ_Z);
    pInstruction->C.Swizzle[3] = (VSH_SWIZZLE)VshGetField(pShaderToken, FLD_C_SWZ_W);
    // Get output
    // Output register
    pInstruction->Output.OutputType = (VSH_OUTPUT_TYPE)VshGetField(pShaderToken, FLD_OUT_ORB);
    switch(pInstruction->Output.OutputType)
    {
    case OUTPUT_C:
        pInstruction->Output.OutputAddress = ConvertCRegister(VshGetField(pShaderToken, FLD_OUT_ADDRESS));
        break;
    case OUTPUT_O:
        pInstruction->Output.OutputAddress = VshGetField(pShaderToken, FLD_OUT_ADDRESS) & 0xF;
        break;
    }
    pInstruction->Output.OutputMux = (VSH_OUTPUT_MUX)VshGetField(pShaderToken, FLD_OUT_MUX);
    pInstruction->Output.OutputMask[0] = VshGetField(pShaderToken, FLD_OUT_O_MASK_X);
    pInstruction->Output.OutputMask[1] = VshGetField(pShaderToken, FLD_OUT_O_MASK_Y);
    pInstruction->Output.OutputMask[2] = VshGetField(pShaderToken, FLD_OUT_O_MASK_Z);
    pInstruction->Output.OutputMask[3] = VshGetField(pShaderToken, FLD_OUT_O_MASK_W);
    // MAC output
    pInstruction->Output.MACRMask[0] = VshGetField(pShaderToken, FLD_OUT_MAC_MASK_X);
    pInstruction->Output.MACRMask[1] = VshGetField(pShaderToken, FLD_OUT_MAC_MASK_Y);
    pInstruction->Output.MACRMask[2] = VshGetField(pShaderToken, FLD_OUT_MAC_MASK_Z);
    pInstruction->Output.MACRMask[3] = VshGetField(pShaderToken, FLD_OUT_MAC_MASK_W);
    pInstruction->Output.MACRAddress = VshGetField(pShaderToken, FLD_OUT_R);
    // ILU output
    pInstruction->Output.ILURMask[0] = VshGetField(pShaderToken, FLD_OUT_ILU_MASK_X);
    pInstruction->Output.ILURMask[1] = VshGetField(pShaderToken, FLD_OUT_ILU_MASK_Y);
    pInstruction->Output.ILURMask[2] = VshGetField(pShaderToken, FLD_OUT_ILU_MASK_Z);
    pInstruction->Output.ILURMask[3] = VshGetField(pShaderToken, FLD_OUT_ILU_MASK_W);
    pInstruction->Output.ILURAddress = VshGetField(pShaderToken, FLD_OUT_R);
    // Finally, get a0.x indirect constant addressing
    pInstruction->a0x = VshGetField(pShaderToken, FLD_A0X);
    pInstruction->Final = VshGetField(pShaderToken, FLD_FINAL);
}

// Print functions
static char *VshGetRegisterName(VSH_PARAMETER_TYPE ParameterType)
{
    switch(ParameterType)
    {
    case PARAM_R:
        return "r";
    case PARAM_V:
        return "v";
    case PARAM_C:
        return "c";
	case PARAM_O:
		return "oPos";
	default:
        return "?";
    }
}

char* XboxVertexRegisterAsString(DWORD VertexRegister)
{
	switch (VertexRegister)
	{
	case XTL::X_D3DVSDE_VERTEX: // -1
		return "D3DVSDE_VERTEX /* xbox ext. */";
	case XTL::X_D3DVSDE_POSITION: // 0
		return "D3DVSDE_POSITION";
	case XTL::X_D3DVSDE_BLENDWEIGHT: // 1
		return "D3DVSDE_BLENDWEIGHT";
	case XTL::X_D3DVSDE_NORMAL: // 2
		return "D3DVSDE_NORMAL";
	case XTL::X_D3DVSDE_DIFFUSE: // 3
		return "D3DVSDE_DIFFUSE";
	case XTL::X_D3DVSDE_SPECULAR: // 4
		return "D3DVSDE_SPECULAR";
	case XTL::X_D3DVSDE_FOG: // 5
		return "D3DVSDE_FOG";
	case XTL::X_D3DVSDE_POINTSIZE: // 6
		return "D3DVDSE_POINTSIZE";
	case XTL::X_D3DVSDE_BACKDIFFUSE: // 7
		return "D3DVSDE_BACKDIFFUSE /* xbox ext. */";
	case XTL::X_D3DVSDE_BACKSPECULAR: // 8
		return "D3DVSDE_BACKSPECULAR /* xbox ext. */";
	case XTL::X_D3DVSDE_TEXCOORD0: // 9
		return "D3DVSDE_TEXCOORD0";
	case XTL::X_D3DVSDE_TEXCOORD1: // 10
		return "D3DVSDE_TEXCOORD1";
	case XTL::X_D3DVSDE_TEXCOORD2: // 11
		return "D3DVSDE_TEXCOORD2";
	case XTL::X_D3DVSDE_TEXCOORD3: // 12
		return "D3DVSDE_TEXCOORD3";
	case 13:
		return "13 /* unknown register */";
	case 14:
		return "14 /* unknown register */";
	case 15:
		return "15 /* unknown register */";
	default:
		return "16 /* or higher, unknown register */";
	}
}

#define D3DDECLUSAGE_UNSUPPORTED ((D3DDECLUSAGE)-1)

D3DDECLUSAGE Xb2PCRegisterType
(
	DWORD VertexRegister,
	BYTE& PCUsageIndex
)
{
	D3DDECLUSAGE PCRegisterType;
	PCUsageIndex = 0;

	switch (VertexRegister)
	{
	case XTL::X_D3DVSDE_VERTEX: // -1
		PCRegisterType = D3DDECLUSAGE_UNSUPPORTED;
		break;
	case XTL::X_D3DVSDE_POSITION: // 0
		PCRegisterType = D3DDECLUSAGE_POSITION;
		break;
	case XTL::X_D3DVSDE_BLENDWEIGHT: // 1
		PCRegisterType = D3DDECLUSAGE_BLENDWEIGHT;
		break;
	case XTL::X_D3DVSDE_NORMAL: // 2
		PCRegisterType = D3DDECLUSAGE_NORMAL;
		break;
	case XTL::X_D3DVSDE_DIFFUSE: // 3
		PCRegisterType = D3DDECLUSAGE_COLOR; PCUsageIndex = 0;
		break;
	case XTL::X_D3DVSDE_SPECULAR: // 4
		PCRegisterType = D3DDECLUSAGE_COLOR; PCUsageIndex = 1;
		break;
	case XTL::X_D3DVSDE_FOG: // 5
		PCRegisterType = D3DDECLUSAGE_FOG;
		break;
	case XTL::X_D3DVSDE_POINTSIZE: // 6
		PCRegisterType = D3DDECLUSAGE_PSIZE;
		break;
	case XTL::X_D3DVSDE_BACKDIFFUSE: // 7
		PCRegisterType = D3DDECLUSAGE_COLOR; PCUsageIndex = 2;
		break;
	case XTL::X_D3DVSDE_BACKSPECULAR: // 8
		PCRegisterType = D3DDECLUSAGE_COLOR; PCUsageIndex = 3;
		break;
	case XTL::X_D3DVSDE_TEXCOORD0: // 9
		PCRegisterType = D3DDECLUSAGE_TEXCOORD; PCUsageIndex = 0;
		break;
	case XTL::X_D3DVSDE_TEXCOORD1: // 10
		PCRegisterType = D3DDECLUSAGE_TEXCOORD; PCUsageIndex = 1;
		break;
	case XTL::X_D3DVSDE_TEXCOORD2: // 11
		PCRegisterType = D3DDECLUSAGE_TEXCOORD; PCUsageIndex = 2;
		break;
	case XTL::X_D3DVSDE_TEXCOORD3: // 12
		PCRegisterType = D3DDECLUSAGE_TEXCOORD; PCUsageIndex = 3;
		break;
	default:
		PCRegisterType = D3DDECLUSAGE_UNSUPPORTED;
		break;
	}

	return PCRegisterType;
}

extern D3DCAPS g_D3DCaps;

static void VshAddParameter(VSH_PARAMETER     *pParameter,
                            boolean           a0x,
                            VSH_IMD_PARAMETER *pIntermediateParameter)
{
    pIntermediateParameter->Parameter = *pParameter;
    pIntermediateParameter->Active    = TRUE;
    pIntermediateParameter->IndexesWithA0_X     = a0x;
}

static void VshAddParameters(VSH_SHADER_INSTRUCTION  *pInstruction,
                             VSH_ILU                 ILU,
                             VSH_MAC                 MAC,
                             VSH_IMD_PARAMETER       *pParameters)
{
    uint8_t ParamCount = 0;
	
    if(MAC >= MAC_MOV)
    {
        VshAddParameter(&pInstruction->A, pInstruction->a0x, &pParameters[ParamCount]);
        ParamCount++;
    }

    if((MAC == MAC_MUL) || ((MAC >= MAC_MAD) && (MAC <= MAC_SGE)))
    {
        VshAddParameter(&pInstruction->B, pInstruction->a0x, &pParameters[ParamCount]);
        ParamCount++;
    }

    if((ILU >= ILU_MOV) || (MAC == MAC_ADD) || (MAC == MAC_MAD))
    {
        VshAddParameter(&pInstruction->C, pInstruction->a0x, &pParameters[ParamCount]);
        ParamCount++;
    }
}

static void VshVerifyBufferBounds(VSH_XBOX_SHADER *pShader)
{
    if(pShader->IntermediateCount >= VSH_MAX_INTERMEDIATE_COUNT)
    {
        CxbxKrnlCleanup("Shader exceeds conversion buffer!");
    }
}

static VSH_INTERMEDIATE_FORMAT *VshNewIntermediate(VSH_XBOX_SHADER *pShader)
{
    VshVerifyBufferBounds(pShader);

    ZeroMemory(&pShader->Intermediate[pShader->IntermediateCount], sizeof(VSH_INTERMEDIATE_FORMAT));

    return &pShader->Intermediate[pShader->IntermediateCount++];
}

static boolean VshAddInstructionMAC_R(VSH_SHADER_INSTRUCTION *pInstruction,
                                      VSH_XBOX_SHADER        *pShader,
                                      boolean                IsCombined)
{
    VSH_INTERMEDIATE_FORMAT *pIntermediate;
    if(!HasMACR(pInstruction))
    {
        return FALSE;
    }

    pIntermediate = VshNewIntermediate(pShader);
    pIntermediate->IsCombined = IsCombined;

    // Opcode
    pIntermediate->InstructionType = IMD_MAC;
    pIntermediate->MAC = pInstruction->MAC;

    // Output param
    pIntermediate->Output.Type = IMD_OUTPUT_R;
    pIntermediate->Output.Address = pInstruction->Output.MACRAddress;
    memcpy(pIntermediate->Output.Mask, pInstruction->Output.MACRMask, sizeof(boolean) * 4);

    // Other parameters
    VshAddParameters(pInstruction, ILU_NOP, pInstruction->MAC, pIntermediate->Parameters);

    return TRUE;
}

static boolean VshAddInstructionMAC_O(VSH_SHADER_INSTRUCTION* pInstruction,
                                      VSH_XBOX_SHADER        *pShader,
                                      boolean                IsCombined)
{
    VSH_INTERMEDIATE_FORMAT *pIntermediate;
    if(!HasMACO(pInstruction))
    {
        return FALSE;
    }

    pIntermediate = VshNewIntermediate(pShader);
    pIntermediate->IsCombined = IsCombined;

    // Opcode
    pIntermediate->InstructionType = IMD_MAC;
    pIntermediate->MAC = pInstruction->MAC;

    // Output param
	pIntermediate->Output.Type = pInstruction->Output.OutputType == OUTPUT_C ? IMD_OUTPUT_C : IMD_OUTPUT_O;
    pIntermediate->Output.Address = pInstruction->Output.OutputAddress;
    memcpy(pIntermediate->Output.Mask, pInstruction->Output.OutputMask, sizeof(boolean) * 4);

    // Other parameters
    VshAddParameters(pInstruction, ILU_NOP, pInstruction->MAC, pIntermediate->Parameters);

    return TRUE;
}

static boolean VshAddInstructionMAC_ARL(VSH_SHADER_INSTRUCTION *pInstruction,
                                        VSH_XBOX_SHADER        *pShader,
                                        boolean                IsCombined)
{
    VSH_INTERMEDIATE_FORMAT *pIntermediate;
    if(!HasMACARL(pInstruction))
    {
        return FALSE;
    }

    pIntermediate = VshNewIntermediate(pShader);
    pIntermediate->IsCombined = IsCombined;

    // Opcode
    pIntermediate->InstructionType = IMD_MAC;
    pIntermediate->MAC = pInstruction->MAC;

    // Output param
    pIntermediate->Output.Type = IMD_OUTPUT_A0X;
    pIntermediate->Output.Address = pInstruction->Output.OutputAddress;

    // Other parameters
    VshAddParameters(pInstruction, ILU_NOP, pInstruction->MAC, pIntermediate->Parameters);

    return TRUE;
}

static boolean VshAddInstructionILU_R(VSH_SHADER_INSTRUCTION *pInstruction,
                                      VSH_XBOX_SHADER        *pShader,
                                      boolean                IsCombined)
{
    VSH_INTERMEDIATE_FORMAT *pIntermediate;
    if(!HasILUR(pInstruction))
    {
        return FALSE;
    }

	pIntermediate = VshNewIntermediate(pShader);
    pIntermediate->IsCombined = IsCombined;

    // Opcode
    pIntermediate->InstructionType = IMD_ILU;
    pIntermediate->ILU = pInstruction->ILU;

    // Output param
    pIntermediate->Output.Type = IMD_OUTPUT_R;
    // If this is a combined instruction, only r1 is allowed (R address should not be used)
    pIntermediate->Output.Address = IsCombined ? 1 : pInstruction->Output.ILURAddress;
    memcpy(pIntermediate->Output.Mask, pInstruction->Output.ILURMask, sizeof(boolean) * 4);

    // Other parameters
    VshAddParameters(pInstruction, pInstruction->ILU, MAC_NOP, pIntermediate->Parameters);

    return TRUE;
}

static boolean VshAddInstructionILU_O(VSH_SHADER_INSTRUCTION *pInstruction,
                                      VSH_XBOX_SHADER        *pShader,
                                      boolean                IsCombined)
{
    VSH_INTERMEDIATE_FORMAT *pIntermediate;
    if(!HasILUO(pInstruction))
    {
        return FALSE;
    }

    pIntermediate = VshNewIntermediate(pShader);
    pIntermediate->IsCombined = IsCombined;

    // Opcode
    pIntermediate->InstructionType = IMD_ILU;
    pIntermediate->ILU = pInstruction->ILU;

    // Output param
    pIntermediate->Output.Type = pInstruction->Output.OutputType == OUTPUT_C ? IMD_OUTPUT_C : IMD_OUTPUT_O;
    pIntermediate->Output.Address = pInstruction->Output.OutputAddress;
    memcpy(pIntermediate->Output.Mask, pInstruction->Output.OutputMask, sizeof(boolean) * 4);

    // Other parameters
    VshAddParameters(pInstruction, pInstruction->ILU, MAC_NOP, pIntermediate->Parameters);

    return TRUE;
}

static void VshConvertToIntermediate(VSH_SHADER_INSTRUCTION *pInstruction,
                                     VSH_XBOX_SHADER        *pShader)
{
    // Five types of instructions:
    //   MAC
    //
    //   ILU
    //
    //   MAC
    //   +ILU
    //
    //   MAC
    //   +MAC
    //   +ILU
    //
    //   MAC
    //   +ILU
    //   +ILU
    boolean IsCombined = FALSE;

    if(VshAddInstructionMAC_R(pInstruction, pShader, IsCombined))
    {
        if(HasMACO(pInstruction) ||
            HasILUR(pInstruction) ||
            HasILUO(pInstruction))
        {
            IsCombined = TRUE;
        }
    }
    if(VshAddInstructionMAC_O(pInstruction, pShader, IsCombined))
    {
        if(HasILUR(pInstruction) ||
            HasILUO(pInstruction))
        {
            IsCombined = TRUE;
        }
    }
    // Special case, arl (mov a0.x, ...)
    if(VshAddInstructionMAC_ARL(pInstruction, pShader, IsCombined))
    {
        if(HasILUR(pInstruction) ||
            HasILUO(pInstruction))
        {
            IsCombined = TRUE;
        }
    }
    if(VshAddInstructionILU_R(pInstruction, pShader, IsCombined))
    {
        if(HasILUO(pInstruction))
        {
            IsCombined = TRUE;
        }
    }
    (void)VshAddInstructionILU_O(pInstruction, pShader, IsCombined);
}

static inline void VshSetSwizzle(VSH_PARAMETER *pParameter,
    VSH_SWIZZLE       x,
    VSH_SWIZZLE       y,
    VSH_SWIZZLE       z,
    VSH_SWIZZLE       w)
{
    pParameter->Swizzle[0] = x;
    pParameter->Swizzle[1] = y;
    pParameter->Swizzle[2] = z;
    pParameter->Swizzle[3] = w;
}

static inline void VshSetSwizzle(VSH_IMD_PARAMETER *pParameter,
                                 VSH_SWIZZLE       x,
                                 VSH_SWIZZLE       y,
                                 VSH_SWIZZLE       z,
                                 VSH_SWIZZLE       w)
{
    VshSetSwizzle(&pParameter->Parameter, x, y, z, w);
}

// ****************************************************************************
// * Vertex shader declaration recompiler
// ****************************************************************************

class XboxVertexDeclarationConverter
{
protected:
	// Internal variables
	CxbxVertexShaderInfo* pVertexShaderInfoToSet;
	CxbxVertexShaderStreamInfo* pCurrentVertexShaderStreamInfo = nullptr;
	DWORD hostTemporaryRegisterCount;
	bool IsFixedFunction;
	D3DVERTEXELEMENT* pRecompiled;

public:
	// Output
	DWORD XboxDeclarationCount;
	DWORD HostDeclarationSize;

private:
	// VERTEX SHADER

	static DWORD VshGetDeclarationCount(DWORD *pXboxDeclaration)
	{
		DWORD Pos = 0;
		while (pXboxDeclaration[Pos] != X_D3DVSD_END())
		{
			Pos++;
		}

		return Pos + 1;
	}

	static inline DWORD VshGetTokenType(DWORD XboxToken)
	{
		return (XboxToken & X_D3DVSD_TOKENTYPEMASK) >> X_D3DVSD_TOKENTYPESHIFT;
	}

	static inline WORD VshGetVertexStream(DWORD XboxToken)
	{
		return (XboxToken & X_D3DVSD_STREAMNUMBERMASK) >> X_D3DVSD_STREAMNUMBERSHIFT;
	}

	inline DWORD VshGetVertexRegister(DWORD XboxToken)
	{
		DWORD regNum = (XboxToken & X_D3DVSD_VERTEXREGMASK) >> X_D3DVSD_VERTEXREGSHIFT;
		if (regNum >= hostTemporaryRegisterCount /*12 for D3D8, D3D9 value depends on host GPU */) {
			// test-case : BLiNX: the time sweeper
			// test-case : Lego Star Wars
			LOG_TEST_CASE("RegNum > NumTemps");
		}
		return regNum;
	}

	inline DWORD VshGetVertexRegisterIn(DWORD XboxToken)
	{
		DWORD regNum = (XboxToken & X_D3DVSD_VERTEXREGINMASK) >> X_D3DVSD_VERTEXREGINSHIFT;
		if (regNum >= hostTemporaryRegisterCount /*12 for D3D8, D3D9 value depends on host GPU */) {
			// test-case : Lego Star Wars
			LOG_TEST_CASE("RegNum > NumTemps");
		}
		return regNum;
	}

	void VshDumpXboxDeclaration(DWORD* pXboxDeclaration)
	{
		DbgVshPrintf("DWORD dwVSHDecl[] =\n{\n");
		unsigned iNumberOfVertexStreams = 0;
		bool bStreamNeedsPatching = false;
		auto pXboxToken = pXboxDeclaration;
		while (*pXboxToken != X_D3DVSD_END()) // X_D3DVSD_TOKEN_END 
		{
			DWORD Step = 1;

			switch (VshGetTokenType(*pXboxToken)) {
			case XTL::X_D3DVSD_TOKEN_NOP: {
				DbgVshPrintf("\tD3DVSD_NOP(),\n");
				break;
			}
			case XTL::X_D3DVSD_TOKEN_STREAM: {
				if (*pXboxToken & X_D3DVSD_STREAMTESSMASK) {
					DbgVshPrintf("\tD3DVSD_STREAM_TESS(),\n");
				} else {
					if (iNumberOfVertexStreams > 0) {
						DbgVshPrintf("\t// NeedPatching: %d\n", bStreamNeedsPatching);
					}
					DWORD StreamNumber = VshGetVertexStream(*pXboxToken);
					DbgVshPrintf("\tD3DVSD_STREAM(%u),\n", StreamNumber);
					iNumberOfVertexStreams++;
					bStreamNeedsPatching = false;
				}
				break;
			}
			case XTL::X_D3DVSD_TOKEN_STREAMDATA: {
				if (*pXboxToken & X_D3DVSD_MASK_SKIP) {
 					WORD SkipCount = (*pXboxToken & X_D3DVSD_SKIPCOUNTMASK) >> X_D3DVSD_SKIPCOUNTSHIFT;
					if (*pXboxToken & X_D3DVSD_MASK_SKIPBYTES) {
						DbgVshPrintf("\tD3DVSD_SKIPBYTES(%d), /* xbox ext. */\n", SkipCount);
					} else {
						DbgVshPrintf("\tD3DVSD_SKIP(%d),\n", SkipCount);
					}
				} else {
					DWORD VertexRegister = VshGetVertexRegister(*pXboxToken);
					if (IsFixedFunction) {
						DbgVshPrintf("\t\tD3DVSD_REG(%s, ", XboxVertexRegisterAsString(VertexRegister));
					} else {
						DbgVshPrintf("\t\tD3DVSD_REG(%d, ", (BYTE)VertexRegister);
					}

					DWORD XboxVertexElementDataType = (*pXboxToken & X_D3DVSD_DATATYPEMASK) >> X_D3DVSD_DATATYPESHIFT;
					switch (XboxVertexElementDataType) {
					case XTL::X_D3DVSDT_FLOAT1: // 0x12:
						DbgVshPrintf("D3DVSDT_FLOAT1");
						break;
					case XTL::X_D3DVSDT_FLOAT2: // 0x22:
						DbgVshPrintf("D3DVSDT_FLOAT2");
						break;
					case XTL::X_D3DVSDT_FLOAT3: // 0x32:
						DbgVshPrintf("D3DVSDT_FLOAT3");
						break;
					case XTL::X_D3DVSDT_FLOAT4: // 0x42:
						DbgVshPrintf("D3DVSDT_FLOAT4");
						break;
					case XTL::X_D3DVSDT_D3DCOLOR: // 0x40:
						DbgVshPrintf("D3DVSDT_D3DCOLOR");
						break;
					case XTL::X_D3DVSDT_SHORT2: // 0x25:
						DbgVshPrintf("D3DVSDT_SHORT2");
						break;
					case XTL::X_D3DVSDT_SHORT4: // 0x45:
						DbgVshPrintf("D3DVSDT_SHORT4");
						break;
					case XTL::X_D3DVSDT_NORMSHORT1: // 0x11:
						DbgVshPrintf("D3DVSDT_NORMSHORT1 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_NORMSHORT2: // 0x21:
						if (g_D3DCaps.DeclTypes & D3DDTCAPS_SHORT2N) {
							DbgVshPrintf("D3DVSDT_NORMSHORT2");
						} else {
							DbgVshPrintf("D3DVSDT_NORMSHORT2 /* xbox ext. */");
							bStreamNeedsPatching = true;
						}
						break;
					case XTL::X_D3DVSDT_NORMSHORT3: // 0x31:
						DbgVshPrintf("D3DVSDT_NORMSHORT3 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_NORMSHORT4: // 0x41:
						if (g_D3DCaps.DeclTypes & D3DDTCAPS_SHORT4N) {
							DbgVshPrintf("D3DVSDT_NORMSHORT4");
							// No need for patching in D3D9
						} else {
							DbgVshPrintf("D3DVSDT_NORMSHORT4 /* xbox ext. */");
							bStreamNeedsPatching = true;
						}
						break;
					case XTL::X_D3DVSDT_NORMPACKED3: // 0x16:
						DbgVshPrintf("D3DVSDT_NORMPACKED3 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_SHORT1: // 0x15:
						DbgVshPrintf("D3DVSDT_SHORT1 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_SHORT3: // 0x35:
						DbgVshPrintf("D3DVSDT_SHORT3 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_PBYTE1: // 0x14:
						DbgVshPrintf("D3DVSDT_PBYTE1 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_PBYTE2: // 0x24:
						DbgVshPrintf("D3DVSDT_PBYTE2 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_PBYTE3: // 0x34:
						DbgVshPrintf("D3DVSDT_PBYTE3 /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_PBYTE4: // 0x44:
						if (g_D3DCaps.DeclTypes & D3DDTCAPS_UBYTE4N) {
							DbgVshPrintf("D3DVSDT_PBYTE4");
						} else {
							DbgVshPrintf("D3DVSDT_PBYTE4 /* xbox ext. */");
							bStreamNeedsPatching = true;
						}
						break;
					case XTL::X_D3DVSDT_FLOAT2H: // 0x72:
						DbgVshPrintf("D3DVSDT_FLOAT2H /* xbox ext. */");
						bStreamNeedsPatching = true;
						break;
					case XTL::X_D3DVSDT_NONE: // 0x02:
						DbgVshPrintf("D3DVSDT_NONE /* xbox ext. */");
						break;
					default:
						DbgVshPrintf("Unknown data type for D3DVSD_REG: 0x%02X\n", XboxVertexElementDataType);
						break;
					}

					DbgVshPrintf("),\n");
				};
				break;
			}
			case XTL::X_D3DVSD_TOKEN_TESSELLATOR: {
				DWORD VertexRegisterOut = VshGetVertexRegister(*pXboxToken);
				if (*pXboxToken & X_D3DVSD_MASK_TESSUV) {
					DbgVshPrintf("\tD3DVSD_TESSUV(%s),\n", XboxVertexRegisterAsString(VertexRegisterOut));
				} else { // D3DVSD_TESSNORMAL
					DWORD VertexRegisterIn = VshGetVertexRegisterIn(*pXboxToken);
					DbgVshPrintf("\tD3DVSD_TESSNORMAL(%s, %s),\n",
						XboxVertexRegisterAsString(VertexRegisterIn),
						XboxVertexRegisterAsString(VertexRegisterOut));
				}
				break;
			}
			case XTL::X_D3DVSD_TOKEN_CONSTMEM: {
				DWORD ConstantAddress = (*pXboxToken & X_D3DVSD_CONSTADDRESSMASK) >> X_D3DVSD_CONSTADDRESSSHIFT;
				DWORD Count = (*pXboxToken & X_D3DVSD_CONSTCOUNTMASK) >> X_D3DVSD_CONSTCOUNTSHIFT;
				DbgVshPrintf("\tD3DVSD_CONST(%d, %d),\n", ConstantAddress, Count);
				LOG_TEST_CASE("X_D3DVSD_TOKEN_CONSTMEM");
				Step = Count * 4 + 1;
				break;
			}
			case XTL::X_D3DVSD_TOKEN_EXT: {
				DWORD ExtInfo = (*pXboxToken & X_D3DVSD_EXTINFOMASK) >> X_D3DVSD_EXTINFOSHIFT;
				DWORD Count = (*pXboxToken & X_D3DVSD_EXTCOUNTMASK) >> X_D3DVSD_EXTCOUNTSHIFT;
				DbgVshPrintf("\tD3DVSD_EXT(%d, %d),\n", ExtInfo, Count);
				LOG_TEST_CASE("X_D3DVSD_TOKEN_EXT");
				Step = Count * 4 + 1; // TODO : Is this correct?
				break;
			}
			default:
				DbgVshPrintf("Unknown token type: %d\n", VshGetTokenType(*pXboxToken));
				break;
			}

			pXboxToken += Step;
		}

		if (iNumberOfVertexStreams > 0) {
			DbgVshPrintf("\t// NeedPatching: %d\n", bStreamNeedsPatching);
		}

		DbgVshPrintf("\tD3DVSD_END()\n};\n");

		DbgVshPrintf("// NbrStreams: %d\n", iNumberOfVertexStreams);
	}

	void VshConvertToken_NOP(DWORD *pXboxToken)
	{
		if(*pXboxToken != X_D3DVSD_NOP())
		{
			LOG_TEST_CASE("Token NOP found, but extra parameters are given!");
		}
	}

	DWORD VshConvertToken_CONSTMEM(DWORD *pXboxToken)
	{
		// DWORD ConstantAddress = (*pXboxToken & X_D3DVSD_CONSTADDRESSMASK) >> X_D3DVSD_CONSTADDRESSSHIFT;
		DWORD Count           = (*pXboxToken & X_D3DVSD_CONSTCOUNTMASK) >> X_D3DVSD_CONSTCOUNTSHIFT;
		LOG_TEST_CASE("CONST"); // TODO : Implement
		return Count * 4 + 1;
	}

	void VshConvertToken_TESSELATOR(DWORD *pXboxToken)
	{
		BYTE Index;

		if(*pXboxToken & X_D3DVSD_MASK_TESSUV)
		{
			DWORD VertexRegister    = VshGetVertexRegister(*pXboxToken);
			DWORD NewVertexRegister = VertexRegister;

			NewVertexRegister = Xb2PCRegisterType(VertexRegister, Index);
			// TODO : Expand on the setting of this TESSUV register element :
			pRecompiled->Usage = D3DDECLUSAGE(NewVertexRegister);
			pRecompiled->UsageIndex = Index;
		}
		else // D3DVSD_TESSNORMAL
		{
			DWORD VertexRegisterIn  = VshGetVertexRegisterIn(*pXboxToken);
			DWORD VertexRegisterOut = VshGetVertexRegister(*pXboxToken);

			DWORD NewVertexRegisterIn  = VertexRegisterIn;
			DWORD NewVertexRegisterOut = VertexRegisterOut;

			NewVertexRegisterIn = Xb2PCRegisterType(VertexRegisterIn, Index);
			// TODO : Expand on the setting of this TESSNORMAL input register element :
			pRecompiled->Usage = D3DDECLUSAGE(NewVertexRegisterIn);
			pRecompiled->UsageIndex = Index;

			NewVertexRegisterOut = Xb2PCRegisterType(VertexRegisterOut, Index);
			// TODO : Expand on the setting of this TESSNORMAL output register element :
			pRecompiled++;
			pRecompiled->Usage = D3DDECLUSAGE(NewVertexRegisterOut);
			pRecompiled->UsageIndex = Index;
		}
	}

	void VshConvertToken_STREAM(DWORD *pXboxToken)
	{
		// D3DVSD_STREAM_TESS
		if(*pXboxToken & X_D3DVSD_STREAMTESSMASK)
		{
			// TODO
		}
		else // D3DVSD_STREAM
		{
			DWORD StreamNumber = VshGetVertexStream(*pXboxToken);

			// new stream
			pCurrentVertexShaderStreamInfo = &(pVertexShaderInfoToSet->VertexStreams[StreamNumber]);
			pCurrentVertexShaderStreamInfo->NeedPatch = FALSE;
			pCurrentVertexShaderStreamInfo->DeclPosition = FALSE;
			pCurrentVertexShaderStreamInfo->CurrentStreamNumber = 0;
			pCurrentVertexShaderStreamInfo->HostVertexStride = 0;
			pCurrentVertexShaderStreamInfo->NumberOfVertexElements = 0;

			// Dxbx note : Use Dophin(s), FieldRender, MatrixPaletteSkinning and PersistDisplay as a testcase

			pCurrentVertexShaderStreamInfo->CurrentStreamNumber = VshGetVertexStream(*pXboxToken);
			pVertexShaderInfoToSet->NumberOfVertexStreams++;
			// TODO : Keep a bitmask for all StreamNumber's seen?
		}
	}

	void VshConvert_RegisterVertexElement(
		UINT XboxVertexElementDataType,
		UINT XboxVertexElementByteSize,
		UINT HostVertexElementByteSize,
		BOOL NeedPatching)
	{
		CxbxVertexShaderStreamElement* pCurrentElement = &(pCurrentVertexShaderStreamInfo->VertexElements[pCurrentVertexShaderStreamInfo->NumberOfVertexElements]);
		pCurrentElement->XboxType = XboxVertexElementDataType;
		pCurrentElement->XboxByteSize = XboxVertexElementByteSize;
		pCurrentElement->HostByteSize = HostVertexElementByteSize;
		pCurrentVertexShaderStreamInfo->NumberOfVertexElements++;
		pCurrentVertexShaderStreamInfo->NeedPatch |= NeedPatching;
	}

	void VshConvert_SkipBytes(int SkipBytesCount)
	{
		if (SkipBytesCount % sizeof(DWORD)) {
			LOG_TEST_CASE("D3DVSD_SKIPBYTES not divisble by 4!");
		}
#if 0 // Potential optimization, for now disabled for simplicity :
		else {
			// Skip size is a whole multiple of 4 bytes;
			// Is stream patching not needed up until this element?
			if (!pCurrentVertexShaderStreamInfo->NeedPatch) {
				// Then we can get away with increasing the host stride,
				// which avoids otherwise needless vertex buffer patching :
				pCurrentVertexShaderStreamInfo->HostVertexStride += SkipBytesCount;
				return;
			}
		}
#endif

		// Register a 'skip' element, so that Xbox data will be skipped
		// without increasing host stride - this does require patching :
		VshConvert_RegisterVertexElement(XTL::X_D3DVSDT_NONE, SkipBytesCount, /*HostSize=*/0, /*NeedPatching=*/TRUE);
	}

	void VshConvertToken_STREAMDATA_SKIP(DWORD *pXboxToken)
	{
		WORD SkipCount = (*pXboxToken & X_D3DVSD_SKIPCOUNTMASK) >> X_D3DVSD_SKIPCOUNTSHIFT;
		VshConvert_SkipBytes(SkipCount * sizeof(DWORD));
	}

	void VshConvertToken_STREAMDATA_SKIPBYTES(DWORD* pXboxToken)
	{
		WORD SkipBytesCount = (*pXboxToken & X_D3DVSD_SKIPCOUNTMASK) >> X_D3DVSD_SKIPCOUNTSHIFT;
		VshConvert_SkipBytes(SkipBytesCount);
	}

	void VshConvertToken_STREAMDATA_REG(DWORD *pXboxToken)
	{
		DWORD VertexRegister = VshGetVertexRegister(*pXboxToken);
		BOOL NeedPatching = FALSE;
		BYTE Index;
		BYTE HostVertexRegisterType;

		if (IsFixedFunction) {
			HostVertexRegisterType = Xb2PCRegisterType(VertexRegister, Index);
		} else {
			// D3DDECLUSAGE_TEXCOORD can be useds for any user-defined data
			// We need this because there is no reliable way to detect the real usage
			// Xbox has no concept of 'usage types', it only requires a list of attribute register numbers.
			// So we treat them all as 'user-defined' with an Index of the Vertex Register Index
			// this prevents information loss in shaders due to non-matching dcl types!
			HostVertexRegisterType = D3DDECLUSAGE_TEXCOORD;
			Index = (BYTE)VertexRegister;
		}

		// Add this register to the list of declared registers
		// TODO : Reinstate and use : RegVIsPresentInDeclaration[VertexRegister] = true;

		DWORD XboxVertexElementDataType = (*pXboxToken & X_D3DVSD_DATATYPEMASK) >> X_D3DVSD_DATATYPESHIFT;
		WORD XboxVertexElementByteSize = 0;
		BYTE HostVertexElementDataType = 0;
		WORD HostVertexElementByteSize = 0;

		switch (XboxVertexElementDataType)
		{
		case XTL::X_D3DVSDT_FLOAT1: // 0x12:
			HostVertexElementDataType = D3DDECLTYPE_FLOAT1;
			HostVertexElementByteSize = 1 * sizeof(FLOAT);
			break;
		case XTL::X_D3DVSDT_FLOAT2: // 0x22:
			HostVertexElementDataType = D3DDECLTYPE_FLOAT2;
			HostVertexElementByteSize = 2 * sizeof(FLOAT);
			break;
		case XTL::X_D3DVSDT_FLOAT3: // 0x32:
			HostVertexElementDataType = D3DDECLTYPE_FLOAT3;
			HostVertexElementByteSize = 3 * sizeof(FLOAT);
			break;
		case XTL::X_D3DVSDT_FLOAT4: // 0x42:
			HostVertexElementDataType = D3DDECLTYPE_FLOAT4;
			HostVertexElementByteSize = 4 * sizeof(FLOAT);
			break;
		case XTL::X_D3DVSDT_D3DCOLOR: // 0x40:
			HostVertexElementDataType = D3DDECLTYPE_D3DCOLOR;
			HostVertexElementByteSize = 1 * sizeof(D3DCOLOR);
			break;
		case XTL::X_D3DVSDT_SHORT2: // 0x25:
			HostVertexElementDataType = D3DDECLTYPE_SHORT2;
			HostVertexElementByteSize = 2 * sizeof(SHORT);
			break;
		case XTL::X_D3DVSDT_SHORT4: // 0x45:
			HostVertexElementDataType = D3DDECLTYPE_SHORT4;
			HostVertexElementByteSize = 4 * sizeof(SHORT);
			break;
		case XTL::X_D3DVSDT_NORMSHORT1: // 0x11:
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_SHORT2N) {
				HostVertexElementDataType = D3DDECLTYPE_SHORT2N;
				HostVertexElementByteSize = 2 * sizeof(SHORT);
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT1;
				HostVertexElementByteSize = 1 * sizeof(FLOAT);
			}
			XboxVertexElementByteSize = 1 * sizeof(XTL::SHORT);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_NORMSHORT2: // 0x21:
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_SHORT2N) {
				HostVertexElementDataType = D3DDECLTYPE_SHORT2N;
				HostVertexElementByteSize = 2 * sizeof(SHORT);
				// No need for patching in D3D9
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT2;
				HostVertexElementByteSize = 2 * sizeof(FLOAT);
				XboxVertexElementByteSize = 2 * sizeof(XTL::SHORT);
				NeedPatching = TRUE;
			}
			break;
		case XTL::X_D3DVSDT_NORMSHORT3: // 0x31:
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_SHORT4N) {
				HostVertexElementDataType = D3DDECLTYPE_SHORT4N;
				HostVertexElementByteSize = 4 * sizeof(SHORT);
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT3;
				HostVertexElementByteSize = 3 * sizeof(FLOAT);
			}
			XboxVertexElementByteSize = 3 * sizeof(XTL::SHORT);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_NORMSHORT4: // 0x41:
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_SHORT4N) {
				HostVertexElementDataType = D3DDECLTYPE_SHORT4N;
				HostVertexElementByteSize = 4 * sizeof(SHORT);
				// No need for patching in D3D9
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT4;
				HostVertexElementByteSize = 4 * sizeof(FLOAT);
				XboxVertexElementByteSize = 4 * sizeof(XTL::SHORT);
				NeedPatching = TRUE;
			}
			break;
		case XTL::X_D3DVSDT_NORMPACKED3: // 0x16:
			HostVertexElementDataType = D3DDECLTYPE_FLOAT3;
			HostVertexElementByteSize = 3 * sizeof(FLOAT);
			XboxVertexElementByteSize = 1 * sizeof(XTL::DWORD);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_SHORT1: // 0x15:
			HostVertexElementDataType = D3DDECLTYPE_SHORT2;
			HostVertexElementByteSize = 2 * sizeof(SHORT);
			XboxVertexElementByteSize = 1 * sizeof(XTL::SHORT);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_SHORT3: // 0x35:
			HostVertexElementDataType = D3DDECLTYPE_SHORT4;
			HostVertexElementByteSize = 4 * sizeof(SHORT);
			XboxVertexElementByteSize = 3 * sizeof(XTL::SHORT);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_PBYTE1: // 0x14:
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_UBYTE4N) {
				HostVertexElementDataType = D3DDECLTYPE_UBYTE4N;
				HostVertexElementByteSize = 4 * sizeof(BYTE);
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT1;
				HostVertexElementByteSize = 1 * sizeof(FLOAT);
			}
			XboxVertexElementByteSize = 1 * sizeof(XTL::BYTE);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_PBYTE2: // 0x24:
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_UBYTE4N) {
				HostVertexElementDataType = D3DDECLTYPE_UBYTE4N;
				HostVertexElementByteSize = 4 * sizeof(BYTE);
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT2;
				HostVertexElementByteSize = 2 * sizeof(FLOAT);
			}
			XboxVertexElementByteSize = 2 * sizeof(XTL::BYTE);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_PBYTE3: // 0x34:
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_UBYTE4N) {
				HostVertexElementDataType = D3DDECLTYPE_UBYTE4N;
				HostVertexElementByteSize = 4 * sizeof(BYTE);
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT3;
				HostVertexElementByteSize = 3 * sizeof(FLOAT);
			}
			XboxVertexElementByteSize = 3 * sizeof(XTL::BYTE);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_PBYTE4: // 0x44:
			// Test-case : Panzer
			if (g_D3DCaps.DeclTypes & D3DDTCAPS_UBYTE4N) {
				HostVertexElementDataType = D3DDECLTYPE_UBYTE4N;
				HostVertexElementByteSize = 4 * sizeof(BYTE);
				// No need for patching when D3D9 supports D3DDECLTYPE_UBYTE4N
			}
			else
			{
				HostVertexElementDataType = D3DDECLTYPE_FLOAT4;
				HostVertexElementByteSize = 4 * sizeof(FLOAT);
				XboxVertexElementByteSize = 4 * sizeof(XTL::BYTE);
				NeedPatching = TRUE;
			}
			break;
		case XTL::X_D3DVSDT_FLOAT2H: // 0x72:
			HostVertexElementDataType = D3DDECLTYPE_FLOAT4;
			HostVertexElementByteSize = 4 * sizeof(FLOAT);
			XboxVertexElementByteSize = 3 * sizeof(FLOAT);
			NeedPatching = TRUE;
			break;
		case XTL::X_D3DVSDT_NONE: // 0x02:
			// No host element data, so no patching
			break;
		default:
			//LOG_TEST_CASE("Unknown data type for D3DVSD_REG: 0x%02X\n", XboxVertexElementDataType);
			break;
		}

		// On X_D3DVSDT_NONE skip this token
		if (XboxVertexElementDataType == XTL::X_D3DVSDT_NONE)
		{
			// Xbox elements with X_D3DVSDT_NONE have size zero, so there's no need to register those.
			// Note, that for skip tokens, we DO call VshConvert_RegisterVertexElement with a X_D3DVSDT_NONE!
			return;
		}

		// save patching information
		VshConvert_RegisterVertexElement(
			XboxVertexElementDataType,
			NeedPatching ? XboxVertexElementByteSize : HostVertexElementByteSize,
			HostVertexElementByteSize,
			NeedPatching);

		pRecompiled->Stream = pCurrentVertexShaderStreamInfo->CurrentStreamNumber;
		pRecompiled->Offset = pCurrentVertexShaderStreamInfo->HostVertexStride;
		pRecompiled->Type = HostVertexElementDataType;
		pRecompiled->Method = D3DDECLMETHOD_DEFAULT;
		pRecompiled->Usage = HostVertexRegisterType;
		pRecompiled->UsageIndex = Index;

		pRecompiled++;

		pCurrentVertexShaderStreamInfo->HostVertexStride += HostVertexElementByteSize;
	}

	void VshConvertToken_STREAMDATA(DWORD *pXboxToken)
	{
		if (*pXboxToken & X_D3DVSD_MASK_SKIP)
		{
			// For D3D9, use D3DDECLTYPE_UNUSED ?
			if (*pXboxToken & X_D3DVSD_MASK_SKIPBYTES) {
				VshConvertToken_STREAMDATA_SKIPBYTES(pXboxToken);
			} else {
				VshConvertToken_STREAMDATA_SKIP(pXboxToken);
			}
		}
		else // D3DVSD_REG
		{
			VshConvertToken_STREAMDATA_REG(pXboxToken);
		}
	}

	DWORD VshRecompileToken(DWORD *pXboxToken)
	{
		DWORD Step = 1;

		switch(VshGetTokenType(*pXboxToken))
		{
		case XTL::X_D3DVSD_TOKEN_NOP:
			VshConvertToken_NOP(pXboxToken);
			break;
		case XTL::X_D3DVSD_TOKEN_STREAM:
		{
			VshConvertToken_STREAM(pXboxToken);
			break;
		}
		case XTL::X_D3DVSD_TOKEN_STREAMDATA:
		{
			VshConvertToken_STREAMDATA(pXboxToken);
			break;
		}
		case XTL::X_D3DVSD_TOKEN_TESSELLATOR:
		{
			VshConvertToken_TESSELATOR(pXboxToken);
			break;
		}
		case XTL::X_D3DVSD_TOKEN_CONSTMEM:
		{
			Step = VshConvertToken_CONSTMEM(pXboxToken);
			break;
		}
		default:
			//LOG_TEST_CASE("Unknown token type: %d\n", VshGetTokenType(*pXboxToken));
			break;
		}

		return Step;
	}

	DWORD* RemoveXboxDeclarationRedefinition(DWORD* pXboxDeclaration)
	{
		// Detect and remove register redefinitions by preprocessing the Xbox Vertex Declaration
		// Test Case: King Kong

		// Find the last token
		DWORD* pXboxToken = pXboxDeclaration;
		while (*pXboxToken != X_D3DVSD_END()){
			pXboxToken++;
		}

		// Operate on a copy of the Xbox declaration, rather than messing with the Xbox's memory
		auto declarationBytes = sizeof(DWORD) * (pXboxToken - pXboxDeclaration + 1);
		auto pXboxDeclarationCopy = (DWORD*)malloc(declarationBytes);
		memcpy(pXboxDeclarationCopy, pXboxDeclaration, declarationBytes);
		pXboxToken = pXboxDeclarationCopy + (pXboxToken - pXboxDeclaration); // Move to end of the copy

		// Remember if we've seen a given output register
		std::bitset<16> seen;

		// We want to keep later definitions, and remove earlier ones
		// Scan back from the end of the declaration, and replace redefinitions with nops
		while (pXboxToken > pXboxDeclarationCopy) {
			auto type = VshGetTokenType(*pXboxToken);
			if (type == XTL::X_D3DVSD_TOKEN_STREAMDATA && !(*pXboxToken & X_D3DVSD_MASK_SKIP) ||
				type == XTL::X_D3DVSD_TOKEN_TESSELLATOR)
			{
				auto outputRegister = VshGetVertexRegister(*pXboxToken);
				if (seen[outputRegister])
				{
					// Blank out tokens for mapped registers
					*pXboxToken = X_D3DVSD_NOP();
					EmuLog(LOG_LEVEL::DEBUG, "Replacing duplicate definition of register %d with D3DVSD_NOP", outputRegister);
				}
				else
				{
					// Mark register as seen
					seen[outputRegister] = true;
				}
			}

			pXboxToken--;
		}

		return pXboxDeclarationCopy;
	}

public:
	D3DVERTEXELEMENT *Convert(DWORD* pXboxDeclaration, bool bIsFixedFunction, CxbxVertexShaderInfo* pCxbxVertexShaderInfo)
	{
		// Get a preprocessed copy of the original Xbox Vertex Declaration
		auto pXboxVertexDeclarationCopy = RemoveXboxDeclarationRedefinition(pXboxDeclaration);

		pVertexShaderInfoToSet = pCxbxVertexShaderInfo;
		hostTemporaryRegisterCount = g_D3DCaps.VS20Caps.NumTemps;
		if (hostTemporaryRegisterCount < VSH_MIN_TEMPORARY_REGISTERS) {
			LOG_TEST_CASE("g_D3DCaps.VS20Caps.NumTemps < 12 (Host minimal vertex shader temporary register count)");
		}
		if (hostTemporaryRegisterCount < 12+1) { // TODO : Use a constant (see X_D3DVSD_REG)
			LOG_TEST_CASE("g_D3DCaps.VS20Caps.NumTemps < 12+1 (Xbox vertex shader temporary register count + r12, reading oPos)");
		}

		// Note, that some Direct3D 9 drivers return only the required minimum temporary register count of 12,
		// but regardless, shaders that use temporary register numbers above r12 still seem to work correctly.
		// So it seems we can't rely on VS20Caps.NumTemps indicating accurately what host hardware supports.
		// (Although it could be that the driver switches to software vertex processing when a shader exceeds hardware limits.)

		IsFixedFunction = bIsFixedFunction;

		// TODO : Reinstate and use : RegVIsPresentInDeclaration.fill(false);

		// First of all some info:
		// We have to figure out which flags are set and then
		// we have to patch their params

		// some token values
		// 0xFFFFFFFF - end of the declaration
		// 0x00000000 - nop (means that this value is ignored)

		// Calculate size of declaration
		XboxDeclarationCount = VshGetDeclarationCount(pXboxVertexDeclarationCopy);
		// For Direct3D9, we need to reserve at least twice the number of elements, as one token can generate two registers (in and out) :
		HostDeclarationSize = XboxDeclarationCount * sizeof(D3DVERTEXELEMENT) * 2;
	
		D3DVERTEXELEMENT *Result = (D3DVERTEXELEMENT *)calloc(1, HostDeclarationSize);
		pRecompiled = Result;
		uint8_t *pRecompiledBufferOverflow = ((uint8_t*)pRecompiled) + HostDeclarationSize;

		VshDumpXboxDeclaration(pXboxDeclaration);

		auto pXboxToken = pXboxVertexDeclarationCopy;
		while (*pXboxToken != X_D3DVSD_END())
		{
			if ((uint8_t*)pRecompiled >= pRecompiledBufferOverflow) {
				DbgVshPrintf("Detected buffer-overflow, breaking out...\n");
				break;
			}

			DWORD Step = VshRecompileToken(pXboxToken);
			pXboxToken += Step;
		}

		*pRecompiled = D3DDECL_END();

		// Ensure valid ordering of the vertex declaration (http://doc.51windows.net/Directx9_SDK/graphics/programmingguide/gettingstarted/vertexdeclaration/vertexdeclaration.htm)
		// In particular "All vertex elements for a stream must be consecutive and sorted by offset"
		// Test case: King Kong (due to register redefinition)
		std::sort(Result, pRecompiled, [] (const auto& x, const auto& y)
			{ return std::tie(x.Stream, x.Method, x.Offset) < std::tie(y.Stream, y.Method, y.Offset); });

		// Free the preprocessed declaration copy
		free(pXboxVertexDeclarationCopy);

		return Result;
	}
};

D3DVERTEXELEMENT *EmuRecompileVshDeclaration
(
    DWORD                *pXboxDeclaration,
    bool                  bIsFixedFunction,
    DWORD                *pXboxDeclarationCount,
    DWORD                *pHostDeclarationSize,
    CxbxVertexShaderInfo *pCxbxVertexShaderInfo
)
{
	XboxVertexDeclarationConverter Converter;

	D3DVERTEXELEMENT* pHostVertexElements = Converter.Convert(pXboxDeclaration, bIsFixedFunction, pCxbxVertexShaderInfo);

	*pXboxDeclarationCount = Converter.XboxDeclarationCount;
	*pHostDeclarationSize = Converter.HostDeclarationSize;

    return pHostVertexElements;
}

extern void BuildShader(std::stringstream& hlsl, VSH_XBOX_SHADER* pShader);

std::string DebugPrependLineNumbers(std::string shaderString) {
	std::stringstream shader(shaderString);
	auto debugShader = std::stringstream();

	int i = 1;
	for (std::string line; std::getline(shader, line); ) {
		auto lineNumber = std::to_string(i++);
		auto paddedLineNumber = lineNumber.insert(0, 3 - lineNumber.size(), ' ');
		debugShader << "/* " << paddedLineNumber << " */ " << line << "\n";
	}

	return debugShader.str();
}

// recompile xbox vertex shader function
extern HRESULT EmuRecompileVshFunction
(
    DWORD        *pXboxFunction,
    bool          bNoReservedConstants,
	D3DVERTEXELEMENT *pRecompiledDeclaration,
    bool   		 *pbUseDeclarationOnly,
    DWORD        *pXboxFunctionSize,
	ID3DBlob **ppRecompiledShader
)
{
	XTL::X_VSH_SHADER_HEADER   *pXboxVertexShaderHeader = (XTL::X_VSH_SHADER_HEADER*)pXboxFunction;
    DWORD              *pToken;
    boolean             EOI = false;
    VSH_XBOX_SHADER    *pShader = (VSH_XBOX_SHADER*)calloc(1, sizeof(VSH_XBOX_SHADER));
	ID3DBlob           *pErrors = nullptr;
    HRESULT             hRet = 0;

    // TODO: support this situation..
    if(pXboxFunction == xbnullptr)
        return E_FAIL;

	// Initialize output arguments to zero
	*pbUseDeclarationOnly = 0;
    *pXboxFunctionSize = 0;
    *ppRecompiledShader = nullptr;

	if(!pShader) {
        EmuLog(LOG_LEVEL::WARNING, "Couldn't allocate memory for vertex shader conversion buffer");
        return E_OUTOFMEMORY;
    }

    pShader->ShaderHeader = *pXboxVertexShaderHeader;
    switch(pXboxVertexShaderHeader->Version) {
        case VERSION_XVS:
            break;
        case VERSION_XVSS:
            EmuLog(LOG_LEVEL::WARNING, "Might not support vertex state shaders?");
            hRet = E_FAIL;
            break;
        case VERSION_XVSW:
            EmuLog(LOG_LEVEL::WARNING, "Might not support vertex read/write shaders?");
            hRet = E_FAIL;
            break;
        default:
            EmuLog(LOG_LEVEL::WARNING, "Unknown vertex shader version 0x%02X", pXboxVertexShaderHeader->Version);
            hRet = E_FAIL;
            break;
    }

    if(SUCCEEDED(hRet)) {
		static std::string hlsl_template =
			#include "core\hle\D3D8\Direct3D9\Xb.hlsl" // Note : This included .hlsl defines a raw string
			;

		auto hlsl_stream = std::stringstream();

        for (pToken = (DWORD*)((uint8_t*)pXboxFunction + sizeof(XTL::X_VSH_SHADER_HEADER)); !EOI; pToken += X_VSH_INSTRUCTION_SIZE) {
            VSH_SHADER_INSTRUCTION Inst;

            VshParseInstruction((uint32_t*)pToken, &Inst);
            VshConvertToIntermediate(&Inst, pShader);
            EOI = Inst.Final;
        }

        // The size of the shader is
        *pXboxFunctionSize = (intptr_t)pToken - (intptr_t)pXboxFunction;

		// Do not attempt to compile empty shaders
		if (pShader->IntermediateCount == 0) {
            // This is a declaration only shader, so there is no function to recompile
            *pbUseDeclarationOnly = 1;
			return D3D_OK;
		}

		BuildShader(hlsl_stream, pShader);
		std::string hlsl_str = hlsl_stream.str();
		hlsl_str = std::regex_replace(hlsl_template, std::regex("// <Xbox Shader>"), hlsl_str);

		DbgVshPrintf("--- HLSL conversion ---\n");
		DbgVshPrintf(DebugPrependLineNumbers(hlsl_str).c_str());
		DbgVshPrintf("-----------------------\n");

		hRet = D3DCompile(
			hlsl_str.c_str(),
			hlsl_str.length(),
			nullptr, // pSourceName
			nullptr, // pDefines
			nullptr, // pInclude // TODO precompile x_* HLSL functions?
			"main", // shader entry poiint
			"vs_3_0", // shader profile
			0, // flags1
			0, // flags2
			ppRecompiledShader, // out
			&pErrors // ppErrorMsgs out
		);
        if (FAILED(hRet)) {
            EmuLog(LOG_LEVEL::WARNING, "Couldn't assemble recompiled vertex shader");
        }

		if (pErrors) {
			// Determine the log level
			auto hlslErrorLogLevel = FAILED(hRet) ? LOG_LEVEL::ERROR2 : LOG_LEVEL::DEBUG;
			// Log HLSL compiler errors
			EmuLog(hlslErrorLogLevel, "%s", (char*)(pErrors->GetBufferPointer()));
			pErrors->Release();
		}
    }

    free(pShader);

    return hRet;
}

extern void FreeVertexDynamicPatch(CxbxVertexShader *pVertexShader)
{
    pVertexShader->VertexShaderInfo.NumberOfVertexStreams = 0;
}

// Checks for failed vertex shaders, and shaders that would need patching
boolean VshHandleIsValidShader(DWORD XboxVertexShaderHandle)
{
#if 0
	//printf( "VS = 0x%.08X\n", XboxVertexShaderHandle );

    CxbxVertexShader *pCxbxVertexShader = GetCxbxVertexShader(XboxVertexShaderHandle);
    if (pCxbxVertexShader) {
        if (pCxbxVertexShader->XboxStatus != 0)
        {
            return FALSE;
        }
        /*
        for (uint32 i = 0; i < pCxbxVertexShader->VertexShaderInfo.NumberOfVertexStreams; i++)
        {
            if (pCxbxVertexShader->VertexShaderInfo.VertexStreams[i].NeedPatch)
            {
                // Just for caching purposes
                pCxbxVertexShader->XboxStatus = 0x80000001;
                return FALSE;
            }
        }
        */
    }
#endif
    return TRUE;
}

extern boolean IsValidCurrentShader(void)
{
	// Dxbx addition : There's no need to call
	// XTL_EmuIDirect3DDevice_GetVertexShader, just check g_Xbox_VertexShader_Handle :
	return VshHandleIsValidShader(g_Xbox_VertexShader_Handle);
}

CxbxVertexShaderInfo *GetCxbxVertexShaderInfo(DWORD XboxVertexShaderHandle)
{
    CxbxVertexShader *pCxbxVertexShader = GetCxbxVertexShader(XboxVertexShaderHandle);

    for (uint32_t i = 0; i < pCxbxVertexShader->VertexShaderInfo.NumberOfVertexStreams; i++)
    {
        if (pCxbxVertexShader->VertexShaderInfo.VertexStreams[i].NeedPatch)
        {
            return &pCxbxVertexShader->VertexShaderInfo;
        }
    }
    return nullptr;
}

std::unordered_map<DWORD, CxbxVertexShader*> g_CxbxVertexShaders;

CxbxVertexShader* GetCxbxVertexShader(DWORD XboxVertexShaderHandle)
{
	if (VshHandleIsVertexShader(XboxVertexShaderHandle)) {
		auto it = g_CxbxVertexShaders.find(XboxVertexShaderHandle);
		if (it != g_CxbxVertexShaders.end()) {
			return it->second;
		}
	}

	return nullptr;
}

void SetCxbxVertexShader(DWORD XboxVertexShaderHandle, CxbxVertexShader* shader)
{
	auto it = g_CxbxVertexShaders.find(XboxVertexShaderHandle);
	if (it != g_CxbxVertexShaders.end() && it->second != nullptr && shader != nullptr) {
		LOG_TEST_CASE("Overwriting existing Vertex Shader");
	}

	g_CxbxVertexShaders[XboxVertexShaderHandle] = shader;
}

void CxbxImpl_SetVertexShaderInput
(
	DWORD              Handle,
	UINT               StreamCount,
	XTL::X_STREAMINPUT* pStreamInputs
)
{
	LOG_INIT

	// If Handle is NULL, all VertexShader input state is cleared.
	// Otherwise, Handle is the address of an Xbox VertexShader struct, or-ed with 1 (X_D3DFVF_RESERVED0)
	// (Thus, a FVF handle is an invalid argument.)
	//

	LOG_UNIMPLEMENTED();
}

void CxbxImpl_SelectVertexShaderDirect
(
	XTL::X_VERTEXATTRIBUTEFORMAT* pVAF,
	DWORD Address
)
{
	LOG_INIT;

	// When pVAF is non-null, this vertex attribute format takes precedence over the the one	
	LOG_UNIMPLEMENTED();
}

// HLSL outputs

void OutputHlsl(std::stringstream& hlsl, VSH_IMD_OUTPUT& dest)
{
	switch (dest.Type) {
	case IMD_OUTPUT_C:
		hlsl << "c[" << dest.Address << "]";
		break;
	case IMD_OUTPUT_R:
		hlsl << "r" << dest.Address;
		break;
	case IMD_OUTPUT_O:
		assert(dest.Address < OREG_A0X);
		hlsl << OReg_Name[dest.Address];
		break;
	case IMD_OUTPUT_A0X:
		hlsl << "a0"; // Is this actually a valid output?
		break;
	default:
		assert(false);
		break;
	}

	// Write the mask as a separate argument to the opcode defines
	// (No space, so that "dest,mask, ..." looks close to "dest.mask, ...")
	hlsl << ",";
	if (dest.Mask[0]) hlsl << "x";
	if (dest.Mask[1]) hlsl << "y";
	if (dest.Mask[2]) hlsl << "z";
	if (dest.Mask[3]) hlsl << "w";
}

void ParameterHlsl(std::stringstream& hlsl, VSH_IMD_PARAMETER& paramMeta)
{
	auto param = paramMeta.Parameter;

	if (param.Neg) {
		hlsl << "-";
	}

	int register_number = param.Address;
	if (param.ParameterType == PARAM_C) {
		// Map Xbox [-96, 95] to Host [0, 191]
		// Account for Xbox's negative constant indexes
		register_number += 96;
		if (paramMeta.IndexesWithA0_X) {
			// Only display the offset if it's not 0.
			if (register_number != 0) {
				hlsl << "c[a0.x+" << register_number << "]";
			}
			else {
				hlsl << "c[a0.x]";
			}
		} else {
			hlsl << "c[" << register_number << "]";
		}
	} else {
		hlsl << VshGetRegisterName(param.ParameterType) << register_number;
	}

	// Write the swizzle if we need to
	// Only bother printing the swizzle if it is not the default .xyzw
	if (!(param.Swizzle[0] == SWIZZLE_X &&
		  param.Swizzle[1] == SWIZZLE_Y &&
		  param.Swizzle[2] == SWIZZLE_Z &&
	      param.Swizzle[3] == SWIZZLE_W ))
	{
		// We'll try to simplify swizzles if we can
		// If all swizzles are the same, we only need to write one out
		unsigned swizzles = 1;

		// Otherwise, we need to use the full swizzle
		if (param.Swizzle[0] != param.Swizzle[1] ||
			param.Swizzle[0] != param.Swizzle[2] ||
			param.Swizzle[0] != param.Swizzle[3]) {
			// Note, we can't remove trailing repeats, like in VS asm,
			// as it may change the type from float4 to float3, float2 or float1!
			swizzles = 4;
		}

		hlsl << ".";
		for (unsigned i = 0; i < swizzles; i++) {
			hlsl << "xyzw"[param.Swizzle[i]];
		}
	}
}

void BuildShader(std::stringstream& hlsl, VSH_XBOX_SHADER* pShader)
{
	// HLSL strings for all MAC opcodes, indexed with VSH_MAC
	static std::string VSH_MAC_HLSL[] = {
		/*MAC_NOP:*/"",
		/*MAC_MOV:*/"x_mov",
		/*MAC_MUL:*/"x_mul",
		/*MAC_ADD:*/"x_add",
		/*MAC_MAD:*/"x_mad",
		/*MAC_DP3:*/"x_dp3",
		/*MAC_DPH:*/"x_dph",
		/*MAC_DP4:*/"x_dp4",
		/*MAC_DST:*/"x_dst",
		/*MAC_MIN:*/"x_min",
		/*MAC_MAX:*/"x_max",
		/*MAC_SLT:*/"x_slt",
		/*MAC_SGE:*/"x_sge",
		/*MAC_ARL:*/"x_arl",
		            "",
		            "" // VSH_MAC 2 final values of the 4 bits are undefined/unknown  TODO : Investigate their effect (if any) and emulate that as well
	};

	// HLSL strings for all ILU opcodes, indexed with VSH_ILU
	static std::string VSH_ILU_HLSL[] = {
		/*ILU_NOP:*/"",
		/*ILU_MOV:*/"x_mov",
		/*ILU_RCP:*/"x_rcp",
		/*ILU_RCC:*/"x_rcc",
		/*ILU_RSQ:*/"x_rsq",
		/*ILU_EXP:*/"x_expp",
		/*ILU_LOG:*/"x_logp",
		/*ILU_LIT:*/"x_lit" // = 7 - all values of the 3 bits are used
	};

	for (int i = 0; i < pShader->IntermediateCount; i++) {
		VSH_INTERMEDIATE_FORMAT& xboxInstruction = pShader->Intermediate[i];

		std::string str = "";
		if (xboxInstruction.InstructionType == IMD_MAC) {
			if (xboxInstruction.MAC > MAC_NOP && xboxInstruction.MAC <= MAC_ARL) {
				str = VSH_MAC_HLSL[xboxInstruction.MAC];
			}
		} else if (xboxInstruction.InstructionType == IMD_ILU) {
			if (xboxInstruction.ILU > ILU_NOP) {
				str = VSH_ILU_HLSL[xboxInstruction.ILU];
			}
		}

		if (!str.empty()) {
			hlsl << "\n  " << str << "("; // opcode
			OutputHlsl(hlsl, xboxInstruction.Output);
			for (int i = 0; i < 3; i++) {
				if (xboxInstruction.Parameters[i].Active) {
					hlsl << ", ";
					ParameterHlsl(hlsl, xboxInstruction.Parameters[i]);
				}
			}
			hlsl << ");";
		}
	}
}
