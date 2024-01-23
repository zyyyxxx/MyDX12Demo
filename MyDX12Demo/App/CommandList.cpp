#include <DX12LibPCH.h>

#include <CommandList.h>

#include <Application.h>
#include <ByteAddressBuffer.h>
#include <ConstantBuffer.h>
#include <IndexBuffer.h>
#include <CommandQueue.h>
#include <DynamicDescriptorHeap.h>
#include <GenerateMipsPSO.h>
#include <PanoToCubemapPSO.h>
#include <RenderTarget.h>
#include <Resource.h>
#include <ResourceStateTracker.h>
#include <RootSignature.h>
#include <StructuredBuffer.h>
#include <Texture.h>
#include <UploadBuffer.h>
#include <VertexBuffer.h>

// static
std::map<std::wstring, ID3D12Resource* > CommandList::ms_TextureCache;
std::mutex CommandList::ms_TextureCacheMutex;

CommandList::CommandList( D3D12_COMMAND_LIST_TYPE type )
    : m_d3d12CommandListType( type )
{
    auto device = Application::Get().GetDevice();

    // 创建命令分配器和命令列表。
    ThrowIfFailed( device->CreateCommandAllocator( m_d3d12CommandListType, IID_PPV_ARGS( &m_d3d12CommandAllocator ) ) );

    ThrowIfFailed( device->CreateCommandList( 0, m_d3d12CommandListType, m_d3d12CommandAllocator.Get(),
                                              nullptr, IID_PPV_ARGS( &m_d3d12CommandList ) ) );

    // 上传资源至GPU 线性分配器
    m_UploadBuffer = std::make_unique<UploadBuffer>();
    // 资源状态跟踪器（追踪CommandList 资源或子资源 状态）
    m_ResourceStateTracker = std::make_unique<ResourceStateTracker>();

    for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
    {
        // GPU 可见描述符堆
        m_DynamicDescriptorHeap[i] = std::make_unique<DynamicDescriptorHeap>( static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>( i ) );
        m_DescriptorHeaps[i] = nullptr;
    }
}


CommandList::~CommandList()
{}


#pragma region Barriers

void CommandList::TransitionBarrier( const Resource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subResource, bool flushBarriers )
{
    auto d3d12Resource = resource.GetD3D12Resource();
    if ( d3d12Resource )
    {
        // 先前的状态并不重要。它将由资源状态跟踪器解决。
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition( d3d12Resource.Get(), D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource );
        m_ResourceStateTracker->ResourceBarrier( barrier );
    }

    if ( flushBarriers )
    {
        FlushResourceBarriers();
    }
}

void CommandList::UAVBarrier( const Resource& resource, bool flushBarriers )
{
    auto d3d12Resource = resource.GetD3D12Resource();
    auto barrier = CD3DX12_RESOURCE_BARRIER::UAV( d3d12Resource.Get() );

    m_ResourceStateTracker->ResourceBarrier( barrier );

    if ( flushBarriers )
    {
        FlushResourceBarriers();
    }
}

void CommandList::AliasingBarrier(const Resource& beforeResource, const Resource& afterResource, bool flushBarriers)
{
    auto d3d12BeforeResource = beforeResource.GetD3D12Resource();
    auto d3d12AfterResource = afterResource.GetD3D12Resource();
    auto barrier = CD3DX12_RESOURCE_BARRIER::Aliasing(d3d12BeforeResource.Get(), d3d12AfterResource.Get() );

    m_ResourceStateTracker->ResourceBarrier(barrier);

    if (flushBarriers)
    {
        FlushResourceBarriers();
    }
}


void CommandList::FlushResourceBarriers()
{
    m_ResourceStateTracker->FlushResourceBarriers( *this );
}

#pragma endregion

#pragma region Resources
void CommandList::CopyResource( Resource& dstRes, const Resource& srcRes )
{
    TransitionBarrier( dstRes, D3D12_RESOURCE_STATE_COPY_DEST );
    TransitionBarrier( srcRes, D3D12_RESOURCE_STATE_COPY_SOURCE );

    FlushResourceBarriers();

    m_d3d12CommandList->CopyResource( dstRes.GetD3D12Resource().Get(), srcRes.GetD3D12Resource().Get() );

    TrackResource(dstRes);
    TrackResource(srcRes);
}

void CommandList::ResolveSubresource( Resource& dstRes, const Resource& srcRes, uint32_t dstSubresource, uint32_t srcSubresource )
{
    TransitionBarrier( dstRes, D3D12_RESOURCE_STATE_RESOLVE_DEST, dstSubresource );
    TransitionBarrier( srcRes, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, srcSubresource );

    FlushResourceBarriers();

    // 将资源的子资源（通常是多重采样的纹理）的内容复制到另一个资源的子资源中
    m_d3d12CommandList->ResolveSubresource( dstRes.GetD3D12Resource().Get(), dstSubresource, srcRes.GetD3D12Resource().Get(), srcSubresource, dstRes.GetD3D12ResourceDesc().Format );

    TrackResource( srcRes );
    TrackResource( dstRes );
}



void CommandList::CopyBuffer( Buffer& buffer, size_t numElements, size_t elementSize, const void* bufferData, D3D12_RESOURCE_FLAGS flags )
{
    auto device = Application::Get().GetDevice();

    size_t bufferSize = numElements * elementSize;

    ComPtr<ID3D12Resource> d3d12Resource;
    if ( bufferSize == 0 )
    {
        // 这将导致 NULL 资源（可能需要定义默认的 null 资源）。
    }
    else
    {
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT );
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer( bufferSize, flags );
        ThrowIfFailed( device->CreateCommittedResource( 
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&d3d12Resource)));

        // 将资源添加到全局资源状态跟踪器。
        ResourceStateTracker::AddGlobalResourceState( d3d12Resource.Get(), D3D12_RESOURCE_STATE_COMMON);

        if ( bufferData != nullptr )
        {
            // 创建上传资源以用作中间缓冲区以复制缓冲区资源
            ComPtr<ID3D12Resource> uploadResource;
            const auto uploadResourceHeapProperties = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD );
            const auto uploadResourceResourceDesc = CD3DX12_RESOURCE_DESC::Buffer( bufferSize );
            ThrowIfFailed( device->CreateCommittedResource( 
                &uploadResourceHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &uploadResourceResourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&uploadResource)));

            D3D12_SUBRESOURCE_DATA subresourceData = {};
            subresourceData.pData = bufferData;
            subresourceData.RowPitch = bufferSize;
            subresourceData.SlicePitch = subresourceData.RowPitch;

            m_ResourceStateTracker->TransitionResource(d3d12Resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
            FlushResourceBarriers();

            UpdateSubresources( m_d3d12CommandList.Get(), d3d12Resource.Get(),
                uploadResource.Get(), 0, 0, 1, &subresourceData );

            // 添加对资源的引用，使它们保持在作用域内，直到命令列表重置。
            TrackObject(uploadResource);
        }
        TrackObject(d3d12Resource);
    }

    buffer.SetD3D12Resource( d3d12Resource );
    buffer.CreateViews( numElements, elementSize );
}

void CommandList::CopyVertexBuffer( VertexBuffer& vertexBuffer, size_t numVertices, size_t vertexStride, const void* vertexBufferData )
{
    CopyBuffer( vertexBuffer, numVertices, vertexStride, vertexBufferData );
}

void CommandList::CopyIndexBuffer( IndexBuffer& indexBuffer, size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData )
{
    size_t indexSizeInBytes = indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
    CopyBuffer( indexBuffer, numIndicies, indexSizeInBytes, indexBufferData );
}

void CommandList::CopyByteAddressBuffer( ByteAddressBuffer& byteAddressBuffer, size_t bufferSize, const void* bufferData )
{
    CopyBuffer( byteAddressBuffer, 1, bufferSize, bufferData, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
}

void CommandList::CopyStructuredBuffer( StructuredBuffer& structuredBuffer, size_t numElements, size_t elementSize, const void* bufferData )
{
    CopyBuffer( structuredBuffer, numElements, elementSize, bufferData, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS );
}

#pragma endregion

#pragma region Textures

void CommandList::LoadTextureFromFile( Texture& texture, const std::wstring& fileName, TextureUsage textureUsage )
{
    auto device = Application::Get().GetDevice();

    fs::path filePath( fileName );
    if ( !fs::exists( filePath ) )
    {
        throw std::exception( "File not found." );
    }

    auto iter = ms_TextureCache.find( fileName );
    if ( iter != ms_TextureCache.end() )
    {
        texture.SetTextureUsage(textureUsage);
        texture.SetD3D12Resource(iter->second);
        texture.CreateViews();
        texture.SetName(fileName);
    }
    else
    {
        TexMetadata metadata;
        ScratchImage scratchImage;
        Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;

        if ( filePath.extension() == ".dds" )
        {
            // Use DDS texture loader.
            ThrowIfFailed( LoadFromDDSFile( fileName.c_str(), DDS_FLAGS_NONE, &metadata, scratchImage ) );
        }
        else if ( filePath.extension() == ".hdr" )
        {
            ThrowIfFailed( LoadFromHDRFile( fileName.c_str(), &metadata, scratchImage ) );
        }
        else if ( filePath.extension() == ".tga" )
        {
            ThrowIfFailed( LoadFromTGAFile( fileName.c_str(), &metadata, scratchImage ) );
        }
        else
        {
            ThrowIfFailed( LoadFromWICFile( fileName.c_str(), WIC_FLAGS_NONE, &metadata, scratchImage ) );
        }

        if ( textureUsage == TextureUsage::Albedo )
        {
            metadata.format = MakeSRGB( metadata.format );
        }

        D3D12_RESOURCE_DESC textureDesc = {};
        switch ( metadata.dimension )
        {
            case TEX_DIMENSION_TEXTURE1D:
                textureDesc = CD3DX12_RESOURCE_DESC::Tex1D( metadata.format, static_cast<UINT64>( metadata.width ), static_cast<UINT16>(metadata.arraySize) );
                break;
            case TEX_DIMENSION_TEXTURE2D:
                textureDesc = CD3DX12_RESOURCE_DESC::Tex2D( metadata.format, static_cast<UINT64>( metadata.width ), static_cast<UINT>(metadata.height), static_cast<UINT16>(metadata.arraySize) );
                break;
            case TEX_DIMENSION_TEXTURE3D:
                textureDesc = CD3DX12_RESOURCE_DESC::Tex3D( metadata.format, static_cast<UINT64>( metadata.width ), static_cast<UINT>(metadata.height), static_cast<UINT16>(metadata.depth) );
                break;
            default:
                throw std::exception( "Invalid texture dimension." );
                break;
        }

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT );
        ThrowIfFailed( device->CreateCommittedResource( &heapProperties,
                                                        D3D12_HEAP_FLAG_NONE,
                                                        &textureDesc,
                                                        D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr,
                                                        IID_PPV_ARGS( &textureResource ) ) );

        // 更新全局状态跟踪器。
        ResourceStateTracker::AddGlobalResourceState( textureResource.Get(), D3D12_RESOURCE_STATE_COMMON );

        texture.SetTextureUsage( textureUsage );
        texture.SetD3D12Resource( textureResource );
        texture.CreateViews();
        texture.SetName(fileName);

        std::vector<D3D12_SUBRESOURCE_DATA> subresources( scratchImage.GetImageCount() );
        const Image* pImages = scratchImage.GetImages();
        for ( int i = 0; i < scratchImage.GetImageCount(); ++i )
        {
            auto& subresource = subresources[i];
            subresource.RowPitch = pImages[i].rowPitch;
            subresource.SlicePitch = pImages[i].slicePitch;
            subresource.pData = pImages[i].pixels;
        }
        
        CopyTextureSubresource( texture, 0, static_cast<uint32_t>( subresources.size() ), subresources.data() );

        if ( subresources.size() < textureResource->GetDesc().MipLevels )
        {
            GenerateMips( texture );
        }

        // 将纹理资源添加到纹理缓存中
        std::lock_guard<std::mutex> lock( ms_TextureCacheMutex );
        ms_TextureCache[fileName] = textureResource.Get();
    }
}

void CommandList::GenerateMips( Texture& texture )
{
    if ( m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COPY )
    {
        if ( !m_ComputeCommandList )
        {
            m_ComputeCommandList = Application::Get().GetCommandQueue( D3D12_COMMAND_LIST_TYPE_COMPUTE )->GetCommandList();
        }
        m_ComputeCommandList->GenerateMips( texture );
        return;
    }

    auto d3d12Resource = texture.GetD3D12Resource();

    // 如果纹理没有有效的资源，则不执行任何操作
    if ( !d3d12Resource ) return;
    auto d3d12ResourceDesc = d3d12Resource->GetDesc();

    // 如果纹理只有一个 mip 级别（级别 0），则不执行任何操作
    if ( d3d12ResourceDesc.MipLevels == 1 ) return;
    // 仅支持 2D 纹理
    if ( d3d12ResourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d3d12ResourceDesc.DepthOrArraySize != 1 )
    {
        throw std::exception( "Generate Mips only supports 2D Textures." );
    }

    if ( Texture::IsUAVCompatibleFormat( d3d12ResourceDesc.Format ) )
    {
        GenerateMips_UAV( texture );
    }
    else if ( Texture::IsBGRFormat( d3d12ResourceDesc.Format ) )
    {
        GenerateMips_BGR( texture );
    }
    else if ( Texture::IsSRGBFormat( d3d12ResourceDesc.Format ) )
    {
        GenerateMips_sRGB( texture );
    }
    else
    {
        throw std::exception( "Unsupported texture format for mipmap generation." );
    }
}

void CommandList::GenerateMips_UAV( Texture& texture )
{
    if ( !m_GenerateMipsPSO )
    {
        m_GenerateMipsPSO = std::make_unique<GenerateMipsPSO>();
    }

    auto device = Application::Get().GetDevice();

    auto resource = texture.GetD3D12Resource();
    auto resourceDesc = resource->GetDesc();

    auto stagingResource = resource;
    Texture stagingTexture( stagingResource );
    // 如果传入的资源不允许 UAV 访问，则创建用于生成 mipmap 链的暂存资源
    if ( ( resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ) == 0 )
    {
        auto stagingDesc = resourceDesc;
        stagingDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT );
        ThrowIfFailed( device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &stagingDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS( &stagingResource )

        ) );

        ResourceStateTracker::AddGlobalResourceState( stagingResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST );

        stagingTexture.SetD3D12Resource( stagingResource );
        stagingTexture.CreateViews();
        stagingTexture.SetName(L"Generate Mips UAV Staging Texture");

        CopyResource( stagingTexture, texture );
    }

    m_d3d12CommandList->SetPipelineState( m_GenerateMipsPSO->GetPipelineState().Get() );
    SetComputeRootSignature( m_GenerateMipsPSO->GetRootSignature() );

    GenerateMipsCB generateMipsCB;

    for ( uint32_t srcMip = 0; srcMip < resourceDesc.MipLevels - 1u; )
    {
        uint64_t srcWidth = resourceDesc.Width >> srcMip;
        uint32_t srcHeight = resourceDesc.Height >> srcMip;
        uint32_t dstWidth = static_cast<uint32_t>( srcWidth >> 1 );
        uint32_t dstHeight = srcHeight >> 1;

        // 根据源纹理的尺寸确定要使用的计算着色器。
        // 0b00(0): Both width and height are even.
        // 0b01(1): Width is odd, height is even.
        // 0b10(2): Width is even, height is odd.
        // 0b11(3): Both width and height are odd.
        generateMipsCB.SrcDimension = ( srcHeight & 1 ) << 1 | ( srcWidth & 1 );

        // 计算此传递的 mipmap 级别数（每次传递最多 4 mips）
        DWORD mipCount;
        
        // 我们可以将纹理的尺寸减小一半的次数。
        // 在宽度或高度的二进制表示中，每存在一个1，表示该维度为奇数。
        // 处理宽度或高度恰好为1的情况时，作为一种特殊情况处理（因为该维度不需要减小）
        _BitScanForward( &mipCount, ( dstWidth == 1 ? dstHeight : dstWidth ) | 
                                    ( dstHeight == 1 ? dstWidth : dstHeight ) );
        // 要生成的最大 mips 数为 4。
        mipCount = std::min<DWORD>( 4, mipCount + 1 );
        // Clamp 剩余的 mips 总数。
        mipCount = ( srcMip + mipCount ) > resourceDesc.MipLevels ? 
            resourceDesc.MipLevels - srcMip : mipCount;

        // 维度不应减少到 0
        // 如果宽度和高度不同，则可能会发生这种情况
        dstWidth = std::max<DWORD>( 1, dstWidth );
        dstHeight = std::max<DWORD>( 1, dstHeight );

        generateMipsCB.SrcMipLevel = srcMip;
        generateMipsCB.NumMipLevels = mipCount;
        generateMipsCB.TexelSize.x = 1.0f / (float)dstWidth;
        generateMipsCB.TexelSize.y = 1.0f / (float)dstHeight;

        SetCompute32BitConstants( GenerateMips::GenerateMipsCB, generateMipsCB );

        SetShaderResourceView( GenerateMips::SrcMip, 0, stagingTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, srcMip, 1 );
        for ( uint32_t mip = 0; mip < mipCount; ++mip )
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = resourceDesc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = srcMip + mip + 1;

            SetUnorderedAccessView(GenerateMips::OutMip, mip, stagingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, srcMip + mip + 1, 1, &uavDesc );
        }
        // 使用默认 UAV 填充任何未使用的 mip 级别。这样做可以使 DX12 运行时保持正常运行
        if ( mipCount < 4 )
        {
            m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors( GenerateMips::OutMip, mipCount, 4 - mipCount, m_GenerateMipsPSO->GetDefaultUAV() );
        }
        
        Dispatch( Math::DivideByMultiple(dstWidth, 8), Math::DivideByMultiple(dstHeight, 8) );

        UAVBarrier( stagingTexture );

        srcMip += mipCount;
    }

    // 复制回原始纹理。
    if ( stagingResource != resource )
    {
        CopyResource( texture, stagingTexture );
    }
}

void CommandList::GenerateMips_BGR( Texture& texture )
{
    //如果原始纹理的格式与 UAV 兼容纹理格式不在同一格式系列中，
    //则尝试使用不同格式系列为资源创建 UAV将导致错误（当启用调试层时）。
    
    auto device = Application::Get().GetDevice();

    auto resource = texture.GetD3D12Resource();
    auto resourceDesc = resource->GetDesc();

    // 使用 UAV 兼容格式和 UAV 标志创建新资源
    auto copyDesc = resourceDesc;
    copyDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    copyDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // 创建一个堆来别名资源。这用于在不使 GPU 验证失败的情况下复制资源。
    auto allocationInfo = device->GetResourceAllocationInfo(0, 1, &resourceDesc);
    auto bufferSize = GetRequiredIntermediateSize(resource.Get(), 0, resourceDesc.MipLevels);

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = allocationInfo.SizeInBytes;
    heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Heap> heap;
    ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)));

    ComPtr<ID3D12Resource> resourceCopy;
    ThrowIfFailed(device->CreatePlacedResource(
        heap.Get(),
        0,
        &copyDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resourceCopy)
    ));

    ResourceStateTracker::AddGlobalResourceState(resourceCopy.Get(), D3D12_RESOURCE_STATE_COMMON );

    Texture copyTexture(resourceCopy);

    // 创建要为其执行复制操作的别名。
    auto aliasDesc = resourceDesc;
    aliasDesc.Format = (resourceDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM || 
                        resourceDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) ? 
                        DXGI_FORMAT_B8G8R8X8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;

    ComPtr<ID3D12Resource> aliasCopy;
    ThrowIfFailed(device->CreatePlacedResource(
        heap.Get(),
        0,
        &aliasDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&aliasCopy)
    ));

    ResourceStateTracker::AddGlobalResourceState(aliasCopy.Get(), D3D12_RESOURCE_STATE_COMMON);

    // texture(BRG) -> aliasTexture(中介) -> copyTexture
    
    // 将原始纹理复制到别名纹理。
    Texture aliasTexture(aliasCopy);
    AliasingBarrier(Texture(), aliasTexture); // 没有“之前”的纹理 默认构造的纹理等同于“空”纹理。
    
    CopyResource(aliasTexture, texture);

    // 别名UAV兼容纹理返回。
    AliasingBarrier(aliasTexture, copyTexture);
    // 使用 copyTexture 生成 mips。
    GenerateMips_UAV(copyTexture);

    // 复制回原始文件（通过别名以确保 GPU 验证）
    AliasingBarrier(copyTexture, aliasTexture);
    CopyResource(texture, aliasTexture);

    // 跟踪资源以确保生命周期。
    TrackObject(heap);
    TrackResource(copyTexture);
    TrackResource(aliasTexture);
    TrackResource(texture);
}

void CommandList::GenerateMips_sRGB( Texture& texture )
{
    auto device = Application::Get().GetDevice();

    // 创建与 UAV 兼容的纹理。
    auto resource = texture.GetD3D12Resource();
    auto resourceDesc = resource->GetDesc();

    // 使用 UAV 兼容格式和 UAV 标志创建原始纹理的副本。
    auto copyDesc = resourceDesc;
    copyDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    copyDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // 将资源创建为堆中的Placed资源，以执行别名复制。
    // 创建一个堆来别名资源。这用于在不使 GPU 验证失败的情况下复制资源。
    auto allocationInfo = device->GetResourceAllocationInfo(0, 1, &resourceDesc);
    auto bufferSize = GetRequiredIntermediateSize(resource.Get(), 0, resourceDesc.MipLevels);

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = allocationInfo.SizeInBytes;
    heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Heap> heap;
    ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)));
    
    ComPtr<ID3D12Resource> resourceCopy;
    ThrowIfFailed(device->CreatePlacedResource(
        heap.Get(),
        0,
        &copyDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resourceCopy)
    ));

    ResourceStateTracker::AddGlobalResourceState(resourceCopy.Get(), D3D12_RESOURCE_STATE_COMMON);

    Texture copyTexture(resourceCopy);

    // 创建要为其执行复制操作的别名。
    auto aliasDesc = resourceDesc;

    ComPtr<ID3D12Resource> aliasCopy;
    ThrowIfFailed(device->CreatePlacedResource(
        heap.Get(),
        0,
        &aliasDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&aliasCopy)
    ));

    ResourceStateTracker::AddGlobalResourceState(aliasCopy.Get(), D3D12_RESOURCE_STATE_COMMON );

    // 将原始纹理复制到别名纹理。
    Texture aliasTexture(aliasCopy);
    AliasingBarrier(Texture(), aliasTexture); // 没有“之前”的纹理 默认构造的纹理等同于“空”纹理。
    CopyResource(aliasTexture, texture);

    // 别名UAV兼容纹理返回。
    AliasingBarrier(aliasTexture, copyTexture);
    // 现在，使用资源副本生成 mips。
    GenerateMips_UAV(copyTexture);

    // 复制回原始文件（通过别名以确保 GPU 验证）
    AliasingBarrier(copyTexture, aliasTexture);
    CopyResource(texture, aliasTexture);

    // 跟踪资源以确保生命周期。
    TrackObject(heap);
    TrackResource(copyTexture);
    TrackResource(aliasTexture);
    TrackResource(texture);
}

void CommandList::PanoToCubemap(Texture& cubemapTexture, const Texture& panoTexture )
{
    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COPY)
    {
        if (!m_ComputeCommandList)
        {
            m_ComputeCommandList = Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->GetCommandList();
        }
        m_ComputeCommandList->PanoToCubemap(cubemapTexture, panoTexture);
        return;
    }

    if (!m_PanoToCubemapPSO)
    {
        m_PanoToCubemapPSO = std::make_unique<PanoToCubemapPSO>();
    }

    auto device = Application::Get().GetDevice();

    auto cubemapResource = cubemapTexture.GetD3D12Resource();
    if (!cubemapResource) return;

    CD3DX12_RESOURCE_DESC cubemapDesc(cubemapResource->GetDesc());

    auto stagingResource = cubemapResource;
    Texture stagingTexture(stagingResource);
    // 如果传入的资源不允许 UAV 访问，则创建用于生成立方体贴图的暂存资源
    if ((cubemapDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
    {
        auto stagingDesc = cubemapDesc;
        stagingDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &stagingDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&stagingResource)

        ));

        ResourceStateTracker::AddGlobalResourceState(stagingResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);

        stagingTexture.SetD3D12Resource(stagingResource);
        stagingTexture.CreateViews();
        stagingTexture.SetName(L"Pano to Cubemap Staging Texture");

        CopyResource(stagingTexture, cubemapTexture );
    }

    TransitionBarrier(stagingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_d3d12CommandList->SetPipelineState(m_PanoToCubemapPSO->GetPipelineState().Get());
    SetComputeRootSignature(m_PanoToCubemapPSO->GetRootSignature());

    PanoToCubemapCB panoToCubemapCB;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = cubemapDesc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = 6;

    for (uint32_t mipSlice = 0; mipSlice < cubemapDesc.MipLevels; )
    {
        // 每个pass生成的最大 mips 数为 5。
        uint32_t numMips = std::min<uint32_t>(5, cubemapDesc.MipLevels - mipSlice);

        panoToCubemapCB.FirstMip = mipSlice;
        panoToCubemapCB.CubemapSize = std::max<uint32_t>( static_cast<uint32_t>( cubemapDesc.Width ), cubemapDesc.Height) >> mipSlice;
        panoToCubemapCB.NumMips = numMips;

        SetCompute32BitConstants(PanoToCubemapRS::PanoToCubemapCB, panoToCubemapCB);

        SetShaderResourceView(PanoToCubemapRS::SrcTexture, 0, panoTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        for ( uint32_t mip = 0; mip < numMips; ++mip )
        {
            uavDesc.Texture2DArray.MipSlice = mipSlice + mip;
            SetUnorderedAccessView(PanoToCubemapRS::DstMips, mip, stagingTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, 0, &uavDesc);
        }

        if (numMips < 5)
        {
            // 填充未使用的 mips。这让 DX12 运行时保持良好状态
            m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors(PanoToCubemapRS::DstMips, panoToCubemapCB.NumMips, 5 - numMips, m_PanoToCubemapPSO->GetDefaultUAV());
        }

        Dispatch(Math::DivideByMultiple(panoToCubemapCB.CubemapSize, 16), Math::DivideByMultiple(panoToCubemapCB.CubemapSize, 16), 6 );

        mipSlice += numMips;
    }

    if (stagingResource != cubemapResource)
    {
        CopyResource(cubemapTexture, stagingTexture);
    }
}

void CommandList::ClearTexture( const Texture& texture, const float clearColor[4])
{
    TransitionBarrier(texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_d3d12CommandList->ClearRenderTargetView(texture.GetRenderTargetView(), clearColor, 0, nullptr );

    TrackResource(texture);
}

void CommandList::ClearDepthStencilTexture( const Texture& texture, D3D12_CLEAR_FLAGS clearFlags, float depth, uint8_t stencil)
{
    TransitionBarrier(texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    m_d3d12CommandList->ClearDepthStencilView(texture.GetDepthStencilView(), clearFlags, depth, stencil, 0, nullptr);

    TrackResource(texture);
}

void CommandList::CopyTextureSubresource( Texture& texture, uint32_t firstSubresource, uint32_t numSubresources, D3D12_SUBRESOURCE_DATA* subresourceData )
{
    auto device = Application::Get().GetDevice();
    auto destinationResource = texture.GetD3D12Resource();
    if ( destinationResource )
    {
        // 资源必须处于 copy-destination 状态。
        TransitionBarrier( texture, D3D12_RESOURCE_STATE_COPY_DEST );
        FlushResourceBarriers();

        const UINT64 requiredSize = GetRequiredIntermediateSize( destinationResource.Get(), firstSubresource, numSubresources );

        // 创建用于上传子资源的临时（中间）资源
        ComPtr<ID3D12Resource> intermediateResource;
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_UPLOAD );
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer( requiredSize );
        ThrowIfFailed( device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &intermediateResource )
        ) );

        UpdateSubresources( m_d3d12CommandList.Get(), destinationResource.Get(), intermediateResource.Get(), 0, firstSubresource, numSubresources, subresourceData );

        TrackObject(intermediateResource);
        TrackObject(destinationResource);
    }
}

#pragma endregion

#pragma region Settings

void CommandList::SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY primitiveTopology )
{
    m_d3d12CommandList->IASetPrimitiveTopology( primitiveTopology );
}

void CommandList::SetGraphicsDynamicConstantBuffer( uint32_t rootParameterIndex, size_t sizeInBytes, const void* bufferData )
{
    // 常量缓冲区必须以 256 字节对齐
    auto heapAllococation = m_UploadBuffer->Allocate( sizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT );
    memcpy( heapAllococation.CPU, bufferData, sizeInBytes );

    m_d3d12CommandList->SetGraphicsRootConstantBufferView( rootParameterIndex, heapAllococation.GPU );
}

void CommandList::SetGraphics32BitConstants( uint32_t rootParameterIndex, uint32_t numConstants, const void* constants )
{
    m_d3d12CommandList->SetGraphicsRoot32BitConstants( rootParameterIndex, numConstants, constants, 0 );
}

void CommandList::SetCompute32BitConstants( uint32_t rootParameterIndex, uint32_t numConstants, const void* constants )
{
    m_d3d12CommandList->SetComputeRoot32BitConstants( rootParameterIndex, numConstants, constants, 0 );
}

void CommandList::SetVertexBuffer( uint32_t slot, const VertexBuffer& vertexBuffer )
{
    TransitionBarrier( vertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );

    auto vertexBufferView = vertexBuffer.GetVertexBufferView();

    m_d3d12CommandList->IASetVertexBuffers( slot, 1, &vertexBufferView );

    TrackResource(vertexBuffer);
}

void CommandList::SetDynamicVertexBuffer( uint32_t slot, size_t numVertices, size_t vertexSize, const void* vertexBufferData )
{
    size_t bufferSize = numVertices * vertexSize;

    auto heapAllocation = m_UploadBuffer->Allocate( bufferSize, vertexSize );
    memcpy( heapAllocation.CPU, vertexBufferData, bufferSize );

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    vertexBufferView.BufferLocation = heapAllocation.GPU;
    vertexBufferView.SizeInBytes = static_cast<UINT>( bufferSize );
    vertexBufferView.StrideInBytes = static_cast<UINT>( vertexSize );

    m_d3d12CommandList->IASetVertexBuffers( slot, 1, &vertexBufferView );
}

void CommandList::SetIndexBuffer( const IndexBuffer& indexBuffer )
{
    TransitionBarrier( indexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER );

    auto indexBufferView = indexBuffer.GetIndexBufferView();

    m_d3d12CommandList->IASetIndexBuffer( &indexBufferView );

    TrackResource(indexBuffer);
}

void CommandList::SetDynamicIndexBuffer( size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData )
{
    size_t indexSizeInBytes = indexFormat == DXGI_FORMAT_R16_UINT ? 2 : 4;
    size_t bufferSize = numIndicies * indexSizeInBytes;

    auto heapAllocation = m_UploadBuffer->Allocate( bufferSize, indexSizeInBytes );
    memcpy( heapAllocation.CPU, indexBufferData, bufferSize );

    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    indexBufferView.BufferLocation = heapAllocation.GPU;
    indexBufferView.SizeInBytes = static_cast<UINT>( bufferSize );
    indexBufferView.Format = indexFormat;

    m_d3d12CommandList->IASetIndexBuffer( &indexBufferView );
}

void CommandList::SetGraphicsDynamicStructuredBuffer( uint32_t slot, size_t numElements, size_t elementSize, const void* bufferData )
{
    size_t bufferSize = numElements * elementSize;

    auto heapAllocation = m_UploadBuffer->Allocate( bufferSize, elementSize );

    memcpy( heapAllocation.CPU, bufferData, bufferSize );

    m_d3d12CommandList->SetGraphicsRootShaderResourceView( slot, heapAllocation.GPU );
}

void CommandList::SetViewport(const D3D12_VIEWPORT& viewport)
{
    SetViewports( {viewport} );
}

void CommandList::SetViewports(const std::vector<D3D12_VIEWPORT>& viewports)
{
    assert(viewports.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    m_d3d12CommandList->RSSetViewports( static_cast<UINT>( viewports.size() ), viewports.data() );
}

void CommandList::SetScissorRect(const D3D12_RECT& scissorRect)
{
    SetScissorRects({scissorRect});
}

void CommandList::SetScissorRects(const std::vector<D3D12_RECT>& scissorRects)
{
    assert( scissorRects.size() < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    m_d3d12CommandList->RSSetScissorRects( static_cast<UINT>( scissorRects.size() ), 
        scissorRects.data());
}

void CommandList::SetPipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState)
{
    m_d3d12CommandList->SetPipelineState(pipelineState.Get());

    TrackObject(pipelineState);
}

void CommandList::SetGraphicsRootSignature( const RootSignature& rootSignature )
{
    auto d3d12RootSignature = rootSignature.GetRootSignature().Get();
    if ( m_RootSignature != d3d12RootSignature )
    {
        m_RootSignature = d3d12RootSignature;

        for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
        {
            m_DynamicDescriptorHeap[i]->ParseRootSignature( rootSignature );
        }

        m_d3d12CommandList->SetGraphicsRootSignature(m_RootSignature);

        TrackObject(m_RootSignature);
    }
}

void CommandList::SetComputeRootSignature( const RootSignature& rootSignature )
{
    auto d3d12RootSignature = rootSignature.GetRootSignature().Get();
    if ( m_RootSignature != d3d12RootSignature )
    {
        m_RootSignature = d3d12RootSignature;

        for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
        {
            m_DynamicDescriptorHeap[i]->ParseRootSignature( rootSignature );
        }

        m_d3d12CommandList->SetComputeRootSignature(m_RootSignature);

        TrackObject(m_RootSignature);
    }
}

void CommandList::SetShaderResourceView( uint32_t rootParameterIndex,
                                         uint32_t descriptorOffset,
                                         const Resource& resource,
                                         D3D12_RESOURCE_STATES stateAfter,
                                         UINT firstSubresource,
                                         UINT numSubresources,
                                         const D3D12_SHADER_RESOURCE_VIEW_DESC* srv)
{
    if (numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        for (uint32_t i = 0; i < numSubresources; ++i)
        {
            TransitionBarrier(resource, stateAfter, firstSubresource + i);
        }
    }
    else
    {
        TransitionBarrier(resource, stateAfter);
    }

    m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors( rootParameterIndex, descriptorOffset, 1, resource.GetShaderResourceView( srv ) );

    TrackResource(resource);
}

void CommandList::SetUnorderedAccessView( uint32_t rootParameterIndex, 
                                          uint32_t descrptorOffset,
                                          const Resource& resource,
                                          D3D12_RESOURCE_STATES stateAfter,
                                          UINT firstSubresource,
                                          UINT numSubresources,
                                          const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav)
{
    if ( numSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES )
    {
        for ( uint32_t i = 0; i < numSubresources; ++i )
        {
            TransitionBarrier( resource, stateAfter, firstSubresource + i );
        }
    }
    else
    {
        TransitionBarrier( resource, stateAfter );
    }

    m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->StageDescriptors( rootParameterIndex, descrptorOffset, 1, resource.GetUnorderedAccessView( uav ) );

    TrackResource(resource);
}

void CommandList::SetRenderTarget(const RenderTarget& renderTarget )
{
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargetDescriptors;
    renderTargetDescriptors.reserve(AttachmentPoint::NumAttachmentPoints);

    const auto& textures = renderTarget.GetTextures();
    
    // 绑定颜色目标（最多可以将 8 个渲染目标绑定到渲染管线）
    for ( int i = 0; i < 8; ++i )
    {
        auto& texture = textures[i];

        if ( texture.IsValid() )
        {
            TransitionBarrier( texture, D3D12_RESOURCE_STATE_RENDER_TARGET );
            renderTargetDescriptors.push_back( texture.GetRenderTargetView() );

            TrackResource( texture );
        }
    }

    const auto& depthTexture = renderTarget.GetTexture( AttachmentPoint::DepthStencil );

    CD3DX12_CPU_DESCRIPTOR_HANDLE depthStencilDescriptor(D3D12_DEFAULT);
    if (depthTexture.GetD3D12Resource())
    {
        TransitionBarrier(depthTexture, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        depthStencilDescriptor = depthTexture.GetDepthStencilView();

        TrackResource(depthTexture);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = depthStencilDescriptor.ptr != 0 ? &depthStencilDescriptor : nullptr;

    m_d3d12CommandList->OMSetRenderTargets( static_cast<UINT>( renderTargetDescriptors.size() ),
        renderTargetDescriptors.data(), FALSE, pDSV );
}

#pragma endregion 

void CommandList::Draw( uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance )
{
    FlushResourceBarriers();

    for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
    {
        m_DynamicDescriptorHeap[i]->CommitStagedDescriptorsForDraw( *this );
    }

    m_d3d12CommandList->DrawInstanced( vertexCount, instanceCount, startVertex, startInstance );
}

void CommandList::DrawIndexed( uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex, uint32_t startInstance )
{
    FlushResourceBarriers();

    for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
    {
        m_DynamicDescriptorHeap[i]->CommitStagedDescriptorsForDraw( *this );
    }

    m_d3d12CommandList->DrawIndexedInstanced( indexCount, instanceCount, startIndex, baseVertex, startInstance );
}

void CommandList::Dispatch( uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ )
{
    FlushResourceBarriers();

    for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
    {
        m_DynamicDescriptorHeap[i]->CommitStagedDescriptorsForDispatch( *this );
    }

    m_d3d12CommandList->Dispatch( numGroupsX, numGroupsY, numGroupsZ );
}

bool CommandList::Close( CommandList& pendingCommandList )
{
    // 刷新任何剩余的屏障。
    FlushResourceBarriers();

    m_d3d12CommandList->Close();

    // 刷新挂起的资源障碍
    uint32_t numPendingBarriers = m_ResourceStateTracker->FlushPendingResourceBarriers( pendingCommandList );
    // 将最终资源状态提交到全局状态
    m_ResourceStateTracker->CommitFinalResourceStates();

    return numPendingBarriers > 0;
}

void CommandList::Close()
{
    FlushResourceBarriers();
    m_d3d12CommandList->Close();
}

void CommandList::Reset()
{
    ThrowIfFailed( m_d3d12CommandAllocator->Reset() );
    ThrowIfFailed( m_d3d12CommandList->Reset( m_d3d12CommandAllocator.Get(), nullptr ) );

    m_ResourceStateTracker->Reset();
    m_UploadBuffer->Reset();

    ReleaseTrackedObjects();

    for ( int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
    {
        m_DynamicDescriptorHeap[i]->Reset();
        m_DescriptorHeaps[i] = nullptr;
    }

    m_RootSignature = nullptr;
    m_ComputeCommandList = nullptr;
}

void CommandList::TrackObject(Microsoft::WRL::ComPtr<ID3D12Object> object)
{
    m_TrackedObjects.push_back(object);
}

void CommandList::TrackResource(const Resource& res)
{
    TrackObject(res.GetD3D12Resource());
}

void CommandList::ReleaseTrackedObjects()
{
    m_TrackedObjects.clear();
}



void CommandList::SetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap )
{
    if ( m_DescriptorHeaps[heapType] != heap )
    {
        m_DescriptorHeaps[heapType] = heap;
        BindDescriptorHeaps();
    }
}

void CommandList::BindDescriptorHeaps()
{
    UINT numDescriptorHeaps = 0;
    ID3D12DescriptorHeap* descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

    for ( uint32_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i )
    {
        ID3D12DescriptorHeap* descriptorHeap = m_DescriptorHeaps[i];
        if ( descriptorHeap )
        {
            descriptorHeaps[numDescriptorHeaps++] = descriptorHeap;
        }
    }

    m_d3d12CommandList->SetDescriptorHeaps( numDescriptorHeaps, descriptorHeaps );
}