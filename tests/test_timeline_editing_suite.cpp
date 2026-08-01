#include "digitor/timeline_editing_suite.hpp"

#include <cstdlib>
#include <iostream>

using namespace digitor;
namespace {
void require(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; std::exit(1); } }
TimelineClipModel clip(std::string id, TimelineClipType type, std::int64_t start, std::int64_t duration, std::int64_t source = 0) {
  TimelineClipModel value; value.id=std::move(id); value.type=type; value.start_us=start; value.duration_us=duration; value.source_start_us=source; value.source_duration_us=20000000; return value;
}
const TimelineClipModel* find(const TimelineProjectModel& project, const std::string& id) {
  for (const auto& track : project.tracks) for (const auto& value : track.clips) if (value.id == id) return &value;
  return nullptr;
}
}

int main() {
  TimelineProjectModel project; project.fps=30;
  TimelineTrackModel video; video.id="v1"; video.name="Video 1"; video.type=TimelineTrackType::video;
  video.clips={clip("a",TimelineClipType::video,0,3000000),clip("b",TimelineClipType::video,3000000,3000000,3000000),clip("c",TimelineClipType::video,6000000,3000000,6000000)};
  TimelineTrackModel audio; audio.id="a1"; audio.name="Audio 1"; audio.type=TimelineTrackType::audio;
  auto music=clip("music",TimelineClipType::audio,0,9000000); audio.clips.push_back(music);
  project.tracks={video,audio};

  ProfessionalTimelineEditor editor(project);
  require(editor.validate(),"initial project invalid");
  require(editor.select_clip("b"),"selection failed");
  require(editor.add_marker({"m1",1500000,0,"Beat","cut here"}),"marker add failed");

  const auto roll=editor.roll_edit("a","b",3500000);
  require(roll.success,"roll edit failed");
  require(find(editor.project(),"a")->duration_us==3500000,"roll left duration wrong");
  require(find(editor.project(),"b")->start_us==3500000,"roll right start wrong");

  const auto slip=editor.slip_edit("b",250000);
  require(slip.success,"slip edit failed");
  require(find(editor.project(),"b")->source_start_us==3750000,"slip source wrong");

  editor.begin_transaction();
  const auto slide=editor.slide_edit("b",250000);
  require(slide.success,"slide edit failed");
  require(editor.commit_transaction(),"transaction commit failed");
  require(find(editor.project(),"b")->start_us==3750000,"slide start wrong");
  require(editor.undo(),"undo failed");
  require(find(editor.project(),"b")->start_us==3500000,"undo did not restore transaction");
  require(editor.redo(),"redo failed");
  require(find(editor.project(),"b")->start_us==3750000,"redo did not restore transaction");

  auto insert=clip("insert",TimelineClipType::video,3000000,500000);
  const auto inserted=editor.insert_clip("v1",insert,TimelineInsertMode::insert);
  require(inserted.success,"insert edit failed");
  require(find(editor.project(),"b")->start_us==4250000,"insert did not ripple clips");

  auto overwrite=clip("overwrite",TimelineClipType::video,4250000,500000);
  const auto overwritten=editor.insert_clip("v1",overwrite,TimelineInsertMode::overwrite);
  require(overwritten.success,"overwrite edit failed");
  require(find(editor.project(),"overwrite")!=nullptr,"overwrite clip missing");

  const auto deleted=editor.ripple_delete("insert",false);
  require(deleted.success,"ripple delete failed");
  require(find(editor.project(),"insert")==nullptr,"ripple delete retained clip");

  const auto compound=editor.make_compound({"a","overwrite"},"compound","v1");
  require(compound.has_value(),"compound creation failed");
  require(find(editor.project(),"compound")!=nullptr,"compound clip missing");
  require(editor.break_apart_compound(*compound,"compound","v1"),"compound break apart failed");
  require(find(editor.project(),"a")!=nullptr,"compound child not restored");

  TimelineProjectModel gap_project; gap_project.fps=30;
  TimelineTrackModel gap_track; gap_track.id="gv"; gap_track.type=TimelineTrackType::video;
  gap_track.clips={clip("g1",TimelineClipType::video,0,1000000),clip("g2",TimelineClipType::video,2000000,1000000)};
  gap_project.tracks.push_back(gap_track);
  ProfessionalTimelineEditor gap_editor(gap_project);
  const auto closed=gap_editor.close_gap("gv",1000000,2000000);
  require(closed.success,"gap close failed");
  require(find(gap_editor.project(),"g2")->start_us==1000000,"gap close position wrong");

  require(editor.validate(),"final project invalid");
  std::cout << "timeline editing suite qualification passed\n";
  return 0;
}
