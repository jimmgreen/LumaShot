#include "model/mark_properties.h"
#include "ui/text_renderer.h"
#include "model/number_label.h"
#include "model/selection.h"
#include <cmath>

namespace lumashot {
namespace {
float Scale(float scale){return std::isfinite(scale)&&scale>0?scale:1.f;}
void Dimension(float& target,float dip,float scale){if(dip!=target/scale)target=dip*scale;}
}
ToolProperties PropertiesOfMark(const Mark& m,float dpi_scale,const ToolProperties& defaults){
    auto p=defaults;const float s=Scale(dpi_scale);
    switch(m.tool){
    case Tool::Rectangle:case Tool::Ellipse:
        p.color=m.color;p.width=m.width/s;p.line_style=m.line_style;p.fill_color=m.fill_color;p.fill_opacity=m.fill_opacity;
        if(m.tool==Tool::Rectangle)p.corner_radius=m.corner_radius/s;
        break;
    case Tool::Arrow:
        p.color=m.color;p.width=m.width/s;p.line_style=m.line_style;p.arrow_style=m.arrow_style;p.arrow_type=m.arrow_type;p.arrow_head=m.arrow_head;p.arrow_size=m.arrow_size/s;break;
    case Tool::Pen:
        p.pen_mode=m.pen_mode;p.pen_smoothing=m.pen_smoothing;p.pen_straight=m.pen_straight;p.pen_polyline=m.pen_polyline;
        if(m.pen_mode==PenMode::Highlighter){p.highlighter_color=m.color;p.highlighter_width=m.width/s;p.highlighter_opacity=m.pen_opacity;}
        else{p.color=m.color;p.width=m.width/s;p.pen_opacity=m.pen_opacity;}break;
    case Tool::Text:
        p.color=m.color;p.font_size=m.font_size/s;p.font_family=m.font_family;p.text_bold=m.text_bold;p.text_align=m.text_align;p.text_background=m.text_background;break;
    case Tool::Mosaic:
        p.mosaic_cell=m.mosaic_cell/s;p.mosaic_strength=(m.mosaic_cell/s-4.f)/.4f;p.mosaic_brush=m.mosaic_brush/s;p.mosaic_mode=m.mosaic_mode;p.mosaic_method=m.mosaic_method;break;
    case Tool::Number:
        p.number_label=m.number_label;p.color=m.color;p.number_size=m.number_size/s;p.number_shape=m.number_shape;p.number_combo=m.number_combo;p.number_text_color=m.number_text_color;p.number_text_preset=m.number_text_preset;p.number_text_size=m.number_text_size/s;break;
    case Tool::Select:break;
    }
    return p;
}
Mark WithMarkProperties(const Mark& source,const ToolProperties& p,float dpi_scale){
    auto m=source;const float s=Scale(dpi_scale);
    switch(m.tool){
    case Tool::Rectangle:case Tool::Ellipse:
        m.color=p.color;Dimension(m.width,p.width,s);m.line_style=p.line_style;m.fill_color=p.fill_color;m.fill_opacity=p.fill_opacity;
        if(m.tool==Tool::Rectangle&&p.corner_radius!=m.corner_radius/s){Dimension(m.corner_radius,p.corner_radius,s);m.corner_radii.reset();}break;
    case Tool::Arrow:
        m.color=p.color;Dimension(m.width,p.width,s);m.line_style=p.line_style;m.arrow_style=p.arrow_style;m.arrow_type=p.arrow_type;m.arrow_head=p.arrow_head;Dimension(m.arrow_size,p.arrow_size,s);break;
    case Tool::Pen:
        if(p.pen_straight&&!p.pen_polyline&&(!m.pen_straight||m.pen_polyline)&&!m.points.empty()){
            const auto old_center=MarkCenter(m);
            m.a=m.points.front();m.b=m.points.back();
            m.points=m.a==m.b?std::vector<Point>{m.a}:std::vector<Point>{m.a,m.b};
            const auto center=MarkCenter(m),rotated=RotatePoint(center,old_center,m.rotation);
            Translate(m,{rotated.x-center.x,rotated.y-center.y});
        }
        m.pen_straight=p.pen_straight||p.pen_polyline;m.pen_polyline=p.pen_polyline;m.pen_mode=p.pen_mode;m.pen_smoothing=p.pen_smoothing;
        if(p.pen_mode==PenMode::Highlighter){m.color=p.highlighter_color;Dimension(m.width,p.highlighter_width,s);m.pen_opacity=p.highlighter_opacity;}
        else{m.color=p.color;Dimension(m.width,p.width,s);m.pen_opacity=p.pen_opacity;}break;
    case Tool::Text:{
        const bool measure=p.font_size!=m.font_size/s||p.font_family!=m.font_family||p.text_bold!=m.text_bold||p.text_align!=m.text_align||p.text_background!=m.text_background;
        m.color=p.color;Dimension(m.font_size,p.font_size,s);m.font_family=p.font_family;m.text_bold=p.text_bold;m.text_align=p.text_align;m.text_background=p.text_background;
        if(measure&&m.text_auto_size)FitTextBounds(m);break;}
    case Tool::Mosaic:
        if(p.mosaic_strength!=(m.mosaic_cell/s-4.f)/.4f)m.mosaic_cell=(4.f+p.mosaic_strength*.4f)*s;
        Dimension(m.mosaic_brush,p.mosaic_brush,s);m.mosaic_mode=p.mosaic_mode;m.mosaic_method=p.mosaic_method;break;
    case Tool::Number:{
        const auto label=NormalizeNumberLabel(p.number_label);
        const bool resize=p.number_size!=m.number_size/s||p.number_shape!=m.number_shape||label!=m.number_label;
        const auto old_center=MarkCenter(m);m.number_label=label;
        m.color=p.color;Dimension(m.number_size,p.number_size,s);m.number_shape=p.number_shape;m.number_combo=p.number_combo;m.number_text_color=p.number_text_color;m.number_text_preset=p.number_text_preset;Dimension(m.number_text_size,p.number_text_size,s);
        if(resize){FitNumberBadge(m);ReconnectNumberBadge(m);const auto center=MarkCenter(m);const auto rotated=RotatePoint(center,old_center,m.rotation);Translate(m,{rotated.x-center.x,rotated.y-center.y});}break;}
    case Tool::Select:break;
    }
    return m;
}
}
