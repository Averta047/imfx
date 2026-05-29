#pragma once

static const char* g_depth_shader = R"(
    Texture2D     BackBuffer  : register(t0);
    SamplerState  BackSampler : register(s0);
 
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
 
    float mod(float x, float y)
    {
        return x - y * floor(x / y);
    }

    float sdBox( float3 p, float3 b )
    {
        float3 q = abs(p) - b;
        return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
    }

    float map(float3 p, out float3 cellColor)
    {
        const float _GridSize = 512.0;
        const float _PlaneSize = 1.0;
        const float _MinDepth = -.5;
        const float _MaxDepth =  .5;

        float2 grid = float2(_GridSize, _GridSize);
        float2 planeSize = float2(4.0, 4.0);

        float2 cellSize = planeSize / grid;

        float voxelFill = _PlaneSize;

        float2 gv = (p.xy + planeSize * 0.5) / planeSize;

        if (gv.x < 0.0 || gv.x > 1.0 ||
            gv.y < 0.0 || gv.y > 1.0)
        {
            cellColor = 0;
            return 9999.0;
        }

        float2 cell = floor(gv * grid);
        float2 cellUV = (cell + 0.5) / grid;

        float2 local =
            frac((p.xy + planeSize * 0.5) / cellSize) - 0.5;

        local *= cellSize;

        float3 tex = BackBuffer.Sample(BackSampler, cellUV).rgb;
        float depth = dot(tex, float3(.333, .333, .333));
        float depth01 = saturate(depth / _MaxDepth);
        depth01 = floor(depth01 * _GridSize) / _GridSize;

        cellColor = tex;

        float depthDisplacement = lerp(
            _MinDepth,
            _MaxDepth,
            depth01
        );

        float z = p.z + depthDisplacement;

        float3 q = float3(local, z);

        float thickness = cellSize.x * 0.05;

        float3 b = float3(
            cellSize * voxelFill * 0.5,
            thickness
        );

        float2 d2 = abs(local) - (cellSize * voxelFill * 0.5);

        float dXY = max(d2.x, d2.y);
        float dZ = abs(z) - thickness;

        return max(dXY, dZ);
    }

    float3 march(in float3 ro, in float3 rd, in out float t)
    {
        float3 col = 0;

        const int MAX_STEPS = 40;
        const float MAX_DIST = 100.0;

        for (int i = 0; i < MAX_STEPS; i++)
        {
            float3 p = ro + rd * t;

            float3 voxelColor;
            float d = map(p, voxelColor);

            // adaptive epsilon prevents ringing
            float hitEpsilon = max(0.0005, t * 0.00015);

            if (d < hitEpsilon)
            {
                col = voxelColor;
                break;
            }

            if (t > MAX_DIST)
                break;

            t += abs(d) * 0.3;
        }

        return col;
    }

    float4 main(PS_IN i) : SV_TARGET
    {
        float2 uv = i.uv.xy - .5;

        float t = 0;
        float3 ro = float3(0,0,-5.5);
        float3 rd = normalize(float3(uv, 1));
        float3 col = march(ro, rd, t);
 
        return float4(col.rgb, 1.0);
    }
)";