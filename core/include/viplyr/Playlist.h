#pragma once
#include <string>
#include <vector>

namespace viplyr {

// An ordered list of media paths with a current item. Index -1 means "none".
class Playlist {
public:
    void assign(const std::vector<std::string> &paths);
    void append(const std::vector<std::string> &paths);
    // Removes an item. Returns true when the removed item was the current one;
    // the current index then points at the item that took its place (or -1).
    bool removeAt(int index);
    // Moves an item to a new position; the current item stays the current item.
    bool move(int from, int to);
    void clear();

    int count() const { return static_cast<int>(paths_.size()); }
    int currentIndex() const { return currentIndex_; }
    const std::string &at(int index) const { return paths_.at(index); }
    const std::vector<std::string> &paths() const { return paths_; }
    bool contains(const std::string &path) const;

    bool setCurrentIndex(int index);
    bool hasNext() const { return currentIndex_ + 1 < count(); }
    bool hasPrevious() const { return currentIndex_ > 0; }
    bool next();      // false (and no change) at the last item
    bool previous();  // false (and no change) at the first item

private:
    std::vector<std::string> paths_;
    int currentIndex_ = -1;
};

}  // namespace viplyr
