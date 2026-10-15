#include "render/vrm.hpp"
#include "render/renderer.hpp"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "nlohmann/json.hpp"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <unordered_map>
#include <cctype>

namespace aura {
using namespace DirectX;
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::size_t MaxBones=512;
struct Vertex { XMFLOAT3 p,n; XMFLOAT2 uv; std::array<UINT,4> joints{}; std::array<float,4> weights{1,0,0,0}; };
struct Constants { XMFLOAT4X4 vp,model; XMFLOAT4 color,options; XMFLOAT4X4 bones[MaxBones]; };
constexpr char Shader[]=R"(
cbuffer Draw : register(b0) { row_major float4x4 vp; row_major float4x4 model; float4 color; float4 options; row_major float4x4 bones[512]; };
Texture2D image:register(t0); SamplerState sampleImage:register(s0);
struct V { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0; uint4 joints:BLENDINDICES; float4 weights:BLENDWEIGHT; };
struct O { float4 p:SV_POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0; };
O vsMain(V v) {
 O o; row_major float4x4 skin=bones[v.joints.x]*v.weights.x+bones[v.joints.y]*v.weights.y+bones[v.joints.z]*v.weights.z+bones[v.joints.w]*v.weights.w;
 o.p=mul(mul(mul(float4(v.p,1),skin),model),vp); o.n=mul(mul(float4(v.n,0),skin),model).xyz; o.uv=v.uv; return o;
}
float4 psMain(O i):SV_TARGET {
 float4 c=image.Sample(sampleImage,i.uv)*color;
 if(options.x>=0) clip(c.a-options.x);
 float light=.74+.26*max(0,dot(normalize(i.n),normalize(float3(-.4,.85,-.5))));
 return float4(c.rgb*light, options.y>0?c.a:1);
})";
cgltf_accessor* attr(cgltf_attribute* a,std::size_t count,cgltf_attribute_type type,int index=0) {
    for(std::size_t i=0;i<count;++i) if(a[i].type==type && a[i].index==index) return a[i].data;
    return nullptr;
}
std::vector<float> unpack(cgltf_accessor* a,std::size_t components) {
    if(!a) return {};
    if(a->count>2'000'000 || cgltf_num_components(a->type)!=components) throw std::runtime_error("Unsupported VRM accessor");
    std::vector<float> values(a->count*components);
    if(cgltf_accessor_unpack_floats(a,values.data(),values.size())!=values.size()) throw std::runtime_error("Invalid VRM accessor");
    for(float v:values) if(!std::isfinite(v)) throw std::runtime_error("Nonfinite VRM geometry");
    return values;
}
std::string boneKey(const std::string& name) {
    std::string key;
    for(unsigned char c:name) if(std::isalnum(c)) key+=static_cast<char>(std::tolower(c));
    while(!key.empty() && std::isdigit(static_cast<unsigned char>(key.back()))) key.pop_back();
    return key;
}
ComPtr<ID3D11ShaderResourceView> texture(ID3D11Device* device,IWICImagingFactory* factory,cgltf_image* image) {
    std::vector<BYTE> pixels;
    UINT width=1,height=1;
    if(image) {
        if(!image->buffer_view || image->uri) throw std::runtime_error("VRM textures must be embedded PNG/JPEG");
        const auto* bytes=cgltf_buffer_view_data(image->buffer_view);
        const auto length=image->buffer_view->size;
        if(!bytes || !length || length>64*1024*1024) throw std::runtime_error("Invalid VRM texture size");
        ComPtr<IWICStream> stream; checked(factory->CreateStream(&stream),"VRM image stream");
        checked(stream->InitializeFromMemory(const_cast<BYTE*>(bytes),static_cast<DWORD>(length)),"VRM image bytes");
        ComPtr<IWICBitmapDecoder> decoder; checked(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"VRM image decode");
        ComPtr<IWICBitmapFrameDecode> frame; checked(decoder->GetFrame(0,&frame),"VRM image frame");
        checked(frame->GetSize(&width,&height),"VRM texture dimensions");
        if(!width || !height || width>16384 || height>16384) throw std::runtime_error("VRM texture dimensions exceed limit");
        const float ratio=std::min(1.f,2048.f/std::max(width,height));
        width=std::max(1u,static_cast<UINT>(width*ratio)); height=std::max(1u,static_cast<UINT>(height*ratio));
        ComPtr<IWICBitmapScaler> scaler; checked(factory->CreateBitmapScaler(&scaler),"VRM texture scaler");
        checked(scaler->Initialize(frame.Get(),width,height,WICBitmapInterpolationModeFant),"VRM texture scale");
        ComPtr<IWICFormatConverter> converter; checked(factory->CreateFormatConverter(&converter),"VRM texture converter");
        checked(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"VRM RGBA conversion");
        pixels.resize(static_cast<std::size_t>(width)*height*4);
        checked(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()),"VRM texture pixels");
    } else pixels={255,255,255,255};
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(),width*4,0}; ComPtr<ID3D11Texture2D> tex;
    checked(device->CreateTexture2D(&desc,&initial,&tex),"VRM GPU texture");
    ComPtr<ID3D11ShaderResourceView> view; checked(device->CreateShaderResourceView(tex.Get(),nullptr,&view),"VRM texture view"); return view;
}
}
std::unique_ptr<VrmAvatar> VrmAvatar::load(ID3D11Device* device,const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(size<20 || size>512ull*1024*1024) throw std::runtime_error("VRM file must be a GLB below 512 MiB");
    std::ifstream input(path,std::ios::binary); std::vector<char> bytes(static_cast<std::size_t>(size));
    if(!input.read(bytes.data(),static_cast<std::streamsize>(size))) throw std::runtime_error("Cannot read VRM file");
    cgltf_options options{}; cgltf_data* raw{};
    if(cgltf_parse(&options,bytes.data(),bytes.size(),&raw)!=cgltf_result_success) throw std::runtime_error("Invalid VRM / GLB file");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    if(data->file_type!=cgltf_file_type_glb || data->buffers_count!=1 || data->buffers[0].uri)
        throw std::runtime_error("Use a self-contained VRM GLB");
    if(cgltf_load_buffers(&options,data.get(),nullptr)!=cgltf_result_success || cgltf_validate(data.get())!=cgltf_result_success)
        throw std::runtime_error("Invalid VRM buffers");
    if(data->nodes_count>20000 || data->images_count>128 || data->meshes_count>2048) throw std::runtime_error("VRM model exceeds import limits");
    const auto json=nlohmann::json::parse(data->json,data->json+data->json_size);
    if(!json.contains("extensions") || (!json["extensions"].contains("VRM") && !json["extensions"].contains("VRMC_vrm")))
        throw std::runtime_error("The selected GLB does not contain a VRM extension");
    auto result=std::make_unique<VrmAvatar>();
    const bool legacy=json["extensions"].contains("VRM");
    result->legacy_=legacy;
    const auto& extension=json["extensions"][legacy?"VRM":"VRMC_vrm"];
    result->title=extension.value("meta",nlohmann::json::object()).value(legacy?"title":"name",std::string("VRM avatar"));
    // Preserve all rest transforms; humanoid and cloned clothing bones receive
    // the same procedural pose at render time.
    std::vector<XMFLOAT4X4> transforms(data->nodes_count);
    std::vector<unsigned char> state(data->nodes_count,0),active(data->nodes_count,0);
    std::function<void(cgltf_node*,int)> world=[&](cgltf_node* n,int depth) {
        const auto i=static_cast<std::size_t>(n-data->nodes);
        if(depth>512 || state[i]==1) throw std::runtime_error("Invalid VRM node hierarchy");
        if(state[i]==2) return;
        state[i]=1; if(n->parent) world(n->parent,depth+1);
        XMFLOAT4X4 local; cgltf_node_transform_local(n,&local._11);
        auto m=XMLoadFloat4x4(&local);
        if(n->parent) m=m*XMLoadFloat4x4(&transforms[n->parent-data->nodes]);
        XMStoreFloat4x4(&transforms[i],m); state[i]=2;
    };
    std::function<void(cgltf_node*,int)> mark=[&](cgltf_node* n,int depth){
        const auto i=n-data->nodes; if(depth>512 || active[i]) throw std::runtime_error("Invalid VRM scene hierarchy");
        active[i]=1; for(std::size_t c=0;c<n->children_count;++c) mark(n->children[c],depth+1);
    };
    if(!data->scene) throw std::runtime_error("VRM has no default scene");
    for(std::size_t i=0;i<data->scene->nodes_count;++i) mark(data->scene->nodes[i],0);
    for(std::size_t i=0;i<data->nodes_count;++i) world(&data->nodes[i],0);
    const char* roleNames[]={"leftUpperArm","rightUpperArm","leftUpperLeg","rightUpperLeg","leftLowerLeg","rightLowerLeg"};
    std::unordered_map<std::string,int> roles; std::vector<int> directRoles(data->nodes_count,-1);
    if(extension.contains("humanoid") && extension["humanoid"].contains("humanBones")) {
        const auto& humanBones=extension["humanoid"]["humanBones"];
        auto addRole=[&](const std::string& name,int index) {
            if(index<0 || static_cast<std::size_t>(index)>=data->nodes_count) return;
            for(int role=0;role<6;++role) if(name==roleNames[role]) {
                directRoles[index]=role;
                if(data->nodes[index].name) roles[boneKey(data->nodes[index].name)]=role;
            }
        };
        if(legacy && humanBones.is_array()) for(const auto& b:humanBones) addRole(b.value("bone",std::string{}),b.value("node",-1));
        if(!legacy && humanBones.is_object()) for(const auto& [name,b]:humanBones.items()) addRole(name,b.value("node",-1));
    }
    result->nodes_.resize(data->nodes_count); result->poseState_.resize(data->nodes_count);
    for(std::size_t i=0;i<data->nodes_count;++i) {
        const auto& source=data->nodes[i]; auto& node=result->nodes_[i];
        cgltf_node_transform_local(&source,&node.local._11); node.rest=node.animated=transforms[i];
        node.parent=source.parent?static_cast<int>(source.parent-data->nodes):-1;
        const auto parent=node.parent<0?XMMatrixIdentity():XMLoadFloat4x4(&transforms[node.parent]);
        XMStoreFloat4x4(&node.parentInverse,XMMatrixInverse(nullptr,parent));
        node.role=directRoles[i];
        if(node.role<0 && source.name) {auto it=roles.find(boneKey(source.name)); if(it!=roles.end()) node.role=it->second;}
        node.side=node.rest._41<0?-1.f:1.f;
    }
    result->skins_.resize(data->skins_count);
    for(std::size_t i=0;i<data->skins_count;++i) {
        const auto& source=data->skins[i]; auto& skin=result->skins_[i];
        if(source.joints_count>MaxBones) throw std::runtime_error("VRM skin exceeds 512 joints");
        auto inverse=unpack(source.inverse_bind_matrices,16);
        if(!inverse.empty() && inverse.size()!=source.joints_count*16) throw std::runtime_error("VRM inverse bind count mismatch");
        skin.inverse.resize(source.joints_count);
        for(std::size_t j=0;j<source.joints_count;++j) {
            skin.joints.push_back(static_cast<std::size_t>(source.joints[j]-data->nodes));
            if(inverse.empty()) XMStoreFloat4x4(&skin.inverse[j],XMMatrixIdentity());
            else std::copy_n(inverse.data()+j*16,16,&skin.inverse[j]._11);
        }
    }
    struct CpuMesh { std::vector<Vertex> vertices; std::vector<UINT> indices; cgltf_material* material; int skin; std::size_t node; };
    std::vector<CpuMesh> meshes;
    XMFLOAT3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for(std::size_t ni=0;ni<data->nodes_count;++ni) {
        auto& node=data->nodes[ni]; if(!active[ni] || !node.mesh) continue;
        std::vector<XMFLOAT4X4> palette;
        if(node.skin) {
            if(node.skin->joints_count>MaxBones) throw std::runtime_error("VRM skin exceeds 512 joints");
            auto inverse=unpack(node.skin->inverse_bind_matrices,16); palette.resize(node.skin->joints_count);
            if(!inverse.empty() && inverse.size()!=palette.size()*16) throw std::runtime_error("VRM inverse bind count mismatch");
            for(std::size_t j=0;j<palette.size();++j) {
                XMMATRIX bind=XMMatrixIdentity();
                if(!inverse.empty()) { XMFLOAT4X4 b; std::copy_n(inverse.data()+j*16,16,&b._11); bind=XMLoadFloat4x4(&b); }
                XMStoreFloat4x4(&palette[j],bind*XMLoadFloat4x4(&transforms[node.skin->joints[j]-data->nodes]));
            }
        }
        for(std::size_t pi=0;pi<node.mesh->primitives_count;++pi) {
            auto& primitive=node.mesh->primitives[pi];
            if(primitive.type!=cgltf_primitive_type_triangles || primitive.has_draco_mesh_compression)
                throw std::runtime_error("VRM requires uncompressed triangle geometry");
            auto* positions=attr(primitive.attributes,primitive.attributes_count,cgltf_attribute_type_position);
            if(!positions) continue;
            auto p=unpack(positions,3),normals=unpack(attr(primitive.attributes,primitive.attributes_count,cgltf_attribute_type_normal),3);
            auto uv=unpack(attr(primitive.attributes,primitive.attributes_count,cgltf_attribute_type_texcoord),2);
            auto joints=unpack(attr(primitive.attributes,primitive.attributes_count,cgltf_attribute_type_joints),4);
            auto weights=unpack(attr(primitive.attributes,primitive.attributes_count,cgltf_attribute_type_weights),4);
            const auto count=positions->count;
            if((!normals.empty() && normals.size()!=count*3) || (!uv.empty() && uv.size()!=count*2) ||
               (!joints.empty() && (joints.size()!=count*4 || weights.size()!=count*4))) throw std::runtime_error("VRM attribute count mismatch");
            for(std::size_t t=0;t<primitive.targets_count;++t) {
                const float weight=t<node.weights_count?node.weights[t]:(t<node.mesh->weights_count?node.mesh->weights[t]:0);
                if(weight==0) continue;
                for(const auto type:{cgltf_attribute_type_position,cgltf_attribute_type_normal}) {
                    auto delta=unpack(attr(primitive.targets[t].attributes,primitive.targets[t].attributes_count,type),3);
                    auto& dest=type==cgltf_attribute_type_position?p:normals;
                    if(delta.empty() || dest.empty()) continue;
                    if(delta.size()!=dest.size()) throw std::runtime_error("VRM morph size mismatch");
                    for(std::size_t k=0;k<dest.size();++k) dest[k]+=weight*delta[k];
                }
            }
            result->vertexCount_+=count; if(result->vertexCount_>2'000'000) throw std::runtime_error("VRM exceeds 2 million vertices");
            CpuMesh mesh; mesh.material=primitive.material; mesh.vertices.resize(count);
            mesh.node=ni; mesh.skin=node.skin && !joints.empty()?static_cast<int>(node.skin-data->skins):-1;
            for(std::size_t v=0;v<count;++v) {
                auto pos=XMVectorSet(p[v*3],p[v*3+1],p[v*3+2],1);
                auto normal=normals.empty()?XMVectorSet(0,1,0,0):XMVectorSet(normals[v*3],normals[v*3+1],normals[v*3+2],0);
                XMMATRIX matrix=XMLoadFloat4x4(&transforms[ni]);
                if(!palette.empty() && !joints.empty()) {
                    matrix=XMMatrixScaling(0,0,0); matrix.r[3]=XMVectorZero(); float total=0;
                    for(int k=0;k<4;++k) {
                        const auto j=static_cast<std::size_t>(joints[v*4+k]); const float w=weights[v*4+k];
                        if(j>=palette.size() || w<0) throw std::runtime_error("Invalid VRM skin weights");
                        matrix=matrix+XMLoadFloat4x4(&palette[j])*w; total+=w;
                    }
                    if(total<=.00001f) throw std::runtime_error("Empty VRM skin weights");
                    matrix=matrix*(1.f/total);
                }
                auto& out=mesh.vertices[v]; XMStoreFloat3(&out.p,pos); XMStoreFloat3(&out.n,normal);
                if(mesh.skin>=0) {
                    float total=0; for(int k=0;k<4;++k) total+=weights[v*4+k];
                    for(int k=0;k<4;++k) {out.joints[k]=static_cast<UINT>(joints[v*4+k]); out.weights[k]=weights[v*4+k]/total;}
                }
                XMFLOAT3 bound; XMStoreFloat3(&bound,XMVector3TransformCoord(pos,matrix));
                if(legacy) bound.x=-bound.x; else bound.z=-bound.z;
                out.uv=uv.empty()?XMFLOAT2{}:XMFLOAT2{uv[v*2],uv[v*2+1]};
                lo={std::min(lo.x,bound.x),std::min(lo.y,bound.y),std::min(lo.z,bound.z)};
                hi={std::max(hi.x,bound.x),std::max(hi.y,bound.y),std::max(hi.z,bound.z)};
            }
            const auto indices=primitive.indices?primitive.indices->count:count;
            if(indices>6'000'000 || indices%3) throw std::runtime_error("Invalid VRM index count");
            mesh.indices.resize(indices);
            for(std::size_t i=0;i<indices;++i) {
                const auto index=primitive.indices?cgltf_accessor_read_index(primitive.indices,i):i;
                if(index>=count) throw std::runtime_error("Invalid VRM vertex index"); mesh.indices[i]=static_cast<UINT>(index);
            }
            meshes.push_back(std::move(mesh));
        }
    }
    const float height=hi.y-lo.y;
    if(meshes.empty() || !std::isfinite(height) || height<.01f || height>10000) throw std::runtime_error("Invalid VRM bounds");
    const float scale=2.8f/height,centerX=(lo.x+hi.x)*.5f,centerZ=(lo.z+hi.z)*.5f;
    XMStoreFloat4x4(&result->normalization_,XMMatrixScaling(legacy?-1.f:1.f,1,legacy?1.f:-1.f)*
        XMMatrixTranslation(-centerX,-lo.y,-centerZ)*XMMatrixScaling(scale,scale,scale)*XMMatrixTranslation(0,.18f,0));
    ComPtr<IWICImagingFactory> factory; checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"VRM WIC factory");
    std::unordered_map<cgltf_image*,ComPtr<ID3D11ShaderResourceView>> textures;
    for(auto& cpu:meshes) {
        Mesh gpu;
        gpu.skin=cpu.skin; gpu.node=cpu.node;
        auto buffer=[&](UINT length,UINT bind,const void* memory,ID3D11Buffer** out) {
            D3D11_BUFFER_DESC desc{}; desc.ByteWidth=length; desc.BindFlags=bind; desc.Usage=D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA initial{memory,0,0}; checked(device->CreateBuffer(&desc,&initial,out),"VRM mesh buffer");
        };
        buffer(static_cast<UINT>(cpu.vertices.size()*sizeof(Vertex)),D3D11_BIND_VERTEX_BUFFER,cpu.vertices.data(),&gpu.vertices);
        buffer(static_cast<UINT>(cpu.indices.size()*sizeof(UINT)),D3D11_BIND_INDEX_BUFFER,cpu.indices.data(),&gpu.indices);
        gpu.count=static_cast<UINT>(cpu.indices.size()); cgltf_image* image=nullptr;
        if(cpu.material) {
            const auto& m=*cpu.material; const auto& base=m.pbr_metallic_roughness;
            gpu.color={base.base_color_factor[0],base.base_color_factor[1],base.base_color_factor[2],base.base_color_factor[3]};
            if(base.base_color_texture.texture) image=base.base_color_texture.texture->image;
            if(m.alpha_mode==cgltf_alpha_mode_mask) gpu.cutoff=m.alpha_cutoff;
            gpu.blend=m.alpha_mode==cgltf_alpha_mode_blend;
        }
        if(!textures.contains(image)) textures[image]=texture(device,factory.Get(),image);
        gpu.texture=textures.at(image); result->meshes_.push_back(std::move(gpu));
    }
    std::stable_sort(result->meshes_.begin(),result->meshes_.end(),[](const auto& a,const auto& b){return a.blend<b.blend;});
    ComPtr<ID3DBlob> vs,ps,error;
    for(auto entry:{"vsMain","psMain"}) {
        const bool vertex=entry[0]=='v'; auto& code=vertex?vs:ps;
        const auto hr=D3DCompile(Shader,sizeof(Shader)-1,"vrm.hlsl",nullptr,nullptr,entry,vertex?"vs_4_0":"ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(hr) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer())); checked(hr,"VRM shader compilation");
    }
    checked(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&result->vs_),"VRM vertex shader");
    checked(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&result->ps_),"VRM pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},{"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
    checked(device->CreateInputLayout(elements,5,vs->GetBufferPointer(),vs->GetBufferSize(),&result->layout_),"VRM vertex layout");
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    checked(device->CreateBuffer(&cb,nullptr,&result->constants_),"VRM draw constants");
    D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; sampler.MaxLOD=D3D11_FLOAT32_MAX;
    checked(device->CreateSamplerState(&sampler,&result->sampler_),"VRM sampler");
    // Two-sided avoids holes when handedness changes and supports thin hair cards.
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
    checked(device->CreateRasterizerState(&raster,&result->raster_),"VRM rasterizer");
    D3D11_BLEND_DESC blend{}; auto& rt=blend.RenderTarget[0]; rt.BlendEnable=TRUE; rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp=D3D11_BLEND_OP_ADD; rt.SrcBlendAlpha=D3D11_BLEND_ONE;
    rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha=D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    checked(device->CreateBlendState(&blend,&result->blend_),"VRM blend state");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=TRUE; depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc=D3D11_COMPARISON_LESS;
    checked(device->CreateDepthStencilState(&depth,&result->depth_),"VRM depth state"); depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    checked(device->CreateDepthStencilState(&depth,&result->transparentDepth_),"VRM transparent depth state");
    return result;
}
void VrmAvatar::animate(const AvatarPose& pose) {
    std::fill(poseState_.begin(),poseState_.end(),static_cast<unsigned char>(0));
    const float pitchSign=legacy_?1.f:-1.f;
    auto update=[&](auto&& self,std::size_t i)->void {
        if(poseState_[i]) return;
        auto& node=nodes_[i]; if(node.parent>=0) self(self,static_cast<std::size_t>(node.parent));
        auto local=XMLoadFloat4x4(&node.local);
        if(node.role>=0) {
            float pitch=0,roll=0; const int side=(legacy_?-node.side:node.side)<0?0:1;
            if(node.role<2) {pitch=pose.armPitch[side]*pitchSign; roll=-node.side*(XM_PIDIV2-pose.armOut[side]);}
            else if(node.role<4) {pitch=pose.legPitch[side]*pitchSign; roll=node.side*pose.legOut[side];}
            else pitch=pose.knee[side]*pitchSign;
            const auto& rest=node.rest;
            local=XMLoadFloat4x4(&rest)*XMMatrixTranslation(-rest._41,-rest._42,-rest._43)*
                XMMatrixRotationZ(roll)*XMMatrixRotationX(pitch)*XMMatrixTranslation(rest._41,rest._42,rest._43)*XMLoadFloat4x4(&node.parentInverse);
        }
        XMStoreFloat4x4(&node.animated,local*(node.parent<0?XMMatrixIdentity():XMLoadFloat4x4(&nodes_[node.parent].animated)));
        poseState_[i]=1;
    };
    for(std::size_t i=0;i<nodes_.size();++i) update(update,i);
}
XMFLOAT3 VrmAvatar::bonePosition(int role) const {
    for(const auto& node:nodes_) if(node.role==role) return {node.animated._41,node.animated._42,node.animated._43};
    return {};
}
void VrmAvatar::draw(ID3D11DeviceContext* c,FXMMATRIX vp,CXMMATRIX model,const AvatarPose& pose) {
    animate(pose);
    c->IASetInputLayout(layout_.Get()); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs_.Get(),nullptr,0); c->PSSetShader(ps_.Get(),nullptr,0); c->RSSetState(raster_.Get());
    ID3D11Buffer* cb=constants_.Get(); c->VSSetConstantBuffers(0,1,&cb); c->PSSetConstantBuffers(0,1,&cb);
    ID3D11SamplerState* sampler=sampler_.Get(); c->PSSetSamplers(0,1,&sampler);
    Constants constants{}; XMStoreFloat4x4(&constants.vp,vp); XMStoreFloat4x4(&constants.model,XMLoadFloat4x4(&normalization_)*model);
    for(const auto& mesh:meshes_) {
        constants.color=mesh.color; constants.options={mesh.cutoff,mesh.blend?1.f:0.f,0,0};
        if(mesh.skin<0) constants.bones[0]=nodes_[mesh.node].animated;
        else {
            const auto& skin=skins_[mesh.skin];
            for(std::size_t j=0;j<skin.joints.size();++j) XMStoreFloat4x4(&constants.bones[j],
                XMLoadFloat4x4(&skin.inverse[j])*XMLoadFloat4x4(&nodes_[skin.joints[j]].animated));
        }
        c->UpdateSubresource(cb,0,nullptr,&constants,0,0);
        c->OMSetBlendState(mesh.blend?blend_.Get():nullptr,nullptr,0xffffffff);
        c->OMSetDepthStencilState(mesh.blend?transparentDepth_.Get():depth_.Get(),0);
        ID3D11Buffer* v=mesh.vertices.Get(); const UINT stride=sizeof(Vertex),offset=0; c->IASetVertexBuffers(0,1,&v,&stride,&offset);
        c->IASetIndexBuffer(mesh.indices.Get(),DXGI_FORMAT_R32_UINT,0);
        ID3D11ShaderResourceView* texture=mesh.texture.Get(); c->PSSetShaderResources(0,1,&texture); c->DrawIndexed(mesh.count,0,0);
    }
    ID3D11ShaderResourceView* empty=nullptr; c->PSSetShaderResources(0,1,&empty);
}
}
