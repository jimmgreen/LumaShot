#pragma once
#include "capture/frame.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace lumashot {
// Pure scrolling-capture stitcher. Frames are same-sized captures of one
// screen region in physical pixels, taken after each scroll settles. It keeps
// only newly revealed rows, so memory grows with the result, not the frames:
// content lives in fixed row blocks (no reallocation spikes) and each undo
// step stores only the rows of the previous frame that the content cannot
// reproduce.
struct StitchOptions {
    int guard{24};          // right-edge columns ignored while matching (scrollbars)
    int max_height{30000};  // composed image height limit in pixels
    double min_match{.82};  // fraction of overlapping rows that must agree exactly
    int min_overlap{24};    // rows that must overlap between neighbouring frames
    int history{32};        // undo depth (each entry keeps a delta of one previous frame)
};
enum class StitchStatus { First, Appended, Unchanged, NoOverlap, LowMatch, Limit, Mismatch };
struct StitchResult {
    StitchStatus status{StitchStatus::Mismatch};
    int shift{};      // content movement in rows between the previous and this frame
    double match{};   // agreement of overlapping rows, 0..1
    int appended{};   // rows added to the composed image
};

struct RowSignatures {
    std::vector<uint64_t> hash;
    std::vector<uint8_t> uniform;
    std::vector<std::array<uint8_t,8>> profile; // luminance means of 8 column bins
};
RowSignatures SignRows(const Frame& frame,int guard);
// Fraction of rows identical at the same position. Rows blank in both frames
// are ignored unless every row is blank.
double SamePositionRatio(const RowSignatures& a,const RowSignatures& b);

class Stitcher {
public:
    explicit Stitcher(StitchOptions options={});
    // hint: expected shift in rows (0 = use the previous shift). Only used to
    // pick between equally good candidates on blank or periodic content.
    StitchResult Add(const Frame& frame,int hint=0);
    // After LowMatch the frame is held; the user can accept the best candidate.
    StitchResult AcceptPending();
    void DropPending(){pending_.reset();}
    bool HasPending()const{return pending_.has_value();}
    int PendingShift()const{return pending_?pending_->shift:0;}
    bool Undo();
    bool CanUndo()const{return !history_.empty();}
    bool Started()const{return width_>0;}
    int Width()const{return width_;}
    int FrameHeight()const{return frame_height_;}
    int Height()const{return content_rows_+std::max(footer_,0);}
    int Segments()const{return Started()?int(seams_.size())+1:0;}
    // Changes whenever rows already in the content are rewritten (the first
    // frame refreshed by a late repaint); appends and undo keep it.
    int ContentEpoch()const{return epoch_;}
    int HeaderRows()const{return header_;}
    int FooterRows()const{return std::max(footer_,0);}
    int ScrollbarColumns()const{return scrollbar_;}
    // Rows of a frame that can overlap with the next one (excludes fixed bars).
    int BandRows()const;
    int LastShift()const{return last_shift_;}
    // Top row of the current frame's scrolling band inside the composed image.
    int ViewportTop()const;
    const std::vector<int>& Seams()const{return seams_;}
    // Composed row y (content, then footer) with full frame width.
    const uint32_t* Row(int y)const;
    Frame Compose(bool crop_scrollbar=true)const&;
    // Consuming compose: releases content blocks while copying, so the peak
    // stays near one image instead of two. Afterwards only Seams(),
    // HeaderRows(), FooterRows() and ScrollbarColumns() remain meaningful.
    Frame Compose(bool crop_scrollbar=true)&&;
    // Bytes held by the undo history (for tests and diagnostics).
    size_t HistoryBytes()const;
    const StitchOptions& Options()const{return options_;}
private:
    struct Estimate {int shift{};double match{};bool fuzzy{};int top{},bottom{},votes{};};
    struct Pending {Frame frame;RowSignatures signatures;int shift{},top{},bottom{};double match{};};
    // Frame row y equals content row ref (-1: none) except columns [x0,x1),
    // which are stored in History::patch in row order.
    struct Line {int ref{-1};int x0{},x1{};};
    struct History {int content_rows{};int header{};int shift{};RECT bounds{};std::vector<Line> lines;std::vector<uint32_t> patch;};
    Estimate Find(const RowSignatures& f,int top,int bottom,int hint)const;
    int ProfileFixedRows(const RowSignatures& f,bool from_top)const;
    double MatchRatio(const RowSignatures& f,int shift,int top,int bottom,int* informative=nullptr)const;
    StitchResult Append(Frame frame,RowSignatures signatures,int shift,int top,int bottom);
    void DetectScrollbar(const Frame& frame,int shift,int top,int bottom);
    History Delta(int previous_header)const;
    Frame Restore(const History& entry)const;
    uint32_t* ContentRow(int y){return blocks_[static_cast<size_t>(y/BlockRows)].get()+static_cast<size_t>(y%BlockRows)*width_;}
    const uint32_t* ContentRow(int y)const{return blocks_[static_cast<size_t>(y/BlockRows)].get()+static_cast<size_t>(y%BlockRows)*width_;}
    void WriteContent(int y,const uint32_t* source,int rows);
    void TruncateContent(int rows);
    static constexpr int BlockRows=512;
    StitchOptions options_;
    int width_{},frame_height_{},guard_{};
    std::vector<std::unique_ptr<uint32_t[]>> blocks_;
    int content_rows_{};
    int footer_{-1},header_{},scrollbar_{},last_shift_{},epoch_{};
    Frame last_;RowSignatures last_signatures_;
    std::vector<int> seams_;
    std::deque<History> history_;
    std::optional<Pending> pending_;
};
}
