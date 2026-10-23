#include "render/renderer.hpp"
#include <cmath>
#include <cstring>

namespace aura {
using namespace DirectX;
void Renderer::createEffects() {
    constexpr float Pi=3.14159265359f;
    std::vector<XMFLOAT2> outline;
    XMFLOAT2 corners[10];
    for(int i=0;i<10;++i) {const float a=Pi*.5f-i*Pi/5,r=i%2?.49f:1.f; corners[i]={std::cos(a)*r,std::sin(a)*r};}
    auto mix=[](XMFLOAT2 a,XMFLOAT2 b,float t){return XMFLOAT2{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};};
    for(int i=0;i<10;++i) {
        const auto p=corners[i],a=mix(p,corners[(i+9)%10],.23f),b=mix(p,corners[(i+1)%10],.23f);
        for(int j=0;j<6;++j) {const float t=j/6.f; outline.push_back(mix(mix(a,p,t),mix(p,b,t),t));}
    }
    std::vector<Vertex> vertices;
    std::vector<unsigned short> indices;
    auto triangle=[&](Vertex a,Vertex b,Vertex c) {
        const auto i=static_cast<unsigned short>(vertices.size()); vertices.insert(vertices.end(),{a,b,c}); indices.insert(indices.end(),{i,static_cast<unsigned short>(i+1),static_cast<unsigned short>(i+2)});
    };
    for(std::size_t i=0;i<outline.size();++i) {
        const auto a=outline[i],b=outline[(i+1)%outline.size()];
        triangle({{0,0,-.07f},{0,0,-1}},{{a.x,a.y,-.07f},{0,0,-1}},{{b.x,b.y,-.07f},{0,0,-1}});
        triangle({{0,0,.07f},{0,0,1}},{{b.x,b.y,.07f},{0,0,1}},{{a.x,a.y,.07f},{0,0,1}});
        const float nx=a.y-b.y,ny=b.x-a.x,length=std::sqrt(nx*nx+ny*ny); const XMFLOAT3 n{nx/length,ny/length,0};
        triangle({{a.x,a.y,-.07f},n},{{a.x,a.y,.07f},n},{{b.x,b.y,.07f},n});
        triangle({{a.x,a.y,-.07f},n},{{b.x,b.y,.07f},n},{{b.x,b.y,-.07f},n});
    }
    auto buffer=[&](UINT bytes,UINT bind,const void* memory,ID3D11Buffer** out) {
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth=bytes; desc.BindFlags=bind; desc.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA initial{memory,0,0}; checked(device_->CreateBuffer(&desc,&initial,out),"Effect geometry");
    };
    buffer(static_cast<UINT>(vertices.size()*sizeof(Vertex)),D3D11_BIND_VERTEX_BUFFER,vertices.data(),&starVertices_);
    buffer(static_cast<UINT>(indices.size()*sizeof(unsigned short)),D3D11_BIND_INDEX_BUFFER,indices.data(),&starIndices_);
    starIndexCount_=static_cast<UINT>(indices.size());
    const Vertex quad[]={{{-.5f,-.5f,0},{0,0,-1}},{{-.5f,.5f,0},{0,0,-1}},{{.5f,.5f,0},{0,0,-1}},{{.5f,-.5f,0},{0,0,-1}}};
    const unsigned short quadIndices[]={0,1,2,0,2,3};
    buffer(sizeof(quad),D3D11_BIND_VERTEX_BUFFER,quad,&holeVertices_);
    buffer(sizeof(quadIndices),D3D11_BIND_INDEX_BUFFER,quadIndices,&holeIndices_);
    D3D11_BLEND_DESC blend{}; auto& rt=blend.RenderTarget[0]; rt.BlendEnable=TRUE; rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp=D3D11_BLEND_OP_ADD; rt.SrcBlendAlpha=D3D11_BLEND_ONE;
    rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha=D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    checked(device_->CreateBlendState(&blend,&effectBlend_),"Effect blending");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=TRUE; depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO; depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    checked(device_->CreateDepthStencilState(&depth,&effectDepth_),"Effect depth");
    starInstances_.reserve(256);
    createPixelation();
    createEquinox();
    createBreakthrough();
    createNanally();
    createFrutiger();
}
void Renderer::particleStar(float x,float y,float z,float size,Color color,float rotation,const Frame& f) {
    if(starInstances_.size()>=512 || size<=.001f) return;
    Instance star;
    XMStoreFloat4x4(&star.world,XMMatrixScaling(size,size,size)*XMMatrixRotationZ(rotation)*XMMatrixRotationX(f.cameraPitch)*
        XMMatrixRotationY(-f.cameraYaw)*XMMatrixTranslation(x,y,z));
    star.color={color.r,color.g,color.b,1.1f}; starInstances_.push_back(star);
}
void Renderer::drawEffects(const Frame&) {
    if(starInstances_.empty() && !holeVisible_ && pixelationInstances_.empty() && equinoxOrbs_.empty() && equinoxTitles_.empty() && nanallyInstances_.empty() && illusionaryInk_.empty() && frutigerInstances_.empty()) return;
    context_->IASetInputLayout(layout_.Get()); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(),nullptr,0); context_->PSSetShader(ps_.Get(),nullptr,0);
    ID3D11Buffer* cb=constants_.Get(); context_->VSSetConstantBuffers(0,1,&cb); context_->PSSetConstantBuffers(0,1,&cb);
    context_->RSSetState(raster_.Get()); context_->OMSetDepthStencilState(depthState_.Get(),0); context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
    auto draw=[&](ID3D11Buffer* vertices,ID3D11Buffer* indices,UINT indexCount,const Instance* instances,std::size_t count) {
        D3D11_MAPPED_SUBRESOURCE mapped{}; checked(context_->Map(instanceBuffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Effect instances");
        std::memcpy(mapped.pData,instances,count*sizeof(Instance)); context_->Unmap(instanceBuffer_.Get(),0);
        ID3D11Buffer* streams[]={vertices,instanceBuffer_.Get()}; const UINT strides[]={sizeof(Vertex),sizeof(Instance)},offsets[]={0,0};
        context_->IASetVertexBuffers(0,2,streams,strides,offsets); context_->IASetIndexBuffer(indices,DXGI_FORMAT_R16_UINT,0);
        context_->DrawIndexedInstanced(indexCount,static_cast<UINT>(count),0,0,0);
    };
    if(!starInstances_.empty()) draw(starVertices_.Get(),starIndices_.Get(),starIndexCount_,starInstances_.data(),starInstances_.size());
    if(holeVisible_) {
        context_->PSSetShader(holePs_.Get(),nullptr,0); context_->OMSetBlendState(effectBlend_.Get(),nullptr,0xffffffff);
        context_->OMSetDepthStencilState(effectDepth_.Get(),0);
        draw(holeVertices_.Get(),holeIndices_.Get(),6,&blackHole_,1);
    }
    if(!equinoxOrbs_.empty()) {
        ID3D11ShaderResourceView* letters=equinoxLettering_.Get(); context_->PSSetShaderResources(0,1,&letters);
        ID3D11SamplerState* sampler=bloomSampler_.Get(); context_->PSSetSamplers(0,1,&sampler);
        context_->PSSetShader(equinoxOrbPs_.Get(),nullptr,0); context_->OMSetBlendState(effectBlend_.Get(),nullptr,0xffffffff);
        context_->OMSetDepthStencilState(effectDepth_.Get(),0);
        draw(holeVertices_.Get(),holeIndices_.Get(),6,equinoxOrbs_.data(),equinoxOrbs_.size());
    }
    if(!nanallyInstances_.empty()) {
        context_->PSSetShader(nanallyPs_.Get(),nullptr,0);
        context_->OMSetBlendState(effectBlend_.Get(),nullptr,0xffffffff);
        context_->OMSetDepthStencilState(effectDepth_.Get(),0);
        draw(holeVertices_.Get(),holeIndices_.Get(),6,nanallyInstances_.data(),nanallyInstances_.size());
    }
    if(!frutigerInstances_.empty()) {
        context_->PSSetShader(frutigerPs_.Get(),nullptr,0);
        context_->OMSetBlendState(effectBlend_.Get(),nullptr,0xffffffff);
        context_->OMSetDepthStencilState(effectDepth_.Get(),0);
        draw(holeVertices_.Get(),holeIndices_.Get(),6,frutigerInstances_.data(),frutigerInstances_.size());
    }
    if(!illusionaryInk_.empty()) {
        // 加算で光を重ねる前に、黒い立体リボンの輪郭を描きます。
        // 深度も保存し、帯の後ろの光線が手前へ透けないようにします。
        context_->PSSetShader(ps_.Get(),nullptr,0);
        context_->OMSetDepthStencilState(depthState_.Get(),0);
        context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
        draw(holeVertices_.Get(),holeIndices_.Get(),6,illusionaryInk_.data(),illusionaryInk_.size());
    }
    if(!pixelationInstances_.empty()) {
        createPixelationTargets();
        const float black[]={0,0,0,0}; context_->ClearRenderTargetView(bloomTargets_[0].Get(),black);
        ID3D11RenderTargetView* emission=bloomTargets_[0].Get(); context_->OMSetRenderTargets(1,&emission,depth_.Get());
        context_->PSSetShader(nanallyVisible_?nanallyGlowPs_.Get():breakthroughVisible_?breakthroughPs_.Get():pixelationPs_.Get(),nullptr,0);
        ID3D11ShaderResourceView* letters=breakthroughVisible_?breakthroughLettering_.Get():equinoxLettering_.Get(); context_->PSSetShaderResources(0,1,&letters);
        ID3D11SamplerState* sampler=bloomSampler_.Get(); context_->PSSetSamplers(0,1,&sampler);
        context_->OMSetBlendState(additiveBlend_.Get(),nullptr,0xffffffff);
        context_->OMSetDepthStencilState(effectDepth_.Get(),0);
        draw(holeVertices_.Get(),holeIndices_.Get(),6,pixelationInstances_.data(),pixelationInstances_.size());
        compositePixelation();
    }
    // Composite alpha lettering after bloom so a black Equinox title stays black.
    if(!equinoxTitles_.empty()) {
        ID3D11RenderTargetView* target=target_.Get(); context_->OMSetRenderTargets(1,&target,depth_.Get());
        context_->IASetInputLayout(layout_.Get()); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(),nullptr,0); context_->PSSetShader(equinoxOrbPs_.Get(),nullptr,0);
        ID3D11Buffer* camera=constants_.Get(); context_->VSSetConstantBuffers(0,1,&camera); context_->PSSetConstantBuffers(0,1,&camera);
        ID3D11ShaderResourceView* letters=equinoxLettering_.Get(); context_->PSSetShaderResources(0,1,&letters);
        ID3D11SamplerState* sampler=bloomSampler_.Get(); context_->PSSetSamplers(0,1,&sampler);
        context_->OMSetDepthStencilState(effectDepth_.Get(),0); context_->OMSetBlendState(effectBlend_.Get(),nullptr,0xffffffff);
        draw(holeVertices_.Get(),holeIndices_.Get(),6,equinoxTitles_.data(),equinoxTitles_.size());
    }
}
}
