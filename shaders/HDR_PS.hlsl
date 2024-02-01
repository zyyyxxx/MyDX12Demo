static const float PI = 3.14159265359;

struct PixelShaderInput
{
    float4 PositionWorld : POSITION;
    float3 Normal   : NORMAL;
    float2 TexCoord   : TEXCOORD;
};

struct Material
{
    float3 Albedo;
    float Metallic;
    float Roughness;
    float AO;
    float2 Padding;
};

struct PointLight
{
    float4 PositionWS; // Light position in world space.
    //----------------------------------- (16 byte boundary)
    float4 PositionVS; // Light position in view space.
    //----------------------------------- (16 byte boundary)
    float4 Color;
    //----------------------------------- (16 byte boundary)
    float       Intensity;
    float       Attenuation;
    float2      Padding;                // Pad to 16 bytes
    //----------------------------------- (16 byte boundary)
    // Total:                              16 * 4 = 64 bytes
};

struct SpotLight
{
    float4 PositionWS; // Light position in world space.
    //----------------------------------- (16 byte boundary)
    float4 PositionVS; // Light position in view space.
    //----------------------------------- (16 byte boundary)
    float4 DirectionWS; // Light direction in world space.
    //----------------------------------- (16 byte boundary)
    float4 DirectionVS; // Light direction in view space.
    //----------------------------------- (16 byte boundary)
    float4 Color;
    //----------------------------------- (16 byte boundary)
    float       Intensity;
    float       SpotAngle;
    float       Attenuation;
    float       Padding;                // Pad to 16 bytes.
    //----------------------------------- (16 byte boundary)
    // Total:                              16 * 6 = 96 bytes
};

struct LightProperties
{
    uint NumPointLights;
};

struct CameraData
{
    float3 Position;
};

ConstantBuffer<Material> MaterialCB : register( b0, space1 );
ConstantBuffer<LightProperties> LightPropertiesCB : register( b1 );
ConstantBuffer<CameraData> CameraDataCB : register(b2);

StructuredBuffer<PointLight> PointLights : register( t0 );
StructuredBuffer<SpotLight> SpotLights : register( t1 );
Texture2D DiffuseTexture            : register( t2 );
TextureCube IrradianceConvolution : register(t3);

SamplerState LinearRepeatSampler    : register(s0);

float3 Fresnel_Schlick( float cosTheta, float3 F0 )
{
    return F0 + ( 1.0 - F0 ) * pow( 1.0 - cosTheta, 5.0 );
}

float Distribution_GGX( float3 N, float3 H, float fRoughness )
{
    float a = fRoughness * fRoughness;
    float a2 = a * a;
    float NdotH = max( dot( N, H ), 0.0 );
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = ( NdotH2 * ( a2 - 1.0 ) + 1.0 );
    denom = PI * denom * denom;

    return num / denom;
}

float Geometry_Schlick_GGX( float NdotV, float fRoughness )
{
    // Dircet Light: k = (fRoughness + 1)^2 / 8;
    // IBL: k = （fRoughness^2)/2;
    float r = ( fRoughness + 1.0 );
    float k = ( r * r ) / 8.0;

    float num = NdotV;
    float denom = NdotV * ( 1.0 - k ) + k;

    return num / denom;
}

float Geometry_Smith( float3 N, float3 V, float3 L, float fRoughness )
{
    float NdotV = max( dot( N, V ), 0.0 );
    float NdotL = max( dot( N, L ), 0.0 );
    float ggx2 = Geometry_Schlick_GGX( NdotV, fRoughness );
    float ggx1 = Geometry_Schlick_GGX( NdotL, fRoughness );

    return ggx1 * ggx2;
}


float4 main( PixelShaderInput IN ) : SV_Target
{
    float3 N = normalize( IN.Normal.xyz );
    // View Vector
    float3 V = normalize( CameraDataCB.Position.xyz - IN.PositionWorld.xyz );

    float3 F0 = float3( 0.04f, 0.04f, 0.04f );
    
    // Gamma Correction
    float3 Albedo = pow( MaterialCB.Albedo, 2.2f );
    
    F0 = lerp( F0, Albedo, MaterialCB.Metallic );
    
    // Out Radiance
    float3 Lo = float3( 0.0f, 0.0f, 0.0f );

    // Roughness
    float Roughness = MaterialCB.Roughness;
    
    for ( int i = 0; i < LightPropertiesCB.NumPointLights; ++i )
    {// Each Light
        // Point Light
        PointLight Light = PointLights[i];
        
        // In
        float3 L = normalize( Light.PositionWS.xyz - IN.PositionWorld.xyz );
        // Intermediate vector (bisector of the angle of incident light to normal)
        float3 H = normalize( V + L );
        float distance = length( Light.PositionWS.xyz - IN.PositionWorld.xyz );

        //float attenuation = 1.0 / ( distance * distance );
        // If Gamma has been corrected, there is no need for a second inverse attenuation, and a single attenuation is fine
        float attenuation = 1.0 / distance ;
        
        float3 radiance = Light.Color.xyz * attenuation;

        // Cook-Torrance BRDF
        float NDF = Distribution_GGX( N, H, Roughness );
        float G = Geometry_Smith( N, V, L, Roughness );
        float3 F = Fresnel_Schlick( max( dot( H, V ), 0.0 ), F0 );

        float3 kS = F;
        float3 kD = float3( 1.0f,1.0f,1.0f ) - kS;
        kD *= 1.0 - MaterialCB.Metallic;

        float3 numerator = NDF * G * F;
        float denominator = 4.0 * max( dot( N, V ), 0.0 ) * max( dot( N, L ), 0.0 );
        float3 specular = numerator / max( denominator, 0.001 );

        // add to outgoing radiance Lo
        float NdotL = max( dot( N, L ), 0.0 );
        Lo += ( kD * Albedo / PI + specular ) * radiance * NdotL;
    }

    float fAO = MaterialCB.AO;
    // Ambient Light
    float3 ambient = float3( 0.03f, 0.03f, 0.03f ) * Albedo * fAO;
    float3 color = ambient + Lo;
    // HDR tonemapping
    color = color / ( color + float3( 1.0f,1.0f,1.0f ) );
    // Gamma 
    color = pow( color, 1.0f / 2.2f );
    return float4( color, 1.0 );
    
}