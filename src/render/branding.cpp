#include "render/renderer.hpp"
#include "tuning/branding.hpp"
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

// 左上のロゴ専用。①PNGを読む → ②透明な余白を除く → ③星・文字・周回ロケットを描く。
// 画像ファイル自体は変更せず、毎フレームのPNG読み込みや大きなテクスチャを避けます。
namespace aura {
namespace {
namespace cfg=tuning::branding;
constexpr double Pi=3.14159265358979323846;

std::filesystem::path imageFolder() {
    std::wstring path(32768,L'\0');
    const DWORD length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!length || length==path.size())throw std::runtime_error("Cannot locate branding images");
    path.resize(length);
    return std::filesystem::path(path).parent_path()/L"data"/L"ui";
}
}

void Renderer::loadBranding() {
    // ID2D1Bitmapはその時の描画先に所属します。ウィンドウサイズ変更後は新しい描画先で作り直す。
    // 呼び出すのは起動・リサイズ時だけで、通常フレームではこの処理を通りません。
    for(auto& bitmap:brandingBitmaps_)bitmap.Reset();
    brandingBounds_.fill({});
    try {
        ComPtr<IWICImagingFactory> factory;
        checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)),"Brand image decoder");
        const auto folder=imageFolder();
        const std::array<const wchar_t*,3> filenames={L"brand-star.png",L"brand-title.png",L"brand-rocket.png"};
        const std::array<float,3> widths={cfg::IconWidth,cfg::TitleWidth,cfg::RocketWidth};
        for(std::size_t index=0;index<filenames.size();++index) {
            ComPtr<IWICBitmapDecoder> decoder;
            checked(factory->CreateDecoderFromFilename((folder/filenames[index]).c_str(),nullptr,GENERIC_READ,
                WICDecodeMetadataCacheOnLoad,&decoder),"Read branding PNG");
            ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,&frame),"Brand PNG frame");
            UINT width{},height{};checked(frame->GetSize(&width,&height),"Brand PNG size");
            // 不正な画像を読み込んでも、巨大なメモリーを確保しないための上限です。
            if(!width || !height || width>4096 || height>4096)throw std::runtime_error("Brand PNG is too large");
            ComPtr<IWICFormatConverter> rgba;
            checked(factory->CreateFormatConverter(&rgba),"Brand PNG color converter");
            checked(rgba->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,
                0,WICBitmapPaletteTypeCustom),"Brand PNG transparency");
            const UINT stride=width*4;
            std::vector<BYTE> pixels(static_cast<std::size_t>(stride)*height);
            checked(rgba->CopyPixels(nullptr,stride,static_cast<UINT>(pixels.size()),pixels.data()),"Read brand alpha");
            UINT left=width,top=height,right=0,bottom=0;
            for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
                if(!pixels[static_cast<std::size_t>(y)*stride+x*4+3])continue;
                left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
            }
            if(right<=left || bottom<=top)throw std::runtime_error("Brand PNG is fully transparent");
            // 元の座標も保存するので、輪の位置を元PNGの数字で指定できます。
            brandingBounds_[index]={static_cast<float>(left),static_cast<float>(top),static_cast<float>(right),static_cast<float>(bottom)};
            const WICRect crop{static_cast<INT>(left),static_cast<INT>(top),static_cast<INT>(right-left),static_cast<INT>(bottom-top)};
            ComPtr<IWICBitmapClipper> clipper;checked(factory->CreateBitmapClipper(&clipper),"Brand PNG cropper");
            checked(clipper->Initialize(rgba.Get(),&crop),"Remove transparent brand padding");
            // 表示サイズの約2倍だけをGPUへ送る。高DPIではscale_に合わせ、元画像以上には拡大しません。
            const float wantedWidth=std::ceil(widths[index]*std::max(2.f,2*scale_));
            const float ratio=std::min(1.f,wantedWidth/static_cast<float>(crop.Width));
            const UINT smallWidth=std::max(1u,static_cast<UINT>(std::ceil(crop.Width*ratio)));
            const UINT smallHeight=std::max(1u,static_cast<UINT>(std::ceil(crop.Height*ratio)));
            ComPtr<IWICBitmapScaler> scaler;checked(factory->CreateBitmapScaler(&scaler),"Brand PNG scaler");
            checked(scaler->Initialize(clipper.Get(),smallWidth,smallHeight,WICBitmapInterpolationModeFant),"Scale brand PNG");
            const auto properties=D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
            checked(d2d_->CreateBitmapFromWicBitmap(scaler.Get(),&properties,&brandingBitmaps_[index]),"Brand bitmap");
        }
    }catch(const std::exception&) {
        // 画像の削除・破損時はゲームを止めず、下の旧表示へ戻します。配布検証では3枚が揃うことを確認。
        for(auto& bitmap:brandingBitmaps_)bitmap.Reset();
        brandingBounds_.fill({});
    }
}

void Renderer::drawBranding(const Frame& f) {
    // 押したときだけ星の組を1周させる。smoothstepで回り始め・止まり際をなめらかにします。
    // タイトルはこの回転の外へ描くので、文字まで回って読みにくくなりません。
    const float spinProgress=std::clamp(std::isfinite(f.logoSpinProgress)?f.logoSpinProgress:0.f,0.f,1.f);
    const float spin=360*spinProgress*spinProgress*(3-2*spinProgress);
    if(!brandingBitmaps_[0] || !brandingBitmaps_[1] || !brandingBitmaps_[2]) {
        star(40,40,18,D2D1::ColorF(.55f,.96f,.82f),false,spin);
        text(L"STAR'S RNG",70,22,225,38,27,D2D1::ColorF(.91f,.95f,.98f),true);
        return;
    }
    const auto& starBounds=brandingBounds_[0];
    const auto& titleBounds=brandingBounds_[1];
    const auto& rocketBounds=brandingBounds_[2];
    const float sourceScale=cfg::IconWidth/(starBounds.right-starBounds.left);
    const float iconHeight=(starBounds.bottom-starBounds.top)*sourceScale;
    const float titleHeight=cfg::TitleWidth*(titleBounds.bottom-titleBounds.top)/(titleBounds.right-titleBounds.left);
    const float rocketHeight=cfg::RocketWidth*(rocketBounds.bottom-rocketBounds.top)/(rocketBounds.right-rocketBounds.left);
    const double seconds=std::isfinite(f.time)?std::max(0.,static_cast<double>(f.time)):0.;
    const double phase=std::fmod(seconds,static_cast<double>(cfg::OrbitSeconds))/cfg::OrbitSeconds*2*Pi+cfg::OrbitPhase;
    const double tilt=cfg::RingTiltDegrees*Pi/180,cosTilt=std::cos(tilt),sinTilt=std::sin(tilt);
    const double u=std::cos(phase),v=std::sin(phase);
    // 楕円を傾け、元の星PNGの座標から画面上の座標へ写します。
    const double sx=cfg::RingCenterX+cfg::RingMajorRadius*u*cosTilt-cfg::RingMinorRadius*v*sinTilt;
    const double sy=cfg::RingCenterY+cfg::RingMajorRadius*u*sinTilt+cfg::RingMinorRadius*v*cosTilt;
    const float x=cfg::IconLeft+static_cast<float>(sx-starBounds.left)*sourceScale;
    const float y=cfg::IconTop+static_cast<float>(sy-starBounds.top)*sourceScale;
    // 軌道の接線＝進行方向。元のロケットは機首が右なので、この角度をそのまま使えます。
    const double dx=-cfg::RingMajorRadius*v*cosTilt-cfg::RingMinorRadius*u*sinTilt;
    const double dy=-cfg::RingMajorRadius*v*sinTilt+cfg::RingMinorRadius*u*cosTilt;
    const float angle=static_cast<float>(std::atan2(dy,dx)*180/Pi)+cfg::RocketAngleOffset;
    D2D1_MATRIX_3X2_F original;d2d_->GetTransform(&original);
    const D2D1_POINT_2F iconCenter{cfg::IconLeft+cfg::IconWidth*.5f,cfg::IconTop+iconHeight*.5f};
    d2d_->SetTransform(D2D1::Matrix3x2F::Rotation(spin,iconCenter)*original);
    auto rocket=[&] {
        D2D1_MATRIX_3X2_F previous;d2d_->GetTransform(&previous);
        d2d_->SetTransform(D2D1::Matrix3x2F::Rotation(angle,{x,y})*previous);
        d2d_->DrawBitmap(brandingBitmaps_[2].Get(),D2D1::RectF(x-cfg::RocketWidth*.5f,y-rocketHeight*.5f,
            x+cfg::RocketWidth*.5f,y+rocketHeight*.5f),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        d2d_->SetTransform(previous);
    };
    // 上側の半周は先に描くと、星の不透明部分がロケットを自然に隠します。
    // 下側の半周は最後に描き、白い輪の前側を飛んでいるように見せます。
    if(v<0)rocket();
    d2d_->DrawBitmap(brandingBitmaps_[0].Get(),D2D1::RectF(cfg::IconLeft,cfg::IconTop,
        cfg::IconLeft+cfg::IconWidth,cfg::IconTop+iconHeight),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if(v>=0)rocket();
    d2d_->SetTransform(original);
    d2d_->DrawBitmap(brandingBitmaps_[1].Get(),D2D1::RectF(cfg::TitleLeft,cfg::TitleTop,
        cfg::TitleLeft+cfg::TitleWidth,cfg::TitleTop+titleHeight),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
} // namespace aura
