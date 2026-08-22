// Composites the SDR-gamma ImGui offscreen texture onto the real swapchain
// back buffer, converting to the back buffer's actual HDR encoding so UI
// colors match what an SDR display would have shown instead of blowing out.
Texture2D    g_tex  : register(t0);
SamplerState g_samp : register(s0);

cbuffer Params : register(b0)
{
    uint   g_mode;           // 0 = SDR passthrough, 1 = scRGB linear, 2 = HDR10 PQ
    float  g_paperWhiteNits; // SDR white level to target inside the HDR range
    float2 g_pad;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2); // (0,0) (2,0) (0,2)
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}

float3 SrgbToLinear(float3 c)
{
    float3 lo = c / 12.92;
    float3 hi = pow(max((c + 0.055) / 1.055, 0.0), 2.4);
    return lerp(hi, lo, c <= 0.04045);
}

static const float3x3 kRec709toRec2020 =
{
    0.6274040, 0.3292820, 0.0433136,
    0.0690970, 0.9195400, 0.0113612,
    0.0163916, 0.0880132, 0.8955950
};

// SMPTE ST.2084 (PQ) inverse EOTF: linear [0,1] (1.0 == 10000 nits) -> PQ code value.
float3 LinearToPQ(float3 c)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 cp  = pow(max(c, 0.0), m1);
    float3 num = c1 + c2 * cp;
    float3 den = 1.0 + c3 * cp;
    return pow(num / den, m2);
}

float4 PSMain(VSOut i) : SV_Target
{
    float4 c = g_tex.Sample(g_samp, i.uv);
    if (g_mode == 0)
        return c;

    float3 lin = SrgbToLinear(c.rgb);
    if (g_mode == 1)
    {
        // scRGB linear: a value of 1.0 represents 80 nits.
        c.rgb = lin * (g_paperWhiteNits / 80.0);
    }
    else
    {
        // HDR10: BT.2020 primaries, ST.2084 (PQ) transfer, 1.0 == 10000 nits.
        float3 rec2020 = mul(kRec709toRec2020, lin);
        c.rgb = LinearToPQ(rec2020 * (g_paperWhiteNits / 10000.0));
    }
    return c;
}
