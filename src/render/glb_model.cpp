#include "render/glb_model.hpp"
#include "cgltf/cgltf.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <span>
#include <cfloat>
#include <stdexcept>
#include <unordered_map>

namespace aura {
void checked(HRESULT,const char*);
using namespace DirectX;
using Microsoft::WRL::ComPtr;
namespace {
struct Vertex {XMFLOAT3 position,normal;XMFLOAT2 uv;};
struct Constants {XMFLOAT4X4 vp,model,normal;XMFLOAT4 color,material;};
constexpr char Shader[]=R"(
cbuffer Draw:register(b0) {row_major float4x4 vp,model,normalMatrix;float4 baseColor,material;};
Texture2D image:register(t0);SamplerState sampleImage:register(s0);
struct V {float3 p:POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;};
struct O {float4 p:SV_POSITION;float3 n:NORMAL;float2 uv:TEXCOORD0;};
O vsMain(V v) {O o;o.p=mul(mul(float4(v.p,1),model),vp);o.n=mul(float4(v.n,0),normalMatrix).xyz;o.uv=v.uv;return o;}
float4 psMain(O i):SV_TARGET {
 float4 c=image.Sample(sampleImage,i.uv)*baseColor;
 if(material.x>=0)clip(c.a-material.x);
 float3 n=normalize(i.n);float light=.76+.24*abs(dot(n,normalize(float3(-.4,.85,-.5))));
 // 青い模様を白飛びさせず、控えめな艶だけ足します。発光用bloomには入りません。
 float shine=pow(abs(dot(n,normalize(float3(-.28,.65,-.70)))),lerp(60,8,material.z));
 float3 color=material.w<0?c.rgb:c.rgb*light+c.rgb*shine*material.w*.22;
 return float4(saturate(color),material.y>0?c.a:1);
})";

cgltf_accessor* attribute(const cgltf_primitive& p,cgltf_attribute_type type) {
    for(std::size_t i=0;i<p.attributes_count;++i)if(p.attributes[i].type==type && p.attributes[i].index==0)return p.attributes[i].data;
    return nullptr;
}
std::vector<float> unpack(cgltf_accessor* source,std::size_t components) {
    if(!source)return {};
    if(source->count>1'000'000 || cgltf_num_components(source->type)!=components)throw std::runtime_error("Unsupported GLB accessor");
    std::vector<float> result(source->count*components);
    if(cgltf_accessor_unpack_floats(source,result.data(),result.size())!=result.size())throw std::runtime_error("Invalid GLB accessor");
    for(float v:result)if(!std::isfinite(v))throw std::runtime_error("Nonfinite GLB data");
    return result;
}
void checkQuaternion(const XMFLOAT4& q) {
    const float length=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if(!std::isfinite(length) || length<1e-8f)throw std::runtime_error("Invalid GLB rotation");
}
ComPtr<ID3D11ShaderResourceView> loadTexture(ID3D11Device* device,IWICImagingFactory* factory,cgltf_image* image) {
    UINT width=1,height=1;std::vector<BYTE> pixels{255,255,255,255};
    if(image) {
        if(image->uri || !image->buffer_view || !image->mime_type ||
            (std::strcmp(image->mime_type,"image/png") && std::strcmp(image->mime_type,"image/jpeg")))
            throw std::runtime_error("GLB texture must be an embedded PNG/JPEG");
        const auto length=image->buffer_view->size;
        const auto* bytes=cgltf_buffer_view_data(image->buffer_view);
        if(!bytes || !length || length>32*1024*1024)throw std::runtime_error("Invalid GLB image size");
        ComPtr<IWICStream> stream;checked(factory->CreateStream(&stream),"GLB image stream");
        checked(stream->InitializeFromMemory(const_cast<BYTE*>(bytes),static_cast<DWORD>(length)),"GLB image bytes");
        ComPtr<IWICBitmapDecoder> decoder;checked(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"GLB image decode");
        ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,&frame),"GLB image frame");
        checked(frame->GetSize(&width,&height),"GLB image dimensions");
        if(!width || !height || width>8192 || height>8192)throw std::runtime_error("GLB image exceeds dimensions limit");
        // 遠くの蝶に巨大な画像は不要。元画像を変えず、GPU用だけ最大2048へ縮小します。
        const float ratio=std::min(1.f,2048.f/std::max(width,height));
        width=std::max(1u,static_cast<UINT>(width*ratio));height=std::max(1u,static_cast<UINT>(height*ratio));
        ComPtr<IWICBitmapScaler> scaler;checked(factory->CreateBitmapScaler(&scaler),"GLB texture scaler");
        checked(scaler->Initialize(frame.Get(),width,height,WICBitmapInterpolationModeFant),"GLB texture scaling");
        ComPtr<IWICFormatConverter> converter;checked(factory->CreateFormatConverter(&converter),"GLB image converter");
        checked(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"GLB RGBA texture");
        pixels.resize(static_cast<std::size_t>(width)*height*4);
        checked(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()),"GLB texture pixels");
    }
    // ミップマップは遠くの小さい羽のちらつきを減らすための縮小版です。作るのは読込時だけ。
    // CPUで作ってまとめて送るので、バックグラウンド読込が描画中のcontextへ触りません。
    std::vector<std::vector<BYTE>> levels;levels.push_back(std::move(pixels));
    UINT mipWidth=width,mipHeight=height;
    while(mipWidth>1 || mipHeight>1) {
        const UINT nextWidth=std::max(1u,mipWidth/2),nextHeight=std::max(1u,mipHeight/2);
        std::vector<BYTE> next(static_cast<std::size_t>(nextWidth)*nextHeight*4);
        const auto& previous=levels.back();
        for(UINT y=0;y<nextHeight;++y)for(UINT x=0;x<nextWidth;++x)for(UINT channel=0;channel<4;++channel) {
            unsigned total=0;
            for(UINT dy=0;dy<2;++dy)for(UINT dx=0;dx<2;++dx)
                total+=previous[(static_cast<std::size_t>(std::min(y*2+dy,mipHeight-1))*mipWidth+std::min(x*2+dx,mipWidth-1))*4+channel];
            next[(static_cast<std::size_t>(y)*nextWidth+x)*4+channel]=static_cast<BYTE>((total+2)/4);
        }
        levels.push_back(std::move(next));mipWidth=nextWidth;mipHeight=nextHeight;
    }
    std::vector<D3D11_SUBRESOURCE_DATA> initial(levels.size());mipWidth=width;
    for(std::size_t i=0;i<levels.size();++i){initial[i]={levels[i].data(),mipWidth*4,0};mipWidth=std::max(1u,mipWidth/2);}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=static_cast<UINT>(levels.size());desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;checked(device->CreateTexture2D(&desc,initial.data(),&texture),"GLB texture");
    ComPtr<ID3D11ShaderResourceView> view;checked(device->CreateShaderResourceView(texture.Get(),nullptr,&view),"GLB texture view");
    return view;
}
}

std::unique_ptr<GlbModel> GlbModel::load(ID3D11Device* device,const std::filesystem::path& path) {
    const auto size=std::filesystem::file_size(path);
    if(size<20 || size>64ull*1024*1024)throw std::runtime_error("Aura GLB must be below 64 MiB");
    std::ifstream input(path,std::ios::binary);std::vector<char> bytes(static_cast<std::size_t>(size));
    if(!input.read(bytes.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("Cannot read aura GLB");
    cgltf_options options{};cgltf_data* parsed{};
    if(cgltf_parse(&options,bytes.data(),bytes.size(),&parsed)!=cgltf_result_success)throw std::runtime_error("Invalid aura GLB");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(parsed,cgltf_free);
    if(data->file_type!=cgltf_file_type_glb || data->buffers_count!=1 || data->buffers[0].uri)
        throw std::runtime_error("Use a self-contained aura GLB");
    // URIを読む処理を許可しないので、GLBから外部ファイルやネットへアクセスしません。
    for(std::size_t i=0;i<data->images_count;++i)if(data->images[i].uri)throw std::runtime_error("External GLB image is unsupported");
    if(cgltf_load_buffers(&options,data.get(),nullptr)!=cgltf_result_success || cgltf_validate(data.get())!=cgltf_result_success)
        throw std::runtime_error("Invalid aura GLB buffers");
    if(!data->scene || !data->nodes_count || data->nodes_count>2048 || data->images_count>16 || data->meshes_count>128 || data->skins_count)
        throw std::runtime_error("Aura GLB exceeds rigid model limits");
    for(std::size_t i=0;i<data->extensions_required_count;++i) {
        const std::string name=data->extensions_required[i];
        if(name!="KHR_materials_specular" && name!="KHR_materials_unlit")throw std::runtime_error("Required GLB extension is unsupported");
    }
    for(std::size_t i=0;i<data->buffer_views_count;++i)if(data->buffer_views[i].has_meshopt_compression)
        throw std::runtime_error("Compressed GLB geometry is unsupported");
    auto result=std::make_unique<GlbModel>();
    result->nodes_.resize(data->nodes_count);result->pose_.resize(data->nodes_count);result->world_.resize(data->nodes_count);
    std::vector<unsigned char> state(data->nodes_count),active(data->nodes_count);
    for(std::size_t i=0;i<data->nodes_count;++i) {
        const auto& source=data->nodes[i];auto& node=result->nodes_[i];
        if(source.skin || source.has_mesh_gpu_instancing)throw std::runtime_error("Use rigid GLB mesh nodes");
        node.parent=source.parent?static_cast<int>(source.parent-data->nodes):-1;node.matrix=source.has_matrix!=0;
        cgltf_node_transform_local(&source,&node.local._11);
        for(float value:std::span<const float>(&node.local._11,16))if(!std::isfinite(value))throw std::runtime_error("Invalid GLB node transform");
        node.rest.translation={source.translation[0],source.translation[1],source.translation[2]};
        node.rest.rotation={source.rotation[0],source.rotation[1],source.rotation[2],source.rotation[3]};
        node.rest.scale={source.scale[0],source.scale[1],source.scale[2]};checkQuaternion(node.rest.rotation);
    }
    std::function<void(std::size_t,unsigned)> order=[&](std::size_t i,unsigned depth) {
        if(depth>128 || state[i]==1)throw std::runtime_error("Invalid GLB hierarchy");
        if(state[i]==2)return;
        state[i]=1;const int parent=result->nodes_[i].parent;if(parent>=0)order(static_cast<std::size_t>(parent),depth+1);
        result->order_.push_back(i);state[i]=2;
    };
    for(std::size_t i=0;i<data->nodes_count;++i)order(i,0);
    std::function<void(cgltf_node*,unsigned)> mark=[&](cgltf_node* n,unsigned depth) {
        const auto i=static_cast<std::size_t>(n-data->nodes);
        if(depth>128 || active[i])throw std::runtime_error("Invalid GLB scene hierarchy");
        active[i]=1;for(std::size_t c=0;c<n->children_count;++c)mark(n->children[c],depth+1);
    };
    for(std::size_t i=0;i<data->scene->nodes_count;++i)mark(data->scene->nodes[i],0);

    // 最初のアニメーションクリップを再生。元のキー時刻と親子関係を保持します。
    if(data->animations_count) {
        const auto& animation=data->animations[0];
        if(animation.channels_count>4096)throw std::runtime_error("Aura GLB has too many animation channels");
        for(std::size_t i=0;i<animation.channels_count;++i) {
            const auto& channel=animation.channels[i];const auto* sampler=channel.sampler;
            if(!channel.target_node || !sampler || !sampler->input || !sampler->output ||
                (sampler->interpolation!=cgltf_interpolation_type_linear && sampler->interpolation!=cgltf_interpolation_type_step))
                throw std::runtime_error("Aura GLB supports linear/step TRS animation");
            Track track;track.node=static_cast<std::size_t>(channel.target_node-data->nodes);
            if(result->nodes_[track.node].matrix)throw std::runtime_error("Animated GLB nodes must use TRS");
            std::size_t components=3;
            if(channel.target_path==cgltf_animation_path_type_translation)track.path=Path::Translation;
            else if(channel.target_path==cgltf_animation_path_type_scale)track.path=Path::Scale;
            else if(channel.target_path==cgltf_animation_path_type_rotation){track.path=Path::Rotation;components=4;}
            else throw std::runtime_error("Aura GLB morph animation is unsupported");
            for(const auto& existing:result->tracks_)if(existing.node==track.node && existing.path==track.path)
                throw std::runtime_error("Duplicate GLB animation channel");
            track.step=sampler->interpolation==cgltf_interpolation_type_step;
            track.times=unpack(sampler->input,1);const auto values=unpack(sampler->output,components);
            if(track.times.empty() || values.size()!=track.times.size()*components)throw std::runtime_error("Invalid GLB animation keys");
            float previous=-1;
            for(float t:track.times){if(t<0 || t<=previous)throw std::runtime_error("Invalid GLB animation times");previous=t;}
            track.values.resize(track.times.size());
            for(std::size_t k=0;k<track.values.size();++k){auto& v=track.values[k];v={values[k*components],values[k*components+1],values[k*components+2],components==4?values[k*components+3]:0};if(components==4)checkQuaternion(v);}
            result->duration_=std::max(result->duration_,track.times.back());result->tracks_.push_back(std::move(track));
        }
    }
    // 正規化は静止時の全メッシュの範囲から一度だけ求めます。羽ばたきで大きさは変えません。
    for(const auto i:result->order_) {
        const auto& node=result->nodes_[i];
        XMStoreFloat4x4(&result->world_[i],XMLoadFloat4x4(&node.local)*(node.parent<0?XMMatrixIdentity():XMLoadFloat4x4(&result->world_[node.parent])));
    }
    XMFLOAT3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    ComPtr<IWICImagingFactory> factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"GLB WIC factory");
    std::unordered_map<cgltf_image*,ComPtr<ID3D11ShaderResourceView>> textures;
    for(std::size_t ni=0;ni<data->nodes_count;++ni) {
        const auto& node=data->nodes[ni];if(!active[ni] || !node.mesh)continue;
        for(std::size_t pi=0;pi<node.mesh->primitives_count;++pi) {
            const auto& primitive=node.mesh->primitives[pi];
            if(primitive.type!=cgltf_primitive_type_triangles || primitive.has_draco_mesh_compression || primitive.targets_count)
                throw std::runtime_error("Aura GLB requires uncompressed rigid triangles");
            const auto positions=unpack(attribute(primitive,cgltf_attribute_type_position),3);
            const auto normals=unpack(attribute(primitive,cgltf_attribute_type_normal),3);
            const auto uv=unpack(attribute(primitive,cgltf_attribute_type_texcoord),2);
            const auto count=positions.size()/3;
            if(!count || (!normals.empty() && normals.size()!=positions.size()) || (!uv.empty() && uv.size()!=count*2))
                throw std::runtime_error("Invalid GLB vertex attributes");
            result->vertexCount_+=count;if(result->vertexCount_>1'000'000 || result->meshes_.size()>=512)throw std::runtime_error("Aura GLB geometry exceeds limit");
            std::vector<Vertex> vertices(count);const auto world=XMLoadFloat4x4(&result->world_[ni]);
            for(std::size_t v=0;v<count;++v) {
                auto& out=vertices[v];out.position={positions[v*3],positions[v*3+1],positions[v*3+2]};
                out.normal=normals.empty()?XMFLOAT3{0,1,0}:XMFLOAT3{normals[v*3],normals[v*3+1],normals[v*3+2]};
                out.uv=uv.empty()?XMFLOAT2{}:XMFLOAT2{uv[v*2],uv[v*2+1]};
                XMFLOAT3 point;XMStoreFloat3(&point,XMVector3TransformCoord(XMLoadFloat3(&out.position),world));
                if(!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))throw std::runtime_error("Invalid GLB geometry bounds");
                lo={std::min(lo.x,point.x),std::min(lo.y,point.y),std::min(lo.z,point.z)};
                hi={std::max(hi.x,point.x),std::max(hi.y,point.y),std::max(hi.z,point.z)};
            }
            const auto indexCount=primitive.indices?primitive.indices->count:count;
            if(!indexCount || indexCount>3'000'000 || indexCount%3)throw std::runtime_error("Invalid GLB triangle indices");
            std::vector<UINT> indices(indexCount);
            for(std::size_t i=0;i<indexCount;++i){const auto index=primitive.indices?cgltf_accessor_read_index(primitive.indices,i):i;if(index>=count)throw std::runtime_error("GLB vertex index out of range");indices[i]=static_cast<UINT>(index);}
            Mesh mesh;mesh.node=ni;mesh.count=static_cast<UINT>(indexCount);
            auto buffer=[&](UINT length,UINT bind,const void* memory,ID3D11Buffer** output){D3D11_BUFFER_DESC desc{};desc.ByteWidth=length;desc.BindFlags=bind;desc.Usage=D3D11_USAGE_IMMUTABLE;D3D11_SUBRESOURCE_DATA init{memory,0,0};checked(device->CreateBuffer(&desc,&init,output),"GLB mesh buffer");};
            buffer(static_cast<UINT>(vertices.size()*sizeof(Vertex)),D3D11_BIND_VERTEX_BUFFER,vertices.data(),&mesh.vertices);
            buffer(static_cast<UINT>(indices.size()*sizeof(UINT)),D3D11_BIND_INDEX_BUFFER,indices.data(),&mesh.indices);
            cgltf_image* image=nullptr;
            if(primitive.material) {
                const auto& material=*primitive.material;const auto& pbr=material.pbr_metallic_roughness;
                mesh.color={pbr.base_color_factor[0],pbr.base_color_factor[1],pbr.base_color_factor[2],pbr.base_color_factor[3]};
                if(pbr.base_color_texture.texture) {
                    if(pbr.base_color_texture.texcoord!=0 || pbr.base_color_texture.has_transform || pbr.base_color_texture.texture->has_basisu || pbr.base_color_texture.texture->has_webp)
                        throw std::runtime_error("Aura GLB requires ordinary embedded base color texture");
                    image=pbr.base_color_texture.texture->image;
                }
                mesh.blend=material.alpha_mode==cgltf_alpha_mode_blend;mesh.unlit=material.unlit!=0;
                if(material.alpha_mode==cgltf_alpha_mode_mask)mesh.cutoff=material.alpha_cutoff;
                mesh.roughness=std::clamp(pbr.roughness_factor,0.f,1.f);
                mesh.specular=material.has_specular?std::clamp(material.specular.specular_factor,0.f,1.f):.15f;
            }
            if(!textures.contains(image))textures[image]=loadTexture(device,factory.Get(),image);
            mesh.texture=textures.at(image);result->meshes_.push_back(std::move(mesh));
        }
    }
    const float longest=std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z});
    if(result->meshes_.empty() || !std::isfinite(longest) || longest<1e-6f)throw std::runtime_error("Aura GLB has empty bounds");
    XMStoreFloat4x4(&result->normalization_,XMMatrixTranslation(-(lo.x+hi.x)*.5f,-(lo.y+hi.y)*.5f,-(lo.z+hi.z)*.5f)*XMMatrixScaling(1/longest,1/longest,-1/longest));
    std::stable_sort(result->meshes_.begin(),result->meshes_.end(),[](const Mesh& a,const Mesh& b){return a.blend<b.blend;});
    ComPtr<ID3DBlob> vs,ps,error;
    for(const auto entry:{"vsMain","psMain"}) {
        const bool vertex=entry[0]=='v';auto& code=vertex?vs:ps;
        const auto hr=D3DCompile(Shader,sizeof(Shader)-1,"aura-glb.hlsl",nullptr,nullptr,entry,vertex?"vs_4_0":"ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(hr) && error)throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));checked(hr,"GLB shader compilation");
    }
    checked(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&result->vs_),"GLB vertex shader");
    checked(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&result->ps_),"GLB pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
    checked(device->CreateInputLayout(elements,3,vs->GetBufferPointer(),vs->GetBufferSize(),&result->layout_),"GLB vertex layout");
    D3D11_BUFFER_DESC constants{};constants.ByteWidth=sizeof(Constants);constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    checked(device->CreateBuffer(&constants,nullptr,&result->constants_),"GLB draw constants");
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    checked(device->CreateSamplerState(&sampler,&result->sampler_),"GLB texture sampler");
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    checked(device->CreateRasterizerState(&raster,&result->raster_),"GLB two-sided rasterizer");
    D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_SRC_ALPHA;target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    checked(device->CreateBlendState(&blend,&result->blend_),"GLB alpha blend");
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth.DepthFunc=D3D11_COMPARISON_LESS;
    checked(device->CreateDepthStencilState(&depth,&result->depth_),"GLB depth state");depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    checked(device->CreateDepthStencilState(&depth,&result->transparentDepth_),"GLB transparent depth state");
    return result;
}

void GlbModel::evaluate(float seconds) {
    const float time=duration_>0 && std::isfinite(seconds)?std::fmod(std::max(0.f,seconds),duration_):0;
    for(std::size_t i=0;i<nodes_.size();++i)pose_[i]=nodes_[i].rest;
    for(const auto& track:tracks_) {
        const auto upper=std::upper_bound(track.times.begin(),track.times.end(),time);
        const std::size_t first=upper==track.times.begin()?0:static_cast<std::size_t>(upper-track.times.begin()-1);
        const std::size_t second=std::min(first+1,track.times.size()-1);
        const float amount=track.step || first==second?0:std::clamp((time-track.times[first])/(track.times[second]-track.times[first]),0.f,1.f);
        const auto a=XMLoadFloat4(&track.values[first]),b=XMLoadFloat4(&track.values[second]);
        auto& pose=pose_[track.node];
        if(track.path==Path::Rotation)XMStoreFloat4(&pose.rotation,XMQuaternionNormalize(XMQuaternionSlerp(XMQuaternionNormalize(a),XMQuaternionNormalize(b),amount)));
        else if(track.path==Path::Translation)XMStoreFloat3(&pose.translation,XMVectorLerp(a,b,amount));
        else XMStoreFloat3(&pose.scale,XMVectorLerp(a,b,amount));
    }
    for(const auto i:order_) {
        const auto& node=nodes_[i];const auto& pose=pose_[i];
        const auto local=node.matrix?XMLoadFloat4x4(&node.local):XMMatrixScaling(pose.scale.x,pose.scale.y,pose.scale.z)*XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&pose.rotation)))*XMMatrixTranslation(pose.translation.x,pose.translation.y,pose.translation.z);
        XMStoreFloat4x4(&world_[i],local*(node.parent<0?XMMatrixIdentity():XMLoadFloat4x4(&world_[node.parent])));
    }
}
XMFLOAT4X4 GlbModel::nodeTransform(std::size_t node,float seconds) {
    if(node>=nodes_.size())throw std::out_of_range("GLB node index");
    evaluate(seconds);return world_[node];
}
void GlbModel::draw(ID3D11DeviceContext* context,FXMMATRIX vp,CXMMATRIX model,float seconds) {
    evaluate(seconds);
    context->IASetInputLayout(layout_.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_.Get(),nullptr,0);context->PSSetShader(ps_.Get(),nullptr,0);context->RSSetState(raster_.Get());
    ID3D11Buffer* buffer=constants_.Get();context->VSSetConstantBuffers(0,1,&buffer);context->PSSetConstantBuffers(0,1,&buffer);
    ID3D11SamplerState* sampler=sampler_.Get();context->PSSetSamplers(0,1,&sampler);
    Constants constants{};XMStoreFloat4x4(&constants.vp,vp);
    for(const auto& mesh:meshes_) {
        const auto transform=XMLoadFloat4x4(&world_[mesh.node])*XMLoadFloat4x4(&normalization_)*model;
        XMStoreFloat4x4(&constants.model,transform);XMStoreFloat4x4(&constants.normal,XMMatrixTranspose(XMMatrixInverse(nullptr,transform)));
        constants.color=mesh.color;constants.material={mesh.cutoff,mesh.blend?1.f:0.f,mesh.roughness,mesh.unlit?-1.f:mesh.specular};
        context->UpdateSubresource(buffer,0,nullptr,&constants,0,0);
        context->OMSetBlendState(mesh.blend?blend_.Get():nullptr,nullptr,0xffffffff);context->OMSetDepthStencilState(mesh.blend?transparentDepth_.Get():depth_.Get(),0);
        ID3D11Buffer* vertices=mesh.vertices.Get();const UINT stride=sizeof(Vertex),offset=0;context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        context->IASetIndexBuffer(mesh.indices.Get(),DXGI_FORMAT_R32_UINT,0);
        ID3D11ShaderResourceView* texture=mesh.texture.Get();context->PSSetShaderResources(0,1,&texture);context->DrawIndexed(mesh.count,0,0);
    }
    ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);
}
} // namespace aura
