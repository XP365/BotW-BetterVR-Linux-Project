#pragma once

class RND_D3D12 {
    friend class RND_Renderer;

public:
    RND_D3D12();
    ~RND_D3D12();

    ID3D12Device* GetDevice() { return m_device.Get(); };

    ID3D12CommandQueue* GetCommandQueue() { return m_queue.Get(); };

    void StartFrame() {
        checkHResult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator)), "Failed to created D3D12_CommandContext's allocator!");
    }
    void EndFrame() {
        // Wait for GPU to finish queue
        checkHResult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "Failed to create fence for end-of-frame waiting!");
        checkHResult(m_queue->Signal(m_fence.Get(), 1), "Failed to signal fence for end-of-frame waiting!");

        HANDLE waitEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        checkAssert(waitEvent != NULL, "Failed to create upload event!");
        checkHResult(m_fence->SetEventOnCompletion(1, waitEvent), "Failed to set event completion for end-of-frame waiting!");
        WaitForSingleObject(waitEvent, INFINITE);

        // Reset allocator after queue is finished
        m_allocator->Reset();
    };

    ID3D12CommandAllocator* GetFrameAllocator() { return m_allocator.Get(); };

    // todo: extract most to a base pipeline class if other pipelines are needed
    class PresentPipeline {
        friend class Texture;

    public:
        PresentPipeline();
        ~PresentPipeline();

        void BindAttachment(uint32_t attachmentIdx, ID3D12Resource* srcTexture, DXGI_FORMAT overwriteFormat = DXGI_FORMAT_UNKNOWN);
        void BindTarget(uint32_t targetIdx, ID3D12Resource* dstTexture, DXGI_FORMAT overwriteFormat = DXGI_FORMAT_UNKNOWN);
        void Render(ID3D12GraphicsCommandList* commandList, ID3D12Resource* swapchain);

    private:
        void RecreatePipeline(uint32_t targetIdx, DXGI_FORMAT targetFormat);

        ComPtr<ID3DBlob> m_vertexShader;
        ComPtr<ID3DBlob> m_pixelShader;

        ComPtr<ID3D12Resource> m_screenIndicesBuffer;
        D3D12_INDEX_BUFFER_VIEW m_screenIndicesView = {};

        ComPtr<ID3D12RootSignature> m_signature;
        ComPtr<ID3D12PipelineState> m_pipelineState;

        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 1> m_attachmentHandles = {};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 1> m_targetHandles = {};
        ComPtr<ID3D12DescriptorHeap> m_attachmentHeap;
        ComPtr<ID3D12DescriptorHeap> m_targetHeap;
        std::array<DXGI_FORMAT, 1> m_targetFormats = { DXGI_FORMAT_UNKNOWN };
    };

    template <bool blockTillExecuted>
    class CommandContext {
    public:
        template <typename F>
        CommandContext(ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12Queue, ID3D12CommandAllocator* d3d12Allocator, F&& recordCallback): m_device(d3d12Device), m_queue(d3d12Queue) {
            // Create commands to upload buffers
            checkHResult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d3d12Allocator, nullptr, IID_PPV_ARGS(&this->m_cmdList)), "Failed to create D3D12_CommandContext's command list!");

            recordCallback(this->m_cmdList.Get());
        }

        ~CommandContext() {
            // Close command list and then execute command list in queue
            checkHResult(this->m_cmdList->Close(), "Failed to close D3D12_CommandContext's queue");
            ID3D12CommandList* collectedList[] = { this->m_cmdList.Get() };
            m_queue->ExecuteCommandLists((UINT)std::size(collectedList), collectedList);

            // If enabled, wait until the command list and the fence signal has been executed
            if constexpr (blockTillExecuted) {
                m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&this->m_blockFence));
                m_queue->Signal(this->m_blockFence.Get(), 1);

                HANDLE waitEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
                checkAssert(waitEvent != NULL, "Failed to create upload event!");

                if (this->m_blockFence->GetCompletedValue() < 1) {
                    this->m_blockFence->SetEventOnCompletion(1, waitEvent);
                    WaitForSingleObject(waitEvent, INFINITE);
                }
                CloseHandle(waitEvent);
            }
        }

    private:
        ID3D12Device* m_device;
        ID3D12CommandQueue* m_queue;

        ComPtr<ID3D12GraphicsCommandList> m_cmdList;
        ComPtr<ID3D12Fence> m_blockFence;
    };

private:
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ComPtr<ID3D12Fence> m_fence;

    std::vector<std::unique_ptr<PresentPipeline>> m_pipelines;

    //ComPtr<ID3D12Fence> m_fence;
    //HANDLE m_fenceEvent;
    //uint64_t m_fenceValue;
};