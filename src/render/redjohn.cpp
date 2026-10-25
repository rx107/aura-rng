#include "render/renderer.hpp"
#include "tuning/redjohn.hpp"
#include <algorithm>
#include <cmath>

// 読む順番: ①部屋 → ②ベッド → ③赤い顔 → ④窓と埃。
// 写真・人物・動画の切り抜きは使わず、共通の小さな立方体と光の粒だけで構成します。
// DirectX の準備を追加しないので、ほかの軽いオーラと同じ描画バッチに収まります。
namespace aura {
using namespace DirectX;
namespace {
namespace settings=tuning::redjohn;
constexpr float Pi=3.14159265359f;
Color color(const std::array<float,3>& value) {return {value[0],value[1],value[2]};}
Color tint(Color c,float factor) {return {c.r*factor,c.g*factor,c.b*factor};}
}

void Renderer::buildRedJohn(const Frame& f) {
    // 部品は「アバターから何メートル離すか」で置き、最後に体の向きと位置をまとめて適用。
    // そのため歩いて移動しても、部屋だけが置き去りになりません。
    const auto room=XMMatrixRotationY(f.facing)*XMMatrixTranslation(f.avatarX,0,f.avatarZ);
    const auto first=instances_.size();
    auto solid=[&](float x,float y,float z,float width,float height,float depth,Color c,
                   float tilt=0.f,float selfLight=settings::RoomSelfLight) {
        // ステージと合わせた共通上限 4096 枚を守る。部屋だけで使う上限も 440 個に限定。
        if(instances_.size()>=4096 || instances_.size()-first>=440) return;
        Instance instance{};
        XMStoreFloat4x4(&instance.world,XMMatrixScaling(width,height,depth)*XMMatrixRotationZ(tilt)*
            XMMatrixTranslation(x,y,z)*room);
        instance.color={c.r,c.g,c.b,selfLight};instances_.push_back(instance);
    };
    auto point=[&](float x,float y,float z) {return XMVector3TransformCoord(XMVectorSet(x,y,z,1),room);};
    const Color wall=color(settings::WallColor),carpet=color(settings::CarpetColor);
    const Color trim=color(settings::SkirtingColor),linen=color(settings::LinenColor);
    const float floor=settings::FloorHeight;
    const float width=settings::RoomWidth,back=settings::RoomBack,front=settings::RoomFront;
    const float depth=back-front,wallY=settings::RoomHeight*.5f+floor;
    // カメラを部屋の裏側へ回した場合、外側の壁を透視用に省略します。
    // 屋根と正面の壁も作らず、真上から座りポーズと寝具を確認できるようにします。
    const float relativeYaw=f.cameraYaw+f.facing;
    const float eyeX=std::sin(relativeYaw),eyeZ=-std::cos(relativeYaw);
    const bool showBack=!settings::CutawayWalls || eyeZ<.12f;
    const bool showLeft=!settings::CutawayWalls || eyeX>-.20f;
    const bool showRight=!settings::CutawayWalls || eyeX<.20f;

    // ① 部屋。床を細い帯に分けて濃淡を変えると、平らな灰色の板より柔らかい絨毯に見えます。
    solid(0,floor-.08f,(back+front)*.5f,width+.16f,.16f,depth+.15f,tint(carpet,.75f));
    constexpr unsigned CarpetRows=18;
    for(unsigned row=0;row<CarpetRows;++row) {
        const float z=front+(row+.5f)*depth/CarpetRows;
        const float shade=.91f+.10f*std::sin(row*2.71f)*std::sin(row*2.71f);
        solid(0,floor+.004f,z,width,.014f,depth/CarpetRows+.003f,tint(carpet,shade));
    }
    if(showBack) solid(0,wallY,back,width,settings::RoomHeight,settings::WallThickness,wall);
    // 側壁は短め。座ったアバターの前へ大きな壁を張り出させない構成です。
    if(showLeft) solid(-width*.5f,wallY,back-.66f,settings::WallThickness,settings::RoomHeight,1.44f,tint(wall,.92f));
    if(showRight) solid(width*.5f,wallY,back-.66f,settings::WallThickness,settings::RoomHeight,1.44f,tint(wall,1.02f));
    solid(0,floor+.10f,back-.075f,width,.20f,.10f,trim);
    solid(-width*.5f+.05f,floor+.10f,back-.66f,.09f,.20f,1.43f,trim);
    solid(width*.5f-.05f,floor+.10f,back-.66f,.09f,.20f,1.43f,trim);

    // ② 脚付きのベッドではなく、参考のような薄いマットレスを床近くへ置きます。
    // z 方向に切った帯の端を丸めるため、横・上どちらから見ても輪郭がなだらかです。
    const float mattressY=settings::BedTop-settings::BedThickness*.5f;
    const float bedX=settings::BedCenterX,bedZ=settings::BedCenterZ;
    // 下へ薄い布の層を重ねて床までつなぐ。脚は作らず、浮いたベッドに見える隙間を塞ぎます。
    const float beddingBottom=floor+.012f,beddingTop=settings::BedTop-settings::BedThickness+.035f;
    solid(bedX,(beddingBottom+beddingTop)*.5f,bedZ,settings::BedWidth-.17f,beddingTop-beddingBottom,
        settings::BedDepth-.11f,tint(linen,.74f));
    for(unsigned row=0;row<settings::MattressSlices;++row) {
        const float u=(row+.5f)/settings::MattressSlices,z=(u-.5f)*settings::BedDepth;
        const float corner=.19f;
        const float end=std::max(0.f,std::abs(z)-(settings::BedDepth*.5f-corner));
        const float round=corner-std::sqrt(std::max(0.f,corner*corner-end*end));
        const float shade=.97f+.035f*std::sin(row*.76f);
        solid(bedX,mattressY,bedZ+z,settings::BedWidth-round*2,settings::BedThickness,
            settings::BedDepth/settings::MattressSlices+.007f,tint(linen,shade));
    }
    // 上面の細い布のしわ。厚い発光線を使わず、わずかな色差だけで布を読ませます。
    for(unsigned i=0;i<16;++i) {
        const float x=-1.93f+i*.257f;
        const float z=bedZ-.57f+.075f*std::sin(i*1.93f);
        solid(bedX+x,settings::BedTop+.006f,z,.015f,.009f,.34f+.11f*std::sin(i*2.61f),
            tint(linen,i%3?.88f:1.06f),.08f*std::sin(static_cast<float>(i)));
    }
    solid(bedX,settings::BedTop-.055f,bedZ-settings::BedDepth*.5f-.008f,settings::BedWidth-.18f,.018f,.012f,tint(linen,.77f));
    // 枕も輪郭を帯で丸くする。写真の人物の持ち物は置かず、アバターのポーズを主役にします。
    for(unsigned row=0;row<settings::PillowSlices;++row) {
        const float u=(row+.5f)/settings::PillowSlices;
        const float contour=std::sqrt(std::max(0.f,1-std::pow((u-.5f)*2,2.f)));
        const float h=settings::PillowHeight*(.45f+.55f*contour);
        solid(settings::PillowX,settings::BedTop+h*.5f,settings::PillowZ+(u-.5f)*settings::PillowDepth,
            settings::PillowWidth*(.76f+.24f*contour),h,settings::PillowDepth/settings::PillowSlices+.006f,
            tint(linen,1.02f+.035f*contour),-.045f);
    }

    // ③ 壁の顔。壁より少し手前に描き、同じ面のちらつき（Z-fighting）を避けます。
    // 細い立方体を線分としてつなぐだけなので、画像ファイルや専用テクスチャは不要です。
    const float fx=settings::FaceCenterX,fy=settings::FaceCenterY,r=settings::FaceRadius;
    const float paintZ=back-settings::WallThickness*.5f-.014f;
    const Color paint=color(settings::PaintColor);
    auto stroke=[&](float ax,float ay,float bx,float by,float thickness,float shade=1.f) {
        const float dx=bx-ax,dy=by-ay,length=std::hypot(dx,dy);
        solid(fx+(ax+bx)*.5f*r,fy+(ay+by)*.5f*r,paintZ,length*r+thickness*.3f,
            thickness,.014f,tint(paint,shade),std::atan2(dy,dx),settings::PaintSelfLight);
    };
    // 円の頂上に小さい切れ目を作り、筆の始点と終点を残します。
    for(unsigned i=0;i<settings::FaceSegments;++i) {
        const float a=1.77f+i*(2*Pi-.22f)/settings::FaceSegments;
        const float b=1.77f+(i+1)*(2*Pi-.22f)/settings::FaceSegments;
        auto ring=[&](float angle) {
            const float wobble=1+.013f*std::sin(angle*5+.6f)+.008f*std::sin(angle*11);
            return XMFLOAT2{std::cos(angle)*wobble,.99f*std::sin(angle)*wobble};
        };
        const auto p=ring(a),q=ring(b);
        stroke(p.x,p.y,q.x,q.y,settings::PaintWidth*(.82f+.18f*std::sin(a*3)*std::sin(a*3)),.91f+.09f*std::sin(a*7)*std::sin(a*7));
    }
    // 目は円ではなく斜めの刷毛跡。幅を変えた短い線を重ね、手で塗った形を作ります。
    for(unsigned eye=0;eye<2;++eye) for(unsigned row=0;row<12;++row) {
        const float sign=eye?1.f:-1.f,u=(row+.5f)/12.f;
        const float x=sign*.34f+(u-.5f)*.37f;
        const float y=.26f-sign*(u-.5f)*.14f;
        const float h=.14f*std::sqrt(std::max(0.f,1-std::pow((u-.5f)*2,2.f)));
        stroke(x,y-h*.5f,x,y+h*.5f,.036f,.94f+.06f*std::sin(row*3.f));
    }
    // 笑う口は両端が上がった U 字。中央を厚くして、顔の一番読みやすい特徴にします。
    for(unsigned i=0;i<31;++i) {
        const float a=-.52f+i*(1.04f/31),b=-.52f+(i+1)*(1.04f/31);
        const float ya=-.51f+.87f*a*a,yb=-.51f+.87f*b*b;
        const float thickness=settings::PaintWidth*(1.15f+.48f*(1-std::abs(a)/.52f));
        stroke(a,ya,b,yb,thickness);
    }
    // 乾いた塗料の垂れ。赤い飛沫を大量に飛ばさず、静かな室内の目印として控えめにします。
    for(unsigned i=0;i<17 && settings::PaintDripScale>0;++i) {
        const bool eye=i<10;
        const float x=eye?((i<5?-.34f:.34f)+(static_cast<int>(i%5)-2)*.052f):-.72f+(i-10)*.22f;
        const float y=eye?.205f:(-.51f+.87f*x*x-.03f);
        const float length=(.08f+.20f*(.5f+.5f*std::sin(i*2.73f)))*settings::PaintDripScale;
        stroke(x,y,x+.006f*std::sin(i*4.f),y-length,.007f+(i%3)*.0035f,.90f);
    }

    // ④ 右側の明るい窓と、窓枠の影。白い光を強くぼかさず、薄いクリーム色で昼を表現。
    if(showBack) {
        const float wx=settings::WindowX,wy=settings::WindowY;
        solid(wx,wy,paintZ+.003f,settings::WindowWidth+.18f,settings::WindowHeight+.18f,.022f,tint(wall,.77f));
        solid(wx,wy,paintZ-.015f,settings::WindowWidth,settings::WindowHeight,.028f,{.91f,.94f,.91f},0,.62f);
        solid(wx,wy,paintZ-.037f,.045f,settings::WindowHeight,.015f,{.75f,.78f,.74f},0,.55f);
        solid(wx,wy-.17f,paintZ-.039f,settings::WindowWidth,.035f,.015f,{.75f,.78f,.74f},0,.55f);
        solid(wx,wy-settings::WindowHeight*.5f-.04f,paintZ-.10f,settings::WindowWidth+.24f,.09f,.22f,linen);
        // 光の筋は赤い顔の右へ限定。顔の赤を白く塗りつぶさないための配置です。
        for(unsigned i=0;i<5;++i) solid(.91f+i*.205f,3.14f-i*.035f,paintZ+.003f,
            .105f,1.77f,.008f,{.79f,.80f,.73f},-.17f,.48f);
    }
    for(unsigned i=0;i<settings::DustCount;++i) {
        const float phase=std::fmod(i*.327f+f.time*settings::DustSpeed,1.f);
        const float x=1.08f+.92f*std::sin(i*3.13f+f.time*.055f);
        const float y=1.14f+phase*2.55f,z=.32f+.60f*std::sin(i*2.47f);
        const float fade=std::sin(phase*Pi),size=.010f+(i%3)*.004f;
        const auto p=point(x,y,z);
        pixelBillboard(XMVectorGetX(p),XMVectorGetY(p),XMVectorGetZ(p),size,size,
            {.83f,.80f,.67f},0,fade*settings::DustBrightness,f);
    }
}
} // namespace aura
