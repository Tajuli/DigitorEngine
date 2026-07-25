#pragma once
#include "digitor/media.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace digitor {
using ClipId=std::uint64_t;
struct Keyframe { FrameNumber frame{}; double value{}; };
struct Clip { ClipId id{}; std::string source; FrameNumber start{},duration{},source_in{}; std::vector<Keyframe> keyframes; FrameNumber end()const{return start+duration;} };
struct Track { std::string name; std::vector<Clip> clips; };
enum class EditMode { overwrite, insert };
class Timeline {
public:
 explicit Timeline(Rational rate={1,30}):rate_(rate){}
 std::size_t add_track(std::string name={}); ClipId add_clip(std::size_t,const std::string&,FrameNumber,FrameNumber,FrameNumber=0,EditMode=EditMode::overwrite);
 bool erase(ClipId,bool ripple=false); bool move(ClipId,std::size_t,FrameNumber,EditMode=EditMode::overwrite);
 bool ripple(ClipId,FrameNumber); bool roll(ClipId left,ClipId right,FrameNumber delta); bool slip(ClipId,FrameNumber); bool slide(ClipId,FrameNumber);
 bool set_keyframe(ClipId,FrameNumber,double); std::optional<double> value_at(ClipId,FrameNumber)const;
 bool undo(); bool redo(); const std::vector<Track>& tracks()const{return tracks_;} Rational frame_rate()const{return rate_;}
 const Clip* find(ClipId)const;
private:
 struct State{std::vector<Track> tracks;}; void checkpoint(); Clip* find_mut(ClipId); void resolve_overwrite(std::size_t,ClipId,FrameNumber,FrameNumber); void sort(std::size_t);
 Rational rate_; std::vector<Track>tracks_;std::vector<State>undo_,redo_;ClipId next_id_{1};
};
}
