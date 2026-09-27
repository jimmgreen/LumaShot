#pragma once
#include "ui/render.h"
namespace lumashot {
Frame PinAnnotationPreview(const Frame& source,RECT display,RECT canvas);
Document PinAnnotationDisplayDocument(const Document&,const Frame& source,RECT display);
Document PinAnnotationDocument(const Document& display_document,RECT display,const Frame& source);
}
