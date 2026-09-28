/*
 * Compiles the shaders of the d3d11 per-sample interpolation test with Windows' own compiler and prints their
 * bytecode as the tests hold it.
 */
#define COBJMACROS
#include <windows.h>
#include <d3dcompiler.h>
#include <stdio.h>

static const struct
{
    const char *name, *profile, *code;
}
shaders[] =
{
    {"vs", "vs_4_1",
        "void main(uint id : SV_VertexID, out float2 v : TEXCOORD, out float4 position : SV_Position)\n"
        "{\n"
        "    float2 p = float2((id << 1) & 2, id & 2);\n"
        "    position = float4(p * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
        "    v = p * 16.0f;\n"
        "}\n"},
    {"ps_sample_index", "ps_4_1",
        "float4 main(float2 v : TEXCOORD, float4 position : SV_Position, uint s : SV_SampleIndex) : SV_Target\n"
        "{\n"
        "    float f = frac(v.x), g = frac(position.x);\n"
        "    return float4(f * f, g * g, s > 100 ? 1.0f : 0.0f, 1.0f);\n"
        "}\n"},
    {"ps_sample", "ps_4_1",
        "float4 main(sample float2 v : TEXCOORD, float4 position : SV_Position) : SV_Target\n"
        "{\n"
        "    float f = frac(v.x), g = frac(position.x);\n"
        "    return float4(f * f, g * g, 0.0f, 1.0f);\n"
        "}\n"},
};

int main(void)
{
    ID3D10Blob *blob, *errors;
    const DWORD *code;
    unsigned int i, j, count;
    HRESULT hr;

    for (i = 0; i < ARRAY_SIZE(shaders); ++i)
    {
        if (FAILED(hr = D3DCompile(shaders[i].code, strlen(shaders[i].code), shaders[i].name, NULL, NULL, "main",
                shaders[i].profile, 0, 0, &blob, &errors)))
        {
            printf("%s: %#lx %s\n", shaders[i].name, hr, errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "");
            continue;
        }
        code = ID3D10Blob_GetBufferPointer(blob);
        count = ID3D10Blob_GetBufferSize(blob) / sizeof(*code);
        printf("    static const DWORD %s_code[] =\n    {\n", shaders[i].name);
        for (j = 0; j < count; ++j)
            printf("%s0x%08lx,%s", j % 6 ? " " : "        ", code[j], j % 6 == 5 || j == count - 1 ? "\n" : "");
        printf("    };\n");
        ID3D10Blob_Release(blob);
    }
    return 0;
}
