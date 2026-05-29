#pragma once

static const char* g_sine_distort_ps = R"(
    Texture2D    BackBuffer  : register(t0);
    SamplerState BackSampler : register(s0);
 
    cbuffer ImFXParamCB : register(b0)
    {
        float2 Resolution;
        float  Time;
        float  Speed;
        float  Scale;
        float  Density;
        float2 _Pad;
        float4 ColorA;
        float4 ColorB;
    };
 
    struct PS_IN { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
 
    float4 main(PS_IN i) : SV_TARGET
    {
        float t   = Time * Speed;
        float ar  = Resolution.x / max(Resolution.y, 1.0);
        float amp = Density * 0.06;
 
        float wx = sin(i.uv.y * Scale * 6.2831 + t * 1.3) * amp;
        float wy = cos(i.uv.x * Scale * 6.2831 * ar + t * 0.9) * amp * 0.6;
 
        float4 scene = BackBuffer.Sample(BackSampler, saturate(i.uv + float2(wx, wy)));
 
        float2 vig  = abs(i.uv * 2.0 - 1.0);
        float  edge = pow(max(vig.x, vig.y), 5.0);
        scene.rgb   = lerp(scene.rgb, ColorB.rgb, edge * ColorB.a);
 
        return float4(scene.rgb, 1.0);
    }
)";
