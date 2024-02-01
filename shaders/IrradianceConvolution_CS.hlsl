/**
 * This compute shader is used to convert a cubemap image into a irradiance convolution cubemap.
 */

#define BLOCK_SIZE 16

struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

struct IrradianceConvolution
{
    uint CubemapSize;
};

ConstantBuffer<IrradianceConvolution> IrradianceConvolutionCB : register(b0);

TextureCube<float4> SrcTexture : register(t0);


RWTexture2DArray<float4> DstTexture : register(u0);

// Linear repeat sampler.
SamplerState LinearRepeatSampler : register(s0);

#define GenerateMips_RootSignature \
    "RootFlags(0), " \
    "RootConstants(b0, num32BitConstants = 1), " \
    "DescriptorTable( SRV(t0, numDescriptors = 1) )," \
    "DescriptorTable( UAV(u0, numDescriptors = 1) )," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_WRAP," \
        "addressV = TEXTURE_ADDRESS_WRAP," \
        "addressW = TEXTURE_ADDRESS_WRAP," \
        "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT )"


// 1 / PI
static const float InvPI = 0.31830988618379067153776752674503f;
static const float Inv2PI = 0.15915494309189533576888376337251f;
static const float2 InvAtan = float2(Inv2PI, InvPI);
static const float PI = 3.14159265359;

// Transform from dispatch ID to cubemap face direction
static const float3x3 RotateUV[6] = {
    // +X
    float3x3(  0,  0,  1,
               0, -1,  0,
              -1,  0,  0 ),
    // -X
    float3x3(  0,  0, -1,
               0, -1,  0,
               1,  0,  0 ),
    // +Y
    float3x3(  1,  0,  0,
               0,  0,  1,
               0,  1,  0 ),
    // -Y
    float3x3(  1,  0,  0,
               0,  0, -1,
               0, -1,  0 ),
    // +Z
    float3x3(  1,  0,  0,
               0, -1,  0,
               0,  0,  1 ),
    // -Z
    float3x3( -1,  0,  0,
               0, -1,  0,
               0,  0, -1 )
};



[RootSignature(GenerateMips_RootSignature)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( ComputeShaderInput IN )
{
    // Cubemap texture coords.
    uint3 cord = IN.DispatchThreadID;
    
    // First check if the thread is in the cubemap dimensions.
    if (cord.x >= IrradianceConvolutionCB.CubemapSize || cord.y >= IrradianceConvolutionCB.CubemapSize) return;

    // Map the UV coords of the cubemap face to a direction
    // [(0, 0), (1, 1)] => [(-0.5, -0.5), (0.5, 0.5)]
    float3 dir = float3( cord.xy / float(IrradianceConvolutionCB.CubemapSize) - 0.5f, 0.5f);

    // Rotate to cubemap face
    dir = normalize( mul( RotateUV[cord.z], dir ) );

    float3 Irradiance = float3(0.0f, 0.0f, 0.0f);
    
    // The sample direction equals the hemisphere's orientation
    float3 Normal = dir;
    float3 Up = float3(0.0, 1.0, 0.0);
    float3 Right = cross(Up, Normal);
    Up = cross(Normal, Right);

    float SampleDelta = 0.025f;
    float SampleCount = 0.0f;
    for (float phi = 0.0f; phi < 2.0f * PI; phi += SampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += SampleDelta)
        {
            // Spherical to cartesian (in tangent space)
            float3 TangentSample = float3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // Tangent space to world
            float3 SampleVec = TangentSample.x * Right + TangentSample.y * Up + TangentSample.z * Normal;
            
            Irradiance += SrcTexture.Sample(LinearRepeatSampler, SampleVec).rgb * cos(theta) * sin(theta);
            SampleCount++;
        }
    }
    Irradiance = PI * Irradiance * (1.0f / SampleCount);

    
    DstTexture[cord] = float4(Irradiance , 1.0f);
}