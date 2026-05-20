// 텍스처 매핑 + MVP 변환을 적용하는 Vertex / Pixel Shader

// ── Constant Buffer (b0) ─────────────────────────────────────────────
// CPU 측 ObjectCB 와 동일한 레이아웃.
// mvp  : DM 행 우선 행렬을 그대로 전달 — HLSL 열 우선 읽기가 자동 전치한다.
// color: SpriteRenderer::colorTint (기본값 float4(1,1,1,1) = 원본 색상 유지).
cbuffer ObjectCB : register(b0)
{
    float4x4 mvp;
    float4   color;
};

// ── 텍스처 / 샘플러 ──────────────────────────────────────────────────
Texture2D    g_texture : register(t0);
SamplerState g_sampler : register(s0);

// ── 입출력 구조체 ─────────────────────────────────────────────────────
struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

// ── Vertex Shader ────────────────────────────────────────────────────
// mul(mvp, pos): 열벡터에 행렬을 왼쪽에서 곱하는 열 우선 방식.
// HLSL cbuffer는 열 우선(column-major)으로 메모리를 읽으므로,
// CPU에서 행 우선(row-major) DM 행렬을 그대로 전달하면 HLSL이 자동으로 전치해서 읽는다.
// 즉 HLSL은 MVP^T를 가지고 mul(MVP^T, v) = 올바른 변환을 수행한다.
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(mvp, float4(input.position, 1.0f));
    output.uv       = input.uv;
    return output;
}

// ── Pixel Shader ─────────────────────────────────────────────────────
// 텍스처 색상에 colorTint 를 곱한다 (component-wise).
// tint = (1,1,1,1) 이면 원본 그대로, (1,0,0,1) 이면 붉은색으로 착색.
float4 PSMain(PSInput input) : SV_TARGET
{
    return g_texture.Sample(g_sampler, input.uv) * color;
}
