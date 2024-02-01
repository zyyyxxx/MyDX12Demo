#pragma once
#include <DirectXMath.h>

struct PBRMaterial
{
    PBRMaterial(DirectX::XMFLOAT3 albedo = {0.0 , 0.0 , 0.0}, float metallic = 0.0, float roughness = 0.0, float ao = 0.0)
        : Albedo( albedo )
        , Metallic( metallic )
        , Roughness( roughness )
        , AO( ao )
    {}
    
    DirectX::XMFLOAT3 Albedo; // 12
    float Metallic;           // 4
    float Roughness;          // 4
    float AO;                 // 4
    // -------------------------24 bytes

    uint32_t Padding[2]; // 8 bytes
    //--------------------------32 bytes
    
    static const PBRMaterial Test;

};