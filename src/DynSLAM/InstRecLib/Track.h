

#ifndef INSTRECLIB_TRACK_H
#define INSTRECLIB_TRACK_H

#include <vector>
#include "InstanceSegmentationResult.h"
#include "InstanceView.h"
#include "../InfiniTamDriver.h"
#include "../Utils.h"
#include "../Defines.h"

namespace instreclib {
namespace reconstruction {

/// \brief One frame of an instance track (InstRecLib::Reconstruction::Track).
struct TrackFrame {
  int frame_idx;
  InstanceView instance_view;

  /// \brief The camera pose at the time when this frame was observed.
  Eigen::Matrix4f camera_pose;

  /// \brief The relative pose to the previous frame in the track, if it could be computed.
  dynslam::utils::Option<Eigen::Matrix4d> *relative_pose;

  TrackFrame(int frame_idx, const InstanceView& instance_view, const Eigen::Matrix4f camera_pose)
      : frame_idx(frame_idx), instance_view(instance_view), camera_pose(camera_pose) {}

  SUPPORT_EIGEN_FIELDS;
};

enum TrackState {
  kStatic,
  kDynamic,
  kUncertain
};

/// \brief A detected object's track through multiple frames.
/// Modeled as a series of detections, contained in the 'frames' field. Note that there can be
/// gaps in this list, due to frames where this particular object was not detected.
/// \todo In the long run, this class should be able to leverage a 3D reconstruction and something
///       like a Kalman Filter for motion tracking to predict an object's (e.g., car) pose in a
///       subsequent frame, in order to aid with tracking.
class Track {
 public:
  /// \brief The maximum number of frames with relative motion estimation failure before a static
  ///        object is reverted to the "Uncertain" state.
  int kMaxUncertainFramesStatic = 3;
  /// \see kMaxUncertainFramesStatic
  int kMaxUncertainFramesDynamic = 2;

  /// \brief Translation error threshold used to differentiate static from dynamic objects.
  float kTransErrorTreshold = 0.20f;

  Track(int id) : id_(id),
                  reconstruction_(nullptr),
                  needs_cleanup_(false),
                  track_state_(TrackState::kUncertain)
  {}

  virtual ~Track() {
    if (reconstruction_.get() != nullptr) {
      fprintf(stderr, "Deleting track [%d] and its associated reconstruction!\n", id_);
    }
  }

  /// \brief Updates the track's state, and, if applicable, populates the most recent relative pose.
  void Update(const Eigen::Matrix4f &egomotion,
              const instreclib::SparseSFProvider &ssf_provider,
              bool verbose);

  /// \brief Evaluates how well this new frame would fit the existing track.
  /// \returns A goodness score between 0 and 1, where 0 means the new frame would not match this
  /// track at all, and 1 would be a perfect match.
  float ScoreMatch(const TrackFrame& new_frame) const;

  void AddFrame(const TrackFrame& new_frame) { frames_.push_back(new_frame); }

  size_t GetSize() const { return frames_.size(); }

  TrackFrame& GetLastFrame() { return frames_.back(); }

  const TrackFrame& GetLastFrame() const { return frames_.back(); }

  int GetStartTime() const { return frames_.front().frame_idx; }

  int GetEndTime() const { return frames_.back().frame_idx; }

  const std::vector<TrackFrame>& GetFrames() const { return frames_; }

  const TrackFrame& GetFrame(int i) const { return frames_[i]; }
  TrackFrame& GetFrame(int i) { return frames_[i]; }

  int GetId() const { return id_; }

  std::string GetClassName() const {
    assert(frames_.size() > 0 || "Need at least one frame to determine a track's class.");
    return GetLastFrame().instance_view.GetInstanceDetection().GetClassName();
  }

  /// \brief Draws a visual representation of this feature track.
  /// \example For an object first seen in frame 11, then in frames 12, 13, and 16, this
  /// representation would look as follows:
  ///    [                                 11 12 13      16]
  std::string GetAsciiArt() const;

  bool HasReconstruction() const { return reconstruction_.get() != nullptr; }

  std::shared_ptr<dynslam::drivers::InfiniTamDriver>& GetReconstruction() {
    return reconstruction_;
  }

  const std::shared_ptr<dynslam::drivers::InfiniTamDriver>& GetReconstruction() const {
    return reconstruction_;
  }

  /// \brief Uses a series of goodness heuristics to establish whether the information contained in
  /// this track's frames is good enough for a 3D reconstruction.
  bool EligibleForReconstruction() const {
    // For now, use this simple heuristic: at least k frames in track.
    return GetSize() >= 6;
  }

  /// \brief Returns the relative pose of the specified frame w.r.t. the first one.
  dynslam::utils::Option<Eigen::Matrix4d> GetFramePose(size_t frame_idx) const;

  bool NeedsCleanup() const {
    return needs_cleanup_;
  }

  void SetNeedsCleanup(bool needs_cleanup) {
    this->needs_cleanup_ = needs_cleanup;
  }

  TrackState GetState() const {
    return track_state_;
  }

  string GetStateLabel() const {
    switch (track_state_) {
      case TrackState::kDynamic:
        return "Dynamic";
      case TrackState::kStatic:
        return "Static";
      case TrackState::kUncertain:
        return "Uncertain";
      default:
        throw runtime_error("Unsupported track type.");
    }
  }

  /// \brief Finds the first frame with a known relative pose, and returns the index of the frame
  ///        right before it.
  int GetFirstFusableFrameIndex() const {
    if (frames_.empty()) {
      return -1;
    }

    for (int i = 0; i < static_cast<int>(frames_.size()); ++i) {
      if (frames_[i].relative_pose->IsPresent()) {
        return max(0, i - 1);
      }
    }

    return -1;
  }

  void CountFusedFrame() {
    fused_frames_++;
  }

  void ReapReconstruction() {
    int reap_weight = max(1, min(5, static_cast<int>(0.33 * fused_frames_)));
    cout << "Reaping track with max weight [" << reap_weight << "]." << endl;
    reconstruction_->Reap(reap_weight);
  }

 private:
  /// \brief A unique identifier for this particular track.
  int id_;
  std::vector<TrackFrame> frames_;

  /// \brief A pointer to a 3D reconstruction of the object in this track.
  /// Is set to `nullptr` if no reconstruction is available.
  std::shared_ptr<dynslam::drivers::InfiniTamDriver> reconstruction_;

  /// \brief Whether the reconstruction is pending a full voxel decay iteration.
  bool needs_cleanup_;

  TrackState track_state_;

  // Used for the constant velocity assumption in tracking.
  int last_known_motion_time_ = -1;
  Eigen::Matrix4d last_known_motion_;

  /// \brief The number of frames fused in the reconstruction.
  int fused_frames_ = 0;

  SUPPORT_EIGEN_FIELDS;
};

}  // namespace reconstruction
}  // namespace instreclib

#endif  // INSTRECLIB_TRACK_H
