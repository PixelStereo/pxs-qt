#pragma once

#include "Anim.h"
#include <pxs/core/tree/tree.h>
#include <functional>
#include <set>

namespace pxs::timeline {

// Named, collision-free address segments for a tree of timelines and their tracks.
std::string claimSegment(std::string_view label, std::set<std::string> &used);

void nameTracks(Animation &animation, const std::set<std::string> &reserved = {});

// Declares the track parameters of a timeline. The callbacks read and replace the owning tracks.
std::vector<std::string> declareTrackTree(pxs::core::tree::Tree &tree, const std::string &path,
    const Animation &layout, const std::function<const Animation *()> &read,
    const std::function<void(size_t, AnimTrack)> &write,
    const std::function<double(size_t)> &output);

bool sameTrackTree(const Animation &left, const Animation &right);
}
