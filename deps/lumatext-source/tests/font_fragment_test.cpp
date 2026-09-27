#include "internal.hpp"
#include <cstdio>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;} } while(false)
class NullContextStream final : public IDWriteFontFileStream {
  ULONG refs_=1;int& released_;
public:
  explicit NullContextStream(int& released):released_(released){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDWriteFontFileStream)){*out=static_cast<IDWriteFontFileStream*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
  ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
  ULONG STDMETHODCALLTYPE Release() override {const ULONG n=--refs_;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE ReadFileFragment(void const**,UINT64,UINT64,void**) override {return E_NOTIMPL;}
  void STDMETHODCALLTYPE ReleaseFileFragment(void*) override {++released_;}
  HRESULT STDMETHODCALLTYPE GetFileSize(UINT64*) override {return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64*) override {return E_NOTIMPL;}
};
int main(){
  int released=0;lt::ComPtr<NullContextStream> stream;stream.Attach(new NullContextStream(released));
  {lt::DWriteFragment fragment;fragment.stream=stream;}
  CHECK(released==1);

  CHECK(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));
  lt::ComPtr<IDWriteFactory> factory;
  CHECK(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))));
  lt::ComPtr<IDWriteFontCollection> collection;CHECK(SUCCEEDED(factory->GetSystemFontCollection(&collection)));
  UINT32 index=0;BOOL found=FALSE;CHECK(SUCCEEDED(collection->FindFamilyName(L"Microsoft YaHei UI",&index,&found))&&found);
  lt::ComPtr<IDWriteFontFamily> family;CHECK(SUCCEEDED(collection->GetFontFamily(index,&family)));
  for(auto weight:{DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_WEIGHT_BOLD}){
    lt::ComPtr<IDWriteFont> font;CHECK(SUCCEEDED(family->GetFirstMatchingFont(weight,DWRITE_FONT_STRETCH_NORMAL,DWRITE_FONT_STYLE_NORMAL,&font)));
    lt::ComPtr<IDWriteFontFace> face;CHECK(SUCCEEDED(font->CreateFontFace(&face)));
    std::shared_ptr<const lt::FontBlob> blob;
    {lt::FontBridge bridge;CHECK(bridge.get_blob(face.Get(),blob)==LT_OK);std::shared_ptr<const lt::FontBlob> again;CHECK(bridge.get_blob(face.Get(),again)==LT_OK&&again==blob);}
    CHECK(blob->owned.empty()&&blob->dwrite&&blob->dwrite->stream&&blob->data()&&blob->size()>1024*1024);
    UINT32 scalar=0x6d4b;UINT16 glyph=0;CHECK(SUCCEEDED(face->GetGlyphIndices(&scalar,1,&glyph))&&glyph);
    face.Reset();font.Reset();
    lt::GlyphKey key{};key.glyph_index=glyph;key.em_size_26_6=16*64;key.dpi_x=key.dpi_y=96;key.gamma_64=key.contrast_64=64;
    std::shared_ptr<const lt::GlyphBitmap> bitmap;CHECK(lt::Rasterizer::render(blob,key,bitmap)==LT_OK&&bitmap&&!bitmap->pixels.empty());
    std::printf("PASS font fragment survives bridge/face release, size=%zu, private font copy=%zu\n",blob->size(),blob->owned.size());
  }
  return 0;
}
