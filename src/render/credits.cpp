#include "render/renderer.hpp"
#include "core/credits.hpp"
#include <wincodec.h>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <vector>

// クレジット専用。PNGと文字の形は最初に準備し、表示中は透明度と位置だけを変えます。
// 元の参考レイアウトは1336×688。中の座標を同じ比率で縮め、狭い画面にも収めます。
namespace aura {
namespace {
namespace cfg=tuning::credits;

std::filesystem::path creditsFolder() {
    std::wstring executable(32768,L'\0');
    const DWORD size=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if(!size || size==executable.size())throw std::runtime_error("Cannot locate credits images");
    executable.resize(size);
    return std::filesystem::path(executable).parent_path()/L"data"/L"ui";
}

// DirectWriteの字形を、Direct2Dで縁取りできる「線の形」に変換する小さな橋渡しです。
// TextLayoutを通すので、日本語の代替フォントと文字間隔もDirectWriteに任せられます。
class OutlineCollector final:public IDWriteTextRenderer {
public:
    explicit OutlineCollector(ID2D1Factory* factory):factory_(factory) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output)return E_POINTER;
        *output=nullptr;
        if(iid==__uuidof(IUnknown) || iid==__uuidof(IDWriteTextRenderer))
            *output=static_cast<IDWriteTextRenderer*>(this);
        else if(iid==__uuidof(IDWritePixelSnapping))*output=static_cast<IDWritePixelSnapping*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {const auto count=--references_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL* disabled) override {*disabled=TRUE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX* matrix) override {*matrix={1,0,0,1,0,0};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT* value) override {*value=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT x,FLOAT y,DWRITE_MEASURING_MODE,
        const DWRITE_GLYPH_RUN* run,const DWRITE_GLYPH_RUN_DESCRIPTION*,IUnknown*) override {
        try {
            ComPtr<ID2D1PathGeometry> path;
            HRESULT result=factory_->CreatePathGeometry(&path);if(FAILED(result))return result;
            ComPtr<ID2D1GeometrySink> sink;
            result=path->Open(&sink);if(FAILED(result))return result;
            result=run->fontFace->GetGlyphRunOutline(run->fontEmSize,run->glyphIndices,run->glyphAdvances,
                run->glyphOffsets,run->glyphCount,run->isSideways,(run->bidiLevel&1)!=0,sink.Get());
            if(FAILED(result))return result;
            result=sink->Close();if(FAILED(result))return result;
            ComPtr<ID2D1TransformedGeometry> moved;
            result=factory_->CreateTransformedGeometry(path.Get(),D2D1::Matrix3x2F::Translation(x,y),&moved);
            if(FAILED(result))return result;
            runs.push_back(moved);return S_OK;
        } catch(...) {return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*,FLOAT,FLOAT,IDWriteInlineObject*,BOOL,BOOL,IUnknown*) override {return S_OK;}
    std::vector<ComPtr<ID2D1Geometry>> runs;
private:
    std::atomic<ULONG> references_{1};
    ID2D1Factory* factory_;
};

ComPtr<ID2D1Geometry> lettering(IDWriteFactory* write,ID2D1Factory* factory,const wchar_t* value,
    const wchar_t* family,float size,bool italic,D2D1_RECT_F& bounds) {
    ComPtr<IDWriteTextFormat> format;
    checked(write->CreateTextFormat(family,nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        italic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"ja-jp",&format),
        "Credits text format");
    checked(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),"Credits text wrapping");
    ComPtr<IDWriteTextLayout> layout;
    checked(write->CreateTextLayout(value,static_cast<UINT32>(wcslen(value)),format.Get(),cfg::ReferenceWidth,200,&layout),
        "Credits text layout");
    ComPtr<OutlineCollector> collector;collector.Attach(new OutlineCollector(factory));
    checked(layout->Draw(nullptr,collector.Get(),0,0),"Credits letter outlines");
    std::vector<ID2D1Geometry*> geometries;
    for(const auto& run:collector->runs)geometries.push_back(run.Get());
    if(geometries.empty())throw std::runtime_error("Credits font has no glyphs");
    ComPtr<ID2D1GeometryGroup> group;
    checked(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING,geometries.data(),static_cast<UINT32>(geometries.size()),&group),
        "Credits letter group");
    checked(group->GetBounds(nullptr,&bounds),"Credits letter bounds");
    ComPtr<ID2D1Geometry> result;checked(group.As(&result),"Credits geometry");return result;
}
}

void Renderer::loadCredits() {
    // Bitmapは描画先に所属するため、ウィンドウのリサイズ後にもここを呼びます。
    for(auto& bitmap:creditsBitmaps_)bitmap.Reset();
    try {
        ComPtr<IWICImagingFactory> factory;
        checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Credits decoder");
        const auto folder=creditsFolder();
        const std::array<const wchar_t*,4> files={L"credits-reviewer-zero.png",L"credits-reviewer-nyon.png",
            L"credits-illustrator.png",L"credits-special-thanks.png"};
        for(std::size_t i=0;i<files.size();++i) {
            try {
                ComPtr<IWICBitmapDecoder> decoder;
                checked(factory->CreateDecoderFromFilename((folder/files[i]).c_str(),nullptr,GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad,&decoder),"Read credits PNG");
                ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,&frame),"Credits PNG frame");
                UINT width{},height{};checked(frame->GetSize(&width,&height),"Credits PNG size");
                if(!width || !height || width>4096 || height>4096)throw std::runtime_error("Credits image is too large");
                ComPtr<IWICFormatConverter> converter;
                checked(factory->CreateFormatConverter(&converter),"Credits color converter");
                checked(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,
                    nullptr,0,WICBitmapPaletteTypeCustom),"Credits transparency");
                const auto properties=D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
                checked(d2d_->CreateBitmapFromWicBitmap(converter.Get(),&properties,&creditsBitmaps_[i]),"Credits bitmap");
            } catch(const std::exception&) {
                // 画像が1枚欠けても、他の画像と名前は表示します。
                creditsBitmaps_[i].Reset();
            }
        }
    } catch(const std::exception&) {}
    if(!creditsLettering_[0]) {
        // 字形は解像度に依存しないので一度だけ準備。毎フレームのレイアウト計算はしない。
        const std::array<const wchar_t*,7> words={L"credits",L"Aura Reviewer",L"ぜろくんでんせつ",L"にょ～ん！",
            L"Illustrator",L"にごり(Lv.1)",L"Special Thanks"};
        const std::array<float,7> sizes={84,72,54,54,72,54,56};
        for(std::size_t i=0;i<words.size();++i)
            creditsLettering_[i]=lettering(write_.Get(),d2dFactory_.Get(),words[i],
                i==0?L"Segoe Script":i==2 || i==3 || i==5?L"Yu Gothic":L"Georgia",sizes[i],i==0,creditsLetterBounds_[i]);
    }
}

void Renderer::creditsUi(const Frame& frame) {
    if(frame.creditsTime<0)return;
    const auto panel=credits::layout(logicalW_,logicalH_);
    const float scale=panel.width/cfg::ReferenceWidth;
    // .30fは「30%だけ不透明」。画面全体を暗くする別の黒い背景は重ねません。
    rect(panel.x,panel.y,panel.width,panel.height,D2D1::ColorF(cfg::PanelGray,cfg::PanelGray,cfg::PanelGray,cfg::PanelOpacity));
    D2D1_MATRIX_3X2_F previous;d2d_->GetTransform(&previous);
    auto words=[&](std::size_t index,float x,float y,float alpha,float offset=0,bool centered=false) {
        if(alpha<=0 || !creditsLettering_[index])return;
        const auto bounds=creditsLetterBounds_[index];
        if(centered)x-=(bounds.right-bounds.left)*.5f;
        d2d_->SetTransform(D2D1::Matrix3x2F::Translation(-bounds.left,-bounds.top)*
            D2D1::Matrix3x2F::Scale(scale,scale)*
            D2D1::Matrix3x2F::Translation(panel.x+(x+offset)*scale,panel.y+y*scale)*previous);
        // 灰色の内側＋白い縁で、参考画像の輪郭文字に近づけます。
        brush_->SetColor(D2D1::ColorF(.25f,.25f,.25f,alpha*.75f));
        d2d_->FillGeometry(creditsLettering_[index].Get(),brush_.Get());
        brush_->SetColor(D2D1::ColorF(1,1,1,alpha));
        d2d_->DrawGeometry(creditsLettering_[index].Get(),brush_.Get(),cfg::OutlineWidth,roundStroke_.Get());
        d2d_->SetTransform(previous);
    };
    auto image=[&](std::size_t index,float x,float y,float width,float height,float alpha,float offset) {
        if(alpha<=0)return;
        const float left=panel.x+(x+offset)*scale,top=panel.y+y*scale;
        if(creditsBitmaps_[index]) {
            // 元画像の縦横比を維持。透明なピクセルもそのまま描く。
            const auto size=creditsBitmaps_[index]->GetSize();
            const float fit=std::min(width/size.width,height/size.height);
            const float w=size.width*fit,h=size.height*fit;
            d2d_->DrawBitmap(creditsBitmaps_[index].Get(),D2D1::RectF(left+(width-w)*scale*.5f,top+(height-h)*scale*.5f,
                left+(width+w)*scale*.5f,top+(height+h)*scale*.5f),alpha,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
            stroke(left,top,width*scale,height*scale,D2D1::ColorF(1,1,1,alpha*.65f));
            label("?",left,top+height*scale*.25f,width*scale,height*scale*.5f,26*scale,D2D1::ColorF(1,1,1,alpha),false,true);
        }
    };
    // 見出しだけはアニメーションに入れず、開いた瞬間から同じ位置で表示。
    words(0,cfg::ReferenceWidth*.5f,32,1,0,true);
    // 上から下へ左列を読んでから、右上のSpecial Thanksへ進む7組です。
    const auto a=credits::appearance(frame.creditsTime,0);words(1,24,139,a.alpha,a.offsetX);
    const auto b=credits::appearance(frame.creditsTime,1);image(0,24,203,112,112,b.alpha,b.offsetX);words(2,172,257,b.alpha,b.offsetX);
    const auto c=credits::appearance(frame.creditsTime,2);image(1,24,336,112,112,c.alpha,c.offsetX);words(3,172,382,c.alpha,c.offsetX);
    const auto d=credits::appearance(frame.creditsTime,3);words(4,24,471,d.alpha,d.offsetX);
    const auto e=credits::appearance(frame.creditsTime,4);image(2,24,559,112,112,e.alpha,e.offsetX);words(5,176,603,e.alpha,e.offsetX);
    const auto f=credits::appearance(frame.creditsTime,5);words(6,735,151,f.alpha,f.offsetX);
    const auto g=credits::appearance(frame.creditsTime,6);image(3,715,214,220,220,g.alpha,g.offsetX);

    const auto close=credits::closeLayout(logicalW_,logicalH_);
    const bool hover=close.contains(frame.mouseX/scale_,frame.mouseY/scale_);
    if(hover)rect(close.x,close.y,close.width,close.height,D2D1::ColorF(1,1,1,.16f));
    brush_->SetColor(D2D1::ColorF(1,1,1,.9f));
    const float inset=10;
    d2d_->DrawLine({close.x+inset,close.y+inset},{close.x+close.width-inset,close.y+close.height-inset},brush_.Get(),1.6f,roundStroke_.Get());
    d2d_->DrawLine({close.x+close.width-inset,close.y+inset},{close.x+inset,close.y+close.height-inset},brush_.Get(),1.6f,roundStroke_.Get());
    label("Esc",panel.x+panel.width-56,panel.y+panel.height-28,42,18,11,D2D1::ColorF(1,1,1,.65f),false,true);
}
} // namespace aura
