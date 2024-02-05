#pragma once

#include <TextureUsage.h>

#include <d3d12.h>
#include <wrl.h>

#include <map> // for std::map
#include <memory> // for std::unique_ptr
#include <mutex> // for std::mutex
#include <vector> // for std::vector

#include "PreFilterPSO.h"

class Buffer;
class ByteAddressBuffer;
class ConstantBuffer;
class DynamicDescriptorHeap;
class GenerateMipsPSO;
class IndexBuffer;
class PanoToCubemapPSO;
class IrradianceConvolutionPSO;
class RenderTarget;
class Resource;
class ResourceStateTracker;
class StructuredBuffer;
class RootSignature;
class Texture;
class UploadBuffer;
class VertexBuffer;

/**
 *  @brief CommandList 类封装 ID3D12GraphicsCommandList2 接口
 */
class CommandList
{
public:
    CommandList(D3D12_COMMAND_LIST_TYPE type);
    virtual ~CommandList();

    /**
     * 获取命令列表的类型
     */
    D3D12_COMMAND_LIST_TYPE GetCommandListType() const
    {
        return m_d3d12CommandListType;
    }

    /**
     * 获取 ID3D12GraphicsCommandList2 
     */
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> GetGraphicsCommandList() const
    {
        return m_d3d12CommandList;
    }
    
    /**
    * 将资源转换为特定状态。
    *
    * @param resource 要转换的资源。
    * @param stateAfter 要将资源转换到的状态。之前的状态由资源状态跟踪器解析.
    * @param subresource The subresource to transition. By default, this is D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES which indicates that all subresources are transitioned to the same state.
    * @param flushBarriers Force flush any barriers. Resource barriers need to be flushed before a command (draw, dispatch, or copy) that expects the resource to be in a particular state can run.
    */
    void TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter,
                           UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool flushBarriers = false);
#pragma region Barriers
    /**
    * 添加 UAV 屏障，以确保在从资源读取之前已完成任何写入
    * @param resource 要为其添加UAV屏障的资源。
    * @param flushBarriers 强行刷新屏障。需要先刷新资源屏障，然后才能运行预期资源处于特定状态的命令（绘制、调度或复制）
    */
    void UAVBarrier(const Resource& resource, bool flushBarriers = false);
 
    /**
    * 添加别名屏障，以指示在堆中占用相同空间的两个不同资源的用法之间的转换。
    * @param beforeResource  当前占用堆的资源
    * @param afterResource  将占用堆中空间的资源
    */
    void AliasingBarrier(const Resource& beforeResource, const Resource& afterResource, bool flushBarriers = false);

    /**
      * 刷新已推送到命令列表的任何屏障
      */
    void FlushResourceBarriers();
#pragma endregion

#pragma region Resources
    /**
      * 复制资源
      */
    void CopyResource(Resource& dstRes, const Resource& srcRes);
    
    /**
     * 将多重采样资源解析为非多重采样资源
     */
    void ResolveSubresource( Resource& dstRes, const Resource& srcRes, uint32_t dstSubresource = 0, uint32_t srcSubresource = 0 );

    /**
     * 将内容复制到 GPU 内存中的顶点缓冲区。
     */
    void CopyVertexBuffer( VertexBuffer& vertexBuffer, size_t numVertices, size_t vertexStride, const void* vertexBufferData );
    template<typename T>
    void CopyVertexBuffer( VertexBuffer& vertexBuffer, const std::vector<T>& vertexBufferData )
    {
        CopyVertexBuffer( vertexBuffer, vertexBufferData.size(), sizeof( T ), vertexBufferData.data() );
    }

    /**
     * 将内容复制到 GPU 内存中的索引缓冲区。
     */
    void CopyIndexBuffer( IndexBuffer& indexBuffer, size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData );
    template<typename T>
    void CopyIndexBuffer( IndexBuffer& indexBuffer, const std::vector<T>& indexBufferData )
    {
        assert( sizeof( T ) == 2 || sizeof( T ) == 4 );

        DXGI_FORMAT indexFormat = ( sizeof( T ) == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        CopyIndexBuffer( indexBuffer, indexBufferData.size(), indexFormat, indexBufferData.data() );
    }
    
    /**
      * 将内容复制到 GPU 内存中的字节地址缓冲区 (byte address buffer)
      */
    void CopyByteAddressBuffer( ByteAddressBuffer& byteAddressBuffer, size_t bufferSize, const void* bufferData );
    template<typename T>
    void CopyByteAddressBuffer( ByteAddressBuffer& byteAddressBuffer, const T& data )
    {
        CopyByteAddressBuffer( byteAddressBuffer, sizeof( T ), &data );
    }

    /**
    * 将内容复制到 GPU 内存中的结构化缓冲区 (structured buffer)
    */
    void CopyStructuredBuffer( StructuredBuffer& structuredBuffer, size_t numElements, size_t elementSize, const void* bufferData );
    template<typename T>
    void CopyStructuredBuffer( StructuredBuffer& structuredBuffer, const std::vector<T>& bufferData )
    {
        CopyStructuredBuffer( structuredBuffer, bufferData.size(), sizeof( T ), bufferData.data() );
    }

    
#pragma endregion 
    
#pragma region Textures
    /**
     * 按文件名加载纹理
     */
    void LoadTextureFromFile( Texture& texture, const std::wstring& fileName, TextureUsage textureUsage = TextureUsage::Albedo );

    /**
     * 清除纹理
     */
    void ClearTexture( const Texture& texture, const float clearColor[4] );

    /**
     * 清除深度/模板纹理
     */
    void ClearDepthStencilTexture( const Texture& texture, D3D12_CLEAR_FLAGS clearFlags, float depth = 1.0f, uint8_t stencil = 0 );

    
    /**
      * 为纹理生成 mips
      * 第一个子资源用于生成 mip 链。
      * Mips 从文件加载的纹理自动生成。
      */
    void GenerateMips( Texture& texture );

    /**
     * 从全景（等距柱状投影）纹理生成立方体贴图纹理。
     */
    void PanoToCubemap(Texture& cubemap, const Texture& pano);

    /**
     * 从 cubemap 纹理生成 irradiance卷积 纹理
     */
    void CubemapToIrradianceConvolution(Texture& irradianceTexture, const Texture& cubemap);

    /**
      * 从 cubemap 纹理生成 irradiance卷积 纹理
      */
    void CubemapToPrefilter(Texture& prefilterTexture, const Texture& cubemap);

    
    /**
      * 将子资源数据复制到纹理
      */
    void CopyTextureSubresource( Texture& texture, uint32_t firstSubresource, uint32_t numSubresources, D3D12_SUBRESOURCE_DATA* subresourceData );
#pragma endregion
    
#pragma region Settings
    /**
      * 设置渲染管线的当前基元拓扑
      */
    void SetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY primitiveTopology );
    
    /**
    * 将动态常量缓冲区数据设置为根签名中的内联描述符
    */
    void SetGraphicsDynamicConstantBuffer( uint32_t rootParameterIndex, size_t sizeInBytes, const void* bufferData );
    template<typename T>
    void SetGraphicsDynamicConstantBuffer( uint32_t rootParameterIndex, const T& data )
    {
        SetGraphicsDynamicConstantBuffer( rootParameterIndex, sizeof( T ), &data );
    }
    
    /**
      * 在图形管线上设置一组 32 位常量。
      */
    void SetGraphics32BitConstants( uint32_t rootParameterIndex, uint32_t numConstants, const void* constants );
    template<typename T>
    void SetGraphics32BitConstants( uint32_t rootParameterIndex, const T& constants )
    {
        static_assert( sizeof( T ) % sizeof( uint32_t ) == 0, "Size of type must be a multiple of 4 bytes" );
        SetGraphics32BitConstants( rootParameterIndex, sizeof( T ) / sizeof( uint32_t ), &constants );
    }

    /**
     * 在计算管线上设置一组 32 位常量。
     */
    void SetCompute32BitConstants( uint32_t rootParameterIndex, uint32_t numConstants, const void* constants );
    template<typename T>
    void SetCompute32BitConstants( uint32_t rootParameterIndex, const T& constants )
    {
        static_assert( sizeof( T ) % sizeof( uint32_t ) == 0, "Size of type must be a multiple of 4 bytes" );
        SetCompute32BitConstants( rootParameterIndex, sizeof( T ) / sizeof( uint32_t ), &constants );
    }

    
    /**
     * 设置渲染管线中的顶点缓冲区
     */
    void SetVertexBuffer( uint32_t slot, const VertexBuffer& vertexBuffer );

    /**
     * 设置渲染管线中的动态顶点缓冲区数据
     */
    void SetDynamicVertexBuffer( uint32_t slot, size_t numVertices, size_t vertexSize, const void* vertexBufferData );
    template<typename T>
    void SetDynamicVertexBuffer( uint32_t slot, const std::vector<T>& vertexBufferData )
    {
        SetDynamicVertexBuffer( slot, vertexBufferData.size(), sizeof( T ), vertexBufferData.data() );
    }
    
    /**
    * 将索引缓冲区绑定到渲染管线。
    */
    void SetIndexBuffer( const IndexBuffer& indexBuffer );

    /**
     * 将动态索引缓冲区数据绑定到渲染管线。
     */
    void SetDynamicIndexBuffer( size_t numIndicies, DXGI_FORMAT indexFormat, const void* indexBufferData );
    template<typename T>
    void SetDynamicIndexBuffer( const std::vector<T>& indexBufferData )
    {
        static_assert( sizeof( T ) == 2 || sizeof( T ) == 4 );

        DXGI_FORMAT indexFormat = ( sizeof( T ) == 2 ) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        SetDynamicIndexBuffer( indexBufferData.size(), indexFormat, indexBufferData.data() );
    }


    /**
     * 设置动态结构化缓冲区内容
     */
    void SetGraphicsDynamicStructuredBuffer( uint32_t slot, size_t numElements, size_t elementSize, const void* bufferData );
    template<typename T>
    void SetGraphicsDynamicStructuredBuffer( uint32_t slot, const std::vector<T>& bufferData )
    {
        SetGraphicsDynamicStructuredBuffer( slot, bufferData.size(), sizeof( T ), bufferData.data() );
    }

    /**
     * 设置视口
     */
    void SetViewport( const D3D12_VIEWPORT& viewport );
    void SetViewports( const std::vector<D3D12_VIEWPORT>& viewports );

    /**
    * 设置 scissor rects.
    */
    void SetScissorRect( const D3D12_RECT& scissorRect );
    void SetScissorRects( const std::vector<D3D12_RECT>& scissorRects );

    /**
    * 在命令列表中设置PSO
    */
    void SetPipelineState( Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState );

    /**
     * 在命令列表中设置当前根签名
     */
    void SetGraphicsRootSignature( const RootSignature& rootSignature );
    void SetComputeRootSignature( const RootSignature& rootSignature );

    /**
     * 在渲染管线上设置 SRV
     */
    void SetShaderResourceView(
        uint32_t rootParameterIndex,
        uint32_t descriptorOffset,
        const Resource& resource,
        D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_SHADER_RESOURCE_VIEW_DESC* srv = nullptr
    );

    /**
     * 在渲染管线上设置 UAV
     */
    void SetUnorderedAccessView( 
        uint32_t rootParameterIndex, 
        uint32_t descrptorOffset,
        const Resource& resource, 
        D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        UINT firstSubresource = 0,
        UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav = nullptr
    );

    /**
     * 设置渲染管线的RTV
     */
    void SetRenderTarget( const RenderTarget& renderTarget );
#pragma endregion

    /**
     * 绘制几何图形
     */
    void Draw( uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertex = 0, uint32_t startInstance = 0 );
    void DrawIndexed( uint32_t indexCount, uint32_t instanceCount = 1, uint32_t startIndex = 0, int32_t baseVertex = 0, uint32_t startInstance = 0 );

    /**
     * 调度计算着色器
     */
    void Dispatch(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1);

    
#pragma region Internal
    
    /**
     * 关闭命令列表
     * 由 CommandQueue 调用
     *
     * @param pendingCommandList 用于执行此命令列表待处理资源屏障（如果有）的命令列表。
     * @return 如果存在任何需要处理的待处理资源障碍，则为 true
     */
    bool Close( CommandList& pendingCommandList );
    
    // 只需关闭命令列表即可。这对于待处理的命令列表很有用。
    void Close();

    /**
     * 重置命令列表。这应该只在从 CommandQueue::GetCommandList 返回命令列表之前由 CommandQueue 调用。
     */
    void Reset();

    /**
     * 释放跟踪的对象。如果需要调整交换链的大小，则很有用
     */
    void ReleaseTrackedObjects();

    /**
     * 设置当前绑定的描述符堆。
     * 只能由 DynamicDescriptorHeap 类调用
     */
    void SetDescriptorHeap( D3D12_DESCRIPTOR_HEAP_TYPE heapType, ID3D12DescriptorHeap* heap );

    std::shared_ptr<CommandList> GetGenerateMipsCommandList() const
    {
        return m_ComputeCommandList;
    }
#pragma endregion

protected:

private:
    void TrackObject(Microsoft::WRL::ComPtr<ID3D12Object> object);
    void TrackResource(const Resource& res);

    // 为 UAV 兼容纹理生成 mips。
    void GenerateMips_UAV(Texture& texture);
    // 为 BGR 纹理生成 mips。
    void GenerateMips_BGR(Texture& texture);
    // 为sRGB纹理生成mips。
    void GenerateMips_sRGB(Texture& texture);

    // 将 CPU 缓冲区的内容复制到 GPU 缓冲区（可能替换以前的缓冲区内容）
    void CopyBuffer( Buffer& buffer, size_t numElements, size_t elementSize, const void* bufferData, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE );

    // 将当前描述符堆绑定到命令列表
    void BindDescriptorHeaps();

    D3D12_COMMAND_LIST_TYPE m_d3d12CommandListType;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> m_d3d12CommandList;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_d3d12CommandAllocator;

    // 对于复制队列，可能需要在加载纹理时生成 mips。
    // Mips 不能在复制队列上生成，必须在计算队列或直接队列上生成。
    // 在这种情况下，将在复制队列完成上传第一个子资源后生成并执行 Compute command list。
    std::shared_ptr<CommandList> m_ComputeCommandList;

    // 跟踪当前绑定的根签名，以最大程度地减少根签名更改。
    ID3D12RootSignature* m_RootSignature;

    // 在上传堆中创建的资源。可用于绘制动态几何图形或上传更改每个绘制调用的常量缓冲区数据
    std::unique_ptr<UploadBuffer> m_UploadBuffer;

    // 命令列表使用资源状态跟踪器来跟踪（每个命令列表）资源的当前状态。资源状态跟踪器还跟踪资源的全局状态，以最大程度地减少资源状态转换。
    std::unique_ptr<ResourceStateTracker> m_ResourceStateTracker;

    // DynamicDescriptorHeap 将描述符提交到命令列表之前暂存。DynamicDescriptorHeap 需要在绘制或调度之前提交。   
    std::unique_ptr<DynamicDescriptorHeap> m_DynamicDescriptorHeap[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    // 跟踪当前绑定的描述符堆。仅当描述符堆与当前绑定的描述符堆不同时，才更改这些描述符堆。
    ID3D12DescriptorHeap* m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    // PSO for Mip map 生成
    std::unique_ptr<GenerateMipsPSO> m_GenerateMipsPSO;
    // PSO for 全景图（等距柱状投影）转换为立方体贴图
    std::unique_ptr<PanoToCubemapPSO> m_PanoToCubemapPSO;
    // PSO for cubemap 转换为 irradiance convolution
    std::unique_ptr<IrradianceConvolutionPSO> m_IrradianceCubemapPSO;
    // PSO for PreFilter
    std::unique_ptr<PreFilterPSO> m_PreFilterPSO;

    // 命令队列上“正在进行”且无法删除的命令列表 跟踪对象
    // 为确保在命令列表执行完毕之前不会删除对象，将存储对对象的引用。
    // 重置命令列表时，将释放引用的对象。
    using TrackedObjects = std::vector<Microsoft::WRL::ComPtr<ID3D12Object>>;
    TrackedObjects m_TrackedObjects;

    // 跟踪加载的纹理，以避免多次加载相同的纹理。
    static std::map<std::wstring, ID3D12Resource* > ms_TextureCache;
    static std::mutex ms_TextureCacheMutex;
};
