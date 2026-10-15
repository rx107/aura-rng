#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace aura {
// Imported neutral geometry is skinned once on the loader thread. The render
// loop retains compact GPU meshes, not the model's hundreds of morph targets.
class VrmAvatar {
public:
    static std::unique_ptr<VrmAvatar> load(ID3D11Device* device,const std::filesystem::path& path);
    void draw(ID3D11DeviceContext* context,DirectX::FXMMATRIX vp,DirectX::CXMMATRIX model);
    std::size_t vertexCount() const { return vertexCount_; }
    std::size_t drawCount() const { return meshes_.size(); }
    std::string title;
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Mesh {
        Ptr<ID3D11Buffer> vertices,indices;
        Ptr<ID3D11ShaderResourceView> texture;
        UINT count{};
        DirectX::XMFLOAT4 color{1,1,1,1};
        float cutoff{-1}; bool blend{};
    };
    std::vector<Mesh> meshes_;
    std::size_t vertexCount_{};
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> ps_;
    Ptr<ID3D11InputLayout> layout_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11BlendState> blend_;
    Ptr<ID3D11DepthStencilState> depth_,transparentDepth_;
};
}
