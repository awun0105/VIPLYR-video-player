#include "viplyr/Playlist.h"

#include <algorithm>

namespace viplyr {

void Playlist::assign(const std::vector<std::string> &paths) {
    paths_ = paths;
    currentIndex_ = paths_.empty() ? -1 : 0;
}

void Playlist::append(const std::vector<std::string> &paths) {
    paths_.insert(paths_.end(), paths.begin(), paths.end());
    if (currentIndex_ < 0 && !paths_.empty()) currentIndex_ = 0;
}

bool Playlist::removeAt(int index) {
    if (index < 0 || index >= count()) return false;
    paths_.erase(paths_.begin() + index);

    const bool removedCurrent = index == currentIndex_;
    if (index < currentIndex_) --currentIndex_;
    if (currentIndex_ >= count()) currentIndex_ = count() - 1;  // -1 when now empty
    return removedCurrent;
}

bool Playlist::move(int from, int to) {
    if (from < 0 || from >= count() || to < 0 || to >= count()) return false;
    if (from == to) return true;

    const bool movingCurrent = from == currentIndex_;
    std::string moved = std::move(paths_[from]);
    paths_.erase(paths_.begin() + from);
    paths_.insert(paths_.begin() + to, std::move(moved));

    if (movingCurrent) currentIndex_ = to;
    else if (from < currentIndex_ && to >= currentIndex_) --currentIndex_;
    else if (from > currentIndex_ && to <= currentIndex_) ++currentIndex_;
    return true;
}

void Playlist::clear() {
    paths_.clear();
    currentIndex_ = -1;
}

bool Playlist::contains(const std::string &path) const {
    return std::find(paths_.begin(), paths_.end(), path) != paths_.end();
}

bool Playlist::setCurrentIndex(int index) {
    if (index < 0 || index >= count()) return false;
    currentIndex_ = index;
    return true;
}

bool Playlist::next() {
    if (!hasNext()) return false;
    ++currentIndex_;
    return true;
}

bool Playlist::previous() {
    if (!hasPrevious()) return false;
    --currentIndex_;
    return true;
}

}  // namespace viplyr
