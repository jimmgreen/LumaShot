#include "ocr/availability.h"
#include "ui/pin_menu.h"
#include <fstream>
#include <iostream>

using namespace lumashot;
int main() {
    int failures=0;
    const auto expect=[&](bool ok,const char* message){std::cout<<(ok?"PASS ":"FAIL ")<<message<<'\n';failures+=!ok;};
    const auto root=std::filesystem::temp_directory_path()/(L"LumaShot-ocr-option-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root/L"ocr");
    expect(!ocr::Available(root),"empty installation has no OCR capability");
    for(const auto* asset:ocr::RequiredAssets)std::ofstream(root/asset,std::ios::binary)<<"fixture";
    expect(ocr::Available(root),"complete nonempty asset set enables OCR");
    for(const auto* asset:ocr::RequiredAssets){
        std::filesystem::remove(root/asset);
        expect(!ocr::Available(root),"each missing OCR asset disables OCR");
        {std::ofstream empty(root/asset,std::ios::binary);}
        expect(!ocr::Available(root),"empty OCR asset disables OCR");
        std::ofstream(root/asset,std::ios::binary)<<"fixture";
    }
    for(float scale:{1.f,1.5f,2.f}){
        auto layout=PlaceToolbar({100,100,700,400},{0,0,1920,1080},scale);
        for(bool available:{false,true}){
            layout.ocr_available=available;bool hasOcr=false;
            for(const auto& control:layout.Controls(Tool::Select))if(control.id>=0){
                hasOcr|=control.id==15;
                const auto b=control.bounds;
                expect(layout.Hit({(b.left+b.right)/2,(b.top+b.bottom)/2},Tool::Select)==control.id,"toolbar actions have distinct clickable geometry");
            }
            expect(hasOcr==available,"OCR toolbar button follows installation capability");
        }
    }
    PinMenuModel menu;menu.ocr_available=false;
    expect(!menu.Visible(7)&&menu.Next(6,1)==8&&menu.Next(8,-1)==6,"missing OCR removes recognition from menu navigation");
    expect(menu.Hit(40,menu.RowTop(8)+21)==8,"close row moves into removed OCR row");
    expect(menu.Height()==334.f,"menu closes unused OCR space");
    for(const auto* asset:ocr::RequiredAssets)std::filesystem::remove(root/asset);
    std::filesystem::remove(root/L"ocr");std::filesystem::remove(root);
    return failures?1:0;
}
