#pragma once
#include <ILexer.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
namespace lumashot::clipboard {
// Read-only UTF-8 document adapter. No editor control or UI dependencies.
class LexDocument final:public Scintilla::IDocument {
    std::vector<Sci_Position> lines_{0};std::vector<int> states_,levels_;Sci_Position styled_{};
public:
    std::string text;std::vector<char> styles;
    explicit LexDocument(std::string value):text(std::move(value)),styles(text.size()+1){
        for(size_t i=0;i<text.size();++i)if(text[i]=='\n'||(text[i]=='\r'&&(i+1==text.size()||text[i+1]!='\n')))lines_.push_back(static_cast<Sci_Position>(i+1));
        states_.resize(lines_.size());levels_.resize(lines_.size(),0x400);
    }
    int SCI_METHOD Version()const override{return Scintilla::dvRelease4;}
    void SCI_METHOD SetErrorStatus(int)override{}
    Sci_Position SCI_METHOD Length()const override{return static_cast<Sci_Position>(text.size());}
    void SCI_METHOD GetCharRange(char* out,Sci_Position pos,Sci_Position length)const override{if(pos>=0&&length>=0&&pos+length<=Length())memcpy(out,text.data()+pos,static_cast<size_t>(length));}
    char SCI_METHOD StyleAt(Sci_Position p)const override{return p>=0&&p<Length()?styles[static_cast<size_t>(p)]:0;}
    Sci_Position SCI_METHOD LineFromPosition(Sci_Position p)const override{return std::max<Sci_Position>(0,std::upper_bound(lines_.begin(),lines_.end(),p)-lines_.begin()-1);}
    Sci_Position SCI_METHOD LineStart(Sci_Position line)const override{return line<0?0:line>=static_cast<Sci_Position>(lines_.size())?Length():lines_[static_cast<size_t>(line)];}
    int SCI_METHOD GetLevel(Sci_Position line)const override{return line>=0&&line<static_cast<Sci_Position>(levels_.size())?levels_[static_cast<size_t>(line)]:0x400;}
    int SCI_METHOD SetLevel(Sci_Position line,int value)override{const int old=GetLevel(line);if(line>=0&&line<static_cast<Sci_Position>(levels_.size()))levels_[static_cast<size_t>(line)]=value;return old;}
    int SCI_METHOD GetLineState(Sci_Position line)const override{return line>=0&&line<static_cast<Sci_Position>(states_.size())?states_[static_cast<size_t>(line)]:0;}
    int SCI_METHOD SetLineState(Sci_Position line,int value)override{const int old=GetLineState(line);if(line>=0&&line<static_cast<Sci_Position>(states_.size()))states_[static_cast<size_t>(line)]=value;return old;}
    void SCI_METHOD StartStyling(Sci_Position p)override{styled_=p;}
    bool SCI_METHOD SetStyleFor(Sci_Position length,char style)override{if(length<0||styled_<0||styled_+length>Length())return false;std::fill_n(styles.begin()+styled_,length,style);styled_+=length;return true;}
    bool SCI_METHOD SetStyles(Sci_Position length,const char* source)override{if(length<0||styled_<0||styled_+length>Length())return false;std::copy_n(source,length,styles.begin()+styled_);styled_+=length;return true;}
    void SCI_METHOD DecorationSetCurrentIndicator(int)override{}
    void SCI_METHOD DecorationFillRange(Sci_Position,int,Sci_Position)override{}
    void SCI_METHOD ChangeLexerState(Sci_Position,Sci_Position)override{}
    int SCI_METHOD CodePage()const override{return 65001;}
    bool SCI_METHOD IsDBCSLeadByte(char)const override{return false;}
    const char* SCI_METHOD BufferPointer()override{return text.data();}
    int SCI_METHOD GetLineIndentation(Sci_Position line)override{int column=0;for(auto i=LineStart(line);i<Length();++i){if(text[static_cast<size_t>(i)]==' ')++column;else if(text[static_cast<size_t>(i)]=='\t')column=(column/8+1)*8;else break;}return column;}
    Sci_Position SCI_METHOD LineEnd(Sci_Position line)const override{auto end=LineStart(line+1);while(end>LineStart(line)&&(text[static_cast<size_t>(end-1)]=='\n'||text[static_cast<size_t>(end-1)]=='\r'))--end;return end;}
    Sci_Position SCI_METHOD GetRelativePosition(Sci_Position p,Sci_Position offset)const override{while(offset>0&&p<Length()){Sci_Position width{};GetCharacterAndWidth(p,&width);p+=width;--offset;}while(offset<0&&p>0){--p;while(p>0&&(static_cast<unsigned char>(text[static_cast<size_t>(p)])&0xc0)==0x80)--p;++offset;}return p;}
    int SCI_METHOD GetCharacterAndWidth(Sci_Position p,Sci_Position* width)const override{
        if(width)*width=1;if(p<0||p>=Length())return 0;const auto c=static_cast<unsigned char>(text[static_cast<size_t>(p)]);
        const int n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;if(p+n>Length())return c;
        int value=c&(n==1?0x7f:n==2?0x1f:n==3?0xf:7);for(int i=1;i<n;++i)value=(value<<6)|(static_cast<unsigned char>(text[static_cast<size_t>(p+i)])&63);if(width)*width=n;return value;
    }
};
}
