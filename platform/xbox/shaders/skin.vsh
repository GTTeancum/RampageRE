!!VP1.1
# cgc version 3.1.0013, build date Apr 18 2012
# command line args: -profile vp20
# source file: skin.vs.cg
#vendor NVIDIA Corporation
#version 3.1.0.13
#profile vp20
#program main
#semantic main.screen
#semantic main.eye
#semantic main.ambient
#semantic main.ldir
#semantic main.lcol
#semantic main.tscale
#semantic main.bones
#var float4 I.pos : $vin.POSITION : ATTR0 : 0 : 1
#var float4 I.weight : $vin.TEXCOORD2 : TEXCOORD2 : 0 : 1
#var float3 I.norm : $vin.NORMAL : ATTR2 : 0 : 1
#var float4 I.col : $vin.COLOR0 : ATTR3 : 0 : 1
#var float4 I.tex : $vin.TEXCOORD0 : TEXCOORD0 : 0 : 1
#var float4 I.bone : $vin.TEXCOORD1 : TEXCOORD1 : 0 : 1
#var float4x4 screen :  : c[0], 4 : 1 : 1
#var float4 eye[0] :  : c[82] : 2 : 1
#var float4 eye[1] :  : c[83] : 2 : 1
#var float4 eye[2] :  : c[84] : 2 : 1
#var float4 ambient :  : c[85] : 3 : 1
#var float4 ldir[0] :  : c[86] : 4 : 1
#var float4 ldir[1] :  : c[87] : 4 : 1
#var float4 ldir[2] :  : c[88] : 4 : 1
#var float4 lcol[0] :  : c[89] : 5 : 1
#var float4 lcol[1] :  : c[90] : 5 : 1
#var float4 lcol[2] :  : c[91] : 5 : 1
#var float4 tscale :  : c[92] : 6 : 1
#var float3x4 bones[0] :  : c[4], 3 : 7 : 1
#var float3x4 bones[1] :  : c[7], 3 : 7 : 1
#var float3x4 bones[2] :  : c[10], 3 : 7 : 1
#var float3x4 bones[3] :  : c[13], 3 : 7 : 1
#var float3x4 bones[4] :  : c[16], 3 : 7 : 1
#var float3x4 bones[5] :  : c[19], 3 : 7 : 1
#var float3x4 bones[6] :  : c[22], 3 : 7 : 1
#var float3x4 bones[7] :  : c[25], 3 : 7 : 1
#var float3x4 bones[8] :  : c[28], 3 : 7 : 1
#var float3x4 bones[9] :  : c[31], 3 : 7 : 1
#var float3x4 bones[10] :  : c[34], 3 : 7 : 1
#var float3x4 bones[11] :  : c[37], 3 : 7 : 1
#var float3x4 bones[12] :  : c[40], 3 : 7 : 1
#var float3x4 bones[13] :  : c[43], 3 : 7 : 1
#var float3x4 bones[14] :  : c[46], 3 : 7 : 1
#var float3x4 bones[15] :  : c[49], 3 : 7 : 1
#var float3x4 bones[16] :  : c[52], 3 : 7 : 1
#var float3x4 bones[17] :  : c[55], 3 : 7 : 1
#var float3x4 bones[18] :  : c[58], 3 : 7 : 1
#var float3x4 bones[19] :  : c[61], 3 : 7 : 1
#var float3x4 bones[20] :  : c[64], 3 : 7 : 1
#var float3x4 bones[21] :  : c[67], 3 : 7 : 1
#var float3x4 bones[22] :  : c[70], 3 : 7 : 1
#var float3x4 bones[23] :  : c[73], 3 : 7 : 1
#var float3x4 bones[24] :  : c[76], 3 : 7 : 1
#var float3x4 bones[25] :  : c[79], 3 : 7 : 1
#var float4 main.pos : $vout.POSITION : HPOS : -1 : 1
#var float4 main.col : $vout.COLOR0 : COL0 : -1 : 1
#var float main.fog : $vout.FOG : FOGC : -1 : 1
#var float4 main.tex0 : $vout.TEXCOORD0 : TEX0 : -1 : 1
#const c[93] = 0 1 3
EXP   R1.y, v[9].y;
ADD   R0.x, v[9].y, -R1.y;
MUL   R2.x, R0, c[93].z;
ARL   A0.x, R2.x;
EXP   R0.y, v[9].x;
ADD   R0.y, v[9].x, -R0;
MUL   R3.x, R0.y, c[93].z;
DP3   R0.z, c[A0.x + 6], v[2];
DP3   R0.x, c[A0.x + 4], v[2];
DP3   R0.y, c[A0.x + 5], v[2];
ARL   A0.x, R3.x;
MUL   R0.xyz, v[10].y, R0;
DP3   R1.z, c[A0.x + 6], v[2];
DP3   R1.y, c[A0.x + 5], v[2];
DP3   R1.x, c[A0.x + 4], v[2];
MAD   R1.xyz, v[10].x, R1, R0;
EXP   R2.y, v[9].z;
ADD   R0.x, v[9].z, -R2.y;
MUL   R2.w, R0.x, c[93].z;
ARL   A0.x, R2.w;
EXP   R0.y, v[9].w;
ADD   R0.y, v[9].w, -R0;
MUL   R1.w, R0.y, c[93].z;
DP3   R0.z, c[A0.x + 6], v[2];
DP3   R0.y, c[A0.x + 5], v[2];
DP3   R0.x, c[A0.x + 4], v[2];
MAD   R0.xyz, v[10].z, R0, R1;
ARL   A0.x, R1.w;
DP3   R1.z, c[A0.x + 6], v[2];
DP3   R1.y, c[A0.x + 5], v[2];
DP3   R1.x, c[A0.x + 4], v[2];
MAD   R1.xyz, v[10].w, R1, R0;
ARL   A0.x, R2.x;
DP3   R0.x, R1, c[82];
DP3   R0.z, R1, c[84];
DP3   R0.y, R1, c[83];
DP3   R0.w, R0, R0;
RSQ   R0.w, R0.w;
MUL   R0.xyz, R0.w, R0;
DP3   R0.w, R0, c[86];
MAX   R1.x, R0.w, c[93];
DP3   R0.w, R0, c[88];
MUL   R1.xyz, R1.x, c[89];
DP3   R2.y, R0, c[87];
ADD   R0.xyz, R1, c[85];
MAX   R1.x, R2.y, c[93];
MAD   R0.xyz, R1.x, c[90], R0;
MAX   R0.w, R0, c[93].x;
MAD   R0.xyz, R0.w, c[91], R0;
MUL   R0.xyz, v[3], R0;
MIN   R0.xyz, R0, c[93].y;
MAX   R0.xyz, R0, c[93].x;
ADD   R1.xyz, -v[3], R0;
MOV   R0.w, c[93].y;
MOV   R0.xyz, v[0];
DP4   R2.z, R0, c[A0.x + 6];
DP4   R2.x, R0, c[A0.x + 4];
DP4   R2.y, R0, c[A0.x + 5];
ARL   A0.x, R3.x;
DP4   R3.z, R0, c[A0.x + 6];
DP4   R3.y, R0, c[A0.x + 5];
DP4   R3.x, R0, c[A0.x + 4];
MUL   R2.xyz, v[10].y, R2;
ARL   A0.x, R2.w;
MAD   R2.xyz, v[10].x, R3, R2;
DP4   R3.z, R0, c[A0.x + 6];
DP4   R3.y, R0, c[A0.x + 5];
DP4   R3.x, R0, c[A0.x + 4];
MAD   R2.xyz, v[10].z, R3, R2;
ARL   A0.x, R1.w;
DP4   R3.z, c[A0.x + 6], R0;
DP4   R3.y, R0, c[A0.x + 5];
DP4   R3.x, R0, c[A0.x + 4];
MAD   R0.xyz, v[10].w, R3, R2;
MOV   R0.w, c[93].y;
DP4   R2.w, R0, c[3];
DP4   R2.z, R0, c[2];
DP4   R2.y, R0, c[1];
RCP   R1.w, R2.w;
DP4   R2.x, R0, c[0];
MUL   o[HPOS].xyz, R2, R1.w;
MAD   o[COL0].xyz, R1, c[85].w, v[3];
MOV   o[HPOS].w, R2;
MOV   o[FOGC].x, R2.w;
MUL   o[TEX0].xy, v[8], c[92];
MOV   o[TEX0].zw, c[93].xyxy;
MOV   o[COL0].w, v[3];
END
# 87 instructions, 0 R-regs
