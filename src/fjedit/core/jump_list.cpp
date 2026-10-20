#include "fjedit/core/jump_list.hpp"

#include <algorithm>
#include <utility>

namespace fjedit {

namespace {

// `at`, a place in `document`'s text as it was at `version`, where it is now.
Position caught_up(const Document& document, Position at, uint64_t version) {
    if (const std::optional<std::span<const Edit>> edits = document.edits_since(version)) {
        for (const Edit& edit : *edits) at = map(edit, at, Bias::Before);
    }
    return document.buffer().clamp(at);
}

} // namespace

void JumpList::jumped(Place from) {
    places_.push_back(std::move(from));
    if (places_.size() > SIZE) places_.erase(places_.begin());
    at_ = places_.size();
}

std::optional<JumpList::Place> JumpList::step(long count, const Place& here) {
    drop_repeats();
    const auto reaches = [&](long index) { return index >= 0 && index < static_cast<long>(places_.size()); };
    long to = static_cast<long>(at_) + count;
    if (!reaches(to)) return std::nullopt;
    if (at_ == places_.size()) {
        // The first step back from the end keeps where it set out from.
        jumped(here);
        at_ = places_.size() - 1;
        to = static_cast<long>(at_) + count;
        if (!reaches(to)) return std::nullopt;
    }
    at_ = static_cast<std::size_t>(to);
    return places_[at_];
}

void JumpList::catch_up(const Workspace& workspace) {
    for (Place& place : places_) {
        if (!place.version) continue;
        const Document* document = workspace.find(place.path);
        if (document == nullptr) continue;
        place.at = caught_up(*document, place.at, *place.version);
        place.version = document->version();
    }
}

void JumpList::closed(const std::filesystem::path& path, const Document& document) {
    for (Place& place : places_) {
        if (place.path != path) continue;
        if (place.version) place.at = caught_up(document, place.at, *place.version);
        place.version.reset();
    }
}

void JumpList::opened(const std::filesystem::path& path, uint64_t version) {
    for (Place& place : places_) {
        if (place.path == path && !place.version) place.version = version;
    }
}

void JumpList::drop_repeats() {
    std::vector<Place> kept;
    std::size_t at = 0;
    for (std::size_t i = 0; i < places_.size(); ++i) {
        if (i == at_) at = kept.size();
        const auto after = places_.begin() + static_cast<std::ptrdiff_t>(i) + 1;
        const bool newer = std::any_of(after, places_.end(), [&](const Place& later) {
            return later.path == places_[i].path && later.at.line == places_[i].at.line;
        });
        if (!newer) kept.push_back(places_[i]);
    }
    if (at_ == places_.size()) at = kept.size();
    places_ = std::move(kept);
    at_ = at;
}

} // namespace fjedit
