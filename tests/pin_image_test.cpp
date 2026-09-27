#include "pin/image.h"
#include "capture/cursor.h"
#include <iostream>
using namespace lumashot;
int main(){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures=0;auto expect=[&](bool ok,const char* s){std::cout<<(ok?"PASS ":"FAIL ")<<s<<'\n';if(!ok)++failures;};
    try {
        auto raw=MakeFrame({-100,-80,220,160});for(int y=0;y<raw.Height();++y)for(int x=0;x<raw.Width();++x)raw.pixels[static_cast<size_t>(y)*raw.Width()+x]=(x+y)%2?0xff101010:0xffeeeeee;
        auto cursor=FrozenCursor::Copy(LoadCursorW(nullptr,IDC_HAND),{-85,-60});RECT clipped{};const RECT cb=cursor.Bounds();IntersectRect(&clipped,&raw.bounds,&cb);auto patch=Crop(raw,clipped);auto frozen=raw;cursor.Composite(frozen);
        Document doc;auto clean=PinOcrImage(frozen,&patch,doc,raw.bounds);expect(clean.pixels==raw.pixels,"OCR restores sampled cursor background at negative coordinates");
        expect(frozen.pixels!=raw.pixels,"display still contains sampled cursor");
        Mark mosaic;mosaic.tool=Tool::Mosaic;mosaic.a={-95,-70};mosaic.b={-40,-20};mosaic.points={mosaic.a,mosaic.b};mosaic.width=48;mosaic.mosaic_cell=16;doc.Add(mosaic);
        Renderer renderer;auto expected=renderer.Flatten(raw,doc,raw.bounds);auto actual=PinOcrImage(frozen,&patch,doc,raw.bounds);
        expect(actual.pixels==expected.pixels,"redaction is applied after cursor restoration; raw source never replaces mosaic");
        const RECT selection{-80,-55,80,80};auto partial=PinOcrImage(frozen,&patch,doc,selection);Renderer r;auto wanted=r.Flatten(Crop(raw,selection),doc,selection);
        expect(partial.pixels==wanted.pixels,"partial cursor intersection and cropped redaction are correct");
    }catch(const std::exception& e){std::cout<<"FAIL "<<e.what()<<'\n';++failures;}
    CoUninitialize();return failures?1:0;
}
