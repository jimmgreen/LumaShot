#include "clipboard_preview_probe.h"
#include "clipboard/preview_cache.h"
#include "export/png.h"
#include <psapi.h>
#include <iostream>
#include <vector>
#include <cstring>

using namespace lumashot;
using namespace lumashot::clipboard;
static void Expect(bool condition, const char* message) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << message << std::endl;
    if (!condition) std::exit(1);
}
static size_t PrivateCommit() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return counters.PrivateUsage;
}
static Entry Image(UINT format) {
    Entry e;e.id = 202;e.kind = Kind::Image;
    BITMAPV5HEADER h{};h.bV5Size = format == CF_DIBV5 ? sizeof(h) : sizeof(BITMAPINFOHEADER);
    h.bV5Width = 2048;h.bV5Height = -1536;h.bV5Planes = 1;h.bV5BitCount = 32;h.bV5Compression = BI_RGB;
    std::vector<unsigned char> bytes(h.bV5Size + 2048 * 1536 * 4);
    std::memcpy(bytes.data(), &h, h.bV5Size);
    const uint32_t color = 0xff4080a0;
    for (size_t i = h.bV5Size; i < bytes.size(); i += 4) std::memcpy(bytes.data() + i, &color, 4);
    e.formats.push_back({format, std::move(bytes)});return e;
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        PreviewWindow p;
        Entry text;text.id = 101;text.kind = Kind::Text;text.text = L"Synthetic preview 文本";
        const RECT panel{100,100,580,844};
        auto image = Image(CF_DIB);
        auto data = PreparePreview(image);
        Expect(data.image && data.image->Width() == 640 && data.image->Height() == 480, "DIB preview decodes to a bounded 640px image");
        auto fallback = Image(CF_DIB);
        fallback.formats.insert(fallback.formats.begin(),{CF_DIB,{1,2,3}});
        Expect(PreparePreview(fallback).image != nullptr,"image decode falls back to a valid clipboard representation");
        const auto v5 = PreparePreview(Image(CF_DIBV5));
        Expect(v5.image && v5.error.empty(), "DIBV5 preview decodes successfully");
        Entry broken;broken.kind = Kind::Image;broken.formats.push_back({CF_DIB,{1,2,3}});
        Expect(!PreparePreview(broken).error.empty(), "invalid image produces a terminal error, never endless loading");
        p.Show(nullptr, image, panel, true, 1);
        Expect(PreviewWindowTest::Loading(p) && !PreviewWindowTest::HasImage(p), "window initially displays loading without decoding in paint");
        p.SetContent(image.id, data);
        Expect(PreviewWindowTest::RenderedImage(p), "decoded image reaches actual rendered center pixel");
        const auto window = PreviewWindowTest::Window(p);
        Expect((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED)==0 && SendMessageW(window,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,
               "preview never activates or steals panel/search focus");
        SetWindowPos(window,nullptr,0,0,600,500,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Expect(PreviewWindowTest::RenderedImage(p), "resize recreates the render-target bitmap from retained bounded pixels");
        p.Show(nullptr,text,panel,false,1);
        p.SetContent(image.id,data);
        Expect(PreviewWindowTest::Loading(p) && !PreviewWindowTest::HasImage(p), "late image cannot overwrite another entry");
        p.SetContent(text.id,PreparePreview(text));
        Expect(PreviewWindowTest::Text(p)==text.text, "text content reaches preview");
        Expect(SendMessageW(window,WM_MOUSEACTIVATE,0,0)==MA_ACTIVATE,"text preview can receive focus for selection");
        PreviewWindowTest::Snapshot(p);
        const auto start=PreviewWindowTest::Position(p,0),end=PreviewWindowTest::Position(p,9);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
        SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
        Expect(p.IsOpen()&&PreviewWindowTest::Selection(p)==L"Synthetic","drag selects exact characters without dismissing preview");
        Entry code=text;code.text=L"const message = \"中文😀\";\nreturn message;";
        p.Show(nullptr,code,panel,false,1,PreparePreview(code));PreviewWindowTest::Snapshot(p);
        const auto cs=PreviewWindowTest::Position(p,17),ce=PreviewWindowTest::Position(p,21);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(cs.x,cs.y));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(ce.x,ce.y));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(ce.x,ce.y));
        Expect(PreviewWindowTest::Selection(p)==code.text.substr(17,4),"code selection preserves Chinese and surrogate pairs");
        BYTE saved_keys[256]{},control_keys[256]{};GetKeyboardState(saved_keys);memcpy(control_keys,saved_keys,sizeof(saved_keys));control_keys[VK_CONTROL]=0x80;SetKeyboardState(control_keys);SendMessageW(window,WM_KEYDOWN,'A',0);SetKeyboardState(saved_keys);
        Expect(PreviewWindowTest::Selection(p)==code.text,"Ctrl+A selects complete code including newline");
        Entry long_text=text;long_text.text.clear();for(int line=0;line<100;++line)long_text.text+=L"Selectable line 中文 0123456789\n";
        p.Show(nullptr,long_text,panel,false,1,PreparePreview(long_text));PreviewWindowTest::Snapshot(p);Expect(PreviewWindowTest::Text(p).size()>2048&&PreviewWindowTest::ScrollMax(p)>0,"long text is retained and scrollable beyond former limit");
        SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);PreviewWindowTest::Snapshot(p);
        const UINT32 row=static_cast<UINT32>(long_text.text.find(L'\n')+1),begin=row*5,finish=row*6+5;
        const auto ls=PreviewWindowTest::Position(p,begin),le=PreviewWindowTest::Position(p,finish);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(ls.x,ls.y));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(le.x,le.y));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(le.x,le.y));
        Expect(PreviewWindowTest::Selection(p)==long_text.text.substr(begin,finish-begin),"scrolled multiline drag copies exact character range");
        Entry highlighted=text;highlighted.text=L"namespace lumashot {\n    // 中文注释 😀\n    const char* value = \"hello\";\n    return nullptr;\n}";
        p.Show(nullptr,highlighted,panel,false,1,PreparePreview(highlighted));Expect(PreviewWindowTest::WaitHighlight(p),"background highlighting completes");
        Expect(PreviewWindowTest::Language(p)==CodeLanguage::Cpp,"preview receives detected language");
        auto highlighted_frame=PreviewWindowTest::Snapshot(p);SavePng(highlighted_frame,L"build/clipboard-code-light.png");
        const auto has_colour=[&](uint32_t colour){return std::count_if(highlighted_frame.pixels.begin(),highlighted_frame.pixels.end(),[&](uint32_t value){return std::abs(int((value>>16)&255)-int((colour>>16)&255))<40&&std::abs(int((value>>8)&255)-int((colour>>8)&255))<40&&std::abs(int(value&255)-int(colour&255))<40;})>2;};
        Expect(has_colour(0x7131aa)&&has_colour(0x34734a)&&has_colour(0x9e3c18),"actual LumaText pixels contain distinct keyword comment and string colours");
        const auto hs=PreviewWindowTest::Position(p,0),he=PreviewWindowTest::Position(p,9);SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(hs.x,hs.y));SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(he.x,he.y));SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(he.x,he.y));
        Expect(PreviewWindowTest::Selection(p)==L"namespace","highlighting preserves exact character selection");
        SavePng(PreviewWindowTest::Snapshot(p),L"build/clipboard-code-selected.png");
        p.Show(nullptr,highlighted,panel,true,1.5f,PreparePreview(highlighted));Expect(PreviewWindowTest::WaitHighlight(p),"dark high-DPI highlight completes");SavePng(PreviewWindowTest::Snapshot(p),L"build/clipboard-code-dark-150.png");
        RECT language_rect{};GetClientRect(window,&language_rect);const int menu_x=language_rect.right-static_cast<int>(120*1.5f);
        SendMessageW(window,WM_LBUTTONDOWN,0,MAKELPARAM(menu_x,30));SendMessageW(window,WM_LBUTTONDOWN,0,MAKELPARAM(menu_x,static_cast<int>(85*1.5f)));
        Expect(PreviewWindowTest::WaitHighlight(p)&&PreviewWindowTest::Language(p)==CodeLanguage::Plain,"language menu can explicitly disable highlighting");
        Entry csharp_entry=highlighted;csharp_entry.id=880;csharp_entry.text=L"using System;\n\npublic class Example {\n    // C# 预览\n    public string Name { get; set; } = @\"中文路径\";\n    public int Count => 42;\n}";
        p.Show(nullptr,csharp_entry,panel,false,1,PreparePreview(csharp_entry));Expect(PreviewWindowTest::WaitHighlight(p)&&PreviewWindowTest::Language(p)==CodeLanguage::CSharp,"C# reaches actual preview renderer");SavePng(PreviewWindowTest::Snapshot(p),L"build/clipboard-code-csharp.png");
        Entry md=highlighted;md.id=881;md.text=L"# 项目说明\n\n**重点内容** 与 [项目链接](https://example.com)\n\n```cpp\nreturn 42;\n```\n";
        p.Show(nullptr,md,panel,false,1,PreparePreview(md));Expect(PreviewWindowTest::WaitHighlight(p)&&PreviewWindowTest::Language(p)==CodeLanguage::Markdown,"Markdown reaches actual preview renderer");SavePng(PreviewWindowTest::Snapshot(p),L"build/clipboard-code-markdown.png");
        p.Show(nullptr,text,panel,false,1,PreparePreview(text));
        RECT fixed{};GetWindowRect(window,&fixed);
        for(const auto dimensions:{SIZE{200,600},SIZE{640,160},SIZE{100,100}}){PreviewData fixture;fixture.image=std::make_shared<Frame>(MakeFrame({0,0,dimensions.cx,dimensions.cy},0xff4080a0));p.Show(nullptr,image,panel,false,1,fixture);RECT resized{};GetWindowRect(window,&resized);Expect(dimensions.cx==dimensions.cy||(dimensions.cx<dimensions.cy?resized.bottom-resized.top>resized.right-resized.left:resized.right-resized.left>resized.bottom-resized.top),"image window follows source aspect ratio");Expect(PreviewWindowTest::RenderedImage(p),"adaptive image stays centered and undistorted");}
        p.Show(nullptr,text,panel,false,1,PreparePreview(text));RECT restored{};GetWindowRect(window,&restored);Expect(restored.right-restored.left==fixed.right-fixed.left&&restored.bottom-restored.top==fixed.bottom-fixed.top,"text restores fixed dimensions after image preview");
        p.Close();
        p.Show(nullptr,image,panel,false,1,data);
        Expect(PreviewWindowTest::RenderedImage(p), "cached image paints without a loading frame");
        const auto warm_window=PreviewWindowTest::Window(p);
        const auto warm_renderer=PreviewWindowTest::Renderer(p);
        p.Hide();
        Expect(!p.IsOpen() && IsWindow(warm_window), "hide retains inactive preview window");
        p.Show(nullptr,image,panel,false,1,data);
        Expect(PreviewWindowTest::RenderedImage(p) && PreviewWindowTest::Window(p)==warm_window &&
            PreviewWindowTest::Renderer(p)==warm_renderer, "reopen reuses window and renderer with correct pixels");
        PreviewCache cache(data.image->pixels.size()*4*2);
        auto second=std::make_shared<Frame>(MakeFrame({0,0,640,480},0xff123456));
        cache.Put(1,data.image);cache.Put(2,second);cache.Find(1);cache.Put(3,second);
        Expect(cache.Find(1) && !cache.Find(2) && cache.Find(3), "cache evicts least recently used preview within byte budget");
        Expect(cache.Bytes()==data.image->pixels.size()*4*2, "cache accounts bounded decoded pixel bytes");
        cache.Put(4,std::make_shared<Frame>(MakeFrame({0,0,2000,2000})));
        Expect(!cache.Find(4), "oversized preview is not cached");
        cache.Clear();Expect(cache.Bytes()==0 && !cache.Find(1), "clear releases cached pixels");
        p.Close();
        Expect(PreviewWindowTest::Released(p), "close destroys all preview resources");
        Entry files;files.id=303;files.kind=Kind::Files;
        for(int i=0;i<256;++i){if(i)files.text+=L"\n";files.text+=L"C:\\Synthetic\\file-"+std::to_wstring(i)+L".txt";}
        auto file_data=PreparePreview(files);
        Expect(file_data.file_count==256&&file_data.text.find(L"file-255.txt")!=std::wstring::npos&&file_data.text.size()<=PreviewData::MaxFileChars,"file preview includes every filename within a bounded list");
        p.Show(nullptr,files,panel,false,1,file_data);PreviewWindowTest::Snapshot(p);Expect(PreviewWindowTest::TextGlyphs(p)>0,"preview text rasterizes through LumaText FreeType");
        Expect(PreviewWindowTest::ScrollMax(p)>0,"long file list exposes scrollable content");
        SendMessageW(PreviewWindowTest::Window(p),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
        Expect(PreviewWindowTest::Scroll(p)>0,"mouse wheel scrolls the file preview");
        RECT preview_rect{};GetWindowRect(PreviewWindowTest::Window(p),&preview_rect);const auto prior_scroll=PreviewWindowTest::Scroll(p);
        Expect(p.ForwardWheel(MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),MAKELPARAM(preview_rect.left+40,preview_rect.top+80))&&PreviewWindowTest::Scroll(p)>prior_scroll,"focused panel can route wheel to inactive file preview");p.Close();
        p.Show(nullptr,image,panel,false,1,data);
        Expect(PreviewWindowTest::RenderedImage(p),"image paints before idle release");
        const auto idle_warm_window=PreviewWindowTest::Window(p);
        const auto idle_warm_renderer=PreviewWindowTest::Renderer(p);
        p.Hide();
        Expect(PreviewWindowTest::Renderer(p)==idle_warm_renderer,"short hide retains drawing resources");
        p.Show(nullptr,image,panel,false,1,data);
        PreviewWindowTest::ExpireIdle(p);
        Expect(p.IsOpen()&&PreviewWindowTest::Window(p)==idle_warm_window&&PreviewWindowTest::Renderer(p)==idle_warm_renderer,"reopen cancels idle release including stale timer messages");
        p.Hide();PreviewWindowTest::ExpireIdle(p);
        Expect(PreviewWindowTest::Released(p),"idle hidden preview releases all resources");
        p.Show(nullptr,image,panel,false,1,data);
        Expect(PreviewWindowTest::RenderedImage(p),"image renders correctly after idle resource rebuild");
        p.Close();
        // A leak grows in every 50-cycle window. One-time allocator/driver caches (~20 MB step at a
        // varying moment, then flat) land in only one window, so judge the smallest window growth.
        decltype(PrivateCommit()) marks[4]{};
        for (int i=0;i<150;++i) {if(i%50==0)marks[i/50]=PrivateCommit();
            p.Show(nullptr,image,panel,i%2!=0,1);p.SetContent(image.id,data);
            Expect(PreviewWindowTest::RenderedImage(p), "open/switch cycle renders real image pixels in Release builds");
            p.Show(nullptr,text,panel,false,1);p.SetContent(text.id,PreparePreview(text));p.Close();
            Expect(PreviewWindowTest::Released(p), "cycle releases image, text and renderer");
        }
        marks[3]=PrivateCommit();long long growth=(long long)marks[1]-(long long)marks[0];
        for(int k=1;k<3;++k){const long long g=(long long)marks[k+1]-(long long)marks[k];growth=g<growth?g:growth;}
        std::cout << "MEMORY at0=" << marks[0] << " at50=" << marks[1] << " at100=" << marks[2] << " at150=" << marks[3] << " min_window_growth=" << growth << std::endl;
        Expect(growth <= 8*1024*1024, "50 cycles retain bounded private memory");
    }
    CoUninitialize();return 0;
}
