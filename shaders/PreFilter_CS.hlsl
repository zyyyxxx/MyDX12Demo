
#define BLOCK_SIZE 16

struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

struct PreFilter
{
    // Size of the cubemap face in pixels at the current mipmap level.
    uint CubemapSize;
};


ConstantBuffer<PreFilter> PreFilterCB : register(b0);

TextureCube<float4> SrcTexture : register(t0);

RWTexture2DArray<float4> DstMip1 : register(u0);
RWTexture2DArray<float4> DstMip2 : register(u1);
RWTexture2DArray<float4> DstMip3 : register(u2);
RWTexture2DArray<float4> DstMip4 : register(u3);
RWTexture2DArray<float4> DstMip5 : register(u4);

SamplerState LinearRepeatSampler : register(s0);

#define GenerateMips_RootSignature \
    "RootFlags(0), " \
    "RootConstants(b0, num32BitConstants = 1), " \
    "DescriptorTable( SRV(t0, numDescriptors = 1) )," \
    "DescriptorTable( UAV(u0, numDescriptors = 5) )," \
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

// ----------------------------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}
// ----------------------------------------------------------------------------
// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// efficient VanDerCorpus calculation.
float RadicalInverse_VdC(uint bits) 
{
     bits = (bits << 16u) | (bits >> 16u);
     bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
     bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
     bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
     bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
     return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
// ----------------------------------------------------------------------------
float2 Hammersley(uint i, uint N)
{
	return float2(float(i)/float(N), RadicalInverse_VdC(i));
}
// ----------------------------------------------------------------------------
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
	float a = roughness*roughness;
	
	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	
	// from spherical coordinates to cartesian coordinates - halfway vector
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;
	
	// from tangent-space H vector to world-space sample vector
	float3 up          = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	float3 tangent   = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);
	
	float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}
// ----------------------------------------------------------------------------


float4 PreFilterSampler(float roughness , float3 N , float3 V)
{
	
	const uint SAMPLE_COUNT = 1024u;
	float3 prefilteredColor = float3(0.0 , 0.0 , 0.0);
	float totalWeight = 0.0;
	
	for(uint i = 0u; i < SAMPLE_COUNT; ++i)
	{
		// generates a sample vector that's biased towards the preferred alignment direction (importance sampling).
		float2 Xi = Hammersley(i, SAMPLE_COUNT);
		float3 H = ImportanceSampleGGX(Xi, N, roughness);
		float3 L  = normalize(2.0 * dot(V, H) * H - V);

		float NdotL = max(dot(N, L), 0.0);
		if(NdotL > 0.0)
		{
			// sample from the environment's mip level based on roughness/pdf
			float D   = DistributionGGX(N, H, roughness);
			float NdotH = max(dot(N, H), 0.0);
			float HdotV = max(dot(H, V), 0.0);
			float pdf = D * NdotH / (4.0 * HdotV) + 0.0001; 

			float resolution = 512.0; // resolution of source cubemap (per face)
			float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
			float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);

			float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel); 
            
			prefilteredColor += SrcTexture.SampleLevel(LinearRepeatSampler , L , mipLevel).rgb * NdotL;
			
			totalWeight      += NdotL;
		}
	}

	prefilteredColor = prefilteredColor / totalWeight;
	
	return float4(prefilteredColor, 1.0);
}

[RootSignature(GenerateMips_RootSignature)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void main( ComputeShaderInput IN )
{
	// Cubemap texture coords.
	uint3 cord = IN.DispatchThreadID;
    
	// First check if the thread is in the cubemap dimensions.
	if (cord.x >= PreFilterCB.CubemapSize || cord.y >= PreFilterCB.CubemapSize) return;

	// Map the UV coords of the cubemap face to a direction
	// [(0, 0), (1, 1)] => [(-0.5, -0.5), (0.5, 0.5)]
	float3 dir = float3( cord.xy / float(PreFilterCB.CubemapSize) - 0.5f, 0.5f);

	// Rotate to cubemap face
	dir = normalize( mul( RotateUV[cord.z], dir ) );

	float3 N = dir;
    
	// make the simplifying assumption that V equals R equals the normal 
	float3 R = N;
	float3 V = R;
	
	
    DstMip1[cord] = PreFilterSampler(0.0, N, V);
	
	DstMip2[cord] = PreFilterSampler(0.25, N, V);

    DstMip3[cord] =  PreFilterSampler(0.50, N, V);

    DstMip4[cord] =  PreFilterSampler(0.75, N, V);
	
    DstMip5[cord] =  PreFilterSampler(1.0, N, V);
}